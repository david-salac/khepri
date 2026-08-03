#pragma once
#ifdef PGSQL
#if defined(PQXX_VERSION_MAJOR) && (PQXX_VERSION_MAJOR >= 7)

/**
* This file modifies behaviour of some components of PostgreSQL driver.
*   This is mostly for compatibility's sake.
*/

#include <algorithm>
#include <cstddef>
#include <chrono>
#if defined(__GNUC__) && (__GNUC__ == 12)
// To support legacy GNU compiler (version 12), should be removed with version 14
#include "legacy_utils.hpp"
#else
#include <format>
#endif
#include <string>
#include <string_view>

#include <pqxx/pqxx>
#include <pqxx/version>   // defines PQXX_VERSION_MAJOR / _MINOR / PQXX_VERSION

// field_ref / row_ref were introduced in libpqxx 8.0. Where they exist, use
// them; otherwise alias them to the pre-8.0 row / field types.
#if defined(PQXX_VERSION_MAJOR) && (PQXX_VERSION_MAJOR >= 8)
#  define PQXXCOMPAT_HAS_REFS 1
#else
#  define PQXXCOMPAT_HAS_REFS 0
#endif

namespace pqxx {
#if PQXXCOMPAT_HAS_REFS
    // Nothing to do
#else
    using field_ref = pqxx::field;   // fallback
    using row_ref = pqxx::row;     // fallback
#endif


    using Timestamp = std::chrono::sys_time<std::chrono::microseconds>;

    // sys_time has no SQL null of its own; reading a NULL throws (use .get<>() for
    // an optional instead).
    template<> struct nullness<Timestamp>
    {
        static constexpr bool has_null{ false };
        static constexpr bool always_null{ false };
        static bool is_null(Timestamp const&) noexcept { return false; }
        [[noreturn]] static Timestamp null()
        {
            throw pqxx::usage_error{ "Tried to read a null into a Timestamp." };
        }
    };

    template<> struct string_traits<Timestamp>
    {
        // ---------- Reading: row["created_at"].as<Timestamp>() ----------
        //
        // Parses PostgreSQL's ISO output, e.g.
        //     timestamp     :  2024-01-15 13:45:30.123456
        //     timestamptz   :  2024-01-15 13:45:30.123456+00
        //                      2024-01-15 19:15:30+05:30
        // The optional zone offset is parsed and normalised to UTC, so the result
        // is always a correct UTC instant for `timestamptz` columns.
        static Timestamp from_string(std::string_view text)
        {
            using namespace std::chrono;

            if (text == "infinity") return Timestamp::max();
            if (text == "-infinity") return Timestamp::min();

            char const* p = text.data();
            char const* const end = p + text.size();

            auto fail = [&] {
                throw pqxx::conversion_error{
                  "Could not parse timestamp from '" + std::string{text} + "'." };
                };

            auto read_uint = [&](int& out) {
                char const* const start = p;
                long v = 0;
                while (p != end && *p >= '0' && *p <= '9')
                {
                    v = v * 10 + (*p - '0');
                    ++p;
                }
                if (p == start) fail();
                out = static_cast<int>(v);
                };

            auto expect = [&](char c) {
                if (p == end || *p != c) fail();
                ++p;
                };

            int Y{}, Mo{}, D{}, h{}, mi{}, s{};
            read_uint(Y); expect('-');
            read_uint(Mo); expect('-');
            read_uint(D);

            if (p == end || (*p != ' ' && *p != 'T')) fail();
            ++p;

            read_uint(h); expect(':');
            read_uint(mi); expect(':');
            read_uint(s);

            // Fractional seconds (PostgreSQL emits up to 6 digits; extra are truncated).
            microseconds frac{ 0 };
            if (p != end && *p == '.')
            {
                ++p;
                long long f = 0;
                int digits = 0;
                while (p != end && *p >= '0' && *p <= '9')
                {
                    if (digits < 6) { f = f * 10 + (*p - '0'); ++digits; }
                    ++p;
                }
                while (digits++ < 6) f *= 10;  // scale to microseconds
                frac = microseconds{ f };
            }

            // Optional numeric zone offset: (+|-)HH[:MM[:SS]]  (absent for `timestamp`).
            seconds offset{ 0 };
            if (p != end && (*p == '+' || *p == '-'))
            {
                int const sign = (*p == '-') ? -1 : 1;
                ++p;
                int oh{}, om{ 0 }, os{ 0 };
                read_uint(oh);
                if (p != end && *p == ':') { ++p; read_uint(om); }
                if (p != end && *p == ':') { ++p; read_uint(os); }
                offset = sign * (hours{ oh } + minutes{ om } + seconds{ os });
            }

            auto const ymd = year{ Y } / month{ static_cast<unsigned>(Mo) } /
                day{ static_cast<unsigned>(D) };
            if (!ymd.ok()) fail();

            sys_days const date{ ymd };
            auto tp = sys_time<microseconds>{
              date.time_since_epoch() + hours{h} + minutes{mi} + seconds{s} + frac };

            tp -= offset;  // wall-clock-with-offset -> UTC
            return tp;
        }

        // ---------- Writing: pass a Timestamp as a query parameter ----------
        static zview to_buf(char* begin, char* end, Timestamp const& value)
        {
            char* const stop = into_buf(begin, end, value);
            return zview{ begin, static_cast<std::size_t>(stop - begin - 1) };
        }

        static char* into_buf(char* begin, char* end, Timestamp const& value)
        {
            // "+00" marks the literal explicitly as UTC for PostgreSQL.
            std::string const text = std::format("{:%Y-%m-%d %H:%M:%S}+00", value);
            if (static_cast<std::ptrdiff_t>(text.size()) + 1 > end - begin)
                throw pqxx::conversion_overrun{ "Buffer too small for Timestamp." };
            char* out = std::copy(text.begin(), text.end(), begin);
            *out++ = '\0';
            return out;
        }

        static std::size_t size_buffer(Timestamp const&) noexcept
        {
            // "YYYY-MM-DD HH:MM:SS.ffffff+00" + '\0' = 30 bytes; round up.
            return 40u;
        }
    };
} // namespace pqxx
#endif  // CLOSES: #if defined(PQXX_VERSION_MAJOR) && (PQXX_VERSION_MAJOR >= 7)

#if defined(PQXX_VERSION_MAJOR) && (PQXX_VERSION_MAJOR == 6)

/* 
 * 
 * ====================================================================
 * TODO: Remove the following once the version of PostgreSQL is updated(with a new Debian version)
 * ====================================================================
 * 
 */

 // libpqxx 6.x (6.4) string_traits for sys_time<microseconds>.
 // NOTE: 6.x has NO nullness<>, NO zview, NO into_buf/to_buf, NO conversion_overrun.
 //       The interface is: name()/has_null()/is_null()/null()/from_string(c-str,&)/to_string().
#include <chrono>
#include "legacy_utils.hpp"          // chrono::format_utc / to_string
#include <pqxx/strconv>              // string_traits primary template
#include <pqxx/except>               // conversion_error, usage_error
#include <chrono>
#include <string>
#include <string_view>

namespace pqxx {
    using field_ref = pqxx::field;   // fallback
    using row_ref = pqxx::row;     // fallback

    using Timestamp = std::chrono::sys_time<std::chrono::microseconds>;

    namespace detail {
        inline Timestamp parse_timestamp(std::string_view text)
        {
            using namespace std::chrono;
            if (text == "infinity")  return Timestamp::max();
            if (text == "-infinity") return Timestamp::min();

            char const* p = text.data();
            char const* const end = p + text.size();
            auto fail = [&] { throw pqxx::conversion_error{
                "Could not parse timestamp from '" + std::string{text} + "'." }; };
            auto read_uint = [&](int& out) {
                char const* const start = p; long v = 0;
                while (p != end && *p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); ++p; }
                if (p == start) fail();
                out = static_cast<int>(v); };
            auto expect = [&](char c) { if (p == end || *p != c) fail(); ++p; };

            int Y{}, Mo{}, D{}, h{}, mi{}, s{};
            read_uint(Y);  expect('-'); read_uint(Mo); expect('-'); read_uint(D);
            if (p == end || (*p != ' ' && *p != 'T')) fail();
            ++p;
            read_uint(h);  expect(':'); read_uint(mi); expect(':'); read_uint(s);

            microseconds frac{ 0 };
            if (p != end && *p == '.') {
                ++p; long long f = 0; int digits = 0;
                while (p != end && *p >= '0' && *p <= '9') {
                    if (digits < 6) { f = f * 10 + (*p - '0'); ++digits; }
                    ++p;
                }
                while (digits++ < 6) f *= 10;
                frac = microseconds{ f };
            }

            seconds offset{ 0 };
            if (p != end && (*p == '+' || *p == '-')) {
                int const sign = (*p == '-') ? -1 : 1; ++p;
                int oh{}, om{ 0 }, os{ 0 };
                read_uint(oh);
                if (p != end && *p == ':') { ++p; read_uint(om); }
                if (p != end && *p == ':') { ++p; read_uint(os); }
                offset = sign * (hours{ oh } + minutes{ om } + seconds{ os });
            }

            auto const ymd = year{ Y } / month{ static_cast<unsigned>(Mo) } / day{ static_cast<unsigned>(D) };
            if (!ymd.ok()) fail();
            sys_days const date{ ymd };
            auto tp = sys_time<microseconds>{
                date.time_since_epoch() + hours{h} + minutes{mi} + seconds{s} + frac };
            tp -= offset;
            return tp;
        }
    } // namespace detail

    template<> struct string_traits<Timestamp>
    {
        static constexpr const char* name() noexcept { return "Timestamp"; }
        static constexpr bool has_null() noexcept { return false; }
        static bool is_null(Timestamp const&) noexcept { return false; }
        [[noreturn]] static Timestamp null()
        {
            throw pqxx::usage_error{ "Tried to read a null into a Timestamp." };
        }

        // 6.x READ interface: (const char*, T&) -> void
        static void from_string(char const Str[], Timestamp& Obj)
        {
            Obj = detail::parse_timestamp(std::string_view{ Str });
        }

        // 6.x WRITE interface: -> std::string  ("+00" marks the literal as UTC)
        static std::string to_string(Timestamp const& Obj)
        {
            return std::chrono::to_string(Obj) + "+00";
        }
    };

} // namespace pqxx

#endif  // CLOSES: #if defined(PQXX_VERSION_MAJOR) && (PQXX_VERSION_MAJOR == 6)

#endif  // CLOSES: #ifdef PGSQL
