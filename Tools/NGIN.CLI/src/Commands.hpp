#pragma once

#include "Model.hpp"

namespace NGIN::CLI
{
    struct ParsedArgs
    {
        std::optional<std::string> projectPath{};
        std::optional<std::string> configurationName{};
        std::optional<std::string> outputPath{};
        std::optional<std::string> targetDir{};
        std::optional<std::string> packageName{};
        std::vector<std::string> runArgs{};
    };

    [[nodiscard]] auto ParseCommonArgs(int argc, char **argv, int startIndex) -> ParsedArgs;

    auto CmdList(const fs::path &root) -> int;
    auto CmdStatus(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdDoctor(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdPackageList(const fs::path &root) -> int;
    auto CmdPackageShow(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdValidate(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdGraph(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdBuild(const fs::path &root, const ParsedArgs &args) -> int;
    auto CmdRun(const fs::path &root, const ParsedArgs &args) -> int;
}
