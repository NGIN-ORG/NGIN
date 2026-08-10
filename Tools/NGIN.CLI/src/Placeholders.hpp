#pragma once

#include "AuthoredManifest.hpp"

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace NGIN::CLI
{
    enum class PlaceholderType
    {
        Identifier,
        SemanticVersion,
        Filename,
        Path,
    };

    enum class PlaceholderPhase
    {
        Output,
        Stage,
        Launch,
        Publish,
        LocalExecution,
    };

    struct PlaceholderSpec
    {
        std::string name{};
        PlaceholderType type{PlaceholderType::Identifier};
        std::vector<PlaceholderPhase> phases{};
        bool canonicalIdentity{true};
    };

    struct PlaceholderValue
    {
        PlaceholderType type{PlaceholderType::Identifier};
        std::string value{};
    };

    struct PlaceholderExpansionResult
    {
        std::optional<std::string> value{};
        bool canonicalIdentity{true};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool;
    };

    [[nodiscard]] auto PlaceholderRegistry() -> std::span<const PlaceholderSpec>;
    [[nodiscard]] auto ExpandPlaceholders(
        std::string_view input, PlaceholderPhase phase,
        const std::map<std::string, PlaceholderValue, std::less<>> &values,
        bool requireCanonicalIdentity = false, const ManifestSourceRange &source = {})
        -> PlaceholderExpansionResult;
}
