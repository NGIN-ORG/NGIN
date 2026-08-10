#include "PackageModel.hpp"

#include "Canonical.hpp"

#include <algorithm>
#include <functional>
#include <sstream>

namespace NGIN::CLI
{
    namespace
    {
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

        [[nodiscard]] auto ParseVisibility(const AuthoredElement &element,
                                           const RequirementVisibility fallback) -> RequirementVisibility
        {
            return AttributeValue(element, "Visibility") == "Public" ? RequirementVisibility::Public : fallback;
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

        [[nodiscard]] auto ParseActionKind(const std::string_view value) -> ActionKind
        {
            if (value == "Generate") return ActionKind::Generate;
            if (value == "Analyze") return ActionKind::Analyze;
            if (value == "Format") return ActionKind::Format;
            if (value == "Validate") return ActionKind::Validate;
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
            if (element.name == "Action") return ExportUseKind::Action;
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
            for (const auto name : {"Library", "Tool", "Plugin", "Action", "Asset"})
                if (const auto value = AttributeValue(element, name); !value.empty()) return value;
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
            const auto targetOs = AttributeValue(node, "TargetOS");
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
                        if (child.name == "Use")
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
                    SemanticProjectRequirement requirement{.name = AttributeValue(node, "Name"),
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
                else if (node.name == "Export")
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
            const auto include = NormalizePortablePath(AttributeValue(node, "Include"), PortablePathBase::Manifest,
                                                       node.source);
            const auto destination = NormalizeStageDestination(AttributeValue(node, "Into"), node.source);
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
            semantic.options = ParseOptionDefinitions(*options, result.diagnostics);
        if (const auto *requiresElement = Child(package.root, "package.requires"))
            ParseRequirements(*requiresElement, RequirementVisibility::Private, semantic.options, package.name,
                              semantic.requirements, result.diagnostics);
        if (const auto *contributions = Child(package.root, "package.contributions"))
            ParseContributionContainer(*contributions, package.name, semantic.contributions, result.diagnostics);

        if (const auto *exports = Child(package.root, "package.exports"))
        {
            for (const auto &node : exports->children)
            {
                PackageExport exportModel{.kind = ParseExportKind(node),
                                          .name = AttributeValue(node, "Name"),
                                          .defaultExport = BoolAttributeValue(node, "Default"),
                                          .source = node.source};
                if (exportModel.kind == ExportUseKind::Action)
                {
                    if (exportModel.defaultExport)
                        AddError(result.diagnostics, "NGIN5001",
                                 "Action exports cannot be defaults and require explicit project selection",
                                 node.source);
                    SemanticActionContract action{
                        .kind = ParseActionKind(AttributeValue(node, "Kind")),
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
                    if (child.name == "Requires")
                        ParseRequirements(child, defaultVisibility, semantic.options,
                                          package.name + "::" + exportModel.name, exportModel.requirements,
                                          result.diagnostics);
                    else if (child.name == "Provides")
                    {
                        for (const auto &capability : child.children)
                        {
                            const auto version = ParseSemanticVersion(AttributeValue(capability, "Version"));
                            if (version.has_value())
                                exportModel.capabilities.push_back(CapabilityImplementation{
                                    .name = AttributeValue(capability, "Name"),
                                    .domain = ParseDomain(AttributeValue(capability, "Domain")),
                                    .version = *version,
                                    .exportName = exportModel.name,
                                    .source = capability.source,
                                });
                        }
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
        result.value = PackageProviderResult{
            .coordinate = PackageCoordinate{.name = request.name,
                                            .exactVersion = selected.package.version,
                                            .sourceBinding = identity_},
            .providerKind = "Directory",
            .nativeIdentity = selected.release->nativeIdentity.empty()
                                  ? selected.release->manifest.generic_string()
                                  : selected.release->nativeIdentity,
            .revision = selected.release->revision,
            .integrity = selected.release->integrity,
            .root = selected.release->root,
            .manifest = selected.release->manifest,
            .context = request.context,
            .hermetic = selected.release->hermetic,
            .provenance = "directory:" + identity_,
            .trust = "workspace",
        };
        return result;
    }

    auto ConstructPackageInstance(const PackageProviderResult &provider, const BinaryCompatibility &compatibility,
                                  std::map<std::string, std::string, std::less<>> artifactOptions)
        -> PackageInstance
    {
        CanonicalValue::Object options{};
        for (const auto &[name, value] : artifactOptions) options.emplace(name, value);
        const auto identity = CanonicalDigestInput(
            "PackageInstance",
            {{"compatibility", CanonicalCompatibility(compatibility)},
             {"context", provider.context == PackageInstanceContext::Host ? "Host" : "Target"},
             {"integrity", provider.integrity},
             {"name", provider.coordinate.name},
             {"nativeIdentity", provider.nativeIdentity},
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
