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
        std::string revision{};
        std::string integrity{};
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
        std::string exportName{};
        std::string targetName{};
    };

    struct CMakeIntegrationBindings
    {
        std::string packageInstance{};
        std::string mode{};
        std::filesystem::path source{};
        std::map<std::string, std::string> cache{};
        std::vector<CMakeTargetBinding> targets{};
    };

    using IntegrationBindings = std::variant<CMakeIntegrationBindings>;
}
