#pragma once

#include <exception>
#include <string>
#include <utility>

namespace Connectors {
	class ConnectorException : public std::exception {
    private:
        std::string message;

    public:
        explicit ConnectorException(const std::string& msg) : message(msg) {}
        explicit ConnectorException(std::string&& msg) : message(std::move(msg)) {}

        const char* what() const noexcept override {
            return message.c_str();
        }
	};
}
