#include "Commands.hpp"

#include "Authoring.hpp"
#include "Build.hpp"
#include "Diagnostics.hpp"
#include "MetaGen.hpp"
#include "Resolution.hpp"
#include "Support.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace NGIN::CLI
{
    namespace
    {
        struct LoadedInvocation
        {
            ProjectManifest project{};
            ProfileDefinition profile{};
        };

        [[nodiscard]] auto ResolveInvocation(const ParsedArgs &args) -> LoadedInvocation
        {
            const auto project = LoadProjectManifest(ResolveProjectPath(args.projectPath));
            const auto &profile = ProfileByName(project, args.profileName);
            return LoadedInvocation{
                .project = project,
                .profile = profile,
            };
        }

        [[nodiscard]] auto ReadTextIfExists(const fs::path &path) -> std::string
        {
            std::ifstream input(path);
            if (!input)
            {
                return {};
            }
            std::ostringstream content{};
            content << input.rdbuf();
            return content.str();
        }

        [[nodiscard]] auto ContainsGitignoreEntry(const std::string &text, std::string_view entry) -> bool
        {
            std::istringstream lines(text);
            std::string line;
            while (std::getline(lines, line))
            {
                if (line == entry)
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] auto HasSelection(const SelectorSet &selectors) -> bool
        {
            return selectors.impossible || selectors.profile.has_value() || selectors.operatingSystem.has_value()
                   || selectors.platform.has_value() || selectors.architecture.has_value() || selectors.buildType.has_value()
                   || selectors.environment.has_value() || !selectors.conditionRefs.empty();
        }

        auto PrintSelectorSummary(const SelectorSet &selectors) -> void
        {
            bool first = true;
            const auto append = [&](const std::string &name, const std::optional<std::string> &value)
            {
                if (!value.has_value())
                {
                    return;
                }
                if (!first)
                {
                    std::cout << ", ";
                }
                std::cout << name << "=\"" << *value << "\"";
                first = false;
            };
            for (const auto &condition : selectors.conditionRefs)
            {
                if (!first)
                {
                    std::cout << ", ";
                }
                std::cout << "Condition=\"" << condition << "\"";
                first = false;
            }
            append("Profile", selectors.profile);
            append("Platform", selectors.platform);
            append("OperatingSystem", selectors.operatingSystem);
            append("Architecture", selectors.architecture);
            append("BuildType", selectors.buildType);
            append("Environment", selectors.environment);
            if (selectors.impossible)
            {
                if (!first)
                {
                    std::cout << ", ";
                }
                std::cout << "contradictory";
            }
        }

        auto PrintConditionalBuildSettings(
            const std::string_view label,
            const ProjectManifest &project,
            const ProfileDefinition &profile,
            const std::vector<BuildSetting> &settings) -> void
        {
            bool printedHeader = false;
            for (const auto &setting : settings)
            {
                if (!HasSelection(setting.selectors))
                {
                    continue;
                }
                if (!printedHeader)
                {
                    std::cout << "    " << label << ":\n";
                    printedHeader = true;
                }
                std::cout << "      - " << setting.value << " "
                          << (SelectionMatches(project, setting.selectors, profile) ? "included" : "excluded")
                          << " (";
                PrintSelectorSummary(setting.selectors);
                std::cout << ")\n";
            }
        }
    }

    auto ParseCommonArgs(int argc, char **argv, int startIndex) -> ParsedArgs
    {
        ParsedArgs args{};
        for (int index = startIndex; index < argc; ++index)
        {
            const std::string current = argv[index];
            if (current == "--project" && index + 1 < argc)
            {
                args.projectPath = argv[++index];
            }
            else if (current == "--profile" && index + 1 < argc)
            {
                args.profileName = argv[++index];
            }
            else if (current == "--")
            {
                for (int argIndex = index + 1; argIndex < argc; ++argIndex)
                {
                    args.runArgs.push_back(argv[argIndex]);
                }
                break;
            }
            else if ((current == "--output" || current == "--output-dir") && index + 1 < argc)
            {
                args.outputPath = argv[++index];
            }
            else if ((current == "--dependencies" || current == "--externals") && index + 1 < argc)
            {
                args.targetDir = argv[++index];
            }
            else if (current.rfind("--", 0) == 0)
            {
                throw std::runtime_error("unknown option: " + current);
            }
            else if (!args.packageName.has_value())
            {
                args.packageName = current;
            }
            else
            {
                throw std::runtime_error("unexpected argument: " + current);
            }
        }
        return args;
    }

    auto CmdList(const fs::path &root) -> int
    {
        const auto workspace = LoadWorkspaceManifest(root);
        std::cout << "Workspace: " << workspace.name << "\n";
        std::cout << "Projects:\n";
        for (const auto &projectPath : workspace.projects)
        {
            const auto project = LoadProjectManifest(projectPath);
            std::cout << "  - " << project.name << " [" << project.type << "] " << project.path.string() << "\n";
        }
        return 0;
    }

    auto CmdStatus(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)args;
        const auto workspace = LoadWorkspaceManifest(root);
        std::cout << "Workspace: " << workspace.name << "\n";
        std::cout << "  manifest: " << workspace.path.string() << "\n";
        std::cout << "  platform version: " << workspace.platformVersion << "\n";
        std::cout << "Package sources:\n";
        for (const auto &source : workspace.packageSources)
        {
            std::cout << "  - " << source.string() << (fs::exists(source) ? "" : " [missing]") << "\n";
        }
        std::cout << "Projects:\n";
        for (const auto &projectPath : workspace.projects)
        {
            std::cout << "  - " << projectPath.string() << (fs::exists(projectPath) ? "" : " [missing]") << "\n";
        }
        return 0;
    }

    auto CmdDoctor(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)args;
        int fail = 0;
        std::cout << "NGIN workspace doctor\n";
        std::cout << "  root: " << root << "\n";
        std::cout << "  workspace manifest: " << WorkspaceFilePath(root).value_or(root / "<missing>") << "\n";
        const auto reportTool = [&root, &fail](const std::string &tool, const bool required)
        {
            const auto resolved = ResolveToolPath(tool, root);
            if (!resolved.has_value())
            {
                std::cout << (required ? "[error] " : "[warn] ") << "missing tool: " << tool << "\n";
                if (required)
                {
                    fail = 1;
                }
                return;
            }
            std::cout << "[ok] tool: " << tool << " (" << resolved->source << ") " << resolved->path.string() << "\n";
        };

        if (!ToolExists("git"))
        {
            std::cout << "[error] missing tool: git\n";
            fail = 1;
        }
        else
        {
            std::cout << "[ok] tool: git\n";
        }
        reportTool("cmake", true);
        reportTool("ninja", false);
        std::optional<WorkspaceManifest> workspace{};
        try
        {
            workspace = LoadWorkspaceManifest(root);
            std::size_t projectsParsed = 0;
            for (const auto &projectPath : workspace->projects)
            {
                (void)LoadProjectManifest(projectPath);
                ++projectsParsed;
            }
            const auto catalog = LoadPackageCatalog(workspace, root);
            std::cout << "[ok] XML manifests parse\n";
            std::cout << "[ok] projects: " << projectsParsed << "\n";
            std::cout << "[ok] packages indexed: " << catalog.size() << "\n";
        }
        catch (const std::exception &ex)
        {
            std::cout << "[error] " << ex.what() << "\n";
            fail = 1;
        }

        if (!workspace.has_value())
        {
            std::cout << "\ndoctor result: FAIL\n";
            return 1;
        }
        for (const auto &source : workspace->packageSources)
        {
            if (!fs::exists(source))
            {
                std::cout << "[warn] package source missing: " << source.string() << "\n";
                fail = 1;
            }
        }
        std::cout << "\ndoctor result: " << (fail == 0 ? "PASS" : "FAIL") << "\n";
        return fail;
    }

    auto CmdPackageList(const fs::path &root) -> int
    {
        const auto workspace = LoadWorkspaceManifest(root);
        const auto catalog = LoadPackageCatalog(workspace, root);
        std::vector<std::string> names{};
        for (const auto &[name, _] : catalog)
        {
            names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        for (const auto &name : names)
        {
            const auto &entry = catalog.at(name);
            const auto manifest = LoadPackageManifest(entry.manifestPath);
            std::cout << manifest.name << " " << manifest.version << " " << entry.manifestPath.string();
            if (!entry.providerRoot.empty())
            {
                std::cout << " provider=" << entry.providerRoot.string();
            }
            std::cout << "\n";
        }
        return 0;
    }

    auto CmdPackageShow(const fs::path &root, const ParsedArgs &args) -> int
    {
        if (!args.packageName.has_value())
        {
            throw std::runtime_error("package show requires a package name");
        }
        const auto workspace = LoadWorkspaceManifest(root);
        const auto catalog = LoadPackageCatalog(workspace, root);
        const auto it = catalog.find(*args.packageName);
        if (it == catalog.end())
        {
            throw std::runtime_error("unknown package '" + *args.packageName + "'");
        }
        const auto manifest = LoadPackageManifest(it->second.manifestPath);
        std::cout << "Package: " << manifest.name << "\n";
        std::cout << "  version: " << manifest.version << "\n";
        std::cout << "  manifest: " << manifest.path << "\n";
        std::cout << "  provider root: ";
        if (it->second.providerRoot.empty())
        {
            std::cout << "(none)\n";
        }
        else
        {
            std::cout << it->second.providerRoot << "\n";
        }
        std::cout << "  build backend: " << (manifest.build.backend.empty() ? "(none)" : manifest.build.backend) << "\n";
        std::cout << "  libraries: " << manifest.artifacts.libraries.size() << "\n";
        for (const auto &library : manifest.artifacts.libraries)
        {
            std::cout << "    - " << library.name;
            if (!library.target.empty())
            {
                std::cout << " target=" << library.target;
            }
            if (!library.linkage.empty())
            {
                std::cout << " linkage=" << library.linkage;
            }
            if (!library.origin.empty())
            {
                std::cout << " origin=" << library.origin;
            }
            if (!library.exported)
            {
                std::cout << " internal";
            }
            std::cout << "\n";
        }
        std::cout << "  executables: " << manifest.artifacts.executables.size() << "\n";
        for (const auto &executable : manifest.artifacts.executables)
        {
            std::cout << "    - " << executable.name;
            if (!executable.target.empty())
            {
                std::cout << " target=" << executable.target;
            }
            if (!executable.origin.empty())
            {
                std::cout << " origin=" << executable.origin;
            }
            if (!executable.exported)
            {
                std::cout << " internal";
            }
            std::cout << "\n";
        }
        std::cout << "  operating systems:";
        if (manifest.compatibility.operatingSystems.empty())
        {
            std::cout << " (none)";
        }
        for (const auto &operatingSystem : manifest.compatibility.operatingSystems)
        {
            std::cout << " " << operatingSystem;
        }
        std::cout << "\n";
        std::cout << "  architectures:";
        if (manifest.compatibility.architectures.empty())
        {
            std::cout << " (none)";
        }
        for (const auto &architecture : manifest.compatibility.architectures)
        {
            std::cout << " " << architecture;
        }
        std::cout << "\n";
        std::cout << "  dependencies: " << manifest.dependencies.size() << "\n";
        for (const auto &dependency : manifest.dependencies)
        {
            std::cout << "    - " << dependency.name;
            if (!dependency.versionRange.empty())
            {
                std::cout << " " << dependency.versionRange;
            }
            if (dependency.optional)
            {
                std::cout << " optional";
            }
            std::cout << "\n";
        }
        std::cout << "  inputs: " << manifest.inputs.size() << "\n";
        for (const auto &input : manifest.inputs)
        {
            std::cout << "    - " << (input.path.empty() ? input.pattern : input.path) << " [" << input.kind << "]";
            if (!input.role.empty())
            {
                std::cout << ":" << input.role;
            }
            if (!input.target.empty())
            {
                std::cout << " -> " << input.target;
            }
            else if (!input.targetRoot.empty())
            {
                std::cout << " -> " << input.targetRoot << "/";
            }
            std::cout << "\n";
        }
        std::cout << "  modules: " << manifest.modules.size() << "\n";
        for (const auto &module : manifest.modules)
        {
            std::cout << "    - " << module.name << " [" << module.type << "]";
            if (!module.required.empty())
            {
                std::cout << " requires:";
                for (const auto &dep : module.required)
                {
                    std::cout << " " << dep;
                }
            }
            if (!module.optional.empty())
            {
                std::cout << " optional:";
                for (const auto &dep : module.optional)
                {
                    std::cout << " " << dep;
                }
            }
            std::cout << "\n";
        }
        std::cout << "  plugins: " << manifest.plugins.size() << "\n";
        for (const auto &plugin : manifest.plugins)
        {
            std::cout << "    - " << plugin.name;
            if (plugin.optional)
            {
                std::cout << " optional";
            }
            if (!plugin.requiredModules.empty())
            {
                std::cout << " requires:";
                for (const auto &dep : plugin.requiredModules)
                {
                    std::cout << " " << dep;
                }
            }
            if (!plugin.optionalModules.empty())
            {
                std::cout << " optional-modules:";
                for (const auto &dep : plugin.optionalModules)
                {
                    std::cout << " " << dep;
                }
            }
            std::cout << "\n";
        }
        return 0;
    }

    auto CmdSettingsInit(const fs::path &root, const ParsedArgs &args) -> int
    {
        const auto projectPath = ResolveProjectPath(args.projectPath);
        (void)root;
        const auto projectRoot = RootDirFrom(projectPath.parent_path()).value_or(projectPath.parent_path());
        const auto settingsPath = projectRoot / ".ngin/local/user.nginsettings";
        bool createdSettings = false;
        if (!fs::exists(settingsPath))
        {
            fs::create_directories(settingsPath.parent_path());
            std::ofstream out(settingsPath);
            out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                << "<LocalSettings SchemaVersion=\"1\">\n"
                << "  <Settings>\n"
                << "  </Settings>\n"
                << "</LocalSettings>\n";
            createdSettings = true;
        }

        const auto gitignorePath = projectRoot / ".gitignore";
        bool updatedGitignore = false;
        const auto gitignoreText = ReadTextIfExists(gitignorePath);
        if (!ContainsGitignoreEntry(gitignoreText, ".ngin/local/") && !ContainsGitignoreEntry(gitignoreText, ".ngin/*"))
        {
            std::ofstream out(gitignorePath, std::ios::app);
            if (!gitignoreText.empty() && gitignoreText.back() != '\n')
            {
                out << "\n";
            }
            out << ".ngin/local/\n";
            updatedGitignore = true;
        }

        std::cout << "Initialized local settings\n";
        std::cout << "  settings: " << settingsPath << (createdSettings ? " [created]" : " [exists]") << "\n";
        std::cout << "  gitignore: " << gitignorePath << (updatedGitignore ? " [updated]" : " [ok]") << "\n";
        std::cout << "  import from project manifests with a path relative to the project file\n";
        return 0;
    }

    auto CmdVariablesExplain(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        const auto invocation = ResolveInvocation(args);
        const auto resolved = ResolveLaunch(invocation.project, invocation.profile);
        if (!resolved.value.has_value() || resolved.diagnostics.HasErrors())
        {
            PrintDiagnostics(resolved.diagnostics, "Variables", std::cout);
            return 1;
        }

        std::cout << "Variables for profile: " << resolved.value->profile.name << "\n";
        if (resolved.value->environmentVariables.empty())
        {
            std::cout << "  (none)\n";
        }
        for (const auto &variable : resolved.value->environmentVariables)
        {
            std::cout << "  " << variable.name << " = ";
            if (!variable.resolved)
            {
                std::cout << "<missing>";
            }
            else if (variable.secret)
            {
                std::cout << "<secret>";
            }
            else
            {
                std::cout << variable.value;
            }
            std::cout << "    source: " << variable.resolvedSource << "\n";
        }
        PrintDiagnostics(resolved.diagnostics, "Variables", std::cout);
        return 0;
    }

    auto CmdValidate(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        const auto invocation = ResolveInvocation(args);
        const auto resolved = ResolveLaunch(invocation.project, invocation.profile);
        if (!resolved.value.has_value() || resolved.diagnostics.HasErrors())
        {
            PrintDiagnostics(resolved.diagnostics, "Validation", std::cout);
            return 1;
        }
        std::cout << "Validated profile: " << resolved.value->profile.name << "\n";
        std::cout << "  project: " << resolved.value->project.name << "\n";
        std::cout << "  packages: " << resolved.value->orderedPackages.size() << "\n";
        std::cout << "  required modules: " << resolved.value->requiredModules.size() << "\n";
        std::cout << "  optional modules: " << resolved.value->optionalModules.size() << "\n";
        std::cout << "  libraries: " << resolved.value->libraries.size() << "\n";
        std::cout << "  executables: " << resolved.value->executables.size() << "\n";
        std::cout << "  selected executable: " << (resolved.value->selectedExecutable.has_value() ? resolved.value->selectedExecutable->name : "(none)") << "\n";
        PrintDiagnostics(resolved.diagnostics, "Validation", std::cout);
        return 0;
    }

    auto CmdGraph(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        const auto invocation = ResolveInvocation(args);
        const auto resolved = ResolveLaunch(invocation.project, invocation.profile);
        if (!resolved.value.has_value() || resolved.diagnostics.HasErrors())
        {
            PrintDiagnostics(resolved.diagnostics, "Graph", std::cout);
            return 1;
        }
        std::cout << "Graph for profile: " << resolved.value->profile.name << "\n\nProjects:\n";
        for (const auto &unit : resolved.value->projectUnits)
        {
            std::cout << "  - " << unit.project.name << " [" << unit.profile.name << "]\n";
        }
        std::cout << "\nPackages:\n";
        for (const auto &package : resolved.value->orderedPackages)
        {
            const auto &edges = resolved.value->packageEdges.at(package.manifest.name);
            std::cout << "  - " << package.manifest.name << " -> ";
            if (edges.empty())
            {
                std::cout << "(none)";
            }
            else
            {
                bool first = true;
                for (const auto &dep : edges)
                {
                    if (!first)
                    {
                        std::cout << ", ";
                    }
                    std::cout << dep;
                    first = false;
                }
            }
            std::cout << "\n";
        }
        std::cout << "\nModules:\n";
        for (const auto &[name, edges] : resolved.value->dependencyEdges)
        {
            std::cout << "  - " << name << " -> ";
            if (edges.empty())
            {
                std::cout << "(none)";
            }
            else
            {
                bool first = true;
                for (const auto &dep : edges)
                {
                    if (!first)
                    {
                        std::cout << ", ";
                    }
                    std::cout << dep;
                    first = false;
                }
            }
            std::cout << "\n";
        }
        std::cout << "\nArtifacts:\n";
        for (const auto &library : resolved.value->libraries)
        {
            std::cout << "  - library " << library.name;
            if (!library.target.empty())
            {
                std::cout << " target=" << library.target;
            }
            if (!library.linkage.empty())
            {
                std::cout << " linkage=" << library.linkage;
            }
            if (!library.origin.empty())
            {
                std::cout << " origin=" << library.origin;
            }
            std::cout << "\n";
        }
        for (const auto &executable : resolved.value->executables)
        {
            std::cout << "  - executable " << executable.name;
            if (!executable.target.empty())
            {
                std::cout << " target=" << executable.target;
            }
            if (!executable.origin.empty())
            {
                std::cout << " origin=" << executable.origin;
            }
            if (resolved.value->selectedExecutable.has_value() && resolved.value->selectedExecutable->name == executable.name)
            {
                std::cout << " selected";
            }
            std::cout << "\n";
        }
        bool printedConditionsHeader = false;
        for (const auto &unit : resolved.value->projectUnits)
        {
            if (unit.project.conditions.empty())
            {
                continue;
            }
            if (!printedConditionsHeader)
            {
                std::cout << "\nConditions:\n";
                printedConditionsHeader = true;
            }
            std::cout << "  - " << unit.project.name << ":\n";
            for (const auto &condition : unit.project.conditions)
            {
                SelectorSet selector{};
                selector.conditionRefs.push_back(condition.name);
                std::cout << "    " << condition.name << ": "
                          << (SelectionMatches(unit.project, selector, unit.profile) ? "matched" : "not matched")
                          << "\n";
            }
        }

        bool printedBuildSettingsHeader = false;
        for (const auto &unit : resolved.value->projectUnits)
        {
            const auto hasConditionalBuildSettings =
                std::any_of(unit.project.build.includeDirectories.begin(), unit.project.build.includeDirectories.end(), [](const BuildSetting &setting)
                            { return HasSelection(setting.selectors); })
                || std::any_of(unit.project.build.compileDefinitions.begin(), unit.project.build.compileDefinitions.end(), [](const BuildSetting &setting)
                               { return HasSelection(setting.selectors); })
                || std::any_of(unit.project.build.compileOptions.begin(), unit.project.build.compileOptions.end(), [](const BuildSetting &setting)
                               { return HasSelection(setting.selectors); })
                || std::any_of(unit.project.build.linkOptions.begin(), unit.project.build.linkOptions.end(), [](const BuildSetting &setting)
                               { return HasSelection(setting.selectors); });
            if (!hasConditionalBuildSettings)
            {
                continue;
            }
            if (!printedBuildSettingsHeader)
            {
                std::cout << "\nConditional build settings:\n";
                printedBuildSettingsHeader = true;
            }
            std::cout << "  - " << unit.project.name << ":\n";
            PrintConditionalBuildSettings("IncludeDirectories", unit.project, unit.profile, unit.project.build.includeDirectories);
            PrintConditionalBuildSettings("CompileDefinitions", unit.project, unit.profile, unit.project.build.compileDefinitions);
            PrintConditionalBuildSettings("CompileOptions", unit.project, unit.profile, unit.project.build.compileOptions);
            PrintConditionalBuildSettings("LinkOptions", unit.project, unit.profile, unit.project.build.linkOptions);
        }
        PrintDiagnostics(resolved.diagnostics, "Graph", std::cout);
        return 0;
    }

    auto CmdMetaGen(const fs::path &root, const ParsedArgs &args) -> int
    {
        const auto invocation = ResolveInvocation(args);
        return RunMetaGen(root, invocation.project, invocation.profile, args.outputPath);
    }

    auto CmdClean(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        const auto invocation = ResolveInvocation(args);
        const auto cleaned = CleanLaunch(
            invocation.project,
            invocation.profile,
            args.outputPath.has_value() ? std::optional<fs::path>{*args.outputPath} : std::nullopt);
        if (!cleaned.value.has_value() || cleaned.diagnostics.HasErrors())
        {
            PrintDiagnostics(cleaned.diagnostics, "Clean", std::cout);
            return 1;
        }

        std::cout << "Cleaned profile: " << invocation.profile.name << "\n";
        std::cout << "  project: " << invocation.project.name << "\n";
        std::cout << "  output: " << *cleaned.value << "\n";
        PrintDiagnostics(cleaned.diagnostics, "Clean", std::cout);
        return 0;
    }

    auto CmdConfigure(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        const auto invocation = ResolveInvocation(args);
        const auto configured = ConfigureLaunch(
            invocation.project,
            invocation.profile,
            args.outputPath.has_value() ? std::optional<fs::path>{*args.outputPath} : std::nullopt);
        if (!configured.value.has_value() || configured.diagnostics.HasErrors())
        {
            PrintDiagnostics(configured.diagnostics, "Configure", std::cout);
            return 1;
        }

        std::cout << "Configured build metadata for profile: " << invocation.profile.name << "\n";
        std::cout << "  project: " << invocation.project.name << "\n";
        std::cout << "  output: " << configured.value->outputDir << "\n";
        if (configured.value->configured)
        {
            std::cout << "  build directory: " << *configured.value->buildDir << "\n";
            std::cout << "  compile commands: " << *configured.value->compileCommandsPath << "\n";
        }
        else
        {
            std::cout << "  generated native build: (none)\n";
        }
        PrintDiagnostics(configured.diagnostics, "Configure", std::cout);
        return 0;
    }

    auto CmdBuild(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        const auto invocation = ResolveInvocation(args);
        auto built = BuildLaunch(
            invocation.project,
            invocation.profile,
            args.outputPath.has_value() ? std::optional<fs::path>{*args.outputPath} : std::nullopt);
        if (!built.value.has_value() || built.diagnostics.HasErrors())
        {
            PrintDiagnostics(built.diagnostics, "Build", std::cout);
            return 1;
        }

        const auto summary = LoadLaunchManifestSummary(built.value->manifestPath);
        std::cout << "Built profile: " << invocation.profile.name << "\n";
        std::cout << "  project: " << invocation.project.name << "\n";
        std::cout << "  output: " << built.value->outputDir << "\n";
        std::cout << "  launch manifest: " << built.value->manifestPath << "\n";
        std::cout << "  selected executable: " << (summary.selectedExecutable.has_value() && !summary.selectedExecutable->empty() ? *summary.selectedExecutable : "(none)") << "\n";
        PrintDiagnostics(built.diagnostics, "Build", std::cout);
        return 0;
    }

    auto CmdRebuild(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        const auto invocation = ResolveInvocation(args);
        const auto cleanResult = CleanLaunch(
            invocation.project,
            invocation.profile,
            args.outputPath.has_value() ? std::optional<fs::path>{*args.outputPath} : std::nullopt);
        if (!cleanResult.value.has_value() || cleanResult.diagnostics.HasErrors())
        {
            PrintDiagnostics(cleanResult.diagnostics, "Rebuild", std::cout);
            return 1;
        }

        auto built = BuildLaunch(
            invocation.project,
            invocation.profile,
            args.outputPath.has_value() ? std::optional<fs::path>{*args.outputPath} : std::nullopt);
        AppendDiagnostics(built.diagnostics, cleanResult.diagnostics);
        if (!built.value.has_value() || built.diagnostics.HasErrors())
        {
            PrintDiagnostics(built.diagnostics, "Rebuild", std::cout);
            return 1;
        }

        const auto summary = LoadLaunchManifestSummary(built.value->manifestPath);
        std::cout << "Rebuilt profile: " << invocation.profile.name << "\n";
        std::cout << "  project: " << invocation.project.name << "\n";
        std::cout << "  output: " << built.value->outputDir << "\n";
        std::cout << "  launch manifest: " << built.value->manifestPath << "\n";
        std::cout << "  selected executable: " << (summary.selectedExecutable.has_value() && !summary.selectedExecutable->empty() ? *summary.selectedExecutable : "(none)") << "\n";
        PrintDiagnostics(built.diagnostics, "Rebuild", std::cout);
        return 0;
    }

    auto CmdRun(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        const auto invocation = ResolveInvocation(args);
        const auto built = BuildLaunch(
            invocation.project,
            invocation.profile,
            args.outputPath.has_value() ? std::optional<fs::path>{*args.outputPath} : std::nullopt);
        if (!built.value.has_value() || built.diagnostics.HasErrors())
        {
            PrintDiagnostics(built.diagnostics, "Run", std::cout);
            return 1;
        }

        const auto summary = LoadLaunchManifestSummary(built.value->manifestPath);
        if (!summary.selectedExecutable.has_value() || summary.selectedExecutable->empty())
        {
            throw std::runtime_error("launch manifest does not declare a selected executable");
        }

        const auto executableName = *summary.selectedExecutable + (fs::exists(built.value->outputDir / "bin" / (*summary.selectedExecutable + ".exe")) ? ".exe" : "");
        const auto executablePath = built.value->outputDir / "bin" / executableName;
        if (!fs::exists(executablePath))
        {
            throw std::runtime_error("selected executable was not staged to '" + executablePath.string() + "'");
        }

        fs::path workingDirectory = summary.workingDirectory == "."
                                        ? built.value->outputDir
                                        : fs::absolute(built.value->outputDir / summary.workingDirectory);
        if (!fs::exists(workingDirectory))
        {
            workingDirectory = built.value->outputDir;
        }

        return RunProcess(executablePath, args.runArgs, workingDirectory);
    }
}
