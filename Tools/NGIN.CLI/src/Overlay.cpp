#include "Overlay.hpp"

#include "Authoring.hpp"
#include "Diagnostics.hpp"

#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace NGIN::CLI
{
    namespace
    {
        [[nodiscard]] auto SelectedEnvironment(const ProjectManifest &project, const ProfileDefinition &profile)
            -> const EnvironmentDefinition *
        {
            if (profile.environmentName.empty())
            {
                return nullptr;
            }
            const auto it = std::find_if(project.environments.begin(), project.environments.end(),
                                         [&](const EnvironmentDefinition &environment)
                                         { return environment.name == profile.environmentName; });
            return it == project.environments.end() ? nullptr : &*it;
        }

        template <typename TItem, typename TIdentity>
        [[nodiscard]] auto EffectiveNamedItems(const std::vector<TItem> &base,
                                               const std::vector<TItem> &overlay,
                                               TIdentity identity) -> std::vector<TItem>
        {
            std::map<std::string, TItem> selected{};
            const auto merge = [&](const std::vector<TItem> &items) {
                for (const auto &item : items)
                {
                    const auto id = identity(item);
                    if (id.empty())
                    {
                        continue;
                    }
                    if (item.disabled)
                    {
                        selected.erase(id);
                    }
                    else
                    {
                        selected[id] = item;
                    }
                }
            };
            merge(base);
            merge(overlay);

            std::vector<TItem> result{};
            for (auto &[_, item] : selected)
            {
                result.push_back(std::move(item));
            }
            return result;
        }
    }

    [[nodiscard]] auto BuildSettingDefineName(const std::string &value) -> std::string
    {
        if (const auto separator = value.find('='); separator != std::string::npos)
        {
            return value.substr(0, separator);
        }
        return value;
    }

    [[nodiscard]] auto NormalizeBuildSettingPath(const std::string &path) -> std::string
    {
        return fs::path(path).lexically_normal().generic_string();
    }

    [[nodiscard]] auto BuildSettingSelectorIdentity(const SelectorSet &selectors) -> std::string
    {
        std::ostringstream identity{};
        if (selectors.profile.has_value())
        {
            identity << "|profile=" << *selectors.profile;
        }
        if (selectors.platform.has_value())
        {
            identity << "|platform=" << *selectors.platform;
        }
        if (selectors.operatingSystem.has_value())
        {
            identity << "|os=" << *selectors.operatingSystem;
        }
        if (selectors.architecture.has_value())
        {
            identity << "|arch=" << *selectors.architecture;
        }
        if (selectors.toolchain.has_value())
        {
            identity << "|toolchain=" << *selectors.toolchain;
        }
        if (selectors.environment.has_value())
        {
            identity << "|environment=" << *selectors.environment;
        }
        for (const auto &condition : selectors.conditionRefs)
        {
            identity << "|condition=" << condition;
        }
        return identity.str();
    }

    [[nodiscard]] auto BuildSettingIdentity(const std::string &kind, const BuildSetting &setting) -> std::string
    {
        if (kind == "Define")
        {
            return BuildSettingDefineName(setting.value);
        }
        if (kind == "IncludePath")
        {
            return NormalizeBuildSettingPath(setting.value) + "|" + setting.visibility;
        }
        return setting.value + BuildSettingSelectorIdentity(setting.selectors);
    }

    [[nodiscard]] auto EffectiveBuildSettings(const ProjectManifest &project, const ProfileDefinition &profile,
                                              const std::vector<BuildSetting> &settings, const std::string &kind)
        -> std::vector<BuildSetting>
    {
        std::map<std::string, BuildSetting> byIdentity{};
        for (const auto &setting : settings)
        {
            if (!SelectionMatches(project, setting.selectors, profile))
            {
                continue;
            }
            const auto identity = setting.disabled ? setting.removeIdentity : BuildSettingIdentity(kind, setting);
            if (identity.empty())
            {
                continue;
            }
            if (setting.disabled)
            {
                byIdentity.erase(identity);
            }
            else
            {
                byIdentity[identity] = setting;
            }
        }

        std::vector<BuildSetting> result{};
        for (auto &[_, setting] : byIdentity)
        {
            result.push_back(std::move(setting));
        }
        return result;
    }

    [[nodiscard]] auto EffectiveToolRuns(const ProjectManifest &project, const ProfileDefinition &profile,
                                         const std::vector<SelectedPackageFeature> &selectedFeatures)
        -> std::map<std::string, ToolRunDefinition>
    {
        std::map<std::string, ToolRunDefinition> runs{};
        const auto mergeConfigs = [](std::vector<ToolConfigDefinition> base,
                                     const std::vector<ToolConfigDefinition> &overlay) {
            for (const auto &config : overlay)
            {
                const auto it = std::find_if(base.begin(), base.end(), [&](const ToolConfigDefinition &candidate) {
                    return candidate.name == config.name;
                });
                if (it == base.end()) base.push_back(config); else *it = config;
            }
            std::sort(base.begin(), base.end(), [](const auto &left, const auto &right) {
                return left.name < right.name;
            });
            return base;
        };
        const auto mergeReports = [](std::vector<ToolReportDefinition> base,
                                     const std::vector<ToolReportDefinition> &overlay) {
            for (const auto &report : overlay)
            {
                const auto it = std::find_if(base.begin(), base.end(), [&](const ToolReportDefinition &candidate) {
                    return candidate.name == report.name;
                });
                if (it == base.end()) base.push_back(report); else *it = report;
            }
            std::sort(base.begin(), base.end(), [](const auto &left, const auto &right) {
                return left.name < right.name;
            });
            return base;
        };
        const auto mergeRun = [&](ToolRunDefinition selected) {
            if (selected.disabled)
            {
                if (const auto existing = runs.find(selected.name); existing != runs.end())
                {
                    existing->second.enabled = false;
                    existing->second.excluded = true;
                    existing->second.provenance = selected.provenance;
                    existing->second.selectors = selected.selectors;
                }
                else
                {
                    selected.enabled = false;
                    selected.excluded = true;
                    runs[selected.name] = std::move(selected);
                }
                return;
            }

            selected.excluded = false;

            const auto existing = runs.find(selected.name);
            if (existing != runs.end())
            {
                auto merged = existing->second;
                if (!selected.displayName.empty())
                    merged.displayName = selected.displayName;
                if (!selected.description.empty())
                    merged.description = selected.description;
                if (!selected.action.empty())
                    merged.action = selected.action;
                if (!selected.packageName.empty())
                    merged.packageName = selected.packageName;
                if (!selected.packageFeature.empty())
                    merged.packageFeature = selected.packageFeature;
                merged.enabled = selected.enabled;
                merged.excluded = false;
                if (selected.hasInput)
                {
                    if (selected.input.merge == "Append" && merged.hasInput)
                    {
                        if (selected.input.contractExplicit) merged.input.contract = selected.input.contract;
                        if (selected.input.scopeExplicit) merged.input.scope = selected.input.scope;
                        if (selected.input.includeGeneratedExplicit)
                            merged.input.includeGenerated = selected.input.includeGenerated;
                        for (const auto &include : selected.input.includes)
                            if (std::find(merged.input.includes.begin(), merged.input.includes.end(), include) ==
                                merged.input.includes.end()) merged.input.includes.push_back(include);
                        for (const auto &exclude : selected.input.excludes)
                            if (std::find(merged.input.excludes.begin(), merged.input.excludes.end(), exclude) ==
                                merged.input.excludes.end()) merged.input.excludes.push_back(exclude);
                        merged.input.merge = "Append";
                    }
                    else
                    {
                        merged.input = selected.input;
                    }
                    merged.hasInput = true;
                }
                if (!selected.configs.empty())
                    merged.configs = mergeConfigs(std::move(merged.configs), selected.configs);
                if (selected.hasPolicy)
                {
                    auto policy = merged.policy;
                    if (selected.policy.gateExplicit) policy.gate = selected.policy.gate;
                    if (selected.policy.failOnExplicit) policy.failOn = selected.policy.failOn;
                    if (selected.policy.baselineExplicit) policy.baseline = selected.policy.baseline;
                    if (selected.policy.newFindingsOnlyExplicit)
                        policy.newFindingsOnly = selected.policy.newFindingsOnly;
                    if (selected.policy.maxFindingsExplicit) policy.maxFindings = selected.policy.maxFindings;
                    if (selected.policy.maxWarningsExplicit) policy.maxWarnings = selected.policy.maxWarnings;
                    for (const auto &mapping : selected.policy.severityMappings)
                    {
                        const auto item = std::find_if(policy.severityMappings.begin(), policy.severityMappings.end(),
                                                       [&](const auto &candidate) { return candidate.rule == mapping.rule; });
                        if (item == policy.severityMappings.end()) policy.severityMappings.push_back(mapping);
                        else *item = mapping;
                    }
                    for (const auto &suppression : selected.policy.suppressions)
                    {
                        const auto item = std::find_if(policy.suppressions.begin(), policy.suppressions.end(),
                                                       [&](const auto &candidate) {
                                                           return (!suppression.rule.empty() && candidate.rule == suppression.rule) ||
                                                                  (!suppression.fingerprint.empty() && candidate.fingerprint == suppression.fingerprint);
                                                       });
                        if (item == policy.suppressions.end()) policy.suppressions.push_back(suppression);
                        else *item = suppression;
                    }
                    for (const auto &budget : selected.policy.ruleBudgets)
                    {
                        const auto item = std::find_if(policy.ruleBudgets.begin(), policy.ruleBudgets.end(),
                                                       [&](const auto &candidate) { return candidate.rule == budget.rule; });
                        if (item == policy.ruleBudgets.end()) policy.ruleBudgets.push_back(budget);
                        else *item = budget;
                    }
                    policy.gateExplicit = policy.gateExplicit || selected.policy.gateExplicit;
                    policy.failOnExplicit = policy.failOnExplicit || selected.policy.failOnExplicit;
                    policy.baselineExplicit = policy.baselineExplicit || selected.policy.baselineExplicit;
                    policy.newFindingsOnlyExplicit = policy.newFindingsOnlyExplicit || selected.policy.newFindingsOnlyExplicit;
                    policy.maxFindingsExplicit = policy.maxFindingsExplicit || selected.policy.maxFindingsExplicit;
                    policy.maxWarningsExplicit = policy.maxWarningsExplicit || selected.policy.maxWarningsExplicit;
                    merged.policy = std::move(policy);
                    merged.hasPolicy = true;
                }
                if (selected.hasExecution)
                {
                    auto execution = merged.execution;
                    if (selected.execution.jobsExplicit) execution.jobs = selected.execution.jobs;
                    if (selected.execution.timeoutExplicit) execution.timeout = selected.execution.timeout;
                    if (selected.execution.cacheExplicit) execution.cache = selected.execution.cache;
                    if (selected.execution.failureStrategyExplicit)
                        execution.failureStrategy = selected.execution.failureStrategy;
                    if (selected.execution.weightExplicit) execution.weight = selected.execution.weight;
                    if (selected.execution.maxParallelismExplicit)
                        execution.maxParallelism = selected.execution.maxParallelism;
                    if (selected.execution.exclusiveResourceExplicit)
                        execution.exclusiveResource = selected.execution.exclusiveResource;
                    execution.jobsExplicit = execution.jobsExplicit || selected.execution.jobsExplicit;
                    execution.timeoutExplicit = execution.timeoutExplicit || selected.execution.timeoutExplicit;
                    execution.cacheExplicit = execution.cacheExplicit || selected.execution.cacheExplicit;
                    execution.failureStrategyExplicit = execution.failureStrategyExplicit || selected.execution.failureStrategyExplicit;
                    execution.weightExplicit = execution.weightExplicit || selected.execution.weightExplicit;
                    execution.maxParallelismExplicit = execution.maxParallelismExplicit || selected.execution.maxParallelismExplicit;
                    execution.exclusiveResourceExplicit = execution.exclusiveResourceExplicit || selected.execution.exclusiveResourceExplicit;
                    merged.execution = std::move(execution);
                    merged.hasExecution = true;
                }
                if (!selected.reports.empty())
                    merged.reports = mergeReports(std::move(merged.reports), selected.reports);
                if (!selected.dependencies.empty())
                    merged.dependencies = selected.dependencies;
                merged.selectors = selected.selectors;
                merged.provenance = selected.provenance;
                runs[selected.name] = std::move(merged);
                return;
            }
            if (selected.originProvenance.sourceKind.empty())
                selected.originProvenance = selected.provenance;
            runs[selected.name] = std::move(selected);
        };
        const auto mergeSelected = [&](const std::vector<ToolRunDefinition> &source) {
            for (const auto &run : source)
            {
                if (SelectionMatches(project, run.selectors, profile))
                {
                    mergeRun(run);
                }
            }
        };
        for (const auto &feature : selectedFeatures)
        {
            for (const auto &run : feature.tooling.runs)
            {
                if (SelectionMatches(project, run.selectors, profile))
                {
                    auto selected = run;
                    if (selected.packageName.empty())
                    {
                        selected.packageName = feature.packageName;
                    }
                    selected.packageFeature = feature.featureName;
                    selected.provenance = ContributionProvenance{
                        .sourceKind = "package-feature",
                        .sourceName = feature.packageName + "::" + feature.featureName,
                        .manifestPath = feature.manifestPath,
                        .reason = "selected package feature tool run",
                    };
                    mergeRun(std::move(selected));
                }
            }
        }
        mergeSelected(project.tooling.runs);
        mergeSelected(profile.tooling.runs);
        return runs;
    }

    [[nodiscard]] auto StageInputIdentity(const InputDeclaration &input, const std::string &declaredSource)
        -> std::string
    {
        if (!input.removeIdentity.empty())
        {
            return fs::path(input.removeIdentity).lexically_normal().generic_string();
        }
        if (!input.target.empty())
        {
            return fs::path(input.target).lexically_normal().generic_string();
        }

        const auto sourcePath = fs::path(declaredSource);
        if (!input.targetRoot.empty())
        {
            auto preserved = sourcePath.filename();
            const auto sourceRelative = sourcePath.lexically_normal();
            if (input.mode == "Directory" && !input.path.empty())
            {
                const auto base = fs::path(input.path).lexically_normal();
                const auto relative = sourceRelative.lexically_relative(base);
                if (!relative.empty() && relative.generic_string().find("..") != 0)
                {
                    preserved = relative;
                }
            }
            else if (!input.basePath.empty())
            {
                const auto base = fs::path(input.basePath).lexically_normal();
                const auto relative = sourceRelative.lexically_relative(base);
                if (!relative.empty() && relative.generic_string().find("..") != 0)
                {
                    preserved = relative;
                }
            }
            return (fs::path(input.targetRoot) / preserved).lexically_normal().generic_string();
        }

        return (sourcePath.is_absolute() ? sourcePath.filename() : sourcePath.lexically_normal()).generic_string();
    }

    [[nodiscard]] auto ProjectReferenceIdentity(const ProjectReference &reference) -> std::string
    {
        return reference.path.lexically_normal().generic_string();
    }

    [[nodiscard]] auto PackageFeatureUseIdentity(const PackageFeatureUse &use) -> std::string
    {
        return use.packageName + "::" + use.featureName;
    }

    auto MergeScopeList(std::string &target, const std::string &source) -> void
    {
        if (source.empty())
        {
            return;
        }
        std::set<std::string> scopes{};
        std::stringstream existing{target};
        std::string scope{};
        while (std::getline(existing, scope, ';'))
        {
            if (!scope.empty())
            {
                scopes.insert(scope);
            }
        }
        std::stringstream incoming{source};
        while (std::getline(incoming, scope, ';'))
        {
            if (!scope.empty())
            {
                scopes.insert(scope);
            }
        }
        target.clear();
        for (const auto &entry : scopes)
        {
            if (!target.empty())
            {
                target += ";";
            }
            target += entry;
        }
    }

    auto RemoveScopeList(std::string &target, const std::string &source) -> void
    {
        if (source.empty() || target.empty())
        {
            return;
        }
        std::set<std::string> scopes{};
        std::stringstream existing{target};
        std::string scope{};
        while (std::getline(existing, scope, ';'))
        {
            if (!scope.empty())
            {
                scopes.insert(scope);
            }
        }
        std::stringstream incoming{source};
        while (std::getline(incoming, scope, ';'))
        {
            if (!scope.empty())
            {
                scopes.erase(scope);
            }
        }
        target.clear();
        for (const auto &entry : scopes)
        {
            if (!target.empty())
            {
                target += ";";
            }
            target += entry;
        }
    }

    auto MergePackageReferences(std::vector<PackageReference> &target,
                                const std::vector<PackageReference> &source,
                                DiagnosticReport &report) -> void
    {
        std::unordered_map<std::string, std::size_t> indexByName{};
        for (std::size_t index = 0; index < target.size(); ++index)
        {
            indexByName[target[index].name] = index;
        }
        for (const auto &reference : source)
        {
            if (reference.disabled)
            {
                if (const auto it = indexByName.find(reference.name); it != indexByName.end())
                {
                    target.erase(target.begin() + static_cast<std::ptrdiff_t>(it->second));
                    indexByName.clear();
                    for (std::size_t index = 0; index < target.size(); ++index)
                    {
                        indexByName[target[index].name] = index;
                    }
                }
                continue;
            }
            if (const auto it = indexByName.find(reference.name); it != indexByName.end())
            {
                auto &existing = target[it->second];
                if (!existing.versionRange.empty() && !reference.versionRange.empty() &&
                    existing.versionRange != reference.versionRange)
                {
                    AddError(report, "package '" + reference.name + "' has conflicting version ranges '" +
                                         existing.versionRange + "' and '" + reference.versionRange + "'");
                }
                auto scope = existing.scope;
                MergeScopeList(scope, reference.scope);
                RemoveScopeList(scope, reference.removeScope);
                if (existing.versionRange.empty() && !reference.versionRange.empty())
                {
                    existing.versionRange = reference.versionRange;
                }
                existing.optional = existing.optional && reference.optional;
                existing.scope = std::move(scope);
                continue;
            }
            if (reference.removeScope.empty())
            {
                indexByName[reference.name] = target.size();
                target.push_back(reference);
                continue;
            }
            auto adjusted = reference;
            RemoveScopeList(adjusted.scope, adjusted.removeScope);
            if (adjusted.scope.empty() && adjusted.versionRange.empty())
            {
                continue;
            }
            indexByName[adjusted.name] = target.size();
            target.push_back(std::move(adjusted));
        }
    }

    [[nodiscard]] auto EffectiveProjectReferences(const ProjectManifest &project, const ProfileDefinition &profile)
        -> std::vector<ProjectReference>
    {
        std::map<std::string, ProjectReference> selected{};
        const auto merge = [&](const std::vector<ProjectReference> &references) {
            for (const auto &reference : references)
            {
                if (!SelectionMatches(project, reference.selectors, profile))
                {
                    continue;
                }
                const auto identity = ProjectReferenceIdentity(reference);
                if (reference.disabled)
                {
                    selected.erase(identity);
                }
                else
                {
                    selected[identity] = reference;
                }
            }
        };
        merge(project.projectRefs);
        if (const auto *environment = SelectedEnvironment(project, profile))
        {
            merge(environment->projectRefs);
        }
        merge(profile.projectRefs);

        std::vector<ProjectReference> result{};
        for (auto &[_, reference] : selected)
        {
            result.push_back(std::move(reference));
        }
        return result;
    }

    [[nodiscard]] auto EffectivePackageReferences(const ProjectManifest &project, const ProfileDefinition &profile,
                                                  DiagnosticReport &report) -> std::vector<PackageReference>
    {
        std::vector<PackageReference> result{};
        const auto mergeSelected = [&](const std::vector<PackageReference> &references) {
            std::vector<PackageReference> selected{};
            for (const auto &reference : references)
            {
                if (SelectionMatches(project, reference.selectors, profile))
                {
                    selected.push_back(reference);
                }
            }
            MergePackageReferences(result, selected, report);
        };
        mergeSelected(project.packageRefs);
        if (const auto *environment = SelectedEnvironment(project, profile))
        {
            mergeSelected(environment->packageRefs);
        }
        mergeSelected(profile.packageRefs);
        return result;
    }

    [[nodiscard]] auto EffectivePackageFeatureUses(const ProjectManifest &project, const ProfileDefinition &profile)
        -> std::vector<PackageFeatureUse>
    {
        std::map<std::string, PackageFeatureUse> selected{};
        const auto merge = [&](const std::vector<PackageFeatureUse> &uses) {
            for (const auto &use : uses)
            {
                if (!SelectionMatches(project, use.selectors, profile))
                {
                    continue;
                }
                const auto identity = PackageFeatureUseIdentity(use);
                if (use.disabled)
                {
                    selected.erase(identity);
                }
                else
                {
                    selected[identity] = use;
                }
            }
        };
        merge(project.packageFeatureUses);
        if (const auto *environment = SelectedEnvironment(project, profile))
        {
            merge(environment->packageFeatureUses);
        }
        merge(profile.packageFeatureUses);

        std::vector<PackageFeatureUse> result{};
        for (auto &[_, use] : selected)
        {
            result.push_back(std::move(use));
        }
        return result;
    }

    [[nodiscard]] auto EffectiveLaunches(const ProjectManifest &project, const ProfileDefinition &profile)
        -> std::vector<LaunchDefinition>
    {
        auto result = EffectiveNamedItems(project.launches, profile.launches,
                                          [](const LaunchDefinition &launch) { return launch.name; });
        if (result.empty() && !profile.launch.name.empty() && !profile.launch.disabled)
        {
            result.push_back(profile.launch);
        }
        return result;
    }

    [[nodiscard]] auto EffectivePublishes(const ProjectManifest &project, const ProfileDefinition &profile)
        -> std::vector<PublishDefinition>
    {
        return EffectiveNamedItems(project.publishes, profile.publishes,
                                   [](const PublishDefinition &publish) { return publish.name; });
    }

    [[nodiscard]] auto EffectivePackageOutputs(const ProjectManifest &project, const ProfileDefinition &profile)
        -> std::vector<PackageOutputDefinition>
    {
        return EffectiveNamedItems(project.packageOutputs, profile.packageOutputs,
                                   [](const PackageOutputDefinition &output) { return output.name; });
    }

    [[nodiscard]] auto EffectiveGenerators(const ProjectManifest &project, const ProfileDefinition &profile)
        -> std::vector<GeneratorDeclaration>
    {
        std::map<std::string, GeneratorDeclaration> selected{};
        const auto merge = [&](const std::vector<GeneratorDeclaration> &generators) {
            for (const auto &generator : generators)
            {
                if (!SelectionMatches(project, generator.selectors, profile))
                {
                    continue;
                }
                if (generator.disabled)
                {
                    selected.erase(generator.name);
                }
                else
                {
                    selected[generator.name] = generator;
                }
            }
        };
        merge(project.generators);
        if (const auto *environment = SelectedEnvironment(project, profile))
        {
            merge(environment->generators);
        }
        merge(profile.generators);

        std::vector<GeneratorDeclaration> result{};
        for (auto &[_, generator] : selected)
        {
            result.push_back(std::move(generator));
        }
        return result;
    }

    [[nodiscard]] auto EffectiveEnvironmentVariables(const std::vector<EnvironmentVariable> &base,
                                                     const std::vector<EnvironmentVariable> &overlay)
        -> std::vector<EnvironmentVariable>
    {
        return EffectiveNamedItems(base, overlay, [](const EnvironmentVariable &variable) { return variable.name; });
    }

    auto MergeRuntimeReferences(std::set<std::string> &selected,
                                const std::vector<RuntimeReference> &enabled,
                                const std::vector<RuntimeReference> &disabled,
                                const std::vector<ConditionDefinition> &conditions,
                                const ProfileDefinition &profile) -> void
    {
        for (const auto &reference : enabled)
        {
            if (SelectionMatches(conditions, reference.selectors, profile))
            {
                selected.insert(reference.name);
            }
        }
        for (const auto &reference : disabled)
        {
            if (SelectionMatches(conditions, reference.selectors, profile))
            {
                selected.erase(reference.name);
            }
        }
    }
}
