#pragma once

#include "CompositionGraph.hpp"

namespace NGIN::CLI
{
    struct PlanIdentity
    {
        std::string kind{};
        std::string compositionIdentity{};
        std::string adapter{};
        std::string adapterVersion{};
        std::string identity{};
    };

    struct BuildPlanItem
    {
        std::string identity{};
        std::string graphIdentity{};
        std::string operation{};
        std::string value{};
        std::string visibility{};
        bool generated{false};
        GraphProvenance provenance{};
    };

    struct BuildPlanLink
    {
        std::string identity{};
        std::string graphIdentity{};
        std::string targetName{};
        std::string visibility{};
        GraphProvenance provenance{};
    };

    struct CMakePackagePlan
    {
        std::string identity{};
        std::string packageInstance{};
        CMakeIntegrationKind kind{CMakeIntegrationKind::AddSubdirectory};
        std::string source{};
        std::string binaryDirectory{};
        std::string installedPrefix{};
        std::vector<CMakeCacheBinding> cache{};
        std::optional<CMakeFindPackageBinding> findPackage{};
        bool installBeforeUse{false};
        IntegrationBindingProvenance provenance{};
    };

    struct BuildPlan
    {
        PlanIdentity plan{};
        std::string productGraphIdentity{};
        std::string targetName{};
        std::string targetKind{};
        std::string configuration{};
        std::string languageStandard{"C++23"};
        bool languageExtensions{false};
        bool languageRequired{true};
        std::string generator{};
        std::optional<std::string> toolchainFile{};
        bool multiConfiguration{false};
        bool crossCompiling{false};
        std::vector<BuildPlanItem> items{};
        std::vector<BuildPlanLink> links{};
        std::vector<CMakePackagePlan> packages{};
        std::vector<std::string> actionDependencies{};
    };

    struct ActionPlanStep
    {
        std::string identity{};
        std::string graphIdentity{};
        ActionKind kind{ActionKind::Custom};
        std::string toolGraphIdentity{};
        std::string toolTarget{};
        bool deterministic{false};
        std::vector<std::string> inputs{};
        std::vector<std::string> outputs{};
        std::vector<std::string> arguments{};
        std::string workingDirectory{};
        std::map<std::string, std::string, std::less<>> environment{};
        std::map<std::string, std::string, std::less<>> options{};
        std::string contextFile{};
        GraphProvenance provenance{};
    };

    struct ActionPlan
    {
        PlanIdentity plan{};
        std::vector<ActionPlanStep> steps{};
    };

    [[nodiscard]] auto SerializeBuildPlan(const BuildPlan &plan) -> std::string;
    [[nodiscard]] auto SerializeActionPlan(const ActionPlan &plan) -> std::string;
    [[nodiscard]] auto FingerprintBuildPlan(const BuildPlan &plan) -> std::string;
    [[nodiscard]] auto FingerprintActionPlan(const ActionPlan &plan) -> std::string;
}
