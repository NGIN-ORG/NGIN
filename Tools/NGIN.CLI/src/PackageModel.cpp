#include "PackageModel.hpp"

#include "Canonical.hpp"

#include <NGIN/Serialization/Core/SourceBuffer.hpp>
#include <NGIN/Serialization/JSON/JsonParser.hpp>

#include <algorithm>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>

namespace NGIN::CLI
{
    namespace
    {
        using JsonObject = NGIN::Serialization::JSON::ObjectView;
        using JsonValue = NGIN::Serialization::JSON::ValueView;

        [[nodiscard]] auto AttributeValue(const AuthoredElement &element, const std::string_view name,
                                          std::string fallback = {}) -> std::string
        {
            const auto *attribute = element.Attribute(name);
            return attribute == nullptr ? std::move(fallback) : attribute->value;
        }

        [[nodiscard]] auto HasAttribute(const AuthoredElement &element, const std::string_view name) -> bool
        {
            return element.Attribute(name) != nullptr;
        }

        [[nodiscard]] auto BoolAttributeValue(const AuthoredElement &element, const std::string_view name,
                                              const bool fallback = false) -> bool
        {
            const auto *attribute = element.Attribute(name);
            return attribute == nullptr ? fallback : attribute->value == "true";
        }

        [[nodiscard]] auto Child(const AuthoredElement &element, const std::string_view specId)
            -> const AuthoredElement *
        {
            const auto found = std::ranges::find(element.children, specId, &AuthoredElement::specId);
            return found == element.children.end() ? nullptr : &*found;
        }

        auto AddError(std::vector<ManifestDiagnostic> &diagnostics, std::string code, std::string message,
                      const ManifestSourceRange &source = {}, std::vector<ManifestSourceRange> related = {}) -> void
        {
            diagnostics.push_back(ManifestDiagnostic{.severity = ManifestDiagnosticSeverity::Error,
                                                       .code = std::move(code),
                                                       .message = std::move(message),
                                                       .source = source,
                                                       .relatedSources = std::move(related)});
        }

        [[nodiscard]] auto ReadFileText(const fs::path &path) -> std::optional<std::string>
        {
            std::ifstream input(path, std::ios::binary);
            if (!input) return std::nullopt;
            std::ostringstream text{};
            text << input.rdbuf();
            return text.str();
        }

        [[nodiscard]] auto JsonString(const JsonObject &object, const std::string_view key)
            -> std::optional<std::string>
        {
            const auto value = object.Find(key);
            if (!value.has_value()) return std::nullopt;
            const auto text = value->TryString();
            return text.has_value() ? std::optional<std::string>{*text} : std::nullopt;
        }

        auto ReadCpsStringArray(const JsonObject &object, const std::string_view key,
                                const ManifestSourceRange &source, std::vector<ManifestDiagnostic> &diagnostics)
            -> std::vector<std::string>
        {
            std::vector<std::string> result{};
            const auto value = object.Find(key);
            if (!value.has_value()) return result;
            const auto array = value->TryArray();
            if (!array.has_value())
            {
                AddError(diagnostics, "NGIN4011", "CPS component field '" + std::string(key) +
                                                       "' must be an array",
                         source);
                return result;
            }
            for (const auto entry : *array)
            {
                const auto text = entry.TryString();
                if (!text.has_value())
                    AddError(diagnostics, "NGIN4011", "CPS component field '" + std::string(key) +
                                                           "' must contain strings",
                             source);
                else
                    result.emplace_back(*text);
            }
            return result;
        }

        auto ReadCpsStringList(const JsonObject &object, const std::string_view key,
                               const ManifestSourceRange &source, std::vector<ManifestDiagnostic> &diagnostics)
            -> std::vector<std::string>
        {
            const auto value = object.Find(key);
            if (!value.has_value()) return {};
            if (value->TryArray().has_value()) return ReadCpsStringArray(object, key, source, diagnostics);
            const auto languages = value->TryObject();
            if (!languages.has_value())
            {
                AddError(diagnostics, "NGIN4011", "CPS component field '" + std::string(key) +
                                                       "' must be an array or language map",
                         source);
                return {};
            }
            std::vector<std::string> result{};
            for (const auto &language : *languages)
            {
                if (language.Key() != "*" && language.Key() != "cpp") continue;
                const auto entries = language.Value().TryArray();
                if (!entries.has_value())
                {
                    AddError(diagnostics, "NGIN4011", "CPS language field '" + std::string(key) +
                                                           "' must contain arrays",
                             source);
                    continue;
                }
                for (const auto entry : *entries)
                    if (const auto text = entry.TryString(); text.has_value()) result.emplace_back(*text);
                    else AddError(diagnostics, "NGIN4011", "CPS language field '" + std::string(key) +
                                                                "' must contain strings",
                                  source);
            }
            return result;
        }

        auto ReadCpsDefinitions(const JsonObject &component, const ManifestSourceRange &source,
                                std::vector<ManifestDiagnostic> &diagnostics) -> std::vector<std::string>
        {
            const auto value = component.Find("definitions");
            if (!value.has_value()) return {};
            const auto languages = value->TryObject();
            if (!languages.has_value())
            {
                AddError(diagnostics, "NGIN4011", "CPS component field 'definitions' must be a language map",
                         source);
                return {};
            }
            std::vector<std::string> result{};
            for (const auto &language : *languages)
            {
                if (language.Key() != "*" && language.Key() != "cpp") continue;
                const auto definitions = language.Value().TryObject();
                if (!definitions.has_value())
                {
                    AddError(diagnostics, "NGIN4011", "CPS definitions language entries must be objects", source);
                    continue;
                }
                for (const auto &definition : *definitions)
                {
                    if (definition.Value().IsNull()) result.emplace_back(definition.Key());
                    else if (const auto text = definition.Value().TryString(); text.has_value())
                        result.emplace_back(std::string(definition.Key()) + "=" + std::string(*text));
                    else AddError(diagnostics, "NGIN4011", "CPS definition values must be strings or null", source);
                }
            }
            return result;
        }

        [[nodiscard]] auto CpsPrefix(const JsonObject &root, const fs::path &cpsPath,
                                     const ManifestSourceRange &source,
                                     std::vector<ManifestDiagnostic> &diagnostics) -> std::optional<fs::path>
        {
            const auto prefix = JsonString(root, "prefix");
            const auto authoredCpsPath = JsonString(root, "cps_path");
            if (prefix.has_value() == authoredCpsPath.has_value())
            {
                AddError(diagnostics, "NGIN4011", "CPS requires exactly one of 'prefix' or 'cps_path'", source);
                return std::nullopt;
            }
            if (prefix.has_value()) return fs::path(*prefix).lexically_normal();
            constexpr std::string_view token{"@prefix@"};
            if (!authoredCpsPath->starts_with(token))
            {
                AddError(diagnostics, "NGIN4011", "CPS 'cps_path' must start with '@prefix@'", source);
                return std::nullopt;
            }
            auto suffix = fs::path(authoredCpsPath->substr(token.size())).relative_path();
            auto prefixPath = cpsPath.parent_path();
            if (suffix.empty()) return prefixPath.lexically_normal();
            for (const auto &segment : suffix)
            {
                if (segment == "." || segment.empty()) continue;
                if (segment == "..")
                {
                    AddError(diagnostics, "NGIN4011", "CPS 'cps_path' cannot escape its prefix", source);
                    return std::nullopt;
                }
                prefixPath = prefixPath.parent_path();
            }
            if ((prefixPath / suffix).lexically_normal() != cpsPath.parent_path().lexically_normal())
            {
                AddError(diagnostics, "NGIN4011", "CPS file location does not match its 'cps_path'", source);
                return std::nullopt;
            }
            return prefixPath.lexically_normal();
        }

        [[nodiscard]] auto ResolveCpsPath(const std::string_view authored, const fs::path &cpsPath,
                                          const fs::path &prefix) -> std::string
        {
            constexpr std::string_view token{"@prefix@"};
            if (authored.starts_with(token))
                return (prefix / fs::path(authored.substr(token.size())).relative_path()).lexically_normal().generic_string();
            const auto path = fs::path(authored);
            return (path.is_absolute() ? path : cpsPath.parent_path() / path).lexically_normal().generic_string();
        }

        auto AddCpsComponentRequirements(const JsonObject &component, const std::string &packageName,
                                         const ManifestSourceRange &source,
                                         std::vector<SemanticRequirement> &requirements,
                                         std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            for (const auto field : {"requires", "compile_requires", "link_requires"})
            {
                const auto value = component.Find(field);
                if (!value.has_value()) continue;
                const auto array = value->TryArray();
                if (!array.has_value())
                {
                    AddError(diagnostics, "NGIN4011", "CPS component field '" + std::string(field) +
                                                           "' must be an array",
                             source);
                    continue;
                }
                for (const auto entry : *array)
                {
                    const auto authored = entry.TryString();
                    if (!authored.has_value())
                    {
                        AddError(diagnostics, "NGIN4011", "CPS component requirement must be a string", source);
                        continue;
                    }
                    auto name = std::string{*authored};
                    if (const auto configuration = name.find('@'); configuration != std::string::npos)
                        name.erase(configuration);
                    if (name.starts_with(':'))
                    {
                        requirements.emplace_back(SemanticExportRequirement{
                            .kind = ExportUseKind::Library,
                            .name = name.substr(1),
                            .visibility = RequirementVisibility::Public,
                            .source = source,
                        });
                        continue;
                    }
                    const auto separator = name.find(':');
                    if (separator == std::string::npos)
                    {
                        AddError(diagnostics, "NGIN4011", "CPS requirement '" + name +
                                                               "' is not a component specification",
                                 source);
                        continue;
                    }
                    const auto dependencyPackage = name.substr(0, separator);
                    const auto componentName = name.substr(separator + 1);
                    if (dependencyPackage == packageName)
                        requirements.emplace_back(SemanticExportRequirement{
                            .kind = ExportUseKind::Library,
                            .name = componentName,
                            .visibility = RequirementVisibility::Public,
                            .source = source,
                        });
                    else
                        requirements.emplace_back(SemanticPackageRequirement{
                            .name = dependencyPackage,
                            .exports = {ExportUse{.kind = ExportUseKind::Library,
                                                 .name = componentName,
                                                 .source = source}},
                            .visibility = RequirementVisibility::Public,
                            .source = source,
                        });
                }
            }
        }

        auto ImportCps(const AuthoredElement &import, const AuthoredPackageManifest &package,
                       SemanticPackage &semantic, std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            const auto cpsPath = package.manifest.path.parent_path() / AttributeValue(import, "Cps");
            const auto source = ManifestSourceRange{.path = cpsPath};
            const auto text = ReadFileText(cpsPath);
            if (!text.has_value())
            {
                AddError(diagnostics, "NGIN4011", "cannot read CPS import '" + cpsPath.generic_string() + "'",
                         import.source);
                return;
            }
            auto parsed = NGIN::Serialization::JSON::Parse(NGIN::Serialization::OwnedTextBuffer{*text});
            if (!parsed.HasValue())
            {
                const auto &error = parsed.Error();
                AddError(diagnostics, "NGIN4011", "invalid CPS JSON: " + std::string(error.message.View()),
                         ManifestSourceRange{.path = cpsPath,
                                             .begin = ManifestSourcePosition{.line = error.location.line,
                                                                             .column = error.location.column}});
                return;
            }
            const auto root = parsed.Value().Root().TryObject();
            if (!root.has_value())
            {
                AddError(diagnostics, "NGIN4011", "CPS root must be an object", source);
                return;
            }
            const auto cpsVersion = JsonString(*root, "cps_version");
            const auto cpsName = JsonString(*root, "name");
            if (!cpsVersion.has_value() || !cpsName.has_value())
            {
                AddError(diagnostics, "NGIN4011", "CPS requires string cps_version and name fields", source);
                return;
            }
            if (*cpsName != package.name)
            {
                AddError(diagnostics, "NGIN4011", "CPS package name '" + *cpsName +
                                                       "' does not match overlay package '" + package.name + "'",
                         source);
                return;
            }
            const auto prefix = CpsPrefix(*root, cpsPath, source, diagnostics);
            if (!prefix.has_value()) return;
            if (const auto value = root->Find("requires"); value.has_value())
            {
                const auto packageRequirements = value->TryObject();
                if (!packageRequirements.has_value())
                    AddError(diagnostics, "NGIN4011", "CPS package requires must be an object", source);
                else
                    for (const auto member : *packageRequirements)
                    {
                        SemanticPackageRequirement requirement{
                            .name = std::string(member.Key()),
                            .visibility = RequirementVisibility::Public,
                            .source = source,
                        };
                        if (const auto object = member.Value().TryObject(); object.has_value())
                            if (const auto componentsValue = object->Find("components"); componentsValue.has_value())
                                if (const auto components = componentsValue->TryArray(); components.has_value())
                                    for (const auto component : *components)
                                        if (const auto name = component.TryString(); name.has_value())
                                            requirement.exports.push_back(ExportUse{
                                                .kind = ExportUseKind::Library,
                                                .name = std::string{*name},
                                                .source = source,
                                            });
                        semantic.requirements.emplace_back(std::move(requirement));
                    }
            }
            std::set<std::string, std::less<>> defaults{};
            if (const auto value = root->Find("default_components"); value.has_value())
                if (const auto array = value->TryArray(); array.has_value())
                    for (const auto entry : *array)
                        if (const auto name = entry.TryString(); name.has_value()) defaults.emplace(*name);
            const auto componentValue = root->Find("components");
            const auto components = componentValue.has_value() ? componentValue->TryObject() : std::nullopt;
            if (!components.has_value())
            {
                AddError(diagnostics, "NGIN4011", "CPS components must be an object", source);
                return;
            }
            for (const auto member : *components)
            {
                const auto component = member.Value().TryObject();
                if (!component.has_value())
                {
                    AddError(diagnostics, "NGIN4011", "CPS component '" + std::string(member.Key()) +
                                                           "' must be an object",
                             source);
                    continue;
                }
                const auto type = JsonString(*component, "type");
                if (!type.has_value())
                {
                    AddError(diagnostics, "NGIN4011", "CPS component '" + std::string(member.Key()) +
                                                           "' requires a type",
                             source);
                    continue;
                }
                std::optional<ExportUseKind> kind{};
                if (*type == "executable") kind = ExportUseKind::Tool;
                else if (*type == "module") kind = ExportUseKind::Plugin;
                else if (*type == "archive" || *type == "dylib" || *type == "interface")
                    kind = ExportUseKind::Library;
                else if (*type == "symbolic") kind = ExportUseKind::Asset;
                else
                    continue;
                PackageExport exportModel{.kind = *kind,
                                          .name = std::string(member.Key()),
                                          .defaultExport = defaults.contains(member.Key()),
                                          .source = source};
                CpsComponentMetadata metadata{.type = *type};
                if (const auto location = JsonString(*component, "location"); location.has_value())
                    metadata.location = ResolveCpsPath(*location, cpsPath, *prefix);
                for (const auto &include : ReadCpsStringList(*component, "includes", source, diagnostics))
                    metadata.includeDirectories.push_back(ResolveCpsPath(include, cpsPath, *prefix));
                metadata.compileDefinitions = ReadCpsDefinitions(*component, source, diagnostics);
                metadata.compileOptions = ReadCpsStringList(*component, "compile_flags", source, diagnostics);
                metadata.linkOptions = ReadCpsStringArray(*component, "link_flags", source, diagnostics);
                exportModel.cps = std::move(metadata);
                AddCpsComponentRequirements(*component, package.name, source, exportModel.requirements, diagnostics);
                if (const auto [existing, inserted] = semantic.exports.emplace(exportModel.name, exportModel);
                    !inserted)
                    AddError(diagnostics, "NGIN4011", "CPS component '" + exportModel.name +
                                                           "' conflicts with an authored export",
                             source, {existing->second.source});
            }
        }

        [[nodiscard]] auto ArtifactIdentity(const PackageProviderResult &result) -> std::string
        {
            if (!result.artifactIdentity.empty()) return result.artifactIdentity;
            if (!result.integrity.empty()) return result.integrity;
            if (!result.revision.empty()) return result.nativeIdentity + "#" + result.revision;
            return result.nativeIdentity;
        }

        [[nodiscard]] auto ParseVisibility(const AuthoredElement &element,
                                           const RequirementVisibility fallback) -> RequirementVisibility
        {
            return BoolAttributeValue(element, "Public") ? RequirementVisibility::Public : fallback;
        }

        [[nodiscard]] auto ParseDomain(const std::string_view value) -> CapabilityDomain
        {
            if (value == "Acquisition") return CapabilityDomain::Acquisition;
            if (value == "Build") return CapabilityDomain::Build;
            if (value == "Generation") return CapabilityDomain::Generation;
            if (value == "Artifact") return CapabilityDomain::Artifact;
            if (value == "Deployment") return CapabilityDomain::Deployment;
            return CapabilityDomain::Link;
        }

        [[nodiscard]] auto ParseActionKind(const AuthoredElement &element) -> ActionKind
        {
            if (element.name == "Generator") return ActionKind::Generate;
            if (element.name == "Analyzer") return ActionKind::Analyze;
            if (element.name == "Formatter") return ActionKind::Format;
            if (element.name == "Validator") return ActionKind::Validate;
            return ActionKind::Custom;
        }

        [[nodiscard]] auto ParseActionInputKind(const std::string_view value) -> ActionInputKind
        {
            if (value == "Header") return ActionInputKind::Header;
            if (value == "Source") return ActionInputKind::Source;
            return ActionInputKind::File;
        }

        [[nodiscard]] auto ParseActionOutputKind(const std::string_view value) -> ActionOutputKind
        {
            if (value == "Source") return ActionOutputKind::Source;
            if (value == "Header") return ActionOutputKind::Header;
            if (value == "Directory") return ActionOutputKind::Directory;
            return ActionOutputKind::File;
        }

        [[nodiscard]] auto ParseExportKind(const AuthoredElement &element) -> ExportUseKind
        {
            if (element.name == "Tool") return ExportUseKind::Tool;
            if (element.name == "Plugin") return ExportUseKind::Plugin;
            if (element.name == "Generator" || element.name == "Analyzer" || element.name == "Formatter" ||
                element.name == "Validator" || element.name == "Action")
                return ExportUseKind::Action;
            if (element.name == "Asset") return ExportUseKind::Asset;
            if (element.name == "Library") return ExportUseKind::Library;
            if (HasAttribute(element, "Tool")) return ExportUseKind::Tool;
            if (HasAttribute(element, "Plugin")) return ExportUseKind::Plugin;
            if (HasAttribute(element, "Action")) return ExportUseKind::Action;
            if (HasAttribute(element, "Asset")) return ExportUseKind::Asset;
            return ExportUseKind::Library;
        }

        [[nodiscard]] auto ExportName(const AuthoredElement &element) -> std::string
        {
            return AttributeValue(element, "Name");
        }

        [[nodiscard]] auto VersionText(const SemanticVersion &version) -> std::string
        {
            std::ostringstream out;
            out << version.major << '.' << version.minor << '.' << version.patch;
            if (!version.prerelease.empty())
            {
                out << '-';
                for (std::size_t index = 0; index < version.prerelease.size(); ++index)
                {
                    if (index != 0) out << '.';
                    out << version.prerelease[index];
                }
            }
            return out.str();
        }

        [[nodiscard]] auto ParseCondition(const AuthoredElement &node,
                                          const std::map<std::string, OptionDefinition, std::less<>> &options,
                                          std::vector<ManifestDiagnostic> &diagnostics)
            -> std::optional<RequirementCondition>
        {
            RequirementCondition condition{.source = node.source};
            const auto option = AttributeValue(node, "Option");
            const auto equals = AttributeValue(node, "Equals");
            const auto targetOs = AttributeValue(node, "OS");
            const auto architecture = AttributeValue(node, "Architecture");
            const auto compiler = AttributeValue(node, "Compiler");
            const auto hasTarget = !targetOs.empty() || !architecture.empty() || !compiler.empty();
            if ((!option.empty() || !equals.empty()) && hasTarget)
                AddError(diagnostics, "NGIN4001", "When cannot mix an Option predicate with Target/Toolchain facts",
                         node.source);
            if (option.empty() != equals.empty())
                AddError(diagnostics, "NGIN4001", "When Option and Equals must be written together", node.source);
            if (option.empty() && !hasTarget)
                AddError(diagnostics, "NGIN4001", "When requires one structured predicate", node.source);
            if (!option.empty())
            {
                condition.option = option;
                const auto definition = options.find(option);
                if (definition == options.end())
                    AddError(diagnostics, "NGIN4001", "When references undeclared Option '" + option + "'", node.source);
                else
                {
                    const auto parsed = ParseOptionValue(definition->second, equals, node.source);
                    if (!parsed.Succeeded())
                        diagnostics.insert(diagnostics.end(), parsed.diagnostics.begin(), parsed.diagnostics.end());
                    else
                        condition.equals = CanonicalOptionValue(*parsed.value);
                }
            }
            if (!targetOs.empty()) condition.targetOperatingSystem = targetOs;
            if (!architecture.empty()) condition.targetArchitecture = architecture;
            if (!compiler.empty()) condition.compiler = compiler;
            return condition;
        }

        auto ParseRequirements(const AuthoredElement &requiresElement,
                               const RequirementVisibility fallbackVisibility,
                               const std::map<std::string, OptionDefinition, std::less<>> &options,
                               const std::string &requester, std::vector<SemanticRequirement> &requirements,
                               std::vector<ManifestDiagnostic> &diagnostics,
                               const std::optional<RequirementCondition> &inheritedCondition = std::nullopt) -> void
        {
            for (const auto &node : requiresElement.children)
            {
                if (node.name == "When")
                {
                    const auto condition = ParseCondition(node, options, diagnostics);
                    ParseRequirements(node, fallbackVisibility, options, requester, requirements, diagnostics, condition);
                    continue;
                }
                if (node.name == "Package")
                {
                    SemanticPackageRequirement requirement{
                        .name = AttributeValue(node, "Name"),
                        .constraint = ParseAuthoredVersionConstraint(node, AttributeValue(node, "Name"), diagnostics),
                        .visibility = ParseVisibility(node, fallbackVisibility),
                        .condition = inheritedCondition,
                        .source = node.source,
                    };
                    for (const auto &child : node.children)
                    {
                        if (child.name == "Library" || child.name == "Tool" || child.name == "Plugin" ||
                            child.name == "Generator" || child.name == "Analyzer" || child.name == "Formatter" ||
                            child.name == "Validator" || child.name == "Action" || child.name == "Asset")
                            requirement.exports.push_back(ExportUse{.kind = ParseExportKind(child),
                                                                    .name = ExportName(child),
                                                                    .source = child.source});
                        else if (child.name == "Option")
                        {
                            const auto name = AttributeValue(child, "Name");
                            const auto value = AttributeValue(child, "Value");
                            if (const auto [existing, inserted] = requirement.optionAssignments.emplace(name, value);
                                !inserted && existing->second != value)
                                AddError(diagnostics, "NGIN4002", "conflicting package Option assignment '" + name + "'",
                                         child.source);
                        }
                    }
                    requirements.emplace_back(std::move(requirement));
                }
                else if (node.name == "Project")
                {
                    const auto projectPath = AttributeValue(node, "Path");
                    SemanticProjectRequirement requirement{.name = std::filesystem::path(projectPath).stem().string(),
                                                           .visibility = ParseVisibility(node, fallbackVisibility),
                                                           .condition = inheritedCondition,
                                                           .source = node.source};
                    if (const auto path = AttributeValue(node, "Path"); !path.empty())
                    {
                        const auto parsed = NormalizePortablePath(path, PortablePathBase::Manifest, node.source);
                        if (parsed.Succeeded()) requirement.path = *parsed.value;
                        else diagnostics.insert(diagnostics.end(), parsed.diagnostics.begin(), parsed.diagnostics.end());
                    }
                    requirements.emplace_back(std::move(requirement));
                }
                else if (node.name == "Library" || node.name == "Tool" || node.name == "Plugin" ||
                         node.name == "Generator" || node.name == "Analyzer" || node.name == "Formatter" ||
                         node.name == "Validator" || node.name == "Action" || node.name == "Asset")
                {
                    requirements.emplace_back(SemanticExportRequirement{.kind = ParseExportKind(node),
                                                                         .name = ExportName(node),
                                                                         .visibility = ParseVisibility(node, fallbackVisibility),
                                                                         .condition = inheritedCondition,
                                                                         .source = node.source});
                }
                else if (node.name == "Capability")
                {
                    auto constraint = ParseAuthoredVersionConstraint(node, AttributeValue(node, "Name"), diagnostics);
                    if (!constraint.has_value())
                        AddError(diagnostics, "NGIN4003", "Capability requirement must declare a Version constraint",
                                 node.source);
                    requirements.emplace_back(SemanticCapabilityRequirement{
                        .name = AttributeValue(node, "Name"),
                        .domain = ParseDomain(AttributeValue(node, "Domain")),
                        .constraint = std::move(constraint),
                        .visibility = fallbackVisibility,
                        .condition = inheritedCondition,
                        .requester = requester,
                        .source = node.source,
                    });
                }
                else if (node.name == "Option")
                {
                    const auto name = AttributeValue(node, "Name");
                    const auto value = AttributeValue(node, "Value");
                    const auto definition = options.find(name);
                    if (definition == options.end())
                        AddError(diagnostics, "NGIN4001", "Requires references undeclared Option '" + name + "'",
                                 node.source);
                    else
                    {
                        const auto parsed = ParseOptionValue(definition->second, value, node.source);
                        if (!parsed.Succeeded())
                            diagnostics.insert(diagnostics.end(), parsed.diagnostics.begin(), parsed.diagnostics.end());
                        else
                            requirements.emplace_back(SemanticOptionPredicate{
                                .name = name, .value = CanonicalOptionValue(*parsed.value), .source = node.source});
                    }
                }
            }
        }

        auto ParseContribution(const AuthoredElement &node, const ContributionKind kind, const std::string &owner,
                               std::vector<PackageContribution> &contributions,
                               std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            const auto include = NormalizePortablePath(AttributeValue(node, "From"), PortablePathBase::Manifest,
                                                       node.source);
            const auto destination = NormalizeStageDestination(AttributeValue(node, "To"), node.source);
            if (!include.Succeeded())
                diagnostics.insert(diagnostics.end(), include.diagnostics.begin(), include.diagnostics.end());
            if (!destination.Succeeded())
                diagnostics.insert(diagnostics.end(), destination.diagnostics.begin(), destination.diagnostics.end());
            if (include.Succeeded() && destination.Succeeded())
                contributions.push_back(PackageContribution{.kind = kind,
                                                            .owner = owner,
                                                            .include = *include.value,
                                                            .destination = *destination.value,
                                                            .source = node.source});
        }

        auto ParseContributionContainer(const AuthoredElement &container, const std::string &owner,
                                        std::vector<PackageContribution> &contributions,
                                        std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            if (container.name == "Notices")
            {
                for (const auto &notice : container.children)
                    ParseContribution(notice, ContributionKind::Notice, owner, contributions, diagnostics);
                return;
            }
            if (container.name == "RuntimeFiles")
            {
                for (const auto &file : container.children)
                    ParseContribution(file, file.name == "Directory" ? ContributionKind::RuntimeDirectory
                                                                       : ContributionKind::RuntimeFile,
                                      owner, contributions, diagnostics);
                return;
            }
            for (const auto &node : container.children)
            {
                if (node.name == "Notices" || node.name == "RuntimeFiles")
                    ParseContributionContainer(node, owner, contributions, diagnostics);
                else if (node.name == "File" || node.name == "Directory")
                {
                    ParseContribution(node, node.name == "Directory" ? ContributionKind::RuntimeDirectory
                                                                       : ContributionKind::RuntimeFile,
                                      owner, contributions, diagnostics);
                }
            }
        }

        [[nodiscard]] auto ConditionMatches(const std::optional<RequirementCondition> &condition,
                                            const PackageActivationRequest &request) -> bool
        {
            if (!condition.has_value()) return true;
            if (condition->option.has_value())
            {
                const auto value = request.options.values.find(*condition->option);
                if (value == request.options.values.end() ||
                    CanonicalOptionValue(value->second) != condition->equals.value_or(""))
                    return false;
            }
            if (condition->targetOperatingSystem.has_value() &&
                request.selection.target.operatingSystem != *condition->targetOperatingSystem)
                return false;
            if (condition->targetArchitecture.has_value() &&
                request.selection.target.architecture != *condition->targetArchitecture)
                return false;
            if (condition->compiler.has_value() && request.selection.toolchain.compiler != *condition->compiler)
                return false;
            return true;
        }

        [[nodiscard]] auto RequirementConditionOf(const SemanticRequirement &requirement)
            -> std::optional<RequirementCondition>
        {
            return std::visit(
                [](const auto &value) -> std::optional<RequirementCondition> {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, SemanticOptionPredicate>) return std::nullopt;
                    else return value.condition;
                },
                requirement);
        }

        [[nodiscard]] auto CanonicalCompatibility(const BinaryCompatibility &compatibility) -> CanonicalValue
        {
            CanonicalValue::Object options{};
            for (const auto &[name, value] : compatibility.artifactOptions) options.emplace(name, value);
            return CanonicalValue::Object{{"architecture", compatibility.architecture},
                                          {"compiler", compatibility.compiler},
                                          {"compilerVersion", compatibility.compilerVersion},
                                          {"configuration", compatibility.configuration},
                                          {"linkage", compatibility.linkage},
                                          {"operatingSystem", compatibility.operatingSystem},
                                          {"options", options},
                                          {"runtimeLibrary", compatibility.runtimeLibrary}};
        }
    }

    auto SemanticPackageResult::Succeeded() const -> bool { return value.has_value() && diagnostics.empty(); }
    auto ResolvedPackageOptions::Succeeded() const -> bool { return diagnostics.empty(); }
    auto PackageProviderResolution::Succeeded() const -> bool { return value.has_value() && diagnostics.empty(); }
    auto ActivePackageExports::Succeeded() const -> bool { return diagnostics.empty(); }
    auto CapabilityResolution::Succeeded() const -> bool { return diagnostics.empty(); }

    auto CapabilityDomainName(const CapabilityDomain domain) -> std::string_view
    {
        switch (domain)
        {
        case CapabilityDomain::Acquisition: return "Acquisition";
        case CapabilityDomain::Build: return "Build";
        case CapabilityDomain::Link: return "Link";
        case CapabilityDomain::Generation: return "Generation";
        case CapabilityDomain::Artifact: return "Artifact";
        case CapabilityDomain::Deployment: return "Deployment";
        }
        return "Link";
    }

    auto ParseSemanticPackage(const AuthoredPackageManifest &package) -> SemanticPackageResult
    {
        SemanticPackageResult result{};
        SemanticPackage semantic{.manifest = package.manifest,
                                 .coordinate = PackageCoordinate{.name = package.name,
                                                                 .exactVersion = package.version},
                                 .source = package.root.source};
        if (const auto *options = Child(package.root, "package.options"))
            semantic.options = ParseOptionDefinitions(*options, result.diagnostics, true);
        if (const auto *requiresElement = Child(package.root, "package.requires"))
            ParseRequirements(*requiresElement, RequirementVisibility::Private, semantic.options, package.name,
                              semantic.requirements, result.diagnostics);
        if (const auto *contributions = Child(package.root, "package.contributions"))
            ParseContributionContainer(*contributions, package.name, semantic.contributions, result.diagnostics);
        if (const auto *import = Child(package.root, "package.import"))
            ImportCps(*import, package, semantic, result.diagnostics);

        {
            bool anyDefault = std::ranges::any_of(semantic.exports, [](const auto &entry) {
                return entry.second.defaultExport;
            });
            std::size_t exportCount = semantic.exports.size();
            for (const auto &node : package.root.children)
        {
                if (!node.specId.starts_with("package.export.")) continue;
                ++exportCount;
                PackageExport exportModel{.kind = ParseExportKind(node),
                                          .name = AttributeValue(node, "Name"),
                                          .defaultExport = BoolAttributeValue(node, "Default"),
                                          .source = node.source};
                anyDefault = anyDefault || exportModel.defaultExport;
                if (exportModel.kind == ExportUseKind::Action)
                {
                    if (exportModel.defaultExport)
                        AddError(result.diagnostics, "NGIN5001",
                                 "Action exports cannot be defaults and require explicit project selection",
                                 node.source);
                    SemanticActionContract action{
                        .kind = ParseActionKind(node),
                        .toolExport = AttributeValue(node, "Tool"),
                        .deterministic = BoolAttributeValue(node, "Deterministic"),
                        .source = node.source,
                    };
                    std::map<std::string, ManifestSourceRange, std::less<>> outputIdentities{};
                    for (const auto &child : node.children)
                    {
                        if (child.name == "Inputs")
                        {
                            for (const auto &input : child.children)
                            {
                                if (!HasAttribute(input, "Include") || HasAttribute(input, "Remove") ||
                                    HasAttribute(input, "Update"))
                                {
                                    AddError(result.diagnostics, "NGIN5001",
                                             "Action input declarations require Include and cannot Remove or Update",
                                             input.source);
                                    continue;
                                }
                                action.inputs.push_back(ActionInputDeclaration{
                                    .kind = ParseActionInputKind(input.name),
                                    .include = AttributeValue(input, "Include"),
                                    .exclude = HasAttribute(input, "Exclude")
                                                   ? std::optional<std::string>{AttributeValue(input, "Exclude")}
                                                   : std::nullopt,
                                    .source = input.source,
                                });
                            }
                        }
                        else if (child.name == "Outputs")
                        {
                            for (const auto &output : child.children)
                            {
                                const auto path = NormalizePortablePath(AttributeValue(output, "Path"),
                                                                        PortablePathBase::ActionOutput, output.source);
                                if (!path.Succeeded())
                                {
                                    result.diagnostics.insert(result.diagnostics.end(), path.diagnostics.begin(),
                                                              path.diagnostics.end());
                                    continue;
                                }
                                if (const auto [existing, inserted] = outputIdentities.emplace(path.value->value,
                                                                                              output.source);
                                    !inserted)
                                    AddError(result.diagnostics, "NGIN5002",
                                             "duplicate Action output path '" + path.value->value + "'", output.source,
                                             {existing->second});
                                action.outputs.push_back(ActionOutputDeclaration{
                                    .kind = ParseActionOutputKind(output.name),
                                    .path = *path.value,
                                    .source = output.source,
                                });
                            }
                        }
                        else if (child.name == "Argument") action.arguments.push_back(child.text);
                        else if (child.name == "WorkingDirectory")
                        {
                            const auto authoredPath = AttributeValue(child, "Path");
                            if (authoredPath == ".")
                            {
                                action.workingDirectory = PortablePath{.value = ".",
                                                                       .base = PortablePathBase::ActionOutput};
                                continue;
                            }
                            const auto path = NormalizePortablePath(authoredPath,
                                                                    PortablePathBase::ActionOutput, child.source);
                            if (path.Succeeded()) action.workingDirectory = *path.value;
                            else result.diagnostics.insert(result.diagnostics.end(), path.diagnostics.begin(),
                                                           path.diagnostics.end());
                        }
                        else if (child.name == "Environment")
                        {
                            const auto name = AttributeValue(child, "Name");
                            const auto value = AttributeValue(child, "Value");
                            if (const auto [existing, inserted] = action.environment.emplace(name, value);
                                !inserted && existing->second != value)
                                AddError(result.diagnostics, "NGIN5003",
                                         "conflicting Action environment value '" + name + "'", child.source);
                        }
                    }
                    exportModel.action = std::move(action);
                }
                const auto defaultVisibility = exportModel.kind == ExportUseKind::Library
                                                   ? RequirementVisibility::Public
                                                   : RequirementVisibility::Private;
                for (const auto &child : node.children)
                {
                    if (child.name == "Uses")
                        ParseRequirements(child, defaultVisibility, semantic.options,
                                          package.name + "::" + exportModel.name, exportModel.requirements,
                                          result.diagnostics);
                    else if (child.name == "Provides")
                    {
                            const auto constraint = ParseAuthoredVersionConstraint(
                                child, AttributeValue(child, "Name"), result.diagnostics);
                            if (constraint.has_value() && constraint->lower.has_value())
                                exportModel.capabilities.push_back(CapabilityImplementation{
                                    .name = AttributeValue(child, "Name"),
                                    .domain = ParseDomain(AttributeValue(child, "Domain",
                                                                         exportModel.kind == ExportUseKind::Library
                                                                             ? "Link"
                                                                             : "Artifact")),
                                    .version = constraint->lower->version,
                                    .exportName = exportModel.name,
                                    .source = child.source,
                                });
                    }
                    else if (child.name == "RuntimeFiles" || child.name == "Notices")
                        ParseContributionContainer(child, exportModel.name, exportModel.contributions,
                                                   result.diagnostics);
                }
                if (exportModel.kind == ExportUseKind::Asset)
                {
                    exportModel.description = AttributeValue(node, "Description");
                    for (const auto &child : node.children)
                        ParseContribution(child, child.name == "Directory" ? ContributionKind::AssetDirectory
                                                                           : ContributionKind::AssetFile,
                                          exportModel.name, exportModel.contributions, result.diagnostics);
                }
                if (const auto [existing, inserted] = semantic.exports.emplace(exportModel.name, exportModel); !inserted)
                    AddError(result.diagnostics, "NGIN4004",
                             "duplicate package export name '" + exportModel.name + "'", node.source,
                             {existing->second.source});
            }
            if (exportCount == 1 && !anyDefault &&
                semantic.exports.begin()->second.kind != ExportUseKind::Action)
                semantic.exports.begin()->second.defaultExport = true;
            if (exportCount == 0)
                AddError(result.diagnostics, "NGIN4004", "Package requires at least one typed export or CPS Import",
                         package.root.source);
        }
        if (const auto *capabilities = Child(package.root, "package.capabilities"))
            for (const auto &provide : capabilities->children)
            {
                auto component = AttributeValue(provide, "Component");
                if (const auto separator = component.find(':'); separator != std::string::npos)
                {
                    if (component.substr(0, separator) != package.name)
                    {
                        AddError(result.diagnostics, "NGIN4009", "Capability overlay Component '" + component +
                                                                      "' belongs to another package",
                                 provide.source);
                        continue;
                    }
                    component.erase(0, separator + 1);
                }
                const auto found = semantic.exports.find(component);
                if (found == semantic.exports.end())
                {
                    AddError(result.diagnostics, "NGIN4009", "Capability overlay references unknown Component '" +
                                                                  component + "'",
                             provide.source);
                    continue;
                }
                const auto constraint = ParseAuthoredVersionConstraint(
                    provide, AttributeValue(provide, "Name"), result.diagnostics);
                if (!constraint.has_value() || !constraint->lower.has_value()) continue;
                found->second.capabilities.push_back(CapabilityImplementation{
                    .name = AttributeValue(provide, "Name"),
                    .domain = ParseDomain(AttributeValue(
                        provide, "Domain", found->second.kind == ExportUseKind::Library ? "Link" : "Artifact")),
                    .version = constraint->lower->version,
                    .exportName = component,
                    .source = provide.source,
                });
            }
        for (const auto &[name, exportModel] : semantic.exports)
        {
            if (!exportModel.action.has_value()) continue;
            const auto tool = semantic.exports.find(exportModel.action->toolExport);
            if (tool == semantic.exports.end() || tool->second.kind != ExportUseKind::Tool)
                AddError(result.diagnostics, "NGIN5004",
                         "Action '" + name + "' references unknown Tool export '" + exportModel.action->toolExport + "'",
                         exportModel.action->source);
        }
        if (const auto *compatibility = Child(package.root, "package.compatibility"))
        {
            semantic.coexistence = AttributeValue(*compatibility, "Coexistence", "Context") == "SideBySide"
                                       ? PackageCoexistence::SideBySide
                                       : PackageCoexistence::Context;
            for (const auto &entry : compatibility->children)
            {
                if (entry.name == "Target")
                    semantic.compatibleTargets.emplace_back(AttributeValue(entry, "OS"),
                                                            AttributeValue(entry, "Architecture"));
                else if (entry.name == "Toolchain")
                    semantic.compatibleCompilers.push_back(AttributeValue(entry, "Compiler"));
            }
        }
        if (result.diagnostics.empty()) result.value = std::move(semantic);
        return result;
    }

    auto ResolvePackageOptions(const SemanticPackage &package,
                               const std::vector<PackageOptionAssignment> &assignments) -> ResolvedPackageOptions
    {
        ResolvedPackageOptions result{};
        std::map<std::string, std::vector<SourcedAssignment<TypedOptionValue>>, std::less<>> grouped{};
        for (const auto &[name, definition] : package.options)
            grouped[name].push_back(SourcedAssignment<TypedOptionValue>{.value = definition.defaultValue,
                                                                        .authority = AssignmentAuthority::Convention,
                                                                        .source = definition.source,
                                                                        .description = "package default"});
        for (const auto &assignment : assignments)
        {
            const auto definition = package.options.find(assignment.name);
            if (definition == package.options.end())
            {
                AddError(result.diagnostics, "NGIN4005", "assignment to undeclared package Option '" + assignment.name +
                                                              "'",
                         assignment.source);
                continue;
            }
            const auto parsed = ParseOptionValue(definition->second, assignment.value, assignment.source);
            if (!parsed.Succeeded())
            {
                result.diagnostics.insert(result.diagnostics.end(), parsed.diagnostics.begin(), parsed.diagnostics.end());
                continue;
            }
            grouped[assignment.name].push_back(SourcedAssignment<TypedOptionValue>{.value = *parsed.value,
                                                                                    .authority = assignment.authority,
                                                                                    .source = assignment.source,
                                                                                    .description = "assignment"});
        }
        for (const auto &[name, values] : grouped)
        {
            const auto merged = MergeScalarSetting(name, values);
            result.diagnostics.insert(result.diagnostics.end(), merged.diagnostics.begin(), merged.diagnostics.end());
            if (!merged.value.has_value()) continue;
            result.values.emplace(name, merged.value->value);
            if (package.options.at(name).artifact)
                result.artifactValues.emplace(name, CanonicalOptionValue(merged.value->value));
        }
        return result;
    }

    DirectoryPackageProvider::DirectoryPackageProvider(std::string identity,
                                                       std::vector<DirectoryPackageRelease> releases)
        : identity_(std::move(identity)), releases_(std::move(releases))
    {
    }

    auto DirectoryPackageProvider::Kind() const -> std::string_view { return "Directory"; }

    auto DirectoryPackageProvider::Resolve(const PackageProviderRequest &request) const -> PackageProviderResolution
    {
        PackageProviderResolution result{};
        if (request.sourceBinding.has_value() && *request.sourceBinding != identity_)
        {
            AddError(result.diagnostics, "NGIN4006", "PackageProvider source binding does not select '" + identity_ +
                                                          "'",
                     request.source);
            return result;
        }
        struct Candidate
        {
            const DirectoryPackageRelease *release{};
            AuthoredPackageManifest package{};
            SemanticVersion version{};
        };
        std::vector<Candidate> candidates{};
        for (const auto &release : releases_)
        {
            if (release.name != request.name) continue;
            if (release.root.empty() || release.manifest.empty() || !fs::is_directory(release.root) ||
                !fs::is_regular_file(release.manifest))
            {
                AddError(result.diagnostics, "NGIN4015", "Directory PackageProvider release root or manifest is missing",
                         request.source);
                continue;
            }
            if (!release.nativeIdentity.empty() && fs::path(release.nativeIdentity).is_absolute())
            {
                AddError(result.diagnostics, "NGIN4016", "PackageProvider native identity must be logical, not an absolute path",
                         request.source);
                continue;
            }
            if (release.hermetic && release.integrity.empty())
            {
                AddError(result.diagnostics, "NGIN4017", "Hermetic PackageProvider release requires an integrity identity",
                         request.source);
                continue;
            }
            const auto authored = ParseAuthoredManifest(release.manifest);
            if (!authored.Succeeded())
            {
                result.diagnostics.insert(result.diagnostics.end(), authored.diagnostics.begin(), authored.diagnostics.end());
                continue;
            }
            const auto *package = std::get_if<AuthoredPackageManifest>(&*authored.value);
            if (package == nullptr || package->name != request.name) continue;
            const auto version = ParseSemanticVersion(package->version);
            if (!version.has_value() ||
                (request.constraint.has_value() && !VersionConstraintContains(*request.constraint, *version)))
                continue;
            candidates.push_back(Candidate{.release = &release, .package = *package, .version = *version});
        }
        if (candidates.empty())
        {
            AddError(result.diagnostics, "NGIN4006", "Directory PackageProvider found no compatible release of '" +
                                                          request.name + "'",
                     request.source);
            return result;
        }
        std::ranges::sort(candidates, [](const Candidate &left, const Candidate &right) {
            if (left.version != right.version) return right.version < left.version;
            return left.release->nativeIdentity < right.release->nativeIdentity;
        });
        if (candidates.size() > 1 && candidates[0].version == candidates[1].version)
        {
            AddError(result.diagnostics, "NGIN4006", "Directory PackageProvider has ambiguous exact release '" +
                                                          request.name + "@" + VersionText(candidates[0].version) + "'",
                     request.source);
            return result;
        }
        const auto &selected = candidates.front();
        PackageProviderResult provider{
            .coordinate = PackageCoordinate{.name = request.name,
                                            .exactVersion = selected.package.version,
                                            .sourceBinding = identity_},
            .providerKind = "Directory",
            .nativeIdentity = selected.release->nativeIdentity.empty()
                                  ? identity_ + "/" + request.name + "@" + selected.package.version
                                  : selected.release->nativeIdentity,
            .nativeVersion = selected.release->nativeVersion.empty() ? selected.package.version
                                                                      : selected.release->nativeVersion,
            .revision = selected.release->revision,
            .integrity = selected.release->integrity,
            .artifactIdentity = selected.release->artifactIdentity,
            .root = selected.release->root,
            .manifest = selected.release->manifest,
            .context = request.context,
            .hermetic = selected.release->hermetic,
            .provenance = "directory:" + identity_,
            .trust = "workspace",
        };
        provider.artifactIdentity = ArtifactIdentity(provider);
        result.value = std::move(provider);
        return result;
    }

    CatalogPackageProvider::CatalogPackageProvider(std::string kind, std::string identity,
                                                   std::vector<PackageProviderResult> releases)
        : kind_(std::move(kind)), identity_(std::move(identity)), releases_(std::move(releases))
    {
    }

    auto CatalogPackageProvider::Kind() const -> std::string_view { return kind_; }

    auto CatalogPackageProvider::Resolve(const PackageProviderRequest &request) const -> PackageProviderResolution
    {
        PackageProviderResolution result{};
        if (request.sourceBinding.has_value() && *request.sourceBinding != identity_)
        {
            AddError(result.diagnostics, "NGIN4006", "PackageProvider source binding does not select '" + identity_ + "'",
                     request.source);
            return result;
        }
        std::vector<const PackageProviderResult *> candidates{};
        for (const auto &release : releases_)
        {
            if (release.coordinate.name != request.name) continue;
            const auto version = ParseSemanticVersion(release.coordinate.exactVersion);
            if (!version.has_value() ||
                (request.constraint.has_value() && !VersionConstraintContains(*request.constraint, *version)))
                continue;
            if (release.nativeIdentity.empty() || release.nativeVersion.empty() || release.providerKind != kind_)
            {
                AddError(result.diagnostics, "NGIN4018", "Catalog PackageProvider result is missing its native identity/version",
                         request.source);
                continue;
            }
            if (release.hermetic && release.integrity.empty())
            {
                AddError(result.diagnostics, "NGIN4017", "Hermetic PackageProvider release requires an integrity identity",
                         request.source);
                continue;
            }
            candidates.push_back(&release);
        }
        std::ranges::sort(candidates, [](const auto *left, const auto *right) {
            const auto leftVersion = *ParseSemanticVersion(left->coordinate.exactVersion);
            const auto rightVersion = *ParseSemanticVersion(right->coordinate.exactVersion);
            if (leftVersion != rightVersion) return rightVersion < leftVersion;
            return left->nativeIdentity < right->nativeIdentity;
        });
        if (candidates.empty())
        {
            AddError(result.diagnostics, "NGIN4006", "Catalog PackageProvider found no compatible release of '" +
                                                          request.name + "'", request.source);
            return result;
        }
        if (candidates.size() > 1 && candidates[0]->coordinate.exactVersion == candidates[1]->coordinate.exactVersion)
        {
            AddError(result.diagnostics, "NGIN4006", "Catalog PackageProvider has ambiguous exact release '" +
                                                          request.name + "@" + candidates[0]->coordinate.exactVersion + "'",
                     request.source);
            return result;
        }
        auto selected = *candidates.front();
        selected.coordinate.sourceBinding = identity_;
        selected.context = request.context;
        selected.artifactIdentity = ArtifactIdentity(selected);
        if (!selected.hermetic && selected.provenance.empty()) selected.provenance = "non-hermetic:" + identity_;
        result.value = std::move(selected);
        return result;
    }

    auto ConstructPackageInstance(const PackageProviderResult &provider, const BinaryCompatibility &compatibility,
                                  std::map<std::string, std::string, std::less<>> artifactOptions)
        -> PackageInstance
    {
        CanonicalValue::Object options{};
        for (const auto &[name, value] : artifactOptions) options.emplace(name, value);
        const auto identity = CanonicalFingerprint(
            "PackageInstance",
            {{"compatibility", CanonicalCompatibility(compatibility)},
             {"context", provider.context == PackageInstanceContext::Host ? "Host" : "Target"},
             {"integrity", provider.integrity},
             {"artifactIdentity", provider.artifactIdentity},
             {"name", provider.coordinate.name},
             {"nativeIdentity", provider.nativeIdentity},
             {"nativeVersion", provider.nativeVersion},
             {"options", options},
             {"provider", provider.providerKind},
             {"revision", provider.revision},
             {"version", provider.coordinate.exactVersion}});
        return PackageInstance{.providerResult = provider,
                               .context = provider.context,
                               .compatibility = compatibility,
                               .artifactOptions = std::move(artifactOptions),
                               .identity = identity};
    }

    auto ActivatePackageExports(const SemanticPackage &package, const PackageInstance &instance,
                                const PackageActivationRequest &request) -> ActivePackageExports
    {
        ActivePackageExports result{.instance = instance};
        if (!request.options.Succeeded())
        {
            result.diagnostics = request.options.diagnostics;
            return result;
        }
        if (!package.compatibleTargets.empty() &&
            std::ranges::none_of(package.compatibleTargets, [&](const auto &allowed) {
                return (allowed.first.empty() || allowed.first == request.selection.target.operatingSystem) &&
                       (allowed.second.empty() || allowed.second == request.selection.target.architecture);
            }))
            AddError(result.diagnostics, "NGIN4011",
                     "package '" + package.coordinate.name + "' is incompatible with target " +
                         request.selection.target.operatingSystem + "-" + request.selection.target.architecture,
                     package.source);
        if (!package.compatibleCompilers.empty() &&
            std::ranges::none_of(package.compatibleCompilers, [&](const std::string &compiler) {
                return compiler.empty() || compiler == request.selection.toolchain.compiler;
            }))
            AddError(result.diagnostics, "NGIN4011",
                     "package '" + package.coordinate.name + "' is incompatible with compiler '" +
                         request.selection.toolchain.compiler + "'",
                     package.source);
        if (!result.diagnostics.empty()) return result;
        std::vector<ExportUse> requested = request.exports;
        if (requested.empty())
        {
            for (const auto &[_, exportModel] : package.exports)
                if (exportModel.defaultExport)
                    requested.push_back(ExportUse{.kind = exportModel.kind,
                                                  .name = exportModel.name,
                                                  .source = exportModel.source});
            if (requested.empty())
            {
                AddError(result.diagnostics, "NGIN4007",
                         "package '" + package.coordinate.name + "' has no default export", package.source);
                return result;
            }
        }

        std::set<std::string, std::less<>> active{};
        std::set<std::string, std::less<>> visiting{};
        std::vector<std::string> stack{};
        std::function<void(const ExportUse &)> activate;
        activate = [&](const ExportUse &selection) {
            const auto found = package.exports.find(selection.name);
            if (found == package.exports.end() || found->second.kind != selection.kind)
            {
                AddError(result.diagnostics, "NGIN4007",
                         "unknown or mismatched export '" + package.coordinate.name + "::" + selection.name + "'",
                         selection.source);
                return;
            }
            if (active.contains(selection.name)) return;
            if (!visiting.insert(selection.name).second)
            {
                auto cycle = stack;
                cycle.push_back(selection.name);
                std::ostringstream message;
                message << "export requirement cycle";
                for (const auto &name : cycle) message << " -> " << name;
                AddError(result.diagnostics, "NGIN4008", message.str(), selection.source);
                return;
            }
            stack.push_back(selection.name);
            const auto &exportModel = found->second;
            for (const auto &requirement : exportModel.requirements)
            {
                if (!ConditionMatches(RequirementConditionOf(requirement), request)) continue;
                if (const auto *local = std::get_if<SemanticExportRequirement>(&requirement))
                    activate(ExportUse{.kind = local->kind, .name = local->name, .source = local->source});
                else if (const auto *predicate = std::get_if<SemanticOptionPredicate>(&requirement))
                {
                    const auto value = request.options.values.find(predicate->name);
                    if (value == request.options.values.end() ||
                        CanonicalOptionValue(value->second) != predicate->value)
                        AddError(result.diagnostics, "NGIN4001",
                                 "required package Option predicate '" + predicate->name + "=" + predicate->value +
                                     "' is not satisfied",
                                 predicate->source);
                }
                else
                    result.requirements.push_back(requirement);
            }
            for (auto capability : exportModel.capabilities)
            {
                capability.context = instance.context;
                capability.packageInstance = instance.identity;
                capability.exportName = exportModel.name;
                result.capabilities.push_back(std::move(capability));
            }
            result.contributions.insert(result.contributions.end(), exportModel.contributions.begin(),
                                        exportModel.contributions.end());
            stack.pop_back();
            visiting.erase(selection.name);
            active.insert(selection.name);
        };
        for (const auto &selection : requested) activate(selection);
        if (!active.empty())
        {
            for (const auto &requirement : package.requirements)
                if (ConditionMatches(RequirementConditionOf(requirement), request))
                    result.requirements.push_back(requirement);
            result.contributions.insert(result.contributions.end(), package.contributions.begin(),
                                        package.contributions.end());
        }
        result.exports.assign(active.begin(), active.end());
        std::ranges::sort(result.capabilities, {}, [](const CapabilityImplementation &capability) {
            return capability.name + ":" + std::string(CapabilityDomainName(capability.domain)) + ":" +
                   capability.exportName;
        });
        return result;
    }

    auto ResolveCapabilityBindings(const std::vector<SemanticCapabilityRequirement> &requirements,
                                   const std::vector<CapabilityImplementation> &implementations)
        -> CapabilityResolution
    {
        CapabilityResolution result{};
        std::map<std::string, std::vector<SemanticCapabilityRequirement>, std::less<>> grouped{};
        for (const auto &requirement : requirements)
            grouped[requirement.requester + "\n" + requirement.name + "\n" +
                    std::string(CapabilityDomainName(requirement.domain))]
                .push_back(requirement);
        for (const auto &[_, group] : grouped)
        {
            std::vector<SourcedVersionConstraint> constraints{};
            for (const auto &requirement : group)
                if (requirement.constraint.has_value()) constraints.push_back(*requirement.constraint);
            const auto intersection = IntersectVersionConstraints(group.front().name, constraints);
            if (!intersection.Succeeded())
            {
                result.diagnostics.insert(result.diagnostics.end(), intersection.diagnostics.begin(),
                                          intersection.diagnostics.end());
                continue;
            }
            std::vector<const CapabilityImplementation *> candidates{};
            for (const auto &implementation : implementations)
                if (implementation.name == group.front().name && implementation.domain == group.front().domain &&
                    implementation.context == group.front().context &&
                    (!group.front().provider.has_value() ||
                     implementation.packageName == *group.front().provider) &&
                    VersionConstraintContains(*intersection.value, implementation.version))
                    candidates.push_back(&implementation);
            std::ranges::sort(candidates, [](const auto *left, const auto *right) {
                if (left->packageInstance != right->packageInstance) return left->packageInstance < right->packageInstance;
                if (left->exportName != right->exportName) return left->exportName < right->exportName;
                return left->version < right->version;
            });
            candidates.erase(std::unique(candidates.begin(), candidates.end(), [](const auto *left, const auto *right) {
                                 return left->packageInstance == right->packageInstance &&
                                        left->exportName == right->exportName && left->version == right->version;
                             }),
                             candidates.end());
            if (candidates.empty())
            {
                AddError(result.diagnostics, "NGIN4009",
                         "no active implementation satisfies capability '" + group.front().name + "'",
                         group.front().source);
                continue;
            }
            if (candidates.size() != 1)
            {
                std::vector<ManifestSourceRange> related{};
                for (const auto *candidate : candidates) related.push_back(candidate->source);
                AddError(result.diagnostics, "NGIN4009",
                         "ambiguous implementations satisfy capability '" + group.front().name + "'",
                         group.front().source, std::move(related));
                continue;
            }
            const auto &candidate = *candidates.front();
            result.bindings.push_back(CapabilityBinding{
                .requirement = group.front().requester,
                .capability = group.front().name,
                .domain = std::string(CapabilityDomainName(group.front().domain)),
                .version = VersionText(candidate.version),
                .packageInstance = candidate.packageInstance,
                .exportName = candidate.exportName,
            });
            result.activatedExports.push_back(candidate.packageInstance + "::" + candidate.exportName);
        }
        return result;
    }

    auto ValidatePackageInstanceCoexistence(const std::vector<PackageInstanceUse> &uses,
                                             const PackageCoexistence policy,
                                             const bool platformAllowsSideBySide)
        -> std::vector<ManifestDiagnostic>
    {
        std::vector<ManifestDiagnostic> diagnostics{};
        std::map<std::string, std::vector<const PackageInstanceUse *>, std::less<>> grouped{};
        for (const auto &use : uses)
        {
            const auto context = use.instance.context == PackageInstanceContext::Host ? "Host" : "Target";
            grouped[use.linkageClosure + "\n" + context + "\n" + use.instance.providerResult.coordinate.name]
                .push_back(&use);
        }
        for (const auto &[_, group] : grouped)
        {
            std::set<std::string, std::less<>> identities{};
            for (const auto *use : group) identities.insert(use->instance.identity);
            if (identities.size() <= 1) continue;
            if (policy == PackageCoexistence::SideBySide && platformAllowsSideBySide) continue;
            std::vector<ManifestSourceRange> related{};
            for (std::size_t index = 1; index < group.size(); ++index) related.push_back(group[index]->source);
            AddError(diagnostics, "NGIN4010",
                     "incompatible instances of package '" + group.front()->instance.providerResult.coordinate.name +
                         "' enter linkage closure '" + group.front()->linkageClosure + "'",
                     group.front()->source, std::move(related));
        }
        return diagnostics;
    }
}
