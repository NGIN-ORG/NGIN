#include "DeploymentPlans.hpp"

#include "Canonical.hpp"
#include "ManifestPaths.hpp"
#include "Placeholders.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>

namespace NGIN::CLI
{
    namespace
    {
        auto AddError(std::vector<ManifestDiagnostic> &diagnostics, std::string code, std::string message,
                      const GraphProvenance &provenance = {}) -> void
        {
            diagnostics.push_back(ManifestDiagnostic{
                .severity = ManifestDiagnosticSeverity::Error,
                .code = std::move(code),
                .message = std::move(message),
                .source = ManifestSourceRange{
                    .path = provenance.document,
                    .begin = ManifestSourcePosition{.line = provenance.line, .column = provenance.column}}});
        }

        [[nodiscard]] auto ProvenanceValue(const GraphProvenance &value) -> CanonicalValue
        {
            return CanonicalValue::Object{{"column", static_cast<std::int64_t>(value.column)},
                                          {"document", value.document},
                                          {"kind", value.kind},
                                          {"line", static_cast<std::int64_t>(value.line)},
                                          {"owner", value.owner},
                                          {"reason", value.reason}};
        }

        [[nodiscard]] auto IdentityValue(const PlanIdentity &value) -> CanonicalValue
        {
            return CanonicalValue::Object{{"adapter", value.adapter},
                                          {"adapterVersion", value.adapterVersion},
                                          {"compositionIdentity", value.compositionIdentity},
                                          {"identity", value.identity},
                                          {"kind", value.kind}};
        }

        [[nodiscard]] auto StringArray(const std::vector<std::string> &values) -> CanonicalValue
        {
            CanonicalValue::Array result{};
            for (const auto &value : values) result.emplace_back(value);
            return result;
        }

        [[nodiscard]] auto StringMap(const std::map<std::string, std::string, std::less<>> &values) -> CanonicalValue
        {
            CanonicalValue::Object result{};
            for (const auto &[name, value] : values) result.emplace(name, value);
            return result;
        }

        [[nodiscard]] auto ItemKindName(const StagePlanItemKind kind) -> std::string
        {
            switch (kind)
            {
            case StagePlanItemKind::ProductArtifact: return "ProductArtifact";
            case StagePlanItemKind::PluginArtifact: return "PluginArtifact";
            case StagePlanItemKind::RuntimeFile: return "RuntimeFile";
            case StagePlanItemKind::Asset: return "Asset";
            case StagePlanItemKind::Notice: return "Notice";
            case StagePlanItemKind::Symbol: return "Symbol";
            case StagePlanItemKind::ProjectFile: return "ProjectFile";
            }
            return "RuntimeFile";
        }

        [[nodiscard]] auto ContributionKind(const GraphContribution &value) -> StagePlanItemKind
        {
            if (value.kind == "Notice") return StagePlanItemKind::Notice;
            if (value.kind.starts_with("Asset")) return StagePlanItemKind::Asset;
            if (value.kind.starts_with("Project")) return StagePlanItemKind::ProjectFile;
            return StagePlanItemKind::RuntimeFile;
        }

        [[nodiscard]] auto HasGlob(const std::string_view value) -> bool
        {
            return value.find_first_of("*?[") != std::string_view::npos;
        }

        [[nodiscard]] auto Fold(std::string value, const bool enabled) -> std::string
        {
            if (enabled)
                std::ranges::transform(value, value.begin(), [](const unsigned char character) {
                    return static_cast<char>(std::tolower(character));
                });
            return value;
        }

        [[nodiscard]] auto PackageOwner(const std::string_view owner) -> std::string
        {
            const auto separator = owner.find("::");
            return std::string(owner.substr(0, separator));
        }

        [[nodiscard]] auto RelativeTail(const std::string &source, const std::string &pattern) -> std::string
        {
            const auto wildcard = pattern.find_first_of("*?[");
            auto prefix = wildcard == std::string::npos
                              ? std::filesystem::path(pattern).parent_path()
                              : std::filesystem::path(pattern.substr(0, wildcard)).parent_path();
            auto tail = std::filesystem::path(source).lexically_relative(prefix);
            if (tail.empty() || tail == ".") tail = std::filesystem::path(source).filename();
            return tail.generic_string();
        }

        auto AddStageItem(StagePlan &plan, std::map<std::string, std::size_t, std::less<>> &destinations,
                          std::vector<ManifestDiagnostic> &diagnostics, StagePlanItem item,
                          const StagePlanBindings &bindings) -> void
        {
            const auto destination = NormalizeStageDestination(item.destination);
            if (!destination.Succeeded())
            {
                diagnostics.insert(diagnostics.end(), destination.diagnostics.begin(), destination.diagnostics.end());
                return;
            }
            item.destination = destination.value->value;
            const auto key = Fold(item.destination, bindings.targetCaseInsensitive);
            if (const auto existing = destinations.find(key); existing != destinations.end())
            {
                const auto &previous = plan.items[existing->second];
                const auto replacement = bindings.allowedReplacements.find(item.destination);
                if (replacement != bindings.allowedReplacements.end() && replacement->second == item.owner)
                {
                    item.reason += "; explicitly replaces stage item owned by " + previous.owner;
                    plan.items[existing->second] = std::move(item);
                    return;
                }
                AddError(diagnostics, "NGIN7202",
                         "stage destination collision at '" + item.destination + "' between owners '" + previous.owner +
                             "' and '" + item.owner + "'",
                         item.provenance);
                return;
            }
            destinations.emplace(key, plan.items.size());
            plan.items.push_back(std::move(item));
        }

        [[nodiscard]] auto ArtifactSource(const std::map<std::string, std::filesystem::path, std::less<>> &bindings,
                                          const std::string &identity, const std::string &fallback)
            -> std::pair<std::string, bool>
        {
            const auto found = bindings.find(identity);
            return found == bindings.end() ? std::pair{fallback, true}
                                           : std::pair{found->second.lexically_normal().generic_string(), false};
        }

        [[nodiscard]] auto ProductDestination(const GraphProduct &product, const std::string &source) -> std::string
        {
            auto filename = std::filesystem::path(source).filename().generic_string();
            if (filename.empty() || source.starts_with("artifact:")) filename = product.name;
            return (product.type == ProductType::Library  ? "lib/"
                    : product.type == ProductType::Plugin ? "plugins/"
                                                          : "bin/") +
                   filename;
        }

        [[nodiscard]] auto StageItemValue(const StagePlanItem &item) -> CanonicalValue
        {
            return CanonicalValue::Object{{"destination", item.destination},
                                          {"identity", item.identity},
                                          {"kind", ItemKindName(item.kind)},
                                          {"owner", item.owner},
                                          {"provenance", ProvenanceValue(item.provenance)},
                                          {"reason", item.reason},
                                          {"source", item.source},
                                          {"symbolicArtifact", item.symbolicArtifact}};
        }

        template <typename Plan, typename Serialize>
        [[nodiscard]] auto Fingerprint(Plan plan, const std::string_view kind, Serialize serialize) -> std::string
        {
            plan.plan.identity.clear();
            return CanonicalFingerprint(kind, {{"plan", serialize(plan)}});
        }

        [[nodiscard]] auto ProductArtifact(const StagePlan &stage, const std::string_view owner)
            -> const StagePlanItem *
        {
            const auto found = std::ranges::find_if(stage.items, [&](const StagePlanItem &item) {
                return item.kind == StagePlanItemKind::ProductArtifact && item.owner == owner;
            });
            return found == stage.items.end() ? nullptr : &*found;
        }

        [[nodiscard]] auto RuntimeVariable(const std::string_view operatingSystem) -> std::string
        {
            if (operatingSystem == "windows") return "PATH";
            if (operatingSystem == "macos" || operatingSystem == "ios") return "DYLD_LIBRARY_PATH";
            return "LD_LIBRARY_PATH";
        }

        [[nodiscard]] auto RuntimeSeparator(const std::string_view operatingSystem) -> std::string
        {
            return operatingSystem == "windows" ? ";" : ":";
        }

        [[nodiscard]] auto PublishCategory(const StagePlanItemKind kind) -> std::string
        {
            if (kind == StagePlanItemKind::Notice) return "Notice";
            if (kind == StagePlanItemKind::PluginArtifact) return "Plugin";
            if (kind == StagePlanItemKind::RuntimeFile) return "Runtime";
            if (kind == StagePlanItemKind::ProductArtifact) return "Product";
            if (kind == StagePlanItemKind::Asset) return "Asset";
            if (kind == StagePlanItemKind::Symbol) return "Symbol";
            return "Project";
        }
    } // namespace

    auto DeriveStagePlan(const ResolvedCompositionGraph &graph, const StagePlanBindings &bindings)
        -> DeploymentPlanResult<StagePlan>
    {
        DeploymentPlanResult<StagePlan> result{};
        const auto &data = graph.Data();
        StagePlan plan{.plan = PlanIdentity{.kind = "StagePlan",
                                            .compositionIdentity = graph.CompositionIdentity(),
                                            .adapter = "NGIN.Stage",
                                            .adapterVersion = "1"},
                       .stageRoot = bindings.stageRoot.lexically_normal().generic_string()};
        if (bindings.stageRoot.empty())
        {
            AddError(result.diagnostics, "NGIN7200", "StagePlan requires a non-empty stage root binding");
            return result;
        }
        std::map<std::string, std::size_t, std::less<>> destinations{};
        if (data.product.type != ProductType::External)
        {
            const auto [source, symbolic] = ArtifactSource(bindings.productArtifacts, data.product.identity,
                                                           "artifact:product:" + data.product.identity);
            AddStageItem(plan, destinations, result.diagnostics,
                         StagePlanItem{.identity = "Stage:Product:" + data.product.identity,
                                       .kind = StagePlanItemKind::ProductArtifact,
                                       .owner = data.product.identity,
                                       .reason = "primary product artifact",
                                       .source = source,
                                       .destination = ProductDestination(data.product, source),
                                       .symbolicArtifact = symbolic,
                                       .provenance = data.product.provenance},
                         bindings);
        }
        for (const auto &plugin : data.plugins)
        {
            const auto [source, symbolic] =
                ArtifactSource(bindings.pluginArtifacts, plugin.identity, "artifact:plugin:" + plugin.identity);
            auto filename = std::filesystem::path(source).filename().generic_string();
            if (filename.empty() || symbolic) filename = plugin.exportName;
            AddStageItem(plan, destinations, result.diagnostics,
                         StagePlanItem{.identity = "Stage:Plugin:" + plugin.identity,
                                       .kind = StagePlanItemKind::PluginArtifact,
                                       .owner = plugin.identity,
                                       .reason = "active Plugin deployment artifact; "
                                                 "loading remains application-owned",
                                       .source = source,
                                       .destination = "plugins/" + filename,
                                       .symbolicArtifact = symbolic,
                                       .provenance = plugin.provenance},
                         bindings);
        }
        for (const auto &[owner, symbols] : bindings.symbolArtifacts)
            for (const auto &symbol : symbols)
            {
                const auto filename = symbol.filename().generic_string();
                if (filename.empty())
                {
                    AddError(result.diagnostics, "NGIN7201",
                             "symbol artifact has no filename for owner '" + owner + "'");
                    continue;
                }
                AddStageItem(plan, destinations, result.diagnostics,
                             StagePlanItem{.identity = "Stage:Symbol:" + owner + ":" + filename,
                                           .kind = StagePlanItemKind::Symbol,
                                           .owner = owner,
                                           .reason = "debug symbol artifact binding",
                                           .source = symbol.lexically_normal().generic_string(),
                                           .destination = "symbols/" + filename},
                             bindings);
            }
        for (const auto &contribution : data.contributions)
        {
            const auto projectOwned = contribution.owner == data.product.identity;
            const auto packageOwner = PackageOwner(contribution.owner);
            const auto packageRoot = bindings.packageRoots.find(packageOwner);
            const auto root = projectOwned                                 ? bindings.projectRoot
                              : packageRoot == bindings.packageRoots.end() ? std::filesystem::path{}
                                                                           : packageRoot->second;
            if (root.empty())
            {
                AddError(result.diagnostics, "NGIN7201",
                         "no source root binding for stage owner '" + contribution.owner + "'",
                         contribution.provenance);
                continue;
            }
            const auto directoryContribution = contribution.kind.ends_with("Directory");
            std::vector<PortablePath> matches{};
            if (directoryContribution)
            {
                const auto directory = root / std::filesystem::path(contribution.include);
                std::error_code error{};
                if (!std::filesystem::is_directory(directory, error))
                {
                    AddError(result.diagnostics, "NGIN7201",
                             "stage directory source is missing: " + directory.generic_string(),
                             contribution.provenance);
                    continue;
                }
                auto expanded = ExpandPortableGlob(root, contribution.include + "/**", bindings.targetCaseInsensitive,
                                                   {}, bindings.allowSymlinks);
                result.diagnostics.insert(result.diagnostics.end(), expanded.diagnostics.begin(),
                                          expanded.diagnostics.end());
                matches = std::move(expanded.matches);
            }
            else if (HasGlob(contribution.include))
            {
                auto expanded = ExpandPortableGlob(root, contribution.include, bindings.targetCaseInsensitive, {},
                                                   bindings.allowSymlinks);
                result.diagnostics.insert(result.diagnostics.end(), expanded.diagnostics.begin(),
                                          expanded.diagnostics.end());
                matches = std::move(expanded.matches);
                if (matches.empty() && expanded.diagnostics.empty())
                    AddError(result.diagnostics, "NGIN7201",
                             "stage file pattern matched no files: " + contribution.include, contribution.provenance);
            }
            else
            {
                const auto source = root / std::filesystem::path(contribution.include);
                std::error_code error{};
                if (!std::filesystem::is_regular_file(source, error))
                {
                    AddError(result.diagnostics, "NGIN7201", "stage file source is missing: " + source.generic_string(),
                             contribution.provenance);
                    continue;
                }
                matches.push_back(PortablePath{.value = contribution.include});
            }
            for (const auto &match : matches)
            {
                const auto packageStyleDestination = !projectOwned;
                auto destination = contribution.destination;
                if (directoryContribution || HasGlob(contribution.include) || packageStyleDestination)
                    destination += "/" + RelativeTail(match.value, contribution.include);
                AddStageItem(
                    plan, destinations, result.diagnostics,
                    StagePlanItem{.identity = "Stage:Contribution:" + contribution.identity + ":" + match.value,
                                  .kind = ContributionKind(contribution),
                                  .owner = contribution.owner,
                                  .reason = contribution.provenance.reason,
                                  .source =
                                      (root / std::filesystem::path(match.value)).lexically_normal().generic_string(),
                                  .destination = destination,
                                  .provenance = contribution.provenance},
                    bindings);
            }
        }
        if (!result.diagnostics.empty()) return result;
        std::ranges::sort(plan.items, {}, &StagePlanItem::identity);
        plan.plan.identity = FingerprintStagePlan(plan);
        result.plan = std::move(plan);
        return result;
    }

    auto DeriveLaunchPlan(const ResolvedCompositionGraph &graph, const StagePlan &stage,
                          const StagePlanBindings &bindings, std::optional<std::string> launchName)
        -> DeploymentPlanResult<LaunchPlan>
    {
        DeploymentPlanResult<LaunchPlan> result{};
        const auto &launches = graph.Data().launches;
        if (launches.empty())
        {
            AddError(result.diagnostics, "NGIN7210", "project has no Launch intent");
            return result;
        }
        const GraphLaunch *selected = nullptr;
        if (launchName.has_value())
        {
            const auto found = std::ranges::find(launches, *launchName, &GraphLaunch::name);
            if (found != launches.end()) selected = &*found;
        }
        else
        {
            const auto found = std::ranges::find(launches, true, &GraphLaunch::defaultLaunch);
            selected = found != launches.end() ? &*found : launches.size() == 1 ? &launches.front() : nullptr;
        }
        if (selected == nullptr)
        {
            AddError(result.diagnostics, "NGIN7210",
                     launchName.has_value() ? "unknown Launch '" + *launchName + "'"
                                            : "multiple Launch definitions require a name or Default");
            return result;
        }
        LaunchPlan plan{.plan = PlanIdentity{.kind = "LaunchPlan",
                                             .compositionIdentity = graph.CompositionIdentity(),
                                             .adapter = "NGIN.Process",
                                             .adapterVersion = "1"},
                        .name = selected->name,
                        .workingDirectory =
                            (std::filesystem::path(stage.stageRoot) / std::filesystem::path(selected->workingDirectory))
                                .lexically_normal()
                                .generic_string(),
                        .arguments = selected->arguments,
                        .environment = selected->environment,
                        .secretReferences = selected->secrets};
        if (selected->workingDirectory != "." && !NormalizeStageDestination(selected->workingDirectory).Succeeded())
        {
            AddError(result.diagnostics, "NGIN7210", "Launch WorkingDirectory escapes the stage root",
                     selected->provenance);
            return result;
        }
        if (selected->executableKind == "Product")
        {
            if (selected->executable != graph.Data().product.name)
            {
                AddError(result.diagnostics, "NGIN7210",
                         "Launch selects unknown Product '" + selected->executable + "'", selected->provenance);
                return result;
            }
            const auto *artifact = ProductArtifact(stage, graph.Data().product.identity);
            if (artifact == nullptr)
                AddError(result.diagnostics, "NGIN7210", "Launch product has no staged artifact", selected->provenance);
            else
            {
                plan.executable = artifact->symbolicArtifact
                                      ? artifact->source
                                      : (std::filesystem::path(stage.stageRoot) / artifact->destination)
                                            .lexically_normal()
                                            .generic_string();
                plan.symbolicExecutable = artifact->symbolicArtifact;
                plan.prerequisites.push_back(artifact->identity);
            }
        }
        else
        {
            const auto separator = selected->executable.rfind("::");
            const auto active =
                separator == std::string::npos
                    ? graph.Data().exports.end()
                    : std::ranges::find_if(graph.Data().exports, [&](const GraphExport &candidate) {
                          const auto package = std::ranges::find(graph.Data().packages, candidate.packageInstance,
                                                                 &GraphPackageInstance::identity);
                          return package != graph.Data().packages.end() && candidate.kind == ExportUseKind::Tool &&
                                 package->coordinate.name == selected->executable.substr(0, separator) &&
                                 candidate.name == selected->executable.substr(separator + 2);
                      });
            if (active == graph.Data().exports.end())
            {
                AddError(result.diagnostics, "NGIN7210",
                         "Launch Tool is not an active Tool Export: '" + selected->executable + "'",
                         selected->provenance);
                return result;
            }
            const auto found = bindings.toolArtifacts.find(selected->executable);
            plan.executable = found == bindings.toolArtifacts.end() ? "artifact:tool:" + selected->executable
                                                                    : found->second.lexically_normal().generic_string();
            plan.symbolicExecutable = found == bindings.toolArtifacts.end();
        }
        const auto variable = RuntimeVariable(graph.Data().selection.targetOperatingSystem);
        const auto libraryPath = (std::filesystem::path(stage.stageRoot) / "lib").lexically_normal().generic_string();
        if (const auto current = plan.environment.find(variable); current != plan.environment.end())
            current->second =
                libraryPath + RuntimeSeparator(graph.Data().selection.targetOperatingSystem) + current->second;
        else
            plan.environment.emplace(variable, libraryPath);
        if (!result.diagnostics.empty()) return result;
        std::ranges::sort(plan.prerequisites);
        plan.plan.identity = FingerprintLaunchPlan(plan);
        result.plan = std::move(plan);
        return result;
    }

    auto DeriveTestPlan(const ResolvedCompositionGraph &graph, const StagePlan &stage) -> DeploymentPlanResult<TestPlan>
    {
        DeploymentPlanResult<TestPlan> result{};
        if (!graph.Data().testing.has_value())
        {
            AddError(result.diagnostics, "NGIN7220", "project has no Testing intent and is not a Test product");
            return result;
        }
        const auto *artifact = ProductArtifact(stage, graph.Data().product.identity);
        if (artifact == nullptr)
        {
            AddError(result.diagnostics, "NGIN7220", "TestPlan requires a product artifact");
            return result;
        }
        TestPlan plan{.plan = PlanIdentity{.kind = "TestPlan",
                                           .compositionIdentity = graph.CompositionIdentity(),
                                           .adapter = "NGIN.Test",
                                           .adapterVersion = "1"},
                      .executable = artifact->symbolicArtifact
                                        ? artifact->source
                                        : (std::filesystem::path(stage.stageRoot) / artifact->destination)
                                              .lexically_normal()
                                              .generic_string(),
                      .symbolicExecutable = artifact->symbolicArtifact,
                      .arguments = graph.Data().testing->arguments,
                      .timeoutSeconds = graph.Data().testing->timeoutSeconds};
        for (const auto &edge : graph.Data().edges)
            if (edge.kind == "ProjectDependency" && edge.context == "Test") plan.dependencyInstances.push_back(edge.to);
        std::ranges::sort(plan.dependencyInstances);
        plan.dependencyInstances.erase(std::unique(plan.dependencyInstances.begin(), plan.dependencyInstances.end()),
                                       plan.dependencyInstances.end());
        plan.plan.identity = FingerprintTestPlan(plan);
        result.plan = std::move(plan);
        return result;
    }

    auto DerivePublishPlan(const ResolvedCompositionGraph &graph, const StagePlan &stage,
                           const std::string_view publishName) -> DeploymentPlanResult<PublishPlan>
    {
        DeploymentPlanResult<PublishPlan> result{};
        const auto found = std::ranges::find(graph.Data().publishes, publishName, &GraphPublish::name);
        if (found == graph.Data().publishes.end())
        {
            AddError(result.diagnostics, "NGIN7230", "unknown Publish '" + std::string(publishName) + "'");
            return result;
        }
        const auto values = std::map<std::string, PlaceholderValue, std::less<>>{
            {"project.name", {PlaceholderType::Identifier, graph.Data().product.name}},
            {"project.version", {PlaceholderType::SemanticVersion, graph.Data().product.version.value_or("0.0.0")}},
            {"configuration", {PlaceholderType::Identifier, graph.Data().selection.configuration}},
            {"target.os", {PlaceholderType::Identifier, graph.Data().selection.targetOperatingSystem}},
            {"target.architecture", {PlaceholderType::Identifier, graph.Data().selection.targetArchitecture}},
            {"output.name", {PlaceholderType::Filename, graph.Data().product.name}}};
        const auto output = ExpandPlaceholders(found->output, PlaceholderPhase::Publish, values, true);
        if (!output.Succeeded())
        {
            result.diagnostics = output.diagnostics;
            return result;
        }
        const auto normalizedOutput = NormalizeStageDestination(*output.value);
        if (!normalizedOutput.Succeeded())
        {
            result.diagnostics = normalizedOutput.diagnostics;
            return result;
        }
        PublishPlan plan{.plan = PlanIdentity{.kind = "PublishPlan",
                                              .compositionIdentity = graph.CompositionIdentity(),
                                              .adapter = "NGIN.Publisher",
                                              .adapterVersion = "1"},
                         .name = found->name,
                         .outputKind = found->outputKind,
                         .format = found->format,
                         .output = normalizedOutput.value->value,
                         .license = graph.Data().product.license};
        for (const auto &item : stage.items)
            plan.inputs.push_back(PublishPlanInput{.stageItem = item.identity,
                                                   .owner = item.owner,
                                                   .category = PublishCategory(item.kind),
                                                   .source = item.source,
                                                   .destination = item.destination,
                                                   .reason = item.reason});
        for (const auto &edge : graph.Data().edges)
            if (edge.kind == "ProjectDependency" && edge.context == "Publish" && edge.scope == publishName)
                plan.dependencyInstances.push_back(edge.to);
        std::ranges::sort(plan.inputs, {}, &PublishPlanInput::stageItem);
        std::ranges::sort(plan.dependencyInstances);
        plan.dependencyInstances.erase(std::unique(plan.dependencyInstances.begin(), plan.dependencyInstances.end()),
                                       plan.dependencyInstances.end());
        plan.plan.identity = FingerprintPublishPlan(plan);
        result.plan = std::move(plan);
        return result;
    }

    auto SerializeStagePlan(const StagePlan &plan) -> std::string
    {
        CanonicalValue::Array items{};
        for (const auto &item : plan.items) items.push_back(StageItemValue(item));
        return SerializeCanonical(CanonicalValue::Object{{"items", items},
                                                         {"kind", "NGIN.StagePlan"},
                                                         {"plan", IdentityValue(plan.plan)},
                                                         {"stageRoot", plan.stageRoot}});
    }

    auto SerializeLaunchPlan(const LaunchPlan &plan) -> std::string
    {
        return SerializeCanonical(CanonicalValue::Object{{"arguments", StringArray(plan.arguments)},
                                                         {"environment", StringMap(plan.environment)},
                                                         {"executable", plan.executable},
                                                         {"kind", "NGIN.LaunchPlan"},
                                                         {"name", plan.name},
                                                         {"plan", IdentityValue(plan.plan)},
                                                         {"prerequisites", StringArray(plan.prerequisites)},
                                                         {"secretReferences", StringMap(plan.secretReferences)},
                                                         {"symbolicExecutable", plan.symbolicExecutable},
                                                         {"workingDirectory", plan.workingDirectory}});
    }

    auto SerializeTestPlan(const TestPlan &plan) -> std::string
    {
        return SerializeCanonical(CanonicalValue::Object{{"arguments", StringArray(plan.arguments)},
                                                         {"dependencyInstances", StringArray(plan.dependencyInstances)},
                                                         {"executable", plan.executable},
                                                         {"kind", "NGIN.TestPlan"},
                                                         {"plan", IdentityValue(plan.plan)},
                                                         {"symbolicExecutable", plan.symbolicExecutable},
                                                         {"timeoutSeconds", plan.timeoutSeconds.has_value()
                                                                                ? CanonicalValue{*plan.timeoutSeconds}
                                                                                : CanonicalValue{nullptr}}});
    }

    auto SerializePublishPlan(const PublishPlan &plan) -> std::string
    {
        CanonicalValue::Array inputs{};
        for (const auto &input : plan.inputs)
            inputs.push_back(CanonicalValue::Object{{"category", input.category},
                                                    {"destination", input.destination},
                                                    {"owner", input.owner},
                                                    {"reason", input.reason},
                                                    {"source", input.source},
                                                    {"stageItem", input.stageItem}});
        CanonicalValue::Object root{{"dependencyInstances", StringArray(plan.dependencyInstances)},
                                    {"format", plan.format},
                                    {"inputs", inputs},
                                    {"kind", "NGIN.PublishPlan"},
                                    {"name", plan.name},
                                    {"output", plan.output},
                                    {"outputKind", plan.outputKind},
                                    {"plan", IdentityValue(plan.plan)}};
        if (plan.license.has_value()) root.emplace("license", *plan.license);
        return SerializeCanonical(root);
    }

    auto FingerprintStagePlan(const StagePlan &plan) -> std::string
    {
        return Fingerprint(plan, "StagePlanFingerprint", SerializeStagePlan);
    }
    auto FingerprintLaunchPlan(const LaunchPlan &plan) -> std::string
    {
        return Fingerprint(plan, "LaunchPlanFingerprint", SerializeLaunchPlan);
    }
    auto FingerprintTestPlan(const TestPlan &plan) -> std::string
    {
        return Fingerprint(plan, "TestPlanFingerprint", SerializeTestPlan);
    }
    auto FingerprintPublishPlan(const PublishPlan &plan) -> std::string
    {
        return Fingerprint(plan, "PublishPlanFingerprint", SerializePublishPlan);
    }

    auto ExecuteStagePlan(const StagePlan &plan) -> StageExecutionResult
    {
        StageExecutionResult result{};
        if (plan.stageRoot.empty())
        {
            AddError(result.diagnostics, "NGIN7240", "StagePlan has an empty stage root");
            return result;
        }
        std::error_code error{};
        const auto root = std::filesystem::weakly_canonical(std::filesystem::path(plan.stageRoot), error);
        if (error)
        {
            std::filesystem::create_directories(plan.stageRoot, error);
            if (!error) return ExecuteStagePlan(plan);
            AddError(result.diagnostics, "NGIN7240", "cannot create stage root: " + error.message());
            return result;
        }
        for (const auto &item : plan.items)
        {
            if (item.symbolicArtifact)
            {
                AddError(result.diagnostics, "NGIN7240", "stage artifact binding is unresolved: " + item.source,
                         item.provenance);
                continue;
            }
            const auto source = std::filesystem::path(item.source);
            if (!std::filesystem::is_regular_file(source, error))
            {
                AddError(result.diagnostics, "NGIN7240", "stage source is missing: " + item.source, item.provenance);
                continue;
            }
            const auto destination =
                std::filesystem::weakly_canonical(root / std::filesystem::path(item.destination), error);
            if (error)
            {
                error.clear();
                const auto parent = (root / std::filesystem::path(item.destination)).parent_path();
                std::filesystem::create_directories(parent, error);
                if (!error)
                {
                    const auto target = (root / std::filesystem::path(item.destination)).lexically_normal();
                    const auto relative = target.lexically_relative(root);
                    if (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..")
                    {
                        std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing,
                                                   error);
                        if (!error) result.written.push_back(target);
                    }
                }
            }
            else
            {
                const auto relative = destination.lexically_relative(root);
                if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
                    error = std::make_error_code(std::errc::permission_denied);
                else
                {
                    std::filesystem::create_directories(destination.parent_path(), error);
                    if (!error)
                        std::filesystem::copy_file(source, destination,
                                                   std::filesystem::copy_options::overwrite_existing, error);
                    if (!error) result.written.push_back(destination);
                }
            }
            if (error)
            {
                AddError(result.diagnostics, "NGIN7240", "cannot stage '" + item.destination + "': " + error.message(),
                         item.provenance);
                error.clear();
            }
        }
        std::ranges::sort(result.written);
        return result;
    }

    auto GenerateCPackConfiguration(const PublishPlan &plan) -> std::string
    {
        const auto generator = plan.outputKind == "Archive"     ? plan.format == "zip" ? "ZIP" : "TGZ"
                               : plan.outputKind == "Installer" ? plan.format == "msi" ? "WIX" : "DEB"
                                                                : "External";
        std::ostringstream out{};
        out << "# Generated exclusively from NGIN.PublishPlan " << plan.plan.identity << "\n";
        out << "set(CPACK_GENERATOR \"" << generator << "\")\n";
        out << "set(CPACK_PACKAGE_FILE_NAME \"" << std::filesystem::path(plan.output).stem().generic_string()
            << "\")\n";
        out << "set(CPACK_PACKAGE_DIRECTORY \"" << std::filesystem::path(plan.output).parent_path().generic_string()
            << "\")\n";
        if (plan.license.has_value())
            out << "set(CPACK_PACKAGE_DESCRIPTION_SUMMARY \"License: " << *plan.license << "\")\n";
        for (const auto &input : plan.inputs)
        {
            const auto destination = std::filesystem::path(input.destination);
            out << "install(FILES \"" << input.source << "\" DESTINATION \""
                << destination.parent_path().generic_string() << "\" RENAME \""
                << destination.filename().generic_string() << "\" COMPONENT Runtime)\n";
        }
        out << "include(CPack)\n";
        return out.str();
    }
} // namespace NGIN::CLI
