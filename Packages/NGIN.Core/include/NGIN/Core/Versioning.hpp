#pragma once

/// @file Versioning.hpp
/// @brief Semantic version and version-range helpers for compatibility checks.

#include <NGIN/Core/Errors.hpp>
#include <NGIN/Core/Export.hpp>

#include <compare>
#include <optional>
#include <string>
#include <string_view>

namespace NGIN::Core
{
    /// @brief Semantic version tuple.
    struct SemanticVersion
    {
        NGIN::UInt32 major {0};
        NGIN::UInt32 minor {0};
        NGIN::UInt32 patch {0};
        std::string  prerelease {};

        [[nodiscard]] NGIN_CORE_API auto operator<=>(const SemanticVersion& other) const noexcept -> std::strong_ordering;
        [[nodiscard]] auto operator==(const SemanticVersion& other) const noexcept -> bool = default;
    };

    /// @brief Closed/open semantic-version range.
    struct VersionRange
    {
        std::optional<SemanticVersion> lower {};
        std::optional<SemanticVersion> upper {};
        bool                           includeLower {true};
        bool                           includeUpper {false};

        [[nodiscard]] NGIN_CORE_API auto Contains(const SemanticVersion& value) const noexcept -> bool;
    };

    [[nodiscard]] NGIN_CORE_API auto ParseSemanticVersion(std::string_view text) noexcept -> CoreResult<SemanticVersion>;
    [[nodiscard]] NGIN_CORE_API auto ParseVersionRange(std::string_view text) noexcept -> CoreResult<VersionRange>;
    [[nodiscard]] NGIN_CORE_API auto FormatSemanticVersion(const SemanticVersion& value) -> std::string;
    [[nodiscard]] NGIN_CORE_API auto FormatVersionRange(const VersionRange& range) -> std::string;
}

