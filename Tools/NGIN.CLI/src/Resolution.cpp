#include "Resolution.hpp"

#include "Authoring.hpp"
#include "Diagnostics.hpp"
#include "Overlay.hpp"
#include "Support.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
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

        [[nodiscard]] auto CompatibilityMatches(
            const CompatibilityDefinition &compatibility,
            const std::string &operatingSystem,
            const std::string &architecture) -> bool
        {
            const auto matchesOperatingSystem =
                compatibility.operatingSystems.empty()
                || std::find(compatibility.operatingSystems.begin(), compatibility.operatingSystems.end(), operatingSystem) != compatibility.operatingSystems.end();
            const auto matchesArchitecture =
                compatibility.architectures.empty()
                || std::find(compatibility.architectures.begin(), compatibility.architectures.end(), architecture) != compatibility.architectures.end();
            return matchesOperatingSystem && matchesArchitecture;
        }

        [[nodiscard]] auto FindEnvironment(
            const ProjectManifest &project,
            const std::string &name) -> const EnvironmentDefinition *
        {
            if (name.empty())
            {
                return nullptr;
            }
            for (const auto &environment : project.environments)
            {
                if (environment.name == name)
                {
                    return &environment;
                }
            }
            return nullptr;
        }

        [[nodiscard]] auto InputDefaultTarget(const InputDeclaration &input, const std::string &source) -> fs::path
        {
            if (!input.target.empty())
            {
                return fs::path(input.target).lexically_normal();
            }
            const auto sourcePath = fs::path(source);
            if (!input.targetRoot.empty())
            {
                auto preserved = sourcePath.filename();
                const auto sourceRelative = sourcePath.lexically_normal();
                if (input.mode == "Directory" && !input.path.empty())
                {
                    const auto base = fs::path(input.path).lexically_normal();
                    const auto relative = sourceRelative.lexically_relative(base);
                    if (!relative.empty() && relative.native().find("..") != 0)
                    {
                        preserved = relative;
                    }
                }
                else if (!input.basePath.empty())
                {
                    const auto base = fs::path(input.basePath).lexically_normal();
                    const auto relative = sourceRelative.lexically_relative(base);
                    if (!relative.empty() && relative.native().find("..") != 0)
                    {
                        preserved = relative;
                    }
                }
                return (fs::path(input.targetRoot) / preserved).lexically_normal();
            }
            return sourcePath.is_absolute() ? sourcePath.filename() : sourcePath.lexically_normal();
        }

        [[nodiscard]] auto InputIsStaged(const InputDeclaration &input) -> bool
        {
            return input.kind == "Config" || input.kind == "Content" || input.kind == "Asset"
                   || (input.kind == "Generated" && (input.role == "Content" || input.role == "Asset" || !input.target.empty() || !input.targetRoot.empty()));
        }

        [[nodiscard]] auto ResolvedInputKind(const InputDeclaration &input) -> std::string
        {
            if (input.kind == "Config")
            {
                return "config-input";
            }
            if (input.kind == "Asset")
            {
                return "asset";
            }
            if (input.kind == "Content")
            {
                return input.contentKind.empty() ? "content" : input.contentKind;
            }
            if (input.kind == "Generated")
            {
                if (input.role == "Asset")
                {
                    return "asset";
                }
                if (input.role == "Content")
                {
                    return input.contentKind.empty() ? "content" : input.contentKind;
                }
                if (input.role == "ToolInput")
                {
                    return "tool-input";
                }
                return "generated";
            }
            if (input.kind == "ToolInput")
            {
                return "tool-input";
            }
            return Lower(input.kind);
        }

        [[nodiscard]] auto SelectionProvenance(const ContributionProvenance &provenance,
                                               const std::string &ownerKind,
                                               const std::string &ownerName,
                                               const fs::path &manifestPath,
                                               const SelectorSet &selectors,
                                               std::string reason) -> ContributionProvenance
        {
            if (!provenance.sourceKind.empty())
            {
                auto selected = provenance;
                if (selected.reason.empty())
                {
                    selected.reason = std::move(reason);
                }
                return selected;
            }
            if (ownerKind == "project" && selectors.profile.has_value())
            {
                return ContributionProvenance{
                    .sourceKind = "project-profile",
                    .sourceName = *selectors.profile,
                    .manifestPath = manifestPath,
                    .reason = std::move(reason),
                };
            }
            return ContributionProvenance{
                .sourceKind = ownerKind,
                .sourceName = ownerName,
                .manifestPath = manifestPath,
                .reason = std::move(reason),
            };
        }

        auto ExpandInputSources(
            const InputDeclaration &input,
            const fs::path &ownerDirectory,
            std::vector<std::pair<std::string, fs::path>> &out) -> void
        {
            if (input.mode == "File")
            {
                const auto declared = fs::path(input.path);
                out.emplace_back(input.path,
                                 declared.is_absolute() ? declared.lexically_normal()
                                                        : (ownerDirectory / declared).lexically_normal());
                return;
            }

            if (input.mode == "Directory")
            {
                const auto declared = fs::path(input.path);
                const auto root = declared.is_absolute() ? declared.lexically_normal()
                                                         : (ownerDirectory / declared).lexically_normal();
                if (!InputIsStaged(input) && input.kind != "ToolInput")
                {
                    out.emplace_back(input.path, root);
                    return;
                }
                if (!fs::exists(root) || !fs::is_directory(root))
                {
                    return;
                }
                std::vector<std::pair<std::string, fs::path>> entries{};
                for (const auto &entry : fs::recursive_directory_iterator(root))
                {
                    if (!entry.is_regular_file())
                    {
                        continue;
                    }
                    const auto relative = entry.path().lexically_relative(root);
                    if (!input.includePatterns.empty() && !AnyGlobMatches(input.includePatterns, relative))
                    {
                        continue;
                    }
                    if (!input.excludePatterns.empty() && AnyGlobMatches(input.excludePatterns, relative))
                    {
                        continue;
                    }
                    entries.emplace_back((fs::path(input.path) / relative).generic_string(), entry.path().lexically_normal());
                }
                std::sort(entries.begin(), entries.end(), [](const auto &left, const auto &right) { return left.first < right.first; });
                out.insert(out.end(), entries.begin(), entries.end());
                return;
            }

            if (input.mode != "Glob")
            {
                return;
            }
            auto globRoot = ownerDirectory;
            if (!input.basePath.empty())
            {
                const fs::path base{input.basePath};
                globRoot = (base.is_absolute() ? base : ownerDirectory / base).lexically_normal();
            }
            if (!fs::exists(globRoot) || !fs::is_directory(globRoot))
            {
                return;
            }
            std::vector<std::pair<std::string, fs::path>> entries{};
            for (const auto &entry : fs::recursive_directory_iterator(globRoot))
            {
                if (!entry.is_regular_file())
                {
                    continue;
                }
                const auto relative = entry.path().lexically_relative(globRoot);
                if (AnyGlobMatches(input.includePatterns, relative) &&
                    (input.excludePatterns.empty() || !AnyGlobMatches(input.excludePatterns, relative)))
                {
                    const auto declared = input.basePath.empty() ? relative : fs::path(input.basePath) / relative;
                    entries.emplace_back(declared.generic_string(), entry.path().lexically_normal());
                }
            }
            std::sort(entries.begin(), entries.end(), [](const auto &left, const auto &right) { return left.first < right.first; });
            out.insert(out.end(), entries.begin(), entries.end());
        }

        struct LoadedSetting
        {
            std::string value{};
            bool secret{false};
            std::string source{};
        };

        [[nodiscard]] auto UserGlobalSettingsPath() -> std::optional<fs::path>
        {
            if (const auto *home = std::getenv("HOME"); home != nullptr && std::string_view(home).size() > 0)
            {
                return fs::path(home) / ".ngin/settings.nginsettings";
            }
#ifdef _WIN32
            if (const auto *profile = std::getenv("USERPROFILE"); profile != nullptr && std::string_view(profile).size() > 0)
            {
                return fs::path(profile) / ".ngin/settings.nginsettings";
            }
#endif
            return std::nullopt;
        }

        auto MergeLocalSettingsFile(
            const fs::path &settingsPath,
            const std::string &sourceLabel,
            std::unordered_map<std::string, LoadedSetting> &settings,
            DiagnosticReport &report) -> void
        {
            try
            {
                const auto manifest = LoadLocalSettingsManifest(settingsPath);
                for (const auto &setting : manifest.settings)
                {
                    settings[setting.key] = LoadedSetting{
                        .value = setting.value,
                        .secret = setting.secret,
                        .source = sourceLabel,
                    };
                }
            }
            catch (const std::exception &ex)
            {
                AddError(report, ex.what());
            }
        }

        [[nodiscard]] auto ShellQuote(const fs::path &path) -> std::string
        {
            const auto text = path.string();
            std::string quoted{"'"};
            for (const char ch : text)
            {
                if (ch == '\'')
                {
                    quoted += "'\\''";
                }
                else
                {
                    quoted.push_back(ch);
                }
            }
            quoted.push_back('\'');
            return quoted;
        }

        [[nodiscard]] auto IsTrackedByGit(const fs::path &workspaceRoot, const fs::path &path) -> bool
        {
#ifdef _WIN32
            (void)workspaceRoot;
            (void)path;
            return false;
#else
            if (!fs::exists(workspaceRoot / ".git"))
            {
                return false;
            }
            const auto relative = fs::relative(path, workspaceRoot);
            const auto command = "git -C " + ShellQuote(workspaceRoot)
                                 + " ls-files --error-unmatch -- "
                                 + ShellQuote(relative) + " >/dev/null 2>&1";
            return std::system(command.c_str()) == 0;
#endif
        }

        auto WarnForTrackedLocalSettings(
            const fs::path &settingsPath,
            const std::optional<fs::path> &workspaceRoot,
            std::set<fs::path> &warnedTrackedSettings,
            DiagnosticReport &report) -> void
        {
            if (!workspaceRoot.has_value())
            {
                return;
            }
            const auto localRoot = (*workspaceRoot / ".ngin/local").lexically_normal();
            const auto normalized = NormalizePath(settingsPath);
            if (!IsPathWithinDirectory(normalized, localRoot))
            {
                return;
            }
            if (!warnedTrackedSettings.insert(normalized).second)
            {
                return;
            }
            if (IsTrackedByGit(*workspaceRoot, normalized))
            {
                AddWarning(report, "repository-local settings file '" + normalized.string() + "' is tracked by git; local settings under .ngin/local should be ignored");
            }
        }

        [[nodiscard]] auto LoadImportedSettings(
            const ProjectManifest &project,
            const std::set<std::string> &requestedLocalSettingKeys,
            const std::optional<fs::path> &workspaceRoot,
            std::set<fs::path> &warnedTrackedSettings,
            DiagnosticReport &report) -> std::unordered_map<std::string, LoadedSetting>
        {
            std::unordered_map<std::string, LoadedSetting> settings{};
            for (const auto &import : project.localSettingsImports)
            {
                const auto declaredPath = fs::path(import.path);
                const auto settingsPath = declaredPath.is_absolute()
                                              ? declaredPath.lexically_normal()
                                              : (project.path.parent_path() / declaredPath).lexically_normal();
                if (!fs::exists(settingsPath))
                {
                    if (!import.optional)
                    {
                        AddError(report, "local settings import '" + settingsPath.string() + "' does not exist");
                    }
                    continue;
                }
                WarnForTrackedLocalSettings(settingsPath, workspaceRoot, warnedTrackedSettings, report);
                MergeLocalSettingsFile(settingsPath, "local setting file " + settingsPath.string(), settings, report);
            }

            const auto needsUserGlobal = std::any_of(
                requestedLocalSettingKeys.begin(),
                requestedLocalSettingKeys.end(),
                [&](const std::string &key)
                { return !settings.contains(key); });
            if (needsUserGlobal)
            {
                if (const auto userSettingsPath = UserGlobalSettingsPath(); userSettingsPath.has_value() && fs::exists(*userSettingsPath))
                {
                    std::unordered_map<std::string, LoadedSetting> userSettings{};
                    MergeLocalSettingsFile(*userSettingsPath, "user-global settings " + userSettingsPath->string(), userSettings, report);
                    for (const auto &[key, setting] : userSettings)
                    {
                        settings.try_emplace(key, setting);
                    }
                }
            }

            return settings;
        }

        [[nodiscard]] auto ResolveVariable(
            EnvironmentVariable variable,
            const std::unordered_map<std::string, LoadedSetting> &settings,
            DiagnosticReport &report) -> EnvironmentVariable
        {
            if (!variable.fromEnvironment.empty())
            {
                if (const auto *value = std::getenv(variable.fromEnvironment.c_str()); value != nullptr)
                {
                    variable.value = value;
                    variable.resolved = true;
                }
                variable.resolvedSource = "environment " + variable.fromEnvironment;
            }
            else if (!variable.fromLocalSetting.empty())
            {
                if (const auto it = settings.find(variable.fromLocalSetting); it != settings.end())
                {
                    variable.value = it->second.value;
                    variable.secret = variable.secret || it->second.secret;
                    variable.resolved = true;
                    variable.resolvedSource = it->second.source + " key " + variable.fromLocalSetting;
                }
                else
                {
                    variable.resolvedSource = "local setting " + variable.fromLocalSetting;
                }
            }
            else
            {
                variable.resolved = true;
                variable.resolvedSource = "project Value";
            }

            if (variable.required && !variable.resolved)
            {
                AddError(report, std::string("missing required ")
                                     + (variable.secret ? "secret " : "")
                                     + "variable '" + variable.name + "'");
            }
            return variable;
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
            if ((rangeText.front() == '[' || rangeText.front() == '(')
                && (rangeText.back() == ']' || rangeText.back() == ')'))
            {
                const auto lowerInclusive = rangeText.front() == '[';
                const auto upperInclusive = rangeText.back() == ']';
                const auto inner = rangeText.substr(1, rangeText.size() - 2);
                const auto comma = inner.find(',');
                if (comma == std::string::npos)
                {
                    return lowerInclusive && upperInclusive && CompareSemver(version, inner) == 0;
                }
                const auto lower = inner.substr(0, comma);
                const auto upper = inner.substr(comma + 1);
                if (!lower.empty())
                {
                    const auto cmp = CompareSemver(version, lower);
                    if (lowerInclusive ? cmp < 0 : cmp <= 0)
                    {
                        return false;
                    }
                }
                if (!upper.empty())
                {
                    const auto cmp = CompareSemver(version, upper);
                    if (upperInclusive ? cmp > 0 : cmp >= 0)
                    {
                        return false;
                    }
                }
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

        [[nodiscard]] auto EffectiveDependencyVersions(
            const std::optional<WorkspaceManifest> &workspace,
            const std::vector<ResolvedProjectUnit> &projectUnits) -> std::unordered_map<std::string, std::string>
        {
            std::unordered_map<std::string, std::string> versions{};
            if (workspace.has_value())
            {
                versions = workspace->dependencyVersions;
            }
            for (const auto &unit : projectUnits)
            {
                for (const auto &[name, range] : unit.project.dependencyVersions)
                {
                    versions[name] = range;
                }
            }
            return versions;
        }

        [[nodiscard]] auto WithDependencyPolicy(PackageReference reference, const std::unordered_map<std::string, std::string> &versions) -> PackageReference
        {
            if (reference.versionRange.empty())
            {
                if (const auto it = versions.find(reference.name); it != versions.end())
                {
                    reference.versionRange = it->second;
                }
            }
            return reference;
        }

        struct PackageResolutionResult
        {
            std::vector<ResolvedPackage> orderedPackages{};
            std::vector<SelectedPackageFeature> selectedFeatures{};
            std::vector<ResolvedCapabilityProvider> capabilityProviders{};
            std::map<std::string, std::set<std::string>> edges{};
            std::map<std::string, std::string> scopes{};
        };

        auto CollectProjectClosure(
            const ProjectManifest &project,
            const ProfileDefinition &profile,
            const std::optional<WorkspaceManifest> &workspace,
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
                auto referencedProject = LoadProjectManifest(referencedPath);
                referencedProject = ProjectWithWorkspacePolicy(std::move(referencedProject), workspace);
                std::optional<std::string> selectedProfile = reference.profile;
                if (!selectedProfile.has_value())
                {
                    const auto it = std::find_if(
                        referencedProject.profiles.begin(),
                        referencedProject.profiles.end(),
                        [&](const ProfileDefinition &candidate)
                        { return candidate.name == profile.name; });
                    if (it != referencedProject.profiles.end())
                    {
                        selectedProfile = profile.name;
                    }
                }
                const auto referencedProfile = ProfileWithWorkspacePolicy(referencedProject, workspace, selectedProfile);
                CollectProjectClosure(referencedProject, referencedProfile, workspace, ordered, visiting, visited, report);
            };

            const auto selectedReferences = EffectiveProjectReferences(project, profile);

            for (const auto &reference : selectedReferences)
            {
                collectReference(reference);
            }

            visiting.erase(canonicalPath);
            visited.insert(canonicalPath);
            ordered.push_back(ResolvedProjectUnit{
                .project = project,
                .profile = profile,
                .environment = [&]() -> std::optional<EnvironmentDefinition>
                {
                    if (const auto *environment = FindEnvironment(project, profile.environmentName))
                    {
                        return *environment;
                    }
                    return std::nullopt;
                }(),
            });
        }

        [[nodiscard]] auto ResolvePackages(
            const std::optional<WorkspaceManifest> &workspace,
            const std::vector<ResolvedProjectUnit> &projectUnits,
            const std::unordered_map<std::string, PackageCatalogEntry> &catalog,
            const std::string &targetOperatingSystem,
            const std::string &targetArchitecture,
            const ProfileDefinition &rootProfile,
            DiagnosticReport &report) -> PackageResolutionResult
        {
            PackageResolutionResult result{};
            const auto dependencyVersions = EffectiveDependencyVersions(workspace, projectUnits);

            std::vector<PackageReference> combinedRefs{};
            std::vector<PackageFeatureUse> requestedFeatures{};
            for (const auto &unit : projectUnits)
            {
                MergePackageReferences(combinedRefs, EffectivePackageReferences(unit.project, unit.profile, report), report);
                const auto selectedFeatures = EffectivePackageFeatureUses(unit.project, unit.profile);
                requestedFeatures.insert(requestedFeatures.end(), selectedFeatures.begin(), selectedFeatures.end());
            }
            if (report.HasErrors())
            {
                return result;
            }
            std::vector<PackageReference> featureRefs{};
            featureRefs.reserve(requestedFeatures.size());
            for (const auto &use : requestedFeatures)
            {
                featureRefs.push_back(PackageReference{
                    .name = use.packageName,
                    .versionRange = use.versionRange,
                    .optional = false,
                    .selectors = use.selectors,
                });
            }
            MergePackageReferences(combinedRefs, featureRefs, report);
            if (report.HasErrors())
            {
                return result;
            }

            std::unordered_map<std::string, ResolvedPackage> resolved;
            std::set<std::string> selectedFeatureKeys{};
            std::vector<PackageReference> queue{};
            queue.reserve(combinedRefs.size());
            for (const auto &reference : combinedRefs)
            {
                queue.push_back(WithDependencyPolicy(reference, dependencyVersions));
            }
            std::vector<std::string> parents(queue.size(), "");

            std::size_t index = 0;
            while (index < queue.size())
            {
                const auto ref = WithDependencyPolicy(queue[index], dependencyVersions);
                const auto requiredBy = parents[index];
                ++index;
                MergeScopeList(result.scopes[ref.name], ref.scope);
                if (ref.versionRange.empty())
                {
                    AddError(report,
                             "package reference '" + ref.name + "' must declare VersionRange or be covered by DependencyPolicy" +
                                 (requiredBy.empty() ? std::string{} : " (required by '" + requiredBy + "')"));
                    continue;
                }

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
                    const auto &existing = resolved.at(ref.name);
                    if (!VersionSatisfies(existing.manifest.version, ref.versionRange))
                    {
                        const auto message = "package '" + ref.name + "' resolved version "
                                             + existing.manifest.version + " does not satisfy later range '"
                                             + ref.versionRange + "'";
                        if (ref.optional)
                        {
                            AddWarning(report, message);
                        }
                        else
                        {
                            AddError(report, requiredBy.empty() ? message : message + " (required by '" + requiredBy + "')");
                        }
                    }
                    if (!requiredBy.empty())
                    {
                        result.edges[requiredBy].insert(ref.name);
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
                if (!CompatibilityMatches(manifest.compatibility, targetOperatingSystem, targetArchitecture))
                {
                    const auto message = "package '" + ref.name + "' is not supported on operating system '" + targetOperatingSystem + "' and architecture '" + targetArchitecture + "'";
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

                for (const auto &input : manifest.inputs)
                {
                    if (input.path.empty())
                    {
                        continue;
                    }
                    const auto resolvedPath = fs::path(input.path).is_absolute()
                                                  ? fs::path(input.path).lexically_normal()
                                                  : (manifest.path.parent_path() / input.path).lexically_normal();
                    if (input.required && !fs::exists(resolvedPath))
                    {
                        AddError(report, "package '" + ref.name + "' input file '" + input.path + "' does not exist");
                    }
                }

                if (!requiredBy.empty())
                {
                    result.edges[requiredBy].insert(ref.name);
                }
                result.edges[ref.name];
                for (const auto &dep : manifest.dependencies)
                {
                    queue.push_back(WithDependencyPolicy(PackageReference{
                                                             .name = dep.name,
                                                             .versionRange = dep.versionRange,
                                                             .optional = dep.optional,
                                                             .scope = dep.scope,
                                                         },
                                                         dependencyVersions));
                    parents.push_back(ref.name);
                    result.edges[ref.name].insert(dep.name);
                }

                for (const auto &use : requestedFeatures)
                {
                    if (use.packageName != ref.name)
                    {
                        continue;
                    }
                    const auto featureIt = std::find_if(
                        manifest.features.begin(), manifest.features.end(),
                        [&](const PackageManifest::Feature &feature)
                        {
                            return feature.name == use.featureName;
                        });
                    if (featureIt == manifest.features.end())
                    {
                        AddError(report, "package '" + ref.name + "' does not declare feature '" + use.featureName + "'");
                        continue;
                    }
                    if (!SelectionMatches(manifest.conditions, featureIt->selectors, rootProfile))
                    {
                        continue;
                    }
                    const auto featureKey = ref.name + "::" + featureIt->name;
                    if (!selectedFeatureKeys.insert(featureKey).second)
                    {
                        continue;
                    }
                    for (const auto &dep : featureIt->packageRefs)
                    {
                        if (!SelectionMatches(manifest.conditions, dep.selectors, rootProfile))
                        {
                            continue;
                        }
                        queue.push_back(WithDependencyPolicy(dep, dependencyVersions));
                        parents.push_back(ref.name);
                        result.edges[ref.name].insert(dep.name);
                    }
                    SelectedPackageFeature selected{};
                    selected.packageName = manifest.name;
                    selected.packageVersion = manifest.version;
                    selected.manifestPath = manifest.path;
                    selected.providerRoot = itCatalog->second.providerRoot;
                    selected.featureName = featureIt->name;
                    selected.description = featureIt->description;
                    selected.selectors = featureIt->selectors;
                    selected.provides = featureIt->provides;
                    selected.requiredCapabilities = featureIt->requiredCapabilities;
                    selected.packageRefs = featureIt->packageRefs;
                    selected.inputs = featureIt->inputs;
                    selected.build = featureIt->build;
                    selected.runtime = featureIt->runtime;
                    selected.variables = featureIt->variables;
                    selected.generators = featureIt->generators;
                    selected.quality = featureIt->quality;
                    result.selectedFeatures.push_back(std::move(selected));
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
                return result;
            }

            std::set<std::string> nodes;
            for (const auto &[name, _] : resolved)
            {
                nodes.insert(name);
            }
            if (const auto cycles = DetectCycles(nodes, result.edges); !cycles.empty())
            {
                AddError(report, "package graph contains dependency cycle(s)");
                return result;
            }
            const auto orderedNames = TopologicalDependenciesFirst(nodes, result.edges);
            if (!orderedNames.has_value())
            {
                AddError(report, "package graph could not be ordered");
                return result;
            }

            for (const auto &name : *orderedNames)
            {
                result.orderedPackages.push_back(resolved.at(name));
            }
            std::unordered_map<std::string, std::vector<ResolvedCapabilityProvider>> providersByCapability{};
            for (const auto &feature : result.selectedFeatures)
            {
                for (const auto &provided : feature.provides)
                {
                    ResolvedCapabilityProvider provider{};
                    provider.capability = provided.name;
                    provider.packageName = feature.packageName;
                    provider.featureName = feature.featureName;
                    provider.exclusive = provided.exclusive;
                    providersByCapability[provided.name].push_back(provider);
                    result.capabilityProviders.push_back(std::move(provider));
                }
            }
            for (const auto &feature : result.selectedFeatures)
            {
                for (const auto &required : feature.requiredCapabilities)
                {
                    if (!providersByCapability.contains(required.name))
                    {
                        AddError(report, "package feature '" + feature.packageName + "::" + feature.featureName + "' requires capability '" + required.name + "' but no selected feature provides it");
                    }
                }
            }
            for (const auto &[capability, providers] : providersByCapability)
            {
                const auto exclusiveCount = std::count_if(
                    providers.begin(), providers.end(),
                    [](const ResolvedCapabilityProvider &provider)
                    {
                        return provider.exclusive;
                    });
                if (exclusiveCount > 1 || (exclusiveCount == 1 && providers.size() > 1))
                {
                    AddError(report, "exclusive capability '" + capability + "' is provided by multiple selected package features");
                }
            }
            return result;
        }

        auto ResolveArtifacts(
            const std::vector<ResolvedProjectUnit> &projectUnits,
            const std::vector<ResolvedPackage> &orderedPackages,
            const ProjectManifest &rootProject,
            const ProfileDefinition &rootProfile,
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
                    artifact.linkage = unit.project.productKind == "External" ? "Interface" : (kind == "sharedlibrary" ? "Shared" : "Static");
                    artifact.origin = unit.project.productKind == "External" ? "Prebuilt" : "Built";
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
            if (!rootProfile.launch.executable.has_value() && rootKind == "executable")
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

            if (!rootProfile.launch.executable.has_value())
            {
                if (executablesOut.size() == 1)
                {
                    selectedExecutableOut = executablesOut.front();
                }
                else if (executablesOut.size() > 1)
                {
                    AddError(report, "profile '" + rootProfile.name + "' resolves multiple executable artifacts; add <Launch Executable=\"...\" /> to select one");
                }
                return;
            }

            const auto desired = *rootProfile.launch.executable;
            for (const auto &executable : executablesOut)
            {
                if (executable.name == desired)
                {
                    selectedExecutableOut = executable;
                    return;
                }
            }
            AddError(report, "profile '" + rootProfile.name + "' selects executable '" + desired + "' but no project or package exposes it");
        }
    }

    [[nodiscard]] auto ResolveLaunch(
        const ProjectManifest &project,
        const ProfileDefinition &profile) -> DiagnosticResult<ResolvedLaunch>
    {
        DiagnosticResult<ResolvedLaunch> result{};

        const auto workspaceRoot = RootDirFrom(project.path.parent_path());
        const auto workspace = workspaceRoot.has_value() ? TryLoadWorkspaceManifest(*workspaceRoot) : std::nullopt;
        const auto effectiveProject = ProjectWithWorkspacePolicy(project, workspace);
        auto effectiveProfile = ProfileWithWorkspacePolicy(effectiveProject, workspace, profile.name);
        if (!profile.launch.name.empty())
        {
            effectiveProfile.launch = profile.launch;
        }
        const auto packageCatalog = LoadPackageCatalog(workspace, project.path);
        std::set<fs::path> warnedTrackedSettings{};

        std::vector<ResolvedProjectUnit> projectUnits{};
        std::set<fs::path> visiting{};
        std::set<fs::path> visited{};
        CollectProjectClosure(effectiveProject, effectiveProfile, workspace, projectUnits, visiting, visited, result.diagnostics);
        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        for (const auto &unit : projectUnits)
        {
            if (unit.profile.operatingSystem != effectiveProfile.operatingSystem || unit.profile.architecture != effectiveProfile.architecture)
            {
                AddError(
                    result.diagnostics,
                    "project '" + unit.project.name + "' profile '" + unit.profile.name
                        + "' targets '" + unit.profile.operatingSystem + "/" + unit.profile.architecture
                        + "' which does not match root target '" + effectiveProfile.operatingSystem + "/" + effectiveProfile.architecture + "'");
            }
        }
        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        auto packageResolution = ResolvePackages(workspace, projectUnits, packageCatalog, effectiveProfile.operatingSystem, effectiveProfile.architecture, effectiveProfile, result.diagnostics);
        if (result.diagnostics.HasErrors())
        {
            return result;
        }
        auto &orderedPackages = packageResolution.orderedPackages;

        std::unordered_map<std::string, std::set<std::string>> providersByModule;
        std::unordered_map<std::string, std::set<std::string>> providersByPlugin;
        std::unordered_map<std::string, ModuleDescriptor> modules;
        std::unordered_map<std::string, PluginDescriptor> plugins;

        for (const auto &unit : projectUnits)
        {
            for (const auto &module : unit.project.runtime.modules)
            {
                if (!SelectionMatches(unit.project, module.selectors, unit.profile))
                {
                    continue;
                }
                if (!CompatibilityMatches(module.compatibility, effectiveProfile.operatingSystem, effectiveProfile.architecture))
                {
                    AddError(result.diagnostics, "project '" + unit.project.name + "' provides module '" + module.name + "' that is not supported on '" + effectiveProfile.operatingSystem + "/" + effectiveProfile.architecture + "'");
                    continue;
                }
                if (const auto providerIt = providersByModule.find(module.name); providerIt != providersByModule.end() && !providerIt->second.empty())
                {
                    AddError(result.diagnostics, "duplicate module declaration for '" + module.name + "' in '" + *providerIt->second.begin() + "' and project '" + unit.project.name + "'");
                    continue;
                }
                auto selectedModule = module;
                selectedModule.provenance =
                    SelectionProvenance(module.provenance, "project", unit.project.name, unit.project.path,
                                        module.selectors, "project runtime module declaration");
                modules.emplace(selectedModule.name, std::move(selectedModule));
                providersByModule[module.name].insert(unit.project.name);
            }
            if (unit.environment.has_value())
            {
                for (const auto &module : unit.environment->runtime.modules)
                {
                    if (!SelectionMatches(unit.project, module.selectors, unit.profile))
                    {
                        continue;
                    }
                    if (!CompatibilityMatches(module.compatibility, effectiveProfile.operatingSystem, effectiveProfile.architecture))
                    {
                        AddError(result.diagnostics, "environment '" + unit.environment->name + "' in project '" + unit.project.name + "' provides module '" + module.name + "' that is not supported on '" + effectiveProfile.operatingSystem + "/" + effectiveProfile.architecture + "'");
                        continue;
                    }
                    if (const auto providerIt = providersByModule.find(module.name); providerIt != providersByModule.end() && !providerIt->second.empty())
                    {
                        AddError(result.diagnostics, "duplicate module declaration for '" + module.name + "' in '" + *providerIt->second.begin() + "' and environment '" + unit.environment->name + "'");
                        continue;
                    }
                    auto selectedModule = module;
                    selectedModule.provenance =
                        SelectionProvenance(module.provenance, "project", unit.project.name, unit.project.path,
                                            module.selectors, "environment runtime module declaration");
                    modules.emplace(selectedModule.name, std::move(selectedModule));
                    providersByModule[module.name].insert(unit.project.name + ":" + unit.environment->name);
                }
            }
            for (const auto &module : unit.profile.runtime.modules)
            {
                if (!SelectionMatches(unit.project, module.selectors, unit.profile))
                {
                    continue;
                }
                if (!CompatibilityMatches(module.compatibility, effectiveProfile.operatingSystem, effectiveProfile.architecture))
                {
                    AddError(result.diagnostics, "profile '" + unit.profile.name + "' in project '" + unit.project.name + "' provides module '" + module.name + "' that is not supported on '" + effectiveProfile.operatingSystem + "/" + effectiveProfile.architecture + "'");
                    continue;
                }
                if (const auto providerIt = providersByModule.find(module.name); providerIt != providersByModule.end() && !providerIt->second.empty())
                {
                    AddError(result.diagnostics, "duplicate module declaration for '" + module.name + "' in '" + *providerIt->second.begin() + "' and profile '" + unit.profile.name + "'");
                    continue;
                }
                auto selectedModule = module;
                selectedModule.provenance =
                    SelectionProvenance(module.provenance, "project", unit.project.name, unit.project.path,
                                        module.selectors, "project profile runtime module declaration");
                modules.emplace(selectedModule.name, std::move(selectedModule));
                providersByModule[module.name].insert(unit.project.name + ":" + unit.profile.name);
            }
        }

        for (const auto &package : orderedPackages)
        {
            for (const auto &module : package.manifest.modules)
            {
                if (!SelectionMatches(package.manifest.conditions, module.selectors, effectiveProfile))
                {
                    continue;
                }
                if (!CompatibilityMatches(module.compatibility, effectiveProfile.operatingSystem, effectiveProfile.architecture))
                {
                    AddError(result.diagnostics, "package '" + package.manifest.name + "' provides module '" + module.name + "' that is not supported on '" + effectiveProfile.operatingSystem + "/" + effectiveProfile.architecture + "'");
                    continue;
                }
                if (const auto providerIt = providersByModule.find(module.name); providerIt != providersByModule.end() && !providerIt->second.empty())
                {
                    AddError(result.diagnostics, "duplicate module declaration for '" + module.name + "' in '" + *providerIt->second.begin() + "' and package '" + package.manifest.name + "'");
                    continue;
                }
                auto selectedModule = module;
                selectedModule.provenance =
                    SelectionProvenance(module.provenance, "package", package.manifest.name, package.manifest.path,
                                        module.selectors, "package runtime module declaration");
                modules.emplace(selectedModule.name, std::move(selectedModule));
                providersByModule[module.name].insert(package.manifest.name);
            }
            for (const auto &plugin : package.manifest.plugins)
            {
                if (!SelectionMatches(package.manifest.conditions, plugin.selectors, effectiveProfile))
                {
                    continue;
                }
                if (!CompatibilityMatches(plugin.compatibility, effectiveProfile.operatingSystem, effectiveProfile.architecture))
                {
                    AddError(result.diagnostics, "package '" + package.manifest.name + "' provides plugin '" + plugin.name + "' that is not supported on '" + effectiveProfile.operatingSystem + "/" + effectiveProfile.architecture + "'");
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
        const auto packageByName = [&]() {
            std::unordered_map<std::string, const PackageManifest *> resultMap{};
            for (const auto &package : orderedPackages)
            {
                resultMap.emplace(package.manifest.name, &package.manifest);
            }
            return resultMap;
        }();
        for (const auto &feature : packageResolution.selectedFeatures)
        {
            const auto packageIt = packageByName.find(feature.packageName);
            if (packageIt == packageByName.end())
            {
                continue;
            }
            const auto &conditions = packageIt->second->conditions;
            const auto owner = feature.packageName + "::" + feature.featureName;
            for (const auto &module : feature.runtime.modules)
            {
                if (!SelectionMatches(conditions, module.selectors, effectiveProfile))
                {
                    continue;
                }
                if (!CompatibilityMatches(module.compatibility, effectiveProfile.operatingSystem, effectiveProfile.architecture))
                {
                    AddError(result.diagnostics, "package feature '" + owner + "' provides module '" + module.name + "' that is not supported on '" + effectiveProfile.operatingSystem + "/" + effectiveProfile.architecture + "'");
                    continue;
                }
                if (const auto providerIt = providersByModule.find(module.name); providerIt != providersByModule.end() && !providerIt->second.empty())
                {
                    AddError(result.diagnostics, "duplicate module declaration for '" + module.name + "' in '" + *providerIt->second.begin() + "' and package feature '" + owner + "'");
                    continue;
                }
                auto selectedModule = module;
                selectedModule.provenance =
                    SelectionProvenance(module.provenance, "package-feature", owner, packageIt->second->path,
                                        module.selectors, "selected package feature runtime module declaration");
                modules.emplace(selectedModule.name, std::move(selectedModule));
                providersByModule[module.name].insert(owner);
            }
        }
        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        std::set<std::string> directModules{};
        for (const auto &unit : projectUnits)
        {
            MergeRuntimeReferences(directModules, unit.project.runtime.enableModules, unit.project.runtime.disableModules, unit.project.conditions, unit.profile);
            if (unit.environment.has_value())
            {
                MergeRuntimeReferences(directModules, unit.environment->runtime.enableModules, unit.environment->runtime.disableModules, unit.project.conditions, unit.profile);
            }
            MergeRuntimeReferences(directModules, unit.profile.runtime.enableModules, unit.profile.runtime.disableModules, unit.project.conditions, unit.profile);
        }
        for (const auto &feature : packageResolution.selectedFeatures)
        {
            const auto packageIt = packageByName.find(feature.packageName);
            if (packageIt != packageByName.end())
            {
                MergeRuntimeReferences(directModules, feature.runtime.enableModules, feature.runtime.disableModules, packageIt->second->conditions, effectiveProfile);
            }
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
            MergeRuntimeReferences(directPlugins, unit.project.runtime.enablePlugins, unit.project.runtime.disablePlugins, unit.project.conditions, unit.profile);
            if (unit.environment.has_value())
            {
                MergeRuntimeReferences(directPlugins, unit.environment->runtime.enablePlugins, unit.environment->runtime.disablePlugins, unit.project.conditions, unit.profile);
            }
            MergeRuntimeReferences(directPlugins, unit.profile.runtime.enablePlugins, unit.profile.runtime.disablePlugins, unit.project.conditions, unit.profile);
        }
        for (const auto &feature : packageResolution.selectedFeatures)
        {
            const auto packageIt = packageByName.find(feature.packageName);
            if (packageIt != packageByName.end())
            {
                MergeRuntimeReferences(directPlugins, feature.runtime.enablePlugins, feature.runtime.disablePlugins, packageIt->second->conditions, effectiveProfile);
            }
        }

        for (const auto &module : directModules)
        {
            if (!modules.contains(module))
            {
                AddError(result.diagnostics, "profile '" + effectiveProfile.name + "' references unknown module '" + module + "'");
                continue;
            }
            if (!providersByModule.contains(module))
            {
                AddError(result.diagnostics, "profile '" + effectiveProfile.name + "' enables module '" + module + "' but no active project or package provides it");
            }
        }
        for (const auto &plugin : directPlugins)
        {
            if (!plugins.contains(plugin))
            {
                AddError(result.diagnostics, "profile '" + effectiveProfile.name + "' references unknown plugin '" + plugin + "'");
                continue;
            }
            if (!providersByPlugin.contains(plugin))
            {
                AddError(result.diagnostics, "profile '" + effectiveProfile.name + "' enables plugin '" + plugin + "' but no active package provides it");
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
                AddError(result.diagnostics, "profile '" + effectiveProfile.name + "' references unknown module '" + current + "'");
                continue;
            }
            if (it->second.requiresReflection && !effectiveProfile.enableReflection)
            {
                AddError(result.diagnostics, "profile '" + effectiveProfile.name + "' includes module '" + current + "' that requires reflection");
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
            AddError(result.diagnostics, "profile closure contains cyclic module dependencies");
            return result;
        }

        ResolvedLaunch resolved{};
        resolved.workspace = workspace;
        resolved.project = effectiveProject;
        resolved.profile = effectiveProfile;
        if (workspace.has_value())
        {
            const auto platformIt = std::find_if(
                workspace->platforms.begin(),
                workspace->platforms.end(),
                [&](const WorkspaceManifest::Platform &platform)
                {
                    return platform.name == effectiveProfile.platform;
                });
            if (platformIt != workspace->platforms.end())
            {
                resolved.targetAbiTag = platformIt->abi;
            }
            const auto toolchainIt = std::find_if(
                workspace->toolchains.begin(),
                workspace->toolchains.end(),
                [&](const WorkspaceManifest::Toolchain &toolchain)
                {
                    return toolchain.name == effectiveProfile.toolchain;
                });
            if (toolchainIt != workspace->toolchains.end())
            {
                resolved.selectedToolchain = *toolchainIt;
            }
        }
        resolved.projectUnits = std::move(projectUnits);
        resolved.selectedPackageFeatures = packageResolution.selectedFeatures;
        resolved.capabilityProviders = packageResolution.capabilityProviders;

        std::map<fs::path, std::string> inputOwnersByDestination{};
        std::map<fs::path, std::size_t> inputIndexByDestination{};
        std::set<std::tuple<std::string, std::string, std::string, std::string>> seenInputDeclarations{};
        auto collectInputs = [&](const std::vector<InputDeclaration> &inputs,
                                 const std::string &ownerKind,
                                 const std::string &ownerName,
                                 const fs::path &ownerDirectory,
                                 const fs::path &manifestPath,
                                 const std::vector<ConditionDefinition> *selectionConditions,
                                 const ProfileDefinition *selectionProfile)
        {
            for (const auto &input : inputs)
            {
                if (selectionConditions != nullptr && selectionProfile != nullptr
                    && !SelectionMatches(*selectionConditions, input.selectors, *selectionProfile))
                {
                    continue;
                }
                if (input.disabled)
                {
                    const auto stageIdentity = fs::path(StageInputIdentity(input, {}));
                    if (const auto index = inputIndexByDestination.find(stageIdentity);
                        index != inputIndexByDestination.end() && index->second < resolved.inputs.size())
                    {
                        resolved.inputs.erase(resolved.inputs.begin() + static_cast<std::ptrdiff_t>(index->second));
                        inputOwnersByDestination.erase(stageIdentity);
                        inputIndexByDestination.clear();
                        for (std::size_t resolvedIndex = 0; resolvedIndex < resolved.inputs.size(); ++resolvedIndex)
                        {
                            const auto &resolvedExisting = resolved.inputs[resolvedIndex];
                            if (resolvedExisting.stagedRelativePath.empty())
                            {
                                continue;
                            }
                            inputIndexByDestination[resolvedExisting.stagedRelativePath] = resolvedIndex;
                        }
                    }
                    continue;
                }
                std::vector<std::pair<std::string, fs::path>> expandedSources{};
                ExpandInputSources(input, ownerDirectory, expandedSources);
                if (input.mode == "Glob" && expandedSources.empty() && input.required)
                {
                    AddError(result.diagnostics, ownerKind + " '" + ownerName + "' input glob '" + input.pattern + "' matched no files");
                    continue;
                }
                for (const auto &[declaredSource, absoluteSource] : expandedSources)
                {
                    const auto declarationKey = std::make_tuple(ownerKind, ownerName, input.kind, declaredSource);
                    if (!seenInputDeclarations.insert(declarationKey).second)
                    {
                        continue;
                    }
                    if ((input.kind == "ToolInput" || input.kind == "Generated") && input.required && !fs::exists(absoluteSource))
                    {
                        AddError(result.diagnostics, ownerKind + " '" + ownerName + "' input '" + declaredSource + "' does not exist");
                        continue;
                    }

                    ResolvedInput resolvedInput{};
                    resolvedInput.ownerKind = ownerKind;
                    resolvedInput.ownerName = ownerName;
                    resolvedInput.ownerDirectory = ownerDirectory;
                    resolvedInput.manifestPath = manifestPath;
                    resolvedInput.declaringScope = input.declaringScope;
                    resolvedInput.setName = input.setName;
                    resolvedInput.name = input.name;
                    resolvedInput.kind = input.kind;
                    resolvedInput.role = input.role;
                    resolvedInput.source = declaredSource;
                    resolvedInput.pattern = input.pattern;
                    resolvedInput.mode = input.mode;
                    resolvedInput.visibility = input.visibility;
                    resolvedInput.target = input.target;
                    resolvedInput.targetRoot = input.targetRoot;
                    resolvedInput.basePath = input.basePath;
                    resolvedInput.contentKind = ResolvedInputKind(input);
                    resolvedInput.required = input.required;
                    resolvedInput.absoluteSourcePath = absoluteSource;
                    resolvedInput.includePatterns = input.includePatterns;
                    resolvedInput.excludePatterns = input.excludePatterns;
                    resolvedInput.metadata = input.metadata;
                    resolvedInput.provenance =
                        SelectionProvenance(input.provenance, ownerKind, ownerName, manifestPath, input.selectors,
                                            InputIsStaged(input) ? "staged file contribution" : "selected build input");

                    if (InputIsStaged(input))
                    {
                        resolvedInput.stagedRelativePath = InputDefaultTarget(input, declaredSource);
                        const auto stageIdentity = fs::path(StageInputIdentity(input, declaredSource));
                        if (const auto it = inputOwnersByDestination.find(stageIdentity);
                            it != inputOwnersByDestination.end())
                        {
                            if (input.overrideExisting)
                            {
                                if (const auto index = inputIndexByDestination.find(stageIdentity);
                                    index != inputIndexByDestination.end() && index->second < resolved.inputs.size())
                                {
                                    resolved.inputs[index->second] = std::move(resolvedInput);
                                    it->second = ownerKind + ":" + ownerName;
                                    continue;
                                }
                            }
                            AddError(result.diagnostics, "input destination collision at '" + resolvedInput.stagedRelativePath.string()
                                                              + "' between '" + it->second + "' and '" + ownerName + "'");
                            continue;
                        }
                        inputOwnersByDestination[stageIdentity] = ownerKind + ":" + ownerName;
                        inputIndexByDestination[stageIdentity] = resolved.inputs.size();
                    }
                    resolved.inputs.push_back(std::move(resolvedInput));
                }
            }
        };

        for (const auto &unit : resolved.projectUnits)
        {
            const auto ownerProjectDirectory = unit.project.path.parent_path();
            collectInputs(unit.project.inputs, "project", unit.project.name, ownerProjectDirectory, unit.project.path, &unit.project.conditions, &unit.profile);
            if (unit.environment.has_value())
            {
                collectInputs(unit.environment->inputs, "project", unit.project.name, ownerProjectDirectory, unit.project.path, &unit.project.conditions, &unit.profile);
            }
            collectInputs(unit.profile.inputs, "project", unit.project.name, ownerProjectDirectory, unit.project.path, &unit.project.conditions, &unit.profile);
        }
        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        {
            std::unordered_map<std::string, std::size_t> variableIndex{};
            std::unordered_map<std::string, std::size_t> featureIndex{};
            for (const auto &unit : resolved.projectUnits)
            {
                if (!unit.environment.has_value())
                {
                    continue;
                }
                const auto ownerProjectDirectory = unit.project.path.parent_path();
                std::set<std::string> requestedLocalSettingKeys{};
                for (const auto &variable : unit.environment->variables)
                {
                    if (!variable.fromLocalSetting.empty())
                    {
                        requestedLocalSettingKeys.insert(variable.fromLocalSetting);
                    }
                }
                const auto localSettings = LoadImportedSettings(unit.project, requestedLocalSettingKeys, workspaceRoot, warnedTrackedSettings, result.diagnostics);
                for (const auto &variable : unit.environment->variables)
                {
                    auto resolvedVariable = ResolveVariable(variable, localSettings, result.diagnostics);
                    resolvedVariable.provenance =
                        SelectionProvenance(variable.provenance, "project", unit.project.name, unit.project.path,
                                            {}, variable.secret ? "secret environment contribution"
                                                                : "environment contribution");
                    if (const auto it = variableIndex.find(resolvedVariable.name); it != variableIndex.end())
                    {
                        resolved.environmentVariables[it->second] = std::move(resolvedVariable);
                    }
                    else
                    {
                        variableIndex[resolvedVariable.name] = resolved.environmentVariables.size();
                        resolved.environmentVariables.push_back(std::move(resolvedVariable));
                    }
                }
                for (const auto &feature : unit.environment->features)
                {
                    if (!SelectionMatches(unit.project.conditions, feature.selectors, unit.profile))
                    {
                        continue;
                    }
                    if (const auto it = featureIndex.find(feature.name); it != featureIndex.end())
                    {
                        resolved.environmentFeatures[it->second] = feature;
                    }
                    else
                    {
                        featureIndex[feature.name] = resolved.environmentFeatures.size();
                        resolved.environmentFeatures.push_back(feature);
                    }
                }
            }
            for (const auto &feature : resolved.selectedPackageFeatures)
            {
                for (const auto &variable : feature.variables)
                {
                    auto resolvedVariable = ResolveVariable(variable, {}, result.diagnostics);
                    resolvedVariable.provenance =
                        SelectionProvenance(variable.provenance, "package-feature",
                                            feature.packageName + "::" + feature.featureName,
                                            feature.manifestPath, {}, variable.secret
                                                ? "selected package feature secret environment contribution"
                                                : "selected package feature environment contribution");
                    if (const auto it = variableIndex.find(resolvedVariable.name); it != variableIndex.end())
                    {
                        resolved.environmentVariables[it->second] = std::move(resolvedVariable);
                    }
                    else
                    {
                        variableIndex[resolvedVariable.name] = resolved.environmentVariables.size();
                        resolved.environmentVariables.push_back(std::move(resolvedVariable));
                    }
                }
            }
        }
        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        resolved.orderedPackages = std::move(orderedPackages);
        auto appendGenerators = [&](const std::vector<GeneratorDeclaration> &generators,
                                    const std::string &ownerKind,
                                    const std::string &ownerName,
                                    const fs::path &ownerDirectory,
                                    const fs::path &manifestPath,
                                    const std::vector<ConditionDefinition> &conditions,
                                    const std::string &packageName = {},
                                    const fs::path &packageDirectory = {},
                                    const fs::path &providerRoot = {})
        {
            for (const auto &generator : generators)
            {
                if (generator.disabled)
                {
                    continue;
                }
                if (!SelectionMatches(conditions, generator.selectors, resolved.profile))
                {
                    continue;
                }
                resolved.generators.push_back(ResolvedGenerator{
                    .declaration = generator,
                    .ownerKind = ownerKind,
                    .ownerName = ownerName,
                    .ownerDirectory = ownerDirectory,
                    .manifestPath = manifestPath,
                    .conditions = conditions,
                    .packageName = packageName,
                    .packageDirectory = packageDirectory,
                    .providerRoot = providerRoot,
                });
            }
        };
        auto collectProjectGenerators = [&](const ResolvedProjectUnit &unit)
        {
            const auto ownerProjectDirectory = unit.project.path.parent_path();
            for (auto &generator : EffectiveGenerators(unit.project, unit.profile))
            {
                resolved.generators.push_back(ResolvedGenerator{
                    .declaration = std::move(generator),
                    .ownerKind = "project",
                    .ownerName = unit.project.name,
                    .ownerDirectory = ownerProjectDirectory,
                    .manifestPath = unit.project.path,
                    .conditions = unit.project.conditions,
                });
            }
        };
        for (const auto &unit : resolved.projectUnits)
        {
            collectProjectGenerators(unit);
        }
        std::unordered_map<std::string, const PackageManifest *> resolvedPackageByName{};
        for (const auto &package : resolved.orderedPackages)
        {
            resolvedPackageByName.emplace(package.manifest.name, &package.manifest);
            const auto packageOwnerDirectory =
                package.sourceDirectory.empty() ? package.manifest.path.parent_path() : package.sourceDirectory;
            collectInputs(package.manifest.inputs, "package", package.manifest.name,
                          packageOwnerDirectory, package.manifest.path,
                          &package.manifest.conditions, &resolved.profile);
        }
        for (const auto &feature : resolved.selectedPackageFeatures)
        {
            const auto packageIt = resolvedPackageByName.find(feature.packageName);
            if (packageIt == resolvedPackageByName.end())
            {
                continue;
            }
            const auto featureOwnerDirectory =
                feature.providerRoot.empty() ? packageIt->second->path.parent_path() : feature.providerRoot;
            collectInputs(feature.inputs, "package-feature", feature.packageName + "::" + feature.featureName,
                          featureOwnerDirectory, packageIt->second->path,
                          &packageIt->second->conditions, &resolved.profile);
            appendGenerators(feature.generators,
                             "package-feature",
                             feature.packageName + "::" + feature.featureName,
                             featureOwnerDirectory,
                             packageIt->second->path,
                             packageIt->second->conditions,
                             feature.packageName,
                             packageIt->second->path.parent_path(),
                             feature.providerRoot);
        }
        if (result.diagnostics.HasErrors())
        {
            return result;
        }
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
        resolved.packageEdges = packageResolution.edges;
        resolved.packageScopes = packageResolution.scopes;
        resolved.enabledPlugins.assign(directPlugins.begin(), directPlugins.end());
        for (const auto &name : orderedModules)
        {
            if (requiredSet.contains(name))
            {
                resolved.requiredModules.push_back(name);
                resolved.runtimeModuleProvenance[name] = modules.at(name).provenance;
            }
            else if (optionalSet.contains(name))
            {
                resolved.optionalModules.push_back(name);
                resolved.runtimeModuleProvenance[name] = modules.at(name).provenance;
            }
        }
        resolved.dependencyEdges = std::move(depEdges);
        ResolveArtifacts(resolved.projectUnits, resolved.orderedPackages, resolved.project, resolved.profile, result.diagnostics, resolved.libraries, resolved.executables, resolved.selectedExecutable);
        if (result.diagnostics.HasErrors())
        {
            return result;
        }

        result.value = std::move(resolved);
        return result;
    }
}
