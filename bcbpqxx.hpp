#pragma once
#ifdef INGRESSQL

#include <chrono>
#if defined(__GNUC__) && (__GNUC__ == 12)
// To support legacy GNU compiler (version 12), should be removed with version 14
#include "legacy_utils.hpp"
#else
#endif
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include <array>
#include <algorithm>

namespace bcbpqxx {

    class usage_error : public std::logic_error {
    public:
        using std::logic_error::logic_error;
    };

    template<class T> struct is_optional : std::false_type {};
    template<class T> struct is_optional<std::optional<T>> : std::true_type {};


    template<class T> struct is_system_time_point : std::false_type {};
    template<class Duration>
    struct is_system_time_point<std::chrono::time_point<std::chrono::system_clock, Duration>>
        : std::true_type {
    };

    namespace detail {

        inline bool is_hex_digit(char ch) noexcept
        {
            const auto c = static_cast<unsigned char>(ch);
            return std::isdigit(c) != 0 ||
                (c >= 'a' && c <= 'f') ||
                (c >= 'A' && c <= 'F');
        }

        inline unsigned hex_digit_value(char ch)
        {
            if (ch >= '0' && ch <= '9')
                return static_cast<unsigned>(ch - '0');

            if (ch >= 'a' && ch <= 'f')
                return static_cast<unsigned>(ch - 'a' + 10);

            if (ch >= 'A' && ch <= 'F')
                return static_cast<unsigned>(ch - 'A' + 10);

            throw usage_error("invalid hexadecimal digit in INGRESDATE");
        }

        inline bool looks_like_ingresdate_hex(std::string_view text) noexcept
        {
            // 12 bytes = 24 hex digits, plus x' and '.
            if (text.size() != 27)
                return false;

            if ((text[0] != 'x' && text[0] != 'X') ||
                text[1] != '\'' ||
                text.back() != '\'') {
                return false;
            }

            return std::all_of(
                text.begin() + 2,
                text.end() - 1,
                [](char ch) { return is_hex_digit(ch); });
        }

        inline std::array<std::uint8_t, 12>
            decode_ingresdate_hex_bytes(std::string_view text)
        {
            if (!looks_like_ingresdate_hex(text))
                throw usage_error("invalid INGRESDATE hexadecimal value");

            std::array<std::uint8_t, 12> bytes{};

            for (std::size_t i = 0; i < bytes.size(); ++i) {
                const std::size_t pos = 2 + i * 2;

                bytes[i] = static_cast<std::uint8_t>(
                    (hex_digit_value(text[pos]) << 4) |
                    hex_digit_value(text[pos + 1]));
            }

            return bytes;
        }

        inline std::uint16_t read_le_u16(
            const std::array<std::uint8_t, 12>& bytes,
            std::size_t offset) noexcept
        {
            return static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(bytes[offset]) |
                (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
        }

        inline std::int16_t read_le_i16(
            const std::array<std::uint8_t, 12>& bytes,
            std::size_t offset) noexcept
        {
            return static_cast<std::int16_t>(read_le_u16(bytes, offset));
        }

        inline std::uint32_t read_le_u32(
            const std::array<std::uint8_t, 12>& bytes,
            std::size_t offset) noexcept
        {
            return
                static_cast<std::uint32_t>(bytes[offset]) |
                (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
                (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
                (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
        }

        inline std::int32_t read_le_i32(
            const std::array<std::uint8_t, 12>& bytes,
            std::size_t offset) noexcept
        {
            return static_cast<std::int32_t>(read_le_u32(bytes, offset));
        }

        template<class Duration>
        std::chrono::time_point<std::chrono::system_clock, Duration>
            parse_ingresdate_hex(std::string_view text)
        {
            const auto bytes = decode_ingresdate_hex_bytes(text);

            // Internal 12-byte INGRESDATE layout:
            //
            //   bytes 0..1   status flags
            //   bytes 2..3   year
            //   bytes 4..5   month
            //   bytes 6..7   day
            //   bytes 8..11  time in milliseconds
            //
            // Integer fields are represented in little-endian byte order in the
            // value currently returned by the OpenAPI path used by this library.

            const std::uint16_t status = read_le_u16(bytes, 0);
            const int year = static_cast<int>(read_le_i16(bytes, 2));
            const int month = static_cast<int>(read_le_i16(bytes, 4));
            const int day = static_cast<int>(read_le_i16(bytes, 6));
            const std::int32_t time_milliseconds = read_le_i32(bytes, 8);

            // An all-zero internal value is commonly associated with an empty
            // INGRESDATE. It cannot be represented by std::chrono::time_point.
            if (status == 0 &&
                year == 0 &&
                month == 0 &&
                day == 0 &&
                time_milliseconds == 0) {
                throw usage_error(
                    "empty INGRESDATE cannot be represented as a system_clock time_point");
            }

            // This conversion supports absolute calendar INGRESDATE values.
            // Ingresdate intervals require a duration-like target, not a time_point.
            if (year == 0 || month == 0 || day == 0) {
                throw usage_error(
                    "partial or interval INGRESDATE cannot be represented as "
                    "a system_clock time_point");
            }

            const std::chrono::year_month_day ymd{
                std::chrono::year{year},
                std::chrono::month{static_cast<unsigned>(month)},
                std::chrono::day{static_cast<unsigned>(day)}
            };

            if (!ymd.ok())
                throw usage_error("invalid calendar date in binary INGRESDATE");

            constexpr std::int64_t milliseconds_per_day =
                24LL * 60LL * 60LL * 1000LL;

            if (time_milliseconds < 0 ||
                static_cast<std::int64_t>(time_milliseconds) >=
                milliseconds_per_day) {
                throw usage_error("invalid time component in binary INGRESDATE");
            }

            const auto value =
                std::chrono::sys_days{ ymd } +
                std::chrono::milliseconds{ time_milliseconds };

            return std::chrono::time_point_cast<Duration>(value);
        }

        inline int parse_fixed_digits(std::string_view text, std::size_t pos, std::size_t count,
            const char* what) {
            if (pos + count > text.size()) throw usage_error(std::string("invalid timestamp: missing ") + what);
            int value = 0;
            for (std::size_t i = 0; i < count; ++i) {
                const unsigned char ch = static_cast<unsigned char>(text[pos + i]);
                if (!std::isdigit(ch)) throw usage_error(std::string("invalid timestamp: bad ") + what);
                value = value * 10 + (ch - '0');
            }
            return value;
        }

        template<class Duration>
        std::chrono::time_point<std::chrono::system_clock, Duration>
            parse_system_time(std::string_view text)
        {
            const auto is_space = [](char ch) {
                return std::isspace(static_cast<unsigned char>(ch)) != 0;
                };

            const auto is_digit = [](char ch) {
                return std::isdigit(static_cast<unsigned char>(ch)) != 0;
                };

            while (!text.empty() && is_space(text.front()))
                text.remove_prefix(1);

            while (!text.empty() && is_space(text.back()))
                text.remove_suffix(1);

            if (looks_like_ingresdate_hex(text))
                return parse_ingresdate_hex<Duration>(text);

            const auto invalid_date_message = [&] {
                return std::string{
                    "invalid timestamp date; expected YYYY-MM-DD, DD/MM/YYYY, "
                    "or a 12-byte INGRESDATE hex value; received '" } +
                    std::string{ text } + "'";
                };

            if (text.size() < 10)
                throw usage_error(invalid_date_message());

            int year = 0;
            unsigned month = 0;
            unsigned day = 0;

            if (text[4] == '-' && text[7] == '-') {
                year = parse_fixed_digits(text, 0, 4, "year");
                month = static_cast<unsigned>(
                    parse_fixed_digits(text, 5, 2, "month"));
                day = static_cast<unsigned>(
                    parse_fixed_digits(text, 8, 2, "day"));
            }
            else if (text[2] == '/' && text[5] == '/') {
                day = static_cast<unsigned>(
                    parse_fixed_digits(text, 0, 2, "day"));
                month = static_cast<unsigned>(
                    parse_fixed_digits(text, 3, 2, "month"));
                year = parse_fixed_digits(text, 6, 4, "year");
            }
            else {
                throw usage_error(invalid_date_message());
            }

            const std::chrono::year_month_day ymd{
                std::chrono::year{year},
                std::chrono::month{month},
                std::chrono::day{day}
            };

            if (!ymd.ok())
                throw usage_error("invalid timestamp calendar date");

            std::size_t pos = 10;
            int hour = 0;
            int minute = 0;
            int second = 0;
            std::chrono::nanoseconds fractional{ 0 };

            if (pos < text.size() &&
                (text[pos] == ' ' || text[pos] == 'T')) {
                ++pos;

                if (pos + 8 > text.size() ||
                    text[pos + 2] != ':' ||
                    text[pos + 5] != ':') {
                    throw usage_error(
                        "invalid timestamp time; expected HH:MM:SS");
                }

                hour = parse_fixed_digits(text, pos, 2, "hour");
                minute = parse_fixed_digits(text, pos + 3, 2, "minute");
                second = parse_fixed_digits(text, pos + 6, 2, "second");
                pos += 8;

                // std::chrono does not directly model leap-second timestamps.
                if (hour > 23 || minute > 59 || second > 59)
                    throw usage_error("invalid timestamp time value");

                if (pos < text.size() && text[pos] == '.') {
                    ++pos;

                    std::int64_t nanoseconds = 0;
                    int digit_count = 0;

                    while (pos < text.size() && is_digit(text[pos])) {
                        if (digit_count < 9) {
                            nanoseconds =
                                nanoseconds * 10 +
                                static_cast<int>(text[pos] - '0');
                        }

                        ++digit_count;
                        ++pos;
                    }

                    if (digit_count == 0)
                        throw usage_error(
                            "invalid timestamp fractional seconds");

                    const int stored_digits = std::min(digit_count, 9);

                    for (int i = stored_digits; i < 9; ++i)
                        nanoseconds *= 10;

                    fractional =
                        std::chrono::nanoseconds{ nanoseconds };
                }
            }

            int offset_minutes = 0;

            if (pos < text.size() &&
                (text[pos] == 'Z' || text[pos] == 'z')) {
                ++pos;
            }
            else if (pos < text.size() &&
                (text[pos] == '+' || text[pos] == '-')) {
                const int sign = text[pos] == '+' ? 1 : -1;
                ++pos;

                if (pos + 2 > text.size())
                    throw usage_error("invalid timestamp UTC offset");

                const int offset_hour =
                    parse_fixed_digits(text, pos, 2, "offset hour");
                pos += 2;

                bool had_colon = false;

                if (pos < text.size() && text[pos] == ':') {
                    had_colon = true;
                    ++pos;
                }

                int offset_minute = 0;

                if (pos < text.size() && is_digit(text[pos])) {
                    if (pos + 2 > text.size())
                        throw usage_error("invalid timestamp UTC offset");

                    offset_minute =
                        parse_fixed_digits(text, pos, 2, "offset minute");
                    pos += 2;
                }
                else if (had_colon) {
                    throw usage_error(
                        "invalid timestamp UTC offset minute");
                }

                // ISO 8601 offsets do not exceed 23:59 syntactically.
                if (offset_hour > 23 || offset_minute > 59)
                    throw usage_error(
                        "invalid timestamp UTC offset value");

                offset_minutes =
                    sign * (offset_hour * 60 + offset_minute);
            }

            while (pos < text.size() && is_space(text[pos]))
                ++pos;

            if (pos != text.size())
                throw usage_error(
                    "invalid trailing characters in timestamp");

            const auto value =
                std::chrono::sys_days{ ymd } +
                std::chrono::hours{ hour } +
                std::chrono::minutes{ minute } +
                std::chrono::seconds{ second } +
                fractional -
                std::chrono::minutes{ offset_minutes };

            return std::chrono::time_point_cast<Duration>(value);
        }

    } // namespace detail

    class sql_error : public std::runtime_error {
    public:
        sql_error(std::string message, std::string query = {});
        const std::string& query() const noexcept;
    private:
        std::string query_;
    };

    struct connection_options {
        std::string target;                 // database, vnode::database, or vnode::database/server_class
        std::optional<std::string> user;
        std::optional<std::string> password;
        int timeout_seconds{ -1 };
    };

    class field_ref {
    public:
        field_ref() = default;

        bool is_null() const noexcept;
        const char* c_str() const;
        std::string_view view() const;

        template<class T>
        T as() const {
            if (is_null()) throw usage_error("attempt to convert a NULL field");

            if constexpr (std::is_same_v<T, std::string>) {
                return std::string(view());
            }
            else if constexpr (std::is_same_v<T, bool>) {
                const auto s = std::string(view());
                return s == "1" || s == "t" || s == "T" || s == "true" || s == "TRUE";
            }
            else if constexpr (std::is_integral_v<T>) {
                std::size_t used = 0;
                if constexpr (std::is_signed_v<T>) {
                    long long v = std::stoll(std::string(view()), &used);
                    if (used != view().size()) throw usage_error("field is not an integer");
                    return static_cast<T>(v);
                }
                else {
                    unsigned long long v = std::stoull(std::string(view()), &used);
                    if (used != view().size()) throw usage_error("field is not an unsigned integer");
                    return static_cast<T>(v);
                }
            }
            else if constexpr (std::is_floating_point_v<T>) {
                std::size_t used = 0;
                long double v = std::stold(std::string(view()), &used);
                if (used != view().size()) throw usage_error("field is not floating-point");
                return static_cast<T>(v);
            }
            else if constexpr (is_system_time_point<T>::value) {
                return detail::parse_system_time<typename T::duration>(view());
            }
            else if constexpr (is_optional<T>::value) {
                using value_type = typename T::value_type;
                return is_null() ? std::nullopt : T{ as<value_type>() };
            }
            else {
                static_assert(!sizeof(T), "bcbpqxx::field_ref::as<T>() has no conversion for T");
            }
        }

    private:
        struct value;
        std::shared_ptr<const value> value_;
        explicit field_ref(std::shared_ptr<const value> value);
        friend class row_ref;
        friend class result;
        friend class work;
    };

    class row_ref {
    public:
        row_ref() = default;

        field_ref operator[](std::size_t index) const;
        field_ref operator[](int index) const;
        field_ref operator[](std::string_view name) const;
        field_ref operator[](const std::string& name) const { return (*this)[std::string_view{ name }]; }

        std::size_t size() const noexcept;

    private:
        struct row_data;
        std::shared_ptr<const row_data> row_;
        explicit row_ref(std::shared_ptr<const row_data> row);
        friend class result;
        friend class work;
    };

    class result {
    public:
        result();

        std::size_t size() const noexcept;
        bool empty() const noexcept;
        std::size_t columns() const noexcept;
        row_ref operator[](std::size_t index) const;

        class const_iterator {
        public:
            using value_type = row_ref;
            using reference = row_ref;
            using pointer = void;
            using difference_type = std::ptrdiff_t;
            using iterator_category = std::forward_iterator_tag;

            reference operator*() const;
            const_iterator& operator++();
            const_iterator operator++(int);
            bool operator==(const const_iterator&) const noexcept = default;

        private:
            const result* owner_{ nullptr };
            std::size_t index_{ 0 };
            const_iterator(const result* owner, std::size_t index) : owner_(owner), index_(index) {}
            friend class result;
        };

        const_iterator begin() const noexcept;
        const_iterator end() const noexcept;

    private:
        struct result_data;
        std::shared_ptr<result_data> data_;
        explicit result(std::shared_ptr<result_data> data);
        friend class work;
    };

    class connection {
    public:
        explicit connection(const connection_options& options);
        explicit connection(std::string_view connection_string);
        ~connection();

        connection(const connection&) = delete;
        connection& operator=(const connection&) = delete;
        connection(connection&&) noexcept;
        connection& operator=(connection&&) noexcept;

        bool is_open() const noexcept;
        void close();

    private:
        struct impl;
        std::unique_ptr<impl> impl_;
        friend class work;
    };

    class work {
    public:
        explicit work(connection& connection);
        ~work();

        work(const work&) = delete;
        work& operator=(const work&) = delete;

        result exec(std::string_view query);
        void commit();
        void abort();

    private:
        connection* connection_;
        void* transaction_{ nullptr };
        bool finished_{ false };
    };

    using read_transaction = work;
    using nontransaction = work; // compatibility alias; OpenAPI still creates a transaction.

} // namespace bcbpqxx

#endif  // #ifdef INGRESSQL