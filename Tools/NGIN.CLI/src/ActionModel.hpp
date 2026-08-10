#pragma once

#include "PackageModel.hpp"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace NGIN::CLI
{
    enum class ActionTrustDecision
    {
        Allow,
        Deny,
        Confirm,
    };

    struct ActionTrustRule
    {
        std::optional<std::string> package{};
        std::optional<ActionKind> kind{};
        std::optional<std::string> providerKind{};
        std::optional<std::string> sourceBinding{};
        std::optional<std::string> trust{};
        std::optional<std::string> signature{};
        std::optional<PortablePath> executableOrigin{};
        ActionTrustDecision decision{ActionTrustDecision::Deny};
        std::string reason{};
        ManifestSourceRange source{};
    };

    struct ActionTrustPolicy
    {
        ActionTrustDecision defaultDecision{ActionTrustDecision::Deny};
        bool requireLocked{false};
        bool requireIntegrity{false};
        bool requireSignature{false};
        std::vector<ActionTrustRule> rules{};
    };

    struct ActionExecutionContext
    {
        bool locked{false};
        bool nonInteractive{false};
        std::optional<PortablePath> executableOrigin{};
    };

    struct ActionTrustExplanation
    {
        ActionTrustDecision decision{ActionTrustDecision::Deny};
        std::string qualifiedAction{};
        std::string toolExport{};
        std::string packageInstance{};
        std::string providerKind{};
        std::string providerIdentity{};
        std::optional<std::string> executableOrigin{};
        std::string reason{};
        std::optional<ManifestSourceRange> matchedRule{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Allowed() const -> bool;
    };

    struct ResolvedAction
    {
        std::string qualifiedAction{};
        ActionKind kind{ActionKind::Custom};
        PackageInstance hostInstance{};
        std::string actionExport{};
        std::string toolExport{};
        SemanticActionContract contract{};
        std::vector<ProjectActionInput> inputs{};
        std::map<std::string, std::string, std::less<>> options{};
        std::vector<std::string> arguments{};
        std::vector<BuildItemDeclaration> generatedItems{};
        std::vector<std::string> activatedExports{};
        std::vector<SemanticRequirement> requirements{};
        std::vector<PackageContribution> contributions{};
        std::vector<CapabilityImplementation> capabilities{};
        ManifestSourceRange source{};
    };

    struct ResolvedActionResult
    {
        std::optional<ResolvedAction> value{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool;
    };

    [[nodiscard]] auto ResolveActionSelection(const ProjectActionSelection &selection,
                                              const SemanticPackage &package,
                                              const PackageProviderResult &provider,
                                              const BinaryCompatibility &hostCompatibility,
                                              const ResolvedPackageOptions &options) -> ResolvedActionResult;
    [[nodiscard]] auto EvaluateActionTrust(const ResolvedAction &action, const ActionTrustPolicy &policy,
                                           const ActionExecutionContext &context) -> ActionTrustExplanation;
    [[nodiscard]] auto ParseActionTrustPolicy(const AuthoredWorkspaceManifest &workspace,
                                              std::vector<ManifestDiagnostic> &diagnostics) -> ActionTrustPolicy;
    [[nodiscard]] auto ValidateActionOutputCollisions(std::span<const ResolvedAction> actions)
        -> std::vector<ManifestDiagnostic>;
    [[nodiscard]] auto ValidateObservedActionOutputs(const ResolvedAction &action,
                                                     std::span<const PortablePath> observedOutputs)
        -> std::vector<ManifestDiagnostic>;
}
