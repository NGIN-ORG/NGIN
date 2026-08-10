#pragma once

#include "DerivedPlans.hpp"
#include "CMakeIntegration.hpp"

#include <set>

namespace NGIN::CLI
{
    struct CMakeAdapterCapabilities
    {
        bool cxxModules{true};
        bool crossCompilation{true};
        bool multiConfiguration{true};
        std::string adapterVersion{"builtin"};
    };

    struct CMakeAdapterContext
    {
        CMakeAdapterCapabilities capabilities{};
        std::string generator{};
        std::optional<std::string> toolchainFile{};
        bool multiConfiguration{false};
        bool crossCompiling{false};
        std::string projectRoot{};
        std::string buildRoot{};
        std::string actionOutputRoot{};
        std::string actionContextRoot{};
        std::set<ActionKind> actionKinds{ActionKind::Generate};
    };

    struct CMakePlanResult
    {
        std::optional<BuildPlan> build{};
        std::optional<ActionPlan> actions{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool;
    };

    [[nodiscard]] auto DeriveCMakePlans(const ResolvedCompositionGraph &graph,
                                        const ResolvedCMakeIntegrationBindings &bindings,
                                        const CMakeAdapterContext &context = {}) -> CMakePlanResult;
    [[nodiscard]] auto GenerateCMakeProject(const BuildPlan &plan, const ActionPlan &actions) -> std::string;
}
