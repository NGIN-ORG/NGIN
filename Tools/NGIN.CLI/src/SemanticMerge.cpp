#include "SemanticMerge.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <compare>
#include <map>

namespace NGIN::CLI
{
    namespace
    {
        [[nodiscard]] auto ComparePrereleaseIdentifier(const std::string &left, const std::string &right)
            -> std::strong_ordering
        {
            const auto numeric = [](const std::string &value) {
                return !value.empty() && std::ranges::all_of(value, [](const unsigned char ch) {
                           return std::isdigit(ch) != 0;
                       });
            };
            const auto leftNumeric = numeric(left);
            const auto rightNumeric = numeric(right);
            if (leftNumeric && rightNumeric)
            {
                if (left.size() != right.size()) return left.size() <=> right.size();
                return left <=> right;
            }
            if (leftNumeric != rightNumeric) return leftNumeric ? std::strong_ordering::less : std::strong_ordering::greater;
            return left <=> right;
        }

        [[nodiscard]] auto Split(const std::string_view value, const char separator) -> std::vector<std::string>
        {
            std::vector<std::string> result{};
            std::size_t start = 0;
            while (start <= value.size())
            {
                const auto end = value.find(separator, start);
                result.emplace_back(value.substr(start, end == std::string_view::npos ? value.size() - start
                                                                                     : end - start));
                if (end == std::string_view::npos) break;
                start = end + 1;
            }
            return result;
        }

        template <typename TBoundary>
        [[nodiscard]] auto LaterLower(const TBoundary &left, const TBoundary &right) -> TBoundary
        {
            if (left.version < right.version) return right;
            if (right.version < left.version) return left;
            return TBoundary{.version = left.version, .inclusive = left.inclusive && right.inclusive};
        }

        template <typename TBoundary>
        [[nodiscard]] auto EarlierUpper(const TBoundary &left, const TBoundary &right) -> TBoundary
        {
            if (left.version < right.version) return left;
            if (right.version < left.version) return right;
            return TBoundary{.version = left.version, .inclusive = left.inclusive && right.inclusive};
        }

        [[nodiscard]] auto MergeIdentitySet(const std::vector<SourcedIdentity> &values) -> std::vector<SourcedIdentity>
        {
            std::map<std::string, SourcedIdentity, std::less<>> selected{};
            for (const auto &value : values) selected.try_emplace(value.identity, value);
            std::vector<SourcedIdentity> result{};
            result.reserve(selected.size());
            for (auto &[_, value] : selected) result.push_back(std::move(value));
            return result;
        }
    }

    auto operator<=>(const SemanticVersion &left, const SemanticVersion &right) -> std::strong_ordering
    {
        if (const auto compared = left.major <=> right.major; compared != 0) return compared;
        if (const auto compared = left.minor <=> right.minor; compared != 0) return compared;
        if (const auto compared = left.patch <=> right.patch; compared != 0) return compared;
        if (left.prerelease.empty() != right.prerelease.empty())
            return left.prerelease.empty() ? std::strong_ordering::greater : std::strong_ordering::less;
        for (std::size_t index = 0; index < std::min(left.prerelease.size(), right.prerelease.size()); ++index)
        {
            if (const auto compared = ComparePrereleaseIdentifier(left.prerelease[index], right.prerelease[index]);
                compared != 0)
                return compared;
        }
        return left.prerelease.size() <=> right.prerelease.size();
    }

    auto VersionIntersectionResult::Succeeded() const -> bool
    {
        return value.has_value() && diagnostics.empty();
    }

    auto ParseSemanticVersion(std::string_view value) -> std::optional<SemanticVersion>
    {
        if (const auto metadata = value.find('+'); metadata != std::string_view::npos)
        {
            const auto identifiers = Split(value.substr(metadata + 1), '.');
            if (std::ranges::any_of(identifiers, [](const std::string &part) {
                    return part.empty() || !std::ranges::all_of(part, [](const unsigned char ch) {
                               return std::isalnum(ch) != 0 || ch == '-';
                           });
                }))
                return std::nullopt;
            value = value.substr(0, metadata);
        }
        std::string_view prerelease{};
        if (const auto separator = value.find('-'); separator != std::string_view::npos)
        {
            prerelease = value.substr(separator + 1);
            if (prerelease.empty()) return std::nullopt;
            value = value.substr(0, separator);
        }
        const auto core = Split(value, '.');
        if (core.size() != 3 || std::ranges::any_of(core, [](const std::string &part) {
                return part.empty() || (part.size() > 1 && part.front() == '0');
            }))
            return std::nullopt;
        SemanticVersion parsed{};
        auto parseNumber = [](const std::string &text, std::uint64_t &output) {
            const auto converted = std::from_chars(text.data(), text.data() + text.size(), output);
            return converted.ec == std::errc{} && converted.ptr == text.data() + text.size();
        };
        if (!parseNumber(core[0], parsed.major) || !parseNumber(core[1], parsed.minor) ||
            !parseNumber(core[2], parsed.patch))
            return std::nullopt;
        if (!prerelease.empty())
        {
            parsed.prerelease = Split(prerelease, '.');
            if (std::ranges::any_of(parsed.prerelease, [](const std::string &part) {
                    const auto numeric = !part.empty() && std::ranges::all_of(part, [](const unsigned char ch) {
                        return std::isdigit(ch) != 0;
                    });
                    return part.empty() || (numeric && part.size() > 1 && part.front() == '0') ||
                           !std::ranges::all_of(part, [](const unsigned char ch) {
                               return std::isalnum(ch) != 0 || ch == '-';
                           });
                }))
                return std::nullopt;
        }
        return parsed;
    }

    auto IntersectVersionConstraints(const std::string_view identity,
                                     const std::vector<SourcedVersionConstraint> &constraints)
        -> VersionIntersectionResult
    {
        VersionIntersectionResult result{};
        SourcedVersionConstraint intersection{};
        std::vector<ManifestSourceRange> sources{};
        for (const auto &constraint : constraints)
        {
            if (constraint.lower.has_value())
                intersection.lower = intersection.lower.has_value() ? LaterLower(*intersection.lower, *constraint.lower)
                                                                     : constraint.lower;
            if (constraint.upper.has_value())
                intersection.upper = intersection.upper.has_value() ? EarlierUpper(*intersection.upper, *constraint.upper)
                                                                     : constraint.upper;
            sources.push_back(constraint.source);
        }
        if (intersection.lower.has_value() && intersection.upper.has_value())
        {
            const auto ordering = intersection.lower->version <=> intersection.upper->version;
            if (ordering > 0 || (ordering == 0 && (!intersection.lower->inclusive || !intersection.upper->inclusive)))
            {
                const auto source = sources.empty() ? ManifestSourceRange{} : sources.back();
                if (!sources.empty()) sources.pop_back();
                result.diagnostics.push_back(ManifestDiagnostic{
                    .severity = ManifestDiagnosticSeverity::Error,
                    .code = "NGIN2002",
                    .message = "version constraints for '" + std::string(identity) + "' have an empty intersection",
                    .source = source,
                    .relatedSources = std::move(sources),
                });
                return result;
            }
        }
        if (!constraints.empty())
        {
            intersection.source = constraints.front().source;
            intersection.description = "intersection for " + std::string(identity);
        }
        result.value = std::move(intersection);
        return result;
    }

    auto MergeRequiredDependencies(const std::vector<SourcedIdentity> &dependencies) -> std::vector<SourcedIdentity>
    {
        return MergeIdentitySet(dependencies);
    }

    auto MergeExportActivations(const std::vector<SourcedIdentity> &exports) -> std::vector<SourcedIdentity>
    {
        return MergeIdentitySet(exports);
    }

    auto MergeActionSelections(const std::vector<SourcedIdentity> &actions) -> std::vector<SourcedIdentity>
    {
        return MergeIdentitySet(actions);
    }
}
