#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>
#include <cstddef>
#include <charconv>
#include <system_error>
#include <regex>
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

#include "connectors.hpp"


using namespace Connectors;

// Auxiliary functions
namespace {
	/**
	* Run through std::string and replaces $<pseudosql_function>(column_name) with an appropriate
	*	RDBMs decrypting function.
	* For example: 
	*	$DECRYPT(surname) -> AES_DECRYPT(surname, <password>, 256)
	*	$ENCRYPT(surname) -> AES_ENCRYPT(surname, <password>, 256)
	* @params pseudosql_function accepts only ENCRYPT and DECRYPT values.
	*/
	std::string transform_pseudosql_syntax_using_regex(
		const std::string& input,
		const std::string& password,
		const std::string& pseudosql_function,
		const Connectors::BaseConnector& conn) {
		// Matches $<pseudosql_function>(...) and captures the text inside the parentheses
		// \$<pseudosql_function>\(([^)]+)\)
		//std::regex pattern(R"(\$DECRYPT\(([^)]+)\))");
		std::regex pattern(
			pseudosql_function == "DECRYPT" ? 
			R"(\$DECRYPT\(([^)]+)\))" : 
			R"(\$ENCRYPT\(([^)]+)\))"
		);
		std::smatch match;

		std::string result = input;

		// Search and replace all instances found in the string
		while (std::regex_search(result, match, pattern)) {
			std::string extracted_column = match[1].str();
			
			std::string transformed_value;
			// Call your decryption function
			if (pseudosql_function == "DECRYPT") {
				transformed_value = conn.decrypt_function_expression(extracted_column, password);
			}
			else { // if (pseudosql_function == "ENCRYPT")
				transformed_value = conn.encrypt_function_expression(extracted_column, password);
			}
			// Replace the current match with the decrypted result
			result.replace(match.position(0), match.length(0), transformed_value);
		}
		return result;
	}
};

void Connectors::IngresConnector::connect() {
	if (!is_closed) {
		throw ConnectorException("connection is already open");
	}
	if (connection_string.empty()) {
		throw ConnectorException("connection string is not defined");
	}
	conn.emplace(connection_string);
	is_closed = false;
}

void Connectors::IngresConnector::update(const std::string& query) { 
	bcbpqxx::work tx(*conn);
	tx.exec(query);
	tx.commit();
}

void Connectors::IngresConnector::insert(const std::string& query) { 
	bcbpqxx::work tx(*conn);
	tx.exec(query);
	tx.commit();
}

void Connectors::IngresConnector::dml_delete(const std::string& query) {
	bcbpqxx::work tx(*conn);
	tx.exec(query);
	tx.commit();
}

int Connectors::IngresConnector::next_value_in_sequence(std::string_view sequence_name) {
	bcbpqxx::work tx(conn.value());

	UniversalResult qres(tx.exec(std::format("SELECT NEXT VALUE FOR {} AS sequence_value_extracted", sequence_name)));
	const auto row = qres.begin();
	int value = (*row)["sequence_value_extracted"].as<int>();

	tx.commit();
	return value;
}

std::vector<unsigned char> Connectors::from_hex(std::string_view hex) {
	if (hex.size() % 2 != 0)
		throw std::invalid_argument("from_hex: odd-length input");

	std::vector<unsigned char> bytes;
	bytes.reserve(hex.size() / 2);

	for (std::size_t i = 0; i < hex.size(); i += 2) {
		unsigned char byte{};
		const char* first = hex.data() + i;
		const char* last = first + 2;
		auto [ptr, ec] = std::from_chars(first, last, byte, 16);
		if (ec != std::errc{} || ptr != last)
			throw std::invalid_argument("from_hex: invalid hex digit");
		bytes.push_back(byte);
	}
	return bytes;
}

void Connectors::PostgreSQLConnector::connect() {
	if (!is_closed) {
		throw ConnectorException("connection is already open");
	}
	if (connection_string.empty()) {
		throw ConnectorException("connection string is not defined");
	}
	// Creates connection
	conn.emplace(connection_string);

	if (!conn.value().is_open()) {
		throw ConnectorException("failed to open database");
	}
	is_closed = false;
}

void Connectors::PostgreSQLConnector::update(const std::string& query) { 
	pqxx::work tx{ conn.value() };

	// TODO: Once we have LibPqxx version >= 6, we can replace with:
	// tx.exec(query).no_rows();
	tx.exec(query);

	tx.commit();
}

void Connectors::PostgreSQLConnector::insert(const std::string& query) { 
	pqxx::work tx{ conn.value() };

	// TODO: Once we have LibPqxx version >= 6, we can replace with:
	// tx.exec(query).no_rows();
	tx.exec(query);

	tx.commit();
}

void Connectors::PostgreSQLConnector::dml_delete(const std::string& query) {
	pqxx::work tx{ conn.value() };

	// TODO: Once we have LibPqxx version >= 6, we can replace with:
	// tx.exec(query).no_rows();
	tx.exec(query);

	tx.commit();
}

std::string Connectors::BaseConnector::transform_pseudosql_syntax(
	const std::string& input,
	std::string_view password) const {

	return transform_pseudosql_syntax_using_regex(
		transform_pseudosql_syntax_using_regex(input, 
			password.data(), "ENCRYPT", *this),
		password.data(), "DECRYPT", *this
	);
}

int Connectors::PostgreSQLConnector::next_value_in_sequence(std::string_view sequence_name) {
	pqxx::work tx{ conn.value() };

	UniversalResult qres(tx.exec(std::format("SELECT nextval('{}') AS sequence_value_extracted", sequence_name)));
	const auto row = qres.begin();
	int value = (*row)["sequence_value_extracted"].as<int>();

	tx.commit();

	return value;
}
