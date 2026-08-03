#pragma once
#ifndef INGRESSQL
// Header-only, no-database mock of the bcbpqxx API.
// It is intended for builds/tests where Actian Ingres and IIapi are unavailable.

#include <chrono>
#if defined(__GNUC__) && (__GNUC__ == 12)
// To support legacy GNU compiler (version 12), should be removed with version 14
#include "legacy_utils.hpp"
#else
#endif
#include <cstddef>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace bcbpqxx {

class usage_error : public std::logic_error {
public:
    using std::logic_error::logic_error;
};

class sql_error : public std::runtime_error {
public:
    explicit sql_error(std::string message, std::string query = {})
        : std::runtime_error(std::move(message)), query_(std::move(query)) {}

    [[nodiscard]] const std::string& query() const noexcept { return query_; }

private:
    std::string query_;
};

template<class T>
struct is_optional : std::false_type {};

template<class T>
struct is_optional<std::optional<T>> : std::true_type {};

template<class T>
inline constexpr bool is_optional_v = is_optional<T>::value;

struct connection_options {
    std::string target;
    std::optional<std::string> user;
    std::optional<std::string> password;
    int timeout_seconds{-1};
};

class field_ref {
public:
    field_ref() = default;

    // Mock fields are NULL.  c_str()/view() are still safe and return empty text.
    [[nodiscard]] bool is_null() const noexcept { return true; }
    [[nodiscard]] const char* c_str() const noexcept { return ""; }
    [[nodiscard]] std::string_view view() const noexcept { return {}; }

    template<class T>
    [[nodiscard]] T as() const {
        using value_type = std::remove_cv_t<std::remove_reference_t<T>>;

        if constexpr (is_optional_v<value_type>) {
            return std::nullopt;
        } else if constexpr (std::is_same_v<value_type, std::string>) {
            return {};
        } else if constexpr (
            std::is_arithmetic_v<value_type> ||
            std::is_enum_v<value_type>) {
            return value_type{};
        } else if constexpr (requires { typename value_type::clock; typename value_type::duration; }) {
            // Handles std::chrono::time_point, including sys_time<microseconds>.
            return value_type{};
        } else if constexpr (std::is_default_constructible_v<value_type>) {
            // Useful for vectors and user-defined value types in compile-only builds.
            return value_type{};
        } else {
            static_assert(std::is_default_constructible_v<value_type>,
                          "mock bcbpqxx::field_ref::as<T>() requires T to be default constructible");
        }
    }
};

class row_ref {
public:
    row_ref() = default;

    [[nodiscard]] field_ref operator[](std::size_t) const noexcept { return {}; }
    [[nodiscard]] field_ref operator[](int) const noexcept { return {}; }
    [[nodiscard]] field_ref operator[](std::string_view) const noexcept { return {}; }
    [[nodiscard]] field_ref operator[](const std::string&) const noexcept { return {}; }
    [[nodiscard]] field_ref operator[](const char*) const noexcept { return {}; }

    [[nodiscard]] std::size_t size() const noexcept { return 0; }
};

class result {
public:
    result() = default;

    [[nodiscard]] std::size_t size() const noexcept { return 0; }
    [[nodiscard]] bool empty() const noexcept { return true; }
    [[nodiscard]] std::size_t columns() const noexcept { return 0; }
    [[nodiscard]] row_ref operator[](std::size_t) const noexcept { return {}; }

    class const_iterator {
    public:
        using value_type = row_ref;
        using reference = row_ref;
        using pointer = void;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;

        [[nodiscard]] reference operator*() const noexcept { return {}; }
        const_iterator& operator++() noexcept { return *this; }
        const_iterator operator++(int) noexcept {
            const_iterator copy = *this;
            return copy;
        }
        [[nodiscard]] bool operator==(const const_iterator&) const noexcept = default;
    };

    [[nodiscard]] const_iterator begin() const noexcept { return {}; }
    [[nodiscard]] const_iterator end() const noexcept { return {}; }
};

class connection {
public:
    explicit connection(const connection_options&) noexcept : open_(true) {}
    explicit connection(std::string_view) noexcept : open_(true) {}
    explicit connection(const std::string&) noexcept : open_(true) {}
    explicit connection(const char*) noexcept : open_(true) {}
    ~connection() = default;

    connection(const connection&) = delete;
    connection& operator=(const connection&) = delete;
    connection(connection&&) noexcept = default;
    connection& operator=(connection&&) noexcept = default;

    [[nodiscard]] bool is_open() const noexcept { return open_; }
    void close() noexcept { open_ = false; }

private:
    bool open_{false};
};

class work {
public:
    explicit work(connection& connection) noexcept : connection_(&connection) {}
    ~work() = default;

    work(const work&) = delete;
    work& operator=(const work&) = delete;
    work(work&&) noexcept = default;
    work& operator=(work&&) noexcept = default;

    [[nodiscard]] result exec(std::string_view) const noexcept { return {}; }
    [[nodiscard]] result exec(const std::string&) const noexcept { return {}; }
    [[nodiscard]] result exec(const char*) const noexcept { return {}; }

    void commit() noexcept { finished_ = true; }
    void abort() noexcept { finished_ = true; }

private:
    connection* connection_{nullptr};
    bool finished_{false};
};

using read_transaction = work;
using nontransaction = work;

} // namespace bcbpqxx

#endif // #ifndef INGRESSQL