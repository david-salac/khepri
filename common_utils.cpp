#include "common_utils.hpp"

#include <optional>
#include <string>
#include <cstdlib>
#include <cstddef>
#include <stdexcept>
#include <string_view>


std::optional<std::string> CommonUtils::safe_get_env(std::string_view key) {
#if defined(_MSC_VER) // If compiling with Microsoft Visual Studio
	char* buffer = nullptr;
	size_t size = 0;
	if (_dupenv_s(&buffer, &size, key.data()) == 0 && buffer != nullptr) {
		std::string result(buffer);
		free(buffer);
		return result;
	}
	return std::nullopt;
#else // Standard POSIX/Linux/macOS implementation
	const char* value = std::getenv(key.data());
	if (!value) return std::nullopt;
	return std::string(value);
#endif
}


void CommonUtils::safe_set_env(
    std::string_view name,
    std::string_view value
)
{
    if (name.empty() || name.find('=') != std::string_view::npos) {
        throw std::invalid_argument(
            "Environment variable name cannot be empty or contain '='");
    }

    // Ensure null-terminated storage because string_view does not guarantee it.
    const std::string name_copy{ name };
    const std::string value_copy{ value };

#ifdef _WIN32
    // Copies both strings into the process environment.
    const errno_t result =
        ::_putenv_s(name_copy.c_str(), value_copy.c_str());

    if (result != 0) {
        throw std::runtime_error(
            "_putenv_s failed with error code " +
            std::to_string(result));
    }
#else
    // The final argument means overwrite an existing value.
    if (::setenv(name_copy.c_str(), value_copy.c_str(), 1) != 0) {
        throw std::runtime_error("setenv failed");
    }
#endif
}


void CommonUtils::safe_unset_env(std::string_view name)
{
    if (name.empty() || name.find('=') != std::string_view::npos) {
        throw std::invalid_argument(
            "Environment variable name cannot be empty or contain '='");
    }

    const std::string name_copy{ name };

#ifdef _WIN32
    // On Windows, setting an environment variable to an empty string
    // removes it from the process environment.
    const errno_t result = ::_putenv_s(name_copy.c_str(), "");

    if (result != 0) {
        throw std::runtime_error(
            "_putenv_s failed with error code " +
            std::to_string(result));
    }
#else
    if (::unsetenv(name_copy.c_str()) != 0) {
        throw std::runtime_error("unsetenv failed");
    }
#endif
}
