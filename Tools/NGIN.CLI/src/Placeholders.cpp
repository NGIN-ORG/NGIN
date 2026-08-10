#include "Placeholders.hpp"

#include "SemanticMerge.hpp"

#include <algorithm>
#include <array>
#include <regex>

namespace NGIN::CLI
{
    namespace
    {
        const auto Registry = std::array{
            PlaceholderSpec{"project.name", PlaceholderType::Identifier,
                            {PlaceholderPhase::Output, PlaceholderPhase::Stage, PlaceholderPhase::Launch,
                             PlaceholderPhase::Publish},
                            true},
            PlaceholderSpec{"project.version", PlaceholderType::SemanticVersion,
                            {PlaceholderPhase::Output, PlaceholderPhase::Publish}, true},
            PlaceholderSpec{"configuration", PlaceholderType::Identifier,
                            {PlaceholderPhase::Output, PlaceholderPhase::Stage, PlaceholderPhase::Launch,
                             PlaceholderPhase::Publish},
                            true},
            PlaceholderSpec{"target.os", PlaceholderType::Identifier,
                            {PlaceholderPhase::Output, PlaceholderPhase::Stage, PlaceholderPhase::Launch,
                             PlaceholderPhase::Publish},
                            true},
            PlaceholderSpec{"target.architecture", PlaceholderType::Identifier,
                            {PlaceholderPhase::Output, PlaceholderPhase::Stage, PlaceholderPhase::Launch,
                             PlaceholderPhase::Publish},
                            true},
            PlaceholderSpec{"output.name", PlaceholderType::Filename,
                            {PlaceholderPhase::Stage, PlaceholderPhase::Launch, PlaceholderPhase::Publish}, true},
            PlaceholderSpec{"workspace.root", PlaceholderType::Path, {PlaceholderPhase::LocalExecution}, false},
        };

        auto AddError(PlaceholderExpansionResult &result, std::string message, const ManifestSourceRange &source)
            -> void
        {
            result.diagnostics.push_back(ManifestDiagnostic{.severity = ManifestDiagnosticSeverity::Error,
                                                            .code = "NGIN2010",
                                                            .message = std::move(message),
                                                            .source = source});
        }

        [[nodiscard]] auto ValidValue(const PlaceholderValue &value) -> bool
        {
            if (value.value.empty()) return false;
            switch (value.type)
            {
            case PlaceholderType::Identifier:
            {
                static const std::regex pattern(R"([A-Za-z0-9][A-Za-z0-9._-]*)");
                return std::regex_match(value.value, pattern);
            }
            case PlaceholderType::SemanticVersion: return ParseSemanticVersion(value.value).has_value();
            case PlaceholderType::Filename:
                return value.value != "." && value.value != ".." && value.value.find('/') == std::string::npos &&
                       value.value.find('\\') == std::string::npos;
            case PlaceholderType::Path: return !value.value.empty();
            }
            return false;
        }
    }

    auto PlaceholderExpansionResult::Succeeded() const -> bool
    {
        return value.has_value() && diagnostics.empty();
    }

    auto PlaceholderRegistry() -> std::span<const PlaceholderSpec> { return Registry; }

    auto ExpandPlaceholders(const std::string_view input, const PlaceholderPhase phase,
                            const std::map<std::string, PlaceholderValue, std::less<>> &values,
                            const bool requireCanonicalIdentity, const ManifestSourceRange &source)
        -> PlaceholderExpansionResult
    {
        PlaceholderExpansionResult result{};
        result.canonicalIdentity = true;
        std::string expanded{};
        expanded.reserve(input.size());
        std::size_t position = 0;
        while (position < input.size())
        {
            const auto opening = input.find("${", position);
            if (opening == std::string_view::npos)
            {
                expanded += input.substr(position);
                break;
            }
            expanded += input.substr(position, opening - position);
            const auto closing = input.find('}', opening + 2);
            if (closing == std::string_view::npos)
            {
                AddError(result, "unterminated placeholder", source);
                return result;
            }
            const auto name = input.substr(opening + 2, closing - opening - 2);
            const auto registered = std::ranges::find(Registry, name, &PlaceholderSpec::name);
            if (registered == Registry.end())
            {
                AddError(result, "unknown placeholder '${" + std::string(name) + "}'", source);
                return result;
            }
            if (std::ranges::find(registered->phases, phase) == registered->phases.end())
            {
                AddError(result, "placeholder '${" + std::string(name) + "}' is not available in this phase", source);
                return result;
            }
            const auto value = values.find(name);
            if (value == values.end())
            {
                AddError(result, "placeholder '${" + std::string(name) + "}' has no resolved value", source);
                return result;
            }
            if (value->second.type != registered->type || !ValidValue(value->second))
            {
                AddError(result, "placeholder '${" + std::string(name) + "}' has an invalid typed value", source);
                return result;
            }
            if (value->second.value.find("${") != std::string::npos)
            {
                AddError(result, "placeholder values cannot contain recursive placeholders", source);
                return result;
            }
            if (!registered->canonicalIdentity)
            {
                result.canonicalIdentity = false;
                if (requireCanonicalIdentity)
                {
                    AddError(result, "placeholder '${" + std::string(name) + "}' is excluded from canonical identity",
                             source);
                    return result;
                }
            }
            expanded += value->second.value;
            position = closing + 1;
        }
        result.value = std::move(expanded);
        return result;
    }
}
