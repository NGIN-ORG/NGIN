#include "CMakeAdapter.hpp"

#include "Canonical.hpp"
#include "CMakeIntegration.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>

namespace NGIN::CLI
{
    namespace
    {
        auto AddError(std::vector<ManifestDiagnostic> &diagnostics, std::string code, std::string message) -> void
        {
            diagnostics.push_back(ManifestDiagnostic{.severity = ManifestDiagnosticSeverity::Error,
                                                       .code = std::move(code),
                                                       .message = std::move(message)});
        }

        [[nodiscard]] auto TargetName(std::string value) -> std::string
        {
            for (auto &character : value)
                if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_' && character != '.')
                    character = '_';
            return value;
        }

        [[nodiscard]] auto TargetKind(const GraphProduct &product) -> std::optional<std::string>
        {
            if (product.type == ProductType::Application || product.type == ProductType::Tool ||
                product.type == ProductType::Test || product.type == ProductType::Benchmark)
                return "Executable";
            if (product.type == ProductType::Plugin) return "ModuleLibrary";
            if (product.type == ProductType::Library)
            {
                if (product.linkage == LibraryLinkage::Static) return "StaticLibrary";
                if (product.linkage == LibraryLinkage::Shared) return "SharedLibrary";
                if (product.linkage == LibraryLinkage::Interface) return "InterfaceLibrary";
            }
            return std::nullopt;
        }

        template <typename T>
        auto SortByIdentity(std::vector<T> &values) -> void
        {
            std::ranges::sort(values, {}, &T::identity);
        }

        [[nodiscard]] auto CacheSignature(const CMakeIntegrationBindings &binding) -> std::string
        {
            CanonicalValue::Array values{};
            for (const auto &cache : binding.cache)
                values.push_back(CanonicalValue::Object{{"artifact", cache.artifact},
                                                        {"name", cache.name},
                                                        {"type", cache.type},
                                                        {"value", cache.value}});
            return SerializeCanonical(values);
        }

        [[nodiscard]] auto Escape(const std::string_view value) -> std::string
        {
            std::string result{};
            for (const auto character : value)
            {
                if (character == '\\' || character == '"') result.push_back('\\');
                result.push_back(character);
            }
            return result;
        }

        [[nodiscard]] auto Scope(const std::string &visibility, const bool interfaceTarget) -> std::string
        {
            if (interfaceTarget) return "INTERFACE";
            if (visibility == "Public") return "PUBLIC";
            if (visibility == "Interface") return "INTERFACE";
            return "PRIVATE";
        }
    }

    auto CMakePlanResult::Succeeded() const -> bool
    {
        return build.has_value() && actions.has_value() && diagnostics.empty();
    }

    auto DeriveCMakePlans(const ResolvedCompositionGraph &graph,
                          const ResolvedCMakeIntegrationBindings &resolvedBindings,
                          const CMakeAdapterContext &context) -> CMakePlanResult
    {
        CMakePlanResult result{};
        const auto &capabilities = context.capabilities;
        const auto &data = graph.Data();
        const auto targetKind = TargetKind(data.product);
        if (!targetKind.has_value())
        {
            AddError(result.diagnostics, "NGIN7101", "CMake adapter does not support the resolved product kind/linkage");
            return result;
        }
        BuildPlan build{.productGraphIdentity = data.product.identity,
                        .targetName = TargetName(data.product.name),
                        .targetKind = *targetKind,
                        .generator = context.generator,
                        .toolchainFile = context.toolchainFile,
                        .multiConfiguration = context.multiConfiguration,
                        .crossCompiling = context.crossCompiling};
        ActionPlan actions{};
        const auto composition = graph.CompositionIdentity();
        build.plan = PlanIdentity{.kind = "BuildPlan",
                                  .compositionIdentity = composition,
                                  .adapter = "CMake",
                                  .adapterVersion = capabilities.adapterVersion};
        actions.plan = PlanIdentity{.kind = "ActionPlan",
                                    .compositionIdentity = composition,
                                    .adapter = "CMake",
                                    .adapterVersion = capabilities.adapterVersion};
        if (context.crossCompiling && !capabilities.crossCompilation)
            AddError(result.diagnostics, "NGIN7104", "selected CMake adapter cannot represent cross compilation");
        if (context.multiConfiguration && !capabilities.multiConfiguration)
            AddError(result.diagnostics, "NGIN7104",
                     "selected CMake adapter cannot represent a multi-configuration generator");

        std::map<std::string, const CMakeIntegrationBindings *, std::less<>> byInstance{};
        std::map<std::string, const CMakeTargetBinding *, std::less<>> byExport{};
        std::map<std::string, std::string, std::less<>> targetOwners{};
        std::map<std::string, std::string, std::less<>> sourceInputs{};
        for (const auto &binding : resolvedBindings.Data())
        {
            if (!byInstance.emplace(binding.packageInstance, &binding).second)
                AddError(result.diagnostics, "NGIN7102", "multiple CMake bindings target PackageInstance '" +
                                                              binding.packageInstance + "'");
            for (const auto &target : binding.targets)
            {
                if (!byExport.emplace(target.exportIdentity, &target).second)
                    AddError(result.diagnostics, "NGIN7102", "multiple CMake targets map Export '" +
                                                                  target.exportIdentity + "'");
                const auto owner = targetOwners.find(target.targetName);
                if (owner != targetOwners.end() && owner->second != target.exportIdentity)
                    AddError(result.diagnostics, "NGIN7102", "CMake target '" + target.targetName +
                                                                  "' is produced by multiple Exports");
                else targetOwners[target.targetName] = target.exportIdentity;
            }
            if (!binding.source.empty())
            {
                const auto source = binding.source.generic_string();
                const auto signature = CacheSignature(binding);
                const auto prior = sourceInputs.find(source);
                if (prior != sourceInputs.end() && prior->second != signature)
                    AddError(result.diagnostics, "NGIN7103", "CMake source is reused with incompatible cache inputs: " +
                                                                  source);
                else sourceInputs[source] = signature;
            }
            build.packages.push_back(CMakePackagePlan{
                .identity = "CMakePackage:" + binding.packageInstance,
                .packageInstance = binding.packageInstance,
                .kind = binding.kind,
                .source = binding.source.generic_string(),
                .binaryDirectory = "packages/" + TargetName(binding.packageInstance),
                .installedPrefix = binding.kind == CMakeIntegrationKind::Isolated
                                       ? "packages/" + TargetName(binding.packageInstance) + "/install"
                                       : std::string{},
                .cache = binding.cache,
                .findPackage = binding.findPackage,
                .installBeforeUse = binding.installBeforeUse,
                .provenance = binding.provenance});
        }

        std::map<std::string, PackageInstanceContext, std::less<>> contexts{};
        for (const auto &package : data.packages) contexts.emplace(package.identity, package.context);
        for (const auto &item : data.buildItems)
        {
            if (item.kind == "CxxModule" && !capabilities.cxxModules)
                AddError(result.diagnostics, "NGIN7104", "selected CMake adapter cannot represent C++ module item '" +
                                                              item.path + "'");
            build.items.push_back(BuildPlanItem{.identity = "CMakeItem:" + item.identity,
                                                .graphIdentity = item.identity,
                                                .operation = item.kind,
                                                .value = item.path,
                                                .visibility = item.visibility,
                                                .generated = item.generated,
                                                .provenance = item.provenance});
        }
        for (const auto &item : data.exports)
        {
            if (item.kind != ExportUseKind::Library || contexts.at(item.packageInstance) != PackageInstanceContext::Target)
                continue;
            const auto target = byExport.find(item.identity);
            if (target == byExport.end())
            {
                AddError(result.diagnostics, "NGIN7105", "active Library Export has no CMake target binding: " +
                                                              item.identity);
                continue;
            }
            build.links.push_back(BuildPlanLink{.identity = "CMakeLink:" + item.identity,
                                                .graphIdentity = item.identity,
                                                .targetName = target->second->targetName,
                                                .visibility = "Private",
                                                .provenance = item.provenance});
        }
        for (const auto &action : data.actions)
        {
            const auto toolIdentity = action.packageInstance + "::" + action.toolExport;
            const auto tool = byExport.find(toolIdentity);
            if (tool == byExport.end())
            {
                AddError(result.diagnostics, "NGIN7106", "Action host Tool has no CMake target binding: " +
                                                              toolIdentity);
                continue;
            }
            actions.steps.push_back(ActionPlanStep{.identity = "CMakeAction:" + action.identity,
                                                   .graphIdentity = action.identity,
                                                   .kind = action.kind,
                                                   .toolGraphIdentity = toolIdentity,
                                                   .toolTarget = tool->second->targetName,
                                                   .deterministic = action.deterministic,
                                                   .outputs = action.outputs,
                                                   .provenance = action.provenance});
            build.actionDependencies.push_back(action.identity);
        }
        if (!result.diagnostics.empty()) return result;
        SortByIdentity(build.items);
        SortByIdentity(build.links);
        SortByIdentity(build.packages);
        SortByIdentity(actions.steps);
        std::ranges::sort(build.actionDependencies);
        build.plan.identity = FingerprintBuildPlan(build);
        actions.plan.identity = FingerprintActionPlan(actions);
        result.build = std::move(build);
        result.actions = std::move(actions);
        return result;
    }

    auto GenerateCMakeProject(const BuildPlan &plan, const ActionPlan &actions) -> std::string
    {
        std::ostringstream out{};
        out << "cmake_minimum_required(VERSION 3.28)\n";
        out << "project(" << plan.targetName << " LANGUAGES CXX)\n\n";
        for (const auto &package : plan.packages)
        {
            for (const auto &cache : package.cache)
                out << "set(" << cache.name << " \"" << Escape(cache.value) << "\" CACHE " << cache.type
                    << " \"NGIN binding\" FORCE)\n";
            if (package.kind == CMakeIntegrationKind::AddSubdirectory ||
                package.kind == CMakeIntegrationKind::Manual)
                out << "add_subdirectory(\"" << Escape(package.source) << "\" \""
                    << Escape(package.binaryDirectory) << "\")\n";
            if ((package.kind == CMakeIntegrationKind::FindPackage ||
                 package.kind == CMakeIntegrationKind::Isolated) && package.findPackage.has_value())
            {
                if (package.kind == CMakeIntegrationKind::Isolated)
                    out << "list(PREPEND CMAKE_PREFIX_PATH \"${CMAKE_BINARY_DIR}/"
                        << Escape(package.installedPrefix) << "\")\n";
                out << "find_package(" << package.findPackage->name;
                if (package.findPackage->version.has_value()) out << ' ' << *package.findPackage->version;
                if (package.findPackage->config) out << " CONFIG";
                if (package.findPackage->required) out << " REQUIRED";
                out << ")\n";
            }
        }
        out << '\n';
        if (plan.targetKind == "Executable") out << "add_executable(" << plan.targetName << ")\n";
        else if (plan.targetKind == "StaticLibrary") out << "add_library(" << plan.targetName << " STATIC)\n";
        else if (plan.targetKind == "SharedLibrary") out << "add_library(" << plan.targetName << " SHARED)\n";
        else if (plan.targetKind == "ModuleLibrary") out << "add_library(" << plan.targetName << " MODULE)\n";
        else out << "add_library(" << plan.targetName << " INTERFACE)\n";
        const auto interfaceTarget = plan.targetKind == "InterfaceLibrary";
        for (const auto &item : plan.items)
        {
            const auto scope = Scope(item.visibility, interfaceTarget);
            if (item.operation == "Source" || item.operation == "Header" || item.operation == "Resource")
                out << "target_sources(" << plan.targetName << ' ' << scope << " \"" << Escape(item.value) << "\")\n";
            else if (item.operation == "CxxModule")
                out << "target_sources(" << plan.targetName << ' ' << scope << " FILE_SET CXX_MODULES FILES \""
                    << Escape(item.value) << "\")\n";
            else if (item.operation == "IncludeDirectory")
                out << "target_include_directories(" << plan.targetName << ' ' << scope << " \""
                    << Escape(item.value) << "\")\n";
            else if (item.operation == "Define")
                out << "target_compile_definitions(" << plan.targetName << ' ' << scope << " \""
                    << Escape(item.value) << "\")\n";
            else if (item.operation == "CompileOption")
                out << "target_compile_options(" << plan.targetName << ' ' << scope << " \""
                    << Escape(item.value) << "\")\n";
            else if (item.operation == "LinkOption")
                out << "target_link_options(" << plan.targetName << ' ' << scope << " \""
                    << Escape(item.value) << "\")\n";
            else if (item.operation == "PrecompiledHeader")
                out << "target_precompile_headers(" << plan.targetName << ' ' << scope << " \""
                    << Escape(item.value) << "\")\n";
        }
        for (const auto &link : plan.links)
            out << "target_link_libraries(" << plan.targetName << ' ' << Scope(link.visibility, interfaceTarget) << ' '
                << link.targetName << ")\n";
        for (const auto &action : actions.steps)
            out << "add_dependencies(" << plan.targetName << ' ' << action.toolTarget << ")\n";
        return out.str();
    }
}
