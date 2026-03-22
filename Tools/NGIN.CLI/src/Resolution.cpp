#include "Resolution.hpp"

#include "Authoring.hpp"
#include "Diagnostics.hpp"
#include "Support.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

namespace NGIN::CLI
{
    namespace
    {
        [[nodiscard]] auto StartupStageRank(const std::string_view value) -> int
        {
            if (value == "Foundation")
            {
                return 0;
            }
            if (value == "Platform")
            {
                return 1;
            }
            if (value == "Services")
            {
                return 2;
            }
            if (value == "Features")
            {
                return 3;
            }
            if (value == "Presentation")
            {
                return 4;
            }
            return 99;
        }

        [[nodiscard]] auto PlatformAliases(const std::string &platform) -> std::set<std::string>
        {
            const auto lower = Lower(platform);
            std::set<std::string> out{lower};
            const auto dash = lower.find('-');
            const auto primary = dash == std::string::npos ? lower : lower.substr(0, dash);
            out.insert(primary);
            if (primary.rfind("win", 0) == 0)
            {
                out.insert("windows");
            }
            if (primary == "darwin")
            {
                out.insert("macos");
            }
            return out;
        }

        [[nodiscard]] auto PlatformSupported(const std::string &targetPlatform, const std::vector<std::string> &declaredPlatforms) -> bool
        {
            const auto aliases = PlatformAliases(targetPlatform);
            for (const auto &candidate : declaredPlatforms)
            {
                if (aliases.contains(Lower(candidate)))
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] auto DefaultArtifactOrigin(const PackageManifest &manifest) -> std::string
        {
            const auto mode = Lower(manifest.build.mode);
            if (mode == "findpackage")
            {
                return "Imported";
            }
            return "Built";
        }

        [[nodiscard]] auto EffectiveArtifactOrigin(const std::string &explicitOrigin, const PackageManifest &manifest) -> std::string
        {
            if (!explicitOrigin.empty())
            {
                return explicitOrigin;
            }
            return DefaultArtifactOrigin(manifest);
        }

        [[nodiscard]] auto ParseSemver(const std::string &text) -> std::optional<std::array<int, 3>>
        {
            std::array<int, 3> parts{};
            std::stringstream ss(text.substr(0, text.find('-')));
            std::string token;
            for (int index = 0; index < 3; ++index)
            {
                if (!std::getline(ss, token, '.'))
                {
                    return std::nullopt;
                }
                if (token.empty() || !std::all_of(token.begin(), token.end(), [](unsigned char c)
                                                  { return std::isdigit(c); }))
                {
                    return std::nullopt;
                }
                parts[index] = std::stoi(token);
            }
            return parts;
        }

        [[nodiscard]] auto CompareSemver(const std::string &left, const std::string &right) -> int
        {
            const auto a = ParseSemver(left);
            const auto b = ParseSemver(right);
            if (!a.has_value() || !b.has_value())
            {
                return left.compare(right);
            }
            for (int index = 0; index < 3; ++index)
            {
                if ((*a)[index] < (*b)[index])
                {
                    return -1;
                }
                if ((*a)[index] > (*b)[index])
                {
                    return 1;
                }
            }
            return 0;
        }

        [[nodiscard]] auto VersionSatisfies(const std::string &version, const std::string &rangeText) -> bool
        {
            if (rangeText.empty())
            {
                return true;
            }
            std::stringstream stream(rangeText);
            std::string token;
            while (stream >> token)
            {
                std::string op{"="};
                std::string rhs{token};
                if (token.rfind(">=", 0) == 0 || token.rfind("<=", 0) == 0)
                {
                    op = token.substr(0, 2);
                    rhs = token.substr(2);
                }
                else if (!token.empty() && (token[0] == '>' || token[0] == '<' || token[0] == '='))
                {
                    op = token.substr(0, 1);
                    rhs = token.substr(1);
                }
                const auto cmp = CompareSemver(version, rhs);
                if (op == "=" && cmp != 0)
                {
                    return false;
                }
                if (op == ">" && cmp <= 0)
                {
                    return false;
                }
                if (op == ">=" && cmp < 0)
                {
                    return false;
                }
                if (op == "<" && cmp >= 0)
                {
                    return false;
                }
                if (op == "<=" && cmp > 0)
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] auto TopologicalDependenciesFirst(
            const std::set<std::string> &nodes,
            const std::map<std::string, std::set<std::string>> &dependencyEdges) -> std::optional<std::vector<std::string>>
        {
            std::map<std::string, int> indegree{};
            std::map<std::string, std::set<std::string>> dependents{};
            for (const auto &node : nodes)
            {
                indegree[node] = 0;
            }
            for (const auto &node : nodes)
            {
                const auto it = dependencyEdges.find(node);
                if (it == dependencyEdges.end())
                {
                    continue;
                }
                for (const auto &dep : it->second)
                {
                    if (nodes.contains(dep))
                    {
                        ++indegree[node];
                        dependents[dep].insert(node);
                    }
                }
            }

            std::vector<std::string> queue;
            for (const auto &[node, deg] : indegree)
            {
                if (deg == 0)
                {
                    queue.push_back(node);
                }
            }
            std::sort(queue.begin(), queue.end());

            std::vector<std::string> ordered;
            while (!queue.empty())
            {
                const auto current = queue.front();
                queue.erase(queue.begin());
                ordered.push_back(current);
                for (const auto &dep : dependents[current])
                {
                    --indegree[dep];
                    if (indegree[dep] == 0)
                    {
                        queue.push_back(dep);
                        std::sort(queue.begin(), queue.end());
                    }
                }
            }

            if (ordered.size() != nodes.size())
            {
                return std::nullopt;
            }
            return ordered;
        }

        [[nodiscard]] auto DetectCycles(
            const std::set<std::string> &nodes,
            const std::map<std::string, std::set<std::string>> &dependencyEdges) -> std::vector<std::string>
        {
            auto ordered = TopologicalDependenciesFirst(nodes, dependencyEdges);
            if (ordered.has_value())
            {
                return {};
            }
            return std::vector<std::string>(nodes.begin(), nodes.end());
        }

        auto MergePackageReferences(std::vector<PackageReference> &target, const std::vector<PackageReference> &source) -> void
        {
            std::unordered_map<std::string, std::size_t> indexByName;
            for (std::size_t index = 0; index < target.size(); ++index)
            {
                indexByName[target[index].name] = index;
            }
            for (const auto &reference : source)
            {
                if (const auto it = indexByName.find(reference.name); it != indexByName.end())
                {
                    target[it->second] = reference;
                    continue;
                }
                indexByName[reference.name] = target.size();
                target.push_back(reference);
            }
        }

        auto MergeStringSelection(std::set<std::string> &enabled, const std::vector<std::string> &add, const std::vector<std::string> &remove) -> void
        {
            for (const auto &name : add)
            {
                enabled.insert(name);
            }
            for (const auto &name : remove)
            {
                enabled.erase(name);
            }
        }

        auto CollectProjectClosure(
            const ProjectManifest &project,
            const ConfigurationDefinition &configuration,
            std::vector<ResolvedProjectUnit> &ordered,
            std::set<fs::path> &visiting,
            std::set<fs::path> &visited,
            DiagnosticReport &report) -> void
        {
            const auto canonicalPath = fs::weakly_canonical(project.path);
            if (visited.contains(canonicalPath))
            {
                return;
            }
            if (!visiting.insert(canonicalPath).second)
            {
                AddError(report, "project reference cycle detected at '" + canonicalPath.string() + "'");
                return;
            }

            auto collectReference = [&](const ProjectReference &reference)
            {
                const auto referencedPath = fs::weakly_canonical(reference.path);
                if (!fs::exists(referencedPath))
                {
                    AddError(report, "project reference '" + referencedPath.string() + "' does not exist");
                    return;
                }
                const auto referencedProject = LoadProjectManifest(referencedPath);
                std::optional<std::string> selectedConfiguration = reference.configuration;
                if (!selectedConfiguration.has_value())
                {
                    const auto it = std::find_if(
                        referencedProject.configurations.begin(),
                        referencedProject.configurations.end(),
                        [&](const ConfigurationDefinition &candidate)
                        { return candidate.name == configuration.name; });
                    if (it != referencedProject.configurations.end())
                    {
                        selectedConfiguration = configuration.name;
                    }
                }
                const auto &referencedConfiguration = ConfigurationByName(referencedProject, selectedConfiguration);
                CollectProjectClosure(referencedProject, referencedConfiguration, ordered, visiting, visited, report);
            };

            for (const auto &reference : project.projectRefs)
            {
                collectReference(reference);
            }
            for (const auto &reference : configuration.projectRefs)
            {
                collectReference(reference);
            }

            visiting.erase(canonicalPath);
            visited.insert(canonicalPath);
            ordered.push_back(ResolvedProjectUnit{
                .project = project,
                .configuration = configuration,
            });
        }

        [[nodiscard]] auto ResolvePackages(
            const std::optional<WorkspaceManifest> &workspace,
            const std::vector<ResolvedProjectUnit> &projectUnits,
            const std::unordered_map<std::string, PackageCatalogEntry> &catalog,
            const std::string &targetPlatform,
            DiagnosticReport &report) -> std::vector<ResolvedPackage>
        {
            std::vector<PackageReference> combinedRefs{};
            for (const auto &unit : projectUnits)
            {
                MergePackageReferences(combinedRefs, unit.project.packageRefs);
                MergePackageReferences(combinedRefs, unit.configuration.packageRefs);
            }

            std::unordered_map<std::string, ResolvedPackage> resolved;
            std::map<std::string, std::set<std::string>> edges{};
            std::vector<PackageReference> queue = combinedRefs;
            std::vector<std::string> parents(queue.size(), "");

            std::size_t index = 0;
            while (index < queue.size())
            {
                const auto ref = queue[index];
                const auto requiredBy = parents[index];
                ++index;

                const auto itCatalog = catalog.find(ref.name);
                if (itCatalog == catalog.end())
                {
                    const auto message = "package '" + ref.name + "' could not be resolved";
                    if (ref.optional)
                    {
                        AddWarning(report, message);
                    }
                    else
                    {
                        AddError(report, requiredBy.empty() ? message : message + " (required by '" + requiredBy + "')");
                    }
                    continue;
                }

                if (resolved.contains(ref.name))
                {
                    if (!requiredBy.empty())
                    {
                        edges[requiredBy].insert(ref.name);
                    }
                    continue;
                }

                auto manifest = LoadPackageManifest(fs::weakly_canonical(itCatalog->second.manifestPath));
                if (manifest.name != ref.name)
                {
                    AddError(report, "package '" + ref.name + "' resolved to manifest for '" + manifest.name + "'");
                    continue;
                }
                if (!ref.versionRange.empty() && !VersionSatisfies(manifest.version, ref.versionRange))
                {
                    const auto message = "package '" + ref.name + "' version " + manifest.version + " does not satisfy '" + ref.versionRange + "'";
                    if (ref.optional)
                    {
                        AddWarning(report, message);
                    }
                    else
                    {
                        AddError(report, message);
                    }
                    continue;
                }
                if (!manifest.platforms.empty() && !PlatformSupported(targetPlatform, manifest.platforms))
                {
                    const auto message = "package '" + ref.name + "' is not supported on platform '" + targetPlatform + "'";
                    if (ref.optional)
                    {
                        AddWarning(report, message);
                    }
                    else
                    {
                        AddError(report, message);
                    }
                    continue;
                }
                const auto platformVersion = workspace.has_value() ? workspace->platformVersion : "0.1.0";
                if (!manifest.compatiblePlatformRange.empty() && !VersionSatisfies(platformVersion, manifest.compatiblePlatformRange))
                {
                    AddError(report, "package '" + ref.name + "' compatible platform range does not include platform version '" + platformVersion + "'");
                    continue;
                }

                for (const auto &content : manifest.contents)
                {
                    const auto resolvedPath = manifest.path.parent_path() / content.source;
                    if (!fs::exists(resolvedPath))
                    {
                        AddError(report, "package '" + ref.name + "' content file '" + content.source + "' does not exist");
                    }
                }

                if (!requiredBy.empty())
                {
                    edges[requiredBy].insert(ref.name);
                }
                edges[ref.name];
                for (const auto &dep : manifest.dependencies)
                {
                    queue.push_back({dep.name, dep.versionRange, dep.optional});
                    parents.push_back(ref.name);
                    edges[ref.name].insert(dep.name);
                }

                const auto sourceDirectory = itCatalog->second.providerRoot.empty()
                                                 ? manifest.path.parent_path()
                                                 : itCatalog->second.providerRoot;
                resolved.emplace(ref.name, ResolvedPackage{
                                               .manifest = std::move(manifest),
                                               .source = itCatalog->second.providerRoot.empty() ? "manifest" : "provider",
                                               .sourceDirectory = sourceDirectory,
                                           });
            }

            if (report.HasErrors())
            {
                return {};
            }

            std::set<std::string> nodes;
            for (const auto &[name, _] : resolved)
            {
                nodes.insert(name);
            }
            if (const auto cycles = DetectCycles(nodes, edges); !cycles.empty())
            {
                AddError(report, "package graph contains dependency cycle(s)");
                return {};
            }
            const auto orderedNames = TopologicalDependenciesFirst(nodes, edges);
            if (!orderedNames.has_value())
            {
                AddError(report, "package graph could not be ordered");
                return {};
            }

            std::vector<ResolvedPackage> ordered;
            for (const auto &name : *orderedNames)
            {
                ordered.push_back(resolved.at(name));
            }
            return ordered;
        }

        auto ResolveArtifacts(
            const std::vector<ResolvedProjectUnit> &projectUnits,
            const std::vector<ResolvedPackage> &orderedPackages,
            const ProjectManifest &rootProject,
            const ConfigurationDefinition &rootConfiguration,
            DiagnosticReport &report,
            std::vector<LibraryArtifact> &librariesOut,
            std::vector<ExecutableArtifact> &executablesOut,
            std::optional<ExecutableArtifact> &selectedExecutableOut) -> void
        {
            std::unordered_map<std::string, std::string> libraryProviders;
            std::unordered_map<std::string, std::string> executableProviders;

            for (const auto &unit : projectUnits)
            {
                const auto kind = Lower(unit.project.output.kind);
                if (kind == "staticlibrary" || kind == "sharedlibrary")
                {
                    LibraryArtifact artifact{};
                    artifact.name = unit.project.output.name;
                    artifact.target = unit.project.output.target;
                    artifact.linkage = kind == "sharedlibrary" ? "Shared" : "Static";
                    artifact.origin = "Built";
                    if (const auto it = libraryProviders.find(artifact.name); it != libraryProviders.end())
                    {
                        AddError(report, "duplicate library artifact '" + artifact.name + "' in projects '" + it->second + "' and '" + unit.project.name + "'");
                        continue;
                    }
                    libraryProviders.emplace(artifact.name, unit.project.name);
                    librariesOut.push_back(std::move(artifact));
                }
                else if (kind == "executable")
                {
                    ExecutableArtifact artifact{};
                    artifact.name = unit.project.output.name;
                    artifact.target = unit.project.output.target;
                    artifact.origin = "Built";
                    if (const auto it = executableProviders.find(artifact.name); it != executableProviders.end())
                    {
                        AddError(report, "duplicate executable artifact '" + artifact.name + "' in projects '" + it->second + "' and '" + unit.project.name + "'");
                        continue;
                    }
                    executableProviders.emplace(artifact.name, unit.project.name);
                    executablesOut.push_back(std::move(artifact));
                }
            }

            for (const auto &package : orderedPackages)
            {
                for (auto artifact : package.manifest.artifacts.libraries)
                {
                    if (!artifact.exported)
                    {
                        continue;
                    }
                    artifact.origin = EffectiveArtifactOrigin(artifact.origin, package.manifest);
                    if (artifact.origin.empty())
                    {
                        AddError(report, "package '" + package.manifest.name + "' library artifact '" + artifact.name + "' does not declare an origin and it could not be inferred");
                        continue;
                    }
                    if (const auto it = libraryProviders.find(artifact.name); it != libraryProviders.end())
                    {
                        AddError(report, "duplicate library artifact '" + artifact.name + "' in '" + it->second + "' and package '" + package.manifest.name + "'");
                        continue;
                    }
                    libraryProviders.emplace(artifact.name, package.manifest.name);
                    librariesOut.push_back(std::move(artifact));
                }

                for (auto artifact : package.manifest.artifacts.executables)
                {
                    if (!artifact.exported)
                    {
                        continue;
                    }
                    artifact.origin = EffectiveArtifactOrigin(artifact.origin, package.manifest);
                    if (artifact.origin.empty())
                    {
                        AddError(report, "package '" + package.manifest.name + "' executable artifact '" + artifact.name + "' does not declare an origin and it could not be inferred");
                        continue;
                    }
                    if (const auto it = executableProviders.find(artifact.name); it != executableProviders.end())
                    {
                        AddError(report, "duplicate executable artifact '" + artifact.name + "' in '" + it->second + "' and package '" + package.manifest.name + "'");
                        continue;
                    }
                    executableProviders.emplace(artifact.name, package.manifest.name);
                    executablesOut.push_back(std::move(artifact));
                }
            }

            const auto rootKind = Lower(rootProject.output.kind);
            if (!rootConfiguration.launchExecutable.has_value() && rootKind == "executable")
            {
                for (const auto &executable : executablesOut)
                {
                    if (executable.name == rootProject.output.name)
                    {
                        selectedExecutableOut = executable;
                        return;
                    }
                }
            }

            if (!rootConfiguration.launchExecutable.has_value())
            {
                if (executablesOut.size() == 1)
                {
                    selectedExecutableOut = executablesOut.front();
                }
                else if (executablesOut.size() > 1)
                {
                    AddError(report, "configuration '" + rootConfiguration.name + "' resolves multiple executable artifacts; add <Launch Executable=\"...\" /> to select one");
                }
                return;
            }

            const auto desired = *rootConfiguration.launchExecutable;
            for (const auto &executable : executablesOut)
            {
                if (executable.name == desired)
                {
                    selectedExecutableOut = executable;
                    return;
                }
            }
            AddError(report, "configuration '" + rootConfiguration.name + "' selects executable '" + desired + "' but no project or package exposes it");
        }
    }

    [[nodiscard]] auto ResolveLaunch(
        const ProjectManifest &project,
        const ConfigurationDefinition &configuration) -> DiagnosticResult<ResolvedLaunch>
    {
        DiagnosticResult<ResolvedLaunch> result{};

        const auto workspaceRoot = RootDirFrom(project.path.parent_path());
        const auto workspace = workspaceRoot.has_value() ? TryLoadWorkspaceManifest(*workspaceRoot) : std::nullopt;
        const auto packageCatalog = LoadPackageCatalog(workspace, project.path);

        std::vector<ResolvedProjectUnit> projectUnits{};
        std::set<fs::path> visiting{};
        std::set<fs::path> visited{};
        CollectProjectClosure(project, configuration, projectUnits, visiting, visited, result.diagnostics);
        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        auto orderedPackages = ResolvePackages(workspace, projectUnits, packageCatalog, configuration.platform, result.diagnostics);
        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        std::unordered_map<std::string, std::set<std::string>> providersByModule;
        std::unordered_map<std::string, std::set<std::string>> providersByPlugin;
        std::unordered_map<std::string, ModuleDescriptor> modules;
        std::unordered_map<std::string, PluginDescriptor> plugins;

        for (const auto &unit : projectUnits)
        {
            for (const auto &module : unit.project.runtime.modules)
            {
                if (!module.platforms.empty() && !PlatformSupported(configuration.platform, module.platforms))
                {
                    AddError(result.diagnostics, "project '" + unit.project.name + "' provides module '" + module.name + "' that is not supported on platform '" + configuration.platform + "'");
                    continue;
                }
                if (const auto providerIt = providersByModule.find(module.name); providerIt != providersByModule.end() && !providerIt->second.empty())
                {
                    AddError(result.diagnostics, "duplicate module declaration for '" + module.name + "' in '" + *providerIt->second.begin() + "' and project '" + unit.project.name + "'");
                    continue;
                }
                modules.emplace(module.name, module);
                providersByModule[module.name].insert(unit.project.name);
            }
        }

        for (const auto &package : orderedPackages)
        {
            for (const auto &module : package.manifest.modules)
            {
                if (!module.platforms.empty() && !PlatformSupported(configuration.platform, module.platforms))
                {
                    AddError(result.diagnostics, "package '" + package.manifest.name + "' provides module '" + module.name + "' that is not supported on platform '" + configuration.platform + "'");
                    continue;
                }
                if (const auto providerIt = providersByModule.find(module.name); providerIt != providersByModule.end() && !providerIt->second.empty())
                {
                    AddError(result.diagnostics, "duplicate module declaration for '" + module.name + "' in '" + *providerIt->second.begin() + "' and package '" + package.manifest.name + "'");
                    continue;
                }
                modules.emplace(module.name, module);
                providersByModule[module.name].insert(package.manifest.name);
            }
            for (const auto &plugin : package.manifest.plugins)
            {
                if (!plugin.platforms.empty() && !PlatformSupported(configuration.platform, plugin.platforms))
                {
                    AddError(result.diagnostics, "package '" + package.manifest.name + "' provides plugin '" + plugin.name + "' that is not supported on platform '" + configuration.platform + "'");
                    continue;
                }
                if (const auto providerIt = providersByPlugin.find(plugin.name); providerIt != providersByPlugin.end() && !providerIt->second.empty())
                {
                    AddError(result.diagnostics, "duplicate plugin declaration for '" + plugin.name + "' in packages '" + *providerIt->second.begin() + "' and '" + package.manifest.name + "'");
                    continue;
                }
                plugins.emplace(plugin.name, plugin);
                providersByPlugin[plugin.name].insert(package.manifest.name);
            }
        }
        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        std::set<std::string> directModules{};
        for (const auto &unit : projectUnits)
        {
            MergeStringSelection(directModules, unit.project.runtime.enableModules, unit.project.runtime.disableModules);
            MergeStringSelection(directModules, unit.configuration.enableModules, unit.configuration.disableModules);
        }

        std::set<std::string> directPlugins{};
        for (const auto &[pluginName, plugin] : plugins)
        {
            if (!plugin.optional)
            {
                directPlugins.insert(pluginName);
            }
        }
        for (const auto &unit : projectUnits)
        {
            MergeStringSelection(directPlugins, unit.project.runtime.enablePlugins, unit.project.runtime.disablePlugins);
            MergeStringSelection(directPlugins, unit.configuration.enablePlugins, unit.configuration.disablePlugins);
        }

        for (const auto &module : directModules)
        {
            if (!modules.contains(module))
            {
                AddError(result.diagnostics, "configuration '" + configuration.name + "' references unknown module '" + module + "'");
                continue;
            }
            if (!providersByModule.contains(module))
            {
                AddError(result.diagnostics, "configuration '" + configuration.name + "' enables module '" + module + "' but no active project or package provides it");
            }
        }
        for (const auto &plugin : directPlugins)
        {
            if (!plugins.contains(plugin))
            {
                AddError(result.diagnostics, "configuration '" + configuration.name + "' references unknown plugin '" + plugin + "'");
                continue;
            }
            if (!providersByPlugin.contains(plugin))
            {
                AddError(result.diagnostics, "configuration '" + configuration.name + "' enables plugin '" + plugin + "' but no active package provides it");
                continue;
            }
            const auto &descriptor = plugins.at(plugin);
            for (const auto &module : descriptor.requiredModules)
            {
                if (!providersByModule.contains(module))
                {
                    AddError(result.diagnostics, "plugin '" + plugin + "' requires module '" + module + "' but no active project or package provides it");
                }
            }
        }
        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        std::set<std::string> requiredSet = directModules;
        std::set<std::string> optionalSet;
        for (const auto &plugin : directPlugins)
        {
            const auto &descriptor = plugins.at(plugin);
            for (const auto &module : descriptor.requiredModules)
            {
                requiredSet.insert(module);
            }
            for (const auto &module : descriptor.optionalModules)
            {
                if (providersByModule.contains(module) && !requiredSet.contains(module))
                {
                    optionalSet.insert(module);
                }
            }
        }

        std::vector<std::string> reqQueue(requiredSet.begin(), requiredSet.end());
        std::vector<std::string> optQueue(optionalSet.begin(), optionalSet.end());
        std::size_t reqIndex = 0;
        while (reqIndex < reqQueue.size())
        {
            const auto current = reqQueue[reqIndex++];
            const auto it = modules.find(current);
            if (it == modules.end())
            {
                AddError(result.diagnostics, "configuration '" + configuration.name + "' references unknown module '" + current + "'");
                continue;
            }
            const auto activeHostProfile = configuration.hostProfile.empty() ? "ConsoleApp" : configuration.hostProfile;
            if (!it->second.supportedHosts.empty() && std::find(it->second.supportedHosts.begin(), it->second.supportedHosts.end(), activeHostProfile) == it->second.supportedHosts.end())
            {
                AddError(result.diagnostics, "configuration '" + configuration.name + "' includes module '" + current + "' that does not support host profile '" + activeHostProfile + "'");
            }
            if (it->second.requiresReflection && !configuration.enableReflection)
            {
                AddError(result.diagnostics, "configuration '" + configuration.name + "' includes module '" + current + "' that requires reflection");
            }
            for (const auto &dep : it->second.required)
            {
                if (!providersByModule.contains(dep))
                {
                    AddError(result.diagnostics, "module '" + current + "' requires '" + dep + "' but no active project or package provides it");
                    continue;
                }
                if (!requiredSet.contains(dep))
                {
                    requiredSet.insert(dep);
                    reqQueue.push_back(dep);
                }
            }
            for (const auto &dep : it->second.optional)
            {
                if (!providersByModule.contains(dep))
                {
                    continue;
                }
                if (!requiredSet.contains(dep) && !optionalSet.contains(dep))
                {
                    optionalSet.insert(dep);
                    optQueue.push_back(dep);
                }
            }
        }
        std::size_t optIndex = 0;
        while (optIndex < optQueue.size())
        {
            const auto current = optQueue[optIndex++];
            if (requiredSet.contains(current))
            {
                continue;
            }
            const auto it = modules.find(current);
            if (it == modules.end())
            {
                continue;
            }
            for (const auto &dep : it->second.required)
            {
                if (!providersByModule.contains(dep))
                {
                    continue;
                }
                if (!requiredSet.contains(dep) && !optionalSet.contains(dep))
                {
                    optionalSet.insert(dep);
                    optQueue.push_back(dep);
                }
            }
            for (const auto &dep : it->second.optional)
            {
                if (!providersByModule.contains(dep))
                {
                    continue;
                }
                if (!requiredSet.contains(dep) && !optionalSet.contains(dep))
                {
                    optionalSet.insert(dep);
                    optQueue.push_back(dep);
                }
            }
        }

        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        std::set<std::string> allNodes = requiredSet;
        allNodes.insert(optionalSet.begin(), optionalSet.end());
        std::map<std::string, std::set<std::string>> depEdges;
        for (const auto &node : allNodes)
        {
            const auto &module = modules.at(node);
            for (const auto &dep : module.required)
            {
                if (allNodes.contains(dep))
                {
                    depEdges[node].insert(dep);
                }
            }
            for (const auto &dep : module.optional)
            {
                if (allNodes.contains(dep))
                {
                    depEdges[node].insert(dep);
                }
            }
        }
        for (const auto &[moduleName, dependencies] : depEdges)
        {
            const auto &module = modules.at(moduleName);
            const auto moduleRank = StartupStageRank(module.startupStage);
            for (const auto &dependencyName : dependencies)
            {
                const auto &dependency = modules.at(dependencyName);
                const auto dependencyRank = StartupStageRank(dependency.startupStage);
                if (moduleRank < dependencyRank)
                {
                    AddError(result.diagnostics, "module '" + moduleName + "' at startup stage '" + module.startupStage + "' depends on '" + dependencyName + "' at later startup stage '" + dependency.startupStage + "'");
                }
            }
        }
        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        std::map<std::string, int> indegree{};
        std::map<std::string, std::set<std::string>> dependents{};
        for (const auto &node : allNodes)
        {
            indegree[node] = 0;
        }
        for (const auto &node : allNodes)
        {
            const auto it = depEdges.find(node);
            if (it == depEdges.end())
            {
                continue;
            }
            for (const auto &dep : it->second)
            {
                if (allNodes.contains(dep))
                {
                    ++indegree[node];
                    dependents[dep].insert(node);
                }
            }
        }

        auto compareModuleOrder = [&](const std::string &left, const std::string &right)
        {
            const auto leftRank = StartupStageRank(modules.at(left).startupStage);
            const auto rightRank = StartupStageRank(modules.at(right).startupStage);
            if (leftRank != rightRank)
            {
                return leftRank < rightRank;
            }
            return left < right;
        };

        std::vector<std::string> queue;
        for (const auto &[node, deg] : indegree)
        {
            if (deg == 0)
            {
                queue.push_back(node);
            }
        }
        std::sort(queue.begin(), queue.end(), compareModuleOrder);

        std::vector<std::string> orderedModules{};
        while (!queue.empty())
        {
            const auto current = queue.front();
            queue.erase(queue.begin());
            orderedModules.push_back(current);
            for (const auto &dep : dependents[current])
            {
                --indegree[dep];
                if (indegree[dep] == 0)
                {
                    queue.push_back(dep);
                    std::sort(queue.begin(), queue.end(), compareModuleOrder);
                }
            }
        }
        if (orderedModules.size() != allNodes.size())
        {
            AddError(result.diagnostics, "configuration closure contains cyclic module dependencies");
            return result;
        }

        ResolvedLaunch resolved{};
        resolved.workspace = workspace;
        resolved.project = project;
        resolved.configuration = configuration;
        resolved.projectUnits = std::move(projectUnits);

        std::map<fs::path, std::string> configOwnersByDestination{};
        std::set<std::pair<std::string, std::string>> seenConfigDeclarations{};
        for (const auto &unit : resolved.projectUnits)
        {
            const auto ownerProjectDirectory = unit.project.path.parent_path();
            const auto collectConfigSources = [&](const std::vector<std::string> &configSources)
            {
                for (const auto &source : configSources)
                {
                    const auto declarationKey = std::make_pair(unit.project.name, source);
                    if (!seenConfigDeclarations.insert(declarationKey).second)
                    {
                        continue;
                    }

                    ResolvedConfigSource configSource{};
                    configSource.ownerProjectName = unit.project.name;
                    configSource.ownerProjectDirectory = ownerProjectDirectory;
                    configSource.source = source;

                    const auto declaredPath = fs::path(source);
                    configSource.stagedRelativePath = declaredPath.is_absolute() ? declaredPath.filename() : declaredPath.lexically_normal();
                    configSource.absoluteSourcePath = declaredPath.is_absolute()
                                                          ? declaredPath.lexically_normal()
                                                          : (ownerProjectDirectory / declaredPath).lexically_normal();

                    if (const auto it = configOwnersByDestination.find(configSource.stagedRelativePath); it != configOwnersByDestination.end())
                    {
                        AddError(result.diagnostics, "config source destination collision at '" + configSource.stagedRelativePath.string() + "' between projects '" + it->second + "' and '" + unit.project.name + "'");
                        continue;
                    }
                    configOwnersByDestination[configSource.stagedRelativePath] = unit.project.name;
                    resolved.configSources.push_back(std::move(configSource));
                }
            };
            collectConfigSources(unit.project.configSources);
            collectConfigSources(unit.configuration.configSources);
        }
        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        resolved.orderedPackages = std::move(orderedPackages);
        for (const auto &package : resolved.orderedPackages)
        {
            if (!package.manifest.bootstrap.has_value())
            {
                continue;
            }
            resolved.bootstraps.push_back(ResolvedBootstrap{
                .packageName = package.manifest.name,
                .mode = package.manifest.bootstrap->mode,
                .entryPoint = package.manifest.bootstrap->entryPoint,
                .autoApply = package.manifest.bootstrap->autoApply,
            });
        }
        for (const auto &package : resolved.orderedPackages)
        {
            resolved.packageEdges[package.manifest.name] = {};
            for (const auto &dep : package.manifest.dependencies)
            {
                resolved.packageEdges[package.manifest.name].insert(dep.name);
            }
        }
        resolved.enabledPlugins.assign(directPlugins.begin(), directPlugins.end());
        for (const auto &name : orderedModules)
        {
            if (requiredSet.contains(name))
            {
                resolved.requiredModules.push_back(name);
            }
            else if (optionalSet.contains(name))
            {
                resolved.optionalModules.push_back(name);
            }
        }
        resolved.dependencyEdges = std::move(depEdges);
        ResolveArtifacts(resolved.projectUnits, resolved.orderedPackages, resolved.project, resolved.configuration, result.diagnostics, resolved.libraries, resolved.executables, resolved.selectedExecutable);
        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        result.value = std::move(resolved);
        return result;
    }
}
