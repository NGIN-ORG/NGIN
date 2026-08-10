#pragma once

#include "AuthoredManifest.hpp"

#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace NGIN::CLI
{
    enum class AssignmentAuthority
    {
        Convention,
        WorkspaceDefault,
        Project,
        Refinement,
        CommandLine,
    };

    template <typename TValue>
    struct SourcedAssignment
    {
        TValue value{};
        AssignmentAuthority authority{AssignmentAuthority::Convention};
        ManifestSourceRange source{};
        std::string description{};
    };

    template <typename TValue>
    struct ScalarMergeResult
    {
        std::optional<SourcedAssignment<TValue>> value{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool
        {
            return value.has_value() && diagnostics.empty();
        }
    };

    template <typename TValue>
    [[nodiscard]] auto MergeScalarSetting(std::string_view identity,
                                          const std::vector<SourcedAssignment<TValue>> &assignments)
        -> ScalarMergeResult<TValue>
    {
        ScalarMergeResult<TValue> result{};
        for (const auto &assignment : assignments)
        {
            if (!result.value.has_value() || assignment.authority > result.value->authority)
            {
                result.value = assignment;
                continue;
            }
            if (assignment.authority == result.value->authority && assignment.value != result.value->value)
            {
                result.diagnostics.push_back(ManifestDiagnostic{
                    .severity = ManifestDiagnosticSeverity::Error,
                    .code = "NGIN2001",
                    .message = "conflicting equal-authority assignments for '" + std::string(identity) + "'",
                    .source = assignment.source,
                    .relatedSources = {result.value->source},
                });
            }
        }
        return result;
    }

    struct SemanticVersion
    {
        std::uint64_t major{0};
        std::uint64_t minor{0};
        std::uint64_t patch{0};
        std::vector<std::string> prerelease{};

        [[nodiscard]] friend auto operator==(const SemanticVersion &, const SemanticVersion &) -> bool = default;
        [[nodiscard]] friend auto operator<=>(const SemanticVersion &left, const SemanticVersion &right)
            -> std::strong_ordering;
    };

    struct VersionBoundary
    {
        SemanticVersion version{};
        bool inclusive{true};
    };

    struct SourcedVersionConstraint
    {
        std::optional<VersionBoundary> lower{};
        std::optional<VersionBoundary> upper{};
        ManifestSourceRange source{};
        std::string description{};
    };

    struct VersionIntersectionResult
    {
        std::optional<SourcedVersionConstraint> value{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool;
    };

    [[nodiscard]] auto ParseSemanticVersion(std::string_view value) -> std::optional<SemanticVersion>;
    [[nodiscard]] auto IntersectVersionConstraints(std::string_view identity,
                                                   const std::vector<SourcedVersionConstraint> &constraints)
        -> VersionIntersectionResult;

    struct SourcedIdentity
    {
        std::string identity{};
        ManifestSourceRange source{};
    };

    [[nodiscard]] auto MergeRequiredDependencies(const std::vector<SourcedIdentity> &dependencies)
        -> std::vector<SourcedIdentity>;
    [[nodiscard]] auto MergeExportActivations(const std::vector<SourcedIdentity> &exports)
        -> std::vector<SourcedIdentity>;
    [[nodiscard]] auto MergeActionSelections(const std::vector<SourcedIdentity> &actions)
        -> std::vector<SourcedIdentity>;
}
