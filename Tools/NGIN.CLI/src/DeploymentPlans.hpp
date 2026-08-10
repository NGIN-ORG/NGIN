#pragma once

#include "DerivedPlans.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace NGIN::CLI
{
    enum class StagePlanItemKind
    {
        ProductArtifact,
        PluginArtifact,
        RuntimeFile,
        Asset,
        Notice,
        Symbol,
        ProjectFile,
    };

    struct StagePlanItem
    {
        std::string identity{};
        StagePlanItemKind kind{StagePlanItemKind::RuntimeFile};
        std::string owner{};
        std::string reason{};
        std::string source{};
        std::string destination{};
        bool symbolicArtifact{false};
        GraphProvenance provenance{};
    };

    struct StagePlan
    {
        PlanIdentity plan{};
        std::string stageRoot{};
        std::vector<StagePlanItem> items{};
    };

    struct StagePlanBindings
    {
        std::filesystem::path projectRoot{};
        std::filesystem::path stageRoot{};
        std::map<std::string, std::filesystem::path, std::less<>> packageRoots{};
        std::map<std::string, std::filesystem::path, std::less<>> productArtifacts{};
        std::map<std::string, std::filesystem::path, std::less<>> pluginArtifacts{};
        std::map<std::string, std::filesystem::path, std::less<>> toolArtifacts{};
        std::map<std::string, std::vector<std::filesystem::path>, std::less<>> symbolArtifacts{};
        std::map<std::string, std::string, std::less<>> allowedReplacements{};
        bool targetCaseInsensitive{false};
    };

    struct LaunchPlan
    {
        PlanIdentity plan{};
        std::string name{};
        std::string executable{};
        bool symbolicExecutable{false};
        std::string workingDirectory{};
        std::vector<std::string> arguments{};
        std::map<std::string, std::string, std::less<>> environment{};
        std::map<std::string, std::string, std::less<>> secretReferences{};
        std::vector<std::string> prerequisites{};
    };

    struct TestPlan
    {
        PlanIdentity plan{};
        std::string executable{};
        bool symbolicExecutable{false};
        std::vector<std::string> arguments{};
        std::optional<std::int64_t> timeoutSeconds{};
        std::vector<std::string> dependencyInstances{};
    };

    struct PublishPlanInput
    {
        std::string stageItem{};
        std::string owner{};
        std::string category{};
        std::string source{};
        std::string destination{};
        std::string reason{};
    };

    struct PublishPlan
    {
        PlanIdentity plan{};
        std::string name{};
        std::string outputKind{};
        std::string format{};
        std::string output{};
        std::optional<std::string> license{};
        std::vector<PublishPlanInput> inputs{};
        std::vector<std::string> dependencyInstances{};
    };

    template <typename T>
    struct DeploymentPlanResult
    {
        std::optional<T> plan{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool { return plan.has_value() && diagnostics.empty(); }
    };

    struct StageExecutionResult
    {
        std::vector<std::filesystem::path> written{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool { return diagnostics.empty(); }
    };

    [[nodiscard]] auto DeriveStagePlan(const ResolvedCompositionGraph &graph, const StagePlanBindings &bindings)
        -> DeploymentPlanResult<StagePlan>;
    [[nodiscard]] auto DeriveLaunchPlan(const ResolvedCompositionGraph &graph, const StagePlan &stage,
                                        const StagePlanBindings &bindings,
                                        std::optional<std::string> launchName = std::nullopt)
        -> DeploymentPlanResult<LaunchPlan>;
    [[nodiscard]] auto DeriveTestPlan(const ResolvedCompositionGraph &graph, const StagePlan &stage)
        -> DeploymentPlanResult<TestPlan>;
    [[nodiscard]] auto DerivePublishPlan(const ResolvedCompositionGraph &graph, const StagePlan &stage,
                                         std::string_view publishName)
        -> DeploymentPlanResult<PublishPlan>;

    [[nodiscard]] auto SerializeStagePlan(const StagePlan &plan) -> std::string;
    [[nodiscard]] auto SerializeLaunchPlan(const LaunchPlan &plan) -> std::string;
    [[nodiscard]] auto SerializeTestPlan(const TestPlan &plan) -> std::string;
    [[nodiscard]] auto SerializePublishPlan(const PublishPlan &plan) -> std::string;
    [[nodiscard]] auto FingerprintStagePlan(const StagePlan &plan) -> std::string;
    [[nodiscard]] auto FingerprintLaunchPlan(const LaunchPlan &plan) -> std::string;
    [[nodiscard]] auto FingerprintTestPlan(const TestPlan &plan) -> std::string;
    [[nodiscard]] auto FingerprintPublishPlan(const PublishPlan &plan) -> std::string;

    [[nodiscard]] auto ExecuteStagePlan(const StagePlan &plan) -> StageExecutionResult;
    [[nodiscard]] auto GenerateCPackConfiguration(const PublishPlan &plan) -> std::string;
}
