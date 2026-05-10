#pragma once

#include "Authoring.hpp"
#include "Build.hpp"
#include "Commands.hpp"
#include "Diagnostics.hpp"
#include "Resolution.hpp"
#include "Support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace NGIN::CLI::Tests
{
    namespace fs = std::filesystem;

    class ScopedCurrentPath
    {
    public:
        explicit ScopedCurrentPath(const fs::path &path) : previous_(fs::current_path())
        {
            fs::current_path(path);
        }

        ~ScopedCurrentPath()
        {
            std::error_code error;
            fs::current_path(previous_, error);
        }

    private:
        fs::path previous_{};
    };

    class ScopedEnvironmentVariable
    {
    public:
        ScopedEnvironmentVariable(const std::string &name, const std::string &value) : name_(name)
        {
            if (const auto *existing = std::getenv(name.c_str()); existing != nullptr)
            {
                previous_ = existing;
            }
            setenv(name.c_str(), value.c_str(), 1);
        }

        ~ScopedEnvironmentVariable()
        {
            if (previous_.has_value())
            {
                setenv(name_.c_str(), previous_->c_str(), 1);
            }
            else
            {
                unsetenv(name_.c_str());
            }
        }

    private:
        std::string name_{};
        std::optional<std::string> previous_{};
    };

    class ScopedUnsetEnvironmentVariable
    {
    public:
        explicit ScopedUnsetEnvironmentVariable(const std::string &name) : name_(name)
        {
            if (const auto *existing = std::getenv(name.c_str()); existing != nullptr)
            {
                previous_ = existing;
            }
            unsetenv(name.c_str());
        }

        ~ScopedUnsetEnvironmentVariable()
        {
            if (previous_.has_value())
            {
                setenv(name_.c_str(), previous_->c_str(), 1);
            }
        }

    private:
        std::string name_{};
        std::optional<std::string> previous_{};
    };

    class TempDir
    {
    public:
        TempDir()
        {
            const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
            path_ = fs::temp_directory_path() /
                    ("ngin-cli-tests-" + std::to_string(now) + "-" + std::to_string(std::rand()));
            fs::create_directories(path_);
        }

        ~TempDir()
        {
            std::error_code error;
            fs::remove_all(path_, error);
        }

        [[nodiscard]] auto path() const -> const fs::path &
        {
            return path_;
        }

    private:
        fs::path path_{};
    };

    inline auto WriteFile(const fs::path &path, const std::string &content) -> void
    {
        fs::create_directories(path.parent_path());
        std::ofstream out(path);
        out << content;
    }

    [[nodiscard]] inline auto ReadFile(const fs::path &path) -> std::string
    {
        std::ifstream input(path);
        std::ostringstream content{};
        content << input.rdbuf();
        return content.str();
    }

    inline auto WriteNginPack(
        const fs::path &path,
        const std::string &manifest,
        std::vector<ZipFileEntry> payload = {}) -> void
    {
        payload.push_back(ZipFileEntry{
            .path = "package.nginpkg",
            .contents = manifest,
        });
        WriteZipFile(path, std::move(payload));
    }

    [[nodiscard]] inline auto RepoRoot() -> fs::path
    {
        return fs::path(NGIN_CLI_TEST_REPO_ROOT);
    }

    [[nodiscard]] inline auto DiagnosticMessages(const DiagnosticReport &report) -> std::vector<std::string>
    {
        std::vector<std::string> messages{};
        for (const auto &entry : report.entries)
        {
            messages.push_back(entry.message);
        }
        return messages;
    }
}

namespace fs = std::filesystem;
using namespace Catch::Matchers;
using namespace NGIN::CLI;
using namespace NGIN::CLI::Tests;
