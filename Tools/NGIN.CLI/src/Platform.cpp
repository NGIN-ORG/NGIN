#include "Platform.hpp"

#include <algorithm>
#include <stdexcept>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace NGIN::CLI
{
    namespace
    {
        auto ValidateIdentity(const PlatformIdentity &identity, const std::string_view source) -> void
        {
            if (identity.name.empty() || !IsValidOperatingSystem(identity.operatingSystem) ||
                !IsValidArchitecture(identity.architecture))
            {
                throw std::runtime_error("unsupported " + std::string{source} + " platform '" + identity.name +
                                         "' (operating system '" + identity.operatingSystem +
                                         "', architecture '" + identity.architecture + "')");
            }
        }
    }

    [[nodiscard]] auto IsValidOperatingSystem(const std::string_view value) -> bool
    {
        return value == "linux" || value == "windows" || value == "macos";
    }

    [[nodiscard]] auto IsValidArchitecture(const std::string_view value) -> bool
    {
        return value == "x64" || value == "arm64";
    }

    [[nodiscard]] auto DetectHostPlatform() -> PlatformIdentity
    {
        PlatformIdentity host{};
#if defined(_WIN32)
        host.operatingSystem = "windows";
#elif defined(__APPLE__)
        host.operatingSystem = "macos";
#elif defined(__linux__)
        host.operatingSystem = "linux";
#else
        host.operatingSystem = "unknown";
#endif

#if defined(_M_X64) || defined(__x86_64__)
        host.architecture = "x64";
#elif defined(_M_ARM64) || defined(__aarch64__)
        host.architecture = "arm64";
#else
        host.architecture = "unknown";
#endif

        host.name = host.operatingSystem + "-" + host.architecture;
        ValidateIdentity(host, "host");
        return host;
    }

    [[nodiscard]] auto IsTerminal(std::FILE *stream) -> bool
    {
        if (stream == nullptr)
        {
            return false;
        }
#if defined(_WIN32)
        return ::_isatty(::_fileno(stream)) != 0;
#else
        return ::isatty(::fileno(stream)) != 0;
#endif
    }

    [[nodiscard]] auto ResolvePlatformIdentity(
        const std::string_view name,
        const std::span<const WorkspaceManifest::Platform> workspacePlatforms,
        const PlatformIdentity &host) -> PlatformIdentity
    {
        ValidateIdentity(host, "host");
        if (name.empty() || name == "host")
        {
            return host;
        }

        const auto workspacePlatform =
            std::find_if(workspacePlatforms.begin(), workspacePlatforms.end(), [&](const auto &candidate) {
                return candidate.name == name;
            });
        if (workspacePlatform != workspacePlatforms.end())
        {
            PlatformIdentity resolved{
                .name = workspacePlatform->name,
                .operatingSystem = workspacePlatform->operatingSystem,
                .architecture = workspacePlatform->architecture,
            };
            ValidateIdentity(resolved, "workspace-defined");
            return resolved;
        }

        const auto separator = name.find('-');
        if (separator == std::string_view::npos)
        {
            throw std::runtime_error("unknown target platform '" + std::string{name} + "'");
        }
        PlatformIdentity resolved{
            .name = std::string{name},
            .operatingSystem = std::string{name.substr(0, separator)},
            .architecture = std::string{name.substr(separator + 1)},
        };
        ValidateIdentity(resolved, "target");
        return resolved;
    }

    [[nodiscard]] auto ResolvePlatformIdentity(
        const std::string_view name,
        const std::span<const WorkspaceManifest::Platform> workspacePlatforms) -> PlatformIdentity
    {
        return ResolvePlatformIdentity(name, workspacePlatforms, DetectHostPlatform());
    }

    auto ApplyTargetPlatform(
        ProfileDefinition &profile,
        const std::string_view name,
        const std::span<const WorkspaceManifest::Platform> workspacePlatforms) -> void
    {
        const auto resolved = ResolvePlatformIdentity(name, workspacePlatforms);
        profile.platform = resolved.name;
        profile.operatingSystem = resolved.operatingSystem;
        profile.architecture = resolved.architecture;
    }
}
