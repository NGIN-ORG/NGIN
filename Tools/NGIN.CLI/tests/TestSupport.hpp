#pragma once

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

    inline auto SetEnvironmentVariableForTest(const std::string &name, const std::string &value) -> void
    {
#if defined(_WIN32)
        (void)_putenv_s(name.c_str(), value.c_str());
#else
        (void)setenv(name.c_str(), value.c_str(), 1);
#endif
    }

    inline auto UnsetEnvironmentVariableForTest(const std::string &name) -> void
    {
#if defined(_WIN32)
        (void)_putenv_s(name.c_str(), "");
#else
        (void)unsetenv(name.c_str());
#endif
    }

    [[nodiscard]] inline auto GetEnvironmentVariableForTest(const std::string &name) -> std::optional<std::string>
    {
#if defined(_WIN32)
        char *value = nullptr;
        std::size_t size = 0;
        if (_dupenv_s(&value, &size, name.c_str()) != 0 || value == nullptr)
        {
            return std::nullopt;
        }
        std::string result{value};
        std::free(value);
        return result;
#else
        if (const auto *value = std::getenv(name.c_str()); value != nullptr)
        {
            return std::string{value};
        }
        return std::nullopt;
#endif
    }

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
            previous_ = GetEnvironmentVariableForTest(name);
            SetEnvironmentVariableForTest(name, value);
        }

        ~ScopedEnvironmentVariable()
        {
            if (previous_.has_value())
            {
                SetEnvironmentVariableForTest(name_, *previous_);
            }
            else
            {
                UnsetEnvironmentVariableForTest(name_);
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
            previous_ = GetEnvironmentVariableForTest(name);
            UnsetEnvironmentVariableForTest(name);
        }

        ~ScopedUnsetEnvironmentVariable()
        {
            if (previous_.has_value())
            {
                SetEnvironmentVariableForTest(name_, *previous_);
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

            std::error_code error;
            const auto canonical = fs::weakly_canonical(path_, error);
            if (!error)
            {
                path_ = canonical;
            }
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

    [[nodiscard]] inline auto PlatformExecutableName(const std::string &name) -> std::string
    {
#if defined(_WIN32)
        return name + ".exe";
#else
        return name;
#endif
    }

    [[nodiscard]] inline auto RepoRoot() -> fs::path
    {
        return fs::path(NGIN_CLI_TEST_REPO_ROOT);
    }

}

namespace fs = std::filesystem;
using namespace Catch::Matchers;
using namespace NGIN::CLI;
using namespace NGIN::CLI::Tests;
