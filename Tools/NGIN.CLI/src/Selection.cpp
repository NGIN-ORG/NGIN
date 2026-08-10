#include "Selection.hpp"

#include "CompositionBoundary.hpp"

#include <algorithm>
#include <charconv>
#include <sstream>
#include <type_traits>

namespace NGIN::CLI
{
    namespace
    {
        auto AddSelectionError(std::vector<ManifestDiagnostic> &diagnostics, std::string code, std::string message,
                               const ManifestSourceRange &source,
                               std::vector<ManifestSourceRange> related = {}) -> void
        {
            diagnostics.push_back(ManifestDiagnostic{.severity = ManifestDiagnosticSeverity::Error,
                                                       .code = std::move(code),
                                                       .message = std::move(message),
                                                       .source = source,
                                                       .relatedSources = std::move(related)});
        }

        [[nodiscard]] auto OptionCanonicalValue(const TypedOptionValue &value) -> CanonicalValue
        {
            const auto type = [&] {
                switch (value.type)
                {
                case OptionType::Boolean: return "Boolean";
                case OptionType::Enumeration: return "Enum";
                case OptionType::String: return "String";
                case OptionType::Integer: return "Integer";
                case OptionType::Path: return "Path";
                }
                return "Unknown";
            }();
            auto canonicalValue = std::visit(
                [](const auto &item) -> CanonicalValue {
                    using T = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<T, bool>) return item;
                    else if constexpr (std::is_same_v<T, std::int64_t>) return item;
                    else if constexpr (std::is_same_v<T, PortablePath>) return item.value;
                    else return item;
                },
                value.value);
            return CanonicalValue::Object{{"type", type}, {"value", std::move(canonicalValue)}};
        }

        template <typename TValue>
        auto MergePresetScalar(std::optional<TValue> &target, const std::optional<TValue> &preset,
                               std::string_view name, const ManifestSourceRange &source,
                               std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            if (!preset.has_value()) return;
            if (target.has_value() && *target != *preset)
            {
                AddSelectionError(diagnostics, "NGIN2004",
                                  "explicit '" + std::string(name) + "' conflicts with preset value", source);
                return;
            }
            target = preset;
        }
    }

    auto OptionValueResult::Succeeded() const -> bool { return value.has_value() && diagnostics.empty(); }

    auto ParseOptionValue(const OptionDefinition &definition, const std::string_view authored,
                          const ManifestSourceRange &source) -> OptionValueResult
    {
        OptionValueResult result{};
        const auto error = [&](const std::string &detail) {
            AddSelectionError(result.diagnostics, "NGIN2003",
                              "invalid value for Option '" + definition.name + "': " + detail, source,
                              {definition.source});
        };
        switch (definition.type)
        {
        case OptionType::Boolean:
            if (authored == "true") result.value = TypedOptionValue{.type = OptionType::Boolean, .value = true};
            else if (authored == "false") result.value = TypedOptionValue{.type = OptionType::Boolean, .value = false};
            else error("expected true or false");
            break;
        case OptionType::Enumeration:
            if (!definition.allowedValues.contains(authored)) error("value is not a declared Enum member");
            else result.value = TypedOptionValue{.type = OptionType::Enumeration, .value = std::string(authored)};
            break;
        case OptionType::String:
            if (authored.empty()) error("String cannot be empty");
            else if (!definition.allowedValues.empty() && !definition.allowedValues.contains(authored))
                error("String is not in the declared allowed set");
            else result.value = TypedOptionValue{.type = OptionType::String, .value = std::string(authored)};
            break;
        case OptionType::Integer:
        {
            std::int64_t value{};
            const auto parsed = std::from_chars(authored.data(), authored.data() + authored.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != authored.data() + authored.size()) error("expected integer");
            else if (definition.minimum.has_value() && value < *definition.minimum) error("below declared minimum");
            else if (definition.maximum.has_value() && value > *definition.maximum) error("above declared maximum");
            else result.value = TypedOptionValue{.type = OptionType::Integer, .value = value};
            break;
        }
        case OptionType::Path:
        {
            const auto path = NormalizePortablePath(authored, PortablePathBase::Manifest, source);
            if (!path.Succeeded()) result.diagnostics = path.diagnostics;
            else result.value = TypedOptionValue{.type = OptionType::Path, .value = *path.value};
            break;
        }
        }
        return result;
    }

    auto CanonicalOptionValue(const TypedOptionValue &value) -> std::string
    {
        return SerializeCanonical(OptionCanonicalValue(value));
    }

    auto CanonicalTargetIdentity(const Target &target) -> std::string
    {
        return CanonicalDigestInput("Target", {{"architecture", target.architecture},
                                                {"operatingSystem", target.operatingSystem}});
    }

    auto CanonicalToolchainIdentity(const Toolchain &toolchain) -> std::string
    {
        CanonicalValue::Object fields{{"compiler", toolchain.compiler},
                                      {"compilerVersion", toolchain.compilerVersion},
                                      {"linker", toolchain.linker},
                                      {"runtimeLibrary", toolchain.runtimeLibrary}};
        if (toolchain.toolchainFile.has_value()) fields["toolchainFile"] = toolchain.toolchainFile->value;
        return CanonicalDigestInput("Toolchain", fields);
    }

    auto DeriveBinaryCompatibility(const SelectionFacts &selection, std::string linkage,
                                   const std::map<std::string, OptionDefinition, std::less<>> &definitions)
        -> BinaryCompatibility
    {
        BinaryCompatibility compatibility{
            .operatingSystem = selection.target.operatingSystem,
            .architecture = selection.target.architecture,
            .compiler = selection.toolchain.compiler,
            .compilerVersion = selection.toolchain.compilerVersion,
            .runtimeLibrary = selection.toolchain.runtimeLibrary,
            .configuration = selection.configuration.name,
            .linkage = std::move(linkage),
        };
        for (const auto &[name, value] : selection.options)
        {
            const auto definition = definitions.find(name);
            if (definition != definitions.end() && definition->second.artifact)
                compatibility.artifactOptions.emplace(name, CanonicalOptionValue(value));
        }
        return compatibility;
    }

    auto CanonicalSelection(const SelectionFacts &selection) -> CanonicalValue
    {
        CanonicalValue::Object options{};
        for (const auto &[name, value] : selection.options) options.emplace(name, OptionCanonicalValue(value));
        return CanonicalValue::Object{
            {"configuration",
             CanonicalValue::Object{{"debugSymbols", selection.configuration.debugSymbols},
                                    {"linkTimeOptimization", selection.configuration.linkTimeOptimization},
                                    {"name", selection.configuration.name},
                                    {"optimization", selection.configuration.optimization}}},
            {"options", std::move(options)},
            {"target", CanonicalValue::Object{{"architecture", selection.target.architecture},
                                               {"operatingSystem", selection.target.operatingSystem}}},
            {"toolchain", CanonicalValue::Object{{"compiler", selection.toolchain.compiler},
                                                  {"compilerVersion", selection.toolchain.compilerVersion},
                                                  {"linker", selection.toolchain.linker},
                                                  {"runtimeLibrary", selection.toolchain.runtimeLibrary}}},
        };
    }

    auto PresetExpansionResult::Succeeded() const -> bool { return value.has_value() && diagnostics.empty(); }

    auto ExpandPreset(const Preset &preset, const std::string_view command, const SelectionRequest &explicitRequest)
        -> PresetExpansionResult
    {
        PresetExpansionResult result{};
        if (preset.command != command)
        {
            AddSelectionError(result.diagnostics, "NGIN2004",
                              "preset '" + preset.name + "' is for command '" + preset.command + "', not '" +
                                  std::string(command) + "'",
                              preset.source);
            return result;
        }
        auto expanded = explicitRequest;
        MergePresetScalar(expanded.configuration, preset.selection.configuration, "configuration", preset.source,
                          result.diagnostics);
        MergePresetScalar(expanded.target, preset.selection.target, "target", preset.source, result.diagnostics);
        MergePresetScalar(expanded.toolchain, preset.selection.toolchain, "toolchain", preset.source,
                          result.diagnostics);
        MergePresetScalar(expanded.launch, preset.selection.launch, "launch", preset.source, result.diagnostics);
        for (const auto &[name, value] : preset.selection.options)
        {
            if (const auto existing = expanded.options.find(name);
                existing != expanded.options.end() && existing->second != value)
            {
                AddSelectionError(result.diagnostics, "NGIN2004",
                                  "explicit Option '" + name + "' conflicts with preset value", preset.source);
            }
            else
            {
                expanded.options[name] = value;
            }
        }
        if (result.diagnostics.empty()) result.value = std::move(expanded);
        return result;
    }

    auto ResolveTargetAlias(const std::string_view name, const std::vector<Target> &targets, const Target &host,
                            const ManifestSourceRange &source)
        -> std::pair<std::optional<Target>, std::vector<ManifestDiagnostic>>
    {
        if (name == "host" || name == host.name || host.aliases.contains(name)) return {host, {}};
        const auto found = std::ranges::find_if(targets, [&](const Target &target) {
            return target.name == name || target.aliases.contains(name);
        });
        if (found != targets.end()) return {*found, {}};
        std::vector<ManifestDiagnostic> diagnostics{};
        AddSelectionError(diagnostics, "NGIN2005", "unknown Target alias '" + std::string(name) + "'", source);
        return {std::nullopt, std::move(diagnostics)};
    }

    auto RefinementResult::Succeeded() const -> bool { return diagnostics.empty(); }

    auto RefinementMatches(const RefinementSelector &selector, const SelectionFacts &selection) -> bool
    {
        if (selector.configuration.has_value() && *selector.configuration != selection.configuration.name) return false;
        if (selector.targetName.has_value() && *selector.targetName != selection.target.name &&
            !selection.target.aliases.contains(*selector.targetName))
            return false;
        if (selector.targetOperatingSystem.has_value() &&
            *selector.targetOperatingSystem != selection.target.operatingSystem)
            return false;
        if (selector.targetArchitecture.has_value() && *selector.targetArchitecture != selection.target.architecture)
            return false;
        if (selector.toolchainName.has_value() && *selector.toolchainName != selection.toolchain.name) return false;
        if (selector.compiler.has_value() && *selector.compiler != selection.toolchain.compiler) return false;
        for (const auto &[name, value] : selector.options)
        {
            const auto selected = selection.options.find(name);
            if (selected == selection.options.end() || selected->second != value) return false;
        }
        return true;
    }

    auto RefinementSpecificity(const RefinementSelector &selector) -> std::size_t
    {
        std::size_t specificity = selector.configuration.has_value() ? 1 : 0;
        if (selector.targetName.has_value() || selector.targetOperatingSystem.has_value() ||
            selector.targetArchitecture.has_value())
            ++specificity;
        if (selector.toolchainName.has_value() || selector.compiler.has_value()) ++specificity;
        specificity += selector.options.size();
        return specificity;
    }

    auto ResolveRefinements(const SelectionFacts &selection, const std::vector<SemanticRefinement> &refinements)
        -> RefinementResult
    {
        struct Selected
        {
            RefinementAssignment assignment{};
            std::size_t specificity{0};
        };
        std::map<std::string, Selected, std::less<>> selected{};
        RefinementResult result{};
        for (const auto &refinement : refinements)
        {
            if (!RefinementMatches(refinement.selector, selection)) continue;
            const auto specificity = RefinementSpecificity(refinement.selector);
            for (const auto &assignment : refinement.assignments)
            {
                const auto key = assignment.category + "\x1f" + assignment.identity;
                const auto existing = selected.find(key);
                if (existing == selected.end() || specificity > existing->second.specificity)
                {
                    selected[key] = Selected{.assignment = assignment, .specificity = specificity};
                }
                else if (specificity == existing->second.specificity &&
                         assignment.value != existing->second.assignment.value)
                {
                    AddSelectionError(result.diagnostics, "NGIN2006",
                                      "equal-specificity refinements conflict for " + assignment.category + " '" +
                                          assignment.identity + "'",
                                      assignment.source, {existing->second.assignment.source});
                }
            }
        }
        for (auto &[key, value] : selected) result.assignments.emplace(std::move(key), std::move(value.assignment));
        return result;
    }
}
