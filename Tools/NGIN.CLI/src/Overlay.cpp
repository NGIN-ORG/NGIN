#include "Overlay.hpp"

#include "Authoring.hpp"

#include <sstream>
#include <utility>

namespace NGIN::CLI
{
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
                        .sourceName = feature.packageName + "/" + feature.featureName,
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
}
