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
        std::string versionConstraint{};
        std::optional<std::string> sourceBinding{};
    };

    struct PackageProviderResult
    {
        PackageCoordinate coordinate{};
        std::string provider{};
        std::string providerIdentity{};
        std::string exactVersion{};
        std::string integrity{};
        std::filesystem::path root{};
        bool hermetic{false};
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
        BinaryCompatibility compatibility{};
        std::string identity{};
    };

    struct CapabilityBinding
    {
        std::string requirement{};
        std::string capability{};
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
