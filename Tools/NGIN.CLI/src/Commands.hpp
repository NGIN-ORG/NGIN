#pragma once

#include "Model.hpp"

namespace NGIN::CLI
{
    struct ParsedArgs
    {
        std::optional<std::string> projectPath{};
        std::optional<std::string> profileName{};
        std::optional<std::string> fromProfileName{};
        std::optional<std::string> toProfileName{};
        std::optional<std::string> outputPath{};
        std::optional<std::string> targetDir{};
        std::optional<std::string> lockPath{};
        std::optional<std::string> fromLockPath{};
        std::optional<std::string> toLockPath{};
        std::optional<std::string> format{};
        std::optional<std::string> packageName{};
        std::optional<std::string> featureName{};
        std::optional<std::string> versionRange{};
        std::optional<std::string> scope{};
        std::optional<std::string> graphPlan{};
        bool locked{false};
        std::vector<std::string> runArgs{};
    };

    [[nodiscard]] auto ParseCommonArgs(int argc, char **argv, int startIndex) -> ParsedArgs;

    auto CmdList(const fs::path &root) -> int;
    auto CmdStatus(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdDoctor(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdPackageList(const fs::path &root) -> int;
    auto CmdPackageShow(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdPackageSourcesList(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdPackageSourcesAdd(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdPackageSourcesRemove(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdPackageAdd(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdProjectReferenceAdd(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdPackageRemove(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdPackageUpdate(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdPackagePack(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdPackageLock(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdPackageVerifyLock(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdRestore(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdSettingsInit(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdVariablesExplain(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdExplainCondition(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdExplainPackageFeature(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdExplainGenerator(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdExplainObject(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdNew(const fs::path &root, const std::string &kind, const std::string &name) -> int;
    auto CmdInspect(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdValidate(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdGraph(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdDiff(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdFormat(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdSchema(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdClean(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdConfigure(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdBuild(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdStage(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdRebuild(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdRun(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdTest(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdBenchmark(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdAnalyze(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdPublish(const fs::path &root, const ParsedArgs &args) -> int;
}
