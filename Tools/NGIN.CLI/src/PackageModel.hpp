#pragma once

#include "ActionContract.hpp"
#include "ProjectModel.hpp"
#include "SemanticAuthoring.hpp"

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace NGIN::CLI
{
    enum class RequirementVisibility
    {
        Private,
        Public,
    };

    enum class CapabilityDomain
    {
        Acquisition,
        Build,
        Link,
        Generation,
        Artifact,
        Deployment,
    };

    enum class PackageCoexistence
    {
        Context,
        SideBySide,
    };

    enum class ContributionKind
    {
        Notice,
        RuntimeFile,
        RuntimeDirectory,
        AssetFile,
        AssetDirectory,
    };

    struct RequirementCondition
    {
        std::optional<std::string> option{};
        std::optional<std::string> equals{};
        std::optional<std::string> targetOperatingSystem{};
        std::optional<std::string> targetArchitecture{};
        std::optional<std::string> compiler{};
        ManifestSourceRange source{};
    };

    struct SemanticPackageRequirement
    {
        std::string name{};
        std::optional<SourcedVersionConstraint> constraint{};
        std::vector<ExportUse> exports{};
        std::map<std::string, std::string, std::less<>> optionAssignments{};
        RequirementVisibility visibility{RequirementVisibility::Private};
        PackageInstanceContext context{PackageInstanceContext::Target};
        std::optional<RequirementCondition> condition{};
        ManifestSourceRange source{};
    };

    struct SemanticProjectRequirement
    {
        std::string name{};
        std::optional<PortablePath> path{};
        RequirementVisibility visibility{RequirementVisibility::Private};
        std::optional<RequirementCondition> condition{};
        ManifestSourceRange source{};
    };

    struct SemanticExportRequirement
    {
        ExportUseKind kind{ExportUseKind::Library};
        std::string name{};
        RequirementVisibility visibility{RequirementVisibility::Private};
        std::optional<RequirementCondition> condition{};
        ManifestSourceRange source{};
    };

    struct SemanticCapabilityRequirement
    {
        std::string name{};
        CapabilityDomain domain{CapabilityDomain::Link};
        std::optional<SourcedVersionConstraint> constraint{};
        RequirementVisibility visibility{RequirementVisibility::Private};
        PackageInstanceContext context{PackageInstanceContext::Target};
        std::optional<std::string> provider{};
        std::optional<RequirementCondition> condition{};
        std::string requester{};
        ManifestSourceRange source{};
    };

    struct SemanticOptionPredicate
    {
        std::string name{};
        std::string value{};
        ManifestSourceRange source{};
    };

    using SemanticRequirement =
        std::variant<SemanticPackageRequirement, SemanticProjectRequirement, SemanticExportRequirement,
                     SemanticCapabilityRequirement, SemanticOptionPredicate>;

    struct PackageContribution
    {
        ContributionKind kind{ContributionKind::RuntimeFile};
        std::string owner{};
        PortablePath include{};
        PortablePath destination{};
        ManifestSourceRange source{};
    };

    struct CapabilityImplementation
    {
        std::string name{};
        CapabilityDomain domain{CapabilityDomain::Link};
        SemanticVersion version{};
        PackageInstanceContext context{PackageInstanceContext::Target};
        std::string packageInstance{};
        std::string packageName{};
        std::string exportName{};
        ManifestSourceRange source{};
    };

    struct CpsComponentMetadata
    {
        std::string type{};
        std::optional<std::string> location{};
        std::vector<std::string> includeDirectories{};
        std::vector<std::string> compileDefinitions{};
        std::vector<std::string> compileOptions{};
        std::vector<std::string> linkOptions{};
    };

    struct PackageExport
    {
        ExportUseKind kind{ExportUseKind::Library};
        std::string name{};
        bool defaultExport{false};
        std::vector<SemanticRequirement> requirements{};
        std::vector<CapabilityImplementation> capabilities{};
        std::vector<PackageContribution> contributions{};
        std::optional<SemanticActionContract> action{};
        std::optional<CpsComponentMetadata> cps{};
        std::optional<std::string> description{};
        ManifestSourceRange source{};
    };

    struct SemanticPackage
    {
        AuthoredManifestIdentity manifest{};
        PackageCoordinate coordinate{};
        std::map<std::string, OptionDefinition, std::less<>> options{};
        std::vector<SemanticRequirement> requirements{};
        std::vector<PackageContribution> contributions{};
        std::map<std::string, PackageExport, std::less<>> exports{};
        PackageCoexistence coexistence{PackageCoexistence::Context};
        std::vector<std::pair<std::string, std::string>> compatibleTargets{};
        std::vector<std::string> compatibleCompilers{};
        ManifestSourceRange source{};
    };

    struct SemanticPackageResult
    {
        std::optional<SemanticPackage> value{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool;
    };

    struct PackageOptionAssignment
    {
        std::string name{};
        std::string value{};
        AssignmentAuthority authority{AssignmentAuthority::Project};
        ManifestSourceRange source{};
    };

    struct ResolvedPackageOptions
    {
        std::map<std::string, TypedOptionValue, std::less<>> values{};
        std::map<std::string, std::string, std::less<>> artifactValues{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool;
    };

    struct PackageProviderRequest
    {
        std::string name{};
        std::optional<SourcedVersionConstraint> constraint{};
        std::optional<std::string> sourceBinding{};
        PackageInstanceContext context{PackageInstanceContext::Target};
        ManifestSourceRange source{};
    };

    struct PackageProviderResolution
    {
        std::optional<PackageProviderResult> value{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool;
    };

    class PackageProvider
    {
    public:
        virtual ~PackageProvider() = default;
        [[nodiscard]] virtual auto Kind() const -> std::string_view = 0;
        [[nodiscard]] virtual auto Resolve(const PackageProviderRequest &request) const
            -> PackageProviderResolution = 0;
    };

    struct DirectoryPackageRelease
    {
        std::string name{};
        fs::path manifest{};
        fs::path root{};
        std::string nativeIdentity{};
        std::string nativeVersion{};
        std::string revision{};
        std::string integrity{};
        std::string artifactIdentity{};
        bool hermetic{true};
    };

    class DirectoryPackageProvider final : public PackageProvider
    {
    public:
        DirectoryPackageProvider(std::string identity, std::vector<DirectoryPackageRelease> releases);
        [[nodiscard]] auto Kind() const -> std::string_view override;
        [[nodiscard]] auto Resolve(const PackageProviderRequest &request) const
            -> PackageProviderResolution override;

    private:
        std::string identity_{};
        std::vector<DirectoryPackageRelease> releases_{};
    };

    class CatalogPackageProvider final : public PackageProvider
    {
    public:
        CatalogPackageProvider(std::string kind, std::string identity,
                               std::vector<PackageProviderResult> releases);
        [[nodiscard]] auto Kind() const -> std::string_view override;
        [[nodiscard]] auto Resolve(const PackageProviderRequest &request) const
            -> PackageProviderResolution override;

    private:
        std::string kind_{};
        std::string identity_{};
        std::vector<PackageProviderResult> releases_{};
    };

    struct PackageActivationRequest
    {
        std::vector<ExportUse> exports{};
        SelectionFacts selection{};
        ResolvedPackageOptions options{};
    };

    struct ActivePackageExports
    {
        PackageInstance instance{};
        std::vector<std::string> exports{};
        std::vector<SemanticRequirement> requirements{};
        std::vector<PackageContribution> contributions{};
        std::vector<CapabilityImplementation> capabilities{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool;
    };

    struct CapabilityResolution
    {
        std::vector<CapabilityBinding> bindings{};
        std::vector<std::string> activatedExports{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool;
    };

    struct PackageInstanceUse
    {
        PackageInstance instance{};
        std::string linkageClosure{};
        std::vector<std::string> activationPath{};
        ManifestSourceRange source{};
    };

    [[nodiscard]] auto CapabilityDomainName(CapabilityDomain domain) -> std::string_view;
    [[nodiscard]] auto ParseSemanticPackage(const AuthoredPackageManifest &package) -> SemanticPackageResult;
    [[nodiscard]] auto ResolvePackageOptions(const SemanticPackage &package,
                                             const std::vector<PackageOptionAssignment> &assignments)
        -> ResolvedPackageOptions;
    [[nodiscard]] auto ConstructPackageInstance(const PackageProviderResult &provider,
                                                const BinaryCompatibility &compatibility,
                                                std::map<std::string, std::string, std::less<>> artifactOptions)
        -> PackageInstance;
    [[nodiscard]] auto ActivatePackageExports(const SemanticPackage &package, const PackageInstance &instance,
                                              const PackageActivationRequest &request) -> ActivePackageExports;
    [[nodiscard]] auto ResolveCapabilityBindings(
        const std::vector<SemanticCapabilityRequirement> &requirements,
        const std::vector<CapabilityImplementation> &implementations) -> CapabilityResolution;
    [[nodiscard]] auto ValidatePackageInstanceCoexistence(const std::vector<PackageInstanceUse> &uses,
                                                          PackageCoexistence policy,
                                                          bool platformAllowsSideBySide)
        -> std::vector<ManifestDiagnostic>;
}
