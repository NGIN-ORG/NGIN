#include "CMakeIntegration.hpp"

#include "ManifestPaths.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace NGIN::CLI
{
    namespace
    {
        auto AddError(std::vector<ManifestDiagnostic> &diagnostics, std::string code, std::string message,
                      const ManifestSourceRange &source = {}, std::vector<ManifestSourceRange> related = {}) -> void
        {
            diagnostics.push_back(ManifestDiagnostic{.severity = ManifestDiagnosticSeverity::Error,
                                                       .code = std::move(code),
                                                       .message = std::move(message),
                                                       .source = source,
                                                       .relatedSources = std::move(related)});
        }

        [[nodiscard]] auto Attribute(const AuthoredElement &node, const std::string_view name,
                                     const std::string_view fallback = {}) -> std::string
        {
            if (const auto *attribute = node.Attribute(name)) return attribute->value;
            return std::string(fallback);
        }

        [[nodiscard]] auto BooleanAttribute(const AuthoredElement &node, const std::string_view name,
                                            const bool fallback) -> bool
        {
            const auto value = Attribute(node, name);
            if (value.empty()) return fallback;
            return value == "true" || value == "True" || value == "1" || value == "ON";
        }

        [[nodiscard]] auto ChildElements(const AuthoredElement &node, const std::string_view specId)
            -> std::vector<const AuthoredElement *>
        {
            std::vector<const AuthoredElement *> result{};
            for (const auto &child : node.children)
                if (child.specId == specId) result.push_back(&child);
            return result;
        }

        [[nodiscard]] auto OptionText(const TypedOptionValue &value) -> std::string
        {
            return std::visit([](const auto &stored) -> std::string {
                using T = std::decay_t<decltype(stored)>;
                if constexpr (std::is_same_v<T, bool>) return stored ? "true" : "false";
                else if constexpr (std::is_same_v<T, std::int64_t>) return std::to_string(stored);
                else if constexpr (std::is_same_v<T, PortablePath>) return stored.value;
                else return stored;
            }, value.value);
        }

        [[nodiscard]] auto Matches(const AuthoredElement &condition, const SelectionFacts &facts,
                                   const ResolvedPackageOptions &options) -> bool
        {
            const auto match = [&](const std::string_view attribute, const std::string_view actual) {
                const auto expected = Attribute(condition, attribute);
                return expected.empty() || expected == actual;
            };
            if (!match("Configuration", facts.configuration.name) || !match("Target", facts.target.name) ||
                !match("OS", facts.target.operatingSystem) || !match("Architecture", facts.target.architecture) ||
                !match("Toolchain", facts.toolchain.name) || !match("Compiler", facts.toolchain.compiler))
                return false;
            const auto option = Attribute(condition, "Option");
            if (option.empty()) return true;
            const auto found = options.values.find(option);
            return found != options.values.end() && OptionText(found->second) == Attribute(condition, "Equals");
        }

        [[nodiscard]] auto IntegrationKind(const std::string_view specId) -> std::optional<CMakeIntegrationKind>
        {
            if (specId == "cmake.add-subdirectory") return CMakeIntegrationKind::AddSubdirectory;
            if (specId == "cmake.isolated") return CMakeIntegrationKind::Isolated;
            if (specId == "cmake.find-package") return CMakeIntegrationKind::FindPackage;
            if (specId == "cmake.manual") return CMakeIntegrationKind::Manual;
            return std::nullopt;
        }

        auto MergeCache(std::map<std::string, CMakeCacheBinding, std::less<>> &cache,
                        const CMakeCacheBinding &candidate, const ManifestSourceRange &source,
                        std::map<std::string, ManifestSourceRange, std::less<>> &sources,
                        std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            const auto found = cache.find(candidate.name);
            if (found == cache.end())
            {
                cache.emplace(candidate.name, candidate);
                sources.emplace(candidate.name, source);
                return;
            }
            if (found->second.value != candidate.value || found->second.type != candidate.type ||
                found->second.artifact != candidate.artifact)
                AddError(diagnostics, "NGIN7003", "conflicting CMake cache binding '" + candidate.name + "'",
                         source, {sources.at(candidate.name)});
        }

        auto MergeTarget(std::map<std::string, CMakeTargetBinding, std::less<>> &targets,
                         const CMakeTargetBinding &candidate, const ManifestSourceRange &source,
                         std::map<std::string, ManifestSourceRange, std::less<>> &sources,
                         std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            const auto found = targets.find(candidate.exportName);
            if (found == targets.end())
            {
                targets.emplace(candidate.exportName, candidate);
                sources.emplace(candidate.exportName, source);
                return;
            }
            if (found->second.targetName != candidate.targetName)
                AddError(diagnostics, "NGIN7004", "conflicting CMake target mapping for Export '" +
                                                       candidate.exportName + "'",
                         source, {sources.at(candidate.exportName)});
        }

        auto ApplyEntries(const AuthoredElement &container, const SemanticPackage &package,
                          const PackageInstance &instance, const ResolvedPackageOptions &options,
                          std::map<std::string, CMakeCacheBinding, std::less<>> &cache,
                          std::map<std::string, CMakeTargetBinding, std::less<>> &targets,
                          std::map<std::string, ManifestSourceRange, std::less<>> &cacheSources,
                          std::map<std::string, ManifestSourceRange, std::less<>> &targetSources,
                          std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            for (const auto &node : container.children)
            {
                if (node.specId == "cmake.cache")
                {
                    MergeCache(cache,
                               CMakeCacheBinding{.name = Attribute(node, "Name"),
                                                 .value = Attribute(node, "Value"),
                                                 .type = Attribute(node, "Type", "STRING"),
                                                 .artifact = BooleanAttribute(node, "Artifact", false)},
                               node.source, cacheSources, diagnostics);
                }
                else if (node.specId == "cmake.map-option")
                {
                    const auto optionName = Attribute(node, "Option");
                    const auto definition = package.options.find(optionName);
                    const auto value = options.values.find(optionName);
                    if (definition == package.options.end() || value == options.values.end())
                    {
                        AddError(diagnostics, "NGIN7005", "CMake MapOption references unknown Option '" + optionName +
                                                               "'",
                                 node.source);
                        continue;
                    }
                    const auto artifact = BooleanAttribute(node, "Artifact", false);
                    if (artifact != definition->second.artifact)
                    {
                        AddError(diagnostics, "NGIN7005", "CMake MapOption Artifact disagrees with Option '" +
                                                               optionName + "'",
                                 node.source, {definition->second.source});
                        continue;
                    }
                    const auto canonical = OptionText(value->second);
                    auto mapped = Attribute(node, "Value");
                    if (mapped.empty())
                    {
                        if (canonical == "true") mapped = Attribute(node, "True", "ON");
                        else if (canonical == "false") mapped = Attribute(node, "False", "OFF");
                        else mapped = canonical;
                    }
                    const auto marker = mapped.find("${option.value}");
                    if (marker != std::string::npos) mapped.replace(marker, 15, canonical);
                    MergeCache(cache,
                               CMakeCacheBinding{.name = Attribute(node, "Cache"),
                                                 .value = std::move(mapped),
                                                 .type = "STRING",
                                                 .artifact = artifact},
                               node.source, cacheSources, diagnostics);
                }
                else if (node.specId == "cmake.target")
                {
                    const auto exportName = Attribute(node, "Export");
                    const auto exportModel = package.exports.find(exportName);
                    if (exportModel == package.exports.end())
                    {
                        AddError(diagnostics, "NGIN7006", "CMake Target references unknown Export '" + exportName +
                                                               "'",
                                 node.source);
                        continue;
                    }
                    if (exportModel->second.kind == ExportUseKind::Action ||
                        exportModel->second.kind == ExportUseKind::Asset)
                    {
                        AddError(diagnostics, "NGIN7006", "CMake Target cannot map " +
                                                               std::string(exportModel->second.kind == ExportUseKind::Action
                                                                               ? "Action"
                                                                               : "Asset") +
                                                               " Export '" + exportName + "'",
                                 node.source, {exportModel->second.source});
                        continue;
                    }
                    MergeTarget(targets,
                                CMakeTargetBinding{.exportIdentity = instance.identity + "::" + exportName,
                                                   .exportName = exportName,
                                                   .targetName = Attribute(node, "Name")},
                                node.source, targetSources, diagnostics);
                }
            }
        }

        [[nodiscard]] auto FindPackage(const AuthoredElement &node) -> CMakeFindPackageBinding
        {
            CMakeFindPackageBinding result{.name = Attribute(node, "Name"),
                                           .config = BooleanAttribute(node, "Config", false),
                                           .required = BooleanAttribute(node, "Required", true)};
            const auto version = Attribute(node, "Version");
            if (!version.empty()) result.version = version;
            return result;
        }
    }

    auto CMakeBindingResolution::Succeeded() const -> bool { return diagnostics.empty(); }

    ResolvedCMakeIntegrationBindings::ResolvedCMakeIntegrationBindings(
        std::vector<CMakeIntegrationBindings> bindings)
    {
        std::ranges::sort(bindings, {}, &CMakeIntegrationBindings::packageInstance);
        bindings_ = std::make_shared<const std::vector<CMakeIntegrationBindings>>(std::move(bindings));
    }

    auto ResolvedCMakeIntegrationBindings::Data() const -> const std::vector<CMakeIntegrationBindings> &
    {
        return *bindings_;
    }

    auto CMakeIntegrationKindName(const CMakeIntegrationKind kind) -> std::string_view
    {
        switch (kind)
        {
        case CMakeIntegrationKind::AddSubdirectory: return "AddSubdirectory";
        case CMakeIntegrationKind::Isolated: return "Isolated";
        case CMakeIntegrationKind::FindPackage: return "FindPackage";
        case CMakeIntegrationKind::Manual: return "Manual";
        case CMakeIntegrationKind::Cps: return "CPS";
        }
        return "AddSubdirectory";
    }

    auto ResolveCMakeIntegration(const AuthoredPackageManifest &authored, const SemanticPackage &package,
                                 const PackageProviderResult &provider, const PackageInstance &instance,
                                 const ActivePackageExports &activation, const SelectionFacts &selection,
                                 const ResolvedPackageOptions &options) -> CMakeBindingResolution
    {
        CMakeBindingResolution result{};
        const auto integrations = ChildElements(authored.root, "package.adapters");
        if (integrations.empty())
        {
            CMakeIntegrationBindings bindings{
                .packageInstance = instance.identity,
                .kind = CMakeIntegrationKind::Cps,
                .provenance = IntegrationBindingProvenance{
                    .document = authored.manifest.path.filename().generic_string(),
                    .reason = "CPS component import"}};
            for (const auto &name : activation.exports)
            {
                const auto &exportModel = package.exports.at(name);
                if (!exportModel.cps.has_value()) continue;
                const auto &component = *exportModel.cps;
                CMakeTargetBinding target{.exportIdentity = instance.identity + "::" + name,
                                          .exportName = name,
                                          .targetName = package.coordinate.name + "::" + name,
                                          .importedKind = component.type};
                if (component.location.has_value())
                    target.location = *component.location;
                for (const auto &include : component.includeDirectories)
                    target.includeDirectories.push_back(include);
                target.compileDefinitions = component.compileDefinitions;
                target.compileOptions = component.compileOptions;
                target.linkOptions = component.linkOptions;
                if (component.type != "interface" && component.type != "symbolic" && target.location.empty())
                    AddError(result.diagnostics, "NGIN7006", "CPS component '" + name +
                                                               "' requires a location for CMake consumption",
                             exportModel.source);
                bindings.targets.push_back(std::move(target));
            }
            if (!bindings.targets.empty() && result.diagnostics.empty()) result.value = std::move(bindings);
            return result;
        }
        std::vector<const AuthoredElement *> roots{};
        for (const auto &child : integrations.front()->children)
            if (IntegrationKind(child.specId).has_value()) roots.push_back(&child);
        if (roots.size() != 1)
        {
            AddError(result.diagnostics, "NGIN7001", "Package must declare exactly one CMake integration",
                     integrations.front()->source);
            return result;
        }
        const auto &root = *roots.front();
        CMakeIntegrationBindings bindings{.packageInstance = instance.identity,
                                          .kind = *IntegrationKind(root.specId),
                                          .provenance = IntegrationBindingProvenance{
                                              .document = authored.manifest.path.filename().generic_string(),
                                              .line = root.source.begin.line,
                                              .column = root.source.begin.column,
                                              .reason = "selected CMake integration"}};
        if (bindings.kind != CMakeIntegrationKind::FindPackage)
        {
            const auto authoredSource = Attribute(root, "Source");
            auto normalized = authoredSource == "."
                                  ? PortablePathResult{.value = PortablePath{.value = ".",
                                                                            .base = PortablePathBase::Manifest}}
                                  : NormalizePortablePath(authoredSource, PortablePathBase::Manifest, root.source);
            if (!normalized.Succeeded())
            {
                result.diagnostics.insert(result.diagnostics.end(), normalized.diagnostics.begin(),
                                          normalized.diagnostics.end());
                return result;
            }
            bindings.source = (provider.root / fs::path(normalized.value->value)).lexically_normal();
            const auto normalizedRoot = provider.root.lexically_normal();
            const auto relative = bindings.source.lexically_relative(normalizedRoot);
            if (relative.is_absolute() || (!relative.empty() && *relative.begin() == ".."))
            {
                AddError(result.diagnostics, "NGIN7002", "CMake integration Source '" + bindings.source.generic_string() +
                                                              "' escapes PackageProvider root '" + normalizedRoot.generic_string() + "'",
                         root.source);
                return result;
            }
            if (!fs::exists(bindings.source / "CMakeLists.txt"))
            {
                AddError(result.diagnostics, "NGIN7002", "CMake integration Source has no CMakeLists.txt",
                         root.source);
                return result;
            }
        }

        std::map<std::string, CMakeCacheBinding, std::less<>> cache{};
        std::map<std::string, CMakeTargetBinding, std::less<>> targets{};
        std::map<std::string, ManifestSourceRange, std::less<>> cacheSources{};
        std::map<std::string, ManifestSourceRange, std::less<>> targetSources{};
        ApplyEntries(root, package, instance, options, cache, targets, cacheSources, targetSources,
                     result.diagnostics);

        for (const auto *node : ChildElements(root, "cmake.when"))
            if (Matches(*node, selection, options))
                ApplyEntries(*node, package, instance, options, cache, targets, cacheSources, targetSources,
                             result.diagnostics);

        const AuthoredElement *findNode = nullptr;
        if (bindings.kind == CMakeIntegrationKind::FindPackage) findNode = &root;
        else if (bindings.kind == CMakeIntegrationKind::Isolated)
        {
            const auto install = ChildElements(root, "cmake.install");
            bindings.installBeforeUse = !install.empty();
            const auto find = ChildElements(root, "cmake.find-package");
            if (!find.empty()) findNode = find.front();
            if (findNode != nullptr)
                ApplyEntries(*findNode, package, instance, options, cache, targets, cacheSources, targetSources,
                             result.diagnostics);
        }
        if (findNode != nullptr) bindings.findPackage = FindPackage(*findNode);

        std::set<std::string, std::less<>> active(activation.exports.begin(), activation.exports.end());
        for (const auto &name : active)
        {
            const auto kind = package.exports.at(name).kind;
            if (kind == ExportUseKind::Action || kind == ExportUseKind::Asset) continue;
            if (!targets.contains(name))
                AddError(result.diagnostics, "NGIN7006", "active Export '" + name +
                                                           "' has no CMake Target mapping",
                         package.exports.at(name).source, {root.source});
        }
        for (auto iterator = targets.begin(); iterator != targets.end();)
            if (!active.contains(iterator->first)) iterator = targets.erase(iterator);
            else ++iterator;
        for (const auto &[_, value] : cache) bindings.cache.push_back(value);
        for (const auto &[_, value] : targets) bindings.targets.push_back(value);
        if (!result.diagnostics.empty()) return result;
        result.value = std::move(bindings);
        return result;
    }
}
