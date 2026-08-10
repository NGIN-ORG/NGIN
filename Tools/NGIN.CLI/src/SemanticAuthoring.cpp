#include "SemanticAuthoring.hpp"

#include <charconv>

namespace NGIN::CLI
{
    namespace
    {
        [[nodiscard]] auto AttributeValue(const AuthoredElement &element, const std::string_view name)
            -> std::string
        {
            const auto *attribute = element.Attribute(name);
            return attribute == nullptr ? std::string{} : attribute->value;
        }

        [[nodiscard]] auto BoolAttributeValue(const AuthoredElement &element, const std::string_view name,
                                              const bool fallback = false) -> bool
        {
            const auto *attribute = element.Attribute(name);
            return attribute == nullptr ? fallback : attribute->value == "true";
        }

        [[nodiscard]] auto ParseInteger(const std::string_view value) -> std::optional<std::int64_t>
        {
            if (value.empty()) return std::nullopt;
            std::int64_t result{};
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
            return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size()
                       ? std::optional<std::int64_t>{result}
                       : std::nullopt;
        }

        auto AddError(std::vector<ManifestDiagnostic> &diagnostics, std::string code, std::string message,
                      const ManifestSourceRange &source, std::vector<ManifestSourceRange> related = {}) -> void
        {
            diagnostics.push_back(ManifestDiagnostic{.severity = ManifestDiagnosticSeverity::Error,
                                                       .code = std::move(code),
                                                       .message = std::move(message),
                                                       .source = source,
                                                       .relatedSources = std::move(related)});
        }

        [[nodiscard]] auto CompatibleConstraint(const std::string_view value, const ManifestSourceRange &source)
            -> std::optional<SourcedVersionConstraint>
        {
            std::vector<std::uint64_t> parts{};
            std::size_t start = 0;
            while (start <= value.size())
            {
                const auto dot = value.find('.', start);
                const auto text = value.substr(start, dot == std::string_view::npos ? value.size() - start : dot - start);
                std::uint64_t part{};
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), part);
                if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return std::nullopt;
                parts.push_back(part);
                if (dot == std::string_view::npos) break;
                start = dot + 1;
            }
            if (parts.empty() || parts.size() > 3) return std::nullopt;
            const SemanticVersion lower{.major = parts[0], .minor = parts.size() > 1 ? parts[1] : 0,
                                        .patch = parts.size() > 2 ? parts[2] : 0};
            SemanticVersion upper{};
            if (lower.major > 0)
                upper.major = lower.major + 1;
            else if (parts.size() > 1 && lower.minor > 0)
                upper = SemanticVersion{.major = 0, .minor = lower.minor + 1};
            else if (parts.size() > 2)
                upper = SemanticVersion{.major = 0, .minor = 0, .patch = lower.patch + 1};
            else
                upper = SemanticVersion{.major = 1};
            return SourcedVersionConstraint{.lower = VersionBoundary{lower, true},
                                            .upper = VersionBoundary{upper, false},
                                            .source = source,
                                            .description = "Compatible=" + std::string(value)};
        }
    }

    auto ParseOptionDefinitions(const AuthoredElement &options, std::vector<ManifestDiagnostic> &diagnostics)
        -> std::map<std::string, OptionDefinition, std::less<>>
    {
        std::map<std::string, OptionDefinition, std::less<>> result{};
        for (const auto &node : options.children)
        {
            OptionDefinition definition{.name = AttributeValue(node, "Name"), .source = node.source};
            const auto defaultValue = AttributeValue(node, "Default");
            if (node.name == "Boolean") definition.type = OptionType::Boolean;
            else if (node.name == "Enum")
            {
                definition.type = OptionType::Enumeration;
                for (const auto &value : node.children) definition.allowedValues.insert(AttributeValue(value, "Name"));
            }
            else if (node.name == "String")
            {
                definition.type = OptionType::String;
                for (const auto &value : node.children) definition.allowedValues.insert(AttributeValue(value, "Name"));
            }
            else if (node.name == "Integer")
            {
                definition.type = OptionType::Integer;
                definition.minimum = ParseInteger(AttributeValue(node, "Min"));
                definition.maximum = ParseInteger(AttributeValue(node, "Max"));
                if (definition.minimum.has_value() && definition.maximum.has_value() &&
                    *definition.minimum > *definition.maximum)
                    AddError(diagnostics, "NGIN3001", "Option '" + definition.name + "' has Min greater than Max",
                             node.source);
            }
            else definition.type = OptionType::Path;
            definition.artifact = BoolAttributeValue(node, "Artifact");
            const auto parsedDefault = ParseOptionValue(definition, defaultValue, node.source);
            if (!parsedDefault.Succeeded())
                diagnostics.insert(diagnostics.end(), parsedDefault.diagnostics.begin(), parsedDefault.diagnostics.end());
            else
                definition.defaultValue = *parsedDefault.value;
            if (const auto [existing, inserted] = result.emplace(definition.name, definition); !inserted)
                AddError(diagnostics, "NGIN3001", "duplicate Option declaration '" + definition.name + "'",
                         node.source, {existing->second.source});
        }
        return result;
    }

    auto ParseAuthoredVersionConstraint(const AuthoredElement &element, const std::string_view identity,
                                        std::vector<ManifestDiagnostic> &diagnostics)
        -> std::optional<SourcedVersionConstraint>
    {
        const auto exact = AttributeValue(element, "Exact");
        const auto compatible = AttributeValue(element, "Compatible");
        const auto versionNode = std::ranges::find(element.children, "Version", &AuthoredElement::name);
        const auto hasStructuredAttributes = !AttributeValue(element, "AtLeast").empty() ||
                                             !AttributeValue(element, "After").empty() ||
                                             !AttributeValue(element, "AtMost").empty() ||
                                             !AttributeValue(element, "Before").empty();
        const auto sourceCount = static_cast<std::size_t>(!exact.empty()) +
                                 static_cast<std::size_t>(!compatible.empty()) +
                                 static_cast<std::size_t>(hasStructuredAttributes) +
                                 static_cast<std::size_t>(versionNode != element.children.end());
        if (sourceCount > 1)
        {
            AddError(diagnostics, "NGIN2004",
                     "version requirement '" + std::string(identity) + "' has multiple constraint forms",
                     element.source);
            return std::nullopt;
        }
        if (!exact.empty())
        {
            const auto version = ParseSemanticVersion(exact);
            if (!version.has_value()) return std::nullopt;
            return SourcedVersionConstraint{.lower = VersionBoundary{*version, true},
                                            .upper = VersionBoundary{*version, true},
                                            .source = element.source,
                                            .description = "Exact=" + exact};
        }
        if (!compatible.empty())
            return CompatibleConstraint(compatible, element.source);
        if (versionNode == element.children.end() && !hasStructuredAttributes) return std::nullopt;
        const auto &constraintNode = versionNode == element.children.end() ? element : *versionNode;
        SourcedVersionConstraint constraint{.source = constraintNode.source, .description = "structured Version"};
        const auto lower = [&](const std::string_view name, const bool inclusive) {
            if (const auto text = AttributeValue(constraintNode, name); !text.empty())
            {
                const auto parsed = ParseSemanticVersion(text);
                if (!parsed.has_value()) return;
                const VersionBoundary candidate{*parsed, inclusive};
                if (!constraint.lower.has_value() || constraint.lower->version < candidate.version)
                    constraint.lower = candidate;
                else if (constraint.lower->version == candidate.version)
                    constraint.lower->inclusive = constraint.lower->inclusive && candidate.inclusive;
            }
        };
        lower("AtLeast", true);
        lower("After", false);
        const auto upper = [&](const std::string_view name, const bool inclusive) {
            if (const auto text = AttributeValue(constraintNode, name); !text.empty())
            {
                const auto parsed = ParseSemanticVersion(text);
                if (!parsed.has_value()) return;
                const VersionBoundary candidate{*parsed, inclusive};
                if (!constraint.upper.has_value() || candidate.version < constraint.upper->version)
                    constraint.upper = candidate;
                else if (constraint.upper->version == candidate.version)
                    constraint.upper->inclusive = constraint.upper->inclusive && candidate.inclusive;
            }
        };
        upper("AtMost", true);
        upper("Before", false);
        const auto intersection = IntersectVersionConstraints(identity, {constraint});
        if (!intersection.Succeeded())
        {
            diagnostics.insert(diagnostics.end(), intersection.diagnostics.begin(), intersection.diagnostics.end());
            return std::nullopt;
        }
        return constraint;
    }

    auto VersionConstraintContains(const SourcedVersionConstraint &constraint, const SemanticVersion &version) -> bool
    {
        if (constraint.lower.has_value() &&
            (version < constraint.lower->version ||
             (version == constraint.lower->version && !constraint.lower->inclusive)))
            return false;
        if (constraint.upper.has_value() &&
            (constraint.upper->version < version ||
             (version == constraint.upper->version && !constraint.upper->inclusive)))
            return false;
        return true;
    }
}
