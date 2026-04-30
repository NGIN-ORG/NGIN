#include "Commands.hpp"

#include "Authoring.hpp"
#include "Build.hpp"
#include "Diagnostics.hpp"
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

        [[nodiscard]] auto EscapeXml(const std::string &value) -> std::string
        {
            std::string escaped{};
            for (const char ch : value)
            {
                switch (ch)
                {
                case '&': escaped += "&amp;"; break;
                case '<': escaped += "&lt;"; break;
                case '>': escaped += "&gt;"; break;
                case '"': escaped += "&quot;"; break;
                case '\'': escaped += "&apos;"; break;
                default: escaped += ch; break;
                }
            }
            return escaped;
        }

        [[nodiscard]] auto DefaultLockPath(const ResolvedLaunch &resolved) -> fs::path
        {
            if (resolved.workspace.has_value())
            {
                return resolved.workspace->path.parent_path() / "ngin.lock";
            }
            return resolved.project.path.parent_path() / "ngin.lock";
        }

        [[nodiscard]] auto GenerateLockFile(const ResolvedLaunch &resolved) -> std::string
        {
            std::ostringstream out{};
            out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
            out << "<LockFile SchemaVersion=\"1\" Project=\"" << EscapeXml(resolved.project.name)
                << "\" Profile=\"" << EscapeXml(resolved.profile.name)
                << "\" BuildType=\"" << EscapeXml(resolved.profile.buildType)
                << "\" Platform=\"" << EscapeXml(resolved.profile.platform)
                << "\" Environment=\"" << EscapeXml(resolved.profile.environmentName) << "\">\n";
            out << "  <Packages>\n";
            for (const auto &package : resolved.orderedPackages)
            {
                out << "    <Package Name=\"" << EscapeXml(package.manifest.name)
                    << "\" Version=\"" << EscapeXml(package.manifest.version)
                    << "\" Manifest=\"" << EscapeXml(package.manifest.path.string())
                    << "\" Source=\"" << EscapeXml(package.source) << "\"";
                if (!package.sourceDirectory.empty())
                {
                    out << " ProviderRoot=\"" << EscapeXml(package.sourceDirectory.string()) << "\"";
                }
                out << " />\n";
            }
            out << "  </Packages>\n";
            out << "  <Features>\n";
            for (const auto &feature : resolved.selectedPackageFeatures)
            {
                out << "    <Feature Package=\"" << EscapeXml(feature.packageName)
                    << "\" Name=\"" << EscapeXml(feature.featureName)
                    << "\" Version=\"" << EscapeXml(feature.packageVersion)
                    << "\" Manifest=\"" << EscapeXml(feature.manifestPath.string()) << "\" />\n";
            }
            out << "  </Features>\n";
            out << "  <Capabilities>\n";
            for (const auto &provider : resolved.capabilityProviders)
            {
                out << "    <Capability Name=\"" << EscapeXml(provider.capability)
                    << "\" Package=\"" << EscapeXml(provider.packageName)
                    << "\" Feature=\"" << EscapeXml(provider.featureName)
                    << "\" Exclusive=\"" << (provider.exclusive ? "true" : "false") << "\" />\n";
            }
            out << "  </Capabilities>\n";
            out << "  <Dependencies>\n";
            for (const auto &[packageName, deps] : resolved.packageEdges)
            {
                out << "    <Package Name=\"" << EscapeXml(packageName) << "\">\n";
                for (const auto &dep : deps)
                {
                    out << "      <PackageRef Name=\"" << EscapeXml(dep) << "\" />\n";
                }
                out << "    </Package>\n";
            }
            out << "  </Dependencies>\n";
            out << "</LockFile>\n";
            return out.str();
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

        auto PrintConditionalProjectReferences(
            const std::string_view label,
            const ProjectManifest &project,
            const ProfileDefinition &profile,
            const std::vector<ProjectReference> &references) -> void
        {
            bool printedHeader = false;
            for (const auto &reference : references)
            {
                if (!HasSelection(reference.selectors))
                {
                    continue;
                }
                if (!printedHeader)
                {
                    std::cout << "    " << label << ":\n";
                    printedHeader = true;
                }
                std::cout << "      - " << reference.path.string() << " "
                          << (SelectionMatches(project, reference.selectors, profile) ? "included" : "excluded")
                          << " (";
                PrintSelectorSummary(reference.selectors);
                std::cout << ")\n";
            }
        }

        auto PrintConditionalPackageReferences(
            const std::string_view label,
            const ProjectManifest &project,
            const ProfileDefinition &profile,
            const std::vector<PackageReference> &references) -> void
        {
            bool printedHeader = false;
            for (const auto &reference : references)
            {
                if (!HasSelection(reference.selectors))
                {
                    continue;
                }
                if (!printedHeader)
                {
                    std::cout << "    " << label << ":\n";
                    printedHeader = true;
                }
                std::cout << "      - " << reference.name << " "
                          << (SelectionMatches(project, reference.selectors, profile) ? "included" : "excluded")
                          << " (";
                PrintSelectorSummary(reference.selectors);
                std::cout << ")\n";
            }
        }

        auto PrintConditionalRuntimeRefs(
            const std::string_view label,
            const ProjectManifest &project,
            const ProfileDefinition &profile,
            const std::vector<RuntimeReference> &references) -> void
        {
            bool printedHeader = false;
            for (const auto &reference : references)
            {
                if (!HasSelection(reference.selectors))
                {
                    continue;
                }
                if (!printedHeader)
                {
                    std::cout << "    " << label << ":\n";
                    printedHeader = true;
                }
                std::cout << "      - " << reference.name << " "
                          << (SelectionMatches(project, reference.selectors, profile) ? "included" : "excluded")
                          << " (";
                PrintSelectorSummary(reference.selectors);
                std::cout << ")\n";
            }
        }

        auto PrintConditionalFeatures(
            const ProjectManifest &project,
            const ProfileDefinition &profile,
            const std::optional<EnvironmentDefinition> &environment) -> void
        {
            if (!environment.has_value())
            {
                return;
            }
            bool printedHeader = false;
            for (const auto &feature : environment->features)
            {
                if (!HasSelection(feature.selectors))
                {
                    continue;
                }
                if (!printedHeader)
                {
                    std::cout << "    Features:\n";
                    printedHeader = true;
                }
                std::cout << "      - " << feature.name << " "
                          << (SelectionMatches(project, feature.selectors, profile) ? "included" : "excluded")
                          << " (";
                PrintSelectorSummary(feature.selectors);
                std::cout << ")\n";
            }
        }

        [[nodiscard]] auto DirectSelectorMatches(const SelectorSet &selectors, const ProfileDefinition &profile) -> bool
        {
            if (selectors.impossible)
            {
                return false;
            }
            if (selectors.profile.has_value() && *selectors.profile != profile.name)
            {
                return false;
            }
            if (selectors.platform.has_value() && *selectors.platform != profile.platform)
            {
                return false;
            }
            if (selectors.operatingSystem.has_value() && *selectors.operatingSystem != profile.operatingSystem)
            {
                return false;
            }
            if (selectors.architecture.has_value() && *selectors.architecture != profile.architecture)
            {
                return false;
            }
            if (selectors.buildType.has_value() && *selectors.buildType != profile.buildType)
            {
                return false;
            }
            if (selectors.environment.has_value() && *selectors.environment != profile.environmentName)
            {
                return false;
            }
            return true;
        }

        [[nodiscard]] auto ConditionMap(const std::vector<ConditionDefinition> &conditions)
            -> std::unordered_map<std::string, const ConditionDefinition *>
        {
            std::unordered_map<std::string, const ConditionDefinition *> result{};
            for (const auto &condition : conditions)
            {
                result.emplace(condition.name, &condition);
            }
            return result;
        }

        [[nodiscard]] auto ConditionOrigin(const ConditionDefinition &condition) -> std::string
        {
            if (condition.builtin)
            {
                return "built-in";
            }
            std::ostringstream text{};
            text << (condition.sourceKind.empty() ? "manifest" : condition.sourceKind);
            if (!condition.sourceName.empty())
            {
                text << " '" << condition.sourceName << "'";
            }
            if (!condition.manifestPath.empty())
            {
                text << " (" << condition.manifestPath.string() << ")";
            }
            return text.str();
        }

        [[nodiscard]] auto EvalConditionNode(
            const ConditionNode &node,
            const std::unordered_map<std::string, const ConditionDefinition *> &conditions,
            const ProfileDefinition &profile) -> bool
        {
            switch (node.kind)
            {
            case ConditionNode::Kind::Match:
                return DirectSelectorMatches(node.match, profile);
            case ConditionNode::Kind::ConditionRef:
            {
                const auto it = conditions.find(node.conditionName);
                return it != conditions.end() && EvalConditionNode(it->second->body, conditions, profile);
            }
            case ConditionNode::Kind::All:
                return std::all_of(
                    node.children.begin(),
                    node.children.end(),
                    [&](const ConditionNode &child)
                    { return EvalConditionNode(child, conditions, profile); });
            case ConditionNode::Kind::Any:
                return std::any_of(
                    node.children.begin(),
                    node.children.end(),
                    [&](const ConditionNode &child)
                    { return EvalConditionNode(child, conditions, profile); });
            case ConditionNode::Kind::Not:
                return node.children.size() == 1 && !EvalConditionNode(node.children.front(), conditions, profile);
            }
            return false;
        }

        auto PrintConditionNode(
            const ConditionNode &node,
            const std::unordered_map<std::string, const ConditionDefinition *> &conditions,
            const ProfileDefinition &profile,
            const int indent) -> bool
        {
            const auto pad = std::string(static_cast<std::size_t>(indent), ' ');
            switch (node.kind)
            {
            case ConditionNode::Kind::Match:
            {
                const auto matched = DirectSelectorMatches(node.match, profile);
                std::cout << pad << "- Match: " << (matched ? "matched" : "not matched");
                if (HasSelection(node.match))
                {
                    std::cout << " (";
                    PrintSelectorSummary(node.match);
                    std::cout << ")";
                }
                std::cout << "\n";
                return matched;
            }
            case ConditionNode::Kind::ConditionRef:
            {
                const auto it = conditions.find(node.conditionName);
                if (it == conditions.end())
                {
                    std::cout << pad << "- ConditionRef " << node.conditionName << ": unknown\n";
                    return false;
                }
                const auto matched = EvalConditionNode(it->second->body, conditions, profile);
                std::cout << pad << "- ConditionRef " << node.conditionName << ": "
                          << (matched ? "matched" : "not matched") << "\n";
                PrintConditionNode(it->second->body, conditions, profile, indent + 2);
                return matched;
            }
            case ConditionNode::Kind::All:
            {
                std::cout << pad << "- All\n";
                bool matched = true;
                for (const auto &child : node.children)
                {
                    matched = PrintConditionNode(child, conditions, profile, indent + 2) && matched;
                }
                std::cout << pad << "  result: " << (matched ? "matched" : "not matched") << "\n";
                return matched;
            }
            case ConditionNode::Kind::Any:
            {
                std::cout << pad << "- Any\n";
                bool matched = false;
                for (const auto &child : node.children)
                {
                    matched = PrintConditionNode(child, conditions, profile, indent + 2) || matched;
                }
                std::cout << pad << "  result: " << (matched ? "matched" : "not matched") << "\n";
                return matched;
            }
            case ConditionNode::Kind::Not:
            {
                std::cout << pad << "- Not\n";
                const auto childMatched = !node.children.empty() && PrintConditionNode(node.children.front(), conditions, profile, indent + 2);
                const auto matched = !childMatched;
                std::cout << pad << "  result: " << (matched ? "matched" : "not matched") << "\n";
                return matched;
            }
            }
            return false;
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
            else if (current == "--lock" && index + 1 < argc)
            {
                args.lockPath = argv[++index];
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
            else if (!args.featureName.has_value())
            {
                args.featureName = current;
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
        std::cout << "  package policy: DefaultFeatures=" << manifest.defaultFeatures
                  << " LockFile=" << manifest.lockFile << "\n";
        std::cout << "  features: " << manifest.features.size() << "\n";
        for (const auto &feature : manifest.features)
        {
            std::cout << "    - " << feature.name;
            if (!feature.description.empty())
            {
                std::cout << " \"" << feature.description << "\"";
            }
            if (HasSelection(feature.selectors))
            {
                std::cout << " selectors=(";
                PrintSelectorSummary(feature.selectors);
                std::cout << ")";
            }
            std::cout << "\n";
            for (const auto &capability : feature.provides)
            {
                std::cout << "      provides: " << capability.name;
                if (capability.exclusive)
                {
                    std::cout << " exclusive";
                }
                std::cout << "\n";
            }
            for (const auto &capability : feature.requiredCapabilities)
            {
                std::cout << "      requires: " << capability.name << "\n";
            }
            for (const auto &dependency : feature.packageRefs)
            {
                std::cout << "      dependency: " << dependency.name;
                if (!dependency.versionRange.empty())
                {
                    std::cout << " " << dependency.versionRange;
                }
                std::cout << "\n";
            }
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

    auto CmdPackageLock(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        const auto invocation = ResolveInvocation(args);
        const auto resolved = ResolveLaunch(invocation.project, invocation.profile);
        if (!resolved.value.has_value() || resolved.diagnostics.HasErrors())
        {
            PrintDiagnostics(resolved.diagnostics, "Package lock", std::cout);
            return 1;
        }
        const auto lockPath = args.outputPath.has_value() ? fs::path(*args.outputPath) : DefaultLockPath(*resolved.value);
        if (!lockPath.parent_path().empty())
        {
            fs::create_directories(lockPath.parent_path());
        }
        std::ofstream out(lockPath);
        out << GenerateLockFile(*resolved.value);
        std::cout << "Generated package lock\n";
        std::cout << "  path: " << lockPath << "\n";
        std::cout << "  packages: " << resolved.value->orderedPackages.size() << "\n";
        std::cout << "  features: " << resolved.value->selectedPackageFeatures.size() << "\n";
        return 0;
    }

    auto CmdPackageVerifyLock(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        const auto invocation = ResolveInvocation(args);
        const auto resolved = ResolveLaunch(invocation.project, invocation.profile);
        if (!resolved.value.has_value() || resolved.diagnostics.HasErrors())
        {
            PrintDiagnostics(resolved.diagnostics, "Package lock", std::cout);
            return 1;
        }
        const auto lockPath = args.lockPath.has_value() ? fs::path(*args.lockPath) : DefaultLockPath(*resolved.value);
        const auto existing = ReadTextIfExists(lockPath);
        if (existing.empty())
        {
            std::cout << "Package lock verification failed\n";
            std::cout << "  missing: " << lockPath << "\n";
            return 1;
        }
        const auto expected = GenerateLockFile(*resolved.value);
        if (existing != expected)
        {
            std::cout << "Package lock verification failed\n";
            std::cout << "  path: " << lockPath << "\n";
            std::cout << "  reason: resolved package graph differs from lock file\n";
            return 1;
        }
        std::cout << "Package lock verified\n";
        std::cout << "  path: " << lockPath << "\n";
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

    auto CmdExplainCondition(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        if (!args.packageName.has_value() || args.packageName->empty())
        {
            throw std::runtime_error("explain condition requires a condition name");
        }
        const auto invocation = ResolveInvocation(args);
        const auto conditions = ConditionMap(invocation.project.conditions);
        const auto it = conditions.find(*args.packageName);
        if (it == conditions.end())
        {
            std::vector<std::string> names{};
            for (const auto &[name, _] : conditions)
            {
                names.push_back(name);
            }
            std::sort(names.begin(), names.end());
            std::ostringstream available{};
            for (std::size_t index = 0; index < names.size(); ++index)
            {
                if (index != 0)
                {
                    available << ", ";
                }
                available << names[index];
            }
            throw std::runtime_error("unknown condition '" + *args.packageName + "' (available: " + available.str() + ")");
        }

        const auto &condition = *it->second;
        const auto matched = EvalConditionNode(condition.body, conditions, invocation.profile);
        std::cout << "Condition: " << condition.name << "\n";
        std::cout << "  result: " << (matched ? "matched" : "not matched") << "\n";
        std::cout << "  origin: " << ConditionOrigin(condition) << "\n";
        std::cout << "  profile: " << invocation.profile.name
                  << " BuildType=" << invocation.profile.buildType
                  << " Platform=" << invocation.profile.platform
                  << " OperatingSystem=" << invocation.profile.operatingSystem
                  << " Architecture=" << invocation.profile.architecture
                  << " Environment=" << invocation.profile.environmentName << "\n";
        std::cout << "  tree:\n";
        PrintConditionNode(condition.body, conditions, invocation.profile, 4);
        return 0;
    }

    auto CmdExplainPackageFeature(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        if (!args.packageName.has_value() || !args.featureName.has_value())
        {
            throw std::runtime_error("explain package-feature requires a package name and feature name");
        }
        const auto invocation = ResolveInvocation(args);
        const auto resolved = ResolveLaunch(invocation.project, invocation.profile);
        if (!resolved.value.has_value() || resolved.diagnostics.HasErrors())
        {
            PrintDiagnostics(resolved.diagnostics, "Package feature", std::cout);
            return 1;
        }

        const auto packageIt = std::find_if(
            resolved.value->orderedPackages.begin(), resolved.value->orderedPackages.end(),
            [&](const ResolvedPackage &package)
            {
                return package.manifest.name == *args.packageName;
            });
        if (packageIt == resolved.value->orderedPackages.end())
        {
            throw std::runtime_error("package '" + *args.packageName + "' is not selected");
        }
        const auto featureIt = std::find_if(
            packageIt->manifest.features.begin(), packageIt->manifest.features.end(),
            [&](const PackageManifest::Feature &feature)
            {
                return feature.name == *args.featureName;
            });
        if (featureIt == packageIt->manifest.features.end())
        {
            throw std::runtime_error("package '" + *args.packageName + "' does not declare feature '" + *args.featureName + "'");
        }
        const auto selectedIt = std::find_if(
            resolved.value->selectedPackageFeatures.begin(), resolved.value->selectedPackageFeatures.end(),
            [&](const SelectedPackageFeature &feature)
            {
                return feature.packageName == *args.packageName && feature.featureName == *args.featureName;
            });
        const auto matchedSelectors = SelectionMatches(packageIt->manifest.conditions, featureIt->selectors, invocation.profile);
        std::cout << "Package feature: " << *args.packageName << "::" << *args.featureName << "\n";
        std::cout << "  result: " << (selectedIt != resolved.value->selectedPackageFeatures.end() ? "selected" : "not selected") << "\n";
        std::cout << "  selector result: " << (matchedSelectors ? "matched" : "not matched") << "\n";
        std::cout << "  manifest: " << packageIt->manifest.path << "\n";
        std::cout << "  dependencies: " << featureIt->packageRefs.size() << "\n";
        for (const auto &dependency : featureIt->packageRefs)
        {
            std::cout << "    - " << dependency.name;
            if (!dependency.versionRange.empty())
            {
                std::cout << " " << dependency.versionRange;
            }
            std::cout << "\n";
        }
        std::cout << "  provides: " << featureIt->provides.size() << "\n";
        for (const auto &capability : featureIt->provides)
        {
            std::cout << "    - " << capability.name << (capability.exclusive ? " exclusive" : "") << "\n";
        }
        std::cout << "  requires: " << featureIt->requiredCapabilities.size() << "\n";
        for (const auto &capability : featureIt->requiredCapabilities)
        {
            std::cout << "    - " << capability.name << "\n";
        }
        std::cout << "  inputs: " << featureIt->inputs.size() << "\n";
        std::cout << "  variables: " << featureIt->variables.size() << "\n";
        std::cout << "  runtime modules: " << featureIt->runtime.modules.size() << "\n";
        return 0;
    }

    auto CmdExplainGenerator(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        if (!args.packageName.has_value())
        {
            throw std::runtime_error("explain generator requires a generator name");
        }
        const auto invocation = ResolveInvocation(args);
        const auto resolved = ResolveLaunch(invocation.project, invocation.profile);
        if (!resolved.value.has_value() || resolved.diagnostics.HasErrors())
        {
            PrintDiagnostics(resolved.diagnostics, "Generator", std::cout);
            return 1;
        }
        const auto generatorIt = std::find_if(
            resolved.value->generators.begin(), resolved.value->generators.end(),
            [&](const ResolvedGenerator &generator)
            {
                return generator.declaration.name == *args.packageName;
            });
        if (generatorIt == resolved.value->generators.end())
        {
            throw std::runtime_error("generator '" + *args.packageName + "' is not selected");
        }
        const auto &generator = *generatorIt;
        std::cout << "Generator: " << generator.declaration.name << "\n";
        std::cout << "  result: selected\n";
        std::cout << "  kind: " << generator.declaration.kind << "\n";
        std::cout << "  owner: " << generator.ownerKind << " " << generator.ownerName << "\n";
        std::cout << "  manifest: " << generator.manifestPath << "\n";
        if (!generator.declaration.packageName.empty())
        {
            std::cout << "  package: " << generator.declaration.packageName << "\n";
        }
        if (!generator.declaration.toolName.empty())
        {
            std::cout << "  tool: " << generator.declaration.toolName << "\n";
        }
        if (generator.declaration.hasInlineTool)
        {
            std::cout << "  inline tool:";
            if (!generator.declaration.inlineTool.executable.empty())
            {
                std::cout << " executable=" << generator.declaration.inlineTool.executable;
            }
            std::cout << "\n";
        }
        std::cout << "  inputs: " << generator.declaration.inputs.size() << "\n";
        for (const auto &input : generator.declaration.inputs)
        {
            std::cout << "    - " << input.kind << " " << input.path << "\n";
        }
        std::cout << "  outputs: " << generator.declaration.outputs.size() << "\n";
        for (const auto &output : generator.declaration.outputs)
        {
            std::cout << "    - " << output.kind << " Role=" << output.role << " Path=" << output.path << "\n";
        }
        std::cout << "  arguments: " << generator.declaration.arguments.size() << "\n";
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
        std::cout << "\nPackage features:\n";
        if (resolved.value->selectedPackageFeatures.empty())
        {
            std::cout << "  (none)\n";
        }
        for (const auto &feature : resolved.value->selectedPackageFeatures)
        {
            std::cout << "  - " << feature.packageName << "::" << feature.featureName << "\n";
        }
        std::cout << "\nGenerators:\n";
        if (resolved.value->generators.empty())
        {
            std::cout << "  (none)\n";
        }
        for (const auto &generator : resolved.value->generators)
        {
            std::cout << "  - " << generator.declaration.name
                      << " kind=" << generator.declaration.kind
                      << " owner=" << generator.ownerKind << ":" << generator.ownerName;
            if (!generator.declaration.toolName.empty())
            {
                std::cout << " tool=" << generator.declaration.toolName;
            }
            std::cout << "\n";
        }
        std::cout << "\nCapabilities:\n";
        if (resolved.value->capabilityProviders.empty())
        {
            std::cout << "  (none)\n";
        }
        for (const auto &provider : resolved.value->capabilityProviders)
        {
            std::cout << "  - " << provider.capability << " <- "
                      << provider.packageName << "::" << provider.featureName;
            if (provider.exclusive)
            {
                std::cout << " exclusive";
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

        bool printedSelectionHeader = false;
        for (const auto &unit : resolved.value->projectUnits)
        {
            const auto hasConditionalSelections =
                std::any_of(unit.project.projectRefs.begin(), unit.project.projectRefs.end(), [](const ProjectReference &reference)
                            { return HasSelection(reference.selectors); })
                || std::any_of(unit.project.packageRefs.begin(), unit.project.packageRefs.end(), [](const PackageReference &reference)
                               { return HasSelection(reference.selectors); })
                || (unit.environment.has_value()
                    && (std::any_of(unit.environment->projectRefs.begin(), unit.environment->projectRefs.end(), [](const ProjectReference &reference)
                                    { return HasSelection(reference.selectors); })
                        || std::any_of(unit.environment->packageRefs.begin(), unit.environment->packageRefs.end(), [](const PackageReference &reference)
                                       { return HasSelection(reference.selectors); })
                        || std::any_of(unit.environment->features.begin(), unit.environment->features.end(), [](const FeatureFlag &feature)
                                       { return HasSelection(feature.selectors); })))
                || std::any_of(unit.profile.projectRefs.begin(), unit.profile.projectRefs.end(), [](const ProjectReference &reference)
                               { return HasSelection(reference.selectors); })
                || std::any_of(unit.profile.packageRefs.begin(), unit.profile.packageRefs.end(), [](const PackageReference &reference)
                               { return HasSelection(reference.selectors); })
                || std::any_of(unit.project.runtime.enableModules.begin(), unit.project.runtime.enableModules.end(), [](const RuntimeReference &reference)
                               { return HasSelection(reference.selectors); })
                || std::any_of(unit.project.runtime.disableModules.begin(), unit.project.runtime.disableModules.end(), [](const RuntimeReference &reference)
                               { return HasSelection(reference.selectors); })
                || std::any_of(unit.profile.runtime.enableModules.begin(), unit.profile.runtime.enableModules.end(), [](const RuntimeReference &reference)
                               { return HasSelection(reference.selectors); })
                || std::any_of(unit.profile.runtime.disableModules.begin(), unit.profile.runtime.disableModules.end(), [](const RuntimeReference &reference)
                               { return HasSelection(reference.selectors); });
            if (!hasConditionalSelections)
            {
                continue;
            }
            if (!printedSelectionHeader)
            {
                std::cout << "\nConditional selections:\n";
                printedSelectionHeader = true;
            }
            std::cout << "  - " << unit.project.name << ":\n";
            PrintConditionalProjectReferences("Project references", unit.project, unit.profile, unit.project.projectRefs);
            PrintConditionalPackageReferences("Package references", unit.project, unit.profile, unit.project.packageRefs);
            if (unit.environment.has_value())
            {
                PrintConditionalProjectReferences("Environment project references", unit.project, unit.profile, unit.environment->projectRefs);
                PrintConditionalPackageReferences("Environment package references", unit.project, unit.profile, unit.environment->packageRefs);
                PrintConditionalFeatures(unit.project, unit.profile, unit.environment);
                PrintConditionalRuntimeRefs("Environment enabled modules", unit.project, unit.profile, unit.environment->runtime.enableModules);
                PrintConditionalRuntimeRefs("Environment disabled modules", unit.project, unit.profile, unit.environment->runtime.disableModules);
            }
            PrintConditionalProjectReferences("Profile project references", unit.project, unit.profile, unit.profile.projectRefs);
            PrintConditionalPackageReferences("Profile package references", unit.project, unit.profile, unit.profile.packageRefs);
            PrintConditionalRuntimeRefs("Enabled modules", unit.project, unit.profile, unit.project.runtime.enableModules);
            PrintConditionalRuntimeRefs("Disabled modules", unit.project, unit.profile, unit.project.runtime.disableModules);
            PrintConditionalRuntimeRefs("Profile enabled modules", unit.project, unit.profile, unit.profile.runtime.enableModules);
            PrintConditionalRuntimeRefs("Profile disabled modules", unit.project, unit.profile, unit.profile.runtime.disableModules);
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
