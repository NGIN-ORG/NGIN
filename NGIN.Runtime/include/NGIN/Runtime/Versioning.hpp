#pragma once

/// @file Versioning.hpp
/// @brief Semantic version and version-range helpers for compatibility checks.

#include <NGIN/Runtime/Errors.hpp>

#include <compare>
#include <optional>
#include <string>
#include <string_view>

namespace NGIN::Runtime
{
    /// @brief Semantic version tuple.
    struct SemanticVersion
    {
        NGIN::UInt32 major {0};
        NGIN::UInt32 minor {0};
        NGIN::UInt32 patch {0};
        std::string  prerelease {};

        [[nodiscard]] auto operator<=>(const SemanticVersion& other) const noexcept -> std::strong_ordering;
        [[nodiscard]] auto operator==(const SemanticVersion& other) const noexcept -> bool = default;
    };

    /// @brief Closed/open semantic-version range.
    struct VersionRange
    {
        std::optional<SemanticVersion> lower {};
        std::optional<SemanticVersion> upper {};
        bool                           includeLower {true};
        bool                           includeUpper {false};

        [[nodiscard]] auto Contains(const SemanticVersion& value) const noexcept -> bool;
    };

    [[nodiscard]] auto ParseSemanticVersion(std::string_view text) noexcept -> RuntimeResult<SemanticVersion>;
    [[nodiscard]] auto ParseVersionRange(std::string_view text) noexcept -> RuntimeResult<VersionRange>;
    [[nodiscard]] auto FormatSemanticVersion(const SemanticVersion& value) -> std::string;
    [[nodiscard]] auto FormatVersionRange(const VersionRange& range) -> std::string;
}

