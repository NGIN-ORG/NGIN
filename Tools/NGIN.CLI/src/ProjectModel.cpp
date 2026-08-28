#include "ProjectModel.hpp"
#include "SemanticAuthoring.hpp"

#include <algorithm>
#include <charconv>
#include <map>
#include <set>
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

        [[nodiscard]] auto Children(const AuthoredElement &element, const std::string_view specId)
            -> std::vector<const AuthoredElement *>
        {
            std::vector<const AuthoredElement *> result{};
            for (const auto &child : element.children)
                if (child.specId == specId) result.push_back(&child);
            return result;
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

        [[nodiscard]] auto ParseArtifactKind(const AuthoredProjectManifest &project) -> ProductArtifactKind
        {
            return project.artifactKind == "Library" ? ProductArtifactKind::Library
                                                      : ProductArtifactKind::Executable;
        }

        [[nodiscard]] auto ParseLibraryKind(const std::string_view value) -> LibraryKind
        {
            if (value == "Shared") return LibraryKind::Shared;
            if (value == "Interface") return LibraryKind::Interface;
            if (value == "Plugin") return LibraryKind::Plugin;
            return LibraryKind::Static;
        }

        [[nodiscard]] auto ParseVisibility(const std::string_view value) -> BuildVisibility
        {
            if (value == "Public") return BuildVisibility::Public;
            if (value == "Interface") return BuildVisibility::Interface;
            return BuildVisibility::Private;
        }

        [[nodiscard]] auto ToolingActionKind(const std::string_view name) -> ActionKind
        {
            if (name == "Analyze") return ActionKind::Analyze;
            if (name == "Format") return ActionKind::Format;
            if (name == "Validate") return ActionKind::Validate;
            return ActionKind::Custom;
        }

        [[nodiscard]] auto NormalizeQualifiedAction(const std::string_view authored) -> std::string
        {
            const auto separator = authored.rfind('/');
            if (separator == std::string_view::npos) return std::string(authored);
            return std::string(authored.substr(0, separator)) + "::" + std::string(authored.substr(separator + 1));
        }

        auto ValidateQualifiedAction(const ProjectActionSelection &selection,
                                     std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            const auto separator = selection.qualifiedAction.rfind("::");
            if (separator == std::string::npos || separator == 0 || separator + 2 == selection.qualifiedAction.size())
                AddError(diagnostics, "NGIN5005",
                         "Action must be qualified as Package/Export, got '" + selection.qualifiedAction + "'",
                         selection.source);
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

        auto ParseOptions(const AuthoredElement &options, SemanticProject &project,
                          std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            project.options = ParseOptionDefinitions(options, diagnostics);
        }

        [[nodiscard]] auto UseKind(const AuthoredElement &use) -> std::optional<ExportUseKind>
        {
            if (use.name == "Library") return ExportUseKind::Library;
            if (use.name == "Tool") return ExportUseKind::Tool;
            if (use.name == "Plugin") return ExportUseKind::Plugin;
            if (use.name == "Generator" || use.name == "Analyzer" || use.name == "Formatter" ||
                use.name == "Validator" || use.name == "Action")
                return ExportUseKind::Action;
            if (use.name == "Asset") return ExportUseKind::Asset;
            return std::nullopt;
        }

        auto ParseDependencies(const AuthoredElement &dependencies, const DependencyContext context,
                               const std::optional<std::string> &owner, SemanticProject &project,
                               std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            for (const auto &node : dependencies.children)
            {
                if (node.name == "Package")
                {
                    PackageDependencyRequest request{
                        .name = AttributeValue(node, "Name"),
                        .constraint = ParseAuthoredVersionConstraint(node, AttributeValue(node, "Name"), diagnostics),
                        .context = context,
                        .owner = owner,
                        .source = node.source,
                    };
                    for (const auto &child : node.children)
                    {
                        if (const auto kind = UseKind(child); kind.has_value())
                        {
                            request.exports.push_back(ExportUse{.kind = *kind,
                                                                .name = AttributeValue(child, "Name"),
                                                                .source = child.source});
                        }
                        else if (child.name == "Option")
                        {
                            const auto name = AttributeValue(child, "Name");
                            const auto value = AttributeValue(child, "Value");
                            if (const auto [existing, inserted] = request.optionAssignments.emplace(name, value);
                                !inserted && existing->second != value)
                                AddError(diagnostics, "NGIN3002", "conflicting dependency Option '" + name + "'",
                                         child.source);
                        }
                    }
                    project.dependencies.emplace_back(std::move(request));
                }
                else if (node.name == "Project")
                {
                    ProjectDependencyRequest request{.context = context, .owner = owner, .source = node.source};
                    if (const auto path = AttributeValue(node, "Path"); !path.empty())
                    {
                        const auto normalized = NormalizePortablePath(path, PortablePathBase::Manifest, node.source);
                        if (normalized.Succeeded())
                        {
                            request.path = *normalized.value;
                            request.name = std::filesystem::path(normalized.value->value).stem().string();
                        }
                        else
                            diagnostics.insert(diagnostics.end(), normalized.diagnostics.begin(),
                                               normalized.diagnostics.end());
                    }
                    project.dependencies.emplace_back(std::move(request));
                }
                else if (node.name == "Capability")
                {
                    project.dependencies.emplace_back(ProjectCapabilityRequest{
                        .name = AttributeValue(node, "Name"),
                        .domain = AttributeValue(node, "Domain", "Link"),
                        .constraint = ParseAuthoredVersionConstraint(node, AttributeValue(node, "Name"), diagnostics),
                        .provider = HasAttribute(node, "Provider")
                                        ? std::optional<std::string>{AttributeValue(node, "Provider")}
                                        : std::nullopt,
                        .context = context,
                        .source = node.source});
                }
            }
        }

        [[nodiscard]] auto ItemKind(const std::string_view name) -> std::optional<BuildItemKind>
        {
            if (name == "Source") return BuildItemKind::Source;
            if (name == "Header") return BuildItemKind::Header;
            if (name == "CxxModule") return BuildItemKind::CxxModule;
            if (name == "Resource") return BuildItemKind::Resource;
            if (name == "IncludeDirectory") return BuildItemKind::IncludeDirectory;
            if (name == "Define") return BuildItemKind::Define;
            if (name == "CompileOption") return BuildItemKind::CompileOption;
            if (name == "LinkOption") return BuildItemKind::LinkOption;
            if (name == "PrecompiledHeader") return BuildItemKind::PrecompiledHeader;
            return std::nullopt;
        }

        auto ParseBuild(const AuthoredElement &build, SemanticProject &project,
                        std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            project.build.conventions = BoolAttributeValue(build, "Conventions", true);
            for (const auto &node : build.children)
            {
                if (node.name == "Language")
                {
                    project.build.language = LanguageRequirement{.standard = AttributeValue(node, "Standard"),
                                                                 .extensions = BoolAttributeValue(node, "Extensions"),
                                                                 .required = BoolAttributeValue(node, "Required", true),
                                                                 .source = node.source};
                    continue;
                }
                if (node.name == "UnityBuild")
                {
                    project.build.unityBuild =
                        UnityBuildSetting{.enabled = BoolAttributeValue(node, "Enabled"),
                                          .batchSize = ParseInteger(AttributeValue(node, "BatchSize")),
                                          .source = node.source};
                    continue;
                }
                if (node.name == "Convention")
                {
                    project.build.namedConventions[AttributeValue(node, "Name")] = BoolAttributeValue(node, "Enabled");
                    continue;
                }
                const auto kind = ItemKind(node.name);
                if (!kind.has_value()) continue;
                BuildItemDeclaration declaration{.kind = *kind, .source = node.source};
                if (HasAttribute(node, "Include"))
                {
                    declaration.operation = BuildItemOperation::Include;
                    declaration.pattern = AttributeValue(node, "Include");
                }
                else if (HasAttribute(node, "Remove"))
                {
                    declaration.operation = BuildItemOperation::Remove;
                    declaration.pattern = AttributeValue(node, "Remove");
                }
                else if (HasAttribute(node, "Update"))
                {
                    declaration.operation = BuildItemOperation::Update;
                    declaration.pattern = AttributeValue(node, "Update");
                }
                else if (*kind == BuildItemKind::IncludeDirectory || *kind == BuildItemKind::PrecompiledHeader)
                    declaration.pattern = AttributeValue(node, "Path");
                else if (*kind == BuildItemKind::Define)
                    declaration.pattern = AttributeValue(node, "Name");
                else
                    declaration.pattern = AttributeValue(node, "Value");
                if (const auto exclude = AttributeValue(node, "Exclude"); !exclude.empty())
                    declaration.exclude = exclude;
                if (const auto into = AttributeValue(node, "Into"); !into.empty())
                {
                    const auto destination = NormalizeStageDestination(into, node.source);
                    if (destination.Succeeded())
                        declaration.destination = *destination.value;
                    else
                        diagnostics.insert(diagnostics.end(), destination.diagnostics.begin(),
                                           destination.diagnostics.end());
                }
                if (HasAttribute(node, "Visibility"))
                    declaration.visibility = ParseVisibility(AttributeValue(node, "Visibility"));
                if (HasAttribute(node, "Generated")) declaration.generated = BoolAttributeValue(node, "Generated");
                if (HasAttribute(node, "System")) declaration.system = BoolAttributeValue(node, "System");
                declaration.allowEmpty = BoolAttributeValue(node, "AllowEmpty");
                declaration.detail = AttributeValue(node, "Kind");
                if (HasAttribute(node, "Value") && *kind == BuildItemKind::Define)
                    declaration.value = AttributeValue(node, "Value");
                project.build.declarations.push_back(std::move(declaration));
            }
        }

        auto ParseStage(const AuthoredElement &stage, const std::string_view directorySpecId,
                        std::vector<ProjectStageInput> &result, std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            for (const auto &node : stage.children)
            {
                const auto include =
                    NormalizePortablePath(AttributeValue(node, "From"), PortablePathBase::Manifest, node.source);
                const auto destination = NormalizeStageDestination(AttributeValue(node, "To"), node.source);
                diagnostics.insert(diagnostics.end(), include.diagnostics.begin(), include.diagnostics.end());
                diagnostics.insert(diagnostics.end(), destination.diagnostics.begin(), destination.diagnostics.end());
                if (include.Succeeded() && destination.Succeeded())
                    result.push_back(ProjectStageInput{
                        .kind = node.specId == directorySpecId ? StageInputKind::Directory : StageInputKind::File,
                        .include = *include.value,
                        .destination = *destination.value,
                        .source = node.source});
            }
        }

        [[nodiscard]] auto BuildDeclarationIdentity(const BuildItemDeclaration &declaration) -> std::string
        {
            return std::to_string(static_cast<int>(declaration.kind)) + ":" + declaration.pattern;
        }

        [[nodiscard]] auto CanonicalBuildDeclaration(const BuildItemDeclaration &declaration) -> CanonicalValue
        {
            CanonicalValue::Object value{
                {"allowEmpty", declaration.allowEmpty},
                {"detail", declaration.detail},
                {"kind", static_cast<std::int64_t>(declaration.kind)},
                {"operation", static_cast<std::int64_t>(declaration.operation)},
                {"pattern", declaration.pattern},
            };
            if (declaration.destination.has_value()) value["destination"] = declaration.destination->value;
            if (declaration.visibility.has_value())
                value["visibility"] = static_cast<std::int64_t>(*declaration.visibility);
            if (declaration.value.has_value()) value["value"] = *declaration.value;
            if (declaration.generated.has_value()) value["generated"] = *declaration.generated;
            if (declaration.system.has_value()) value["system"] = *declaration.system;
            if (declaration.exclude.has_value()) value["exclude"] = *declaration.exclude;
            return value;
        }

        auto AddRefinementAssignment(ProjectRefinement &refinement, std::string category, std::string identity,
                                     CanonicalValue value, const ManifestSourceRange &source) -> void
        {
            refinement.semantic.assignments.push_back(RefinementAssignment{.category = std::move(category),
                                                                           .identity = std::move(identity),
                                                                           .value = std::move(value),
                                                                           .source = source});
        }

        auto ParseProjectRefinement(const AuthoredElement &authored, const SemanticProject &project,
                                    const DependencyContext rootContext, ProjectRefinement &refinement,
                                    std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            refinement.semantic.source = authored.source;
            if (HasAttribute(authored, "Configuration"))
                refinement.semantic.selector.configuration = AttributeValue(authored, "Configuration");
            if (HasAttribute(authored, "Target"))
                refinement.semantic.selector.targetName = AttributeValue(authored, "Target");
            if (HasAttribute(authored, "OS"))
                refinement.semantic.selector.targetOperatingSystem = AttributeValue(authored, "OS");
            if (HasAttribute(authored, "Architecture"))
                refinement.semantic.selector.targetArchitecture = AttributeValue(authored, "Architecture");
            if (HasAttribute(authored, "Toolchain"))
                refinement.semantic.selector.toolchainName = AttributeValue(authored, "Toolchain");
            if (HasAttribute(authored, "Compiler"))
                refinement.semantic.selector.compiler = AttributeValue(authored, "Compiler");
            if (HasAttribute(authored, "Option"))
            {
                const auto name = AttributeValue(authored, "Option");
                const auto definition = project.options.find(name);
                if (definition == project.options.end())
                {
                    AddError(diagnostics, "NGIN2006", "When selects undeclared Option '" + name + "'",
                             authored.source);
                }
                else
                {
                    const auto value = ParseOptionValue(definition->second, AttributeValue(authored, "Equals"),
                                                        authored.source);
                    diagnostics.insert(diagnostics.end(), value.diagnostics.begin(), value.diagnostics.end());
                    if (value.Succeeded()) refinement.semantic.selector.options.emplace(name, *value.value);
                }
            }
            const auto &selector = refinement.semantic.selector;
            if (!selector.configuration.has_value() && !selector.targetName.has_value() &&
                !selector.targetOperatingSystem.has_value() && !selector.targetArchitecture.has_value() &&
                !selector.toolchainName.has_value() && !selector.compiler.has_value() && selector.options.empty())
                AddError(diagnostics, "NGIN2006", "When must contain at least one selection fact", authored.source);

            if (const auto *build = Child(authored, "project.when.build"))
            {
                SemanticProject parsed{.artifactKind = project.artifactKind, .libraryKind = project.libraryKind};
                ParseBuild(*build, parsed, diagnostics);
                if (HasAttribute(*build, "Conventions"))
                {
                    refinement.build.conventions = SourcedAssignment<bool>{.value = parsed.build.conventions,
                                                                           .authority = AssignmentAuthority::Refinement,
                                                                           .source = build->source,
                                                                           .description = "matched project Refinement"};
                    AddRefinementAssignment(refinement, "BuildScalar", "Conventions", parsed.build.conventions,
                                            build->source);
                }
                if (const auto *language = Child(*build, "project.when.build.language"))
                {
                    refinement.build.language = parsed.build.language;
                    AddRefinementAssignment(refinement, "BuildScalar", "Language",
                                            CanonicalValue::Object{{"extensions", parsed.build.language.extensions},
                                                                   {"required", parsed.build.language.required},
                                                                   {"standard", parsed.build.language.standard}},
                                            language->source);
                }
                if (const auto *unity = Child(*build, "project.when.build.unity-build"))
                {
                    refinement.build.unityBuild = parsed.build.unityBuild;
                    CanonicalValue::Object value{{"enabled", parsed.build.unityBuild->enabled}};
                    if (parsed.build.unityBuild->batchSize.has_value())
                        value["batchSize"] = *parsed.build.unityBuild->batchSize;
                    AddRefinementAssignment(refinement, "BuildScalar", "UnityBuild", std::move(value), unity->source);
                }
                for (const auto &node : build->children)
                    if (node.name == "Convention")
                    {
                        const auto name = AttributeValue(node, "Name");
                        const auto enabled = BoolAttributeValue(node, "Enabled");
                        refinement.build.namedConventions[name] =
                            SourcedAssignment<bool>{.value = enabled,
                                                    .authority = AssignmentAuthority::Refinement,
                                                    .source = node.source,
                                                    .description = "matched project Refinement"};
                        AddRefinementAssignment(refinement, "BuildConvention", name, enabled, node.source);
                    }
                refinement.build.declarations = std::move(parsed.build.declarations);
                for (const auto &declaration : refinement.build.declarations)
                    AddRefinementAssignment(refinement, "BuildItem", BuildDeclarationIdentity(declaration),
                                            CanonicalBuildDeclaration(declaration), declaration.source);
            }
            if (const auto *dependencies = Child(authored, "project.when.uses"))
            {
                SemanticProject parsed{};
                ParseDependencies(*dependencies, rootContext, std::nullopt, parsed, diagnostics);
                refinement.dependencies = std::move(parsed.dependencies);
            }
            if (const auto *stage = Child(authored, "project.when.stage"))
                ParseStage(*stage, "project.when.stage.directory", refinement.stage, diagnostics);
        }

        [[nodiscard]] auto SameSource(const ManifestSourceRange &left, const ManifestSourceRange &right) -> bool
        {
            return left.path == right.path && left.begin.line == right.begin.line &&
                   left.begin.column == right.begin.column && left.end.line == right.end.line &&
                   left.end.column == right.end.column;
        }

        [[nodiscard]] auto KindName(const BuildItemKind kind) -> std::string_view
        {
            switch (kind)
            {
            case BuildItemKind::Source: return "Source";
            case BuildItemKind::Header: return "Header";
            case BuildItemKind::CxxModule: return "CxxModule";
            case BuildItemKind::Resource: return "Resource";
            case BuildItemKind::IncludeDirectory: return "IncludeDirectory";
            case BuildItemKind::Define: return "Define";
            case BuildItemKind::CompileOption: return "CompileOption";
            case BuildItemKind::LinkOption: return "LinkOption";
            case BuildItemKind::PrecompiledHeader: return "PrecompiledHeader";
            }
            return "Unknown";
        }

        [[nodiscard]] auto DefaultVisibility(const SemanticProject &project, const BuildItemKind kind)
            -> BuildVisibility
        {
            return project.artifactKind == ProductArtifactKind::Library && kind == BuildItemKind::Header
                       ? BuildVisibility::Public
                       : BuildVisibility::Private;
        }

        [[nodiscard]] auto HasMagic(const std::string_view pattern) -> bool
        {
            return pattern.find_first_of("*?[") != std::string_view::npos;
        }

        [[nodiscard]] auto IsDefaultExcluded(const std::string_view path) -> bool
        {
            return path.starts_with("build/") || path.starts_with(".ngin/") || path.starts_with(".git/") ||
                   path.starts_with(".hg/") || path.starts_with(".svn/");
        }

        [[nodiscard]] auto GlobBase(const std::string_view pattern) -> std::string
        {
            const auto magic = pattern.find_first_of("*?[");
            if (magic == std::string_view::npos)
            {
                const auto slash = pattern.rfind('/');
                return slash == std::string_view::npos ? std::string{} : std::string(pattern.substr(0, slash));
            }
            const auto slash = pattern.substr(0, magic).rfind('/');
            return slash == std::string_view::npos ? std::string{} : std::string(pattern.substr(0, slash));
        }

        [[nodiscard]] auto ItemIdentity(const ResolvedBuildItem &item) -> std::string
        {
            std::string identity = std::string(KindName(item.kind)) + ":";
            if (item.kind == BuildItemKind::Define || item.kind == BuildItemKind::CompileOption ||
                item.kind == BuildItemKind::LinkOption)
                identity += item.detail + ":" + std::to_string(static_cast<int>(item.visibility));
            else if (item.kind == BuildItemKind::IncludeDirectory || item.kind == BuildItemKind::PrecompiledHeader)
                identity += item.path.value + ":" + std::to_string(static_cast<int>(item.visibility));
            else
                identity += item.path.value;
            if (item.kind == BuildItemKind::Resource && item.destination.has_value())
                identity += "->" + item.destination->value;
            return identity;
        }

        [[nodiscard]] auto EquivalentBuildItem(const ResolvedBuildItem &left, const ResolvedBuildItem &right) -> bool
        {
            return left.kind == right.kind && left.path == right.path && left.destination == right.destination &&
                   left.visibility == right.visibility && left.detail == right.detail && left.value == right.value &&
                   left.generated == right.generated && left.system == right.system;
        }

        [[nodiscard]] auto MatchesDeclaration(const BuildItemDeclaration &declaration, const ResolvedBuildItem &item)
            -> bool
        {
            return declaration.kind == item.kind &&
                   (HasMagic(declaration.pattern) ? GlobMatchesPortable(declaration.pattern, item.path.value)
                                                  : declaration.pattern == item.path.value);
        }
    } // namespace

    auto ResolvedProjectBuild::Succeeded() const -> bool { return diagnostics.empty(); }
    auto SemanticProjectResult::Succeeded() const -> bool { return value.has_value() && diagnostics.empty(); }

    auto ProductArtifactKindName(const ProductArtifactKind kind) -> std::string_view
    {
        return kind == ProductArtifactKind::Executable ? "Executable" : "Library";
    }

    auto LibraryKindName(const LibraryKind kind) -> std::string_view
    {
        switch (kind)
        {
        case LibraryKind::None: return "None";
        case LibraryKind::Static: return "Static";
        case LibraryKind::Shared: return "Shared";
        case LibraryKind::Interface: return "Interface";
        case LibraryKind::Plugin: return "Plugin";
        }
        return "None";
    }

    auto ParseSemanticProject(const AuthoredProjectManifest &project) -> SemanticProjectResult
    {
        SemanticProjectResult result{};
        SemanticProject semantic{
            .manifest = project.manifest, .name = project.name, .artifactKind = ParseArtifactKind(project)};
        if (project.version.has_value()) semantic.version = ParseSemanticVersion(*project.version);
        if (project.artifactKind == "Library")
            semantic.libraryKind = ParseLibraryKind(project.libraryKind.value_or("Static"));

        if (const auto *metadata = Child(project.root, "project.metadata"))
        {
            const auto text = [&](const std::string_view id) -> std::optional<std::string> {
                const auto *node = Child(*metadata, id);
                return node == nullptr ? std::nullopt : std::optional<std::string>{node->text};
            };
            semantic.metadata.description = text("project.metadata.description");
            semantic.metadata.license = text("project.metadata.license");
            semantic.metadata.homepage = text("project.metadata.homepage");
            semantic.metadata.vendor = text("project.metadata.vendor");
        }
        if (const auto *options = Child(project.root, "project.options"))
            ParseOptions(*options, semantic, result.diagnostics);

        constexpr auto rootContext = DependencyContext::Target;
        if (const auto *dependencies = Child(project.root, "project.uses"))
            ParseDependencies(*dependencies, rootContext, std::nullopt, semantic, result.diagnostics);

        if (const auto *build = Child(project.root, "project.build"))
        {
            semantic.hasBuildSection = true;
            ParseBuild(*build, semantic, result.diagnostics);
        }
        for (const auto *generate : Children(project.root, "project.generate"))
        {
            ProjectActionSelection selection{.kind = ActionKind::Generate,
                                             .qualifiedAction = NormalizeQualifiedAction(AttributeValue(*generate, "Using")),
                                             .source = generate->source};
            for (const auto &child : generate->children)
            {
                if (child.name == "Header" || child.name == "Source" || child.name == "File")
                {
                    selection.inputs.push_back(ProjectActionInput{
                        .include = AttributeValue(child, "Include"),
                        .exclude = HasAttribute(child, "Exclude")
                                       ? std::optional<std::string>{AttributeValue(child, "Exclude")}
                                       : std::nullopt,
                        .source = child.source,
                    });
                }
                else if (child.name == "Option")
                {
                    const auto name = AttributeValue(child, "Name");
                    const auto value = AttributeValue(child, "Value");
                    if (const auto [existing, inserted] = selection.options.emplace(name, value);
                        !inserted && existing->second != value)
                        AddError(result.diagnostics, "NGIN5005", "conflicting Generate Option '" + name + "'",
                                 child.source);
                }
                else if (child.name == "Argument")
                    selection.arguments.push_back(child.text);
            }
            ValidateQualifiedAction(selection, result.diagnostics);
            semantic.actions.push_back(std::move(selection));
        }
        if (const auto *tooling = Child(project.root, "project.tooling"))
            for (const auto &verb : tooling->children)
            {
                ProjectActionSelection selection{.kind = ToolingActionKind(verb.name),
                                                 .qualifiedAction = NormalizeQualifiedAction(AttributeValue(verb, "Using")),
                                                 .source = verb.source};
                ValidateQualifiedAction(selection, result.diagnostics);
                semantic.actions.push_back(std::move(selection));
            }
        if (const auto *stage = Child(project.root, "project.stage"))
            ParseStage(*stage, "project.stage.directory", semantic.stage, result.diagnostics);

        std::optional<ManifestSourceRange> defaultRunSource{};
        for (const auto *run : Children(project.root, "project.run"))
        {
            ProjectRunDefinition definition{.name = AttributeValue(*run, "Name", "default"),
                                               .defaultRun = BoolAttributeValue(*run, "Default"),
                                               .product = semantic.name,
                                               .source = run->source};
            if (const auto tool = AttributeValue(*run, "Using"); !tool.empty())
            {
                definition.product.reset();
                definition.tool = NormalizeQualifiedAction(tool);
            }
            if (const auto path = AttributeValue(*run, "WorkingDirectory"); !path.empty())
            {
                if (path == ".")
                    definition.workingDirectory = PortablePath{.value = "."};
                else
                {
                    const auto normalized = NormalizeStageDestination(path, run->source);
                    if (normalized.Succeeded())
                        definition.workingDirectory = *normalized.value;
                    else
                        result.diagnostics.insert(result.diagnostics.end(), normalized.diagnostics.begin(),
                                                  normalized.diagnostics.end());
                }
            }
            for (const auto &child : run->children)
            {
                if (child.specId == "project.run.argument")
                    definition.arguments.push_back(child.text);
                else if (child.specId == "project.run.environment")
                {
                    const auto name = AttributeValue(child, "Name");
                    if (definition.secrets.contains(name) ||
                        !definition.environment.emplace(name, AttributeValue(child, "Value")).second)
                        AddError(result.diagnostics, "NGIN3012", "duplicate Run Environment '" + name + "'",
                                 child.source);
                }
                else if (child.specId == "project.run.secret")
                {
                    const auto name = AttributeValue(child, "Name");
                    if (definition.environment.contains(name) ||
                        !definition.secrets.emplace(name, AttributeValue(child, "From")).second)
                        AddError(result.diagnostics, "NGIN3012", "duplicate Run environment/Secret '" + name + "'",
                                 child.source);
                }
            }
            if (definition.defaultRun)
            {
                if (defaultRunSource.has_value())
                    AddError(result.diagnostics, "NGIN3012", "only one Run may be Default", run->source,
                             {*defaultRunSource});
                else
                    defaultRunSource = run->source;
            }
            semantic.runs.push_back(std::move(definition));
        }
        if (semantic.artifactKind == ProductArtifactKind::Executable && semantic.runs.empty())
            semantic.runs.push_back(ProjectRunDefinition{.name = "default",
                                                                 .defaultRun = true,
                                                                 .product = semantic.name,
                                                                 .implicit = true,
                                                                 .source = project.root.source});
        else if (semantic.runs.size() == 1)
            semantic.runs.front().defaultRun = true;
        else if (semantic.runs.size() > 1 && !defaultRunSource.has_value())
            AddError(result.diagnostics, "NGIN3012", "multiple Run definitions require exactly one Default=\"true\"",
                     project.root.source);

        const auto parseRegistration = [&](const AuthoredElement &registration, ProjectTestingDefinition &definition) {
            definition.name = AttributeValue(registration, "Name", "default");
            definition.source = registration.source;
            definition.timeoutSeconds = ParseInteger(AttributeValue(registration, "Timeout"));
            if (HasAttribute(registration, "Timeout") &&
                (!definition.timeoutSeconds.has_value() || *definition.timeoutSeconds <= 0))
                AddError(result.diagnostics, "NGIN3013", "registration Timeout must be greater than zero",
                         registration.source);
            for (const auto &child : registration.children)
                if (child.specId == "project.registration.argument")
                    definition.arguments.push_back(child.text);
                else if (child.specId == "project.registration.environment")
                {
                    const auto name = AttributeValue(child, "Name");
                    if (!definition.environment.emplace(name, AttributeValue(child, "Value")).second)
                        AddError(result.diagnostics, "NGIN3013", "duplicate registration Environment '" + name + "'",
                                 child.source);
                }
        };
        for (const auto *test : Children(project.root, "project.test"))
        {
            ProjectTestingDefinition definition{};
            parseRegistration(*test, definition);
            semantic.tests.push_back(std::move(definition));
        }
        for (const auto *benchmark : Children(project.root, "project.benchmark"))
        {
            ProjectBenchmarkDefinition definition{};
            parseRegistration(*benchmark, definition);
            definition.repetitions = ParseInteger(AttributeValue(*benchmark, "Repetitions"));
            definition.warmupSeconds = ParseInteger(AttributeValue(*benchmark, "Warmup"));
            semantic.benchmarks.push_back(std::move(definition));
        }
        if (const auto *publish = Child(project.root, "project.publish"))
            for (const auto &output : publish->children)
            {
                const auto normalized = NormalizeStageDestination(AttributeValue(output, "Output"), output.source);
                if (normalized.Succeeded())
                    semantic.publishes.push_back(ProjectPublishDefinition{
                        .name = AttributeValue(output, "Name"),
                        .kind = output.specId == "project.publish.archive"     ? PublishOutputKind::Archive
                                : output.specId == "project.publish.installer" ? PublishOutputKind::Installer
                                                                                : PublishOutputKind::Folder,
                        .format = AttributeValue(output, "Format"),
                        .output = *normalized.value,
                        .source = output.source});
                else
                    result.diagnostics.insert(result.diagnostics.end(), normalized.diagnostics.begin(),
                                              normalized.diagnostics.end());
            }

        for (const auto *authored : Children(project.root, "project.when"))
        {
            ProjectRefinement refinement{};
            ParseProjectRefinement(*authored, semantic, rootContext, refinement, result.diagnostics);
            semantic.refinements.push_back(std::move(refinement));
        }

        if (project.artifactKind == "Library" && project.libraryKind == "Interface")
            for (const auto &declaration : semantic.build.declarations)
                if (declaration.kind == BuildItemKind::Source)
                    AddError(result.diagnostics, "NGIN3000", "Interface Library cannot contain compiled Source items",
                             declaration.source);
        if (project.artifactKind != "Library")
        {
            for (const auto &declaration : semantic.build.declarations)
                if (declaration.visibility.has_value() && *declaration.visibility != BuildVisibility::Private)
                    AddError(result.diagnostics, "NGIN3000",
                             "Public and Interface build visibility requires a Library product", declaration.source);
            for (const auto &refinement : semantic.refinements)
                for (const auto &declaration : refinement.build.declarations)
                    if (declaration.visibility.has_value() && *declaration.visibility != BuildVisibility::Private)
                        AddError(result.diagnostics, "NGIN3000",
                                 "Public and Interface build visibility requires a Library "
                                 "product",
                                 declaration.source);
        }
        if (result.diagnostics.empty()) result.value = std::move(semantic);
        return result;
    }

    auto ApplyProjectRefinements(const SemanticProject &project, const SelectionFacts &selection)
        -> SemanticProjectResult
    {
        SemanticProjectResult result{};
        std::vector<SemanticRefinement> semantic{};
        semantic.reserve(project.refinements.size());
        for (const auto &refinement : project.refinements) semantic.push_back(refinement.semantic);
        const auto resolved = ResolveRefinements(selection, semantic);
        result.diagnostics = resolved.diagnostics;
        if (!result.diagnostics.empty()) return result;

        auto effective = project;
        effective.refinements.clear();
        const auto selected = [&](const RefinementAssignment &assignment) {
            const auto key = assignment.category + "\x1f" + assignment.identity;
            const auto found = resolved.assignments.find(key);
            return found != resolved.assignments.end() && SameSource(found->second.source, assignment.source);
        };
        for (const auto &refinement : project.refinements)
        {
            if (!RefinementMatches(refinement.semantic.selector, selection)) continue;
            effective.dependencies.insert(effective.dependencies.end(), refinement.dependencies.begin(),
                                          refinement.dependencies.end());
            effective.stage.insert(effective.stage.end(), refinement.stage.begin(), refinement.stage.end());
            for (const auto &assignment : refinement.semantic.assignments)
            {
                if (!selected(assignment)) continue;
                if (assignment.category == "BuildScalar" && assignment.identity == "Conventions")
                    effective.build.conventions = refinement.build.conventions->value;
                else if (assignment.category == "BuildScalar" && assignment.identity == "Language")
                    effective.build.language = *refinement.build.language;
                else if (assignment.category == "BuildScalar" && assignment.identity == "UnityBuild")
                    effective.build.unityBuild = refinement.build.unityBuild;
                else if (assignment.category == "BuildConvention")
                    effective.build.namedConventions[assignment.identity] =
                        refinement.build.namedConventions.at(assignment.identity).value;
                else if (assignment.category == "BuildItem")
                {
                    const auto declaration =
                        std::ranges::find_if(refinement.build.declarations, [&](const BuildItemDeclaration &candidate) {
                            return SameSource(candidate.source, assignment.source);
                        });
                    if (declaration != refinement.build.declarations.end())
                        effective.build.declarations.push_back(*declaration);
                }
            }
        }
        result.value = std::move(effective);
        return result;
    }

    auto ResolveProjectBuild(const SemanticProject &project, const std::filesystem::path &projectDirectory,
                             const bool targetCaseInsensitive, const bool allowSymlinks) -> ResolvedProjectBuild
    {
        ResolvedProjectBuild result{.language = project.build.language, .unityBuild = project.build.unityBuild};
        std::vector<std::pair<BuildItemDeclaration, BuildItemOriginKind>> includes{};
        const auto conventionEnabled = [&](const std::string_view name) {
            if (!project.build.conventions) return false;
            const auto found = project.build.namedConventions.find(name);
            return found == project.build.namedConventions.end() || found->second;
        };
        const auto addConvention = [&](const BuildItemKind kind, const std::string &name,
                                       const std::vector<std::string> &patterns,
                                       const std::optional<std::string_view> destination = std::nullopt) {
            if (!conventionEnabled(name)) return;
            for (const auto &pattern : patterns)
                includes.push_back({BuildItemDeclaration{.kind = kind,
                                                         .operation = BuildItemOperation::Include,
                                                         .pattern = pattern,
                                                         .destination = destination.has_value()
                                                                            ? std::optional<PortablePath>{PortablePath{
                                                                                  .value = std::string(*destination)}}
                                                                            : std::nullopt,
                                                         .allowEmpty = true},
                                    BuildItemOriginKind::Convention});
        };
        addConvention(BuildItemKind::Source, "NGIN.Cxx.Sources",
                      {"src/**/*.c", "src/**/*.cc", "src/**/*.cpp", "src/**/*.cxx"});
        addConvention(BuildItemKind::Header, "NGIN.Cxx.Headers",
                      {"include/**/*.h", "include/**/*.hh", "include/**/*.hpp", "include/**/*.hxx"});
        addConvention(BuildItemKind::CxxModule, "NGIN.Cxx.Modules", {"src/**/*.ixx", "src/**/*.cppm"});
        addConvention(BuildItemKind::Resource, "NGIN.Resources.Assets", {"assets/**"}, "assets");
        addConvention(BuildItemKind::Resource, "NGIN.Resources.Resources", {"resources/**"}, "resources");
        for (const auto &declaration : project.build.declarations)
            if (declaration.operation == BuildItemOperation::Include)
                includes.push_back({declaration, BuildItemOriginKind::Authored});

        std::map<std::string, ResolvedBuildItem, std::less<>> items{};
        for (const auto &[declaration, origin] : includes)
        {
            std::vector<PortablePath> paths{};
            const auto fileKind =
                declaration.kind == BuildItemKind::Source || declaration.kind == BuildItemKind::Header ||
                declaration.kind == BuildItemKind::CxxModule || declaration.kind == BuildItemKind::Resource;
            if (fileKind)
            {
                if (declaration.generated.value_or(false) && !HasMagic(declaration.pattern))
                {
                    const auto path =
                        NormalizePortablePath(declaration.pattern, PortablePathBase::Manifest, declaration.source);
                    if (path.Succeeded())
                        paths.push_back(*path.value);
                    else
                        result.diagnostics.insert(result.diagnostics.end(), path.diagnostics.begin(),
                                                  path.diagnostics.end());
                }
                else
                {
                    const auto expanded = ExpandPortableGlob(projectDirectory, declaration.pattern,
                                                             targetCaseInsensitive, declaration.source, allowSymlinks,
                                                             true);
                    result.diagnostics.insert(result.diagnostics.end(), expanded.diagnostics.begin(),
                                              expanded.diagnostics.end());
                    paths = expanded.matches;
                }
                std::erase_if(paths, [&](const PortablePath &path) {
                    return IsDefaultExcluded(path.value) ||
                           (declaration.exclude.has_value() && GlobMatchesPortable(*declaration.exclude, path.value));
                });
            }
            else
            {
                const auto normalized =
                    declaration.kind == BuildItemKind::Define || declaration.kind == BuildItemKind::CompileOption ||
                            declaration.kind == BuildItemKind::LinkOption
                        ? PortablePathResult{.value = PortablePath{.value = declaration.pattern}}
                        : NormalizePortablePath(declaration.pattern, PortablePathBase::Manifest, declaration.source);
                if (normalized.Succeeded())
                    paths.push_back(*normalized.value);
                else
                    result.diagnostics.insert(result.diagnostics.end(), normalized.diagnostics.begin(),
                                              normalized.diagnostics.end());
            }
            if (paths.empty() && !declaration.allowEmpty)
                AddError(result.diagnostics, "NGIN3003",
                         std::string(KindName(declaration.kind)) + " Include '" + declaration.pattern +
                             "' matched no items",
                         declaration.source);
            const auto base = GlobBase(declaration.pattern);
            for (const auto &path : paths)
            {
                ResolvedBuildItem item{
                    .kind = declaration.kind,
                    .path = path,
                    .visibility = declaration.visibility.value_or(DefaultVisibility(project, declaration.kind)),
                    .detail = declaration.kind == BuildItemKind::Define ||
                                      declaration.kind == BuildItemKind::CompileOption ||
                                      declaration.kind == BuildItemKind::LinkOption
                                  ? declaration.pattern
                                  : declaration.detail,
                    .value = declaration.value,
                    .generated = declaration.generated.value_or(false),
                    .system = declaration.system.value_or(false),
                    .origin = origin,
                    .source = declaration.source};
                if (declaration.destination.has_value())
                {
                    auto relative = path.value;
                    if (!base.empty() && relative.starts_with(base + "/")) relative.erase(0, base.size() + 1);
                    item.destination = PortablePath{.value = declaration.destination->value +
                                                             (relative.empty() ? std::string{} : "/" + relative)};
                }
                item.identity = ItemIdentity(item);
                if (const auto existing = items.find(item.identity); existing != items.end())
                {
                    if (!EquivalentBuildItem(existing->second, item))
                        AddError(result.diagnostics, "NGIN3004",
                                 "incompatible duplicate build item '" + item.identity + "'", item.source,
                                 {existing->second.source});
                    else if (origin == BuildItemOriginKind::Authored)
                        existing->second = std::move(item);
                }
                else
                    items.emplace(item.identity, std::move(item));
            }
        }

        for (const auto &declaration : project.build.declarations)
        {
            if (declaration.operation == BuildItemOperation::Include) continue;
            std::vector<std::string> matches{};
            for (const auto &[identity, item] : items)
                if (MatchesDeclaration(declaration, item)) matches.push_back(identity);
            if (matches.empty() && !declaration.allowEmpty)
                AddError(result.diagnostics, "NGIN3003",
                         std::string(KindName(declaration.kind)) + " operation '" + declaration.pattern +
                             "' matched no existing items",
                         declaration.source);
            if (declaration.operation == BuildItemOperation::Remove)
            {
                for (const auto &identity : matches) items.erase(identity);
                continue;
            }
            for (const auto &identity : matches)
            {
                auto item = items.at(identity);
                if (declaration.visibility.has_value()) item.visibility = *declaration.visibility;
                if (declaration.generated.has_value()) item.generated = *declaration.generated;
                if (!declaration.detail.empty()) item.detail = declaration.detail;
                if (declaration.value.has_value()) item.value = declaration.value;
                item.origin = BuildItemOriginKind::Updated;
                item.source = declaration.source;
                item.identity = ItemIdentity(item);
                items.erase(identity);
                if (const auto existing = items.find(item.identity);
                    existing != items.end() && !EquivalentBuildItem(existing->second, item))
                    AddError(result.diagnostics, "NGIN3004", "Update conflicts for build item '" + item.identity + "'",
                             declaration.source, {existing->second.source});
                else
                    items[item.identity] = std::move(item);
            }
        }
        for (auto &[_, item] : items)
        {
            if (project.libraryKind == LibraryKind::Interface &&
                (item.kind == BuildItemKind::Source || item.kind == BuildItemKind::CxxModule ||
                 item.kind == BuildItemKind::PrecompiledHeader))
                AddError(result.diagnostics, "NGIN3000",
                         "Interface Library cannot contain compiled " + std::string(KindName(item.kind)) + " items",
                         item.source);
            result.items.push_back(std::move(item));
        }
        if (project.libraryKind == LibraryKind::Interface && result.unityBuild.has_value() && result.unityBuild->enabled)
            AddError(result.diagnostics, "NGIN3000", "Interface Library cannot enable UnityBuild",
                     result.unityBuild->source);
        return result;
    }
} // namespace NGIN::CLI
