#pragma once

#include "CMakeIntegration.hpp"
#include "CompositionGraph.hpp"
#include "PackageModel.hpp"

#include <map>
#include <optional>
#include <set>
#include <vector>

namespace NGIN::CLI
{
    struct SemanticResolutionRequest
    {
        SemanticProject project{};
        fs::path projectDirectory{};
        fs::path workspaceRoot{};
        SelectionFacts targetSelection{};
        SelectionFacts hostSelection{};
        std::vector<const PackageProvider *> packageProviders{};
        std::map<std::string, std::vector<SourcedVersionConstraint>, std::less<>> centralVersions{};
        std::map<std::string, std::string, std::less<>> packageSourceBindings{};
        std::map<std::string, std::vector<PackageOptionAssignment>, std::less<>> packageOptions{};
        std::set<DependencyContext> dependencyContexts{};
        std::set<ActionKind> actionKinds{ActionKind::Generate};
        bool targetCaseInsensitive{false};
        bool allowSymlinks{false};
        bool providerIntegrityRequired{false};
        bool allowNonHermeticProviders{true};
        bool platformAllowsSideBySidePackages{false};
    };

    struct SemanticResolutionResult
    {
        std::optional<ResolvedCompositionGraph> graph{};
        ResolvedCMakeIntegrationBindings cmakeIntegrations{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool;
    };

    [[nodiscard]] auto ResolveComposition(const SemanticResolutionRequest &request) -> SemanticResolutionResult;
} // namespace NGIN::CLI
