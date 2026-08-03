#include "common_utils.hpp"

#include <optional>
#include <string>
#include <cstdlib>
#include <cstddef>


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
