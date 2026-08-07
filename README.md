# C++ PostgreSQL / Actian Ingres Connectors

Author: David Salac

A small, copy-friendly C++ library that provides one application-facing API for:

- PostgreSQL through [libpqxx](https://github.com/jtv/libpqxx)
- Actian Ingres through the Ingres OpenAPI (`IIapi`)
- builds where either database SDK is unavailable, using compile-only mock headers

The library is intended to be copied directly into another codebase rather than installed as a separate package.

> Current library version declared by the public header: **1.3.0**

## What it provides

The `Connectors` namespace exposes:

- `PostgreSQLConnector`
- `IngresConnector`
- `BaseConnector`, for code that should work with either backend
- `UniversalResult`, `UniversalRow`, and `UniversalField`, which hide most result-type differences
- SQL literal helpers for strings, nullable values, timestamps, booleans, and binary data
- `$ENCRYPT(...)` and `$DECRYPT(...)` pseudo-SQL expansion
- a PostgreSQL timestamp compatibility layer across libpqxx versions
- a lightweight libpqxx-like wrapper around the Ingres OpenAPI
- mock driver APIs for builds and tests that do not have one or both database SDKs

This is not an ORM. Callers provide SQL strings and consume rows and fields through a common wrapper.

## Repository layout

Copy the following files into one directory in the destination project:

```text
connectors.hpp
connectors.cpp
connectors_exceptions.hpp

connectors_pqxx.hpp
bcbpqxx.hpp
bcbpqxx.cpp

mocked_libpqxx.hpp
mocked_bcbpqxx.hpp
legacy_utils.hpp
```

Optional general-purpose utilities:

```text
common_utils.hpp
common_utils.cpp
```

The supplied source archive may contain files named `connectors(1).hpp` and
`bcbpqxx(1).hpp`. Rename them when copying:

```text
connectors(1).hpp  -> connectors.hpp
bcbpqxx(1).hpp    -> bcbpqxx.hpp
```

The implementation files include the names without `(1)`.

## Requirements

### Language level

The provided build script uses:

```text
C++23
```

The core code relies on modern standard-library features such as `std::optional`,
`std::string_view`, `std::chrono::sys_time`, `std::filesystem`, and
`std::string::starts_with`.

GCC 12 receives a compatibility implementation from `legacy_utils.hpp` for
formatting functionality that is missing from that standard library version.
Newer compilers use the native standard-library implementation.

### PostgreSQL backend

For a real PostgreSQL connection, install:

- PostgreSQL client headers and library (`libpq`)
- libpqxx
- a libpqxx version compatible with the adapters in `connectors_pqxx.hpp`

The compatibility header contains explicit handling for libpqxx 6.x, 7.x, and
8.x-style row/field APIs.

### Ingres backend

For a real Ingres connection, install the Actian Ingres SDK/runtime, including:

- `iiapi.h`
- the Ingres OpenAPI library, commonly `libiiapi.1.so`
- the supporting Ingres runtime libraries required by your installation

Set `II_SYSTEM` to the root of the Ingres installation before building with
Ingres support.

## Compile-time configuration

Backend support is selected through preprocessor definitions.

| Definition | Effect |
|---|---|
| `PGSQL` | Use real libpqxx/PostgreSQL types and connections |
| not `PGSQL` | Use `mocked_libpqxx.hpp` |
| `INGRESSQL` | Use the real Ingres OpenAPI wrapper |
| not `INGRESSQL` | Use `mocked_bcbpqxx.hpp` |

This allows four build configurations:

| Build | Definitions |
|---|---|
| PostgreSQL and Ingres | `-DPGSQL -DINGRESSQL` |
| PostgreSQL only | `-DPGSQL` |
| Ingres only | `-DINGRESSQL` |
| No database SDKs, compile-only mocks | no backend definitions |

The mock backends are intended to keep shared code compilable when a database
SDK is unavailable. They do not execute SQL, persist data, or simulate realistic
query results.

## Quick integration

### 1. Copy the files

Place the library files in a directory such as:

```text
your-project/
├── include/
│   └── db_connectors/
│       ├── connectors.hpp
│       ├── connectors_exceptions.hpp
│       ├── connectors_pqxx.hpp
│       ├── bcbpqxx.hpp
│       ├── mocked_libpqxx.hpp
│       ├── mocked_bcbpqxx.hpp
│       └── legacy_utils.hpp
└── src/
    ├── connectors.cpp
    └── bcbpqxx.cpp
```

The current sources use quoted includes such as `"connectors.hpp"` and
`"bcbpqxx.hpp"`. Either keep all files in one directory or adjust include paths
and include statements to match your project layout.

### 2. Compile the implementation files

Always compile:

```text
connectors.cpp
```

Compile `bcbpqxx.cpp` as well. Its implementation is automatically excluded
unless `INGRESSQL` is defined, so it can normally remain in a shared source list.

### 3. Add the required definition and linker flags

#### PostgreSQL only

```bash
g++ -std=c++23 \
  -DPGSQL \
  -Ipath/to/db_connectors \
  app.cpp connectors.cpp bcbpqxx.cpp \
  -lpqxx -lpq \
  -o app
```

#### Ingres only

The exact supporting library names can differ between Ingres installations.
The supplied project build uses the following form:

```bash
g++ -std=c++23 \
  -DINGRESSQL \
  -Ipath/to/db_connectors \
  -I"$II_SYSTEM/ingres/files" \
  app.cpp connectors.cpp bcbpqxx.cpp \
  -L"$II_SYSTEM/ingres/lib" \
  -Wl,-rpath,"$II_SYSTEM/ingres/lib" \
  -l:libiiapi.1.so \
  -linterp.1 -lframe.1 -lq.1 -lcompat.1 -lm \
  -o app
```

#### PostgreSQL and Ingres

```bash
g++ -std=c++23 \
  -DPGSQL -DINGRESSQL \
  -Ipath/to/db_connectors \
  -I/usr/include \
  -I"$II_SYSTEM/ingres/files" \
  app.cpp connectors.cpp bcbpqxx.cpp \
  -L/usr/lib/x86_64-linux-gnu \
  -L"$II_SYSTEM/ingres/lib" \
  -Wl,-rpath,"$II_SYSTEM/ingres/lib" \
  -lpqxx -lpq \
  -l:libiiapi.1.so \
  -linterp.1 -lframe.1 -lq.1 -lcompat.1 -lm \
  -o app
```

Adapt include paths, library paths, and Ingres library names to the destination
environment.

## CMake example

A minimal copy-integration target can look like this:

```cmake
cmake_minimum_required(VERSION 3.20)
project(example LANGUAGES CXX)

option(CONNECTORS_WITH_POSTGRES "Enable PostgreSQL support" ON)
option(CONNECTORS_WITH_INGRES "Enable Actian Ingres support" OFF)

add_library(db_connectors
    vendor/db_connectors/connectors.cpp
    vendor/db_connectors/bcbpqxx.cpp
)

target_include_directories(db_connectors
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/db_connectors
)

target_compile_features(db_connectors PUBLIC cxx_std_23)

if(CONNECTORS_WITH_POSTGRES)
    find_package(PostgreSQL REQUIRED)

    # Depending on how libpqxx is installed, use its package target or locate
    # the library manually.
    find_package(libpqxx CONFIG REQUIRED)

    target_compile_definitions(db_connectors PUBLIC PGSQL)
    target_link_libraries(db_connectors
        PUBLIC
            pqxx
            PostgreSQL::PostgreSQL
    )
endif()

if(CONNECTORS_WITH_INGRES)
    if(NOT DEFINED ENV{II_SYSTEM})
        message(FATAL_ERROR "II_SYSTEM must be set for Ingres support")
    endif()

    set(II_SYSTEM "$ENV{II_SYSTEM}")
    set(INGRES_INCLUDE_DIR "${II_SYSTEM}/ingres/files")
    set(INGRES_LIBRARY_DIR "${II_SYSTEM}/ingres/lib")

    target_compile_definitions(db_connectors PUBLIC INGRESSQL)
    target_include_directories(db_connectors PUBLIC "${INGRES_INCLUDE_DIR}")
    target_link_directories(db_connectors PUBLIC "${INGRES_LIBRARY_DIR}")

    target_link_libraries(db_connectors
        PUBLIC
            "${INGRES_LIBRARY_DIR}/libiiapi.1.so"
            interp.1
            frame.1
            q.1
            compat.1
            m
    )

    set_target_properties(db_connectors PROPERTIES
        BUILD_RPATH "${INGRES_LIBRARY_DIR}"
    )
endif()

add_executable(example app.cpp)
target_link_libraries(example PRIVATE db_connectors)
```

Package names and imported CMake targets for libpqxx vary by distribution. Adjust
the PostgreSQL section to match the installed package.

## Basic usage

### PostgreSQL

```cpp
#include "connectors.hpp"

#include <iostream>
#include <string>

int main()
{
    Connectors::PostgreSQLConnector db{
        "host=localhost port=5432 dbname=app user=app password=secret",
        "public."
    };

    db.connect();

    db.select(
        "SELECT id, display_name "
        "FROM " + db.get_table_prefix() + "users "
        "ORDER BY id"
    );

    for (const Connectors::UniversalRow& row : db.result()) {
        const int id = row["id"].as<int>();
        const std::string name = row["display_name"].as<std::string>();

        std::cout << id << ": " << name << '\n';
    }

    db.close();
}
```

### Ingres

```cpp
#include "connectors.hpp"

#include <iostream>
#include <string>

int main()
{
    Connectors::IngresConnector db{
        "target=my_vnode::my_database user=my_user password=my_password",
        "app."
    };

    db.connect();

    db.select(
        "SELECT id, display_name "
        "FROM " + db.get_table_prefix() + "users "
        "ORDER BY id"
    );

    for (const Connectors::UniversalRow& row : db.result()) {
        std::cout
            << row["id"].as<int>()
            << ": "
            << row["display_name"].as<std::string>()
            << '\n';
    }

    db.close();
}
```

The Ingres connection-string parser recognises these keys:

```text
target=
database=
dbname=
user=
username=
password=
timeout=
```

A token without `=` is treated as the target when no target has already been
set.

## Writing backend-independent code

Use `BaseConnector&` when application logic should support either backend:

```cpp
#include "connectors.hpp"

#include <iostream>

void print_users(Connectors::BaseConnector& db)
{
    db.select(
        "SELECT id, display_name "
        "FROM " + db.get_table_prefix() + "users "
        "ORDER BY id"
    );

    for (const auto& row : db.result()) {
        std::cout
            << row["id"].as<int>()
            << ": "
            << row["display_name"].as<std::string>()
            << '\n';
    }
}
```

The concrete connector is still responsible for opening and closing the
connection:

```cpp
Connectors::PostgreSQLConnector db{connection_string};
db.connect();
print_users(db);
db.close();
```

## Query operations

The connector interface provides:

```cpp
void connect();
void select(const std::string& query);
void insert(const std::string& query);
void update(const std::string& query);
void dml_delete(const std::string& query);
bool is_empty(const std::string& query);
void close();
```

### `select`

Stores the latest result inside the connector. Retrieve it through:

```cpp
Connectors::UniversalResult& rows = db.result();
```

Calling `result()` before a successful `select()` throws
`Connectors::ConnectorException`.

A later `select()` replaces the stored result. `close()` clears it.

### `insert`, `update` and `dml_delete`

Both execute a statement in a transaction and commit it. They currently have
the same behaviour and are separated to express caller intent.

### `is_empty`

Executes a query and reports whether it returned zero rows:

```cpp
if (db.is_empty("SELECT id FROM users WHERE id = 42")) {
    // No matching row.
}
```

## Reading results

### By column name

```cpp
for (const auto& row : db.result()) {
    auto id = row["id"].as<int>();
    auto name = row["name"].as<std::string>();
}
```

### By column position

```cpp
for (const auto& row : db.result()) {
    auto id = row[0].as<int>();
    auto name = row[1].as<std::string>();
}
```

### Null handling

Check directly:

```cpp
if (!row["deleted_at"].is_null()) {
    // Read the value.
}
```

Or request one of the explicitly supported optional types:

```cpp
auto name = row["name"].as<std::optional<std::string>>();
auto count = row["count"].as<std::optional<int>>();
auto created = row["created_at"].as<std::optional<Connectors::Timestamp>>();
auto payload =
    row["payload"].as<std::optional<std::vector<unsigned char>>>();
```

The unified field adapter includes dedicated handling for:

- `std::optional<std::string>`
- `std::optional<int>`
- `std::optional<Connectors::Timestamp>`
- `std::vector<unsigned char>`
- `std::optional<std::vector<unsigned char>>`

Other non-null values are delegated to the active driver through `as<T>()`.

## Timestamps

The common timestamp type is:

```cpp
using Connectors::Timestamp =
    std::chrono::sys_time<std::chrono::microseconds>;
```

The PostgreSQL compatibility layer:

- parses PostgreSQL timestamp text
- handles fractional seconds
- handles numeric UTC offsets
- normalises offset timestamps to UTC
- maps PostgreSQL `infinity` and `-infinity` to the timestamp type's extrema

The Ingres wrapper accepts date/time text such as:

```text
2026-07-31 13:30:45
2026-07-31T13:30:45.123456
2026-07-31T13:30:45.123456Z
2026-07-31T13:30:45.123456+01:00
```

## Binary values

Convert bytes to lowercase hexadecimal:

```cpp
std::vector<unsigned char> bytes{0x01, 0xA0, 0xFF};
std::string hex = Connectors::to_hex(bytes);
// "01a0ff"
```

Convert hexadecimal text back to bytes:

```cpp
auto bytes = Connectors::from_hex("01a0ff");
```

`from_hex` throws `std::invalid_argument` for odd-length input or invalid
hexadecimal characters.

Generate a backend-specific SQL literal through the connector:

```cpp
std::string literal = db.to_sql_binary_literal(bytes);
```

The generated forms are:

```text
PostgreSQL: '\x01a0ff'
Ingres:     x'01a0ff'
```

Fetched values beginning with PostgreSQL's `\x` prefix or Ingres's `x` prefix
can be converted with:

```cpp
auto bytes = Connectors::string_byte_array_to_vector(text);
```

## SQL value helpers

`BaseConnector` exposes backend-aware formatting helpers:

```cpp
db.to_null();
db.to_sql_now();
db.to_sql_string(std::string{"O'Brien"});
db.to_sql_string(std::optional<std::string>{});
db.to_sql_string(std::optional<int>{42});
db.bool_to_sql_string(std::optional<bool>{true});
db.to_sql_string(binary_bytes);
db.to_sql_string(optional_timestamp);
```

Examples:

```text
NULL
NOW()                    -- PostgreSQL
current_timestamp()      -- Ingres
'O''Brien'
TRUE
```

These helpers are useful when maintaining existing string-built SQL, but
parameterised queries should be preferred for untrusted values.

## Pseudo-SQL encryption expressions

The library can translate portable placeholders into backend-specific SQL:

```cpp
std::string sql =
    "SELECT $DECRYPT(secret_value) "
    "FROM records "
    "WHERE encrypted_key = $ENCRYPT('lookup-key')";

sql = db.transform_pseudosql_syntax(sql, encryption_password);
```

Generated expressions:

| Placeholder | PostgreSQL | Ingres |
|---|---|---|
| `$DECRYPT(value)` | `pgp_sym_decrypt(value, 'password')` | `AES_DECRYPT(value, 'password', 256)` |
| `$ENCRYPT(value)` | `pgp_sym_encrypt(value, 'password')` | `AES_ENCRYPT(value, 'password', 256)` |

For PostgreSQL, these functions normally require the `pgcrypto` extension:

```sql
CREATE EXTENSION IF NOT EXISTS pgcrypto;
```

The transformation is regex-based and is not a SQL parser. It matches text up
to the next closing parenthesis and therefore is best suited to simple column
names and expressions.

Do not log transformed SQL when it contains encryption passwords.

## Table prefixes

Both concrete connector constructors accept an optional table prefix:

```cpp
Connectors::PostgreSQLConnector db{connection_string, "public."};
Connectors::IngresConnector db{connection_string, "application."};
```

Retrieve it with:

```cpp
db.get_table_prefix()
```

The method returns an empty string when no prefix was configured.

The prefix is not inserted automatically. Concatenate it into queries where
needed.

## Error handling

Connector-level misuse and connection failures may throw:

```cpp
Connectors::ConnectorException
```

The Ingres compatibility layer also defines:

```cpp
bcbpqxx::usage_error
bcbpqxx::sql_error
```

`bcbpqxx::sql_error` retains the SQL query associated with the failure:

```cpp
try {
    db.select("SELECT * FROM missing_table");
}
catch (const bcbpqxx::sql_error& error) {
    std::cerr << error.what() << '\n';
    std::cerr << "Query: " << error.query() << '\n';
}
```

libpqxx exceptions are not translated into `ConnectorException`; callers may
also need to catch the relevant `pqxx` exception types.

## Connection lifetime

Recommended usage is explicit:

```cpp
db.connect();

try {
    // Database work.
    db.close();
}
catch (...) {
    db.close();
    throw;
}
```

The concrete connector destructors also close an open connector. The Ingres
connection and transaction wrappers use RAII, and an unfinished Ingres
transaction is rolled back by its destructor.

Calling `connect()` twice without closing throws `ConnectorException`.

## Mock builds

When a backend definition is absent, the matching mock header is included
automatically.

Example compile-only build with neither SDK installed:

```bash
g++ -std=c++23 \
  -Ipath/to/db_connectors \
  app.cpp connectors.cpp bcbpqxx.cpp \
  -o app
```

Important mock behaviour:

- connections report as open
- `exec()` returns an empty result
- fields return null/default values
- commits and rollbacks do nothing
- no SQL is validated or executed

Use the mocks to compile shared business logic or lightweight tests. Do not use
them as behavioural database substitutes.

## Optional `CommonUtils`

`common_utils.hpp/.cpp` are separate from the database connector API and may be
omitted.

They provide:

### Command-line parsing

```cpp
CommonUtils::CommandLine args({
    "--config", "settings.json",
    "--verbose"
});

args.has_flag("verbose"); // true
args.get("config");       // optional containing "settings.json"
```

Only `--name value` and `--flag` forms are parsed. Positional arguments are
retained for iteration but otherwise ignored.

### Environment variables

```cpp
auto value = CommonUtils::safe_get_env("DATABASE_URL");
```

This returns `std::nullopt` when the variable is absent and uses the appropriate
Windows or POSIX implementation.

### Directory cleanup

```cpp
CommonUtils::remove_folder_content("/tmp/application-cache");
```

This removes every entry inside the directory but leaves the directory itself
in place.

## Security notes

### Prefer parameters for external input

The current public connector API primarily accepts complete SQL strings.
`to_sql_string()` escapes single quotes, but building SQL manually remains more
error-prone than binding parameters.

Do not concatenate raw user input, identifiers, passwords, or externally
controlled values into SQL.

For security-sensitive code, consider extending each connector with a
parameterised execution API and keeping SQL structure separate from values.

### Protect credentials

Avoid hard-coding connection strings and encryption passwords. Load them from a
secret manager, protected configuration, or environment variables.

### Pseudo-SQL embeds the password

`transform_pseudosql_syntax()` writes the encryption password directly into the
resulting SQL expression. This can expose it through logs, tracing, database
monitoring, exception messages, or process diagnostics.

### Identifiers are not quoted

`table_prefix`, column expressions passed to the pseudo-SQL helpers, and other
SQL identifiers are inserted as provided. Treat them as trusted configuration,
not user input.

## Known limitations

- The API stores only the most recent `select()` result per connector.
- The connector objects are not documented as thread-safe.
- Query parameters and prepared statements are not exposed by the common API.
- Insert/update affected-row counts are not returned.
- PostgreSQL and Ingres driver exceptions are not normalised into one hierarchy.
- `UniversalField::as<T>()` has explicit optional handling for only a small set
  of types.
- The Ingres decoder converts common character, integer, floating-point,
  boolean, and binary types. Other OpenAPI types may be returned as hexadecimal
  bytes unless explicitly cast in SQL or additional conversion logic is added.
- The Ingres `nontransaction` name is a compatibility alias; OpenAPI operations
  still create a transaction.
- The pseudo-SQL transformer is regex-based and does not understand nested SQL
  expressions.
- The provided `compile.sh` compiles every `.cpp` file in its working directory
  into an application named `bin/khepri`; it is an example project build, not a
  general library installer.
- The code does not currently declare a licence. Add one before redistributing
  it between independent projects or organisations.

## Suggested vendoring policy

When copying the library into another repository:

1. Keep all connector files together in a clearly named `vendor/` or
   `third_party/` directory.
2. Record the copied version and source commit in this README or a small
   `VERSION` file.
3. Keep local modifications in focused commits.
4. When upstream changes are copied in, review the backend compatibility code
   and rerun builds for all four macro configurations.
5. Add a licence file or an explicit internal-use notice.

Example provenance block:

```text
Vendored component: db_connectors
Library version: 1.2.0
Source revision: <commit or tag>
Copied on: <YYYY-MM-DD>
Local changes: <summary or "none">
```

## Recommended validation matrix

At minimum, compile these configurations:

```text
1. no definitions
2. PGSQL
3. INGRESSQL
4. PGSQL + INGRESSQL
```

For real-driver builds, add integration tests that cover:

- connecting and closing
- a zero-row `SELECT`
- reading rows by name and position
- null strings, integers, timestamps, and binary values
- insert/update commit behaviour
- transaction rollback on exceptions
- timestamp offsets and fractional seconds
- binary round trips
- encryption expression generation
- invalid credentials and invalid SQL

## Example project build script

The supplied `compile.sh` expects:

```bash
export II_SYSTEM=/path/to/ingres
./compile.sh
```

It:

- uses `g++` unless `CXX` is set
- compiles in C++23 mode
- enables both `PGSQL` and `INGRESSQL`
- compiles all `.cpp` files in the current directory
- links libpqxx, libpq, and the Ingres libraries
- writes the executable to `bin/khepri`

Review and adapt its hard-coded PostgreSQL include/library paths before copying
it to a different distribution or architecture.
