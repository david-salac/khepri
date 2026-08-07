#include "bcbpqxx.hpp"
#ifdef INGRESSQL

#include <iiapi.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace bcbpqxx {
    namespace {

        // OpenAPI is process-global, so initialization/termination is shared by all
        // bcbpqxx connections. The mutex protects the reference count and environment
        // handle from concurrent connection construction/destruction.
        std::mutex runtime_mutex;
        std::size_t runtime_users = 0;
        II_PTR runtime_env_handle = nullptr;

        /**
         * @brief Classifies OpenAPI statuses that still represent successful completion.
         * @param status OpenAPI status code.
         * @return true for success, message, or warning statuses.
         */
        bool ok_status(II_LONG status) {
            return status == IIAPI_ST_SUCCESS || status == IIAPI_ST_MESSAGE || status == IIAPI_ST_WARNING;
        }

        /**
         * @brief Converts an OpenAPI status code to a readable symbolic name.
         * @param status OpenAPI status code.
         * @return Static string describing the status.
         */
        const char* status_name(II_LONG status) noexcept {
            switch (status) {
            case IIAPI_ST_SUCCESS:         return "IIAPI_ST_SUCCESS";
            case IIAPI_ST_MESSAGE:         return "IIAPI_ST_MESSAGE";
            case IIAPI_ST_WARNING:         return "IIAPI_ST_WARNING";
            case IIAPI_ST_NO_DATA:         return "IIAPI_ST_NO_DATA";
            case IIAPI_ST_ERROR:           return "IIAPI_ST_ERROR";
            case IIAPI_ST_FAILURE:         return "IIAPI_ST_FAILURE";
            case IIAPI_ST_NOT_INITIALIZED: return "IIAPI_ST_NOT_INITIALIZED";
            case IIAPI_ST_INVALID_HANDLE:  return "IIAPI_ST_INVALID_HANDLE";
            case IIAPI_ST_OUT_OF_MEMORY:   return "IIAPI_ST_OUT_OF_MEMORY";
            default:                       return "IIAPI_ST_UNKNOWN";
            }
        }

        /**
         * @brief Collects all diagnostic records attached to an OpenAPI error handle.
         * @param error_handle Handle supplied by an OpenAPI operation.
         * @return Multi-line diagnostic text, or an empty string when no handle exists.
         */
        std::string collect_error_info(II_PTR error_handle) {
            if (!error_handle) return {};

            std::ostringstream details;
            std::size_t record_number = 0;

            for (;;) {
                IIAPI_GETEINFOPARM info{};
                info.ge_errorHandle = error_handle;
                IIapi_getErrorInfo(&info);

                if (info.ge_status == IIAPI_ST_NO_DATA) break;
                if (info.ge_status != IIAPI_ST_SUCCESS) {
                    details << "\n  [diagnostics unavailable: IIapi_getErrorInfo returned "
                        << status_name(info.ge_status) << " (" << info.ge_status << ")]";
                    break;
                }

                ++record_number;
                details << "\n  [" << record_number << "]";

                // ge_SQLSTATE is documented as a six-byte, null-terminated array.
                if (info.ge_SQLSTATE[0] != '\0')
                    details << " SQLSTATE=" << info.ge_SQLSTATE;

                details << " native_code=" << info.ge_errorCode
                    << " type=" << info.ge_type;

                if (info.ge_message && *info.ge_message)
                    details << " message=" << info.ge_message;
            }

            return details.str();
        }

        /**
         * @brief Builds and throws a sql_error from a completed OpenAPI operation.
         * @param gen Generic OpenAPI completion/status structure.
         * @param operation Name of the OpenAPI operation that failed.
         * @param query Optional SQL text associated with the failure.
         * @return This function does not return.
         */
        [[noreturn]] void throw_openapi_error(
            const IIAPI_GENPARM& gen,
            std::string_view operation,
            std::string_view query = {})
        {
            std::ostringstream message;
            message << operation << " failed: " << status_name(gen.gp_status)
                << " (OpenAPI status " << gen.gp_status << ')'
                << collect_error_info(gen.gp_errorHandle);

            if (!query.empty()) message << "\n  SQL: " << query;
            throw sql_error(message.str(), std::string(query));
        }

        /**
         * @brief Waits for an asynchronous OpenAPI operation and validates its status.
         * @param gen Generic completion structure populated by the OpenAPI call.
         * @param operation Human-readable operation name for diagnostics.
         * @param query Optional SQL text to include if the operation fails.
         */
        void wait_complete(IIAPI_GENPARM& gen, std::string_view operation, std::string_view query = {}) {
            while (!gen.gp_completed) {
                IIAPI_WAITPARM wait{};
                wait.wt_timeout = -1;
                IIapi_wait(&wait);
            }
            if (!ok_status(gen.gp_status)) throw_openapi_error(gen, operation, query);
        }

        /**
         * @brief Acquires a shared reference to the process-wide OpenAPI runtime.
         * @return Environment handle used when opening database connections.
         * @throws sql_error if OpenAPI cannot be initialized at a supported version.
         */
        II_PTR acquire_runtime() {
            std::lock_guard lock(runtime_mutex);
            if (runtime_users++ != 0) return runtime_env_handle;

            IIAPI_INITPARM init{};
            // Use the newest OpenAPI interface exposed by the installed iiapi.h.
            // ANSI date/time types (including IIAPI_TSWO_TYPE) require version 5+.
#if defined(IIAPI_VERSION)
            init.in_version = IIAPI_VERSION;
#elif defined(IIAPI_VERSION_7)
            init.in_version = IIAPI_VERSION_7;
#elif defined(IIAPI_VERSION_6)
            init.in_version = IIAPI_VERSION_6;
#elif defined(IIAPI_VERSION_5)
            init.in_version = IIAPI_VERSION_5;
#else
            runtime_users = 0;
            throw sql_error(
                "Ingres OpenAPI client is too old: timestamp without time zone "
                "requires IIAPI_VERSION_5 or newer");
#endif
            init.in_timeout = -1;
            IIapi_initialize(&init);
            if (!ok_status(init.in_status)) {
                runtime_users = 0;
                throw sql_error("IIapi_initialize failed");
            }

            // For OpenAPI version 2+, initialization creates an environment
            // handle. Connections must be opened in that environment; otherwise
            // they may negotiate using the legacy/default API context.
            runtime_env_handle = init.in_envHandle;
            return runtime_env_handle;
        }

        /**
         * @brief Releases one reference to the process-wide OpenAPI runtime.
         *
         * The final user releases the environment and terminates OpenAPI. Errors are
         * intentionally not propagated because this helper is used during cleanup.
         */
        void release_runtime() noexcept {
            std::lock_guard lock(runtime_mutex);
            if (runtime_users == 0 || --runtime_users != 0) return;

            if (runtime_env_handle) {
                IIAPI_RELENVPARM rel{};
                rel.re_envHandle = runtime_env_handle;
                IIapi_releaseEnv(&rel);
                runtime_env_handle = nullptr;
            }

            IIAPI_TERMPARM term{};
            IIapi_terminate(&term);
        }

        /**
         * @brief Removes trailing spaces and NUL padding from fixed-width character data.
         * @param s Decoded character value.
         * @return Trimmed string.
         */
        std::string trim_right(std::string s) {
            while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
            return s;
        }

        /**
         * @brief Formats arbitrary binary data using Ingres-style x'...' notation.
         * @param p Pointer to the first byte.
         * @param n Number of bytes to encode.
         * @return Lower-case hexadecimal string wrapped in x'...'.
         */
        std::string hex_bytes(const unsigned char* p, std::size_t n) {
            std::ostringstream out;
            out << "x'" << std::hex << std::setfill('0');
            for (std::size_t i = 0; i < n; ++i) out << std::setw(2) << static_cast<unsigned>(p[i]);
            out << '\'';
            return out.str();
        }

        /**
         * @brief Safely loads a trivially copyable value from potentially unaligned bytes.
         * @tparam T Destination scalar type.
         * @param ptr Address of the source bytes.
         * @return Value copied from the source buffer.
         */
        template<class T>
        T load_unaligned(const void* ptr) {
            T value{};
            std::memcpy(&value, ptr, sizeof(T));
            return value;
        }

        /**
         * @brief Converts an OpenAPI date/time value to fixed-width character text.
         * @param source_desc Descriptor describing the source OpenAPI datatype.
         * @param source_value Binary value returned by OpenAPI.
         * @return Human-readable text, falling back to hexadecimal bytes if conversion fails.
         */
        std::string convert_to_varchar40(const IIAPI_DESCRIPTOR& source_desc, const IIAPI_DATAVALUE& source_value) {
            // OpenAPI represents ANSI date/time values in binary structures. Convert
            // those values through the API instead of exposing their raw bytes.
            // A 40-byte character target is large enough for Ingres timestamp text,
            // including fractional seconds and a time-zone suffix when present.
            std::array<char, 40> text{};

            IIAPI_CONVERTPARM cv{};
            cv.cv_srcDesc = source_desc;
            cv.cv_srcValue = source_value;

            cv.cv_dstDesc.ds_dataType = IIAPI_CHA_TYPE;
            cv.cv_dstDesc.ds_nullable = FALSE;
            cv.cv_dstDesc.ds_length = static_cast<II_UINT2>(text.size());
            cv.cv_dstDesc.ds_precision = 0;
            cv.cv_dstDesc.ds_scale = 0;
            cv.cv_dstDesc.ds_columnType = source_desc.ds_columnType;
            cv.cv_dstDesc.ds_columnName = nullptr;

            cv.cv_dstValue.dv_null = FALSE;
            cv.cv_dstValue.dv_length = static_cast<II_UINT2>(text.size());
            cv.cv_dstValue.dv_value = text.data();

            IIapi_convertData(&cv);
            if (cv.cv_status != IIAPI_ST_SUCCESS) {
                const auto* bytes = static_cast<const unsigned char*>(source_value.dv_value);
                return hex_bytes(bytes, source_value.dv_length);
            }

            return trim_right(std::string(text.data(), cv.cv_dstValue.dv_length));
        }

        /**
         * @brief Converts one OpenAPI column value to the library's textual representation.
         * @param d Column descriptor identifying the source datatype.
         * @param v Raw value buffer returned by IIapi_getColumns().
         * @return Decoded text; SQL NULL is represented by an empty string and tracked separately.
         */
        std::string decode_value(const IIAPI_DESCRIPTOR& d, const IIAPI_DATAVALUE& v) {
            if (v.dv_null || v.dv_value == nullptr) return {};
            const auto* bytes = static_cast<const unsigned char*>(v.dv_value);
            const std::size_t n = v.dv_length;

            switch (d.ds_dataType) {
            case IIAPI_CHA_TYPE:
            case IIAPI_TXT_TYPE:
            case IIAPI_LTXT_TYPE:
                return trim_right(std::string(reinterpret_cast<const char*>(bytes), n));

            case IIAPI_VCH_TYPE: {
                if (n < sizeof(II_UINT2)) return {};
                const auto len = std::min<std::size_t>(load_unaligned<II_UINT2>(bytes), n - sizeof(II_UINT2));
                return std::string(reinterpret_cast<const char*>(bytes + sizeof(II_UINT2)), len);
            }

            case IIAPI_INT_TYPE:
                switch (n) {
                case 1: return std::to_string(load_unaligned<std::int8_t>(bytes));
                case 2: return std::to_string(load_unaligned<std::int16_t>(bytes));
                case 4: return std::to_string(load_unaligned<std::int32_t>(bytes));
                case 8: return std::to_string(load_unaligned<std::int64_t>(bytes));
                default: return hex_bytes(bytes, n);
                }

            case IIAPI_FLT_TYPE: {
                std::ostringstream out;
                out << std::setprecision(std::numeric_limits<long double>::digits10 + 1);
                if (n == sizeof(float)) out << load_unaligned<float>(bytes);
                else if (n == sizeof(double)) out << load_unaligned<double>(bytes);
                else return hex_bytes(bytes, n);
                return out.str();
            }

            case IIAPI_BOOL_TYPE:
                return (n && bytes[0]) ? "true" : "false";

            case IIAPI_DTE_TYPE:
            case IIAPI_DATE_TYPE:
            case IIAPI_TIME_TYPE:
            case IIAPI_TMWO_TYPE:
            case IIAPI_TMTZ_TYPE:
            case IIAPI_TS_TYPE:
            case IIAPI_TSWO_TYPE:
            case IIAPI_TSTZ_TYPE:
                return convert_to_varchar40(d, v);

            case IIAPI_BYTE_TYPE:
            case IIAPI_VBYTE_TYPE:
            case IIAPI_LBYTE_TYPE:
            case IIAPI_TABKEY_TYPE:
            case IIAPI_LOGKEY_TYPE:
            case IIAPI_UUID_TYPE:
                return hex_bytes(bytes, n);

            default:
                // Decimal, money, intervals, Unicode and newer types still need
                // IIapi_formatData/IIapi_convertData or an explicit CAST in SQL.
                return hex_bytes(bytes, n);
            }
        }

        /**
         * @brief Parses the compact bcbpqxx connection-string syntax.
         * @param text Space-separated target and key=value tokens.
         * @return Structured connection options.
         */
        connection_options parse_connection_string(std::string_view text) {
            connection_options out;
            std::string token;
            std::istringstream input{ std::string(text) };
            while (input >> std::quoted(token)) {
                const auto eq = token.find('=');
                if (eq == std::string::npos) {
                    if (out.target.empty()) out.target = token;
                    continue;
                }
                const auto key = token.substr(0, eq);
                const auto value = token.substr(eq + 1);
                if (key == "target" || key == "database" || key == "dbname") out.target = value;
                else if (key == "user" || key == "username") out.user = value;
                else if (key == "password") out.password = value;
                else if (key == "timeout") out.timeout_seconds = std::stoi(value);
            }
            if (out.target.empty()) out.target = std::string(text);
            return out;
        }

    } // namespace

    // Result storage is shared so lightweight field_ref/row_ref objects can safely
    // outlive temporary iterator expressions without copying every field value.
    struct field_ref::value {
        bool null{ true };
        std::string text;
    };

    struct row_ref::row_data {
        std::vector<std::shared_ptr<const field_ref::value>> fields;
        std::shared_ptr<const std::unordered_map<std::string, std::size_t>> names;
    };

    struct result::result_data {
        std::vector<std::shared_ptr<const row_ref::row_data>> rows;
        std::vector<std::string> columns;
    };

    // Keep OpenAPI-specific handles out of the public header. The negotiated API
    // level is retained here for diagnostics and future capability checks.
    struct connection::impl {
        II_PTR handle{ nullptr };
        II_PTR environment{ nullptr };
        II_LONG api_level{ -1 };
        connection_options options;
        bool runtime_acquired{ false };
    };

    sql_error::sql_error(std::string message, std::string query)
        : std::runtime_error(std::move(message)), query_(std::move(query)) {}
    const std::string& sql_error::query() const noexcept { return query_; }

    field_ref::field_ref(std::shared_ptr<const value> value) : value_(std::move(value)) {}
    bool field_ref::is_null() const noexcept { return !value_ || value_->null; }
    const char* field_ref::c_str() const { return is_null() ? nullptr : value_->text.c_str(); }
    std::string_view field_ref::view() const { return is_null() ? std::string_view{} : std::string_view(value_->text); }

    row_ref::row_ref(std::shared_ptr<const row_data> row) : row_(std::move(row)) {}
    field_ref row_ref::operator[](std::size_t index) const {
        if (!row_ || index >= row_->fields.size()) throw std::out_of_range("column index out of range");
        return field_ref(row_->fields[index]);
    }
    field_ref row_ref::operator[](int index) const {
        if (index < 0) throw std::out_of_range("negative column index");
        return (*this)[static_cast<std::size_t>(index)];
    }
    field_ref row_ref::operator[](std::string_view name) const {
        if (!row_ || !row_->names) throw usage_error("row has no column metadata");
        auto it = row_->names->find(std::string(name));
        if (it == row_->names->end()) throw std::out_of_range("unknown column: " + std::string(name));
        return (*this)[it->second];
    }
    std::size_t row_ref::size() const noexcept { return row_ ? row_->fields.size() : 0; }

    result::result() : data_(std::make_shared<result_data>()) {}
    result::result(std::shared_ptr<result_data> data) : data_(std::move(data)) {}
    std::size_t result::size() const noexcept { return data_ ? data_->rows.size() : 0; }
    bool result::empty() const noexcept { return size() == 0; }
    std::size_t result::columns() const noexcept { return data_ ? data_->columns.size() : 0; }
    row_ref result::operator[](std::size_t index) const {
        if (!data_ || index >= data_->rows.size()) throw std::out_of_range("row index out of range");
        return row_ref(data_->rows[index]);
    }
    result::const_iterator::reference result::const_iterator::operator*() const { return (*owner_)[index_]; }
    result::const_iterator& result::const_iterator::operator++() { ++index_; return *this; }
    result::const_iterator result::const_iterator::operator++(int) { auto copy = *this; ++*this; return copy; }
    result::const_iterator result::begin() const noexcept { return const_iterator(this, 0); }
    result::const_iterator result::end() const noexcept { return const_iterator(this, size()); }

    connection::connection(const connection_options& options) : impl_(std::make_unique<impl>()) {
        // Acquire the shared OpenAPI environment before creating the physical DBMS
        // connection so the connection inherits the requested API capabilities.
        impl_->options = options;
        if (impl_->options.target.empty()) throw usage_error("Ingres target/database is empty");

        impl_->environment = acquire_runtime();
        impl_->runtime_acquired = true;

        IIAPI_CONNPARM p{};
        p.co_target = const_cast<char*>(impl_->options.target.c_str());
        p.co_username = impl_->options.user ? const_cast<char*>(impl_->options.user->c_str()) : nullptr;
        p.co_password = impl_->options.password ? const_cast<char*>(impl_->options.password->c_str()) : nullptr;
        p.co_timeout = impl_->options.timeout_seconds;
        // Pass the environment returned by IIapi_initialize(). This preserves
        // the requested API version when the server connection is negotiated.
        p.co_connHandle = impl_->environment;
        p.co_tranHandle = nullptr;
        // co_type is ignored when co_connHandle is NULL, but becomes mandatory
        // when an environment handle is supplied. Zero is not a valid type.
        p.co_type = IIAPI_CT_SQL;

        // OpenAPI calls complete asynchronously. wait_complete() drives the API
        // until the connection attempt has either completed successfully or failed.
        IIapi_connect(&p);
        try {
            wait_complete(p.co_genParm, "IIapi_connect");
            impl_->handle = p.co_connHandle;
            impl_->api_level = p.co_apiLevel;
        }
        catch (...) {
            if (p.co_connHandle) {
                IIAPI_ABORTPARM abort{};
                abort.ab_connHandle = p.co_connHandle;
                IIapi_abort(&abort);
                try { wait_complete(abort.ab_genParm, "IIapi_abort"); }
                catch (...) {}
            }
            release_runtime();
            impl_->runtime_acquired = false;
            throw;
        }
    }

    connection::connection(std::string_view connection_string)
        : connection(parse_connection_string(connection_string)) {
    }
    connection::~connection() { try { close(); } catch (...) {} }
    connection::connection(connection&&) noexcept = default;
    connection& connection::operator=(connection&&) noexcept = default;
    bool connection::is_open() const noexcept { return impl_ && impl_->handle; }
    void connection::close() {
        if (!impl_) return;
        if (impl_->handle) {
            IIAPI_DISCONNPARM p{};
            p.dc_connHandle = impl_->handle;
            IIapi_disconnect(&p);
            wait_complete(p.dc_genParm, "IIapi_disconnect");
            impl_->handle = nullptr;
        }
        if (impl_->runtime_acquired) {
            release_runtime();
            impl_->runtime_acquired = false;
        }
    }

    work::work(connection& c) : connection_(&c) {
        if (!c.is_open()) throw usage_error("transaction created on a closed connection");
    }
    work::~work() { if (!finished_) { try { abort(); } catch (...) {} } }

    result work::exec(std::string_view query) {
        // A work object has one lifecycle: statements may execute until commit() or
        // abort() marks it finished. Reuse after that point is treated as misuse.
        if (finished_) throw usage_error("transaction is already finished");
        if (!connection_ || !connection_->is_open()) throw usage_error("connection is closed");

        std::string sql(query);
        IIAPI_QUERYPARM qp{};
        qp.qy_connHandle = connection_->impl_->handle;
        qp.qy_queryType = IIAPI_QT_QUERY;
        qp.qy_queryText = sql.data();
        qp.qy_parameters = FALSE;
        qp.qy_tranHandle = transaction_;
        qp.qy_stmtHandle = nullptr;

        // Submitting the SQL may create both a statement handle and the transaction
        // handle. Preserve the latter so commit()/abort() can finish the same unit of work.
        IIapi_query(&qp);
        II_PTR statement = qp.qy_stmtHandle;
        transaction_ = qp.qy_tranHandle;

        // Statement handles are always closed, including exception paths. Cleanup
        // failures are suppressed so they do not hide the original database error.
        auto close_statement = [&]() noexcept {
            if (!statement) return;
            IIAPI_CLOSEPARM cp{};
            cp.cl_stmtHandle = statement;
            IIapi_close(&cp);
            try { wait_complete(cp.cl_genParm, "IIapi_close", sql); }
            catch (...) {}
            statement = nullptr;
            };

        try {
            wait_complete(qp.qy_genParm, "IIapi_query", sql);

            IIAPI_GETDESCRPARM dp{};
            dp.gd_stmtHandle = statement;
            IIapi_getDescriptor(&dp);

            // SELECT-like statements expose a result descriptor. INSERT, UPDATE,
            // DELETE, DDL, and other commands normally do not. For those commands
            // OpenAPI reports IIAPI_ST_NO_DATA from IIapi_getDescriptor; that is a
            // successful command with an empty result, not an execution error.
            while (!dp.gd_genParm.gp_completed) {
                IIAPI_WAITPARM wait{};
                wait.wt_timeout = -1;
                IIapi_wait(&wait);
            }

            auto output = std::make_shared<result::result_data>();
            if (dp.gd_genParm.gp_status == IIAPI_ST_NO_DATA) {
                close_statement();
                return result(std::move(output));
            }
            if (!ok_status(dp.gd_genParm.gp_status)) {
                throw_openapi_error(dp.gd_genParm, "IIapi_getDescriptor", sql);
            }

            // The descriptor tells us how many columns exist, their names, datatypes,
            // and buffer sizes. Build stable name metadata once and share it across rows.
            const std::size_t count = dp.gd_descriptorCount;
            output->columns.reserve(count);
            auto names = std::make_shared<std::unordered_map<std::string, std::size_t>>();
            for (std::size_t i = 0; i < count; ++i) {
                const char* raw = dp.gd_descriptor[i].ds_columnName;
                std::string name = raw ? raw : ("column" + std::to_string(i + 1));
                output->columns.push_back(name);
                names->emplace(name, i);
            }

            if (count > 0) {
                // Allocate one reusable OpenAPI buffer per result column. The buffers
                // are refilled for each fetched row and immediately decoded into strings.
                std::vector<std::vector<unsigned char>> buffers(count);
                std::vector<IIAPI_DATAVALUE> values(count);
                for (std::size_t i = 0; i < count; ++i) {
                    const auto length = static_cast<std::size_t>(dp.gd_descriptor[i].ds_length);
                    buffers[i].resize(std::max<std::size_t>(length, 1));
                    values[i].dv_null = FALSE;
                    values[i].dv_length = static_cast<II_UINT2>(std::min<std::size_t>(length, std::numeric_limits<II_UINT2>::max()));
                    values[i].dv_value = buffers[i].data();
                }

                // Fetch one row at a time. This keeps the OpenAPI interaction simple;
                // each fetched row is copied into the materialized result before reuse.
                while (true) {
                    IIAPI_GETCOLPARM gp{};
                    gp.gc_stmtHandle = statement;
                    gp.gc_rowCount = 1;
                    gp.gc_columnCount = static_cast<II_UINT2>(count);
                    gp.gc_columnData = values.data();
                    IIapi_getColumns(&gp);

                    while (!gp.gc_genParm.gp_completed) {
                        IIAPI_WAITPARM wait{};
                        wait.wt_timeout = -1;
                        IIapi_wait(&wait);
                    }
                    if (gp.gc_genParm.gp_status == IIAPI_ST_NO_DATA || gp.gc_rowsReturned == 0) break;
                    if (!ok_status(gp.gc_genParm.gp_status)) {
                        throw_openapi_error(gp.gc_genParm, "IIapi_getColumns", sql);
                    }

                    auto row = std::make_shared<row_ref::row_data>();
                    row->names = names;
                    row->fields.reserve(count);
                    for (std::size_t i = 0; i < count; ++i) {
                        auto field = std::make_shared<field_ref::value>();
                        field->null = values[i].dv_null != FALSE;
                        if (!field->null) field->text = decode_value(dp.gd_descriptor[i], values[i]);
                        row->fields.push_back(std::move(field));
                    }
                    output->rows.push_back(std::move(row));
                }
            }

            close_statement();
            return result(std::move(output));
        }
        catch (...) {
            close_statement();
            throw;
        }
    }

    void work::commit() {
        // OpenAPI creates the transaction lazily when the first statement executes,
        // so a work object with no statements has no transaction handle to commit.
        if (finished_) return;
        if (transaction_) {
            IIAPI_COMMITPARM p{};
            p.cm_tranHandle = transaction_;
            IIapi_commit(&p);
            wait_complete(p.cm_genParm, "IIapi_commit");
            transaction_ = nullptr;
        }
        finished_ = true;
    }

    void work::abort() {
        // Rollback mirrors commit and is also safe for a work object that never
        // executed a statement (transaction_ remains null in that case).
        if (finished_) return;
        if (transaction_) {
            IIAPI_ROLLBACKPARM p{};
            p.rb_tranHandle = transaction_;
            p.rb_savePointHandle = nullptr;
            IIapi_rollback(&p);
            wait_complete(p.rb_genParm, "IIapi_rollback");
            transaction_ = nullptr;
        }
        finished_ = true;
    }

} // namespace bcbpqxx
#endif  // #ifdef INGRESSQL