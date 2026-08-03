#pragma once

#include <exception>
#include <string>
#include <utility>
#include <optional>
#include <map>
#include <vector>
#include <filesystem>
#include <string_view>


/**
* Generic application utilities needed across various solutions. 
*   Version: 1.0.0
*/
namespace CommonUtils {
    /**
    * Parses command line arguments
    */
    class CommandLine {
    public:
        using iterator = std::vector<std::string>::const_iterator;

        explicit CommandLine(std::vector<std::string> argv)
            : argv_(std::move(argv)) {
            parse();
        }

        // ----- parsed option API -----

        bool has_flag(std::string_view name) const {
            return options_.contains(std::string{ name });
        }

        std::optional<std::string> get(std::string_view name) const {
            auto it = options_.find(std::string{ name });
            if (it == options_.end()) {
                return std::nullopt;
            }
            return it->second;
        }

        // ----- raw argv iteration API -----

        iterator begin() const noexcept {
            return argv_.begin();
        }

        iterator end() const noexcept {
            return argv_.end();
        }

    private:
        void parse() {
            for (std::size_t i = 0; i < argv_.size(); ++i) {
                const std::string& arg = argv_[i];

                if (!arg.starts_with("--")) {
                    continue; // ignore positional args for now
                }

                std::string key = arg.substr(2);

                if (i + 1 < argv_.size() && !argv_[i + 1].starts_with("--")) {
                    options_[key] = argv_[i + 1];
                    ++i; // consume value
                }
                else {
                    options_[key] = ""; // flag
                }
            }
        }

        std::vector<std::string> argv_;
        std::map<std::string, std::string> options_;
    };

    /**
    * Clean-up function that remove whole content of a given folder, if it exists.
    */
    inline void remove_folder_content(std::filesystem::path folder) {
        // Check if the folder exists
        if (std::filesystem::exists(folder) && std::filesystem::is_directory(folder)) {
            // Remove all contents inside the folder
            for (const auto& entry : std::filesystem::directory_iterator(folder)) {
                std::filesystem::remove_all(entry.path());
            }
        }
    }

    /**
    * Provides a safe way to retrieve environment variable values for both Unix and Windows systems.
    */
    std::optional<std::string> safe_get_env(std::string_view key);
};
