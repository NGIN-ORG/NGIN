#pragma once

#include "AuthoredManifest.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace NGIN::CLI
{
    enum class PortablePathBase
    {
        Manifest,
        Workspace,
        ActionOutput,
    };

    struct PortablePath
    {
        std::string value{};
        PortablePathBase base{PortablePathBase::Manifest};

        [[nodiscard]] friend auto operator==(const PortablePath &, const PortablePath &) -> bool = default;
        [[nodiscard]] friend auto operator<(const PortablePath &left, const PortablePath &right) -> bool
        {
            return left.value < right.value || (left.value == right.value && left.base < right.base);
        }
    };

    struct PortablePathResult
    {
        std::optional<PortablePath> value{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool;
    };

    struct GlobResult
    {
        std::vector<PortablePath> matches{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool;
    };

    [[nodiscard]] auto NormalizePortablePath(std::string_view authored, PortablePathBase base,
                                             const ManifestSourceRange &source = {}) -> PortablePathResult;
    [[nodiscard]] auto ResolvePortablePath(const PortablePath &path, const std::filesystem::path &manifestDirectory,
                                           const std::filesystem::path &workspaceRoot,
                                           const std::filesystem::path &allowedRoot,
                                           const ManifestSourceRange &source = {}) -> PortablePathResult;
    [[nodiscard]] auto NormalizeStageDestination(std::string_view authored, const ManifestSourceRange &source = {})
        -> PortablePathResult;
    [[nodiscard]] auto GlobMatchesPortable(std::string_view pattern, std::string_view portablePath) -> bool;
    [[nodiscard]] auto ValidateTargetPathCaseCollisions(std::span<const PortablePath> paths, bool targetCaseInsensitive,
                                                        const ManifestSourceRange &source = {})
        -> std::vector<ManifestDiagnostic>;
    [[nodiscard]] auto ExpandPortableGlob(const std::filesystem::path &root, std::string_view pattern,
                                          bool targetCaseInsensitive, const ManifestSourceRange &source = {},
                                          bool allowSymlinks = false,
                                          bool excludeGeneratedDirectories = false) -> GlobResult;
} // namespace NGIN::CLI
