// NGIN native CLI: XML manifests, package-first composition, no lockfile.
#include <NGIN/Serialization/Core/ParseError.hpp>
#include <NGIN/Serialization/XML/XmlParser.hpp>
#include <NGIN/Serialization/XML/XmlTypes.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    using NGIN::Serialization::ParseError;
    using NGIN::Serialization::XmlDocument;
    using NGIN::Serialization::XmlElement;
    using NGIN::Serialization::XmlNode;
    using NGIN::Serialization::XmlParseOptions;
    using NGIN::Serialization::XmlParser;

    struct IssueReport
    {
        std::vector<std::string> errors {};
        std::vector<std::string> warnings {};
    };

    struct LoadedXml
    {
        std::string text {};
        XmlDocument document {0};
    };

    auto AddError(IssueReport& report, std::string message) -> void
    {
        report.errors.push_back(std::move(message));
    }

    auto AddWarning(IssueReport& report, std::string message) -> void
    {
        report.warnings.push_back(std::move(message));
    }

    [[nodiscard]] auto ReadText(const fs::path& path) -> std::string
    {
        std::ifstream input(path);
        if (!input)
        {
            throw std::runtime_error("failed to open file: " + path.string());
        }
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }

    [[nodiscard]] auto ToString(const ParseError& error) -> std::string
    {
        return std::string(error.message.Data(), error.message.Size());
    }

    [[nodiscard]] auto LoadXml(const fs::path& path) -> LoadedXml
    {
        LoadedXml loaded {};
        loaded.text = ReadText(path);
        XmlParseOptions options {};
        options.decodeEntities = true;
        options.arenaBytes = std::max<NGIN::UIntSize>(16384, static_cast<NGIN::UIntSize>(loaded.text.size() * 8 + 4096));
        auto parsed = XmlParser::Parse(loaded.text, options);
        if (!parsed.HasValue())
        {
            throw std::runtime_error(path.string() + ": failed to parse XML: " + ToString(parsed.ErrorUnsafe()));
        }
        loaded.document = std::move(parsed.ValueUnsafe());
        return loaded;
    }

    [[nodiscard]] auto ChildElements(const XmlElement& node, std::string_view name = {}) -> std::vector<const XmlElement*>
    {
        std::vector<const XmlElement*> out;
        out.reserve(static_cast<std::size_t>(node.children.Size()));
        for (NGIN::UIntSize index = 0; index < node.children.Size(); ++index)
        {
            const auto& child = node.children[index];
            if (child.type != XmlNode::Type::Element || child.element == nullptr)
            {
                continue;
            }
            if (name.empty() || child.element->name == name)
            {
                out.push_back(child.element);
            }
        }
        return out;
    }

    [[nodiscard]] auto FindChild(const XmlElement& node, std::string_view name) -> const XmlElement*
    {
        for (NGIN::UIntSize index = 0; index < node.children.Size(); ++index)
        {
            const auto& child = node.children[index];
            if (child.type == XmlNode::Type::Element && child.element != nullptr && child.element->name == name)
            {
                return child.element;
            }
        }
        return nullptr;
    }

    [[nodiscard]] auto Attribute(const XmlElement& node, std::string_view key) -> std::optional<std::string>
    {
        const auto* attr = node.FindAttribute(key);
        if (attr == nullptr)
        {
            return std::nullopt;
        }
        return std::string(attr->value);
    }

    [[nodiscard]] auto RequireAttribute(const XmlElement& node, std::string_view key, const fs::path& path) -> std::string
    {
        const auto value = Attribute(node, key);
        if (!value.has_value())
        {
            throw std::runtime_error(path.string() + ": missing required attribute '" + std::string(key) + "'");
        }
        return *value;
    }

    [[nodiscard]] auto BoolAttribute(const XmlElement& node, std::string_view key, bool defaultValue = false) -> bool
    {
        const auto value = Attribute(node, key);
        if (!value.has_value())
        {
            return defaultValue;
        }
        return *value == "true" || *value == "1" || *value == "yes";
    }

    [[nodiscard]] auto Lower(std::string value) -> std::string
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    [[nodiscard]] auto EscapeXml(std::string_view input) -> std::string
    {
        std::string out;
        out.reserve(input.size());
        for (const char ch : input)
        {
            switch (ch)
            {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                case '"': out += "&quot;"; break;
                case '\'': out += "&apos;"; break;
                default: out.push_back(ch); break;
            }
        }
        return out;
    }

    [[nodiscard]] auto EscapeCMake(std::string_view input) -> std::string
    {
        std::string out;
        out.reserve(input.size());
        for (const char ch : input)
        {
            if (ch == '\\' || ch == '"')
            {
                out.push_back('\\');
            }
            out.push_back(ch);
        }
        return out;
    }

    [[nodiscard]] auto ToolExists(const std::string& tool) -> bool
    {
#if defined(_WIN32)
        return std::system(("where " + tool + " >nul 2>nul").c_str()) == 0;
#else
        return std::system(("command -v " + tool + " >/dev/null 2>&1").c_str()) == 0;
#endif
    }

    [[nodiscard]] auto CaptureCommand(const std::string& command) -> std::optional<std::string>
    {
        std::array<char, 256> buffer {};
        std::string out;
#if defined(_WIN32)
        FILE* pipe = _popen(command.c_str(), "r");
#else
        FILE* pipe = popen(command.c_str(), "r");
#endif
        if (pipe == nullptr)
        {
            return std::nullopt;
        }
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
        {
            out += buffer.data();
        }
#if defined(_WIN32)
        const int rc = _pclose(pipe);
#else
        const int rc = pclose(pipe);
#endif
        if (rc != 0)
        {
            return std::nullopt;
        }
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        {
            out.pop_back();
        }
        return out;
    }

    struct Component
    {
        std::string              name {};
        std::string              kind {};
        std::string              location {};
        std::string              packagePath {};
        std::string              repoUrl {};
        std::string              ref {};
        std::string              version {};
        bool                     required {false};
        std::vector<std::string> dependsOn {};
    };

    struct PlatformRelease
    {
        std::string            platformVersion {};
        std::vector<Component> components {};
    };

    struct PackageDependency
    {
        std::string name {};
        std::string versionRange {};
        bool        optional {false};
    };

    struct ContentFile
    {
        std::string source {};
        std::string kind {};
        std::string target {};
    };

    struct SourceBinding
    {
        std::string kind {};
        std::string path {};
    };

    struct LibraryArtifact
    {
        std::string name {};
        std::string target {};
        std::string linkage {};
        std::string origin {};
        bool        exported {true};
    };

    struct ExecutableArtifact
    {
        std::string name {};
        std::string target {};
        std::string origin {};
        bool        exported {true};
    };

    struct ArtifactDescriptor
    {
        std::vector<LibraryArtifact>   libraries {};
        std::vector<ExecutableArtifact> executables {};
    };

    struct BuildDescriptor
    {
        std::string backend {};
    };

    struct ModuleDescriptor
    {
        std::string              name {};
        std::string              family {};
        std::string              type {};
        std::string              loadPhase {};
        std::string              version {};
        std::string              compatiblePlatformRange {};
        std::vector<std::string> platforms {};
        std::vector<std::string> required {};
        std::vector<std::string> optional {};
        std::vector<std::string> providesServices {};
        std::vector<std::string> requiresServices {};
        std::vector<std::string> capabilities {};
        bool                     editorOnly {false};
        bool                     requiresReflection {false};
    };

    struct PluginDescriptor
    {
        std::string              name {};
        std::vector<std::string> platforms {};
        std::vector<std::string> requiredModules {};
        std::vector<std::string> optionalModules {};
        bool                     optional {false};
    };

    struct PackageManifest
    {
        fs::path                       path {};
        std::string                    name {};
        std::string                    version {};
        std::string                    compatiblePlatformRange {};
        SourceBinding                  sourceBinding {};
        ArtifactDescriptor             artifacts {};
        BuildDescriptor                build {};
        std::vector<std::string>       platforms {};
        std::vector<PackageDependency> dependencies {};
        std::vector<ContentFile>       contents {};
        std::vector<ModuleDescriptor>  modules {};
        std::vector<PluginDescriptor>  plugins {};
    };

    struct PackageCatalogEntry
    {
        std::string name {};
        fs::path    manifestPath {};
        std::string component {};
    };

    struct PackageReference
    {
        std::string name {};
        std::string versionRange {};
        bool        optional {false};
    };

    struct TargetDefinition
    {
        std::string                   name {};
        std::string                   type {};
        std::string                   profile {};
        std::string                   platform {};
        bool                          enableReflection {false};
        std::string                   environmentName {};
        std::string                   workingDirectory {};
        std::optional<std::string>    launchExecutable {};
        std::vector<std::string>      configSources {};
        std::vector<PackageReference> packages {};
        std::vector<std::string>      modulesEnable {};
        std::vector<std::string>      modulesDisable {};
        std::vector<std::string>      pluginsEnable {};
        std::vector<std::string>      pluginsDisable {};
    };

    struct ProjectManifest
    {
        fs::path                     path {};
        std::string                  name {};
        std::string                  defaultTarget {};
        std::vector<TargetDefinition> targets {};
    };

    struct ResolvedPackage
    {
        PackageManifest manifest {};
        std::string     source {"catalog"};
    };

    struct ResolvedTarget
    {
        ProjectManifest                              project {};
        TargetDefinition                             target {};
        std::vector<ResolvedPackage>                 orderedPackages {};
        std::map<std::string, std::set<std::string>> packageEdges {};
        std::vector<std::string>                     requiredModules {};
        std::vector<std::string>                     optionalModules {};
        std::map<std::string, std::set<std::string>> dependencyEdges {};
        std::vector<std::string>                     enabledPlugins {};
        std::vector<LibraryArtifact>                libraries {};
        std::vector<ExecutableArtifact>             executables {};
        std::optional<ExecutableArtifact>           selectedExecutable {};
    };

    [[nodiscard]] auto PlatformReleasePath(const fs::path& root) -> fs::path
    {
        return root / "Workspace" / "Releases" / "platform-release.xml";
    }

    [[nodiscard]] auto PackageCatalogPath(const fs::path& root) -> fs::path
    {
        return root / "Workspace" / "Catalogs" / "package-catalog.xml";
    }

    [[nodiscard]] auto RootDirFrom(const fs::path& start) -> std::optional<fs::path>
    {
        auto current = fs::weakly_canonical(start);
        if (fs::is_regular_file(current))
        {
            current = current.parent_path();
        }
        while (!current.empty())
        {
            if (fs::exists(PlatformReleasePath(current)) && fs::exists(PackageCatalogPath(current)))
            {
                return current;
            }
            if (current == current.parent_path())
            {
                break;
            }
            current = current.parent_path();
        }
        return std::nullopt;
    }

    [[nodiscard]] auto RootDir(const char* argv0) -> fs::path
    {
        if (const auto fromExe = RootDirFrom(fs::absolute(argv0)); fromExe.has_value())
        {
            return *fromExe;
        }
        if (const auto fromCwd = RootDirFrom(fs::current_path()); fromCwd.has_value())
        {
            return *fromCwd;
        }
        throw std::runtime_error("failed to locate NGIN workspace root");
    }

    [[nodiscard]] auto PlatformAliases(const std::string& platform) -> std::set<std::string>
    {
        const auto lower = Lower(platform);
        std::set<std::string> out {lower};
        const auto dash = lower.find('-');
        const auto primary = dash == std::string::npos ? lower : lower.substr(0, dash);
        out.insert(primary);
        if (primary.rfind("win", 0) == 0)
        {
            out.insert("windows");
        }
        if (primary == "darwin")
        {
            out.insert("macos");
        }
        return out;
    }

    [[nodiscard]] auto PlatformSupported(const std::string& targetPlatform, const std::vector<std::string>& declaredPlatforms) -> bool
    {
        const auto aliases = PlatformAliases(targetPlatform);
        for (const auto& candidate : declaredPlatforms)
        {
            if (aliases.contains(Lower(candidate)))
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto DefaultArtifactOrigin(const std::string& sourceKind) -> std::string
    {
        const auto kind = Lower(sourceKind);
        if (kind == "source")
        {
            return "Built";
        }
        if (kind == "cmakepackage")
        {
            return "Imported";
        }
        if (kind == "prebuilt")
        {
            return "Prebuilt";
        }
        return {};
    }

    [[nodiscard]] auto EffectiveArtifactOrigin(const std::string& explicitOrigin, const std::string& sourceKind) -> std::string
    {
        if (!explicitOrigin.empty())
        {
            return explicitOrigin;
        }
        return DefaultArtifactOrigin(sourceKind);
    }

    [[nodiscard]] auto ParseSemver(const std::string& text) -> std::optional<std::array<int, 3>>
    {
        std::array<int, 3> parts {};
        std::stringstream  ss(text.substr(0, text.find('-')));
        std::string        token;
        for (int index = 0; index < 3; ++index)
        {
            if (!std::getline(ss, token, '.'))
            {
                return std::nullopt;
            }
            if (token.empty() || !std::all_of(token.begin(), token.end(), [](unsigned char c) { return std::isdigit(c); }))
            {
                return std::nullopt;
            }
            parts[index] = std::stoi(token);
        }
        return parts;
    }

    [[nodiscard]] auto CompareSemver(const std::string& left, const std::string& right) -> int
    {
        const auto a = ParseSemver(left);
        const auto b = ParseSemver(right);
        if (!a.has_value() || !b.has_value())
        {
            return left.compare(right);
        }
        for (int index = 0; index < 3; ++index)
        {
            if ((*a)[index] < (*b)[index])
            {
                return -1;
            }
            if ((*a)[index] > (*b)[index])
            {
                return 1;
            }
        }
        return 0;
    }

    [[nodiscard]] auto VersionSatisfies(const std::string& version, const std::string& rangeText) -> bool
    {
        if (rangeText.empty())
        {
            return true;
        }
        std::stringstream stream(rangeText);
        std::string       token;
        while (stream >> token)
        {
            std::string op {"="};
            std::string rhs {token};
            if (token.rfind(">=", 0) == 0 || token.rfind("<=", 0) == 0)
            {
                op = token.substr(0, 2);
                rhs = token.substr(2);
            }
            else if (!token.empty() && (token[0] == '>' || token[0] == '<' || token[0] == '='))
            {
                op = token.substr(0, 1);
                rhs = token.substr(1);
            }
            const auto cmp = CompareSemver(version, rhs);
            if (op == "=" && cmp != 0)
            {
                return false;
            }
            if (op == ">" && cmp <= 0)
            {
                return false;
            }
            if (op == ">=" && cmp < 0)
            {
                return false;
            }
            if (op == "<" && cmp >= 0)
            {
                return false;
            }
            if (op == "<=" && cmp > 0)
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] auto TopologicalDependenciesFirst(
        const std::set<std::string>& nodes,
        const std::map<std::string, std::set<std::string>>& dependencyEdges) -> std::optional<std::vector<std::string>>
    {
        std::map<std::string, int>               indegree {};
        std::map<std::string, std::set<std::string>> dependents {};
        for (const auto& node : nodes)
        {
            indegree[node] = 0;
        }
        for (const auto& node : nodes)
        {
            const auto it = dependencyEdges.find(node);
            if (it == dependencyEdges.end())
            {
                continue;
            }
            for (const auto& dep : it->second)
            {
                if (nodes.contains(dep))
                {
                    ++indegree[node];
                    dependents[dep].insert(node);
                }
            }
        }

        std::vector<std::string> queue;
        for (const auto& [node, deg] : indegree)
        {
            if (deg == 0)
            {
                queue.push_back(node);
            }
        }
        std::sort(queue.begin(), queue.end());
        std::vector<std::string> ordered;
        while (!queue.empty())
        {
            const auto current = queue.front();
            queue.erase(queue.begin());
            ordered.push_back(current);
            for (const auto& dep : dependents[current])
            {
                --indegree[dep];
                if (indegree[dep] == 0)
                {
                    queue.push_back(dep);
                    std::sort(queue.begin(), queue.end());
                }
            }
        }

        if (ordered.size() != nodes.size())
        {
            return std::nullopt;
        }
        return ordered;
    }

    [[nodiscard]] auto DetectCycles(
        const std::set<std::string>& nodes,
        const std::map<std::string, std::set<std::string>>& dependencyEdges) -> std::vector<std::string>
    {
        auto ordered = TopologicalDependenciesFirst(nodes, dependencyEdges);
        if (ordered.has_value())
        {
            return {};
        }
        return std::vector<std::string>(nodes.begin(), nodes.end());
    }

    [[nodiscard]] auto LoadPlatformRelease(const fs::path& root) -> PlatformRelease
    {
        const auto path = PlatformReleasePath(root);
        const auto doc = LoadXml(path);
        const auto* rootElement = doc.document.Root();
        if (rootElement == nullptr || rootElement->name != "PlatformRelease")
        {
            throw std::runtime_error(path.string() + ": root element must be <PlatformRelease>");
        }

        PlatformRelease release {};
        release.platformVersion = RequireAttribute(*rootElement, "PlatformVersion", path);
        const auto* componentsNode = FindChild(*rootElement, "Components");
        if (componentsNode == nullptr)
        {
            throw std::runtime_error(path.string() + ": missing <Components>");
        }
        for (const auto* child : ChildElements(*componentsNode, "Component"))
        {
            Component component {};
            component.name = RequireAttribute(*child, "Name", path);
            component.kind = Attribute(*child, "Kind").value_or("");
            component.location = Attribute(*child, "Location").value_or("");
            component.packagePath = Attribute(*child, "PackagePath").value_or("");
            component.repoUrl = Attribute(*child, "RepoUrl").value_or("");
            component.ref = Attribute(*child, "Ref").value_or("");
            component.version = Attribute(*child, "Version").value_or("");
            component.required = BoolAttribute(*child, "Required");
            if (const auto* depends = FindChild(*child, "DependsOn"))
            {
                for (const auto* dep : ChildElements(*depends, "ComponentRef"))
                {
                    component.dependsOn.push_back(RequireAttribute(*dep, "Name", path));
                }
            }
            release.components.push_back(std::move(component));
        }
        return release;
    }

    [[nodiscard]] auto LoadPackageCatalog(const fs::path& root) -> std::unordered_map<std::string, PackageCatalogEntry>
    {
        const auto path = PackageCatalogPath(root);
        const auto doc = LoadXml(path);
        const auto* rootElement = doc.document.Root();
        if (rootElement == nullptr || rootElement->name != "PackageCatalog")
        {
            throw std::runtime_error(path.string() + ": root element must be <PackageCatalog>");
        }
        std::unordered_map<std::string, PackageCatalogEntry> out;
        const auto* packagesNode = FindChild(*rootElement, "Packages");
        if (packagesNode == nullptr)
        {
            throw std::runtime_error(path.string() + ": missing <Packages>");
        }
        for (const auto* node : ChildElements(*packagesNode, "PackageEntry"))
        {
            PackageCatalogEntry entry {};
            entry.name = RequireAttribute(*node, "Name", path);
            entry.manifestPath = root / RequireAttribute(*node, "ManifestPath", path);
            entry.component = Attribute(*node, "Component").value_or("");
            out.emplace(entry.name, std::move(entry));
        }
        return out;
    }

    [[nodiscard]] auto LoadPackageManifest(const fs::path& path) -> PackageManifest
    {
        const auto doc = LoadXml(path);
        const auto* rootElement = doc.document.Root();
        if (rootElement == nullptr || rootElement->name != "Package")
        {
            throw std::runtime_error(path.string() + ": root element must be <Package>");
        }
        PackageManifest package {};
        package.path = path;
        package.name = RequireAttribute(*rootElement, "Name", path);
        package.version = RequireAttribute(*rootElement, "Version", path);
        package.compatiblePlatformRange = Attribute(*rootElement, "CompatiblePlatformRange").value_or("");
        if (const auto* sourceBinding = FindChild(*rootElement, "SourceBinding"))
        {
            package.sourceBinding.kind = Attribute(*sourceBinding, "Kind").value_or("");
            package.sourceBinding.path = Attribute(*sourceBinding, "Path").value_or("");
        }
        if (const auto* artifacts = FindChild(*rootElement, "Artifacts"))
        {
            if (const auto* libraries = FindChild(*artifacts, "Libraries"))
            {
                for (const auto* node : ChildElements(*libraries, "Library"))
                {
                    LibraryArtifact artifact {};
                    artifact.name = RequireAttribute(*node, "Name", path);
                    artifact.target = Attribute(*node, "Target").value_or("");
                    artifact.linkage = Attribute(*node, "Linkage").value_or("");
                    artifact.origin = Attribute(*node, "Origin").value_or("");
                    artifact.exported = !Attribute(*node, "Exported").has_value() || BoolAttribute(*node, "Exported", true);
                    package.artifacts.libraries.push_back(std::move(artifact));
                }
            }
            if (const auto* executables = FindChild(*artifacts, "Executables"))
            {
                for (const auto* node : ChildElements(*executables, "Executable"))
                {
                    ExecutableArtifact artifact {};
                    artifact.name = RequireAttribute(*node, "Name", path);
                    artifact.target = Attribute(*node, "Target").value_or("");
                    artifact.origin = Attribute(*node, "Origin").value_or("");
                    artifact.exported = !Attribute(*node, "Exported").has_value() || BoolAttribute(*node, "Exported", true);
                    package.artifacts.executables.push_back(std::move(artifact));
                }
            }
        }
        if (const auto* build = FindChild(*rootElement, "Build"))
        {
            package.build.backend = Attribute(*build, "Backend").value_or("");
        }
        if (const auto* platforms = FindChild(*rootElement, "Platforms"))
        {
            for (const auto* node : ChildElements(*platforms, "Platform"))
            {
                package.platforms.push_back(RequireAttribute(*node, "Name", path));
            }
        }
        if (const auto* deps = FindChild(*rootElement, "Dependencies"))
        {
            for (const auto* node : ChildElements(*deps, "Dependency"))
            {
                PackageDependency dependency {};
                dependency.name = RequireAttribute(*node, "Name", path);
                dependency.versionRange = Attribute(*node, "VersionRange").value_or("");
                dependency.optional = BoolAttribute(*node, "Optional");
                package.dependencies.push_back(std::move(dependency));
            }
        }
        if (const auto* contents = FindChild(*rootElement, "Contents"))
        {
            for (const auto* node : ChildElements(*contents, "File"))
            {
                ContentFile content {};
                content.source = RequireAttribute(*node, "Source", path);
                content.kind = Attribute(*node, "Kind").value_or("other");
                content.target = Attribute(*node, "Target").value_or("");
                package.contents.push_back(std::move(content));
            }
        }

        const auto* modules = FindChild(*rootElement, "Modules");
        if (modules == nullptr)
        {
            throw std::runtime_error(path.string() + ": missing <Modules>");
        }
        for (const auto* node : ChildElements(*modules, "Module"))
        {
            ModuleDescriptor module {};
            module.name = RequireAttribute(*node, "Name", path);
            module.family = Attribute(*node, "Family").value_or("Core");
            module.type = Attribute(*node, "Type").value_or("Runtime");
            module.loadPhase = Attribute(*node, "LoadPhase").value_or("CoreServices");
            module.version = Attribute(*node, "Version").value_or("");
            module.compatiblePlatformRange = Attribute(*node, "CompatiblePlatformRange").value_or("");
            module.requiresReflection = BoolAttribute(*node, "ReflectionRequired");
            module.editorOnly = Lower(module.type) == "editor";

            if (const auto* platforms = FindChild(*node, "Platforms"))
            {
                for (const auto* platform : ChildElements(*platforms, "Platform"))
                {
                    module.platforms.push_back(RequireAttribute(*platform, "Name", path));
                }
            }

            if (const auto* dependencies = FindChild(*node, "Dependencies"))
            {
                for (const auto* dep : ChildElements(*dependencies, "Dependency"))
                {
                    const auto name = RequireAttribute(*dep, "Name", path);
                    if (BoolAttribute(*dep, "Optional"))
                    {
                        module.optional.push_back(name);
                    }
                    else
                    {
                        module.required.push_back(name);
                    }
                }
            }

            if (const auto* providesServices = FindChild(*node, "ProvidesServices"))
            {
                for (const auto* service : ChildElements(*providesServices, "Service"))
                {
                    module.providesServices.push_back(RequireAttribute(*service, "Name", path));
                }
            }

            if (const auto* requiresServices = FindChild(*node, "RequiresServices"))
            {
                for (const auto* service : ChildElements(*requiresServices, "Service"))
                {
                    module.requiresServices.push_back(RequireAttribute(*service, "Name", path));
                }
            }

            if (const auto* capabilities = FindChild(*node, "Capabilities"))
            {
                for (const auto* capability : ChildElements(*capabilities, "Capability"))
                {
                    module.capabilities.push_back(RequireAttribute(*capability, "Name", path));
                }
            }

            package.modules.push_back(std::move(module));
        }

        if (const auto* plugins = FindChild(*rootElement, "Plugins"))
        {
            for (const auto* node : ChildElements(*plugins, "Plugin"))
            {
                PluginDescriptor plugin {};
                plugin.name = RequireAttribute(*node, "Name", path);
                plugin.optional = BoolAttribute(*node, "Optional");

                if (const auto* platforms = FindChild(*node, "Platforms"))
                {
                    for (const auto* platform : ChildElements(*platforms, "Platform"))
                    {
                        plugin.platforms.push_back(RequireAttribute(*platform, "Name", path));
                    }
                }

                if (const auto* modulesElement = FindChild(*node, "Modules"))
                {
                    if (const auto* required = FindChild(*modulesElement, "Required"))
                    {
                        for (const auto* dep : ChildElements(*required, "ModuleRef"))
                        {
                            plugin.requiredModules.push_back(RequireAttribute(*dep, "Name", path));
                        }
                    }
                    if (const auto* optional = FindChild(*modulesElement, "Optional"))
                    {
                        for (const auto* dep : ChildElements(*optional, "ModuleRef"))
                        {
                            plugin.optionalModules.push_back(RequireAttribute(*dep, "Name", path));
                        }
                    }
                }

                package.plugins.push_back(std::move(plugin));
            }
        }
        return package;
    }

    [[nodiscard]] auto LoadProjectManifest(const fs::path& path) -> ProjectManifest
    {
        const auto doc = LoadXml(path);
        const auto* rootElement = doc.document.Root();
        if (rootElement == nullptr || rootElement->name != "Project")
        {
            throw std::runtime_error(path.string() + ": root element must be <Project>");
        }
        ProjectManifest project {};
        project.path = path;
        project.name = RequireAttribute(*rootElement, "Name", path);
        project.defaultTarget = RequireAttribute(*rootElement, "DefaultTarget", path);
        const auto* targetsNode = FindChild(*rootElement, "Targets");
        if (targetsNode == nullptr)
        {
            throw std::runtime_error(path.string() + ": missing <Targets>");
        }
        for (const auto* node : ChildElements(*targetsNode, "Target"))
        {
            TargetDefinition target {};
            target.name = RequireAttribute(*node, "Name", path);
            target.type = RequireAttribute(*node, "Type", path);
            target.profile = RequireAttribute(*node, "Profile", path);
            target.platform = RequireAttribute(*node, "Platform", path);
            target.enableReflection = BoolAttribute(*node, "EnableReflection");
            target.environmentName = Attribute(*node, "Environment").value_or("");
            target.workingDirectory = Attribute(*node, "WorkingDirectory").value_or(".");
            if (const auto* launch = FindChild(*node, "Launch"))
            {
                if (const auto executable = Attribute(*launch, "Executable"); executable.has_value() && !executable->empty())
                {
                    target.launchExecutable = *executable;
                }
            }
            if (const auto* config = FindChild(*node, "ConfigSources"))
            {
                for (const auto* item : ChildElements(*config, "Config"))
                {
                    target.configSources.push_back(RequireAttribute(*item, "Source", path));
                }
            }
            if (const auto* packages = FindChild(*node, "Packages"))
            {
                for (const auto* item : ChildElements(*packages, "PackageRef"))
                {
                    PackageReference packageReference {};
                    packageReference.name = RequireAttribute(*item, "Name", path);
                    packageReference.versionRange = Attribute(*item, "VersionRange").value_or("");
                    packageReference.optional = BoolAttribute(*item, "Optional");
                    target.packages.push_back(std::move(packageReference));
                }
            }
            if (const auto* modules = FindChild(*node, "Modules"))
            {
                for (const auto* item : ChildElements(*modules, "Enable"))
                {
                    target.modulesEnable.push_back(RequireAttribute(*item, "Name", path));
                }
                for (const auto* item : ChildElements(*modules, "Disable"))
                {
                    target.modulesDisable.push_back(RequireAttribute(*item, "Name", path));
                }
            }
            if (const auto* plugins = FindChild(*node, "Plugins"))
            {
                for (const auto* item : ChildElements(*plugins, "Enable"))
                {
                    target.pluginsEnable.push_back(RequireAttribute(*item, "Name", path));
                }
                for (const auto* item : ChildElements(*plugins, "Disable"))
                {
                    target.pluginsDisable.push_back(RequireAttribute(*item, "Name", path));
                }
            }
            project.targets.push_back(std::move(target));
        }
        return project;
    }

    [[nodiscard]] auto FindProjectFile(const fs::path& start) -> std::optional<fs::path>
    {
        auto current = fs::weakly_canonical(start);
        while (true)
        {
            std::vector<fs::path> candidates;
            if (fs::exists(current))
            {
                for (const auto& entry : fs::directory_iterator(current))
                {
                    if (entry.is_regular_file() && entry.path().extension() == ".nginproj")
                    {
                        candidates.push_back(entry.path());
                    }
                }
            }
            if (!candidates.empty())
            {
                std::sort(candidates.begin(), candidates.end());
                return candidates.front();
            }
            if (current == current.parent_path())
            {
                break;
            }
            current = current.parent_path();
        }
        return std::nullopt;
    }

    [[nodiscard]] auto ResolveProjectPath(const std::optional<std::string>& explicitPath) -> fs::path
    {
        if (explicitPath.has_value())
        {
            return fs::weakly_canonical(*explicitPath);
        }
        if (const auto discovered = FindProjectFile(fs::current_path()); discovered.has_value())
        {
            return *discovered;
        }
        throw std::runtime_error("no project manifest specified and no .nginproj file found in the current directory tree");
    }

    [[nodiscard]] auto TargetByName(const ProjectManifest& project, const std::optional<std::string>& targetName) -> const TargetDefinition&
    {
        const auto desired = targetName.value_or(project.defaultTarget);
        for (const auto& target : project.targets)
        {
            if (target.name == desired)
            {
                return target;
            }
        }
        throw std::runtime_error("unknown target '" + desired + "'");
    }

    auto ResolvePackages(
        const ProjectManifest&,
        const TargetDefinition& target,
        const PlatformRelease& release,
        const std::unordered_map<std::string, PackageCatalogEntry>& catalog,
        IssueReport& report) -> std::vector<ResolvedPackage>
    {
        std::unordered_map<std::string, ResolvedPackage> resolved;
        std::map<std::string, std::set<std::string>>     edges;
        std::vector<PackageReference>                    queue = target.packages;
        std::vector<std::string>                         parents(queue.size(), "");

        std::size_t index = 0;
        while (index < queue.size())
        {
            const auto ref = queue[index];
            const auto requiredBy = parents[index];
            ++index;

            const auto itCatalog = catalog.find(ref.name);
            if (itCatalog == catalog.end())
            {
                const auto message = "package '" + ref.name + "' could not be resolved";
                if (ref.optional)
                {
                    AddWarning(report, message);
                }
                else
                {
                    AddError(report, requiredBy.empty() ? message : message + " (required by '" + requiredBy + "')");
                }
                continue;
            }

            if (resolved.contains(ref.name))
            {
                if (!requiredBy.empty())
                {
                    edges[requiredBy].insert(ref.name);
                }
                continue;
            }

            auto manifest = LoadPackageManifest(fs::weakly_canonical(itCatalog->second.manifestPath));
            if (manifest.name != ref.name)
            {
                AddError(report, "package '" + ref.name + "' resolved to manifest for '" + manifest.name + "'");
                continue;
            }
            if (!ref.versionRange.empty() && !VersionSatisfies(manifest.version, ref.versionRange))
            {
                const auto message = "package '" + ref.name + "' version " + manifest.version + " does not satisfy '" + ref.versionRange + "'";
                if (ref.optional)
                {
                    AddWarning(report, message);
                }
                else
                {
                    AddError(report, message);
                }
                continue;
            }
            if (!manifest.platforms.empty() && !PlatformSupported(target.platform, manifest.platforms))
            {
                const auto message = "package '" + ref.name + "' is not supported on platform '" + target.platform + "'";
                if (ref.optional)
                {
                    AddWarning(report, message);
                }
                else
                {
                    AddError(report, message);
                }
                continue;
            }
            if (!manifest.compatiblePlatformRange.empty() && !VersionSatisfies(release.platformVersion, manifest.compatiblePlatformRange))
            {
                AddError(report, "package '" + ref.name + "' compatible platform range does not include platform version '" + release.platformVersion + "'");
                continue;
            }

            for (const auto& content : manifest.contents)
            {
                const auto resolvedPath = manifest.path.parent_path() / content.source;
                if (!fs::exists(resolvedPath))
                {
                    AddError(report, "package '" + ref.name + "' content file '" + content.source + "' does not exist");
                }
            }

            if (!requiredBy.empty())
            {
                edges[requiredBy].insert(ref.name);
            }
            edges[ref.name];
            for (const auto& dep : manifest.dependencies)
            {
                queue.push_back({dep.name, dep.versionRange, dep.optional});
                parents.push_back(ref.name);
                edges[ref.name].insert(dep.name);
            }

            resolved.emplace(ref.name, ResolvedPackage {std::move(manifest), "catalog"});
        }

        if (!report.errors.empty())
        {
            return {};
        }

        std::set<std::string> nodes;
        for (const auto& [name, _] : resolved)
        {
            nodes.insert(name);
        }
        if (const auto cycles = DetectCycles(nodes, edges); !cycles.empty())
        {
            AddError(report, "package graph contains dependency cycle(s)");
            return {};
        }
        const auto orderedNames = TopologicalDependenciesFirst(nodes, edges);
        if (!orderedNames.has_value())
        {
            AddError(report, "package graph could not be ordered");
            return {};
        }
        std::vector<ResolvedPackage> ordered;
        for (const auto& name : *orderedNames)
        {
            ordered.push_back(resolved.at(name));
        }
        return ordered;
    }

    auto ResolveArtifacts(
        const std::vector<ResolvedPackage>& orderedPackages,
        const TargetDefinition& target,
        IssueReport& report,
        std::vector<LibraryArtifact>& librariesOut,
        std::vector<ExecutableArtifact>& executablesOut,
        std::optional<ExecutableArtifact>& selectedExecutableOut) -> void
    {
        std::unordered_map<std::string, std::string> libraryProviders;
        std::unordered_map<std::string, std::string> executableProviders;

        for (const auto& package : orderedPackages)
        {
            for (auto artifact : package.manifest.artifacts.libraries)
            {
                if (!artifact.exported)
                {
                    continue;
                }
                artifact.origin = EffectiveArtifactOrigin(artifact.origin, package.manifest.sourceBinding.kind);
                if (artifact.origin.empty())
                {
                    AddError(report, "package '" + package.manifest.name + "' library artifact '" + artifact.name + "' does not declare an origin and it could not be inferred");
                    continue;
                }
                if (const auto it = libraryProviders.find(artifact.name); it != libraryProviders.end())
                {
                    AddError(report, "duplicate library artifact '" + artifact.name + "' in packages '" + it->second + "' and '" + package.manifest.name + "'");
                    continue;
                }
                libraryProviders.emplace(artifact.name, package.manifest.name);
                librariesOut.push_back(std::move(artifact));
            }

            for (auto artifact : package.manifest.artifacts.executables)
            {
                if (!artifact.exported)
                {
                    continue;
                }
                artifact.origin = EffectiveArtifactOrigin(artifact.origin, package.manifest.sourceBinding.kind);
                if (artifact.origin.empty())
                {
                    AddError(report, "package '" + package.manifest.name + "' executable artifact '" + artifact.name + "' does not declare an origin and it could not be inferred");
                    continue;
                }
                if (const auto it = executableProviders.find(artifact.name); it != executableProviders.end())
                {
                    AddError(report, "duplicate executable artifact '" + artifact.name + "' in packages '" + it->second + "' and '" + package.manifest.name + "'");
                    continue;
                }
                executableProviders.emplace(artifact.name, package.manifest.name);
                executablesOut.push_back(std::move(artifact));
            }
        }

        if (!target.launchExecutable.has_value())
        {
            if (executablesOut.size() == 1)
            {
                selectedExecutableOut = executablesOut.front();
            }
            else if (executablesOut.size() > 1)
            {
                AddError(report, "target '" + target.name + "' resolves multiple executable artifacts; add <Launch Executable=\"...\" /> to select one");
            }
            return;
        }

        const auto desired = *target.launchExecutable;
        for (const auto& executable : executablesOut)
        {
            if (executable.name == desired)
            {
                selectedExecutableOut = executable;
                return;
            }
        }
        AddError(report, "target '" + target.name + "' selects executable '" + desired + "' but no package exposes it");
    }

    auto ResolveTarget(
        const fs::path& root,
        const ProjectManifest& project,
        const TargetDefinition& target,
        IssueReport& report) -> std::optional<ResolvedTarget>
    {
        const auto release = LoadPlatformRelease(root);
        const auto packageCatalog = LoadPackageCatalog(root);

        auto orderedPackages = ResolvePackages(project, target, release, packageCatalog, report);
        if (!report.errors.empty())
        {
            return std::nullopt;
        }

        std::unordered_map<std::string, std::set<std::string>> providersByModule;
        std::unordered_map<std::string, std::set<std::string>> providersByPlugin;
        std::unordered_map<std::string, ModuleDescriptor>        modules;
        std::unordered_map<std::string, PluginDescriptor>        plugins;
        for (const auto& package : orderedPackages)
        {
            for (const auto& module : package.manifest.modules)
            {
                if (!module.platforms.empty() && !PlatformSupported(target.platform, module.platforms))
                {
                    AddError(report, "package '" + package.manifest.name + "' provides module '" + module.name + "' that is not supported on platform '" + target.platform + "'");
                    continue;
                }
                if (const auto providerIt = providersByModule.find(module.name); providerIt != providersByModule.end() && !providerIt->second.empty())
                {
                    AddError(report, "duplicate module declaration for '" + module.name + "' in packages '" + *providerIt->second.begin() + "' and '" + package.manifest.name + "'");
                    continue;
                }
                modules.emplace(module.name, module);
                providersByModule[module.name].insert(package.manifest.name);
            }
            for (const auto& plugin : package.manifest.plugins)
            {
                if (!plugin.platforms.empty() && !PlatformSupported(target.platform, plugin.platforms))
                {
                    AddError(report, "package '" + package.manifest.name + "' provides plugin '" + plugin.name + "' that is not supported on platform '" + target.platform + "'");
                    continue;
                }
                if (const auto providerIt = providersByPlugin.find(plugin.name); providerIt != providersByPlugin.end() && !providerIt->second.empty())
                {
                    AddError(report, "duplicate plugin declaration for '" + plugin.name + "' in packages '" + *providerIt->second.begin() + "' and '" + package.manifest.name + "'");
                    continue;
                }
                plugins.emplace(plugin.name, plugin);
                providersByPlugin[plugin.name].insert(package.manifest.name);
            }
        }
        if (!report.errors.empty())
        {
            return std::nullopt;
        }

        std::set<std::string> directModules;
        for (const auto& name : target.modulesEnable)
        {
            if (std::find(target.modulesDisable.begin(), target.modulesDisable.end(), name) == target.modulesDisable.end())
            {
                directModules.insert(name);
            }
        }
        std::set<std::string> directPlugins;
        for (const auto& name : target.pluginsEnable)
        {
            if (std::find(target.pluginsDisable.begin(), target.pluginsDisable.end(), name) == target.pluginsDisable.end())
            {
                directPlugins.insert(name);
            }
        }

        for (const auto& plugin : directPlugins)
        {
            if (!plugins.contains(plugin))
            {
                AddError(report, "target '" + target.name + "' references unknown plugin '" + plugin + "'");
                continue;
            }
            if (!providersByPlugin.contains(plugin))
            {
                AddError(report, "target '" + target.name + "' enables plugin '" + plugin + "' but no active package provides it");
            }
        }
        for (const auto& module : directModules)
        {
            if (!modules.contains(module))
            {
                AddError(report, "target '" + target.name + "' references unknown module '" + module + "'");
                continue;
            }
            if (!providersByModule.contains(module))
            {
                AddError(report, "target '" + target.name + "' enables module '" + module + "' but no active package provides it");
            }
        }
        if (!report.errors.empty())
        {
            return std::nullopt;
        }

        std::set<std::string> requiredSet = directModules;
        std::set<std::string> optionalSet;
        for (const auto& pluginName : directPlugins)
        {
            const auto& plugin = plugins.at(pluginName);
            for (const auto& moduleName : plugin.requiredModules)
            {
                if (!providersByModule.contains(moduleName))
                {
                    AddError(report, "plugin '" + pluginName + "' requires module '" + moduleName + "' but no active package provides it");
                    continue;
                }
                requiredSet.insert(moduleName);
            }
            for (const auto& moduleName : plugin.optionalModules)
            {
                if (!providersByModule.contains(moduleName))
                {
                    AddWarning(report, "plugin '" + pluginName + "' optional module '" + moduleName + "' is not provided by any active package");
                    continue;
                }
                optionalSet.insert(moduleName);
            }
        }

        std::vector<std::string> reqQueue(requiredSet.begin(), requiredSet.end());
        std::vector<std::string> optQueue(optionalSet.begin(), optionalSet.end());
        std::size_t reqIndex = 0;
        while (reqIndex < reqQueue.size())
        {
            const auto current = reqQueue[reqIndex++];
            const auto it = modules.find(current);
            if (it == modules.end())
            {
                AddError(report, "target '" + target.name + "' references unknown module '" + current + "'");
                continue;
            }
            if (it->second.editorOnly && target.type != "Editor")
            {
                AddError(report, "target '" + target.name + "' includes editor-only module '" + current + "'");
            }
            if (it->second.requiresReflection && !target.enableReflection)
            {
                AddError(report, "target '" + target.name + "' includes module '" + current + "' that requires reflection");
            }
            for (const auto& dep : it->second.required)
            {
                if (!providersByModule.contains(dep))
                {
                    AddError(report, "module '" + current + "' requires '" + dep + "' but no active package provides it");
                    continue;
                }
                if (!requiredSet.contains(dep))
                {
                    requiredSet.insert(dep);
                    reqQueue.push_back(dep);
                }
            }
            for (const auto& dep : it->second.optional)
            {
                if (!providersByModule.contains(dep))
                {
                    continue;
                }
                if (!requiredSet.contains(dep) && !optionalSet.contains(dep))
                {
                    optionalSet.insert(dep);
                    optQueue.push_back(dep);
                }
            }
        }
        std::size_t optIndex = 0;
        while (optIndex < optQueue.size())
        {
            const auto current = optQueue[optIndex++];
            if (requiredSet.contains(current))
            {
                continue;
            }
            const auto it = modules.find(current);
            if (it == modules.end())
            {
                continue;
            }
            for (const auto& dep : it->second.required)
            {
                if (!providersByModule.contains(dep))
                {
                    continue;
                }
                if (!requiredSet.contains(dep) && !optionalSet.contains(dep))
                {
                    optionalSet.insert(dep);
                    optQueue.push_back(dep);
                }
            }
            for (const auto& dep : it->second.optional)
            {
                if (!providersByModule.contains(dep))
                {
                    continue;
                }
                if (!requiredSet.contains(dep) && !optionalSet.contains(dep))
                {
                    optionalSet.insert(dep);
                    optQueue.push_back(dep);
                }
            }
        }

        if (!report.errors.empty())
        {
            return std::nullopt;
        }

        std::set<std::string> allNodes = requiredSet;
        allNodes.insert(optionalSet.begin(), optionalSet.end());
        std::map<std::string, std::set<std::string>> depEdges;
        for (const auto& node : allNodes)
        {
            const auto& module = modules.at(node);
            for (const auto& dep : module.required)
            {
                if (allNodes.contains(dep))
                {
                    depEdges[node].insert(dep);
                }
            }
            for (const auto& dep : module.optional)
            {
                if (allNodes.contains(dep))
                {
                    depEdges[node].insert(dep);
                }
            }
        }
        const auto orderedModules = TopologicalDependenciesFirst(allNodes, depEdges);
        if (!orderedModules.has_value())
        {
            AddError(report, "target closure contains cyclic module dependencies");
            return std::nullopt;
        }

        ResolvedTarget resolved {};
        resolved.project = project;
        resolved.target = target;
        resolved.orderedPackages = std::move(orderedPackages);
        for (const auto& package : resolved.orderedPackages)
        {
            resolved.packageEdges[package.manifest.name] = {};
            for (const auto& dep : package.manifest.dependencies)
            {
                resolved.packageEdges[package.manifest.name].insert(dep.name);
            }
        }
        resolved.enabledPlugins.assign(directPlugins.begin(), directPlugins.end());
        for (const auto& name : *orderedModules)
        {
            if (requiredSet.contains(name))
            {
                resolved.requiredModules.push_back(name);
            }
            else if (optionalSet.contains(name))
            {
                resolved.optionalModules.push_back(name);
            }
        }
        resolved.dependencyEdges = std::move(depEdges);
        ResolveArtifacts(resolved.orderedPackages, resolved.target, report, resolved.libraries, resolved.executables, resolved.selectedExecutable);
        if (!report.errors.empty())
        {
            return std::nullopt;
        }
        return resolved;
    }

    auto PrintIssues(const IssueReport& report, const std::string& title) -> void
    {
        if (!report.errors.empty())
        {
            std::cout << "\n" << title << " errors:\n";
            for (const auto& issue : report.errors)
            {
                std::cout << "  - " << issue << "\n";
            }
        }
        if (!report.warnings.empty())
        {
            std::cout << "\nWarnings:\n";
            for (const auto& issue : report.warnings)
            {
                std::cout << "  - " << issue << "\n";
            }
        }
    }

    [[nodiscard]] auto SanitizeIdentifier(std::string value) -> std::string
    {
        for (auto& ch : value)
        {
            if (!std::isalnum(static_cast<unsigned char>(ch)))
            {
                ch = '_';
            }
        }
        if (value.empty())
        {
            return "artifact";
        }
        return value;
    }

    [[nodiscard]] auto PackageExposesSelectedExecutable(const PackageManifest& manifest, const std::optional<ExecutableArtifact>& selectedExecutable) -> bool
    {
        if (!selectedExecutable.has_value())
        {
            return false;
        }
        return std::any_of(
            manifest.artifacts.executables.begin(),
            manifest.artifacts.executables.end(),
            [&](const ExecutableArtifact& artifact) { return artifact.exported && artifact.name == selectedExecutable->name; });
    }

    [[nodiscard]] auto PackageNeedsCMakeWrapper(const PackageManifest& manifest, const std::optional<ExecutableArtifact>& selectedExecutable) -> bool
    {
        if (Lower(manifest.build.backend) != "cmake")
        {
            return false;
        }
        const auto hasLibraries = std::any_of(
            manifest.artifacts.libraries.begin(),
            manifest.artifacts.libraries.end(),
            [](const LibraryArtifact& artifact) { return artifact.exported && !artifact.target.empty(); });
        return hasLibraries || PackageExposesSelectedExecutable(manifest, selectedExecutable);
    }

    [[nodiscard]] auto HasArtifactTargetsToBuild(const ResolvedTarget& resolved) -> bool
    {
        if (resolved.selectedExecutable.has_value() && !resolved.selectedExecutable->target.empty())
        {
            return true;
        }
        return std::any_of(
            resolved.libraries.begin(),
            resolved.libraries.end(),
            [](const LibraryArtifact& artifact)
            {
                return !artifact.target.empty() && Lower(artifact.linkage) != "interface" && Lower(artifact.origin) != "prebuilt";
            });
    }

    auto WriteGeneratedBuildProject(const ResolvedTarget& resolved, const fs::path& outputDir, IssueReport& report) -> std::optional<fs::path>
    {
        if (!HasArtifactTargetsToBuild(resolved))
        {
            return std::nullopt;
        }

        const auto generatedSourceDir = outputDir / ".ngin" / "cmake-src";
        const auto generatedBuildDir = outputDir / ".ngin" / "cmake-build";
        fs::create_directories(generatedSourceDir);
        fs::create_directories(generatedBuildDir);

        std::ofstream out(generatedSourceDir / "CMakeLists.txt");
        out << "cmake_minimum_required(VERSION 3.20)\n";
        out << "project(NGINGeneratedBuild LANGUAGES CXX)\n";
        out << "set(CMAKE_SUPPRESS_REGENERATION ON)\n";

        std::unordered_set<std::string> addedPackageDirs;
        for (const auto& package : resolved.orderedPackages)
        {
            if (!PackageNeedsCMakeWrapper(package.manifest, resolved.selectedExecutable))
            {
                continue;
            }
            const auto packageDir = fs::weakly_canonical(package.manifest.path.parent_path());
            const auto cmakeLists = packageDir / "CMakeLists.txt";
            if (!fs::exists(cmakeLists))
            {
                AddError(report, "package '" + package.manifest.name + "' requires a CMake wrapper at '" + cmakeLists.string() + "'");
                continue;
            }
            const auto key = packageDir.string();
            if (!addedPackageDirs.insert(key).second)
            {
                continue;
            }
            out << "add_subdirectory(\"" << EscapeCMake(packageDir.string()) << "\" \"${CMAKE_BINARY_DIR}/pkg_" << SanitizeIdentifier(package.manifest.name) << "\")\n";
        }
        if (!report.errors.empty())
        {
            return std::nullopt;
        }

        out << "add_custom_target(ngin_stage_artifacts)\n";

        auto emitStageTarget = [&](const std::string& artifactName,
                                   const std::string& targetName,
                                   const std::string& subdir,
                                   const bool copyFile) {
            const auto safeName = SanitizeIdentifier(artifactName);
            out << "if(NOT TARGET \"" << EscapeCMake(targetName) << "\")\n";
            out << "  message(FATAL_ERROR \"required build target '" << EscapeCMake(targetName) << "' is not available\")\n";
            out << "endif()\n";
            out << "add_custom_target(stage_" << safeName;
            if (copyFile)
            {
                out << "\n"
                    << "  COMMAND ${CMAKE_COMMAND} -E make_directory \"" << EscapeCMake((outputDir / subdir).string()) << "\"\n"
                    << "  COMMAND ${CMAKE_COMMAND} -E copy_if_different \"$<TARGET_FILE:" << targetName << ">\" \"" << EscapeCMake((outputDir / subdir).string()) << "/$<TARGET_FILE_NAME:" << targetName << ">\"\n";
            }
            out << "  DEPENDS \"" << EscapeCMake(targetName) << "\"\n";
            out << "  VERBATIM)\n";
            out << "add_dependencies(ngin_stage_artifacts stage_" << safeName << ")\n";
        };

        for (const auto& library : resolved.libraries)
        {
            if (library.target.empty() || Lower(library.origin) == "prebuilt")
            {
                continue;
            }
            emitStageTarget(library.name, library.target, "lib", Lower(library.linkage) != "interface");
        }
        if (resolved.selectedExecutable.has_value() && !resolved.selectedExecutable->target.empty() && Lower(resolved.selectedExecutable->origin) != "prebuilt")
        {
            emitStageTarget(resolved.selectedExecutable->name, resolved.selectedExecutable->target, "bin", true);
        }

        return generatedBuildDir;
    }

    auto BuildArtifacts(const ResolvedTarget& resolved, const fs::path& outputDir, IssueReport& report) -> void
    {
        const auto generatedBuildDir = WriteGeneratedBuildProject(resolved, outputDir, report);
        if (!generatedBuildDir.has_value() || !report.errors.empty())
        {
            return;
        }

        const auto generatedSourceDir = outputDir / ".ngin" / "cmake-src";
        const auto configure = "cmake -S \"" + generatedSourceDir.string() + "\" -B \"" + generatedBuildDir->string() + "\"";
        if (std::system(configure.c_str()) != 0)
        {
            AddError(report, "failed to configure generated CMake build project for target '" + resolved.target.name + "'");
            return;
        }
        const auto build = "cmake --build \"" + generatedBuildDir->string() + "\" --target ngin_stage_artifacts";
        if (std::system(build.c_str()) != 0)
        {
            AddError(report, "failed to build or stage artifacts for target '" + resolved.target.name + "'");
        }
    }

    auto CollectBuiltArtifactFiles(
        const fs::path& outputDir,
        std::map<fs::path, std::string>& collisions,
        IssueReport& report,
        std::vector<std::tuple<std::string, fs::path, fs::path>>& staged) -> void
    {
        for (const auto& subdir : {std::string("bin"), std::string("lib")})
        {
            const auto base = outputDir / subdir;
            if (!fs::exists(base))
            {
                continue;
            }
            for (const auto& entry : fs::recursive_directory_iterator(base))
            {
                if (!entry.is_regular_file())
                {
                    continue;
                }
                const auto dest = entry.path();
                if (collisions.contains(dest))
                {
                    AddError(report, "build output collision at '" + fs::relative(dest, outputDir).string() + "'");
                    continue;
                }
                collisions[dest] = "<artifact>";
                staged.emplace_back(subdir == "bin" ? "executable" : "library", dest, dest);
            }
        }
    }

    struct ParsedArgs
    {
        std::optional<std::string> projectPath {};
        std::optional<std::string> targetName {};
        std::optional<std::string> outputPath {};
        std::optional<std::string> targetDir {};
        std::optional<std::string> packageName {};
    };

    auto ParseCommonArgs(int argc, char** argv, int startIndex) -> ParsedArgs
    {
        ParsedArgs args {};
        for (int index = startIndex; index < argc; ++index)
        {
            const std::string current = argv[index];
            if (current == "--project" && index + 1 < argc)
            {
                args.projectPath = argv[++index];
            }
            else if (current == "--target" && index + 1 < argc)
            {
                args.targetName = argv[++index];
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

    auto CmdList(const fs::path& root) -> int
    {
        const auto release = LoadPlatformRelease(root);
        for (const auto& component : release.components)
        {
            std::cout << component.name << " " << component.version << " " << (component.ref.empty() ? "unpinned" : component.ref) << "\n";
        }
        return 0;
    }

    [[nodiscard]] auto DefaultComponentLocation(const Component& component) -> fs::path
    {
        if (!component.location.empty())
        {
            return fs::path(component.location);
        }
        if (component.kind == "WorkspaceComponent")
        {
            return fs::path("Packages") / component.name;
        }
        if (component.kind == "ThirdPartyDependency")
        {
            return fs::path("Dependencies") / "ThirdParty" / component.name;
        }
        return fs::path("Dependencies") / "NGIN" / component.name;
    }

    [[nodiscard]] auto ResolveComponentPath(const fs::path& root, const Component& component, const std::optional<std::string>& targetDir) -> std::pair<std::string, fs::path>
    {
        const auto configured = root / DefaultComponentLocation(component);
        if (fs::exists(configured))
        {
            return {"configured", configured};
        }
        if (targetDir.has_value())
        {
            const auto overridePath = fs::path(*targetDir) / component.name;
            if (fs::exists(overridePath))
            {
                return {"override", overridePath};
            }
        }
        return {"none", {}};
    }

    auto CmdStatus(const fs::path& root, const ParsedArgs& args) -> int
    {
        const auto release = LoadPlatformRelease(root);
        std::cout << "COMPONENT             KIND            SOURCE      REQ   REF           HEAD          PATH\n";
        for (const auto& component : release.components)
        {
            const auto [source, path] = ResolveComponentPath(root, component, args.targetDir);
            if (source == "none")
            {
                std::cout << component.name << " " << (component.kind.empty() ? "-" : component.kind) << " none "
                          << (component.required ? "yes" : "no") << " - - missing\n";
                continue;
            }
            const auto head = CaptureCommand("git -C \"" + path.string() + "\" rev-parse HEAD").value_or("-");
            std::cout << component.name << " " << (component.kind.empty() ? "-" : component.kind) << " " << source << " "
                      << (component.required ? "yes" : "no") << " "
                      << (component.ref.empty() ? "unpinned" : component.ref.substr(0, std::min<std::size_t>(12, component.ref.size()))) << " "
                      << head.substr(0, std::min<std::size_t>(12, head.size())) << " " << path.string() << "\n";
        }
        return 0;
    }

    auto CmdDoctor(const fs::path& root, const ParsedArgs& args) -> int
    {
        int fail = 0;
        std::cout << "NGIN workspace doctor\n";
        std::cout << "  root: " << root << "\n";
        std::cout << "  workspace release manifest: " << PlatformReleasePath(root) << "\n";
        if (!ToolExists("git"))
        {
            std::cout << "[error] missing tool: git\n";
            fail = 1;
        }
        else
        {
            std::cout << "[ok] tool: git\n";
        }
        if (!ToolExists("cmake"))
        {
            std::cout << "[error] missing tool: cmake\n";
            fail = 1;
        }
        else
        {
            std::cout << "[ok] tool: cmake\n";
        }
        std::optional<PlatformRelease> release;
        try
        {
            release = LoadPlatformRelease(root);
            (void)LoadPackageCatalog(root);
            std::cout << "[ok] XML manifests parse\n";
        }
        catch (const std::exception& ex)
        {
            std::cout << "[error] " << ex.what() << "\n";
            fail = 1;
        }

        if (!release.has_value())
        {
            std::cout << "\ndoctor result: FAIL\n";
            return 1;
        }
        for (const auto& component : release->components)
        {
            const auto [source, path] = ResolveComponentPath(root, component, args.targetDir);
            if (source == "none")
            {
                std::cout << (component.required ? "[warn] " : "[ok] ") << component.name << ": not present locally\n";
                continue;
            }
            if (!fs::exists(path / ".git"))
            {
                std::cout << "[warn] " << component.name << ": source tree present without nested .git metadata at " << path << "\n";
            }
        }
        std::cout << "\ndoctor result: " << (fail == 0 ? "PASS" : "FAIL") << "\n";
        return fail;
    }

    auto CmdSync(const fs::path& root, const ParsedArgs& args) -> int
    {
        const auto release = LoadPlatformRelease(root);
        for (const auto& component : release.components)
        {
            if (component.kind == "WorkspaceComponent")
            {
                continue;
            }
            if (component.repoUrl.empty() || component.ref.empty())
            {
                continue;
            }
            auto dest = root / DefaultComponentLocation(component);
            if (args.targetDir.has_value())
            {
                dest = fs::path(*args.targetDir) / component.name;
            }
            fs::create_directories(dest.parent_path());
            if (!fs::exists(dest / ".git"))
            {
                const auto clone = "git clone \"" + component.repoUrl + "\" \"" + dest.string() + "\"";
                if (std::system(clone.c_str()) != 0)
                {
                    return 1;
                }
            }
            const auto fetch = "git -C \"" + dest.string() + "\" fetch --tags origin";
            if (std::system(fetch.c_str()) != 0)
            {
                return 1;
            }
            const auto checkout = "git -C \"" + dest.string() + "\" checkout --detach " + component.ref;
            if (std::system(checkout.c_str()) != 0)
            {
                return 1;
            }
        }
        return 0;
    }

    auto CmdPackageList(const fs::path& root) -> int
    {
        const auto catalog = LoadPackageCatalog(root);
        std::vector<std::string> names;
        for (const auto& [name, _] : catalog)
        {
            names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        for (const auto& name : names)
        {
            const auto& entry = catalog.at(name);
            const auto manifest = LoadPackageManifest(entry.manifestPath);
            std::cout << manifest.name << " " << manifest.version << " "
                      << (manifest.sourceBinding.kind.empty() ? "-" : manifest.sourceBinding.kind) << " "
                      << entry.manifestPath.string() << "\n";
        }
        return 0;
    }

    auto CmdPackageShow(const fs::path& root, const ParsedArgs& args) -> int
    {
        if (!args.packageName.has_value())
        {
            throw std::runtime_error("package show requires a package name");
        }
        const auto catalog = LoadPackageCatalog(root);
        const auto it = catalog.find(*args.packageName);
        if (it == catalog.end())
        {
            throw std::runtime_error("unknown package '" + *args.packageName + "'");
        }
        const auto manifest = LoadPackageManifest(it->second.manifestPath);
        std::cout << "Package: " << manifest.name << "\n";
        std::cout << "  version: " << manifest.version << "\n";
        std::cout << "  manifest: " << manifest.path << "\n";
        std::cout << "  source binding: ";
        if (manifest.sourceBinding.kind.empty() && manifest.sourceBinding.path.empty())
        {
            std::cout << "(none)\n";
        }
        else
        {
            std::cout << (manifest.sourceBinding.kind.empty() ? "-" : manifest.sourceBinding.kind);
            if (!manifest.sourceBinding.path.empty())
            {
                std::cout << " -> " << manifest.sourceBinding.path;
            }
            std::cout << "\n";
        }
        std::cout << "  build backend: " << (manifest.build.backend.empty() ? "(none)" : manifest.build.backend) << "\n";
        std::cout << "  libraries: " << manifest.artifacts.libraries.size() << "\n";
        for (const auto& library : manifest.artifacts.libraries)
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
        for (const auto& executable : manifest.artifacts.executables)
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
        std::cout << "  platforms:";
        if (manifest.platforms.empty())
        {
            std::cout << " (none)";
        }
        for (const auto& platform : manifest.platforms)
        {
            std::cout << " " << platform;
        }
        std::cout << "\n";
        std::cout << "  dependencies: " << manifest.dependencies.size() << "\n";
        for (const auto& dependency : manifest.dependencies)
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
        std::cout << "  contents: " << manifest.contents.size() << "\n";
        for (const auto& content : manifest.contents)
        {
            std::cout << "    - " << content.source << " [" << content.kind << "]";
            if (!content.target.empty())
            {
                std::cout << " -> " << content.target;
            }
            std::cout << "\n";
        }
        std::cout << "  modules: " << manifest.modules.size() << "\n";
        for (const auto& module : manifest.modules)
        {
            std::cout << "    - " << module.name << " [" << module.type << "]";
            if (!module.required.empty())
            {
                std::cout << " requires:";
                for (const auto& dep : module.required)
                {
                    std::cout << " " << dep;
                }
            }
            if (!module.optional.empty())
            {
                std::cout << " optional:";
                for (const auto& dep : module.optional)
                {
                    std::cout << " " << dep;
                }
            }
            std::cout << "\n";
        }
        std::cout << "  plugins: " << manifest.plugins.size() << "\n";
        for (const auto& plugin : manifest.plugins)
        {
            std::cout << "    - " << plugin.name;
            if (plugin.optional)
            {
                std::cout << " optional";
            }
            if (!plugin.requiredModules.empty())
            {
                std::cout << " requires:";
                for (const auto& dep : plugin.requiredModules)
                {
                    std::cout << " " << dep;
                }
            }
            if (!plugin.optionalModules.empty())
            {
                std::cout << " optional-modules:";
                for (const auto& dep : plugin.optionalModules)
                {
                    std::cout << " " << dep;
                }
            }
            std::cout << "\n";
        }
        return 0;
    }

    auto CmdValidate(const fs::path& root, const ParsedArgs& args) -> int
    {
        const auto project = LoadProjectManifest(ResolveProjectPath(args.projectPath));
        const auto& target = TargetByName(project, args.targetName);
        IssueReport report {};
        const auto resolved = ResolveTarget(root, project, target, report);
        if (!resolved.has_value() || !report.errors.empty())
        {
            PrintIssues(report, "Validation");
            return 1;
        }
        std::cout << "Validated target: " << resolved->target.name << "\n";
        std::cout << "  project: " << resolved->project.name << "\n";
        std::cout << "  packages: " << resolved->orderedPackages.size() << "\n";
        std::cout << "  required modules: " << resolved->requiredModules.size() << "\n";
        std::cout << "  optional modules: " << resolved->optionalModules.size() << "\n";
        std::cout << "  libraries: " << resolved->libraries.size() << "\n";
        std::cout << "  executables: " << resolved->executables.size() << "\n";
        std::cout << "  selected executable: " << (resolved->selectedExecutable.has_value() ? resolved->selectedExecutable->name : "(none)") << "\n";
        PrintIssues(report, "Validation");
        return 0;
    }

    auto CmdGraph(const fs::path& root, const ParsedArgs& args) -> int
    {
        const auto project = LoadProjectManifest(ResolveProjectPath(args.projectPath));
        const auto& target = TargetByName(project, args.targetName);
        IssueReport report {};
        const auto resolved = ResolveTarget(root, project, target, report);
        if (!resolved.has_value() || !report.errors.empty())
        {
            PrintIssues(report, "Graph");
            return 1;
        }
        std::cout << "Graph for target: " << resolved->target.name << "\n\nPackages:\n";
        for (const auto& package : resolved->orderedPackages)
        {
            const auto& edges = resolved->packageEdges.at(package.manifest.name);
            std::cout << "  - " << package.manifest.name << " -> ";
            if (edges.empty())
            {
                std::cout << "(none)";
            }
            else
            {
                bool first = true;
                for (const auto& dep : edges)
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
        for (const auto& [name, edges] : resolved->dependencyEdges)
        {
            std::cout << "  - " << name << " -> ";
            if (edges.empty())
            {
                std::cout << "(none)";
            }
            else
            {
                bool first = true;
                for (const auto& dep : edges)
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
        for (const auto& library : resolved->libraries)
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
        for (const auto& executable : resolved->executables)
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
            if (resolved->selectedExecutable.has_value() && resolved->selectedExecutable->name == executable.name)
            {
                std::cout << " selected";
            }
            std::cout << "\n";
        }
        PrintIssues(report, "Graph");
        return 0;
    }

    auto WriteTargetLayout(const ResolvedTarget& resolved, const fs::path& outputDir, const std::vector<std::tuple<std::string, fs::path, fs::path>>& staged) -> void
    {
        const auto manifestPath = outputDir / (resolved.target.name + ".ngintarget");
        std::ofstream out(manifestPath);
        out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
        out << "<TargetLayout SchemaVersion=\"1\" Target=\"" << EscapeXml(resolved.target.name)
            << "\" Project=\"" << EscapeXml(resolved.project.name)
            << "\" Type=\"" << EscapeXml(resolved.target.type)
            << "\" Profile=\"" << EscapeXml(resolved.target.profile)
            << "\" Platform=\"" << EscapeXml(resolved.target.platform)
            << "\">\n";
        out << "  <Runtime Environment=\"" << EscapeXml(resolved.target.environmentName)
            << "\" WorkingDirectory=\"" << EscapeXml(resolved.target.workingDirectory)
            << "\" />\n";
        if (resolved.selectedExecutable.has_value())
        {
            out << "  <SelectedExecutable Name=\"" << EscapeXml(resolved.selectedExecutable->name)
                << "\" Target=\"" << EscapeXml(resolved.selectedExecutable->target)
                << "\" Origin=\"" << EscapeXml(resolved.selectedExecutable->origin)
                << "\" />\n";
        }
        out << "  <ConfigSources>\n";
        for (const auto& source : resolved.target.configSources)
        {
            out << "    <Config Source=\"" << EscapeXml(source) << "\" />\n";
        }
        out << "  </ConfigSources>\n";
        out << "  <Packages>\n";
        for (const auto& package : resolved.orderedPackages)
        {
            out << "    <Package Name=\"" << EscapeXml(package.manifest.name) << "\" Version=\"" << EscapeXml(package.manifest.version) << "\" Source=\"catalog\">\n";
            for (const auto& content : package.manifest.contents)
            {
                const auto rel = content.target.empty() ? content.source : content.target;
                out << "      <Content Source=\"" << EscapeXml(content.source)
                    << "\" Kind=\"" << EscapeXml(content.kind)
                    << "\" Destination=\"" << EscapeXml(rel) << "\" />\n";
            }
            out << "    </Package>\n";
        }
        out << "  </Packages>\n";
        out << "  <Artifacts>\n";
        out << "    <Libraries>\n";
        for (const auto& library : resolved.libraries)
        {
            out << "      <Library Name=\"" << EscapeXml(library.name)
                << "\" Target=\"" << EscapeXml(library.target)
                << "\" Linkage=\"" << EscapeXml(library.linkage)
                << "\" Origin=\"" << EscapeXml(library.origin)
                << "\" />\n";
        }
        out << "    </Libraries>\n";
        out << "    <Executables>\n";
        for (const auto& executable : resolved.executables)
        {
            out << "      <Executable Name=\"" << EscapeXml(executable.name)
                << "\" Target=\"" << EscapeXml(executable.target)
                << "\" Origin=\"" << EscapeXml(executable.origin)
                << "\" />\n";
        }
        out << "    </Executables>\n";
        out << "  </Artifacts>\n";
        out << "  <Modules>\n";
        for (const auto& module : resolved.requiredModules)
        {
            out << "    <Module Name=\"" << EscapeXml(module) << "\" />\n";
        }
        for (const auto& module : resolved.optionalModules)
        {
            out << "    <Module Name=\"" << EscapeXml(module) << "\" Optional=\"true\" />\n";
        }
        out << "  </Modules>\n";
        out << "  <Plugins>\n";
        for (const auto& plugin : resolved.enabledPlugins)
        {
            out << "    <Plugin Name=\"" << EscapeXml(plugin) << "\" />\n";
        }
        out << "  </Plugins>\n";
        out << "  <StagedFiles>\n";
        for (const auto& [kind, source, destination] : staged)
        {
            out << "    <File Kind=\"" << EscapeXml(kind)
                << "\" Source=\"" << EscapeXml(source.string())
                << "\" Destination=\"" << EscapeXml(destination.string())
                << "\" RelativeDestination=\"" << EscapeXml(fs::relative(destination, outputDir).string()) << "\" />\n";
        }
        out << "  </StagedFiles>\n";
        out << "</TargetLayout>\n";
    }

    auto CmdBuild(const fs::path& root, const ParsedArgs& args) -> int
    {
        const auto project = LoadProjectManifest(ResolveProjectPath(args.projectPath));
        const auto& target = TargetByName(project, args.targetName);
        IssueReport report {};
        const auto resolved = ResolveTarget(root, project, target, report);
        if (!resolved.has_value() || !report.errors.empty())
        {
            PrintIssues(report, "Build");
            return 1;
        }
        const auto outputDir = args.outputPath.has_value() ? fs::absolute(*args.outputPath) : (root / ".ngin" / "build" / resolved->target.name);
        if (fs::exists(outputDir))
        {
            fs::remove_all(outputDir);
        }
        fs::create_directories(outputDir);
        std::map<fs::path, std::string> collisions;
        std::vector<std::tuple<std::string, fs::path, fs::path>> staged;

        BuildArtifacts(*resolved, outputDir, report);
        if (!report.errors.empty())
        {
            PrintIssues(report, "Build");
            return 1;
        }
        CollectBuiltArtifactFiles(outputDir, collisions, report, staged);
        if (!report.errors.empty())
        {
            PrintIssues(report, "Build");
            return 1;
        }

        for (const auto& package : resolved->orderedPackages)
        {
            for (const auto& content : package.manifest.contents)
            {
                const auto source = package.manifest.path.parent_path() / content.source;
                const auto rel = content.target.empty() ? content.source : content.target;
                const auto dest = outputDir / rel;
                if (collisions.contains(dest))
                {
                    AddError(report, "build output collision at '" + rel + "' between packages '" + collisions[dest] + "' and '" + package.manifest.name + "'");
                    continue;
                }
                collisions[dest] = package.manifest.name;
                fs::create_directories(dest.parent_path());
                fs::copy_file(source, dest, fs::copy_options::overwrite_existing);
                staged.emplace_back(content.kind, source, dest);
            }
        }
        for (const auto& config : resolved->target.configSources)
        {
            const auto source = resolved->project.path.parent_path() / config;
            if (!fs::exists(source))
            {
                AddError(report, "missing config source '" + config + "'");
                continue;
            }
            const auto dest = outputDir / config;
            if (collisions.contains(dest))
            {
                AddError(report, "build output collision at config source '" + config + "'");
                continue;
            }
            collisions[dest] = "<config>";
            fs::create_directories(dest.parent_path());
            fs::copy_file(source, dest, fs::copy_options::overwrite_existing);
            staged.emplace_back("config-source", source, dest);
        }
        if (!report.errors.empty())
        {
            PrintIssues(report, "Build");
            return 1;
        }
        WriteTargetLayout(*resolved, outputDir, staged);
        std::cout << "Built target: " << resolved->target.name << "\n";
        std::cout << "  project: " << resolved->project.name << "\n";
        std::cout << "  output: " << outputDir << "\n";
        std::cout << "  selected executable: " << (resolved->selectedExecutable.has_value() ? resolved->selectedExecutable->name : "(none)") << "\n";
        std::cout << "  staged files: " << staged.size() << "\n";
        return 0;
    }

    auto PrintHelp() -> void
    {
        std::cout
            << "usage: ngin <group> <command> [options]\n\n"
            << "Commands:\n"
            << "  workspace list\n"
            << "  workspace status [--dependencies <dir>]\n"
            << "  workspace doctor [--dependencies <dir>]\n"
            << "  workspace sync [--dependencies <dir>]\n"
            << "  project validate [--project <file.nginproj>] [--target <name>]\n"
            << "  project graph [--project <file.nginproj>] [--target <name>]\n"
            << "  project build [--project <file.nginproj>] [--target <name>] [--output <dir>]\n"
            << "  package list\n"
            << "  package show <PackageName>\n";
    }
}

auto main(int argc, char** argv) -> int
{
    try
    {
        const auto root = RootDir(argv[0]);
        if (argc < 2)
        {
            PrintHelp();
            return 0;
        }
        const std::string command = argv[1];
        if (command == "workspace")
        {
            if (argc < 3)
            {
                throw std::runtime_error("workspace requires a subcommand");
            }
            const std::string subcommand = argv[2];
            if (subcommand == "list")
            {
                return CmdList(root);
            }
            if (subcommand == "status")
            {
                return CmdStatus(root, ParseCommonArgs(argc, argv, 3));
            }
            if (subcommand == "doctor")
            {
                return CmdDoctor(root, ParseCommonArgs(argc, argv, 3));
            }
            if (subcommand == "sync")
            {
                return CmdSync(root, ParseCommonArgs(argc, argv, 3));
            }
            throw std::runtime_error("unknown workspace subcommand '" + subcommand + "'");
        }
        if (command == "project")
        {
            if (argc < 3)
            {
                throw std::runtime_error("project requires a subcommand");
            }
            const std::string subcommand = argv[2];
            if (subcommand == "validate")
            {
                return CmdValidate(root, ParseCommonArgs(argc, argv, 3));
            }
            if (subcommand == "graph")
            {
                return CmdGraph(root, ParseCommonArgs(argc, argv, 3));
            }
            if (subcommand == "build")
            {
                return CmdBuild(root, ParseCommonArgs(argc, argv, 3));
            }
            throw std::runtime_error("unknown project subcommand '" + subcommand + "'");
        }
        if (command == "package")
        {
            if (argc < 3)
            {
                throw std::runtime_error("package requires a subcommand");
            }
            const std::string subcommand = argv[2];
            if (subcommand == "list")
            {
                return CmdPackageList(root);
            }
            if (subcommand == "show")
            {
                return CmdPackageShow(root, ParseCommonArgs(argc, argv, 3));
            }
            throw std::runtime_error("unknown package subcommand '" + subcommand + "'");
        }
        if (command == "list")
        {
            return CmdList(root);
        }
        if (command == "status")
        {
            return CmdStatus(root, ParseCommonArgs(argc, argv, 2));
        }
        if (command == "doctor")
        {
            return CmdDoctor(root, ParseCommonArgs(argc, argv, 2));
        }
        if (command == "sync")
        {
            return CmdSync(root, ParseCommonArgs(argc, argv, 2));
        }
        if (command == "validate")
        {
            return CmdValidate(root, ParseCommonArgs(argc, argv, 2));
        }
        if (command == "graph")
        {
            return CmdGraph(root, ParseCommonArgs(argc, argv, 2));
        }
        if (command == "build")
        {
            return CmdBuild(root, ParseCommonArgs(argc, argv, 2));
        }

        throw std::runtime_error("unknown command '" + command + "'");
    }
    catch (const std::exception& ex)
    {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
