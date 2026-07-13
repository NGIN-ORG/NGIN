#pragma once

#include "Events.hpp"
#include "Model.hpp"

#include <filesystem>
#include <string>

namespace NGIN::CLI {

[[nodiscard]] auto DeterministicInstallerGuid(const std::string &identifier)
    -> std::string;

[[nodiscard]] auto GenerateCpackPublish(
    const std::filesystem::path &stageDirectory,
    const std::filesystem::path &buildDirectory,
    const std::filesystem::path &publishOutput,
    const PublishDefinition &publish, const ProjectManifest &project,
    CliEventEmitter &events) -> std::filesystem::path;

} // namespace NGIN::CLI
