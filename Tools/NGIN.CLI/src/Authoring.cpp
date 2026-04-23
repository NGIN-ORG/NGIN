#include "Authoring.hpp"

#include "Support.hpp"

#include <algorithm>
#include <set>
#include <sstream>
#include <unordered_map>

namespace NGIN::CLI
{
    namespace
    {
        auto ValidateSchemaVersion(const XmlElement &node, const fs::path &path) -> void
        {
            const auto schemaVersion = RequireAttribute(node, "SchemaVersion", path);
            if (schemaVersion != "2")
            {
                throw std::runtime_error(path.string() + ": unsupported SchemaVersion '" + schemaVersion + "' (expected '2')");
            }
        }

        [[nodiscard]] auto IsValidStartupStage(const std::string_view value) -> bool
        {
            return value == "Foundation" || value == "Platform" || value == "Services" || value == "Features" || value == "Presentation";
        }

        [[nodiscard]] auto IsValidModuleFamily(const std::string_view value) -> bool
        {
            return value == "Base" || value == "Reflection" || value == "Core" || value == "Platform" || value == "Editor" || value == "Domain" || value == "App";
        }

        [[nodiscard]] auto IsSupportedBuildVisibility(const std::string_view value) -> bool
        {
            return value == "Private" || value == "Public" || value == "Interface";
        }

        [[nodiscard]] auto IsSupportedPackageBuildMode(const std::string_view value) -> bool
        {
            return value == "Manual" || value == "FindPackage" || value == "AddSubdirectory";
        }

        [[nodiscard]] auto ParseCompatibility(const XmlElement &node, const fs::path &path) -> CompatibilityDefinition
        {
            CompatibilityDefinition compatibility{};
            if (FindChild(node, "Platforms") != nullptr)
            {
                throw std::runtime_error(path.string() + ": legacy <Platforms> is no longer supported; use <Compatibility>");
            }
            if (FindChild(node, "SupportedHosts") != nullptr)
            {
                throw std::runtime_error(path.string() + ": legacy <SupportedHosts> is no longer supported");
            }
            if (const auto *section = FindChild(node, "Compatibility"))
            {
                if (const auto *operatingSystems = FindChild(*section, "OperatingSystems"))
                {
                    for (const auto *entry : ChildElements(*operatingSystems, "OperatingSystem"))
                    {
                        const auto value = RequireAttribute(*entry, "Name", path);
                        if (!IsValidOperatingSystem(value))
                        {
                            throw std::runtime_error(path.string() + ": unknown operating system '" + value + "'");
                        }
                        compatibility.operatingSystems.push_back(value);
                    }
                }
                if (const auto *architectures = FindChild(*section, "Architectures"))
                {
                    for (const auto *entry : ChildElements(*architectures, "Architecture"))
                    {
                        const auto value = RequireAttribute(*entry, "Name", path);
                        if (!IsValidArchitecture(value))
                        {
                            throw std::runtime_error(path.string() + ": unknown architecture '" + value + "'");
                        }
                        compatibility.architectures.push_back(value);
                    }
                }
            }
            return compatibility;
        }

        auto ParseConfigSources(const XmlElement &parent, const fs::path &path, std::vector<std::string> &out) -> void
        {
            if (const auto *config = FindChild(parent, "ConfigSources"))
            {
                for (const auto *item : ChildElements(*config, "Config"))
                {
                    out.push_back(RequireAttribute(*item, "Source", path));
                }
            }
        }

        auto ParseContents(const XmlElement &parent, const fs::path &path, std::vector<ContentFile> &out) -> void
        {
            if (const auto *contents = FindChild(parent, "Contents"))
            {
                for (const auto *node : ChildElements(*contents, "File"))
                {
                    ContentFile content{};
                    content.source = RequireAttribute(*node, "Source", path);
                    content.kind = Attribute(*node, "Kind").value_or("other");
                    content.target = Attribute(*node, "Target").value_or("");
                    out.push_back(std::move(content));
                }
            }
        }

        auto ParseVariables(const XmlElement &parent, const fs::path &path, std::vector<EnvironmentVariable> &out) -> void
        {
            if (const auto *variables = FindChild(parent, "Variables"))
            {
                for (const auto *node : ChildElements(*variables, "Variable"))
                {
                    EnvironmentVariable variable{};
                    variable.name = RequireAttribute(*node, "Name", path);
                    variable.value = RequireAttribute(*node, "Value", path);
                    out.push_back(std::move(variable));
                }
            }
        }

        auto ParseFeatures(const XmlElement &parent, const fs::path &path, std::vector<FeatureFlag> &out) -> void
        {
            if (const auto *features = FindChild(parent, "Features"))
            {
                for (const auto *node : ChildElements(*features, "Feature"))
                {
                    FeatureFlag feature{};
                    feature.name = RequireAttribute(*node, "Name", path);
                    feature.enabled = !Attribute(*node, "Enabled").has_value() || BoolAttribute(*node, "Enabled", true);
                    out.push_back(std::move(feature));
                }
            }
        }

        [[nodiscard]] auto ParseModuleDefinition(const XmlElement &node, const fs::path &path) -> ModuleDescriptor;

        auto ParseRuntimeDefinition(const XmlElement &runtime, const fs::path &path, RuntimeDefinition &target, const bool allowModules) -> void
        {
            if (allowModules)
            {
                if (const auto *modules = FindChild(runtime, "Modules"))
                {
                    for (const auto *node : ChildElements(*modules, "Module"))
                    {
                        target.modules.push_back(ParseModuleDefinition(*node, path));
                    }
                }
            }
            if (const auto *enableModules = FindChild(runtime, "EnableModules"))
            {
                for (const auto *node : ChildElements(*enableModules, "ModuleRef"))
                {
                    target.enableModules.push_back(RequireAttribute(*node, "Name", path));
                }
            }
            if (const auto *disableModules = FindChild(runtime, "DisableModules"))
            {
                for (const auto *node : ChildElements(*disableModules, "ModuleRef"))
                {
                    target.disableModules.push_back(RequireAttribute(*node, "Name", path));
                }
            }
            if (const auto *enablePlugins = FindChild(runtime, "EnablePlugins"))
            {
                for (const auto *node : ChildElements(*enablePlugins, "PluginRef"))
                {
                    target.enablePlugins.push_back(RequireAttribute(*node, "Name", path));
                }
            }
            if (const auto *disablePlugins = FindChild(runtime, "DisablePlugins"))
            {
                for (const auto *node : ChildElements(*disablePlugins, "PluginRef"))
                {
                    target.disablePlugins.push_back(RequireAttribute(*node, "Name", path));
                }
            }
        }

        [[nodiscard]] auto ResolveStartupStage(const XmlElement &node, std::string_view defaultStage) -> std::string
        {
            if (const auto startupStage = Attribute(node, "StartupStage"); startupStage.has_value() && !startupStage->empty())
            {
                return *startupStage;
            }
            return std::string(defaultStage);
        }

        auto ValidateModuleDescriptor(const ModuleDescriptor &module, const fs::path &path) -> void
        {
            if (!IsValidModuleFamily(module.family))
            {
                throw std::runtime_error(path.string() + ": unknown module family '" + module.family + "'");
            }
            if (!IsValidStartupStage(module.startupStage))
            {
                throw std::runtime_error(path.string() + ": unknown startup stage '" + module.startupStage + "'");
            }
        }

        [[nodiscard]] auto IsValidPackageBootstrapMode(const std::string_view value) -> bool
        {
            return value == "BuilderHookV1";
        }

        [[nodiscard]] auto DiscoverPackageSourceRoots(const fs::path &start) -> std::vector<fs::path>
        {
            std::vector<fs::path> roots;
            std::set<fs::path> unique;
            auto current = fs::weakly_canonical(fs::is_regular_file(start) ? start.parent_path() : start);
            while (true)
            {
                const auto candidate = current / "Packages";
                if (fs::exists(candidate) && fs::is_directory(candidate))
                {
                    const auto normalized = candidate.lexically_normal();
                    if (unique.insert(normalized).second)
                    {
                        roots.push_back(normalized);
                    }
                }
                if (current == current.parent_path())
                {
                    break;
                }
                current = current.parent_path();
            }
            return roots;
        }

        [[nodiscard]] auto ParseModuleDefinition(const XmlElement &node, const fs::path &path) -> ModuleDescriptor
        {
            ModuleDescriptor module{};
            module.name = RequireAttribute(node, "Name", path);
            module.family = Attribute(node, "Family").value_or("App");
            module.type = Attribute(node, "Type").value_or("Runtime");
            module.startupStage = ResolveStartupStage(node, "Features");
            module.version = Attribute(node, "Version").value_or("");
            module.compatiblePlatformRange = Attribute(node, "CompatiblePlatformRange").value_or("");
            module.requiresReflection = BoolAttribute(node, "ReflectionRequired");
            ValidateModuleDescriptor(module, path);
            module.compatibility = ParseCompatibility(node, path);

            if (const auto *dependencies = FindChild(node, "Dependencies"))
            {
                for (const auto *dep : ChildElements(*dependencies, "Dependency"))
                {
                    const auto name = RequireAttribute(*dep, "Name", path);
                    if (BoolAttribute(*dep, "Optional"))
                    {
                        module.optional.push_back(name);
                    }
                    else
                    {
                        module.required.push_back(name);
                    }
                }
            }

            if (const auto *providesServices = FindChild(node, "ProvidesServices"))
            {
                for (const auto *service : ChildElements(*providesServices, "Service"))
                {
                    module.providesServices.push_back(RequireAttribute(*service, "Name", path));
                }
            }

            if (const auto *requiresServices = FindChild(node, "RequiresServices"))
            {
                for (const auto *service : ChildElements(*requiresServices, "Service"))
                {
                    module.requiresServices.push_back(RequireAttribute(*service, "Name", path));
                }
            }

            if (const auto *capabilities = FindChild(node, "Capabilities"))
            {
                for (const auto *capability : ChildElements(*capabilities, "Capability"))
                {
                    module.capabilities.push_back(RequireAttribute(*capability, "Name", path));
                }
            }

            return module;
        }

        [[nodiscard]] auto ParseBuildSetting(const XmlElement &node, const fs::path &path, std::string_view valueAttribute) -> BuildSetting
        {
            BuildSetting setting{};
            setting.value = RequireAttribute(node, valueAttribute, path);
            setting.visibility = Attribute(node, "Visibility").value_or("Private");
            if (!IsSupportedBuildVisibility(setting.visibility))
            {
                throw std::runtime_error(path.string() + ": unknown build visibility '" + setting.visibility + "'");
            }
            return setting;
        }

        auto LoadProjectBuildDescriptor(ProjectBuildDescriptor &build, const XmlElement *buildElement, const fs::path &path) -> void
        {
            if (buildElement == nullptr)
            {
                return;
            }

            if (const auto backend = Attribute(*buildElement, "Backend"); backend.has_value() && !backend->empty())
            {
                build.backend = *backend;
            }
            if (const auto mode = Attribute(*buildElement, "Mode"); mode.has_value() && !mode->empty())
            {
                build.mode = *mode;
            }
            if (!IsSupportedProjectBuildMode(build.mode))
            {
                throw std::runtime_error(path.string() + ": unknown project build mode '" + build.mode + "'");
            }
            if (const auto language = Attribute(*buildElement, "Language"); language.has_value() && !language->empty())
            {
                build.language = *language;
            }
            if (const auto languageStandard = Attribute(*buildElement, "LanguageStandard"); languageStandard.has_value() && !languageStandard->empty())
            {
                build.languageStandard = *languageStandard;
            }

            if (const auto *sources = FindChild(*buildElement, "Sources"))
            {
                for (const auto *item : ChildElements(*sources, "Source"))
                {
                    build.sources.push_back(RequireAttribute(*item, "Path", path));
                }
            }

            if (const auto *includeDirectories = FindChild(*buildElement, "IncludeDirectories"))
            {
                for (const auto *item : ChildElements(*includeDirectories, "IncludeDirectory"))
                {
                    build.includeDirectories.push_back(ParseBuildSetting(*item, path, "Path"));
                }
            }

            if (const auto *compileDefinitions = FindChild(*buildElement, "CompileDefinitions"))
            {
                for (const auto *item : ChildElements(*compileDefinitions, "Definition"))
                {
                    build.compileDefinitions.push_back(ParseBuildSetting(*item, path, "Value"));
                }
            }

            if (const auto *compileOptions = FindChild(*buildElement, "CompileOptions"))
            {
                for (const auto *item : ChildElements(*compileOptions, "Option"))
                {
                    build.compileOptions.push_back(ParseBuildSetting(*item, path, "Value"));
                }
            }

            if (const auto *linkOptions = FindChild(*buildElement, "LinkOptions"))
            {
                for (const auto *item : ChildElements(*linkOptions, "Option"))
                {
                    build.linkOptions.push_back(ParseBuildSetting(*item, path, "Value"));
                }
            }
        }

        auto LoadPackageBuildDescriptor(PackageBuildDescriptor &build, const XmlElement *buildElement, const fs::path &path) -> void
        {
            if (buildElement == nullptr)
            {
                return;
            }
            build.backend = Attribute(*buildElement, "Backend").value_or("");
            build.mode = Attribute(*buildElement, "Mode").value_or("");
            if (!build.mode.empty() && !IsSupportedPackageBuildMode(build.mode))
            {
                throw std::runtime_error(path.string() + ": unknown package build mode '" + build.mode + "'");
            }
            if (const auto *options = FindChild(*buildElement, "Options"))
            {
                for (const auto *item : ChildElements(*options, "Option"))
                {
                    BuildVariable variable{};
                    variable.name = RequireAttribute(*item, "Name", path);
                    variable.value = RequireAttribute(*item, "Value", path);
                    build.options.push_back(std::move(variable));
                }
            }
        }
    }

    [[nodiscard]] auto IsSupportedBuildConfiguration(std::string_view value) -> bool
    {
        return value == "Debug" || value == "Release" || value == "RelWithDebInfo" || value == "MinSizeRel";
    }

    [[nodiscard]] auto IsSupportedProjectBuildMode(std::string_view value) -> bool
    {
        return value == "Generated" || value == "Manual";
    }

    [[nodiscard]] auto IsValidOperatingSystem(std::string_view value) -> bool
    {
        return value == "linux" || value == "windows" || value == "macos";
    }

    [[nodiscard]] auto IsValidArchitecture(std::string_view value) -> bool
    {
        return value == "x64" || value == "arm64";
    }

    [[nodiscard]] auto IsSupportedProjectType(std::string_view value) -> bool
    {
        return value == "Application" || value == "Tool" || value == "Library";
    }

    [[nodiscard]] auto IsSupportedOutputKind(std::string_view value) -> bool
    {
        return value == "Executable" || value == "StaticLibrary" || value == "SharedLibrary";
    }

    [[nodiscard]] auto IsValidProjectOutputPairing(std::string_view projectType, std::string_view outputKind) -> bool
    {
        if (projectType == "Application" || projectType == "Tool")
        {
            return outputKind == "Executable";
        }
        if (projectType == "Library")
        {
            return outputKind == "StaticLibrary" || outputKind == "SharedLibrary";
        }
        return false;
    }

    [[nodiscard]] auto WorkspaceFilePath(const fs::path &root) -> std::optional<fs::path>
    {
        if (!fs::exists(root))
        {
            return std::nullopt;
        }
        std::vector<fs::path> candidates;
        for (const auto &entry : fs::directory_iterator(root))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".ngin")
            {
                candidates.push_back(entry.path());
            }
        }
        if (candidates.empty())
        {
            return std::nullopt;
        }
        std::sort(candidates.begin(), candidates.end());
        return candidates.front();
    }

    [[nodiscard]] auto RootDirFrom(const fs::path &start) -> std::optional<fs::path>
    {
        auto current = fs::weakly_canonical(start);
        if (fs::is_regular_file(current))
        {
            current = current.parent_path();
        }
        while (!current.empty())
        {
            if (WorkspaceFilePath(current).has_value())
            {
                return current;
            }
            if (current == current.parent_path())
            {
                break;
            }
            current = current.parent_path();
        }
        return std::nullopt;
    }

    [[nodiscard]] auto RootDir(const char *argv0) -> fs::path
    {
        if (const auto fromExe = RootDirFrom(fs::absolute(argv0)); fromExe.has_value())
        {
            return *fromExe;
        }
        if (const auto fromCwd = RootDirFrom(fs::current_path()); fromCwd.has_value())
        {
            return *fromCwd;
        }
        return fs::current_path();
    }

    [[nodiscard]] auto LoadWorkspaceManifest(const fs::path &root) -> WorkspaceManifest
    {
        const auto path = WorkspaceFilePath(root);
        if (!path.has_value())
        {
            throw std::runtime_error(root.string() + ": no .ngin workspace file found");
        }
        const auto doc = LoadXml(*path);
        const auto *rootElement = doc.document.Root();
        if (rootElement == nullptr || rootElement->name != "Workspace")
        {
            throw std::runtime_error(path->string() + ": root element must be <Workspace>");
        }
        ValidateSchemaVersion(*rootElement, *path);

        WorkspaceManifest workspace{};
        workspace.path = fs::weakly_canonical(*path);
        workspace.name = RequireAttribute(*rootElement, "Name", *path);
        workspace.platformVersion = Attribute(*rootElement, "PlatformVersion").value_or("0.1.0");

        const auto *packageSourcesNode = FindChild(*rootElement, "PackageSources");
        if (packageSourcesNode == nullptr)
        {
            throw std::runtime_error(path->string() + ": missing <PackageSources>");
        }
        for (const auto *child : ChildElements(*packageSourcesNode, "PackageSource"))
        {
            workspace.packageSources.push_back((workspace.path.parent_path() / RequireAttribute(*child, "Path", *path)).lexically_normal());
        }
        if (const auto *providersNode = FindChild(*rootElement, "PackageProviders"))
        {
            for (const auto *child : ChildElements(*providersNode, "PackageProvider"))
            {
                const auto name = RequireAttribute(*child, "Name", *path);
                const auto providerRoot = (workspace.path.parent_path() / RequireAttribute(*child, "Root", *path)).lexically_normal();
                workspace.packageProviders[name] = providerRoot;
            }
        }

        const auto *projectsNode = FindChild(*rootElement, "Projects");
        if (projectsNode == nullptr)
        {
            throw std::runtime_error(path->string() + ": missing <Projects>");
        }
        for (const auto *node : ChildElements(*projectsNode, "Project"))
        {
            workspace.projects.push_back((workspace.path.parent_path() / RequireAttribute(*node, "Path", *path)).lexically_normal());
        }

        return workspace;
    }

    [[nodiscard]] auto TryLoadWorkspaceManifest(const fs::path &root) -> std::optional<WorkspaceManifest>
    {
        if (!WorkspaceFilePath(root).has_value())
        {
            return std::nullopt;
        }
        return LoadWorkspaceManifest(root);
    }

    [[nodiscard]] auto LoadPackageCatalog(
        const std::optional<WorkspaceManifest> &workspace,
        const fs::path &projectPath) -> std::unordered_map<std::string, PackageCatalogEntry>
    {
        std::unordered_map<std::string, PackageCatalogEntry> out;
        const auto packageRoots = workspace.has_value() ? workspace->packageSources : DiscoverPackageSourceRoots(projectPath);
        for (const auto &packageRoot : packageRoots)
        {
            if (!fs::exists(packageRoot))
            {
                continue;
            }
            for (const auto &entry : fs::recursive_directory_iterator(packageRoot))
            {
                if (!entry.is_regular_file() || entry.path().extension() != ".nginpkg")
                {
                    continue;
                }
                const auto manifestPath = fs::weakly_canonical(entry.path());
                const auto manifest = LoadPackageManifest(manifestPath);
                fs::path providerRoot{};
                if (workspace.has_value())
                {
                    if (const auto provider = workspace->packageProviders.find(manifest.name); provider != workspace->packageProviders.end())
                    {
                        providerRoot = provider->second;
                    }
                }
                out.emplace(manifest.name, PackageCatalogEntry{
                                               .name = manifest.name,
                                               .manifestPath = manifestPath,
                                               .providerRoot = providerRoot,
                                           });
            }
        }
        return out;
    }

    [[nodiscard]] auto LoadPackageManifest(const fs::path &path) -> PackageManifest
    {
        const auto doc = LoadXml(path);
        const auto *rootElement = doc.document.Root();
        if (rootElement == nullptr || rootElement->name != "Package")
        {
            throw std::runtime_error(path.string() + ": root element must be <Package>");
        }
        ValidateSchemaVersion(*rootElement, path);

        PackageManifest package{};
        package.path = path;
        package.name = RequireAttribute(*rootElement, "Name", path);
        package.version = RequireAttribute(*rootElement, "Version", path);
        package.compatiblePlatformRange = Attribute(*rootElement, "CompatiblePlatformRange").value_or("");

        if (const auto *artifacts = FindChild(*rootElement, "Artifacts"))
        {
            if (const auto *libraries = FindChild(*artifacts, "Libraries"))
            {
                for (const auto *node : ChildElements(*libraries, "Library"))
                {
                    LibraryArtifact artifact{};
                    artifact.name = RequireAttribute(*node, "Name", path);
                    artifact.target = Attribute(*node, "Target").value_or("");
                    artifact.linkage = Attribute(*node, "Linkage").value_or("");
                    artifact.origin = Attribute(*node, "Origin").value_or("");
                    artifact.exported = !Attribute(*node, "Exported").has_value() || BoolAttribute(*node, "Exported", true);
                    package.artifacts.libraries.push_back(std::move(artifact));
                }
            }
            if (const auto *executables = FindChild(*artifacts, "Executables"))
            {
                for (const auto *node : ChildElements(*executables, "Executable"))
                {
                    ExecutableArtifact artifact{};
                    artifact.name = RequireAttribute(*node, "Name", path);
                    artifact.target = Attribute(*node, "Target").value_or("");
                    artifact.origin = Attribute(*node, "Origin").value_or("");
                    artifact.exported = !Attribute(*node, "Exported").has_value() || BoolAttribute(*node, "Exported", true);
                    package.artifacts.executables.push_back(std::move(artifact));
                }
            }
        }

        LoadPackageBuildDescriptor(package.build, FindChild(*rootElement, "Build"), path);
        package.compatibility = ParseCompatibility(*rootElement, path);

        if (const auto *deps = FindChild(*rootElement, "Dependencies"))
        {
            for (const auto *node : ChildElements(*deps, "Dependency"))
            {
                PackageDependency dependency{};
                dependency.name = RequireAttribute(*node, "Name", path);
                dependency.versionRange = Attribute(*node, "VersionRange").value_or("");
                dependency.optional = BoolAttribute(*node, "Optional");
                package.dependencies.push_back(std::move(dependency));
            }
        }

        if (const auto *bootstrap = FindChild(*rootElement, "Bootstrap"))
        {
            PackageBootstrapDescriptor descriptor{};
            descriptor.mode = RequireAttribute(*bootstrap, "Mode", path);
            if (!IsValidPackageBootstrapMode(descriptor.mode))
            {
                throw std::runtime_error(path.string() + ": unknown package bootstrap mode '" + descriptor.mode + "'");
            }
            descriptor.entryPoint = RequireAttribute(*bootstrap, "EntryPoint", path);
            descriptor.autoApply = BoolAttribute(*bootstrap, "AutoApply");
            package.bootstrap = std::move(descriptor);
        }

        ParseContents(*rootElement, path, package.contents);

        const auto *modules = FindChild(*rootElement, "Modules");
        if (modules == nullptr)
        {
            throw std::runtime_error(path.string() + ": missing <Modules>");
        }
        for (const auto *node : ChildElements(*modules, "Module"))
        {
            package.modules.push_back(ParseModuleDefinition(*node, path));
        }

        if (const auto *plugins = FindChild(*rootElement, "Plugins"))
        {
            for (const auto *node : ChildElements(*plugins, "Plugin"))
            {
                PluginDescriptor plugin{};
                plugin.name = RequireAttribute(*node, "Name", path);
                plugin.optional = BoolAttribute(*node, "Optional");
                plugin.compatibility = ParseCompatibility(*node, path);

                if (const auto *modulesElement = FindChild(*node, "Modules"))
                {
                    if (const auto *required = FindChild(*modulesElement, "Required"))
                    {
                        for (const auto *dep : ChildElements(*required, "ModuleRef"))
                        {
                            plugin.requiredModules.push_back(RequireAttribute(*dep, "Name", path));
                        }
                    }
                    if (const auto *optional = FindChild(*modulesElement, "Optional"))
                    {
                        for (const auto *dep : ChildElements(*optional, "ModuleRef"))
                        {
                            plugin.optionalModules.push_back(RequireAttribute(*dep, "Name", path));
                        }
                    }
                }

                package.plugins.push_back(std::move(plugin));
            }
        }

        return package;
    }

    [[nodiscard]] auto LoadProjectManifest(const fs::path &path) -> ProjectManifest
    {
        const auto doc = LoadXml(path);
        const auto *rootElement = doc.document.Root();
        if (rootElement == nullptr || rootElement->name != "Project")
        {
            throw std::runtime_error(path.string() + ": root element must be <Project>");
        }
        ValidateSchemaVersion(*rootElement, path);

        ProjectManifest project{};
        project.path = path;
        project.name = RequireAttribute(*rootElement, "Name", path);
        project.type = RequireAttribute(*rootElement, "Type", path);
        project.defaultConfiguration = RequireAttribute(*rootElement, "DefaultConfiguration", path);
        if (!IsSupportedProjectType(project.type))
        {
            throw std::runtime_error(path.string() + ": unknown project type '" + project.type + "'");
        }
        if (FindChild(*rootElement, "Host") != nullptr)
        {
            throw std::runtime_error(path.string() + ": legacy <Host> is no longer supported");
        }

        if (const auto *sourceRoots = FindChild(*rootElement, "SourceRoots"))
        {
            for (const auto *node : ChildElements(*sourceRoots, "SourceRoot"))
            {
                project.sourceRoots.push_back(RequireAttribute(*node, "Path", path));
            }
        }

        const auto *output = FindChild(*rootElement, "Output");
        if (output == nullptr)
        {
            throw std::runtime_error(path.string() + ": missing <Output>");
        }
        project.output.kind = RequireAttribute(*output, "Kind", path);
        project.output.name = RequireAttribute(*output, "Name", path);
        project.output.target = RequireAttribute(*output, "Target", path);
        if (!IsSupportedOutputKind(project.output.kind))
        {
            throw std::runtime_error(path.string() + ": unknown output kind '" + project.output.kind + "'");
        }
        if (!IsValidProjectOutputPairing(project.type, project.output.kind))
        {
            throw std::runtime_error(path.string() + ": project type '" + project.type + "' is not compatible with output kind '" + project.output.kind + "'");
        }

        LoadProjectBuildDescriptor(project.build, FindChild(*rootElement, "Build"), path);

        auto parseReferences = [&](const XmlElement &referencesElement, std::vector<ProjectReference> &projectRefs, std::vector<PackageReference> &packageRefs)
        {
            for (const auto *node : ChildElements(referencesElement, "Project"))
            {
                ProjectReference reference{};
                reference.path = (path.parent_path() / RequireAttribute(*node, "Path", path)).lexically_normal();
                if (const auto configuration = Attribute(*node, "Configuration"); configuration.has_value() && !configuration->empty())
                {
                    reference.configuration = *configuration;
                }
                projectRefs.push_back(std::move(reference));
            }
            for (const auto *node : ChildElements(referencesElement, "Package"))
            {
                PackageReference packageReference{};
                packageReference.name = RequireAttribute(*node, "Name", path);
                packageReference.versionRange = Attribute(*node, "Version").value_or(Attribute(*node, "VersionRange").value_or(""));
                packageReference.optional = BoolAttribute(*node, "Optional");
                packageRefs.push_back(std::move(packageReference));
            }
        };

        if (const auto *references = FindChild(*rootElement, "References"))
        {
            parseReferences(*references, project.projectRefs, project.packageRefs);
        }

        ParseConfigSources(*rootElement, path, project.configSources);

        if (const auto *runtime = FindChild(*rootElement, "Runtime"))
        {
            ParseRuntimeDefinition(*runtime, path, project.runtime, true);
        }

        if (const auto *environments = FindChild(*rootElement, "Environments"))
        {
            for (const auto *node : ChildElements(*environments, "Environment"))
            {
                EnvironmentDefinition environment{};
                environment.name = RequireAttribute(*node, "Name", path);
                if (const auto *references = FindChild(*node, "References"))
                {
                    parseReferences(*references, environment.projectRefs, environment.packageRefs);
                }
                ParseConfigSources(*node, path, environment.configSources);
                ParseContents(*node, path, environment.contents);
                ParseVariables(*node, path, environment.variables);
                ParseFeatures(*node, path, environment.features);
                if (const auto *runtime = FindChild(*node, "Runtime"))
                {
                    ParseRuntimeDefinition(*runtime, path, environment.runtime, true);
                }
                project.environments.push_back(std::move(environment));
            }
        }

        const auto *configurationsNode = FindChild(*rootElement, "Configurations");
        if (configurationsNode == nullptr)
        {
            throw std::runtime_error(path.string() + ": missing <Configurations>");
        }
        for (const auto *node : ChildElements(*configurationsNode, "Configuration"))
        {
            ConfigurationDefinition configuration{};
            configuration.name = RequireAttribute(*node, "Name", path);
            configuration.buildConfiguration = Attribute(*node, "BuildConfiguration").value_or("Debug");
            if (!IsSupportedBuildConfiguration(configuration.buildConfiguration))
            {
                throw std::runtime_error(path.string() + ": unknown build configuration '" + configuration.buildConfiguration + "'");
            }
            if (Attribute(*node, "HostProfile").has_value())
            {
                throw std::runtime_error(path.string() + ": legacy configuration attribute 'HostProfile' is no longer supported");
            }
            if (Attribute(*node, "Platform").has_value())
            {
                throw std::runtime_error(path.string() + ": legacy configuration attribute 'Platform' is no longer supported");
            }
            if (Attribute(*node, "WorkingDirectory").has_value())
            {
                throw std::runtime_error(path.string() + ": legacy configuration attribute 'WorkingDirectory' is no longer supported");
            }
            configuration.operatingSystem = Attribute(*node, "OperatingSystem").value_or("linux");
            configuration.architecture = Attribute(*node, "Architecture").value_or("x64");
            if (!IsValidOperatingSystem(configuration.operatingSystem))
            {
                throw std::runtime_error(path.string() + ": unknown operating system '" + configuration.operatingSystem + "'");
            }
            if (!IsValidArchitecture(configuration.architecture))
            {
                throw std::runtime_error(path.string() + ": unknown architecture '" + configuration.architecture + "'");
            }
            configuration.enableReflection = BoolAttribute(*node, "EnableReflection");
            configuration.environmentName = RequireAttribute(*node, "Environment", path);

            if (const auto *launch = FindChild(*node, "Launch"))
            {
                if (const auto executable = Attribute(*launch, "Executable"); executable.has_value() && !executable->empty())
                {
                    configuration.launch.executable = *executable;
                }
                configuration.launch.workingDirectory = Attribute(*launch, "WorkingDirectory").value_or(".");
            }
            if (FindChild(*node, "EnableModules") != nullptr || FindChild(*node, "DisableModules") != nullptr
                || FindChild(*node, "EnablePlugins") != nullptr || FindChild(*node, "DisablePlugins") != nullptr)
            {
                throw std::runtime_error(path.string() + ": legacy configuration runtime sections must be nested under <Runtime>");
            }
            ParseConfigSources(*node, path, configuration.configSources);
            if (const auto *references = FindChild(*node, "References"))
            {
                parseReferences(*references, configuration.projectRefs, configuration.packageRefs);
            }
            if (const auto *runtime = FindChild(*node, "Runtime"))
            {
                ParseRuntimeDefinition(*runtime, path, configuration.runtime, true);
            }
            if (Lower(project.type) == "library" && FindChild(*node, "Launch") != nullptr)
            {
                throw std::runtime_error(path.string() + ": library projects may not declare <Launch> in configurations");
            }
            project.configurations.push_back(std::move(configuration));
        }

        std::set<std::string> knownEnvironments{};
        for (const auto &environment : project.environments)
        {
            if (!knownEnvironments.insert(environment.name).second)
            {
                throw std::runtime_error(path.string() + ": duplicate environment '" + environment.name + "'");
            }
        }
        for (const auto &configuration : project.configurations)
        {
            if (!knownEnvironments.contains(configuration.environmentName))
            {
                throw std::runtime_error(path.string() + ": configuration '" + configuration.name + "' selects unknown environment '" + configuration.environmentName + "'");
            }
        }

        return project;
    }

    [[nodiscard]] auto FindProjectFile(const fs::path &start) -> std::optional<fs::path>
    {
        auto current = fs::weakly_canonical(start);
        while (true)
        {
            std::vector<fs::path> candidates;
            if (fs::exists(current))
            {
                for (const auto &entry : fs::directory_iterator(current))
                {
                    if (entry.is_regular_file() && entry.path().extension() == ".nginproj")
                    {
                        candidates.push_back(entry.path());
                    }
                }
            }
            if (!candidates.empty())
            {
                std::sort(candidates.begin(), candidates.end());
                return candidates.front();
            }
            if (current == current.parent_path())
            {
                break;
            }
            current = current.parent_path();
        }
        return std::nullopt;
    }

    [[nodiscard]] auto ResolveProjectPath(const std::optional<std::string> &explicitPath) -> fs::path
    {
        if (explicitPath.has_value())
        {
            return fs::weakly_canonical(*explicitPath);
        }
        if (const auto discovered = FindProjectFile(fs::current_path()); discovered.has_value())
        {
            return *discovered;
        }
        throw std::runtime_error("no project manifest specified and no .nginproj file found in the current directory tree");
    }

    [[nodiscard]] auto ConfigurationByName(const ProjectManifest &project, const std::optional<std::string> &configurationName) -> const ConfigurationDefinition &
    {
        const auto desired = configurationName.value_or(project.defaultConfiguration);
        for (const auto &configuration : project.configurations)
        {
            if (configuration.name == desired)
            {
                return configuration;
            }
        }
        throw std::runtime_error("unknown configuration '" + desired + "'");
    }
}
