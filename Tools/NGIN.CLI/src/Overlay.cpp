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
        if (selectors.buildType.has_value())
        {
            identity << "|buildType=" << *selectors.buildType;
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

    [[nodiscard]] auto EffectiveAnalyzers(const ProjectManifest &project, const ProfileDefinition &profile,
                                          const std::vector<SelectedPackageFeature> &selectedFeatures)
        -> std::map<std::string, AnalyzerDefinition>
    {
        std::map<std::string, AnalyzerDefinition> analyzers{};
        const auto mergeAnalyzer = [&](AnalyzerDefinition selected) {
            if (selected.toolName.empty())
            {
                selected.toolName = selected.name;
            }

            const auto existing = analyzers.find(selected.name);
            if (existing != analyzers.end())
            {
                if (selected.toolName == selected.name && !existing->second.toolName.empty())
                {
                    selected.toolName = existing->second.toolName;
                }
                if (selected.packageName.empty())
                {
                    selected.packageName = existing->second.packageName;
                }
                if (selected.configPath.empty())
                {
                    selected.configPath = existing->second.configPath;
                    selected.configOptional = existing->second.configOptional;
                }
            }

            analyzers[selected.name] = std::move(selected);
        };
        const auto mergeSelected = [&](const std::vector<AnalyzerDefinition> &source) {
            for (const auto &analyzer : source)
            {
                if (SelectionMatches(project, analyzer.selectors, profile))
                {
                    mergeAnalyzer(analyzer);
                }
            }
        };
        for (const auto &feature : selectedFeatures)
        {
            for (const auto &analyzer : feature.quality.analyzers)
            {
                if (SelectionMatches(project, analyzer.selectors, profile))
                {
                    auto selected = analyzer;
                    if (selected.toolName.empty())
                    {
                        selected.toolName = selected.name;
                    }
                    if (selected.packageName.empty())
                    {
                        selected.packageName = feature.packageName;
                    }
                    selected.provenance = ContributionProvenance{
                        .sourceKind = "package-feature",
                        .sourceName = feature.packageName + "::" + feature.featureName,
                        .manifestPath = feature.manifestPath,
                        .reason = "selected package feature analyzer",
                    };
                    mergeAnalyzer(std::move(selected));
                }
            }
        }
        mergeSelected(project.quality.analyzers);
        mergeSelected(profile.quality.analyzers);
        return analyzers;
    }

    [[nodiscard]] auto AnalyzerToolName(const AnalyzerDefinition &analyzer) -> std::string
    {
        return analyzer.toolName.empty() ? analyzer.name : analyzer.toolName;
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
