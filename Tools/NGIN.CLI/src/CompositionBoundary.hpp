#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace NGIN::CLI
{
    struct PackageCoordinate
    {
        std::string name{};
        std::string exactVersion{};
        std::optional<std::string> sourceBinding{};

        [[nodiscard]] friend auto operator==(const PackageCoordinate &, const PackageCoordinate &) -> bool = default;
    };

    enum class PackageInstanceContext
    {
        Host,
        Target,
    };

    struct PackageProviderResult
    {
        PackageCoordinate coordinate{};
        std::string providerKind{};
        std::string nativeIdentity{};
        std::string nativeVersion{};
        std::string revision{};
        std::string integrity{};
        std::string artifactIdentity{};
        std::filesystem::path root{};
        std::filesystem::path manifest{};
        std::optional<std::filesystem::path> installedPrefix{};
        PackageInstanceContext context{PackageInstanceContext::Target};
        bool hermetic{false};
        std::string provenance{};
        std::string trust{};
        std::string signature{};
    };

    struct BinaryCompatibility
    {
        std::string operatingSystem{};
        std::string architecture{};
        std::string compiler{};
        std::string compilerVersion{};
        std::string runtimeLibrary{};
        std::string configuration{};
        std::string linkage{};
        std::map<std::string, std::string> artifactOptions{};

        [[nodiscard]] friend auto operator==(const BinaryCompatibility &, const BinaryCompatibility &) -> bool = default;
    };

    struct PackageInstance
    {
        PackageProviderResult providerResult{};
        PackageInstanceContext context{PackageInstanceContext::Target};
        BinaryCompatibility compatibility{};
        std::map<std::string, std::string, std::less<>> artifactOptions{};
        std::string identity{};
    };

    struct CapabilityBinding
    {
        std::string requirement{};
        std::string capability{};
        std::string domain{};
        std::string version{};
        std::string packageInstance{};
        std::string exportName{};
    };

    struct CMakeTargetBinding
    {
        std::string exportIdentity{};
        std::string exportName{};
        std::string targetName{};
        std::string importedKind{};
        std::string location{};
        std::vector<std::string> includeDirectories{};
        std::vector<std::string> compileDefinitions{};
        std::vector<std::string> compileOptions{};
        std::vector<std::string> linkOptions{};
    };

    enum class CMakeIntegrationKind
    {
        AddSubdirectory,
        Isolated,
        FindPackage,
        Manual,
        Cps,
    };

    struct CMakeCacheBinding
    {
        std::string name{};
        std::string value{};
        std::string type{"STRING"};
        bool artifact{false};
    };

    struct CMakeFindPackageBinding
    {
        std::string name{};
        bool config{false};
        bool required{true};
        std::optional<std::string> version{};
    };

    struct IntegrationBindingProvenance
    {
        std::string document{};
        std::size_t line{0};
        std::size_t column{0};
        std::string reason{};
    };

    struct CMakeIntegrationBindings
    {
        std::string packageInstance{};
        CMakeIntegrationKind kind{CMakeIntegrationKind::AddSubdirectory};
        std::filesystem::path source{};
        std::vector<CMakeCacheBinding> cache{};
        std::vector<CMakeTargetBinding> targets{};
        std::optional<CMakeFindPackageBinding> findPackage{};
        bool installBeforeUse{false};
        IntegrationBindingProvenance provenance{};
    };

    using IntegrationBindings = std::variant<CMakeIntegrationBindings>;
}
