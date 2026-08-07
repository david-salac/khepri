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

        /**
         * @brief Tests whether a character is a hexadecimal digit.
         * @param ch Character to test.
         * @return true when @p ch is in 0-9, a-f, or A-F; otherwise false.
         */
        inline bool is_hex_digit(char ch) noexcept
        {
            const auto c = static_cast<unsigned char>(ch);
            return std::isdigit(c) != 0 ||
                (c >= 'a' && c <= 'f') ||
                (c >= 'A' && c <= 'F');
        }

        /**
         * @brief Converts a hexadecimal digit to its numeric value.
         * @param ch Hexadecimal character in the range 0-9, a-f, or A-F.
         * @return Numeric value in the range 0 through 15.
         * @throws usage_error if @p ch is not a hexadecimal digit.
         */
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

        /**
         * @brief Checks for the 12-byte INGRESDATE hexadecimal text representation.
         * @param text Candidate field value.
         * @return true when the value has the form x'<24 hex digits>'; otherwise false.
         */
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

        /**
         * @brief Decodes an INGRESDATE hexadecimal string into its 12 binary bytes.
         * @param text Value in x'<24 hex digits>' form.
         * @return The decoded 12-byte INGRESDATE representation.
         * @throws usage_error if @p text does not contain a valid INGRESDATE hex value.
         */
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

        /**
         * @brief Reads an unsigned 16-bit little-endian value from an INGRESDATE buffer.
         * @param bytes Source 12-byte buffer.
         * @param offset Zero-based byte offset of the value.
         * @return Decoded unsigned 16-bit value.
         */
        inline std::uint16_t read_le_u16(
            const std::array<std::uint8_t, 12>& bytes,
            std::size_t offset) noexcept
        {
            return static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(bytes[offset]) |
                (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
        }

        /**
         * @brief Reads a signed 16-bit little-endian value from an INGRESDATE buffer.
         * @param bytes Source 12-byte buffer.
         * @param offset Zero-based byte offset of the value.
         * @return Decoded signed 16-bit value.
         */
        inline std::int16_t read_le_i16(
            const std::array<std::uint8_t, 12>& bytes,
            std::size_t offset) noexcept
        {
            return static_cast<std::int16_t>(read_le_u16(bytes, offset));
        }

        /**
         * @brief Reads an unsigned 32-bit little-endian value from an INGRESDATE buffer.
         * @param bytes Source 12-byte buffer.
         * @param offset Zero-based byte offset of the value.
         * @return Decoded unsigned 32-bit value.
         */
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

        /**
         * @brief Reads a signed 32-bit little-endian value from an INGRESDATE buffer.
         * @param bytes Source 12-byte buffer.
         * @param offset Zero-based byte offset of the value.
         * @return Decoded signed 32-bit value.
         */
        inline std::int32_t read_le_i32(
            const std::array<std::uint8_t, 12>& bytes,
            std::size_t offset) noexcept
        {
            return static_cast<std::int32_t>(read_le_u32(bytes, offset));
        }

        /**
         * @brief Converts a binary INGRESDATE hex value to a system-clock time point.
         * @tparam Duration Precision of the returned time point.
         * @param text INGRESDATE value encoded as x'<24 hex digits>'.
         * @return Parsed absolute date/time converted to @p Duration precision.
         * @throws usage_error for empty, partial, interval, or invalid INGRESDATE values.
         */
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

        /**
         * @brief Parses a fixed-width decimal component from timestamp text.
         * @param text Full timestamp text being parsed.
         * @param pos Starting position of the component.
         * @param count Number of decimal digits to consume.
         * @param what Human-readable component name used in error messages.
         * @return Parsed integer value.
         * @throws usage_error if the component is missing or contains non-digits.
         */
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

        /**
         * @brief Parses a database date/time field into a system-clock time point.
         *
         * Accepts ISO-style YYYY-MM-DD values, DD/MM/YYYY values, optional time and
         * UTC offsets, and the legacy 12-byte INGRESDATE hexadecimal representation.
         *
         * @tparam Duration Precision of the returned time point.
         * @param text Database field text to parse.
         * @return Parsed instant converted to @p Duration precision.
         * @throws usage_error if the value is malformed or cannot represent an instant.
         */
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

    /** @brief Exception raised for Ingres/OpenAPI execution failures. */
    class sql_error : public std::runtime_error {
    public:
        /**
         * @brief Creates an SQL exception with optional query context.
         * @param message Human-readable error message.
         * @param query SQL text associated with the error, if available.
         */
        sql_error(std::string message, std::string query = {});

        /**
         * @brief Returns the SQL statement associated with this error.
         * @return Stored SQL text, or an empty string when no query was supplied.
         */
        const std::string& query() const noexcept;
    private:
        std::string query_;
    };

    /** @brief Settings used to establish an Ingres OpenAPI connection. */
    struct connection_options {
        std::string target;                 // database, vnode::database, or vnode::database/server_class
        std::optional<std::string> user;
        std::optional<std::string> password;
        int timeout_seconds{ -1 };
    };

    /** @brief Lightweight reference to a single value in a result row. */
    class field_ref {
    public:
        field_ref() = default;

        /**
         * @brief Tests whether the database value is SQL NULL.
         * @return true for SQL NULL; otherwise false.
         */
        bool is_null() const noexcept;

        /**
         * @brief Returns the decoded field as a null-terminated string.
         * @return Pointer to the field text, or nullptr when the field is SQL NULL.
         */
        const char* c_str() const;

        /**
         * @brief Returns a non-owning view of the decoded field text.
         * @return Field text, or an empty view when the field is SQL NULL.
         */
        std::string_view view() const;

        /**
         * @brief Converts the field text to a supported C++ type.
         * @tparam T Requested destination type.
         * @return Converted field value.
         * @throws usage_error when the field is NULL or cannot be converted to @p T.
         */
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

    /** @brief Lightweight reference to one row of a query result. */
    class row_ref {
    public:
        row_ref() = default;

        /**
         * @brief Accesses a field by zero-based column index.
         * @param index Zero-based column index.
         * @return Reference to the requested field.
         * @throws std::out_of_range when @p index is outside the row.
         */
        field_ref operator[](std::size_t index) const;

        /**
         * @brief Accesses a field by signed zero-based column index.
         * @param index Zero-based column index.
         * @return Reference to the requested field.
         * @throws std::out_of_range when @p index is negative or outside the row.
         */
        field_ref operator[](int index) const;

        /**
         * @brief Accesses a field by column name.
         * @param name Column name returned by the result descriptor.
         * @return Reference to the requested field.
         * @throws std::out_of_range when the column name is unknown.
         */
        field_ref operator[](std::string_view name) const;
        field_ref operator[](const std::string& name) const { return (*this)[std::string_view{ name }]; }

        /**
         * @brief Returns the number of fields in this row.
         * @return Number of columns represented by the row.
         */
        std::size_t size() const noexcept;

    private:
        struct row_data;
        std::shared_ptr<const row_data> row_;
        explicit row_ref(std::shared_ptr<const row_data> row);
        friend class result;
        friend class work;
    };

    /** @brief Materialized set of rows returned by a query. */
    class result {
    public:
        result();

        /** @brief Returns the number of rows. @return Row count. */
        std::size_t size() const noexcept;

        /** @brief Tests whether the result contains no rows. @return true when empty. */
        bool empty() const noexcept;

        /** @brief Returns the number of result columns. @return Column count. */
        std::size_t columns() const noexcept;

        /**
         * @brief Accesses a row by zero-based index.
         * @param index Zero-based row index.
         * @return Reference object for the requested row.
         * @throws std::out_of_range when @p index is outside the result.
         */
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

        /** @brief Returns an iterator to the first row. @return Beginning iterator. */
        const_iterator begin() const noexcept;

        /** @brief Returns the past-the-end row iterator. @return End iterator. */
        const_iterator end() const noexcept;

    private:
        struct result_data;
        std::shared_ptr<result_data> data_;
        explicit result(std::shared_ptr<result_data> data);
        friend class work;
    };

    /** @brief RAII wrapper around an Ingres OpenAPI connection. */
    class connection {
    public:
        /**
         * @brief Opens a connection using structured options.
         * @param options Target, credentials, and timeout settings.
         * @throws usage_error if the target is empty.
         * @throws sql_error if OpenAPI initialization or connection fails.
         */
        explicit connection(const connection_options& options);

        /**
         * @brief Opens a connection from a compact connection string.
         * @param connection_string Space-separated target/key=value connection text.
         * @throws usage_error if the resulting target is empty.
         * @throws sql_error if OpenAPI initialization or connection fails.
         */
        explicit connection(std::string_view connection_string);

        /** @brief Closes the connection and releases OpenAPI resources. */
        ~connection();

        connection(const connection&) = delete;
        connection& operator=(const connection&) = delete;
        connection(connection&&) noexcept;
        connection& operator=(connection&&) noexcept;

        /** @brief Tests whether the underlying OpenAPI connection is active. @return true when open. */
        bool is_open() const noexcept;

        /** @brief Disconnects and releases this connection's runtime reference. */
        void close();

    private:
        struct impl;
        std::unique_ptr<impl> impl_;
        friend class work;
    };

    /** @brief Transaction scope for executing SQL through an open connection. */
    class work {
    public:
        /**
         * @brief Starts a transaction scope associated with an open connection.
         * @param connection Connection used for subsequent statements.
         * @throws usage_error if @p connection is closed.
         */
        explicit work(connection& connection);

        /** @brief Rolls back an unfinished transaction during destruction. */
        ~work();

        work(const work&) = delete;
        work& operator=(const work&) = delete;

        /**
         * @brief Executes one SQL statement and materializes any returned rows.
         * @param query SQL statement to execute.
         * @return Result rows; non-row-producing statements return an empty result.
         * @throws usage_error if the transaction is finished or the connection is closed.
         * @throws sql_error if OpenAPI reports an execution or fetch failure.
         */
        result exec(std::string_view query);

        /** @brief Commits the active transaction and marks this work object finished. */
        void commit();

        /** @brief Rolls back the active transaction and marks this work object finished. */
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