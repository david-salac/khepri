#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <charconv>
#include <chrono>
#if defined(__GNUC__) && (__GNUC__ == 12)
// To support legacy GNU compiler (version 12), should be removed with version 14
#include "legacy_utils.hpp"
#else
#endif

// === Conditional Database Imports ===
#ifdef PGSQL
// To work with PostgreSQL
#include <pqxx/pqxx>
#include "connectors_pqxx.hpp"
#else
#include "mocked_libpqxx.hpp"
#endif

#ifdef INGRESSQL
#include "bcbpqxx.hpp"
#else
#include "mocked_bcbpqxx.hpp"
#endif
// ------------------------------------

#include "connectors_exceptions.hpp"

/**
* Generic connectors capable of working with both PostgreSQL and Actian Ingres databases, either separately or simultaneously.
*/
namespace Connectors {
    inline const std::string KHEPRI_VERSION = "1.0.0.4";

    /**
    * To convert back from binary vector represented by hexadecimal string
    */
    std::vector<unsigned char> from_hex(std::string_view hex);

    /**
    * Auxiliary function to convert fetched string to array of bytes (works for both Ingres and Postgres)
    */
    inline std::vector<unsigned char> string_byte_array_to_vector(const std::string& str) {
        std::string substring = str;
        if (str.starts_with("\\x")) {
            substring = substring.substr(2);
        }
        else if (str.starts_with("x")) {
            substring = substring.substr(1);
        }
        return Connectors::from_hex(substring);
    }

    ///  To declare a type for storing date and time
    using Timestamp = std::chrono::sys_time<std::chrono::microseconds>;

    /**
    * Abstraction of types for both Ingres and Postgres,
    *   this case for a single field
    */
    class UniversalField {
    private:
        std::optional<pqxx::field_ref>    pg_field;
        std::optional<bcbpqxx::field_ref> ig_field;
 
    public:
        UniversalField(pqxx::field_ref    f) : pg_field(std::move(f)) {}
        UniversalField(bcbpqxx::field_ref f) : ig_field(std::move(f)) {}
 
        inline bool is_null() const {
            if (pg_field) return pg_field->is_null();
            if (ig_field) return ig_field->is_null();
            throw ConnectorException("No matching driver");
        }
 
        const char* c_str() const {
            if (pg_field) return pg_field->c_str();
            if (ig_field) return ig_field->c_str();
            throw ConnectorException("No matching driver");
        }
 
        template <typename T>
        T as() const {
            if (!pg_field && !ig_field) {
                throw ConnectorException("No matching driver");
            }
            // Special case: a nullable string maps to std::optional<Timestamp>.
            if constexpr (std::is_same_v<T, std::optional<Timestamp>>) {
                if (is_null()) {
                    return std::nullopt;
                }
                if (pg_field) return pg_field->as<Timestamp>();
                return ig_field->as<Timestamp>();
            }
            // Special case: a nullable string maps to std::optional<Timestamp>.
            else if constexpr (std::is_same_v<T, std::vector<unsigned char>>) {
                if (pg_field) return string_byte_array_to_vector(pg_field->as<std::string>());
                return string_byte_array_to_vector(ig_field->as<std::string>());
            }
            // Special case: a nullable string maps to std::optional<Timestamp>.
            else if constexpr (std::is_same_v<T, std::optional<std::vector<unsigned char>>>) {
                if (is_null()) {
                    return std::nullopt;
                }
                if (pg_field) return string_byte_array_to_vector(pg_field->as<std::string>());
                return string_byte_array_to_vector(ig_field->as<std::string>());
            }
            // Special case: a nullable string maps to std::optional<std::string>.
            else if constexpr (std::is_same_v<T, std::optional<std::string>>) {
                if (is_null()) {
                    return std::nullopt;
                }
                if (pg_field) return pg_field->as<std::string>();
                return ig_field->as<std::string>();
            }
            // Special case: a nullable integer maps to std::optional<int>.
            else if constexpr (std::is_same_v<T, std::optional<int>>) {
                if (is_null()) {
                    return std::nullopt;
                }
                if (pg_field) return pg_field->as<int>();
                return ig_field->as<int>();
            }
            else {
                if (pg_field) return pg_field->as<T>();
                return ig_field->as<T>();
            }
        }
    };
 
    /**
    * Abstraction of types for both Ingres and Postgres,
    *   this case for a single database row
    */
    class UniversalRow {
    private:
        std::optional<pqxx::row_ref>    pg_row;
        std::optional<bcbpqxx::row_ref> ig_row;
 
    public:
        UniversalRow(pqxx::row_ref    r) : pg_row(std::move(r)) {}
        UniversalRow(bcbpqxx::row_ref r) : ig_row(std::move(r)) {}
 
        inline UniversalField operator[](const std::string& key) const {
            if (pg_row) return UniversalField(pg_row.value()[key]);
            if (ig_row) return UniversalField(ig_row.value()[key]);
            throw ConnectorException("No matching driver");
        }
 
        inline UniversalField operator[](int pos) const {
            if (pg_row) return UniversalField(pg_row.value()[pos]);
            if (ig_row) return UniversalField(ig_row.value()[pos]);
            throw ConnectorException("No matching driver");
        }
    };
 
    /**
    * Abstraction of types for both Ingres and Postgres,
    *   this case for a container with result
    */
    class UniversalResult {
 
    private:
        using pg_result_t = pqxx::result;
        using ig_result_t = bcbpqxx::result;
 
        std::optional<pg_result_t> pg_result;
        std::optional<ig_result_t> ig_result;
 
    public:
        UniversalResult(const pg_result_t& r) : pg_result(r) {}
        UniversalResult(pg_result_t&& r)      : pg_result(std::move(r)) {}
 
        UniversalResult(const ig_result_t& r) : ig_result(r) {}
        UniversalResult(ig_result_t&& r)      : ig_result(std::move(r)) {}
 
        // ============================
        // Iterator
        // ============================
        class const_iterator {
        private:
            std::optional<pg_result_t::const_iterator> pg_it;
            std::optional<ig_result_t::const_iterator> ig_it;
 
            // The underlying iterators dereference to a pqxx / bcbpqxx row, not a
            // UniversalRow, so we materialize a UniversalRow on demand and hand
            // back a reference to it. Reset on every increment.
            mutable std::optional<UniversalRow> cache;
 
        public:
            using value_type        = UniversalRow;
            using reference         = const UniversalRow&;
            using pointer           = const UniversalRow*;
            using difference_type   = std::ptrdiff_t;
            using iterator_category = std::forward_iterator_tag;
 
            const_iterator(pg_result_t::const_iterator i) : pg_it(i) {}
            const_iterator(ig_result_t::const_iterator i) : ig_it(i) {}
 
            reference operator*() const {
                if (pg_it) {
                    cache.emplace(*pg_it.value());
                }
                else {
                    cache.emplace(*ig_it.value());
                }
                return *cache;
            }
 
            pointer operator->() const {
                return &(**this);
            }
 
            const_iterator& operator++() {
                if (pg_it) {
                    ++pg_it.value();
                }
                else if (ig_it) {
                    ++ig_it.value();
                }
                cache.reset();
                return *this;
            }
 
            const_iterator operator++(int) {
                const_iterator tmp = *this;
                ++(*this);
                return tmp;
            }
 
            bool operator==(const const_iterator& other) const {
                return pg_it == other.pg_it && ig_it == other.ig_it;
            }
 
            bool operator!=(const const_iterator& other) const {
                return !(*this == other);
            }
        };
 
        // ============================
        // begin / end
        // ============================
        const_iterator begin() const {
            if (pg_result) {
                return const_iterator(pg_result->begin());
            }
            return const_iterator(ig_result->begin());
        }
 
        const_iterator end() const {
            if (pg_result) {
                return const_iterator(pg_result->end());
            }
            return const_iterator(ig_result->end());
        }
    };

    /** 
    * To convert binary vectors to hexadecimal string (for queries)
    */
    inline std::string to_hex(const std::vector<unsigned char>& bytes) {
        std::stringstream ss;
        for (unsigned char b : bytes) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
        }
        return ss.str();
    }

    enum class ConnectorType {
        POSTGRESQL, INGRES
    };

    class BaseConnector {
    protected:
        bool is_closed = true;
        ConnectorType contype;

        std::optional<UniversalResult> query_result;
        /// Prefix that is added to the table name (like "thoth.", "biobank.").
        std::optional<std::string> table_prefix;
    public:
        const ConnectorType& get_connector_type() const {
            return contype;
        }
        virtual void connect() = 0;
        virtual void select(const std::string& query) = 0;
        virtual void update(const std::string& query) = 0;
        virtual void dml_delete(const std::string& query) = 0;
        virtual void insert(const std::string& query) = 0;
        virtual bool is_empty(const std::string& query) = 0;
        inline UniversalResult& result() {
            if(query_result)
                return query_result.value();
            throw ConnectorException("Result does not exist.");
        }
        /**
        * Returns either table prefix or an empty string
        */
        inline std::string get_table_prefix() const {
            return table_prefix.value_or("");
        }

        /** 
        * To facilitate conversion to types such as BYTEA, where values like ('\xHEX' or x'HEX') is required.
        */
        virtual std::string to_sql_binary_literal(const std::vector<unsigned char>& bytes) const = 0;
        /**
        * Returns NOW() for PostgreSQL and DATE('now') for Ingres Database (abstraction)
        */
        virtual std::string to_sql_now() const = 0;
        virtual std::string to_null() const = 0;
        /**
        * Converts string to insertable/quiarable entity
        */
        virtual std::string to_sql_string(std::string_view in_string) const = 0;
        virtual std::string to_sql_string(const std::string& in_string) const = 0;
        virtual std::string to_sql_string(const std::optional<std::string>& in_string) const = 0;
        virtual std::string to_sql_string(const std::vector<unsigned char>& bytes) const = 0;
        virtual std::string to_sql_string(const std::optional<std::vector<unsigned char>>& bytes) const = 0;
        virtual std::string to_sql_string(const std::optional<int>& value) const = 0;
        virtual std::string bool_to_sql_string(const std::optional<bool>& value) const = 0;
        /**
        * Check whether the string input is a number, if so, returns identity, if not, throws exception.
        */
        inline static std::string_view as_number_string(std::string_view sv)
        {
            if (sv.empty())
                throw std::invalid_argument("not a number");

            // Integer
            long long integer{};
            auto [end, ec] = std::from_chars(
                sv.data(), sv.data() + sv.size(), integer);

            if (ec == std::errc{} && end == sv.data() + sv.size())
                return sv;  // Original string, unchanged

            // Floating point
            double floating{};
            auto [float_end, float_ec] = std::from_chars(
                sv.data(), sv.data() + sv.size(), floating);

            if (float_ec == std::errc{} &&
                float_end == sv.data() + sv.size())
                return sv;  // Original string, unchanged

            throw std::invalid_argument("not a number");
        }
        inline virtual std::string to_sql_string(const std::optional <Timestamp>& value) const = 0;
        inline virtual std::vector<unsigned char> string_byte_array_to_vector(const std::string& str) const {
            return string_byte_array_to_vector(str);
        }
        /**
        * Returns a SQL-formated function for decrypting, e.g. AES_DECRYPT(<COL>, <PWD>, 256) as a string
        */
        virtual std::string decrypt_function_expression(std::string_view column, std::string_view password) const = 0;
        /**
        * Returns a SQL-formated function for encrypting, e.g. AES_ENCRYPT(<COL>, <PWD>, 256) as a string
        */
        virtual std::string encrypt_function_expression(std::string_view column, std::string_view password) const = 0;

        /**
         * Returns the next value from the specified sequence.
         * This operation advances the sequence state.
         */
        virtual int next_value_in_sequence(std::string_view sequence_name) = 0;

        virtual void close() = 0;
        // To safely disconnect from the DB
        virtual ~BaseConnector() {
        };
        BaseConnector(
            ConnectorType contype, 
            const std::optional<std::string>& table_prefix = std::nullopt
        ) : contype(contype), table_prefix(table_prefix) {
        }
        /**
        * Run through std::string and replaces $DECRYPT(column_name) with an appropriate
        *	RDBMs decrypting function.
        * For example (for Ingres): 
        *   $DECRYPT(surname) -> AES_DECRYPT(surname, <password>, 256)
        *   $ENCRYPT(surname) -> AES_ENCRYPT(surname, <password>, 256)
        */
        std::string transform_pseudosql_syntax(
            const std::string& input,
            std::string_view password) const;
    };

    class IngresConnector : public BaseConnector {
    private:
        std::string connection_string;
        std::optional<bcbpqxx::connection> conn;
    public:
        IngresConnector() : 
            BaseConnector(ConnectorType::INGRES) {
        }
        IngresConnector(const std::string& connection_string, const std::optional<std::string>& table_prefix = std::nullopt) : 
            BaseConnector(ConnectorType::INGRES, table_prefix),
            connection_string(connection_string)
        {
        }
        virtual void connect() override;
        virtual void select(const std::string& query) override {
            bcbpqxx::work tx(*conn);
            
            UniversalResult qres(tx.exec(query));
            this->query_result = std::move(qres);
            tx.commit();
        }
        virtual void update(const std::string& query) override;
        virtual void insert(const std::string& query) override;
        virtual void dml_delete(const std::string& query) override;

        inline virtual bool is_empty(const std::string& query) override {
            bcbpqxx::work tx(*conn);
            bool empty_state = tx.exec(query).size() == 0;
            tx.commit();
            return empty_state;
        }
        inline virtual void close() override {
            query_result.reset();
            conn.reset();
            is_closed = true;
        }
        inline virtual std::string to_null() const override {
            return "NULL";
        }
        inline virtual std::string to_sql_binary_literal(const std::vector<unsigned char>& bytes) const override {
            std::string strres = "x'";
            strres += to_hex(bytes);
            strres += "'";
            return strres;
        }
        inline virtual std::string to_sql_now() const override {
            return "current_timestamp()";
        }
        inline virtual std::string to_sql_string(const std::string& in_string) const override {
            std::string out = "'";
            for (char c : in_string) {
                if (c == '\'') out += "''";
                else           out += c;
            }
            out += "'";
            return out;
        }
        inline virtual std::string to_sql_string(const std::optional<std::string>& in_string) const override {
            return in_string ? to_sql_string(*in_string) : to_null();
        }
        inline virtual std::string to_sql_string(const std::vector<unsigned char>& bytes) const override {
            return to_sql_binary_literal(bytes);
        }
        inline virtual std::string to_sql_string(const std::optional<int>& value) const override {
            return value.has_value() ? std::to_string(*value) : to_null();
        }
        inline virtual std::string bool_to_sql_string(const std::optional<bool>& value) const override {
            return value.has_value() ? (value.value() ? "TRUE" : "FALSE") : to_null();
        }
        inline virtual std::string to_sql_string(const std::optional<std::vector<unsigned char>>& bytes) const override {
            if (bytes) {
                return to_sql_string(*bytes);
            }
            return to_null();
        }
        inline virtual std::string to_sql_string(std::string_view in_string) const override {
            std::string out = "'";
            for (char c : in_string) {
                if (c == '\'') out += "''";
                else           out += c;
            }
            out += "'";
            return out;
        }
        inline virtual std::string to_sql_string(const std::optional <Timestamp>& value) const override {
            if (!value) {
                return to_null();
            }
            std::stringstream output;
            output << "'";
            output << value.value();
            output << "'";
            return output.str();
        }
        inline virtual std::string decrypt_function_expression(std::string_view column, std::string_view password) const override {
            return std::format("VARCHAR(AES_DECRYPT({}, {}, 256))", column, to_sql_string(password));
        }
        inline virtual std::string encrypt_function_expression(std::string_view column, std::string_view password) const override {
            return std::format("AES_ENCRYPT({}, {}, 256)", column, to_sql_string(password));
        }

        virtual int next_value_in_sequence(std::string_view sequence_name) override;

        virtual ~IngresConnector() override {
            if (!is_closed) {
                this->close();
            }
        }
    };

    class PostgreSQLConnector : public BaseConnector {
    private:
        std::string connection_string;
        std::optional<pqxx::connection> conn;
    public:
        PostgreSQLConnector() : 
            BaseConnector(ConnectorType::POSTGRESQL) {
        }
        PostgreSQLConnector(const std::string& connection_string, const std::optional<std::string>& table_prefix = std::nullopt) :
            BaseConnector(ConnectorType::POSTGRESQL, table_prefix), 
            connection_string(connection_string) {
        }
        virtual void connect() override;
        virtual void select(const std::string& query) override {
            pqxx::read_transaction tx(conn.value());
            UniversalResult qres(tx.exec(query));
            this->query_result = std::move(qres);
        }
        virtual void update(const std::string& query) override;
        virtual void insert(const std::string& query) override;
        virtual void dml_delete(const std::string& query) override;

        inline virtual bool is_empty(const std::string& query) override {
            pqxx::nontransaction tx(conn.value());
            return tx.exec(query.c_str()).empty();
        }
        inline virtual void close() override {
            conn.reset();
            is_closed = true;
        }
        inline virtual std::string to_null() const override {
            return "NULL";
        }
        inline virtual std::string to_sql_binary_literal(const std::vector<unsigned char>& bytes) const override {
            std::string strres = "'\\x";
            strres += to_hex(bytes);
            strres += "'";
            return strres;
        }
        inline virtual std::string to_sql_now() const override {
            return "NOW()";
        }
        inline virtual std::string to_sql_string(std::string_view in_string) const override {
            std::string out = "'";
            for (char c : in_string) {
                if (c == '\'') out += "''";
                else           out += c;
            }
            out += "'";
            return out;
        }
        inline virtual std::string to_sql_string(const std::string& in_string) const override {
            std::string out = "'";
            for (char c : in_string) {
                if (c == '\'') out += "''";
                else           out += c;
            }
            out += "'";
            return out;
        }
        inline virtual std::string to_sql_string(const std::optional<std::string>& in_string) const override {
            return in_string ? to_sql_string(*in_string) : to_null();
        }
        inline virtual std::string to_sql_string(const std::vector<unsigned char>& bytes) const override {
            return to_sql_binary_literal(bytes);
        }
        inline virtual std::string to_sql_string(const std::optional<int>& value) const override {
            return value.has_value() ? std::to_string(*value) : to_null();
        }
        inline virtual std::string bool_to_sql_string(const std::optional<bool>& value) const override {
            return value.has_value() ? (value.value() ? "TRUE" : "FALSE") : to_null();
        }
        inline virtual std::string to_sql_string(const std::optional<std::vector<unsigned char>>& bytes) const override {
            if (bytes) {
                return to_sql_string(*bytes);
            }
            return to_null();
        }
        inline virtual std::string to_sql_string(const std::optional <Timestamp>& value) const override {
            if (!value) {
                return to_null();
            }
            std::stringstream output;
            output << "'";
            output << value.value();
            output << "'";
            return output.str();
        }
        inline virtual std::string decrypt_function_expression(std::string_view column, std::string_view password) const override {
            return std::format("pgp_sym_decrypt({}, {})", column, to_sql_string(password));
        }
        inline virtual std::string encrypt_function_expression(std::string_view column, std::string_view password) const override {
            return std::format("pgp_sym_encrypt({}, {})", column, to_sql_string(password));
        }

        virtual int next_value_in_sequence(std::string_view sequence_name) override;

        virtual ~PostgreSQLConnector() {
            if (!is_closed) {
                this->close();
            }
        }
    };
}
