#pragma once

#include "Model.hpp"

#include <map>
#include <string>
#include <vector>

namespace NGIN::CLI
{
    [[nodiscard]] auto BuildSettingDefineName(const std::string &value) -> std::string;

    [[nodiscard]] auto NormalizeBuildSettingPath(const std::string &path) -> std::string;

    [[nodiscard]] auto BuildSettingSelectorIdentity(const SelectorSet &selectors) -> std::string;

    [[nodiscard]] auto BuildSettingIdentity(const std::string &kind, const BuildSetting &setting) -> std::string;

    [[nodiscard]] auto EffectiveBuildSettings(const ProjectManifest &project, const ProfileDefinition &profile,
                                              const std::vector<BuildSetting> &settings, const std::string &kind)
        -> std::vector<BuildSetting>;

    [[nodiscard]] auto EffectiveAnalyzers(const ProjectManifest &project, const ProfileDefinition &profile,
                                          const std::vector<SelectedPackageFeature> &selectedFeatures = {})
        -> std::map<std::string, AnalyzerDefinition>;

    [[nodiscard]] auto AnalyzerToolName(const AnalyzerDefinition &analyzer) -> std::string;
}
