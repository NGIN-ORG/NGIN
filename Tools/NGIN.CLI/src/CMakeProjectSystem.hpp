#pragma once

#include "Canonical.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace NGIN::CLI
{
    struct CMakePreset
    {
        std::string name{};
        std::string displayName{};
        std::string description{};
        std::optional<std::string> configurePreset{};
    };

    struct CMakeSource
    {
        std::string path{};
        std::optional<std::string> compileGroup{};
        std::optional<std::string> declaration{};
        std::optional<std::int64_t> declarationLine{};
    };

    struct CMakeCompileGroup
    {
        std::string id{};
        std::string language{};
        std::vector<std::string> compileCommandFragments{};
        std::vector<std::string> includes{};
        std::vector<std::string> defines{};
    };

    struct CMakeTarget
    {
        std::string id{};
        std::string name{};
        std::string type{};
        std::vector<CMakeSource> sources{};
        std::vector<CMakeCompileGroup> compileGroups{};
        std::vector<std::string> dependencies{};
        std::vector<std::string> artifacts{};
        std::optional<std::string> declaration{};
        std::optional<std::int64_t> declarationLine{};
    };

    struct CMakeTest
    {
        std::string name{};
    };

    struct CMakeToolchain
    {
        std::string language{};
        std::string compilerPath{};
        std::string compilerId{};
        std::string compilerVersion{};
        std::string target{};
    };

    struct CMakeProjectSnapshot
    {
        std::string id{};
        std::filesystem::path root{};
        std::string name{};
        std::string configurePreset{};
        std::string configuration{};
        std::filesystem::path buildDirectory{};
        bool configured{false};
        bool stale{false};
        bool multiConfig{false};
        std::vector<std::string> configurations{};
        std::vector<std::string> directories{};
        std::vector<CMakePreset> configurePresets{};
        std::vector<CMakePreset> buildPresets{};
        std::vector<CMakePreset> testPresets{};
        std::vector<CMakeTarget> targets{};
        std::vector<CMakeToolchain> toolchains{};
        std::vector<CMakeTest> tests{};
        std::vector<std::string> diagnostics{};
    };

    struct CMakeOperationRequest
    {
        std::filesystem::path projectRoot{};
        std::optional<std::filesystem::path> workspaceManifest{};
        std::optional<std::string> configurePreset{};
        std::optional<std::string> operationPreset{};
        std::optional<std::string> configuration{};
        std::optional<std::string> target{};
        std::vector<std::string> tests{};
    };

    [[nodiscard]] auto IsCMakeProject(const std::filesystem::path &path) -> bool;
    [[nodiscard]] auto InspectCMakeProject(const CMakeOperationRequest &request) -> CMakeProjectSnapshot;
    [[nodiscard]] auto SerializeCMakeProjectSnapshot(const CMakeProjectSnapshot &snapshot) -> std::string;
    auto ConfigureCMakeProject(const CMakeOperationRequest &request) -> int;
    auto BuildCMakeProject(const CMakeOperationRequest &request) -> int;
    auto TestCMakeProject(const CMakeOperationRequest &request) -> int;
}
