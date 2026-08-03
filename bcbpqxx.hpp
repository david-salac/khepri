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
            parse_system_time(std::string_view text) {
            // Accepted examples:
            //   2026-07-31 13:30:45
            //   2026-07-31T13:30:45.123456
            //   2026-07-31T13:30:45.123456Z
            //   2026-07-31T13:30:45.123456+01:00
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);

            if (text.size() < 10 || text[4] != '-' || text[7] != '-')
                throw usage_error("invalid timestamp date; expected YYYY-MM-DD");

            const int y = parse_fixed_digits(text, 0, 4, "year");
            const unsigned mo = static_cast<unsigned>(parse_fixed_digits(text, 5, 2, "month"));
            const unsigned d = static_cast<unsigned>(parse_fixed_digits(text, 8, 2, "day"));

            const std::chrono::year_month_day ymd{
                std::chrono::year{y}, std::chrono::month{mo}, std::chrono::day{d} };
            if (!ymd.ok()) throw usage_error("invalid timestamp calendar date");

            std::size_t pos = 10;
            int hh = 0, mm = 0, ss = 0;
            std::chrono::nanoseconds fractional{ 0 };

            if (pos < text.size() && (text[pos] == ' ' || text[pos] == 'T')) {
                ++pos;
                if (pos + 8 > text.size() || text[pos + 2] != ':' || text[pos + 5] != ':')
                    throw usage_error("invalid timestamp time; expected HH:MM:SS");
                hh = parse_fixed_digits(text, pos, 2, "hour");
                mm = parse_fixed_digits(text, pos + 3, 2, "minute");
                ss = parse_fixed_digits(text, pos + 6, 2, "second");
                pos += 8;

                if (hh > 23 || mm > 59 || ss > 60) throw usage_error("invalid timestamp time value");

                if (pos < text.size() && text[pos] == '.') {
                    ++pos;
                    std::int64_t digits = 0;
                    int count = 0;
                    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
                        if (count < 9) digits = digits * 10 + (text[pos] - '0');
                        ++count;
                        ++pos;
                    }
                    if (count == 0) throw usage_error("invalid timestamp fractional seconds");
                    for (int i = count; i < 9; ++i) digits *= 10;
                    fractional = std::chrono::nanoseconds{ digits };
                }
            }

            int offset_minutes = 0;
            if (pos < text.size() && (text[pos] == 'Z' || text[pos] == 'z')) {
                ++pos;
            }
            else if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
                const int sign = text[pos] == '+' ? 1 : -1;
                ++pos;
                if (pos + 2 > text.size()) throw usage_error("invalid timestamp UTC offset");
                const int oh = parse_fixed_digits(text, pos, 2, "offset hour");
                pos += 2;
                if (pos < text.size() && text[pos] == ':') ++pos;
                int om = 0;
                if (pos + 2 <= text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
                    om = parse_fixed_digits(text, pos, 2, "offset minute");
                    pos += 2;
                }
                if (oh > 23 || om > 59) throw usage_error("invalid timestamp UTC offset value");
                offset_minutes = sign * (oh * 60 + om);
            }

            while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
            if (pos != text.size()) throw usage_error("invalid trailing characters in timestamp");

            auto tp = std::chrono::sys_days{ ymd }
                + std::chrono::hours{ hh }
                + std::chrono::minutes{ mm }
                + std::chrono::seconds{ ss }
                + fractional
                - std::chrono::minutes{ offset_minutes };
            return std::chrono::time_point_cast<Duration>(tp);
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