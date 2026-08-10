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

        [[nodiscard]] auto ParseProductType(const std::string_view value) -> ProductType
        {
            if (value == "Library") return ProductType::Library;
            if (value == "Tool") return ProductType::Tool;
            if (value == "Test") return ProductType::Test;
            if (value == "Benchmark") return ProductType::Benchmark;
            if (value == "Plugin") return ProductType::Plugin;
            if (value == "External") return ProductType::External;
            return ProductType::Application;
        }

        [[nodiscard]] auto ParseLinkage(const std::string_view value) -> LibraryLinkage
        {
            if (value == "Shared") return LibraryLinkage::Shared;
            if (value == "Interface") return LibraryLinkage::Interface;
            return LibraryLinkage::Static;
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

        auto ValidateQualifiedAction(const ProjectActionSelection &selection,
                                     std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            const auto separator = selection.qualifiedAction.rfind("::");
            if (separator == std::string::npos || separator == 0 || separator + 2 == selection.qualifiedAction.size())
                AddError(diagnostics, "NGIN5005",
                         "Action must be qualified as Package::Export, got '" + selection.qualifiedAction + "'",
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
            if (HasAttribute(use, "Library")) return ExportUseKind::Library;
            if (HasAttribute(use, "Tool")) return ExportUseKind::Tool;
            if (HasAttribute(use, "Plugin")) return ExportUseKind::Plugin;
            if (HasAttribute(use, "Action")) return ExportUseKind::Action;
            if (HasAttribute(use, "Asset")) return ExportUseKind::Asset;
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
                        if (child.name == "Use")
                        {
                            const auto kind = UseKind(child);
                            if (kind.has_value())
                            {
                                const auto name = child.attributes.empty() ? std::string{} : child.attributes.front().value;
                                request.exports.push_back(ExportUse{.kind = *kind, .name = name, .source = child.source});
                            }
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
                    ProjectDependencyRequest request{.name = AttributeValue(node, "Name"),
                                                     .context = context,
                                                     .owner = owner,
                                                     .source = node.source};
                    if (const auto path = AttributeValue(node, "Path"); !path.empty())
                    {
                        const auto normalized = NormalizePortablePath(path, PortablePathBase::Manifest, node.source);
                        if (normalized.Succeeded()) request.path = *normalized.value;
                        else diagnostics.insert(diagnostics.end(), normalized.diagnostics.begin(), normalized.diagnostics.end());
                    }
                    project.dependencies.emplace_back(std::move(request));
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
                    project.build.unityBuild = UnityBuildSetting{.enabled = BoolAttributeValue(node, "Enabled"),
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
                if (const auto exclude = AttributeValue(node, "Exclude"); !exclude.empty()) declaration.exclude = exclude;
                if (const auto into = AttributeValue(node, "Into"); !into.empty())
                {
                    const auto destination = NormalizeStageDestination(into, node.source);
                    if (destination.Succeeded()) declaration.destination = *destination.value;
                    else diagnostics.insert(diagnostics.end(), destination.diagnostics.begin(), destination.diagnostics.end());
                }
                if (HasAttribute(node, "Visibility")) declaration.visibility = ParseVisibility(AttributeValue(node, "Visibility"));
                if (HasAttribute(node, "Generated")) declaration.generated = BoolAttributeValue(node, "Generated");
                if (HasAttribute(node, "System")) declaration.system = BoolAttributeValue(node, "System");
                declaration.allowEmpty = BoolAttributeValue(node, "AllowEmpty");
                declaration.detail = AttributeValue(node, "Kind");
                if (HasAttribute(node, "Value") && *kind == BuildItemKind::Define)
                    declaration.value = AttributeValue(node, "Value");
                project.build.declarations.push_back(std::move(declaration));
            }
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

        [[nodiscard]] auto DefaultVisibility(const SemanticProject &project, const BuildItemKind kind) -> BuildVisibility
        {
            return project.type == ProductType::Library && kind == BuildItemKind::Header ? BuildVisibility::Public
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
    }

    auto ResolvedProjectBuild::Succeeded() const -> bool { return diagnostics.empty(); }
    auto SemanticProjectResult::Succeeded() const -> bool { return value.has_value() && diagnostics.empty(); }

    auto ProductTypeName(const ProductType type) -> std::string_view
    {
        switch (type)
        {
        case ProductType::Application: return "Application";
        case ProductType::Library: return "Library";
        case ProductType::Tool: return "Tool";
        case ProductType::Test: return "Test";
        case ProductType::Benchmark: return "Benchmark";
        case ProductType::Plugin: return "Plugin";
        case ProductType::External: return "External";
        }
        return "Unknown";
    }

    auto ParseSemanticProject(const AuthoredProjectManifest &project) -> SemanticProjectResult
    {
        SemanticProjectResult result{};
        SemanticProject semantic{.manifest = project.manifest,
                                 .name = project.name,
                                 .type = ParseProductType(project.type)};
        if (project.version.has_value()) semantic.version = ParseSemanticVersion(*project.version);
        if (semantic.type == ProductType::Library)
            semantic.linkage = ParseLinkage(AttributeValue(project.root, "Linkage", "Static"));
        else if (HasAttribute(project.root, "Linkage"))
            AddError(result.diagnostics, "NGIN3000", "Linkage is valid only for a Library product", project.root.source);

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
        if (const auto *options = Child(project.root, "project.options")) ParseOptions(*options, semantic, result.diagnostics);

        DependencyContext rootContext = DependencyContext::Target;
        if (semantic.type == ProductType::Test) rootContext = DependencyContext::Test;
        else if (semantic.type == ProductType::Benchmark) rootContext = DependencyContext::Benchmark;
        if (const auto *dependencies = Child(project.root, "project.dependencies"))
            ParseDependencies(*dependencies, rootContext, std::nullopt, semantic, result.diagnostics);

        if (const auto *build = Child(project.root, "project.build"))
        {
            semantic.hasBuildSection = true;
            ParseBuild(*build, semantic, result.diagnostics);
        }
        for (const auto *generate : Children(project.root, "project.generate"))
        {
            ProjectActionSelection selection{.kind = ActionKind::Generate,
                                             .qualifiedAction = AttributeValue(*generate, "Action"),
                                             .source = generate->source};
            for (const auto &child : generate->children)
            {
                if (child.name == "Input")
                {
                    if (!HasAttribute(child, "Include") || HasAttribute(child, "Remove") || HasAttribute(child, "Update"))
                        AddError(result.diagnostics, "NGIN5005",
                                 "Generate Input requires Include and cannot Remove or Update", child.source);
                    else
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
                else if (child.name == "Argument") selection.arguments.push_back(child.text);
            }
            ValidateQualifiedAction(selection, result.diagnostics);
            semantic.actions.push_back(std::move(selection));
        }
        if (const auto *tooling = Child(project.root, "project.tooling"))
            for (const auto &verb : tooling->children)
            {
                ProjectActionSelection selection{.kind = ToolingActionKind(verb.name),
                                                 .qualifiedAction = AttributeValue(verb, "Action"),
                                                 .source = verb.source};
                ValidateQualifiedAction(selection, result.diagnostics);
                semantic.actions.push_back(std::move(selection));
            }
        if (const auto *stage = Child(project.root, "project.stage"))
            for (const auto &node : stage->children)
            {
                const auto include = NormalizePortablePath(AttributeValue(node, "Include"),
                                                           PortablePathBase::Manifest, node.source);
                const auto destination = NormalizeStageDestination(AttributeValue(node, "Into"), node.source);
                if (!include.Succeeded())
                    result.diagnostics.insert(result.diagnostics.end(), include.diagnostics.begin(), include.diagnostics.end());
                if (!destination.Succeeded())
                    result.diagnostics.insert(result.diagnostics.end(), destination.diagnostics.begin(), destination.diagnostics.end());
                if (include.Succeeded() && destination.Succeeded())
                    semantic.stage.push_back(ProjectStageInput{.kind = node.specId == "project.stage.directory"
                                                                           ? StageInputKind::Directory
                                                                           : StageInputKind::File,
                                                                .include = *include.value,
                                                                .destination = *destination.value,
                                                                .source = node.source});
            }

        std::optional<ManifestSourceRange> defaultLaunchSource{};
        for (const auto *launch : Children(project.root, "project.launch"))
        {
            ProjectLaunchDefinition definition{.name = AttributeValue(*launch, "Name"),
                                               .defaultLaunch = BoolAttributeValue(*launch, "Default"),
                                               .product = semantic.name,
                                               .source = launch->source};
            if (const auto *executable = Child(*launch, "project.launch.executable"))
            {
                const auto productName = AttributeValue(*executable, "Product");
                const auto toolName = AttributeValue(*executable, "Tool");
                if (!productName.empty() && !toolName.empty())
                    AddError(result.diagnostics, "NGIN3012", "Launch Executable selects either Product or Tool, not both",
                             executable->source);
                else if (!toolName.empty())
                {
                    definition.product.reset();
                    definition.tool = toolName;
                }
                else if (!productName.empty()) definition.product = productName;
            }
            if (const auto *working = Child(*launch, "project.launch.working-directory"))
            {
                const auto path = AttributeValue(*working, "Path");
                if (path == ".") definition.workingDirectory = PortablePath{.value = "."};
                else
                {
                    const auto normalized = NormalizeStageDestination(path, working->source);
                    if (normalized.Succeeded()) definition.workingDirectory = *normalized.value;
                    else result.diagnostics.insert(result.diagnostics.end(), normalized.diagnostics.begin(), normalized.diagnostics.end());
                }
            }
            for (const auto &child : launch->children)
            {
                if (child.specId == "project.launch.argument") definition.arguments.push_back(child.text);
                else if (child.specId == "project.launch.environment")
                {
                    const auto name = AttributeValue(child, "Name");
                    if (definition.secrets.contains(name) ||
                        !definition.environment.emplace(name, AttributeValue(child, "Value")).second)
                        AddError(result.diagnostics, "NGIN3012", "duplicate Launch Environment '" + name + "'", child.source);
                }
                else if (child.specId == "project.launch.secret")
                {
                    const auto name = AttributeValue(child, "Name");
                    if (definition.environment.contains(name) ||
                        !definition.secrets.emplace(name, AttributeValue(child, "From")).second)
                        AddError(result.diagnostics, "NGIN3012", "duplicate Launch environment/Secret '" + name + "'", child.source);
                }
            }
            if (definition.defaultLaunch)
            {
                if (defaultLaunchSource.has_value())
                    AddError(result.diagnostics, "NGIN3012", "only one Launch may be Default", launch->source,
                             {*defaultLaunchSource});
                else defaultLaunchSource = launch->source;
            }
            semantic.launches.push_back(std::move(definition));
        }
        if (const auto *testing = Child(project.root, "project.testing"))
        {
            ProjectTestingDefinition definition{.source = testing->source};
            for (const auto &child : testing->children)
            {
                if (child.specId == "project.testing.argument") definition.arguments.push_back(child.text);
                else if (child.specId == "project.testing.timeout")
                {
                    definition.timeoutSeconds = ParseInteger(AttributeValue(child, "Seconds"));
                    if (!definition.timeoutSeconds.has_value() || *definition.timeoutSeconds <= 0)
                        AddError(result.diagnostics, "NGIN3013", "Testing Timeout Seconds must be greater than zero",
                                 child.source);
                }
            }
            semantic.testing = std::move(definition);
            if (const auto *dependencies = Child(*testing, "project.testing.dependencies"))
            {
                if (semantic.type == ProductType::Test || semantic.type == ProductType::Benchmark)
                    AddError(result.diagnostics, "NGIN3000", std::string(ProductTypeName(semantic.type)) +
                                                              " products declare test dependencies at the root",
                             dependencies->source);
                ParseDependencies(*dependencies, DependencyContext::Test, std::nullopt, semantic, result.diagnostics);
            }
        }
        for (const auto *publish : Children(project.root, "project.publish"))
        {
            const AuthoredElement *output = nullptr;
            for (const auto &child : publish->children)
                if (child.specId == "project.publish.folder" || child.specId == "project.publish.archive" ||
                    child.specId == "project.publish.installer") output = &child;
            if (output != nullptr)
            {
                const auto normalized = NormalizeStageDestination(AttributeValue(*output, "Output"), output->source);
                if (normalized.Succeeded())
                    semantic.publishes.push_back(ProjectPublishDefinition{
                        .name = AttributeValue(*publish, "Name"),
                        .kind = output->specId == "project.publish.archive" ? PublishOutputKind::Archive
                              : output->specId == "project.publish.installer" ? PublishOutputKind::Installer
                                                                              : PublishOutputKind::Folder,
                        .format = AttributeValue(*output, "Format"),
                        .output = *normalized.value,
                        .source = publish->source});
                else result.diagnostics.insert(result.diagnostics.end(), normalized.diagnostics.begin(), normalized.diagnostics.end());
            }
            if (const auto *dependencies = Child(*publish, "project.publish.dependencies"))
                ParseDependencies(*dependencies, DependencyContext::Publish, AttributeValue(*publish, "Name"), semantic,
                                  result.diagnostics);
        }

        if ((semantic.type == ProductType::Library || semantic.type == ProductType::Plugin) && !semantic.launches.empty())
            AddError(result.diagnostics, "NGIN3000", std::string(ProductTypeName(semantic.type)) +
                                                          " products cannot declare Launch",
                     project.root.source);
        if (semantic.type == ProductType::External && semantic.hasBuildSection)
            AddError(result.diagnostics, "NGIN3000", "External products cannot declare core Build inputs",
                     project.root.source);
        if (semantic.type != ProductType::Library)
        {
            for (const auto &declaration : semantic.build.declarations)
                if (declaration.visibility.has_value() && *declaration.visibility != BuildVisibility::Private)
                    AddError(result.diagnostics, "NGIN3000", "Public and Interface build visibility requires a Library product",
                             declaration.source);
        }
        if (result.diagnostics.empty()) result.value = std::move(semantic);
        return result;
    }

    auto ResolveProjectBuild(const SemanticProject &project, const std::filesystem::path &projectDirectory)
        -> ResolvedProjectBuild
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
            const auto fileKind = declaration.kind == BuildItemKind::Source || declaration.kind == BuildItemKind::Header ||
                                  declaration.kind == BuildItemKind::CxxModule || declaration.kind == BuildItemKind::Resource;
            if (fileKind)
            {
                if (declaration.generated.value_or(false) && !HasMagic(declaration.pattern))
                {
                    const auto path = NormalizePortablePath(declaration.pattern, PortablePathBase::Manifest, declaration.source);
                    if (path.Succeeded()) paths.push_back(*path.value);
                    else result.diagnostics.insert(result.diagnostics.end(), path.diagnostics.begin(), path.diagnostics.end());
                }
                else
                {
                    const auto expanded = ExpandPortableGlob(projectDirectory, declaration.pattern, false, declaration.source);
                    result.diagnostics.insert(result.diagnostics.end(), expanded.diagnostics.begin(), expanded.diagnostics.end());
                    paths = expanded.matches;
                }
                std::erase_if(paths, [&](const PortablePath &path) {
                    return IsDefaultExcluded(path.value) ||
                           (declaration.exclude.has_value() && GlobMatchesPortable(*declaration.exclude, path.value));
                });
            }
            else
            {
                const auto normalized = declaration.kind == BuildItemKind::Define ||
                                                declaration.kind == BuildItemKind::CompileOption ||
                                                declaration.kind == BuildItemKind::LinkOption
                                            ? PortablePathResult{.value = PortablePath{.value = declaration.pattern}}
                                            : NormalizePortablePath(declaration.pattern, PortablePathBase::Manifest,
                                                                    declaration.source);
                if (normalized.Succeeded()) paths.push_back(*normalized.value);
                else result.diagnostics.insert(result.diagnostics.end(), normalized.diagnostics.begin(),
                                               normalized.diagnostics.end());
            }
            if (paths.empty() && !declaration.allowEmpty)
                AddError(result.diagnostics, "NGIN3003", std::string(KindName(declaration.kind)) + " Include '" +
                                                              declaration.pattern + "' matched no items",
                         declaration.source);
            const auto base = GlobBase(declaration.pattern);
            for (const auto &path : paths)
            {
                ResolvedBuildItem item{.kind = declaration.kind,
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
                AddError(result.diagnostics, "NGIN3003", std::string(KindName(declaration.kind)) + " operation '" +
                                                              declaration.pattern + "' matched no existing items",
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
            if (project.linkage == LibraryLinkage::Interface &&
                (item.kind == BuildItemKind::Source || item.kind == BuildItemKind::CxxModule ||
                 item.kind == BuildItemKind::PrecompiledHeader))
                AddError(result.diagnostics, "NGIN3000",
                         "Interface Library cannot contain compiled " + std::string(KindName(item.kind)) + " items",
                         item.source);
            result.items.push_back(std::move(item));
        }
        if (project.linkage == LibraryLinkage::Interface && result.unityBuild.has_value() && result.unityBuild->enabled)
            AddError(result.diagnostics, "NGIN3000", "Interface Library cannot enable UnityBuild",
                     result.unityBuild->source);
        return result;
    }
}
