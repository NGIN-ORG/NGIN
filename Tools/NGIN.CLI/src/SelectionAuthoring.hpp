#pragma once

#include "Selection.hpp"

namespace NGIN::CLI
{
    struct WorkspaceSelectionModel
    {
        std::vector<Configuration> configurations{};
        std::vector<Target> targets{};
        std::vector<Toolchain> toolchains{};
        SelectionRequest defaults{};
        std::vector<Preset> presets{};
    };

    struct WorkspaceSelectionResult
    {
        std::optional<WorkspaceSelectionModel> value{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool;
    };

    [[nodiscard]] auto ParseWorkspaceSelection(const AuthoredWorkspaceManifest &workspace)
        -> WorkspaceSelectionResult;
}
