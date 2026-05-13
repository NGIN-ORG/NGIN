#pragma once

#include "Diagnostics.hpp"
#include "Model.hpp"

#include <map>
#include <optional>
#include <set>
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

    [[nodiscard]] auto StageInputIdentity(const InputDeclaration &input, const std::string &declaredSource)
        -> std::string;

    [[nodiscard]] auto ProjectReferenceIdentity(const ProjectReference &reference) -> std::string;

    [[nodiscard]] auto PackageFeatureUseIdentity(const PackageFeatureUse &use) -> std::string;

    auto MergeScopeList(std::string &target, const std::string &source) -> void;

    auto RemoveScopeList(std::string &target, const std::string &source) -> void;

    auto MergePackageReferences(std::vector<PackageReference> &target,
                                const std::vector<PackageReference> &source,
                                DiagnosticReport &report) -> void;

    [[nodiscard]] auto EffectiveProjectReferences(const ProjectManifest &project, const ProfileDefinition &profile)
        -> std::vector<ProjectReference>;

    [[nodiscard]] auto EffectivePackageReferences(const ProjectManifest &project, const ProfileDefinition &profile,
                                                  DiagnosticReport &report) -> std::vector<PackageReference>;

    [[nodiscard]] auto EffectivePackageFeatureUses(const ProjectManifest &project, const ProfileDefinition &profile)
        -> std::vector<PackageFeatureUse>;

    [[nodiscard]] auto EffectiveLaunches(const ProjectManifest &project, const ProfileDefinition &profile)
        -> std::vector<LaunchDefinition>;

    [[nodiscard]] auto EffectivePublishes(const ProjectManifest &project, const ProfileDefinition &profile)
        -> std::vector<PublishDefinition>;

    [[nodiscard]] auto EffectivePackageOutputs(const ProjectManifest &project, const ProfileDefinition &profile)
        -> std::vector<PackageOutputDefinition>;

    [[nodiscard]] auto EffectiveGenerators(const ProjectManifest &project, const ProfileDefinition &profile)
        -> std::vector<GeneratorDeclaration>;

    [[nodiscard]] auto EffectiveEnvironmentVariables(const std::vector<EnvironmentVariable> &base,
                                                     const std::vector<EnvironmentVariable> &overlay)
        -> std::vector<EnvironmentVariable>;

    auto MergeRuntimeReferences(std::set<std::string> &selected,
                                const std::vector<RuntimeReference> &enabled,
                                const std::vector<RuntimeReference> &disabled,
                                const std::vector<ConditionDefinition> &conditions,
                                const ProfileDefinition &profile) -> void;
}
