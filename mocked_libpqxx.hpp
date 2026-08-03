#pragma once
#ifndef PGSQL

// Minimal mock of the pqxx/pqxx (libpqxx) API.
// Nothing realistic is returned — this only provides enough surface
// to compile and link code that uses the subset of pqxx below.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace pqxx {

    // A single column value.
    class field {
    public:
        bool is_null() const { return true; }
        const char* c_str() const { return "mocked"; }

        template <typename T>
        T as() const {
            if constexpr (std::is_same_v<T, std::string>) {
                return std::string(c_str());
            } else {
                return T{};
            }
        }
    };

    // A single row; indexable by column name or column index.
    class row {
    public:
        field operator[](std::string_view) const { return field{}; }
        field operator[](std::size_t) const { return field{}; }
    };

    // Aliases for comaptibility
    using field_ref = field;
    using row_ref = row;

    // Result set: iterable, reports emptiness.
    class result {
    public:
        using const_iterator = std::vector<row>::const_iterator;

        const_iterator begin() const { return rows_.begin(); }
        const_iterator end() const { return rows_.end(); }

        bool empty()    const { return rows_.empty(); }
        bool no_rows()  const { return rows_.empty(); }
        std::size_t size() const { return rows_.size(); }

    private:
        std::vector<row> rows_;
    };

    // Database connection.
    class connection {
    public:
        connection() = default;
        explicit connection(std::string conn_str) : conn_str_(std::move(conn_str)) {}

        bool is_open() const { return true; }

        void close() {}

    private:
        std::string conn_str_;
    };

    // Base for the various transaction flavours.
    class transaction_base {
    public:
        explicit transaction_base(connection&) {}

        result exec(std::string_view) { return result{}; }

        void commit() {}
        void abort()  {}
    };

    // Read/write transaction.
    class work : public transaction_base {
    public:
        using transaction_base::transaction_base;
    };

    // Non-transaction (autocommit, no BEGIN/COMMIT).
    class nontransaction : public transaction_base {
    public:
        using transaction_base::transaction_base;
    };

    // Read-only transaction.
    class read_transaction : public transaction_base {
    public:
        using transaction_base::transaction_base;
    };

} // namespace pqxx
#endif
