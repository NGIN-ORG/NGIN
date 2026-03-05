#include <NGIN/Core/Versioning.hpp>

#include <charconv>
#include <sstream>
#include <string>
#include <vector>

namespace NGIN::Core
{
    namespace
    {
        [[nodiscard]] auto ParseUInt32(std::string_view text, NGIN::UInt32& out) -> bool
        {
            if (text.empty())
            {
                return false;
            }

            NGIN::UInt32 value = 0;
            const auto* begin = text.data();
            const auto* end = begin + text.size();
            const auto [ptr, ec] = std::from_chars(begin, end, value);
            if (ec != std::errc{} || ptr != end)
            {
                return false;
            }

            out = value;
            return true;
        }

        [[nodiscard]] auto Trim(std::string_view text) -> std::string_view
        {
            std::size_t left = 0;
            std::size_t right = text.size();
            while (left < right && (text[left] == ' ' || text[left] == '\t'))
            {
                ++left;
            }
            while (right > left && (text[right - 1] == ' ' || text[right - 1] == '\t'))
            {
                --right;
            }
            return text.substr(left, right - left);
        }
    }

    auto SemanticVersion::operator<=>(const SemanticVersion& other) const noexcept -> std::strong_ordering
    {
        if (major != other.major)
        {
            return major < other.major ? std::strong_ordering::less : std::strong_ordering::greater;
        }
        if (minor != other.minor)
        {
            return minor < other.minor ? std::strong_ordering::less : std::strong_ordering::greater;
        }
        if (patch != other.patch)
        {
            return patch < other.patch ? std::strong_ordering::less : std::strong_ordering::greater;
        }

        if (prerelease.empty() && other.prerelease.empty())
        {
            return std::strong_ordering::equal;
        }
        if (prerelease.empty())
        {
            return std::strong_ordering::greater;
        }
        if (other.prerelease.empty())
        {
            return std::strong_ordering::less;
        }
        if (prerelease == other.prerelease)
        {
            return std::strong_ordering::equal;
        }
        return prerelease < other.prerelease ? std::strong_ordering::less : std::strong_ordering::greater;
    }

    auto VersionRange::Contains(const SemanticVersion& value) const noexcept -> bool
    {
        if (lower.has_value())
        {
            if (includeLower)
            {
                if (value < *lower)
                {
                    return false;
                }
            }
            else if (value <= *lower)
            {
                return false;
            }
        }

        if (upper.has_value())
        {
            if (includeUpper)
            {
                if (value > *upper)
                {
                    return false;
                }
            }
            else if (value >= *upper)
            {
                return false;
            }
        }

        return true;
    }

    auto ParseSemanticVersion(const std::string_view text) noexcept -> CoreResult<SemanticVersion>
    {
        const auto normalized = Trim(text);
        if (normalized.empty())
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Versioning", {}, "semantic version is empty"));
        }

        SemanticVersion out {};

        const auto dash = normalized.find('-');
        const auto mainPart = normalized.substr(0, dash);
        if (dash != std::string_view::npos)
        {
            out.prerelease = std::string(normalized.substr(dash + 1));
        }

        const auto dot1 = mainPart.find('.');
        if (dot1 == std::string_view::npos)
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Versioning", {}, "semantic version missing major.minor.patch"));
        }
        const auto dot2 = mainPart.find('.', dot1 + 1);
        if (dot2 == std::string_view::npos)
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Versioning", {}, "semantic version missing patch"));
        }

        if (!ParseUInt32(mainPart.substr(0, dot1), out.major)
            || !ParseUInt32(mainPart.substr(dot1 + 1, dot2 - dot1 - 1), out.minor)
            || !ParseUInt32(mainPart.substr(dot2 + 1), out.patch))
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Versioning", {}, "semantic version contains invalid numeric field"));
        }

        return out;
    }

    auto ParseVersionRange(const std::string_view text) noexcept -> CoreResult<VersionRange>
    {
        VersionRange range {};
        const auto normalized = Trim(text);
        if (normalized.empty())
        {
            return range;
        }

        std::istringstream stream {std::string(normalized)};
        std::string token;
        while (stream >> token)
        {
            std::string_view view(token);
            bool include = true;
            bool isLower = false;

            if (view.starts_with(">="))
            {
                include = true;
                isLower = true;
                view.remove_prefix(2);
            }
            else if (view.starts_with('>'))
            {
                include = false;
                isLower = true;
                view.remove_prefix(1);
            }
            else if (view.starts_with("<="))
            {
                include = true;
                isLower = false;
                view.remove_prefix(2);
            }
            else if (view.starts_with('<'))
            {
                include = false;
                isLower = false;
                view.remove_prefix(1);
            }
            else if (view.starts_with('='))
            {
                auto parsed = ParseSemanticVersion(view.substr(1));
                if (!parsed)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(parsed.ErrorUnsafe());
                }
                range.lower = parsed.ValueUnsafe();
                range.upper = parsed.ValueUnsafe();
                range.includeLower = true;
                range.includeUpper = true;
                continue;
            }
            else
            {
                auto parsed = ParseSemanticVersion(view);
                if (!parsed)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(parsed.ErrorUnsafe());
                }
                range.lower = parsed.ValueUnsafe();
                range.upper = parsed.ValueUnsafe();
                range.includeLower = true;
                range.includeUpper = true;
                continue;
            }

            auto parsed = ParseSemanticVersion(view);
            if (!parsed)
            {
                return NGIN::Utilities::Unexpected<KernelError>(parsed.ErrorUnsafe());
            }

            if (isLower)
            {
                range.lower = parsed.ValueUnsafe();
                range.includeLower = include;
            }
            else
            {
                range.upper = parsed.ValueUnsafe();
                range.includeUpper = include;
            }
        }

        return range;
    }

    auto FormatSemanticVersion(const SemanticVersion& value) -> std::string
    {
        std::string output = std::to_string(value.major) + "." + std::to_string(value.minor) + "." + std::to_string(value.patch);
        if (!value.prerelease.empty())
        {
            output += "-";
            output += value.prerelease;
        }
        return output;
    }

    auto FormatVersionRange(const VersionRange& range) -> std::string
    {
        if (range.lower.has_value() && range.upper.has_value() && *range.lower == *range.upper && range.includeLower && range.includeUpper)
        {
            return "=" + FormatSemanticVersion(*range.lower);
        }

        std::string output {};
        if (range.lower.has_value())
        {
            output += range.includeLower ? ">=" : ">";
            output += FormatSemanticVersion(*range.lower);
        }
        if (range.upper.has_value())
        {
            if (!output.empty())
            {
                output += " ";
            }
            output += range.includeUpper ? "<=" : "<";
            output += FormatSemanticVersion(*range.upper);
        }
        return output;
    }
}
