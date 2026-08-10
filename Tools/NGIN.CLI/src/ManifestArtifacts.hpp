#pragma once

#include "ManifestSpec.hpp"

#include <filesystem>
#include <map>
#include <string>

namespace NGIN::CLI
{
    [[nodiscard]] auto GenerateManifestArtifacts(const ManifestSpec &spec = CurrentManifestSpec())
        -> std::map<std::string, std::string>;
    auto WriteManifestArtifacts(const std::filesystem::path &directory,
                                const ManifestSpec &spec = CurrentManifestSpec()) -> void;
}
