#include "CMakeAdapter.hpp"

#include "CMakeIntegration.hpp"
#include "Canonical.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>

namespace NGIN::CLI
{
    namespace
    {
        auto AddError(std::vector<ManifestDiagnostic> &diagnostics, std::string code, std::string message) -> void
        {
            diagnostics.push_back(ManifestDiagnostic{
                .severity = ManifestDiagnosticSeverity::Error, .code = std::move(code), .message = std::move(message)});
        }

        [[nodiscard]] auto TargetName(std::string value) -> std::string
        {
            for (auto &character : value)
                if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_' && character != '.')
                    character = '_';
            return value;
        }

        [[nodiscard]] auto PackageDirectoryKey(const std::string_view packageInstance) -> std::string
        {
            const auto fingerprint = Sha256Fingerprint(packageInstance);
            return fingerprint.substr(std::string_view{"sha256:"}.size(), 16);
        }

        [[nodiscard]] auto TargetKind(const GraphProduct &product) -> std::optional<std::string>
        {
            if (product.artifactKind == ProductArtifactKind::Executable) return "Executable";
            if (product.artifactKind == ProductArtifactKind::Library)
            {
                if (product.libraryKind == LibraryKind::Static) return "StaticLibrary";
                if (product.libraryKind == LibraryKind::Shared) return "SharedLibrary";
                if (product.libraryKind == LibraryKind::Interface) return "InterfaceLibrary";
                if (product.libraryKind == LibraryKind::Plugin) return "ModuleLibrary";
            }
            return std::nullopt;
        }

        template <typename T> auto SortByIdentity(std::vector<T> &values) -> void
        {
            std::ranges::sort(values, {}, &T::identity);
        }

        [[nodiscard]] auto SortPackagesDependencyFirst(
            std::vector<CMakePackagePlan> &packages, const std::vector<GraphEdge> &edges,
            const std::map<std::string, std::string, std::less<>> &representatives) -> bool
        {
            std::map<std::string, std::size_t, std::less<>> indices{};
            std::map<std::string, std::size_t, std::less<>> incoming{};
            std::map<std::string, std::set<std::string, std::less<>>, std::less<>> dependents{};
            for (std::size_t index = 0; index < packages.size(); ++index)
            {
                indices.emplace(packages[index].packageInstance, index);
                incoming.emplace(packages[index].packageInstance, 0);
            }
            for (const auto &edge : edges)
            {
                if (edge.kind != "PackageRequirement") continue;
                const auto dependent = representatives.find(edge.from);
                const auto dependency = representatives.find(edge.to);
                if (dependent == representatives.end() || dependency == representatives.end() ||
                    dependent->second == dependency->second || !indices.contains(dependent->second) ||
                    !indices.contains(dependency->second))
                    continue;
                if (dependents[dependency->second].insert(dependent->second).second)
                    ++incoming[dependent->second];
            }

            std::set<std::string, std::less<>> ready{};
            for (const auto &[package, count] : incoming)
                if (count == 0) ready.insert(package);
            std::vector<CMakePackagePlan> ordered{};
            ordered.reserve(packages.size());
            while (!ready.empty())
            {
                const auto package = *ready.begin();
                ready.erase(ready.begin());
                ordered.push_back(std::move(packages[indices.at(package)]));
                for (const auto &dependent : dependents[package])
                    if (--incoming[dependent] == 0) ready.insert(dependent);
            }
            if (ordered.size() != packages.size()) return false;
            packages = std::move(ordered);
            return true;
        }

        [[nodiscard]] auto CacheSignature(const CMakeIntegrationBindings &binding) -> std::string
        {
            CanonicalValue::Array values{};
            for (const auto &cache : binding.cache)
                values.push_back(CanonicalValue::Object{
                    {"artifact", cache.artifact}, {"name", cache.name}, {"type", cache.type}, {"value", cache.value}});
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

        [[nodiscard]] auto CMakeList(const std::vector<std::string> &values) -> std::string
        {
            std::ostringstream result{};
            for (std::size_t index = 0; index < values.size(); ++index)
            {
                if (index != 0) result << ';';
                result << Escape(values[index]);
            }
            return result.str();
        }

        [[nodiscard]] auto ExpandActionArgument(std::string value, const CMakeAdapterContext &context,
                                                const std::string &contextFile) -> std::string
        {
            const auto replace = [&](const std::string_view name, const std::string_view replacement) {
                const auto token = "${" + std::string(name) + '}';
                for (auto position = value.find(token); position != std::string::npos; position = value.find(token))
                    value.replace(position, token.size(), replacement);
            };
            replace("ProjectDir", context.projectRoot);
            replace("BuildDir", context.buildRoot);
            replace("ActionOutputDir", context.actionOutputRoot);
            replace("ActionContext", contextFile);
            return value;
        }
    } // namespace

    auto CMakePlanResult::Succeeded() const -> bool
    {
        return build.has_value() && actions.has_value() && diagnostics.empty();
    }

    auto DeriveCMakePlans(const ResolvedCompositionGraph &graph,
                          const ResolvedCMakeIntegrationBindings &resolvedBindings, const CMakeAdapterContext &context)
        -> CMakePlanResult
    {
        CMakePlanResult result{};
        const auto &capabilities = context.capabilities;
        const auto &data = graph.Data();
        const auto targetKind = TargetKind(data.product);
        if (!targetKind.has_value())
        {
            AddError(result.diagnostics, "NGIN7101",
                     "CMake adapter does not support the resolved product kind/linkage");
            return result;
        }
        BuildPlan build{.productGraphIdentity = data.product.identity,
                        .targetName = TargetName(data.product.name),
                        .targetKind = *targetKind,
                        .configuration = data.selection.configuration,
                        .compiler = data.selection.compiler,
                        .optimization = data.selection.optimization,
                        .debugSymbols = data.selection.debugSymbols,
                        .linkTimeOptimization = data.selection.linkTimeOptimization,
                        .languageStandard = data.product.languageStandard,
                        .languageExtensions = data.product.languageExtensions,
                        .languageRequired = data.product.languageRequired,
                        .generator = context.generator,
                        .toolchainFile =
                            context.toolchainFile.has_value() ? context.toolchainFile : data.selection.toolchainFile,
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
                     "selected CMake adapter cannot represent a multi-configuration "
                     "generator");

        std::map<std::string, const CMakeIntegrationBindings *, std::less<>> byInstance{};
        std::map<std::string, const CMakeTargetBinding *, std::less<>> byExport{};
        std::map<std::string, std::string, std::less<>> targetOwners{};
        std::map<std::string, std::string, std::less<>> sourceInputs{};
        std::map<std::string, std::string, std::less<>> sourceRepresentatives{};
        std::map<std::string, std::string, std::less<>> packageRepresentatives{};
        std::map<std::string, PackageCoordinate, std::less<>> coordinates{};
        for (const auto &package : data.packages) coordinates.emplace(package.identity, package.coordinate);
        const auto equivalentExport = [&](const std::string &left, const std::string &right) {
            const auto leftSeparator = left.rfind("::");
            const auto rightSeparator = right.rfind("::");
            if (leftSeparator == std::string::npos || rightSeparator == std::string::npos ||
                left.substr(leftSeparator + 2) != right.substr(rightSeparator + 2))
                return false;
            const auto leftCoordinate = coordinates.find(left.substr(0, leftSeparator));
            const auto rightCoordinate = coordinates.find(right.substr(0, rightSeparator));
            return leftCoordinate != coordinates.end() && rightCoordinate != coordinates.end() &&
                   leftCoordinate->second.name == rightCoordinate->second.name &&
                   leftCoordinate->second.exactVersion == rightCoordinate->second.exactVersion;
        };
        for (const auto &binding : resolvedBindings.Data())
        {
            if (!byInstance.emplace(binding.packageInstance, &binding).second)
                AddError(result.diagnostics, "NGIN7102",
                         "multiple CMake bindings target PackageInstance '" + binding.packageInstance + "'");
            for (const auto &target : binding.targets)
            {
                if (!byExport.emplace(target.exportIdentity, &target).second)
                    AddError(result.diagnostics, "NGIN7102",
                             "multiple CMake targets map Export '" + target.exportIdentity + "'");
                const auto owner = targetOwners.find(target.targetName);
                if (owner != targetOwners.end() && owner->second != target.exportIdentity &&
                    !equivalentExport(owner->second, target.exportIdentity))
                    AddError(result.diagnostics, "NGIN7102",
                             "CMake target '" + target.targetName + "' is produced by multiple Exports");
                else
                    targetOwners[target.targetName] = target.exportIdentity;
            }
            bool addPackagePlan = true;
            auto representative = binding.packageInstance;
            if (!binding.source.empty())
            {
                const auto source = binding.source.generic_string();
                const auto signature = CacheSignature(binding);
                const auto prior = sourceInputs.find(source);
                if (prior != sourceInputs.end() && prior->second != signature)
                    AddError(result.diagnostics, "NGIN7103",
                             "CMake source is reused with incompatible cache inputs: " + source);
                else if (prior != sourceInputs.end())
                {
                    addPackagePlan = false;
                    representative = sourceRepresentatives.at(source);
                }
                else
                {
                    sourceInputs[source] = signature;
                    sourceRepresentatives[source] = binding.packageInstance;
                }
            }
            packageRepresentatives[binding.packageInstance] = representative;
            if (addPackagePlan)
                build.packages.push_back(CMakePackagePlan{
                    .identity = "CMakePackage:" + binding.packageInstance,
                    .packageInstance = binding.packageInstance,
                    .kind = binding.kind,
                    .source = binding.source.generic_string(),
                    .binaryDirectory = "packages/" + PackageDirectoryKey(binding.packageInstance),
                    .installedPrefix = binding.kind == CMakeIntegrationKind::Isolated
                                           ? "packages/" + PackageDirectoryKey(binding.packageInstance) + "/install"
                                           : std::string{},
                    .cache = binding.cache,
                    .targets = binding.targets,
                    .findPackage = binding.findPackage,
                    .installBeforeUse = binding.installBeforeUse,
                    .provenance = binding.provenance});
        }

        std::map<std::string, PackageInstanceContext, std::less<>> contexts{};
        for (const auto &package : data.packages) contexts.emplace(package.identity, package.context);
        for (const auto &item : data.buildItems)
        {
            if (item.kind == "CxxModule" && !capabilities.cxxModules)
                AddError(result.diagnostics, "NGIN7104",
                         "selected CMake adapter cannot represent C++ module item '" + item.path + "'");
            auto value = item.path;
            if (item.kind == "Define" && item.value.has_value()) value += "=" + *item.value;
            const auto pathItem = item.kind == "Source" || item.kind == "Header" || item.kind == "CxxModule" ||
                                  item.kind == "Resource" || item.kind == "IncludeDirectory" ||
                                  item.kind == "PrecompiledHeader";
            if (pathItem && item.generated && !context.actionOutputRoot.empty())
                value =
                    (std::filesystem::path(context.actionOutputRoot) / item.path).lexically_normal().generic_string();
            else if (pathItem && !context.projectRoot.empty() && !std::filesystem::path(item.path).is_absolute())
                value = (std::filesystem::path(context.projectRoot) / item.path).lexically_normal().generic_string();
            build.items.push_back(BuildPlanItem{.identity = "CMakeItem:" + item.identity,
                                                .graphIdentity = item.identity,
                                                .operation = item.kind,
                                                .value = std::move(value),
                                                .visibility = item.visibility,
                                                .generated = item.generated,
                                                .provenance = item.provenance});
        }
        for (const auto &item : data.exports)
        {
            if (item.kind != ExportUseKind::Library ||
                contexts.at(item.packageInstance) != PackageInstanceContext::Target)
                continue;
            const auto target = byExport.find(item.identity);
            if (target == byExport.end())
            {
                AddError(result.diagnostics, "NGIN7105",
                         "active Library Export has no CMake target binding: " + item.identity);
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
            if (!context.actionKinds.contains(action.kind)) continue;
            const auto toolIdentity = action.packageInstance + "::" + action.toolExport;
            const auto tool = byExport.find(toolIdentity);
            if (tool == byExport.end())
            {
                AddError(result.diagnostics, "NGIN7106",
                         "Action host Tool has no CMake target binding: " + toolIdentity);
                continue;
            }
            std::vector<std::string> inputs{};
            for (const auto &input : action.inputs)
                inputs.push_back(
                    context.projectRoot.empty()
                        ? input
                        : (std::filesystem::path(context.projectRoot) / input).lexically_normal().generic_string());
            std::vector<std::string> outputs{};
            for (const auto &output : action.outputs)
                outputs.push_back(context.actionOutputRoot.empty()
                                      ? output
                                      : (std::filesystem::path(context.actionOutputRoot) / output)
                                            .lexically_normal()
                                            .generic_string());
            const auto workingDirectory =
                context.actionOutputRoot.empty()
                    ? action.workingDirectory
                    : (std::filesystem::path(context.actionOutputRoot) / action.workingDirectory)
                          .lexically_normal()
                          .generic_string();
            const auto contextFile =
                context.actionContextRoot.empty() || action.kind != ActionKind::Generate
                    ? std::string{}
                    : (std::filesystem::path(context.actionContextRoot) / (TargetName(action.identity) + ".xml"))
                          .lexically_normal()
                          .generic_string();
            auto arguments = action.arguments;
            for (auto &argument : arguments) argument = ExpandActionArgument(std::move(argument), context, contextFile);
            actions.steps.push_back(ActionPlanStep{.identity = "CMakeAction:" + action.identity,
                                                   .graphIdentity = action.identity,
                                                   .kind = action.kind,
                                                   .toolGraphIdentity = toolIdentity,
                                                   .toolTarget = tool->second->targetName,
                                                   .deterministic = action.deterministic,
                                                   .inputs = std::move(inputs),
                                                   .outputs = std::move(outputs),
                                                   .arguments = std::move(arguments),
                                                   .workingDirectory = workingDirectory,
                                                   .environment = action.environment,
                                                   .options = action.options,
                                                   .contextFile = contextFile,
                                                   .provenance = action.provenance});
            build.actionDependencies.push_back(action.identity);
        }
        if (!SortPackagesDependencyFirst(build.packages, data.edges, packageRepresentatives))
            AddError(result.diagnostics, "NGIN7107", "CMake package integrations contain a dependency cycle");
        if (!result.diagnostics.empty()) return result;
        SortByIdentity(build.items);
        SortByIdentity(build.links);
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
            if (package.kind == CMakeIntegrationKind::AddSubdirectory || package.kind == CMakeIntegrationKind::Manual)
                out << "add_subdirectory(\"" << Escape(package.source) << "\" \"" << Escape(package.binaryDirectory)
                    << "\")\n";
            if ((package.kind == CMakeIntegrationKind::FindPackage || package.kind == CMakeIntegrationKind::Isolated) &&
                package.findPackage.has_value())
            {
                if (package.kind == CMakeIntegrationKind::Isolated)
                    out << "list(PREPEND CMAKE_PREFIX_PATH \"${CMAKE_BINARY_DIR}/" << Escape(package.installedPrefix)
                        << "\")\n";
                out << "find_package(" << package.findPackage->name;
                if (package.findPackage->version.has_value()) out << ' ' << *package.findPackage->version;
                if (package.findPackage->config) out << " CONFIG";
                if (package.findPackage->required) out << " REQUIRED";
                out << ")\n";
            }
            if (package.kind == CMakeIntegrationKind::Cps)
            {
                for (const auto &target : package.targets)
                {
                    if (target.importedKind == "executable")
                        out << "add_executable(" << target.targetName << " IMPORTED GLOBAL)\n";
                    else if (target.importedKind == "interface")
                        out << "add_library(" << target.targetName << " INTERFACE IMPORTED GLOBAL)\n";
                    else if (target.importedKind == "archive")
                        out << "add_library(" << target.targetName << " STATIC IMPORTED GLOBAL)\n";
                    else if (target.importedKind == "module")
                        out << "add_library(" << target.targetName << " MODULE IMPORTED GLOBAL)\n";
                    else
                        out << "add_library(" << target.targetName << " SHARED IMPORTED GLOBAL)\n";
                    if (!target.location.empty())
                        out << "set_property(TARGET " << target.targetName << " PROPERTY IMPORTED_LOCATION \""
                            << Escape(target.location) << "\")\n";
                    if (!target.includeDirectories.empty())
                        out << "set_property(TARGET " << target.targetName
                            << " PROPERTY INTERFACE_INCLUDE_DIRECTORIES \"" << CMakeList(target.includeDirectories)
                            << "\")\n";
                    if (!target.compileDefinitions.empty())
                        out << "set_property(TARGET " << target.targetName
                            << " PROPERTY INTERFACE_COMPILE_DEFINITIONS \"" << CMakeList(target.compileDefinitions)
                            << "\")\n";
                    if (!target.compileOptions.empty())
                        out << "set_property(TARGET " << target.targetName
                            << " PROPERTY INTERFACE_COMPILE_OPTIONS \"" << CMakeList(target.compileOptions)
                            << "\")\n";
                    if (!target.linkOptions.empty())
                        out << "set_property(TARGET " << target.targetName
                            << " PROPERTY INTERFACE_LINK_OPTIONS \"" << CMakeList(target.linkOptions) << "\")\n";
                }
            }
        }
        out << '\n';
        if (plan.targetKind == "Executable")
            out << "add_executable(" << plan.targetName << ")\n";
        else if (plan.targetKind == "StaticLibrary")
            out << "add_library(" << plan.targetName << " STATIC)\n";
        else if (plan.targetKind == "SharedLibrary")
            out << "add_library(" << plan.targetName << " SHARED)\n";
        else if (plan.targetKind == "ModuleLibrary")
            out << "add_library(" << plan.targetName << " MODULE)\n";
        else
            out << "add_library(" << plan.targetName << " INTERFACE)\n";
        const auto interfaceTarget = plan.targetKind == "InterfaceLibrary";
        auto standard = plan.languageStandard;
        if (standard.starts_with("C++")) standard.erase(0, 3);
        out << "set_property(TARGET " << plan.targetName << " PROPERTY CXX_STANDARD " << standard << ")\n"
            << "set_property(TARGET " << plan.targetName << " PROPERTY CXX_EXTENSIONS "
            << (plan.languageExtensions ? "ON" : "OFF") << ")\n"
            << "set_property(TARGET " << plan.targetName << " PROPERTY CXX_STANDARD_REQUIRED "
            << (plan.languageRequired ? "ON" : "OFF") << ")\n";
        const auto compileScope = interfaceTarget ? "INTERFACE" : "PRIVATE";
        const auto msvcOptimization = plan.optimization == "Off"     ? "/Od"
                                      : plan.optimization == "Size"  ? "/O1"
                                      : plan.optimization == "Speed" ? "/O2"
                                                                     : "/Ox";
        const auto portableOptimization = plan.optimization == "Off"     ? "-O0"
                                          : plan.optimization == "Size"  ? "-Os"
                                          : plan.optimization == "Speed" ? "-O2"
                                                                         : "-O3";
        out << "target_compile_options(" << plan.targetName << ' ' << compileScope
            << " \"$<$<CXX_COMPILER_ID:MSVC>:" << msvcOptimization << ">\""
            << " \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:" << portableOptimization << ">\")\n";
        if (plan.debugSymbols)
            out << "target_compile_options(" << plan.targetName << ' ' << compileScope
                << " \"$<$<CXX_COMPILER_ID:MSVC>:/Zi>\""
                << " \"$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-g>\")\n";
        if (plan.linkTimeOptimization)
            out << "set_property(TARGET " << plan.targetName << " PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)\n";
        for (const auto &item : plan.items)
        {
            const auto scope = Scope(item.visibility, interfaceTarget);
            if (item.operation == "Source" || item.operation == "Header" || item.operation == "Resource")
                out << "target_sources(" << plan.targetName << ' ' << scope << " \"" << Escape(item.value) << "\")\n";
            else if (item.operation == "CxxModule")
                out << "target_sources(" << plan.targetName << ' ' << scope << " FILE_SET CXX_MODULES FILES \""
                    << Escape(item.value) << "\")\n";
            else if (item.operation == "IncludeDirectory")
                out << "target_include_directories(" << plan.targetName << ' ' << scope << " \"" << Escape(item.value)
                    << "\")\n";
            else if (item.operation == "Define")
                out << "target_compile_definitions(" << plan.targetName << ' ' << scope << " \"" << Escape(item.value)
                    << "\")\n";
            else if (item.operation == "CompileOption")
                out << "target_compile_options(" << plan.targetName << ' ' << scope << " \"" << Escape(item.value)
                    << "\")\n";
            else if (item.operation == "LinkOption")
                out << "target_link_options(" << plan.targetName << ' ' << scope << " \"" << Escape(item.value)
                    << "\")\n";
            else if (item.operation == "PrecompiledHeader")
                out << "target_precompile_headers(" << plan.targetName << ' ' << scope << " \"" << Escape(item.value)
                    << "\")\n";
        }
        for (const auto &link : plan.links)
        {
            out << "target_link_libraries(" << plan.targetName << ' ' << Scope(link.visibility, interfaceTarget) << ' ';
            if (std::filesystem::path{link.targetName}.is_absolute())
                out << '"' << Escape(link.targetName) << '"';
            else
                out << link.targetName;
            out << ")\n";
        }
        for (const auto &action : actions.steps)
        {
            const auto actionTarget = "ngin_action_" + TargetName(action.graphIdentity);
            if (action.outputs.empty())
            {
                out << "add_custom_target(" << actionTarget << "\n  COMMAND ${CMAKE_COMMAND} -E env";
            }
            else
            {
                out << "add_custom_command(OUTPUT";
                for (const auto &output : action.outputs) out << " \"" << Escape(output) << "\"";
                out << "\n  COMMAND ${CMAKE_COMMAND} -E make_directory \"" << Escape(action.workingDirectory)
                    << "\"\n  COMMAND ${CMAKE_COMMAND} -E env";
            }
            for (const auto &[name, value] : action.environment) out << " \"" << Escape(name + "=" + value) << "\"";
            out << " \"$<TARGET_FILE:" << action.toolTarget << ">\"";
            if (!action.contextFile.empty()) out << " --context \"" << Escape(action.contextFile) << "\"";
            for (const auto &argument : action.arguments) out << " \"" << Escape(argument) << "\"";
            if (action.contextFile.empty())
                for (const auto &input : action.inputs) out << " \"" << Escape(input) << "\"";
            out << "\n  DEPENDS " << action.toolTarget;
            for (const auto &input : action.inputs) out << " \"" << Escape(input) << "\"";
            if (!action.workingDirectory.empty())
                out << "\n  WORKING_DIRECTORY \"" << Escape(action.workingDirectory) << "\"";
            out << "\n  VERBATIM\n)\n";
            if (!action.outputs.empty())
            {
                out << "add_custom_target(" << actionTarget << " DEPENDS";
                for (const auto &output : action.outputs) out << " \"" << Escape(output) << "\"";
                out << ")\n";
            }
            out << "add_dependencies(" << plan.targetName << ' ' << actionTarget << ")\n";
        }
        return out.str();
    }
} // namespace NGIN::CLI
