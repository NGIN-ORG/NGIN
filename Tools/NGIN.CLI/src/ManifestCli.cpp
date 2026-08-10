#include "ManifestCli.hpp"

#include "AuthoredManifest.hpp"
#include "CMakeAdapter.hpp"
#include "Canonical.hpp"
#include "DependencyLock.hpp"
#include "DeploymentPlans.hpp"
#include "ManifestArtifacts.hpp"
#include "ManifestFormatter.hpp"
#include "PackageModel.hpp"
#include "ProjectModel.hpp"
#include "SemanticResolver.hpp"
#include "WorkspaceModel.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <ranges>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <type_traits>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace NGIN::CLI
{
    namespace
    {
        [[nodiscard]] auto ReadText(const fs::path &path) -> std::string
        {
            std::ifstream input(path, std::ios::binary);
            if (!input) throw std::runtime_error(path.string() + ": cannot read file");
            std::ostringstream text{};
            text << input.rdbuf();
            return text.str();
        }

        auto WriteText(const fs::path &path, const std::string &text) -> void
        {
            fs::create_directories(path.parent_path());
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error(path.string() + ": cannot write file");
            output << text;
        }

        [[nodiscard]] auto EscapeXml(const std::string_view value) -> std::string
        {
            std::string escaped{};
            for (const auto character : value)
            {
                if (character == '&')
                    escaped += "&amp;";
                else if (character == '<')
                    escaped += "&lt;";
                else if (character == '>')
                    escaped += "&gt;";
                else if (character == '"')
                    escaped += "&quot;";
                else
                    escaped += character;
            }
            return escaped;
        }

        [[nodiscard]] auto EscapeJson(const std::string_view value) -> std::string
        {
            std::string escaped{};
            for (const auto character : value)
            {
                switch (character)
                {
                case '"': escaped += "\\\""; break;
                case '\\': escaped += "\\\\"; break;
                case '\n': escaped += "\\n"; break;
                case '\r': escaped += "\\r"; break;
                case '\t': escaped += "\\t"; break;
                default: escaped += character; break;
                }
            }
            return escaped;
        }

        [[nodiscard]] auto Child(const AuthoredElement &owner, const std::string_view specId) -> const AuthoredElement *
        {
            const auto found = std::ranges::find(owner.children, specId, &AuthoredElement::specId);
            return found == owner.children.end() ? nullptr : &*found;
        }

        [[nodiscard]] auto LoadProject(const fs::path &path) -> AuthoredProjectManifest
        {
            const auto parsed = ParseAuthoredManifest(path);
            if (!parsed.Succeeded())
                throw std::runtime_error(
                    path.string() + ": " +
                    (parsed.diagnostics.empty() ? "invalid manifest" : parsed.diagnostics[0].message));
            if (!std::holds_alternative<AuthoredProjectManifest>(*parsed.value))
                throw std::runtime_error(path.string() + ": expected Project manifest");
            return std::get<AuthoredProjectManifest>(*parsed.value);
        }

        [[nodiscard]] auto FindPackage(const AuthoredProjectManifest &project, const std::string_view name)
            -> const AuthoredElement *
        {
            const auto *dependencies = Child(project.root, "project.dependencies");
            if (dependencies == nullptr) return nullptr;
            const auto found = std::ranges::find_if(dependencies->children, [&](const auto &candidate) {
                const auto *attribute = candidate.Attribute("Name");
                return candidate.specId == "project.dependencies.package" && attribute != nullptr &&
                       attribute->value == name;
            });
            return found == dependencies->children.end() ? nullptr : &*found;
        }

        [[nodiscard]] auto InsertDependency(std::string text, const AuthoredProjectManifest &project,
                                            const std::string &declaration) -> std::string
        {
            if (const auto *dependencies = Child(project.root, "project.dependencies"))
            {
                const auto closing = text.rfind("</Dependencies>", dependencies->source.end.offset);
                if (closing == std::string::npos) throw std::runtime_error("cannot locate </Dependencies>");
                text.insert(closing, declaration);
                return text;
            }
            const auto openEnd = text.find('>', project.root.source.begin.offset);
            if (openEnd == std::string::npos) throw std::runtime_error("cannot locate Project start tag");
            const auto last = text.find_last_not_of(" \t\r\n", openEnd - 1);
            if (last != std::string::npos && text[last] == '/')
            {
                text.replace(last, openEnd - last + 1,
                             ">\n  <Dependencies>\n" + declaration + "  </Dependencies>\n</Project>");
                return text;
            }
            text.insert(openEnd + 1, "\n  <Dependencies>\n" + declaration + "  </Dependencies>");
            return text;
        }

        [[nodiscard]] auto VersionMarkup(const CliArguments &arguments) -> std::pair<std::string, std::string>
        {
            const auto scalarCount = static_cast<int>(arguments.exactVersion.has_value()) +
                                     static_cast<int>(arguments.compatibleVersion.has_value());
            const auto interval = arguments.atLeastVersion || arguments.afterVersion || arguments.atMostVersion ||
                                  arguments.beforeVersion;
            if (scalarCount + static_cast<int>(interval) > 1)
                throw std::runtime_error("choose one version constraint form");
            if (arguments.exactVersion) return {" Exact=\"" + EscapeXml(*arguments.exactVersion) + "\"", {}};
            if (arguments.compatibleVersion)
                return {" Compatible=\"" + EscapeXml(*arguments.compatibleVersion) + "\"", {}};
            std::string attributes{};
            const auto append = [&](const std::string_view name, const std::optional<std::string> &value) {
                if (value) attributes += " " + std::string{name} + "=\"" + EscapeXml(*value) + "\"";
            };
            append("AtLeast", arguments.atLeastVersion);
            append("After", arguments.afterVersion);
            append("AtMost", arguments.atMostVersion);
            append("Before", arguments.beforeVersion);
            return {{}, attributes.empty() ? std::string{} : "      <Version" + attributes + " />\n"};
        }

        [[nodiscard]] auto PackageMarkup(const CliArguments &arguments) -> std::string
        {
            if (!arguments.packageName) throw std::runtime_error("package name is required");
            const auto [attributes, versionChild] = VersionMarkup(arguments);
            std::string children = versionChild;
            for (const auto &use : arguments.exportUses)
            {
                const auto separator = use.find(':');
                if (separator == std::string::npos) throw std::runtime_error("--use expects Kind:Name");
                const auto kind = use.substr(0, separator);
                if (kind != "Library" && kind != "Tool" && kind != "Plugin" && kind != "Action" && kind != "Asset")
                    throw std::runtime_error("unknown export kind '" + kind + "'");
                children += "      <Use " + kind + "=\"" + EscapeXml(use.substr(separator + 1)) + "\" />\n";
            }
            for (const auto &assignment : arguments.optionAssignments)
            {
                const auto separator = assignment.find('=');
                if (separator == std::string::npos) throw std::runtime_error("--option expects Name=Value");
                children += "      <Option Name=\"" + EscapeXml(assignment.substr(0, separator)) + "\" Value=\"" +
                            EscapeXml(assignment.substr(separator + 1)) + "\" />\n";
            }
            const auto opening = "    <Package Name=\"" + EscapeXml(*arguments.packageName) + "\"" + attributes;
            return children.empty() ? opening + " />\n" : opening + ">\n" + children + "    </Package>\n";
        }

        auto PrintDiagnostics(const std::vector<ManifestDiagnostic> &diagnostics, const fs::path &fallback) -> int
        {
            int errors = 0;
            for (const auto &diagnostic : diagnostics)
            {
                if (diagnostic.severity == ManifestDiagnosticSeverity::Error) ++errors;
                const auto &path = diagnostic.source.path.empty() ? fallback : diagnostic.source.path;
                std::cerr << path.string() << ':' << diagnostic.source.begin.line << ':'
                          << diagnostic.source.begin.column << ": "
                          << (diagnostic.severity == ManifestDiagnosticSeverity::Error ? "error" : "warning") << ' '
                          << diagnostic.code << ": " << diagnostic.message << '\n';
                if (diagnostic.fixHint) std::cerr << "  hint: " << *diagnostic.fixHint << '\n';
            }
            return errors;
        }

        [[nodiscard]] auto FindWorkspace(const fs::path &start, const CliArguments &arguments)
            -> std::optional<fs::path>
        {
            if (arguments.workspacePath) return fs::weakly_canonical(*arguments.workspacePath);
            auto directory = fs::is_directory(start) ? start : start.parent_path();
            while (!directory.empty())
            {
                const auto candidate = directory / "NGIN.ngin";
                if (fs::is_regular_file(candidate)) return fs::weakly_canonical(candidate);
                const auto parent = directory.parent_path();
                if (parent == directory) break;
                directory = parent;
            }
            return std::nullopt;
        }

        [[nodiscard]] auto HostTarget() -> Target
        {
            Target result{.name = "host", .aliases = {"native"}};
#if defined(_WIN32)
            result.operatingSystem = "windows";
#elif defined(__APPLE__)
            result.operatingSystem = "macos";
#elif defined(__linux__)
            result.operatingSystem = "linux";
#else
            result.operatingSystem = "unknown";
#endif
#if defined(_M_X64) || defined(__x86_64__)
            result.architecture = "x64";
#elif defined(_M_IX86) || defined(__i386__)
            result.architecture = "x86";
#elif defined(_M_ARM64) || defined(__aarch64__)
            result.architecture = "arm64";
#elif defined(_M_ARM) || defined(__arm__)
            result.architecture = "arm";
#else
            result.architecture = "unknown";
#endif
            return result;
        }

        [[nodiscard]] auto DefaultToolchain() -> Toolchain
        {
            return Toolchain{.name = "default", .compiler = "default", .linker = "default"};
        }

        [[nodiscard]] auto RequestedSelection(const CliArguments &arguments) -> SelectionRequest
        {
            SelectionRequest result{
                .configuration = arguments.configuration, .target = arguments.target, .toolchain = arguments.toolchain};
            for (const auto &assignment : arguments.optionAssignments)
            {
                const auto separator = assignment.find('=');
                if (separator == std::string::npos || separator == 0)
                    throw std::runtime_error("--option expects Name=Value");
                const auto name = assignment.substr(0, separator);
                const auto value = assignment.substr(separator + 1);
                if (const auto [found, inserted] = result.options.emplace(name, value);
                    !inserted && found->second != value)
                    throw std::runtime_error("conflicting --option values for '" + name + "'");
            }
            return result;
        }

        [[nodiscard]] auto DependencyContextsForCommand(const std::string_view command) -> std::set<DependencyContext>
        {
            if (command == "test") return {DependencyContext::Target, DependencyContext::Test};
            if (command == "publish") return {DependencyContext::Target, DependencyContext::Publish};
            if (command == "configure" || command == "build" || command == "stage" || command == "run" ||
                command == "analyze" || command == "format")
                return {DependencyContext::Target};
            return {DependencyContext::Target, DependencyContext::Test, DependencyContext::Benchmark,
                    DependencyContext::Publish};
        }

        [[nodiscard]] auto ActionKindsForCommand(const std::string_view command) -> std::set<ActionKind>
        {
            if (command == "analyze") return {ActionKind::Analyze};
            if (command == "format") return {ActionKind::Format};
            if (command == "configure" || command == "build" || command == "stage" || command == "run" ||
                command == "test" || command == "publish")
                return {ActionKind::Generate};
            return {ActionKind::Generate, ActionKind::Analyze, ActionKind::Format, ActionKind::Validate,
                    ActionKind::Custom};
        }

        struct LoadedComposition;

        class UnavailablePackageProvider final : public PackageProvider
        {
          public:
            UnavailablePackageProvider(std::string kind, std::string identity)
                : kind_(std::move(kind)), identity_(std::move(identity))
            {
            }

            [[nodiscard]] auto Kind() const -> std::string_view override { return kind_; }

            [[nodiscard]] auto Resolve(const PackageProviderRequest &request) const
                -> PackageProviderResolution override
            {
                PackageProviderResolution result{};
                if (request.sourceBinding.has_value() && *request.sourceBinding != identity_) return result;
                result.diagnostics.push_back(
                    ManifestDiagnostic{.code = "NGIN4019",
                                       .message = "PackageProvider Source '" + identity_ + "' uses " + kind_ +
                                                  ", but this installation implements only Directory acquisition",
                                       .source = request.source});
                return result;
            }

          private:
            std::string kind_{};
            std::string identity_{};
        };

        struct LoadedProjectReference
        {
            std::string name{};
            DependencyContext context{DependencyContext::Target};
            fs::path path{};
            std::unique_ptr<LoadedComposition> composition{};
        };

        struct LoadedComposition
        {
            std::optional<ResolvedCompositionGraph> graph{};
            ResolvedCMakeIntegrationBindings cmake{};
            fs::path projectDirectory{};
            std::map<std::string, fs::path, std::less<>> packageRoots{};
            ActionTrustPolicy actionTrustPolicy{};
            WorkspaceStageCollisionPolicy stageCollision{WorkspaceStageCollisionPolicy::Error};
            bool allowSymlinks{false};
            bool providerLockRequired{false};
            std::vector<LoadedProjectReference> projectReferences{};
            std::vector<ManifestDiagnostic> diagnostics{};
        };

        [[nodiscard]] auto ResolveImpl(const fs::path &projectPath, const CliArguments &arguments,
                                       const std::string_view command, std::vector<fs::path> ancestry)
            -> LoadedComposition
        {
            LoadedComposition loaded{};
            loaded.projectDirectory = projectPath.parent_path();
            const auto authored = ParseAuthoredManifest(projectPath);
            loaded.diagnostics = authored.diagnostics;
            if (!authored.Succeeded() || !std::holds_alternative<AuthoredProjectManifest>(*authored.value))
                return loaded;
            auto project = ParseSemanticProject(std::get<AuthoredProjectManifest>(*authored.value));
            loaded.diagnostics.insert(loaded.diagnostics.end(), project.diagnostics.begin(), project.diagnostics.end());
            if (!project.Succeeded()) return loaded;
            const auto projectDependencies = project.value->dependencies;

            auto requested = RequestedSelection(arguments);
            SelectionFacts selection{};
            selection.configuration = Configuration{.name = requested.configuration.value_or("Debug")};
            selection.target = HostTarget();
            if (requested.target.has_value()) selection.target.name = *requested.target;
            selection.toolchain = DefaultToolchain();
            if (requested.toolchain.has_value()) selection.toolchain.name = *requested.toolchain;
            for (const auto &[name, definition] : project.value->options)
                selection.options.emplace(name, definition.defaultValue);

            std::vector<std::unique_ptr<PackageProvider>> providers{};
            std::map<std::string, fs::path, std::less<>> localRoots{};
            std::map<std::string, std::vector<SourcedVersionConstraint>, std::less<>> central{};
            std::map<std::string, std::string, std::less<>> packageSourceBindings{};
            std::map<std::string, fs::path, std::less<>> workspaceProjects{};
            bool allowSymlinks = false;
            bool providerIntegrityRequired = false;
            bool allowNonHermeticProviders = true;
            auto workspaceRoot = projectPath.parent_path();
            bool foundWorkspace = false;
            if (const auto workspacePath = FindWorkspace(projectPath, arguments))
            {
                foundWorkspace = true;
                const auto workspaceAuthored = ParseAuthoredManifest(*workspacePath);
                loaded.diagnostics.insert(loaded.diagnostics.end(), workspaceAuthored.diagnostics.begin(),
                                          workspaceAuthored.diagnostics.end());
                if (workspaceAuthored.Succeeded() &&
                    std::holds_alternative<AuthoredWorkspaceManifest>(*workspaceAuthored.value))
                {
                    auto workspace =
                        ParseSemanticWorkspace(std::get<AuthoredWorkspaceManifest>(*workspaceAuthored.value));
                    loaded.diagnostics.insert(loaded.diagnostics.end(), workspace.diagnostics.begin(),
                                              workspace.diagnostics.end());
                    if (workspace.Succeeded())
                    {
                        workspaceRoot = workspace.value->root;
                        for (const auto &entry : workspace.value->projects)
                            workspaceProjects.emplace(entry.project.name, entry.path);
                        loaded.actionTrustPolicy = workspace.value->actionTrustPolicy;
                        loaded.stageCollision = workspace.value->stageCollision;
                        loaded.allowSymlinks = workspace.value->pathPolicy.allowSymlinks;
                        allowSymlinks = workspace.value->pathPolicy.allowSymlinks;
                        loaded.providerLockRequired = workspace.value->providerPolicy.locked;
                        providerIntegrityRequired = workspace.value->providerPolicy.integrityRequired;
                        allowNonHermeticProviders = workspace.value->providerPolicy.allowNonHermetic;
                        if (arguments.preset.has_value())
                        {
                            const auto preset =
                                std::ranges::find(workspace.value->selection.presets, *arguments.preset, &Preset::name);
                            if (preset == workspace.value->selection.presets.end())
                                throw std::runtime_error("unknown Preset '" + *arguments.preset + "'");
                            auto expansion = ExpandPreset(*preset, command, requested);
                            loaded.diagnostics.insert(loaded.diagnostics.end(), expansion.diagnostics.begin(),
                                                      expansion.diagnostics.end());
                            if (!expansion.Succeeded()) return loaded;
                            requested = std::move(*expansion.value);
                        }
                        const auto selectByName = [](const auto &items, const std::optional<std::string> &requested,
                                                     const std::optional<std::string> &fallback)
                            -> const typename std::decay_t<decltype(items)>::value_type * {
                            const auto name = requested ? requested : fallback;
                            if (name)
                            {
                                if (const auto found =
                                        std::ranges::find(items, *name, &std::decay_t<decltype(items[0])>::name);
                                    found != items.end())
                                    return &*found;
                                throw std::runtime_error("unknown selection '" + *name + "'");
                            }
                            return items.empty() ? nullptr : &items.front();
                        };
                        if (const auto *value =
                                selectByName(workspace.value->selection.configurations, requested.configuration,
                                             workspace.value->selection.defaults.configuration))
                            selection.configuration = *value;
                        const auto selectedTargetName =
                            requested.target ? requested.target : workspace.value->selection.defaults.target;
                        if (selectedTargetName)
                        {
                            const auto target =
                                std::ranges::find_if(workspace.value->selection.targets, [&](const Target &candidate) {
                                    return candidate.name == *selectedTargetName ||
                                           candidate.aliases.contains(*selectedTargetName);
                                });
                            if (target == workspace.value->selection.targets.end())
                                throw std::runtime_error("unknown Target '" + *selectedTargetName + "'");
                            selection.target = *target;
                        }
                        else if (!workspace.value->selection.targets.empty())
                            selection.target = workspace.value->selection.targets.front();
                        if (const auto *value = selectByName(workspace.value->selection.toolchains, requested.toolchain,
                                                             workspace.value->selection.defaults.toolchain))
                            selection.toolchain = *value;
                        if (!workspace.value->compatibilityPolicy.targets.empty() &&
                            !workspace.value->compatibilityPolicy.targets.contains(selection.target.name))
                            throw std::runtime_error("Target '" + selection.target.name +
                                                     "' is rejected by workspace Compatibility policy");
                        if (!workspace.value->compatibilityPolicy.toolchains.empty() &&
                            !workspace.value->compatibilityPolicy.toolchains.contains(selection.toolchain.name))
                            throw std::runtime_error("Toolchain '" + selection.toolchain.name +
                                                     "' is rejected by workspace Compatibility policy");
                        const auto applyWorkspaceOption = [&](const std::string &name, const std::string &value) {
                            const auto definition = project.value->options.find(name);
                            if (definition == project.value->options.end()) return;
                            const auto parsed = ParseOptionValue(definition->second, value);
                            if (!parsed.Succeeded()) throw std::runtime_error(parsed.diagnostics.front().message);
                            selection.options[name] = *parsed.value;
                        };
                        for (const auto &[name, value] : workspace.value->selection.defaults.options)
                            applyWorkspaceOption(name, value);
                        for (const auto &[name, value] : selection.configuration.options)
                            if (project.value->options.contains(name)) selection.options[name] = value;
                        std::map<std::string, std::vector<DirectoryPackageRelease>, std::less<>> releasesBySource{};
                        std::set<fs::path> explicitManifests{};
                        const auto sourceForRoot = [&](const fs::path &rootPath) {
                            for (const auto &[sourceName, source] : workspace.value->packageSources)
                            {
                                if (source.kind != "Directory" || !source.path.has_value()) continue;
                                const auto sourceRoot = fs::weakly_canonical(workspaceRoot / source.path->value);
                                const auto relative = fs::weakly_canonical(rootPath).lexically_relative(sourceRoot);
                                if (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..")
                                    return sourceName;
                            }
                            return std::string{"workspace"};
                        };
                        for (const auto &[name, local] : workspace.value->localPackages)
                        {
                            const auto &version = local.coordinate.exactVersion;
                            const auto manifestPath = fs::weakly_canonical(workspaceRoot / local.manifest.value);
                            const auto rootPath = fs::weakly_canonical(workspaceRoot / local.root.value);
                            const auto sourceName = sourceForRoot(rootPath);
                            releasesBySource[sourceName].push_back(
                                DirectoryPackageRelease{.name = name,
                                                        .manifest = manifestPath,
                                                        .root = rootPath,
                                                        .nativeIdentity = name,
                                                        .nativeVersion = version,
                                                        .revision = "workspace",
                                                        .integrity = Sha256Fingerprint(ReadText(manifestPath)),
                                                        .artifactIdentity = name + '@' + version,
                                                        .hermetic = false});
                            explicitManifests.insert(manifestPath);
                            localRoots[name + '@' + version] = rootPath;
                        }
                        for (const auto &[sourceName, source] : workspace.value->packageSources)
                        {
                            if (source.kind != "Directory" || !source.path.has_value()) continue;
                            const auto sourceRoot = fs::weakly_canonical(workspaceRoot / source.path->value);
                            std::error_code enumerationError{};
                            auto options = fs::directory_options::skip_permission_denied;
                            if (allowSymlinks) options |= fs::directory_options::follow_directory_symlink;
                            fs::recursive_directory_iterator iterator(sourceRoot, options, enumerationError);
                            const fs::recursive_directory_iterator end{};
                            for (; !enumerationError && iterator != end; iterator.increment(enumerationError))
                            {
                                if (iterator->is_symlink() && !allowSymlinks)
                                {
                                    loaded.diagnostics.push_back(ManifestDiagnostic{
                                        .code = "NGIN2008",
                                        .message = "symlink encountered in Directory PackageProvider "
                                                   "Source while "
                                                   "workspace policy disallows symlinks",
                                        .source = source.source});
                                    continue;
                                }
                                if (!iterator->is_regular_file() || iterator->path().extension() != ".nginpkg")
                                    continue;
                                const auto manifestPath = fs::weakly_canonical(iterator->path());
                                if (explicitManifests.contains(manifestPath)) continue;
                                const auto packageAuthored = ParseAuthoredManifest(manifestPath);
                                loaded.diagnostics.insert(loaded.diagnostics.end(), packageAuthored.diagnostics.begin(),
                                                          packageAuthored.diagnostics.end());
                                if (!packageAuthored.Succeeded()) continue;
                                const auto *package = std::get_if<AuthoredPackageManifest>(&*packageAuthored.value);
                                if (package == nullptr) continue;
                                releasesBySource[sourceName].push_back(
                                    DirectoryPackageRelease{.name = package->name,
                                                            .manifest = manifestPath,
                                                            .root = manifestPath.parent_path(),
                                                            .nativeIdentity = package->name,
                                                            .nativeVersion = package->version,
                                                            .revision = "workspace",
                                                            .integrity = Sha256Fingerprint(ReadText(manifestPath)),
                                                            .artifactIdentity = package->name + '@' + package->version,
                                                            .hermetic = false});
                                localRoots.try_emplace(package->name + '@' + package->version,
                                                       manifestPath.parent_path());
                            }
                            if (enumerationError)
                                loaded.diagnostics.push_back(ManifestDiagnostic{
                                    .code = "NGIN7001",
                                    .message = "cannot enumerate Directory PackageProvider Source '" + sourceName +
                                               "': " + enumerationError.message(),
                                    .source = source.source});
                        }
                        for (auto &[sourceName, releases] : releasesBySource)
                            if (!releases.empty())
                                providers.push_back(
                                    std::make_unique<DirectoryPackageProvider>(sourceName, std::move(releases)));
                        for (const auto &[sourceName, source] : workspace.value->packageSources)
                            if (source.kind != "Directory")
                                providers.push_back(
                                    std::make_unique<UnavailablePackageProvider>(source.kind, sourceName));
                        for (const auto &[name, constraint] : workspace.value->centralVersions)
                            central[name].push_back(constraint);
                        for (const auto &[name, binding] : workspace.value->packageBindings)
                            packageSourceBindings.emplace(name, binding.sourceName);
                    }
                }
            }
            if (arguments.preset.has_value() && !foundWorkspace)
                throw std::runtime_error("--preset requires a Workspace manifest");
            const auto host = HostTarget();
            if (selection.target.name == "host" || selection.target.operatingSystem == "host")
                selection.target.operatingSystem = host.operatingSystem;
            if (selection.target.name == "host" || selection.target.architecture == "host")
                selection.target.architecture = host.architecture;
            if (selection.toolchain.compiler.empty() || selection.toolchain.compiler == "default")
                selection.toolchain.compiler = DefaultToolchain().compiler;
            for (const auto &[name, authoredValue] : requested.options)
            {
                if (!project.value->options.contains(name))
                    throw std::runtime_error("unknown project Option '" + name + "'");
                const auto parsed = ParseOptionValue(project.value->options.at(name), authoredValue);
                if (!parsed.Succeeded()) throw std::runtime_error(parsed.diagnostics[0].message);
                selection.options[name] = *parsed.value;
            }
            std::vector<const PackageProvider *> providerViews{};
            for (const auto &provider : providers) providerViews.push_back(provider.get());
            auto hostSelection = selection;
            hostSelection.target = host;
            auto resolution = ResolveComposition(
                SemanticResolutionRequest{.project = std::move(*project.value),
                                          .projectDirectory = projectPath.parent_path(),
                                          .workspaceRoot = workspaceRoot,
                                          .targetSelection = selection,
                                          .hostSelection = hostSelection,
                                          .packageProviders = std::move(providerViews),
                                          .centralVersions = std::move(central),
                                          .packageSourceBindings = std::move(packageSourceBindings),
                                          .dependencyContexts = DependencyContextsForCommand(command),
                                          .actionKinds = ActionKindsForCommand(command),
                                          .targetCaseInsensitive = selection.target.operatingSystem == "windows",
                                          .allowSymlinks = allowSymlinks,
                                          .providerIntegrityRequired = providerIntegrityRequired,
                                          .allowNonHermeticProviders = allowNonHermeticProviders});
            loaded.diagnostics.insert(loaded.diagnostics.end(), resolution.diagnostics.begin(),
                                      resolution.diagnostics.end());
            loaded.graph = std::move(resolution.graph);
            loaded.cmake = std::move(resolution.cmakeIntegrations);
            if (loaded.graph)
                for (const auto &package : loaded.graph->Data().packages)
                    if (const auto found =
                            localRoots.find(package.coordinate.name + '@' + package.coordinate.exactVersion);
                        found != localRoots.end())
                        loaded.packageRoots.emplace(package.identity, found->second);

            if (loaded.graph)
            {
                const auto contexts = DependencyContextsForCommand(command);
                for (const auto &dependency : projectDependencies)
                {
                    const auto *reference = std::get_if<ProjectDependencyRequest>(&dependency);
                    if (reference == nullptr || !contexts.contains(reference->context)) continue;
                    fs::path referencedPath{};
                    if (reference->path.has_value())
                        referencedPath = fs::weakly_canonical(projectPath.parent_path() / reference->path->value);
                    else if (const auto discovered = workspaceProjects.find(reference->name);
                             discovered != workspaceProjects.end())
                        referencedPath = fs::weakly_canonical(discovered->second);
                    else
                    {
                        loaded.diagnostics.push_back(ManifestDiagnostic{
                            .code = "NGIN3005",
                            .message = "Project dependency '" + reference->name + "' needs Path or workspace discovery",
                            .source = reference->source});
                        continue;
                    }
                    if (!fs::is_regular_file(referencedPath))
                    {
                        loaded.diagnostics.push_back(ManifestDiagnostic{
                            .code = "NGIN3005",
                            .message = "Project dependency manifest is missing: " + referencedPath.string(),
                            .source = reference->source});
                        continue;
                    }
                    if (std::ranges::find(ancestry, referencedPath) != ancestry.end())
                    {
                        loaded.diagnostics.push_back(
                            ManifestDiagnostic{.code = "NGIN3006",
                                               .message = "Project dependency cycle reaches '" + reference->name + "'",
                                               .source = reference->source});
                        continue;
                    }
                    auto childAncestry = ancestry;
                    childAncestry.push_back(referencedPath);
                    auto child = std::make_unique<LoadedComposition>(
                        ResolveImpl(referencedPath, arguments, command, std::move(childAncestry)));
                    loaded.diagnostics.insert(loaded.diagnostics.end(), child->diagnostics.begin(),
                                              child->diagnostics.end());
                    if (!child->graph) continue;
                    if (child->graph->Data().product.name != reference->name)
                    {
                        loaded.diagnostics.push_back(
                            ManifestDiagnostic{.code = "NGIN3005",
                                               .message = "Project dependency Name '" + reference->name +
                                                          "' does not match referenced Project '" +
                                                          child->graph->Data().product.name + "'",
                                               .source = reference->source});
                        continue;
                    }
                    if (reference->context == DependencyContext::Target &&
                        child->graph->Data().product.type != ProductType::Library)
                    {
                        loaded.diagnostics.push_back(ManifestDiagnostic{
                            .code = "NGIN3005",
                            .message = "target Project dependency '" + reference->name + "' must produce a Library",
                            .source = reference->source});
                        continue;
                    }
                    loaded.projectReferences.push_back(LoadedProjectReference{.name = reference->name,
                                                                              .context = reference->context,
                                                                              .path = referencedPath,
                                                                              .composition = std::move(child)});
                }
                if (!loaded.projectReferences.empty())
                {
                    auto graphData = loaded.graph->Data();
                    for (const auto &reference : loaded.projectReferences)
                        for (auto &edge : graphData.edges)
                            if (edge.kind == "ProjectDependency" && edge.to == "Project:" + reference.name)
                                edge.to += '@' + reference.composition->graph->CompositionIdentity();
                    loaded.graph.emplace(std::move(graphData));
                }
            }
            return loaded;
        }

        [[nodiscard]] auto Resolve(const fs::path &projectPath, const CliArguments &arguments,
                                   const std::string_view command = "inspect") -> LoadedComposition
        {
            const auto canonical = fs::weakly_canonical(projectPath);
            return ResolveImpl(canonical, arguments, command, {canonical});
        }

        auto CollectDependencyLockEntries(const LoadedComposition &composition,
                                          std::map<std::string, DependencyLockEntry, std::less<>> &entries) -> void
        {
            if (composition.graph)
                for (const auto &entry : CreateDependencyLock(*composition.graph).Data().packages)
                    entries.emplace(entry.packageInstance, entry);
            for (const auto &reference : composition.projectReferences)
                CollectDependencyLockEntries(*reference.composition, entries);
        }

        [[nodiscard]] auto CreateCompositionDependencyLock(const LoadedComposition &composition)
            -> ResolvedDependencyLock
        {
            std::map<std::string, DependencyLockEntry, std::less<>> entries{};
            CollectDependencyLockEntries(composition, entries);
            DependencyLockData data{};
            for (auto &[_, entry] : entries) data.packages.push_back(std::move(entry));
            return ResolvedDependencyLock{std::move(data)};
        }

        [[nodiscard]] auto QuoteProcessArgument(const fs::path &value) -> std::string
        {
            const auto text = value.string();
            if (text.contains('"')) throw std::runtime_error("process path contains an unsupported quote");
            return '"' + text + '"';
        }

        [[nodiscard]] auto QuoteProcessArgument(const std::string &value) -> std::string
        {
            if (value.contains('"')) throw std::runtime_error("process argument contains an unsupported quote");
            return '"' + value + '"';
        }

        auto Execute(const std::string &command) -> void
        {
            const auto result = std::system(command.c_str());
            if (result != 0) throw std::runtime_error("process failed with exit code " + std::to_string(result));
        }

#if defined(_WIN32)
        [[nodiscard]] auto QuoteWindowsArgument(const std::wstring &value) -> std::wstring
        {
            std::wstring result{L'"'};
            std::size_t slashes = 0;
            for (const auto character : value)
            {
                if (character == L'\\')
                {
                    ++slashes;
                    continue;
                }
                if (character == L'"')
                {
                    result.append(slashes * 2 + 1, L'\\');
                    result += L'"';
                    slashes = 0;
                    continue;
                }
                result.append(slashes, L'\\');
                slashes = 0;
                result += character;
            }
            result.append(slashes * 2, L'\\');
            result += L'"';
            return result;
        }
#endif

        auto ExecuteProcess(const std::string &executable, const std::vector<std::string> &arguments,
                            const std::optional<fs::path> &workingDirectory,
                            const std::map<std::string, std::string, std::less<>> &environment,
                            const std::optional<std::int64_t> timeoutSeconds = std::nullopt) -> void
        {
#if defined(_WIN32)
            struct EnvironmentSnapshot
            {
                std::wstring name{};
                std::optional<std::wstring> value{};
            };
            std::vector<EnvironmentSnapshot> snapshots{};
            for (const auto &[name, value] : environment)
            {
                const auto wideName = fs::path{name}.wstring();
                const auto required = GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
                std::optional<std::wstring> previous{};
                if (required != 0)
                {
                    std::wstring buffer(required, L'\0');
                    const auto written = GetEnvironmentVariableW(wideName.c_str(), buffer.data(), required);
                    buffer.resize(written);
                    previous = std::move(buffer);
                }
                snapshots.push_back(EnvironmentSnapshot{.name = wideName, .value = std::move(previous)});
                const auto wideValue = fs::path{value}.wstring();
                if (!SetEnvironmentVariableW(wideName.c_str(), wideValue.c_str()))
                    throw std::runtime_error("failed to set process environment variable '" + name + "'");
            }
            const auto restoreEnvironment = [&] {
                for (const auto &snapshot : snapshots)
                    SetEnvironmentVariableW(snapshot.name.c_str(),
                                            snapshot.value.has_value() ? snapshot.value->c_str() : nullptr);
            };

            std::wstring commandLine = QuoteWindowsArgument(fs::path{executable}.wstring());
            for (const auto &argument : arguments)
                commandLine += L' ' + QuoteWindowsArgument(fs::path{argument}.wstring());
            std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
            mutableCommand.push_back(L'\0');
            const auto wideDirectory = workingDirectory.has_value() ? workingDirectory->wstring() : std::wstring{};
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            const auto created =
                CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE, 0, nullptr,
                               workingDirectory.has_value() ? wideDirectory.c_str() : nullptr, &startup, &process);
            const auto createError = created ? ERROR_SUCCESS : GetLastError();
            restoreEnvironment();
            if (!created)
                throw std::runtime_error("failed to start process '" + executable + "' (Win32 " +
                                         std::to_string(createError) + ")");

            const auto timeout =
                timeoutSeconds.has_value()
                    ? static_cast<DWORD>(std::min<std::int64_t>(*timeoutSeconds * 1000, INFINITE - 1ULL))
                    : INFINITE;
            const auto waited = WaitForSingleObject(process.hProcess, timeout);
            if (waited == WAIT_TIMEOUT)
            {
                TerminateProcess(process.hProcess, 124);
                WaitForSingleObject(process.hProcess, INFINITE);
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
                throw std::runtime_error("process timed out after " + std::to_string(*timeoutSeconds) + " seconds");
            }
            DWORD exitCode = 1;
            const auto readExit = GetExitCodeProcess(process.hProcess, &exitCode);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            if (!readExit || exitCode != 0)
                throw std::runtime_error("process failed with exit code " + std::to_string(exitCode));
#else
            const auto child = fork();
            if (child < 0) throw std::runtime_error("failed to fork process '" + executable + "'");
            if (child == 0)
            {
                if (workingDirectory.has_value() && chdir(workingDirectory->c_str()) != 0) _exit(126);
                for (const auto &[name, value] : environment) setenv(name.c_str(), value.c_str(), 1);
                std::vector<std::string> storage{};
                storage.reserve(arguments.size() + 1);
                storage.push_back(executable);
                storage.insert(storage.end(), arguments.begin(), arguments.end());
                std::vector<char *> argv{};
                argv.reserve(storage.size() + 1);
                for (auto &value : storage) argv.push_back(value.data());
                argv.push_back(nullptr);
                execvp(executable.c_str(), argv.data());
                _exit(127);
            }

            int status = 0;
            const auto deadline =
                timeoutSeconds.has_value()
                    ? std::optional{std::chrono::steady_clock::now() + std::chrono::seconds(*timeoutSeconds)}
                    : std::nullopt;
            while (waitpid(child, &status, WNOHANG) == 0)
            {
                if (deadline.has_value() && std::chrono::steady_clock::now() >= *deadline)
                {
                    kill(child, SIGKILL);
                    waitpid(child, &status, 0);
                    throw std::runtime_error("process timed out after " + std::to_string(*timeoutSeconds) + " seconds");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
                throw std::runtime_error("process failed with exit code " +
                                         std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : 128));
#endif
        }

        struct PreparedBuild
        {
            LoadedComposition composition{};
            BuildPlan build{};
            ActionPlan actions{};
            fs::path output{};
            fs::path generated{};
            fs::path binary{};
            std::vector<std::unique_ptr<PreparedBuild>> projectDependencies{};
        };

        [[nodiscard]] auto SafePathComponent(std::string value) -> std::string
        {
            for (auto &character : value)
                if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_' && character != '.')
                    character = '_';
            return value;
        }

        [[nodiscard]] auto PlannedProductArtifact(const BuildPlan &plan, const fs::path &binary) -> fs::path
        {
            auto artifact = binary / plan.targetName;
#if defined(_WIN32)
            if (plan.targetKind == "Executable")
                artifact += ".exe";
            else if (plan.targetKind == "StaticLibrary")
                artifact = binary / (plan.targetName + ".lib");
            else if (plan.targetKind == "SharedLibrary" || plan.targetKind == "ModuleLibrary")
                artifact += ".dll";
#else
            if (plan.targetKind == "StaticLibrary")
                artifact = binary / ("lib" + plan.targetName + ".a");
            else if (plan.targetKind == "SharedLibrary")
                artifact = binary / ("lib" + plan.targetName + ".so");
#endif
            return artifact;
        }

        [[nodiscard]] auto PlannedLinkArtifact(const BuildPlan &plan, const fs::path &binary) -> fs::path
        {
#if defined(_WIN32)
            if (plan.targetKind == "StaticLibrary" || plan.targetKind == "SharedLibrary")
                return binary / (plan.targetName + ".lib");
#else
            if (plan.targetKind == "StaticLibrary") return binary / ("lib" + plan.targetName + ".a");
            if (plan.targetKind == "SharedLibrary") return binary / ("lib" + plan.targetName + ".so");
#endif
            return {};
        }

        [[nodiscard]] auto FilesHaveIdenticalBytes(const fs::path &left, const fs::path &right) -> bool
        {
            std::error_code error{};
            const auto leftSize = fs::file_size(left, error);
            if (error) return false;
            const auto rightSize = fs::file_size(right, error);
            if (error || leftSize != rightSize) return false;
            std::ifstream leftStream(left, std::ios::binary);
            std::ifstream rightStream(right, std::ios::binary);
            return leftStream && rightStream &&
                   std::equal(std::istreambuf_iterator<char>{leftStream}, std::istreambuf_iterator<char>{},
                              std::istreambuf_iterator<char>{rightStream});
        }

        [[nodiscard]] auto DerivePreparedBuild(LoadedComposition composition, const fs::path &projectPath,
                                               const std::string_view command, const fs::path &output) -> PreparedBuild
        {
            const auto actionOutput = output / "actions";
            const auto actionContexts = output / "action-contexts";
            const auto host = HostTarget();
            const auto &selected = composition.graph->Data().selection;
            const auto crossCompiling = selected.targetOperatingSystem != host.operatingSystem ||
                                        selected.targetArchitecture != host.architecture;
            if (crossCompiling && !selected.toolchainFile.has_value())
                throw std::runtime_error("cross-compiling Target '" + selected.targetOperatingSystem + '-' +
                                         selected.targetArchitecture +
                                         "' requires a ToolchainFile in the selected Toolchain");
            auto plans = DeriveCMakePlans(*composition.graph, composition.cmake,
                                          CMakeAdapterContext{.generator = "Ninja",
                                                              .crossCompiling = crossCompiling,
                                                              .projectRoot = projectPath.parent_path().generic_string(),
                                                              .buildRoot = (output / "cmake").generic_string(),
                                                              .actionOutputRoot = actionOutput.generic_string(),
                                                              .actionContextRoot = actionContexts.generic_string(),
                                                              .actionKinds = ActionKindsForCommand(command)});
            if (PrintDiagnostics(plans.diagnostics, projectPath) != 0 || !plans.Succeeded())
                throw std::runtime_error("CMake plan derivation failed");

            auto references = std::move(composition.projectReferences);
            composition.projectReferences.clear();
            PreparedBuild prepared{.composition = std::move(composition),
                                   .build = std::move(*plans.build),
                                   .actions = std::move(*plans.actions),
                                   .output = output,
                                   .generated = output / "generated",
                                   .binary = output / "cmake"};
            for (auto &reference : references)
            {
                const auto childOutput = output / "projects" / SafePathComponent(reference.name);
                auto child = std::make_unique<PreparedBuild>(
                    DerivePreparedBuild(std::move(*reference.composition), reference.path, command, childOutput));
                if (reference.context == DependencyContext::Target)
                {
                    const auto edge =
                        std::ranges::find_if(prepared.composition.graph->Data().edges, [&](const GraphEdge &candidate) {
                            return candidate.kind == "ProjectDependency" &&
                                   candidate.to.starts_with("Project:" + reference.name + '@');
                        });
                    const auto linkArtifact = PlannedLinkArtifact(child->build, child->binary);
                    if (!linkArtifact.empty())
                        prepared.build.links.push_back(BuildPlanLink{
                            .identity = "CMakeProjectLink:" + reference.name,
                            .graphIdentity = edge == prepared.composition.graph->Data().edges.end()
                                                 ? "Project:" + reference.name
                                                 : edge->identity,
                            .targetName = linkArtifact.generic_string(),
                            .visibility = "Private",
                            .provenance = edge == prepared.composition.graph->Data().edges.end() ? GraphProvenance{}
                                                                                                 : edge->provenance});
                    for (const auto &item : child->build.items)
                    {
                        if (item.visibility == "Private" ||
                            (item.operation != "IncludeDirectory" && item.operation != "Define" &&
                             item.operation != "CompileOption" && item.operation != "LinkOption" &&
                             item.operation != "PrecompiledHeader"))
                            continue;
                        auto propagated = item;
                        propagated.identity = "ProjectUsage:" + reference.name + ':' + item.identity;
                        propagated.visibility = "Private";
                        prepared.build.items.push_back(std::move(propagated));
                    }
                }
                prepared.projectDependencies.push_back(std::move(child));
            }
            std::ranges::sort(prepared.build.links, {}, &BuildPlanLink::identity);
            std::ranges::sort(prepared.build.items, {}, &BuildPlanItem::identity);
            prepared.build.plan.identity = FingerprintBuildPlan(prepared.build);
            return prepared;
        }

        auto AuthorizeActions(PreparedBuild &prepared, const bool locked, const CliArguments &arguments,
                              const fs::path &projectPath) -> void
        {
            for (const auto &step : prepared.actions.steps)
            {
                const auto action = std::ranges::find(prepared.composition.graph->Data().actions, step.graphIdentity,
                                                      &GraphAction::identity);
                if (action == prepared.composition.graph->Data().actions.end())
                    throw std::runtime_error("ActionPlan references an unknown graph Action");
                const auto package = std::ranges::find(prepared.composition.graph->Data().packages,
                                                       action->packageInstance, &GraphPackageInstance::identity);
                if (package == prepared.composition.graph->Data().packages.end())
                    throw std::runtime_error("Action references an unknown host PackageInstance");
                PackageProviderResult provider{.coordinate = package->coordinate,
                                               .providerKind = package->providerKind,
                                               .nativeIdentity = package->providerIdentity,
                                               .nativeVersion = package->providerVersion,
                                               .revision = package->revision,
                                               .integrity = package->integrity,
                                               .artifactIdentity = package->artifactIdentity,
                                               .context = package->context,
                                               .hermetic = package->hermetic,
                                               .trust = package->trust,
                                               .signature = package->signature};
                if (const auto rootPath = prepared.composition.packageRoots.find(package->identity);
                    rootPath != prepared.composition.packageRoots.end())
                    provider.root = rootPath->second;
                ResolvedAction trustAction{.qualifiedAction = action->identity,
                                           .kind = action->kind,
                                           .hostInstance = PackageInstance{.providerResult = std::move(provider),
                                                                           .context = package->context,
                                                                           .compatibility = package->compatibility,
                                                                           .artifactOptions = package->artifactOptions,
                                                                           .identity = package->identity},
                                           .actionExport = action->actionExport,
                                           .toolExport = action->toolExport,
                                           .source = {}};
                ActionExecutionContext execution{.locked = locked, .nonInteractive = arguments.quiet};
                if (!trustAction.hostInstance.providerResult.root.empty())
                    execution.executableOrigin =
                        PortablePath{.value = trustAction.hostInstance.providerResult.root.generic_string(),
                                     .base = PortablePathBase::Workspace};
                auto explanation = EvaluateActionTrust(trustAction, prepared.composition.actionTrustPolicy, execution);
                if (PrintDiagnostics(explanation.diagnostics, projectPath) != 0)
                    throw std::runtime_error("Action trust validation failed for '" + action->identity + "'");
                if (explanation.decision == ActionTrustDecision::Confirm)
                {
                    std::cout << "Run Action " << action->identity << " from " << package->providerIdentity
                              << "? [y/N] " << std::flush;
                    std::string answer{};
                    std::getline(std::cin, answer);
                    if (answer != "y" && answer != "Y" && answer != "yes" && answer != "YES")
                        throw std::runtime_error("Action execution was not authorized");
                }
                else if (!explanation.Allowed())
                    throw std::runtime_error("Action execution denied for '" + action->identity +
                                             "': " + explanation.reason);
                if (!arguments.quiet)
                    std::cout << "Authorized Action " << action->identity << ": " << explanation.reason << '\n';
            }
            for (auto &child : prepared.projectDependencies)
                AuthorizeActions(*child, locked, arguments, child->composition.projectDirectory);
        }

        [[nodiscard]] auto PrepareBuild(const fs::path &root, const CliArguments &arguments,
                                        const std::string_view command) -> PreparedBuild
        {
            const auto projectPath = FindProject(root, arguments);
            auto composition = Resolve(projectPath, arguments, command);
            if (PrintDiagnostics(composition.diagnostics, projectPath) != 0 || !composition.graph)
                throw std::runtime_error("composition resolution failed");
            const auto selection = composition.graph->Data().selection.configuration + '-' +
                                   composition.graph->Data().selection.targetOperatingSystem + '-' +
                                   composition.graph->Data().selection.compiler;
            const auto output = arguments.outputPath ? fs::weakly_canonical(*arguments.outputPath)
                                                     : projectPath.parent_path() / ".ngin/build" / selection;
            bool locked = false;
            const auto lockPath = arguments.lockPath.has_value() ? fs::path{*arguments.lockPath}
                                                                 : projectPath.parent_path() / "ngin.lock";
            if (fs::is_regular_file(lockPath))
            {
                const auto parsed = ParseDependencyLock(ReadText(lockPath));
                if (PrintDiagnostics(parsed.diagnostics, lockPath) != 0 || !parsed.Succeeded())
                    throw std::runtime_error("dependency lock is invalid");
                const auto verification =
                    VerifyDependencyLock(*parsed.lock, CreateCompositionDependencyLock(composition));
                if (!verification.reusable)
                {
                    for (const auto &item : verification.invalidations)
                        std::cerr << "error: " << item.package << ' ' << item.field << ": " << item.reason << '\n';
                    throw std::runtime_error("dependency lock does not match the resolved PackageInstances");
                }
                locked = true;
            }
            else if (composition.providerLockRequired &&
                     !CreateCompositionDependencyLock(composition).Data().packages.empty())
                throw std::runtime_error("workspace PackageProvider policy requires a "
                                         "dependency lock; run 'ngin package lock'");
            auto prepared = DerivePreparedBuild(std::move(composition), projectPath, command, output);
            AuthorizeActions(prepared, locked, arguments, projectPath);
            return prepared;
        }

        [[nodiscard]] auto GenerateActionContext(const PreparedBuild &prepared, const ActionPlanStep &step)
            -> std::string
        {
            std::ostringstream out{};
            const auto &selection = prepared.composition.graph->Data().selection;
            out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                << "<GeneratorContext Version=\"1\" Generator=\"" << EscapeXml(step.graphIdentity) << "\" Project=\""
                << EscapeXml(prepared.composition.graph->Data().product.name) << "\" Configuration=\""
                << EscapeXml(selection.configuration) << "\" OperatingSystem=\""
                << EscapeXml(selection.targetOperatingSystem) << "\" Architecture=\""
                << EscapeXml(selection.targetArchitecture) << "\" ProjectDir=\""
                << EscapeXml(prepared.composition.projectDirectory.generic_string()) << "\" OutputDir=\""
                << EscapeXml(prepared.output.generic_string()) << "\" GeneratedDir=\""
                << EscapeXml((prepared.output / "actions").generic_string()) << "\">\n";
            out << "  <Sources>\n";
            for (const auto &input : step.inputs)
            {
                const auto extension = fs::path{input}.extension().string();
                const auto role = extension == ".h" || extension == ".hh" || extension == ".hpp" || extension == ".hxx"
                                      ? "Header"
                                      : "Source";
                out << "    <File Path=\"" << EscapeXml(input) << "\" Role=\"" << role << "\" />\n";
            }
            out << "  </Sources>\n  <IncludeDirectories>\n";
            std::set<std::string, std::less<>> includeDirectories{};
            for (const auto &input : step.inputs)
                includeDirectories.insert(fs::path{input}.parent_path().generic_string());
            for (const auto &directory : includeDirectories)
                out << "    <IncludeDirectory Path=\"" << EscapeXml(directory) << "\" />\n";
            out << "  </IncludeDirectories>\n  <Arguments>\n";
            for (const auto &argument : step.arguments)
                out << "    <Argument Value=\"" << EscapeXml(argument) << "\" />\n";
            out << "  </Arguments>\n  <Options>\n";
            for (const auto &[name, value] : step.options)
                out << "    <Option Name=\"" << EscapeXml(name) << "\" Value=\"" << EscapeXml(value) << "\" />\n";
            out << "  </Options>\n  <Outputs>\n";
            for (const auto &output : step.outputs)
                out << "    <Generated Path=\"" << EscapeXml(output) << "\" Role=\"Source\" />\n";
            out << "  </Outputs>\n</GeneratorContext>\n";
            return out.str();
        }

        auto PrepareIsolatedPackages(const PreparedBuild &prepared) -> void
        {
            for (const auto &package : prepared.build.packages)
            {
                if (package.kind != CMakeIntegrationKind::Isolated) continue;
                const auto binary = prepared.binary / package.binaryDirectory;
                const auto install = prepared.binary / package.installedPrefix;
                auto configure = "cmake -S " + QuoteProcessArgument(package.source) + " -B " +
                                 QuoteProcessArgument(binary) +
                                 " -G Ninja -DCMAKE_BUILD_TYPE=" + QuoteProcessArgument(prepared.build.configuration) +
                                 " -DCMAKE_INSTALL_PREFIX=" + QuoteProcessArgument(install);
                for (const auto &cache : package.cache)
                    configure += " -D" + cache.name + ':' + cache.type + '=' + QuoteProcessArgument(cache.value);
                Execute(configure);
                Execute("cmake --build " + QuoteProcessArgument(binary) + " --target install");
            }
        }

        auto Configure(PreparedBuild &prepared) -> void
        {
            PrepareIsolatedPackages(prepared);
            fs::create_directories(prepared.generated);
            for (const auto &action : prepared.actions.steps)
            {
                for (const auto &output : action.outputs) fs::create_directories(fs::path{output}.parent_path());
                fs::create_directories(action.workingDirectory);
                if (!action.contextFile.empty()) WriteText(action.contextFile, GenerateActionContext(prepared, action));
            }
            WriteText(prepared.generated / "CMakeLists.txt", GenerateCMakeProject(prepared.build, prepared.actions));
            auto command = "cmake -S " + QuoteProcessArgument(prepared.generated) + " -B " +
                           QuoteProcessArgument(prepared.binary) +
                           " -G Ninja -DCMAKE_BUILD_TYPE=" + QuoteProcessArgument(prepared.build.configuration) +
                           " -DCMAKE_EXPORT_COMPILE_COMMANDS=ON";
            if (std::ranges::none_of(prepared.build.items,
                                     [](const BuildPlanItem &item) { return item.operation == "CxxModule"; }))
                command += " -DCMAKE_CXX_SCAN_FOR_MODULES=OFF";
            if (prepared.build.toolchainFile.has_value())
                command += " -DCMAKE_TOOLCHAIN_FILE=" + QuoteProcessArgument(*prepared.build.toolchainFile);
            else if (!prepared.build.compiler.empty() && prepared.build.compiler != "default")
            {
                auto compiler = prepared.build.compiler;
                if (compiler == "MSVC" || compiler == "msvc")
                    compiler = "cl";
                else if (compiler == "Clang" || compiler == "clang")
                    compiler = "clang++";
                else if (compiler == "GCC" || compiler == "gcc")
                    compiler = "g++";
                command += " -DCMAKE_CXX_COMPILER=" + QuoteProcessArgument(compiler);
            }
            Execute(command);
        }

        auto ConfigureTree(PreparedBuild &prepared) -> void
        {
            for (auto &child : prepared.projectDependencies) ConfigureTree(*child);
            Configure(prepared);
        }

        auto Build(PreparedBuild &prepared) -> void
        {
            for (auto &child : prepared.projectDependencies) Build(*child);
            Configure(prepared);
            Execute("cmake --build " + QuoteProcessArgument(prepared.binary) + " --target " +
                    QuoteProcessArgument(prepared.build.targetName));
        }

        [[nodiscard]] auto ProductArtifact(const PreparedBuild &prepared) -> fs::path
        {
            return PlannedProductArtifact(prepared.build, prepared.binary);
        }

        struct PreparedStage
        {
            PreparedBuild build{};
            StagePlanBindings bindings{};
            StagePlan plan{};
        };

        auto AppendReferencedStageItems(const PreparedBuild &prepared, const fs::path &stageRoot, StagePlan &combined)
            -> void
        {
            for (const auto &child : prepared.projectDependencies)
            {
                StagePlanBindings bindings{.projectRoot = child->composition.projectDirectory,
                                           .stageRoot = stageRoot,
                                           .packageRoots = child->composition.packageRoots,
                                           .targetCaseInsensitive =
                                               child->composition.graph->Data().selection.targetOperatingSystem ==
                                               "windows",
                                           .allowSymlinks = child->composition.allowSymlinks};
                const auto artifact = ProductArtifact(*child);
                if (fs::exists(artifact))
                    bindings.productArtifacts.emplace(child->composition.graph->Data().product.identity, artifact);
                auto stage = DeriveStagePlan(*child->composition.graph, bindings);
                if (PrintDiagnostics(stage.diagnostics, child->composition.projectDirectory) != 0 || !stage.Succeeded())
                    throw std::runtime_error("referenced Project StagePlan derivation failed");
                combined.items.insert(combined.items.end(), stage.plan->items.begin(), stage.plan->items.end());
                AppendReferencedStageItems(*child, stageRoot, combined);
            }
        }

        [[nodiscard]] auto Stage(PreparedBuild prepared) -> PreparedStage
        {
            Build(prepared);
            StagePlanBindings bindings{.projectRoot = prepared.composition.projectDirectory,
                                       .stageRoot = prepared.output / "stage",
                                       .packageRoots = prepared.composition.packageRoots,
                                       .targetCaseInsensitive =
                                           prepared.composition.graph->Data().selection.targetOperatingSystem ==
                                           "windows",
                                       .allowSymlinks = prepared.composition.allowSymlinks};
            const auto artifact = ProductArtifact(prepared);
            if (fs::exists(artifact))
                bindings.productArtifacts.emplace(prepared.composition.graph->Data().product.identity, artifact);
            auto stage = DeriveStagePlan(*prepared.composition.graph, bindings);
            if (PrintDiagnostics(stage.diagnostics, prepared.composition.projectDirectory) != 0 || !stage.Succeeded())
                throw std::runtime_error("StagePlan derivation failed");
            AppendReferencedStageItems(prepared, bindings.stageRoot, *stage.plan);
            std::map<std::string, StagePlanItem, std::less<>> destinations{};
            for (const auto &item : stage.plan->items)
            {
                auto key = item.destination;
                if (bindings.targetCaseInsensitive)
                    std::ranges::transform(key, key.begin(), [](const unsigned char value) {
                        return static_cast<char>(std::tolower(value));
                    });
                const auto existing = destinations.find(key);
                if (existing != destinations.end() && existing->second.source != item.source)
                {
                    if (prepared.composition.stageCollision != WorkspaceStageCollisionPolicy::IdenticalBytes ||
                        !FilesHaveIdenticalBytes(existing->second.source, item.source))
                        throw std::runtime_error("StagePlan collision at '" + item.destination + "' between '" +
                                                 existing->second.owner + "' and '" + item.owner + "'");
                }
                destinations.try_emplace(std::move(key), item);
            }
            stage.plan->items.clear();
            for (auto &[_, item] : destinations) stage.plan->items.push_back(std::move(item));
            std::ranges::sort(stage.plan->items, {}, &StagePlanItem::identity);
            stage.plan->plan.identity = FingerprintStagePlan(*stage.plan);
            const auto executed = ExecuteStagePlan(*stage.plan);
            if (PrintDiagnostics(executed.diagnostics, prepared.composition.projectDirectory) != 0)
                throw std::runtime_error("staging failed");
            return PreparedStage{
                .build = std::move(prepared), .bindings = std::move(bindings), .plan = std::move(*stage.plan)};
        }
    } // namespace

    auto ParseCliArguments(const int argc, char **argv, const int first) -> CliArguments
    {
        CliArguments result{};
        const auto value = [&](int &index, const std::string_view option) -> std::string {
            if (++index >= argc) throw std::runtime_error(std::string(option) + " requires a value");
            return argv[index];
        };
        for (int index = first; index < argc; ++index)
        {
            const std::string argument = argv[index];
            if (argument == "--")
            {
                for (++index; index < argc; ++index) result.trailing.emplace_back(argv[index]);
                break;
            }
            if (argument == "--project")
                result.projectPath = value(index, argument);
            else if (argument == "--workspace")
                result.workspacePath = value(index, argument);
            else if (argument == "--output")
                result.outputPath = value(index, argument);
            else if (argument == "--lock")
                result.lockPath = value(index, argument);
            else if (argument == "--against")
                result.againstPath = value(index, argument);
            else if (argument == "--format")
                result.format = value(index, argument);
            else if (argument == "--configuration")
                result.configuration = value(index, argument);
            else if (argument == "--target")
                result.target = value(index, argument);
            else if (argument == "--toolchain")
                result.toolchain = value(index, argument);
            else if (argument == "--preset")
                result.preset = value(index, argument);
            else if (argument == "--exact")
                result.exactVersion = value(index, argument);
            else if (argument == "--compatible")
                result.compatibleVersion = value(index, argument);
            else if (argument == "--at-least")
                result.atLeastVersion = value(index, argument);
            else if (argument == "--after")
                result.afterVersion = value(index, argument);
            else if (argument == "--at-most")
                result.atMostVersion = value(index, argument);
            else if (argument == "--before")
                result.beforeVersion = value(index, argument);
            else if (argument == "--use")
                result.exportUses.push_back(value(index, argument));
            else if (argument == "--option")
                result.optionAssignments.push_back(value(index, argument));
            else if (argument == "--kind")
                result.actionKind = value(index, argument);
            else if (argument == "--quiet" || argument == "-q")
                result.quiet = true;
            else if (argument.starts_with('-'))
                throw std::runtime_error("unknown option: " + argument);
            else
                result.positional.push_back(argument);
        }
        return result;
    }

    auto FindProject(const fs::path &root, const CliArguments &arguments) -> fs::path
    {
        if (arguments.projectPath) return fs::weakly_canonical(*arguments.projectPath);
        std::vector<fs::path> found{};
        for (const auto &entry : fs::directory_iterator(root))
            if (entry.is_regular_file() && entry.path().extension() == ".nginproj") found.push_back(entry.path());
        if (found.size() != 1) throw std::runtime_error("select exactly one project with --project");
        return fs::weakly_canonical(found.front());
    }

    auto NewProject(const fs::path &root, const std::string_view kind, const std::string_view name) -> int
    {
        const std::map<std::string_view, std::string_view> kinds{
            {"app", "Application"},     {"lib", "Library"},   {"tool", "Tool"},        {"test", "Test"},
            {"benchmark", "Benchmark"}, {"plugin", "Plugin"}, {"external", "External"}};
        const auto found = kinds.find(kind);
        if (found == kinds.end()) throw std::runtime_error("unknown project kind");
        const auto directory = root / name;
        if (fs::exists(directory)) throw std::runtime_error(directory.string() + ": already exists");
        const auto build =
            found->second == "External" ? std::string{} : "  <Build><Source Include=\"src/**/*.cpp\" /></Build>\n";
        WriteText(directory / (std::string{name} + ".nginproj"),
                  "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<Project Name=\"" + EscapeXml(name) + "\" Type=\"" +
                      std::string{found->second} + "\">\n" + build + "</Project>\n");
        if (found->second == "Library")
        {
            WriteText(directory / "include" / (std::string{name} + ".hpp"), "#pragma once\n");
            WriteText(directory / "src" / (std::string{name} + ".cpp"), "#include \"" + std::string{name} + ".hpp\"\n");
        }
        else if (found->second != "External")
            WriteText(directory / "src/main.cpp", "int main() { return 0; }\n");
        std::cout << "Created " << found->second << " project at " << directory << '\n';
        return 0;
    }

    auto ValidateManifest(const fs::path &root, const CliArguments &arguments) -> int
    {
        fs::path path{};
        if (arguments.projectPath)
            path = fs::weakly_canonical(*arguments.projectPath);
        else if (arguments.workspacePath)
            path = fs::weakly_canonical(*arguments.workspacePath);
        else if (fs::is_regular_file(root / "NGIN.ngin"))
            path = fs::weakly_canonical(root / "NGIN.ngin");
        else
            path = FindProject(root, arguments);
        const auto authored = ParseAuthoredManifest(path);
        auto diagnostics = authored.diagnostics;
        std::string kind = "Manifest";
        std::string name = path.filename().string();
        if (authored.value)
            std::visit(
                [&](const auto &manifest) {
                    name = manifest.name;
                    using T = std::decay_t<decltype(manifest)>;
                    if constexpr (std::is_same_v<T, AuthoredProjectManifest>)
                    {
                        kind = "Project";
                        const auto value = ParseSemanticProject(manifest);
                        diagnostics.insert(diagnostics.end(), value.diagnostics.begin(), value.diagnostics.end());
                    }
                    else if constexpr (std::is_same_v<T, AuthoredPackageManifest>)
                    {
                        kind = "Package";
                        const auto value = ParseSemanticPackage(manifest);
                        diagnostics.insert(diagnostics.end(), value.diagnostics.begin(), value.diagnostics.end());
                    }
                    else
                    {
                        kind = "Workspace";
                        const auto value = ParseSemanticWorkspace(manifest);
                        diagnostics.insert(diagnostics.end(), value.diagnostics.begin(), value.diagnostics.end());
                    }
                },
                *authored.value);
        if (PrintDiagnostics(diagnostics, path) != 0) return 1;
        if (!arguments.quiet) std::cout << "Validation passed: " << kind << ' ' << name << '\n';
        return 0;
    }

    auto FormatManifest(const fs::path &root, const CliArguments &arguments) -> int
    {
        const auto path =
            arguments.projectPath ? fs::weakly_canonical(*arguments.projectPath) : FindProject(root, arguments);
        WriteText(path, FormatManifestFile(path));
        std::cout << "Formatted " << path << '\n';
        return 0;
    }

    auto PrintSchema(const CliArguments &arguments) -> int
    {
        if (arguments.format && *arguments.format != "json")
            throw std::runtime_error("schema supports only JSON metadata");
        std::cout << GenerateManifestArtifacts().at("manifest-editor-metadata.json");
        return 0;
    }

    auto AddPackage(const fs::path &root, const CliArguments &arguments) -> int
    {
        if (!arguments.packageName) throw std::runtime_error("package add requires a name");
        const auto path = FindProject(root, arguments);
        const auto project = LoadProject(path);
        if (FindPackage(project, *arguments.packageName)) throw std::runtime_error("package is already declared");
        WriteText(path, FormatManifestXml(InsertDependency(ReadText(path), project, PackageMarkup(arguments))));
        return 0;
    }

    auto UpdatePackage(const fs::path &root, const CliArguments &arguments) -> int
    {
        if (!arguments.packageName) throw std::runtime_error("package update requires a name");
        const auto path = FindProject(root, arguments);
        const auto project = LoadProject(path);
        const auto *package = FindPackage(project, *arguments.packageName);
        if (!package) throw std::runtime_error("package is not declared");
        auto effective = arguments;
        if (effective.exportUses.empty())
            for (const auto &child : package->children)
                if (child.specId == "project.dependencies.use")
                    for (const auto *kind : {"Library", "Tool", "Plugin", "Action", "Asset"})
                        if (const auto *value = child.Attribute(kind))
                            effective.exportUses.push_back(std::string{kind} + ':' + value->value);
        if (effective.optionAssignments.empty())
            for (const auto &child : package->children)
                if (child.specId == "project.dependencies.option")
                    effective.optionAssignments.push_back(child.Attribute("Name")->value + '=' +
                                                          child.Attribute("Value")->value);
        auto text = ReadText(path);
        auto replacement = PackageMarkup(effective);
        text.replace(package->source.begin.offset, package->source.end.offset - package->source.begin.offset,
                     replacement.substr(4));
        WriteText(path, FormatManifestXml(text));
        return 0;
    }

    auto RemovePackage(const fs::path &root, const CliArguments &arguments) -> int
    {
        if (!arguments.packageName) throw std::runtime_error("package remove requires a name");
        const auto path = FindProject(root, arguments);
        const auto project = LoadProject(path);
        const auto *package = FindPackage(project, *arguments.packageName);
        if (!package) throw std::runtime_error("package is not declared");
        auto text = ReadText(path);
        text.erase(package->source.begin.offset, package->source.end.offset - package->source.begin.offset);
        WriteText(path, FormatManifestXml(text));
        return 0;
    }

    auto AddProjectReference(const fs::path &root, const CliArguments &arguments) -> int
    {
        if (arguments.positional.empty()) throw std::runtime_error("project-reference requires a path");
        const auto path = FindProject(root, arguments);
        const auto project = LoadProject(path);
        const auto relative = arguments.positional.front();
        const auto referenced = LoadProject(fs::weakly_canonical(path.parent_path() / relative));
        const auto declaration =
            "    <Project Name=\"" + EscapeXml(referenced.name) + "\" Path=\"" + EscapeXml(relative) + "\" />\n";
        WriteText(path, FormatManifestXml(InsertDependency(ReadText(path), project, declaration)));
        return 0;
    }

    auto AddAction(const fs::path &root, const CliArguments &arguments) -> int
    {
        if (arguments.positional.empty()) throw std::runtime_error("action requires Package::Action");
        const auto qualified = arguments.positional.front();
        if (!qualified.contains("::")) throw std::runtime_error("action must be package-qualified");
        const auto kind = arguments.actionKind.value_or("Generate");
        const auto path = FindProject(root, arguments);
        const auto project = LoadProject(path);
        auto text = ReadText(path);
        auto close = text.rfind("</Project>");
        if (close == std::string::npos)
        {
            const auto end = text.find('>', project.root.source.begin.offset);
            const auto slash = text.find_last_not_of(" \t\r\n", end - 1);
            text.replace(slash, end - slash + 1, ">\n</Project>");
            close = text.rfind("</Project>");
        }
        if (kind == "Generate")
            text.insert(close, "  <Generate Action=\"" + EscapeXml(qualified) + "\" />\n");
        else
        {
            if (kind != "Analyze" && kind != "Format" && kind != "Validate" && kind != "Custom")
                throw std::runtime_error("invalid Action kind");
            text.insert(close, "  <Tooling><" + kind + " Action=\"" + EscapeXml(qualified) + "\" /></Tooling>\n");
        }
        WriteText(path, FormatManifestXml(text));
        return 0;
    }

    auto InspectComposition(const fs::path &root, const CliArguments &arguments) -> int
    {
        const auto project = FindProject(root, arguments);
        auto resolved = Resolve(project, arguments);
        if (PrintDiagnostics(resolved.diagnostics, project) != 0 || !resolved.graph) return 1;
        std::cout << resolved.graph->CanonicalSerialization() << '\n';
        return 0;
    }

    auto DiffComposition(const fs::path &root, const CliArguments &arguments) -> int
    {
        const auto beforePath = FindProject(root, arguments);
        fs::path afterPath{};
        if (arguments.againstPath.has_value())
            afterPath = fs::weakly_canonical(*arguments.againstPath);
        else if (!arguments.positional.empty())
            afterPath = fs::weakly_canonical(arguments.positional.front());
        else
            throw std::runtime_error("diff requires --against <project.nginproj>");

        auto before = Resolve(beforePath, arguments, "diff");
        auto afterArguments = arguments;
        afterArguments.projectPath = afterPath.string();
        afterArguments.againstPath.reset();
        afterArguments.positional.clear();
        auto after = Resolve(afterPath, afterArguments, "diff");
        if (PrintDiagnostics(before.diagnostics, beforePath) != 0 || !before.graph) return 1;
        if (PrintDiagnostics(after.diagnostics, afterPath) != 0 || !after.graph) return 1;

        const auto differences = DiffCompositionGraphs(*before.graph, *after.graph);
        if (arguments.format.value_or("text") == "json")
        {
            std::cout << "[";
            for (std::size_t index = 0; index < differences.size(); ++index)
            {
                const auto &item = differences[index];
                if (index != 0) std::cout << ',';
                std::cout << "{\"category\":\"" << EscapeJson(item.category) << "\",\"identity\":\""
                          << EscapeJson(item.identity) << "\",\"change\":\"" << EscapeJson(item.change) << '"';
                if (item.before.has_value()) std::cout << ",\"before\":\"" << EscapeJson(*item.before) << '"';
                if (item.after.has_value()) std::cout << ",\"after\":\"" << EscapeJson(*item.after) << '"';
                std::cout << '}';
            }
            std::cout << "]\n";
        }
        else
        {
            for (const auto &item : differences)
                std::cout << item.change << ' ' << item.category << ' ' << item.identity << '\n';
            if (differences.empty()) std::cout << "No composition differences\n";
        }
        return differences.empty() ? 0 : 2;
    }

    auto ExplainComposition(const fs::path &root, const CliArguments &arguments) -> int
    {
        if (arguments.positional.empty()) throw std::runtime_error("explain requires a graph identity");
        const auto project = FindProject(root, arguments);
        auto resolved = Resolve(project, arguments, "explain");
        if (PrintDiagnostics(resolved.diagnostics, project) != 0 || !resolved.graph) return 1;
        const auto explanation = ExplainCompositionIdentity(*resolved.graph, arguments.positional.front());
        if (!explanation.has_value())
            throw std::runtime_error("unknown graph identity '" + arguments.positional.front() + "'");

        if (arguments.format.value_or("text") == "json")
        {
            std::cout << "{\"identity\":\"" << EscapeJson(explanation->identity) << "\",\"category\":\""
                      << EscapeJson(explanation->category) << "\",\"value\":\"" << EscapeJson(explanation->value)
                      << "\",\"provenance\":[";
            for (std::size_t index = 0; index < explanation->provenance.size(); ++index)
            {
                const auto &source = explanation->provenance[index];
                if (index != 0) std::cout << ',';
                std::cout << "{\"kind\":\"" << EscapeJson(source.kind) << "\",\"owner\":\"" << EscapeJson(source.owner)
                          << "\",\"document\":\"" << EscapeJson(source.document) << "\",\"line\":" << source.line
                          << ",\"column\":" << source.column << ",\"reason\":\"" << EscapeJson(source.reason) << "\"}";
            }
            std::cout << "],\"edges\":[";
            for (std::size_t index = 0; index < explanation->edges.size(); ++index)
            {
                const auto &edge = explanation->edges[index];
                if (index != 0) std::cout << ',';
                std::cout << "{\"identity\":\"" << EscapeJson(edge.identity) << "\",\"from\":\""
                          << EscapeJson(edge.from) << "\",\"to\":\"" << EscapeJson(edge.to) << "\",\"kind\":\""
                          << EscapeJson(edge.kind) << "\"}";
            }
            std::cout << "]}\n";
        }
        else
        {
            std::cout << explanation->category << ' ' << explanation->identity << "\n  " << explanation->value << '\n';
            for (const auto &source : explanation->provenance)
                std::cout << "  from " << source.document << ':' << source.line << ':' << source.column << " ("
                          << source.reason << ")\n";
            for (const auto &edge : explanation->edges)
                std::cout << "  " << edge.kind << ": " << edge.from << " -> " << edge.to << '\n';
        }
        return 0;
    }

    auto ConfigureProject(const fs::path &root, const CliArguments &arguments) -> int
    {
        auto prepared = PrepareBuild(root, arguments, "configure");
        ConfigureTree(prepared);
        std::cout << "Configured " << prepared.build.targetName << " in " << prepared.binary << '\n';
        return 0;
    }

    auto BuildProject(const fs::path &root, const CliArguments &arguments) -> int
    {
        auto prepared = PrepareBuild(root, arguments, "build");
        Build(prepared);
        std::cout << "Built " << prepared.build.targetName << '\n';
        return 0;
    }

    auto StageProject(const fs::path &root, const CliArguments &arguments) -> int
    {
        auto staged = Stage(PrepareBuild(root, arguments, "stage"));
        std::cout << "Staged " << staged.build.build.targetName << " in " << staged.bindings.stageRoot << '\n';
        return 0;
    }

    auto RunProject(const fs::path &root, const CliArguments &arguments) -> int
    {
        auto staged = Stage(PrepareBuild(root, arguments, "run"));
        auto launch = DeriveLaunchPlan(*staged.build.composition.graph, staged.plan, staged.bindings);
        if (PrintDiagnostics(launch.diagnostics, staged.build.composition.projectDirectory) != 0 || !launch.Succeeded())
            throw std::runtime_error("LaunchPlan derivation failed");
        if (!launch.plan->secretReferences.empty())
            throw std::runtime_error("LaunchPlan requires an external secret provider, "
                                     "but none is configured");
        auto processArguments = launch.plan->arguments;
        processArguments.insert(processArguments.end(), arguments.trailing.begin(), arguments.trailing.end());
        ExecuteProcess(launch.plan->executable, processArguments, fs::path{launch.plan->workingDirectory},
                       launch.plan->environment);
        return 0;
    }

    auto TestProject(const fs::path &root, const CliArguments &arguments) -> int
    {
        auto staged = Stage(PrepareBuild(root, arguments, "test"));
        auto test = DeriveTestPlan(*staged.build.composition.graph, staged.plan);
        if (PrintDiagnostics(test.diagnostics, staged.build.composition.projectDirectory) != 0 || !test.Succeeded())
            throw std::runtime_error("TestPlan derivation failed");
        auto processArguments = test.plan->arguments;
        processArguments.insert(processArguments.end(), arguments.trailing.begin(), arguments.trailing.end());
        ExecuteProcess(test.plan->executable, processArguments, staged.bindings.stageRoot, {},
                       test.plan->timeoutSeconds);
        return 0;
    }

    auto PublishProject(const fs::path &root, const CliArguments &arguments) -> int
    {
        auto staged = Stage(PrepareBuild(root, arguments, "publish"));
        const auto &publishes = staged.build.composition.graph->Data().publishes;
        const auto name = arguments.positional.empty()
                              ? (publishes.size() == 1 ? publishes.front().name : std::string{})
                              : arguments.positional.front();
        if (name.empty()) throw std::runtime_error("select a Publish name");
        auto publish = DerivePublishPlan(*staged.build.composition.graph, staged.plan, name);
        if (PrintDiagnostics(publish.diagnostics, staged.build.composition.projectDirectory) != 0 ||
            !publish.Succeeded())
            throw std::runtime_error("PublishPlan derivation failed");
        const auto configuration = staged.build.output / ("CPack-" + name + ".cmake");
        WriteText(configuration, GenerateCPackConfiguration(*publish.plan));
        Execute("cpack --config " + QuoteProcessArgument(configuration));
        return 0;
    }

    auto ExecuteProjectActions(const fs::path &root, const CliArguments &arguments, const std::string_view command)
        -> int
    {
        if (command != "analyze" && command != "format")
            throw std::runtime_error("unsupported Action command '" + std::string(command) + "'");
        auto prepared = PrepareBuild(root, arguments, command);
        const auto actionCount = [](const auto &self, const PreparedBuild &build) -> std::size_t {
            auto count = build.actions.steps.size();
            for (const auto &child : build.projectDependencies) count += self(self, *child);
            return count;
        };
        if (actionCount(actionCount, prepared) == 0)
            throw std::runtime_error("project selects no " + std::string(command) + " Actions");
        const auto execute = [](const auto &self, PreparedBuild &build) -> void {
            for (auto &child : build.projectDependencies) self(self, *child);
            Configure(build);
            for (const auto &step : build.actions.steps)
            {
                auto target = std::string{"ngin_action_"} + step.graphIdentity;
                for (auto &character : target)
                    if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_' && character != '.')
                        character = '_';
                Execute("cmake --build " + QuoteProcessArgument(build.binary) + " --target " +
                        QuoteProcessArgument(target));
            }
        };
        execute(execute, prepared);
        return 0;
    }

    auto RestorePackages(const fs::path &root, const CliArguments &arguments) -> int
    {
        const auto project = FindProject(root, arguments);
        auto resolved = Resolve(project, arguments);
        if (PrintDiagnostics(resolved.diagnostics, project) != 0 || !resolved.graph) return 1;
        const auto lock = CreateCompositionDependencyLock(resolved);
        std::cout << "Resolved " << lock.Data().packages.size() << " exact PackageInstances\n";
        for (const auto &package : lock.Data().packages)
            std::cout << "  " << package.coordinate.name << '@' << package.coordinate.exactVersion << " ["
                      << package.providerKind << "]\n";
        return 0;
    }

    auto WriteDependencyLock(const fs::path &root, const CliArguments &arguments) -> int
    {
        const auto project = FindProject(root, arguments);
        auto resolved = Resolve(project, arguments);
        if (PrintDiagnostics(resolved.diagnostics, project) != 0 || !resolved.graph) return 1;
        const auto lock = CreateCompositionDependencyLock(resolved);
        const auto path = arguments.outputPath ? fs::path{*arguments.outputPath} : project.parent_path() / "ngin.lock";
        WriteText(path, lock.CanonicalSerialization() + '\n');
        std::cout << "Wrote dependency lock " << path << "\n  fingerprint: " << lock.Fingerprint() << '\n';
        return 0;
    }

    auto VerifyDependencyLockFile(const fs::path &root, const CliArguments &arguments) -> int
    {
        const auto project = FindProject(root, arguments);
        const auto path = arguments.lockPath ? fs::path{*arguments.lockPath} : project.parent_path() / "ngin.lock";
        const auto parsed = ParseDependencyLock(ReadText(path));
        if (PrintDiagnostics(parsed.diagnostics, path) != 0 || !parsed.Succeeded()) return 1;
        auto resolved = Resolve(project, arguments);
        if (PrintDiagnostics(resolved.diagnostics, project) != 0 || !resolved.graph) return 1;
        const auto actual = CreateCompositionDependencyLock(resolved);
        const auto verification = VerifyDependencyLock(*parsed.lock, actual);
        if (verification.reusable)
        {
            std::cout << "Dependency lock verified\n";
            return 0;
        }
        for (const auto &item : verification.invalidations)
            std::cout << "error: " << item.package << ' ' << item.field << ": " << item.reason << '\n';
        return 1;
    }
} // namespace NGIN::CLI
