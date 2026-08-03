// TODO: REMOVE AS SOON AS WE HAVE LATEST GCC COMPILER READY, THIS IS ALL NATIVE FUNCTIONALITY IN std SINCE C++ 17
#pragma once

#if defined(__GNUC__) && (__GNUC__ == 12)

#include <iostream>
#include <string>
#include <string_view>
#include <sstream>
#include <tuple>
#include <array>
#include <stdexcept>
#include <cctype>
// For std::chrono
#include <chrono>
#include <ostream>
#include <cstdio>
#include <cstddef>


namespace std {
    // ============ std::format simulation ===================
    // Base case: No more arguments to format.
    // Returns the remainder of the format string.
    // Convert one argument to string
    template <typename T>
    std::string to_string_format(const T& value) {
        std::ostringstream ss;
        ss << value;
        return ss.str();
    }

    // std::format-like simulation supporting:
    //   "{}"      automatic argument order
    //   "{0}"     indexed argument
    //   "{1}"     indexed argument
    template <typename... Args>
    std::string format(std::string_view fmt, const Args&... args) {
        std::array<std::string, sizeof...(Args)> values = {
            to_string_format(args)...
        };

        std::string result;
        size_t auto_index = 0;

        for (size_t i = 0; i < fmt.size(); ++i) {
            if (fmt[i] != '{') {
                result += fmt[i];
                continue;
            }

            size_t end = fmt.find('}', i);
            if (end == std::string_view::npos) {
                throw std::runtime_error("Unmatched '{' in format string");
            }

            std::string_view inside = fmt.substr(i + 1, end - i - 1);

            size_t index = 0;

            if (inside.empty()) {
                // "{}"
                index = auto_index++;
            }
            else {
                // "{0}", "{1}", ...
                for (char c : inside) {
                    if (!std::isdigit(static_cast<unsigned char>(c))) {
                        throw std::runtime_error("Invalid format placeholder");
                    }
                    index = index * 10 + (c - '0');
                }
            }

            if (index >= values.size()) {
                throw std::runtime_error("Format argument index out of range");
            }

            result += values[index];
            i = end;
        }

        return result;
    }

    // Base case: no arguments
    inline std::string format(std::string_view fmt) {
        return std::string(fmt);
    }

    // ============ std::format_string simulation ===================
    template <class... Args>
    struct format_string {
        constexpr format_string(const char* s) : str(s) {}
        constexpr format_string(std::string_view s) : str(s) {}

        std::string_view str;
    };

    // ============ CHRONO SIMULATION ============
    //
    // timestamp_io.hpp — serialization helpers for
    //     std::chrono::sys_time<std::chrono::microseconds>
    // that work on GCC 12 (which has neither <format> nor chrono's std::formatter
    // /operator<<). Only the C++20 calendar types (year_month_day, hh_mm_ss,
    // sys_days), present since GCC 11, are used.
    //

    namespace chrono {

        using Timestamp = std::chrono::sys_time<std::chrono::microseconds>;

        // Renders an instant as UTC "YYYY-MM-DD HH:MM:SS.ffffff" (always 6 fractional
        // digits, no zone suffix) into `buf`, with snprintf() semantics: at most `cap`
        // bytes are written including the terminating NUL; the return value is the
        // number of characters that the full text would occupy (excluding the NUL).
        inline int format_utc(char* buf, std::size_t cap, Timestamp tp) noexcept
        {
            using namespace std::chrono;
            sys_days               const dp{ floor<days>(tp) };      // midnight of that day
            year_month_day         const ymd{ dp };                  // calendar date
            hh_mm_ss<microseconds> const tod{ tp - dp };             // time within the day

            return std::snprintf(
                buf, cap,
                "%04d-%02u-%02u %02lld:%02lld:%02lld.%06lld",
                static_cast<int>(ymd.year()),
                static_cast<unsigned>(ymd.month()),
                static_cast<unsigned>(ymd.day()),
                static_cast<long long>(tod.hours().count()),
                static_cast<long long>(tod.minutes().count()),
                static_cast<long long>(tod.seconds().count()),
                static_cast<long long>(tod.subseconds().count()));
        }

        // "YYYY-MM-DD HH:MM:SS.ffffff" as a std::string.
        inline std::string to_string(Timestamp tp)
        {
            char buf[32];
            int const n = format_utc(buf, sizeof buf, tp);
            return (n > 0) ? std::string(buf, static_cast<std::size_t>(n)) : std::string{};
        }

        } // namespace chrono_io

        // Stream operator. On GCC 12 the standard library provides no operator<< for
        // chrono time points, so we supply one in the global namespace; ordinary
        // (non-ADL) lookup finds it wherever the header is included. It is compiled
        // ONLY when <format> is absent, so on GCC 13+ the standard library's own
        // operator<< is used instead and there is no ambiguity.
        #if !defined(__cpp_lib_format)
        inline std::ostream&
        operator<<(std::ostream& os, std::chrono::sys_time<std::chrono::microseconds> tp)
        {
            char buf[32];
            int const n = chrono::format_utc(buf, sizeof buf, tp);
            if (n > 0) os.write(buf, n);
            return os;
        }
        #endif

    };

#endif
