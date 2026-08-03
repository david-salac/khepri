#include "bcbpqxx.hpp"
#include "connectors.hpp"

#include <chrono>
#if defined(__GNUC__) && (__GNUC__ == 12)
// To support legacy GNU compiler (version 12), should be removed with version 14
#include "legacy_utils.hpp"
#else
#endif
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>


using namespace Connectors;


static std::string quote_sql(const std::string& value)
{
    std::string quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back('\'');

    for (const char ch : value)
    {
        if (ch == '\'')
            quoted += "''";
        else
            quoted.push_back(ch);
    }

    quoted.push_back('\'');
    return quoted;
}

int main(int argc, char* argv[])
{
    if (argc > 2)
    {
        std::cerr
            << "Usage:\n"
            << "  " << argv[0] << " database\n";

        return EXIT_FAILURE;
    }

    const std::string database = argv[1];

    try
    {
        IngresConnector connection(database);

        std::cout << "Connected to " << database << '\n';
        connection.connect();

        constexpr int test_id = 1001;

        const std::string initial_description =
            "OpenAPI demo row";

        const long long initial_large_value =
            8'000'000LL;

        const Timestamp initial_time =
            std::chrono::sys_days{
                std::chrono::year{2026} /
                std::chrono::August /
                std::chrono::day{3}
            } +
            std::chrono::hours{10} +
            std::chrono::minutes{30} +
            std::chrono::seconds{15} +
            std::chrono::microseconds{123456};

        /*
         * Clean up a previous demo run.
         *
         * This makes the example repeatable even when id is not declared
         * as a primary key.
         */
        {
            connection.dml_delete(
                "DELETE FROM openapi_demo "
                "WHERE id = " +
                std::to_string(test_id));
            std::cout << "Removed any previous demo row.\n";
        }

        /*
         * INSERT
         *
         * The current wrapper does not yet implement bound parameters,
         * so the example constructs the SQL statement explicitly.
         */
        {
            std::stringstream str;
            str << initial_time;
            const std::string timestamp_text = str.str();

            const std::string insert_sql =
                "INSERT INTO openapi_demo "
                "(id, description, large_value, event_time) "
                "VALUES (" +
                std::to_string(test_id) + ", " +
                quote_sql(initial_description) + ", " +
                std::to_string(initial_large_value) + ", " +
                "TIMESTAMP " + quote_sql(timestamp_text) +
                ")";
            connection.insert(insert_sql);

            std::cout << "Inserted demo row.\n";
        }

        /*
         * INSERT WITH ERROR
         *
         * We expect this statement to fail and print error.
         */
        try {
            std::stringstream str;
            str << initial_time;
            const std::string timestamp_text = str.str();

            const std::string insert_sql =
                "INSERT INTO openapi_demo_non_existant "
                "(id, description, large_value, event_time) "
                "VALUES (" +
                std::to_string(test_id) + ", " +
                quote_sql(initial_description) + ", " +
                std::to_string(initial_large_value) + ", " +
                "TIMESTAMP " + quote_sql(timestamp_text) +
                ")";
            connection.insert(insert_sql);

            std::cout << "Inserted demo row.\n";
        }
        catch (const std::exception& ex) {
            std::cout << "GOOD, WE EXPECT ERROR HERE!\n";
            std::cerr << ex.what();
        }

        /*
         * SELECT after INSERT.
         *
         * event_time is cast to VARCHAR because the current wrapper's
         * timestamp conversion expects a textual timestamp.
         */
        {
            const std::string select_sql =
                "SELECT "
                "id, "
                "description, "
                "large_value, "
                "CAST(event_time AS VARCHAR(40)) "
                    "AS event_time_text "
                "FROM openapi_demo "
                "WHERE id = " +
                std::to_string(test_id);

            connection.select(select_sql);

            for (auto && row : connection.result()) {
                const int id =
                row["id"].as<int>();

                const std::string description =
                    row["description"].as<std::string>();

                const long long large_value =
                    row["large_value"].as<long long>();

                const Timestamp event_time =
                    row["event_time_text"].as<Timestamp>();

                std::cout
                    << "id=" << id
                    << ", description=" << description
                    << ", large_value=" << large_value
                    << ", event_time=" << event_time;

                std::cout << '\n';
            }
        }

        /*
         * UPDATE
         */
        const std::string updated_description =
            "Updated through Ingres OpenAPI";

        const long long updated_large_value =
            9'223'036LL;

        const Timestamp updated_time =
            initial_time +
            std::chrono::hours{2} +
            std::chrono::minutes{15};
        
        std::stringstream str;
            str << updated_time;
            const std::string updated_time_str = str.str();

        {
            const std::string update_sql =
                "UPDATE openapi_demo SET "
                "description = " +
                quote_sql(updated_description) + ", "
                "large_value = " +
                std::to_string(updated_large_value) + ", "
                "event_time = TIMESTAMP " +
                quote_sql(updated_time_str) + " "
                "WHERE id = " +
                std::to_string(test_id);

            connection.update(update_sql);

            std::cout << "\nUpdated demo row.\n";
        }

        /*
         * SELECT after UPDATE
         */
        {
            const std::string select_sql =
                "SELECT "
                "id, "
                "description, "
                "large_value, "
                "CAST(event_time AS VARCHAR(40)) "
                    "AS event_time_text "
                "FROM openapi_demo "
                "WHERE id = " +
                std::to_string(test_id);

            connection.select(select_sql);

            for (auto && row : connection.result()) {
                const int id =
                row["id"].as<int>();

                const std::string description =
                    row["description"].as<std::string>();

                const long long large_value =
                    row["large_value"].as<long long>();

                const Timestamp event_time =
                    row["event_time_text"].as<Timestamp>();

                std::cout
                    << "id=" << id
                    << ", description=" << description
                    << ", large_value=" << large_value
                    << ", event_time=" << event_time;

                std::cout << '\n';
            }
        }

        connection.close();

        std::cout << "\nDemo completed successfully.\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr
            << "Error: " << error.what() << '\n';

        return EXIT_FAILURE;
    }
}