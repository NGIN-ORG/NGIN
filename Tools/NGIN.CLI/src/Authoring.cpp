#include "Authoring.hpp"

#include "Support.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <set>
#include <sstream>
#include <unordered_map>

namespace NGIN::CLI
{
    namespace
    {
        [[nodiscard]] auto SchemaVersion(const XmlElement &node, const fs::path &path) -> std::string
        {
            return RequireAttribute(node, "SchemaVersion", path);
        }

        auto ValidateSchemaVersion(const XmlElement &node, const fs::path &path, const std::string_view expected = "2") -> void
        {
            const auto schemaVersion = SchemaVersion(node, path);
            if (schemaVersion != expected)
            {
                throw std::runtime_error(path.string() + ": unsupported SchemaVersion '" + schemaVersion + "' (expected '" + std::string(expected) + "')");
            }
        }

        auto ValidateProjectSchemaVersion(const XmlElement &node, const fs::path &path) -> std::string
        {
            const auto schemaVersion = SchemaVersion(node, path);
            if (schemaVersion != "3")
            {
                throw std::runtime_error(path.string() + ": unsupported project SchemaVersion '" + schemaVersion + "' (expected '3')");
            }
            return schemaVersion;
        }

        auto ValidateLocalSettingsSchemaVersion(const XmlElement &node, const fs::path &path) -> void
        {
            const auto schemaVersion = RequireAttribute(node, "SchemaVersion", path);
            if (schemaVersion != "1")
            {
                throw std::runtime_error(path.string() + ": unsupported local settings SchemaVersion '" + schemaVersion + "' (expected '1')");
            }
        }

        [[nodiscard]] auto IsValidStartupStage(const std::string_view value) -> bool
        {
            return value == "Foundation" || value == "Platform" || value == "Services" || value == "Features" || value == "Presentation";
        }

        [[nodiscard]] auto IsValidModuleFamily(const std::string_view value) -> bool
        {
            return value == "Base" || value == "Reflection" || value == "Core" || value == "Platform" || value == "Editor" || value == "Domain" || value == "App";
        }

        [[nodiscard]] auto IsSupportedBuildVisibility(const std::string_view value) -> bool
        {
            return value == "Private" || value == "Public" || value == "Interface";
        }

        [[nodiscard]] auto IsSupportedPackageBuildMode(const std::string_view value) -> bool
        {
            return value == "Manual" || value == "FindPackage" || value == "AddSubdirectory";
        }

        [[nodiscard]] auto IsSelectorAttribute(const std::string_view name) -> bool
        {
            return name == "Profile" || name == "Platform" || name == "OperatingSystem" || name == "Architecture"
                   || name == "BuildType" || name == "Environment";
        }

        [[nodiscard]] auto IsValidManifestIdentifier(const std::string_view value) -> bool
        {
            if (value.empty())
            {
                return false;
            }
            const auto first = static_cast<unsigned char>(value.front());
            if (!std::isalpha(first) && value.front() != '_')
            {
                return false;
            }
            return std::all_of(
                value.begin() + 1,
                value.end(),
                [](const char ch)
                {
                    const auto value = static_cast<unsigned char>(ch);
                    return std::isalnum(value) || ch == '_' || ch == '.' || ch == '-';
                });
        }

        [[nodiscard]] auto HasSelectorAttributes(const XmlElement &node) -> bool
        {
            for (NGIN::UIntSize index = 0; index < node.attributes.Size(); ++index)
            {
                if (IsSelectorAttribute(node.attributes[index].name))
                {
                    return true;
                }
            }
            return false;
        }

        auto ValidateAllowedAttributes(const XmlElement &node, const fs::path &path, const std::vector<std::string_view> &allowed) -> void
        {
            for (NGIN::UIntSize index = 0; index < node.attributes.Size(); ++index)
            {
                const auto name = node.attributes[index].name;
                const auto isAllowed = std::any_of(
                    allowed.begin(),
                    allowed.end(),
                    [name](const std::string_view allowedName)
                    { return name == allowedName; });
                if (!isAllowed)
                {
                    throw std::runtime_error(path.string() + ": unsupported attribute '" + std::string(name) + "' on <" + std::string(node.name) + ">");
                }
            }
        }

        [[nodiscard]] auto ParseSelectors(const XmlElement &node, const fs::path &path) -> SelectorSet
        {
            SelectorSet selectors{};
            const auto profileSelector = Attribute(node, "Profile").value_or("");
            if (!profileSelector.empty())
            {
                if (!IsValidManifestIdentifier(profileSelector))
                {
                    throw std::runtime_error(path.string() + ": invalid profile selector '" + profileSelector + "'");
                }
                selectors.profile = profileSelector;
            }
            if (const auto value = Attribute(node, "Platform"); value.has_value() && !value->empty())
            {
                if (!IsValidManifestIdentifier(*value))
                {
                    throw std::runtime_error(path.string() + ": invalid platform selector '" + *value + "'");
                }
                selectors.platform = *value;
            }
            if (const auto value = Attribute(node, "OperatingSystem"); value.has_value() && !value->empty())
            {
                if (!IsValidOperatingSystem(*value))
                {
                    throw std::runtime_error(path.string() + ": unknown operating system '" + *value + "'");
                }
                selectors.operatingSystem = *value;
            }
            if (const auto value = Attribute(node, "Architecture"); value.has_value() && !value->empty())
            {
                if (!IsValidArchitecture(*value))
                {
                    throw std::runtime_error(path.string() + ": unknown architecture '" + *value + "'");
                }
                selectors.architecture = *value;
            }
            const auto buildTypeSelector = Attribute(node, "BuildType").value_or("");
            if (!buildTypeSelector.empty())
            {
                if (!IsSupportedBuildType(buildTypeSelector))
                {
                    throw std::runtime_error(path.string() + ": unknown build type '" + buildTypeSelector + "'");
                }
                selectors.buildType = buildTypeSelector;
            }
            if (const auto value = Attribute(node, "Environment"); value.has_value() && !value->empty())
            {
                if (!IsValidManifestIdentifier(*value))
                {
                    throw std::runtime_error(path.string() + ": invalid environment selector '" + *value + "'");
                }
                selectors.environment = *value;
            }
            return selectors;
        }

        auto AddWhenSelector(const XmlElement &node, const fs::path &path, SelectorSet &selectors) -> void
        {
            const auto condition = Attribute(node, "Condition").value_or("");
            if (!condition.empty())
            {
                if (!IsValidManifestIdentifier(condition))
                {
                    throw std::runtime_error(path.string() + ": invalid condition name '" + condition + "'");
                }
                selectors.conditionRefs.push_back(condition);
            }
        }

        auto MergeStringSelector(
            std::optional<std::string> &target,
            const std::optional<std::string> &source,
            bool &impossible) -> void
        {
            if (!source.has_value())
            {
                return;
            }
            if (!target.has_value())
            {
                target = source;
                return;
            }
            if (*target != *source)
            {
                impossible = true;
            }
        }

        [[nodiscard]] auto MergeSelectors(SelectorSet left, const SelectorSet &right) -> SelectorSet
        {
            left.impossible = left.impossible || right.impossible;
            MergeStringSelector(left.profile, right.profile, left.impossible);
            MergeStringSelector(left.platform, right.platform, left.impossible);
            MergeStringSelector(left.operatingSystem, right.operatingSystem, left.impossible);
            MergeStringSelector(left.architecture, right.architecture, left.impossible);
            MergeStringSelector(left.buildType, right.buildType, left.impossible);
            MergeStringSelector(left.environment, right.environment, left.impossible);
            left.conditionRefs.insert(left.conditionRefs.end(), right.conditionRefs.begin(), right.conditionRefs.end());
            return left;
        }

        [[nodiscard]] auto ParseSelection(const XmlElement &node, const fs::path &path) -> SelectorSet
        {
            auto selectors = ParseSelectors(node, path);
            AddWhenSelector(node, path, selectors);
            return selectors;
        }

        [[nodiscard]] auto Trim(std::string value) -> std::string
        {
            auto first = value.begin();
            while (first != value.end() && std::isspace(static_cast<unsigned char>(*first)))
            {
                ++first;
            }
            auto last = value.end();
            while (last != first && std::isspace(static_cast<unsigned char>(*(last - 1))))
            {
                --last;
            }
            return std::string(first, last);
        }

        [[nodiscard]] auto SplitPathList(std::string_view text) -> std::vector<std::string>
        {
            std::vector<std::string> entries{};
            std::string current{};
            auto flush = [&]()
            {
                auto value = Trim(current);
                if (!value.empty())
                {
                    entries.push_back(std::move(value));
                }
                current.clear();
            };

            for (const char ch : text)
            {
                if (ch == '\n' || ch == '\r' || ch == ';' || ch == ',')
                {
                    flush();
                    continue;
                }
                current.push_back(ch);
            }
            flush();
            return entries;
        }

        [[nodiscard]] auto OptionalPathListAttribute(const XmlElement &node, const std::string_view key) -> std::vector<std::string>
        {
            if (const auto value = Attribute(node, key); value.has_value())
            {
                return SplitPathList(*value);
            }
            return {};
        }

        [[nodiscard]] auto TextContent(const XmlElement &node) -> std::string
        {
            std::string text{};
            for (NGIN::UIntSize index = 0; index < node.children.Size(); ++index)
            {
                const auto &child = node.children[index];
                if (child.type == XmlNode::Type::Text || child.type == XmlNode::Type::CData)
                {
                    text.append(child.text.data(), child.text.size());
                }
            }
            return text;
        }

        [[nodiscard]] auto ParseCompatibility(const XmlElement &node, const fs::path &path) -> CompatibilityDefinition
        {
            CompatibilityDefinition compatibility{};
            if (FindChild(node, "Platforms") != nullptr)
            {
                throw std::runtime_error(path.string() + ": legacy <Platforms> is no longer supported; use <Compatibility>");
            }
            if (FindChild(node, "SupportedHosts") != nullptr)
            {
                throw std::runtime_error(path.string() + ": legacy <SupportedHosts> is no longer supported");
            }
            if (const auto *section = FindChild(node, "Compatibility"))
            {
                if (const auto *operatingSystems = FindChild(*section, "OperatingSystems"))
                {
                    for (const auto *entry : ChildElements(*operatingSystems, "OperatingSystem"))
                    {
                        const auto value = RequireAttribute(*entry, "Name", path);
                        if (!IsValidOperatingSystem(value))
                        {
                            throw std::runtime_error(path.string() + ": unknown operating system '" + value + "'");
                        }
                        compatibility.operatingSystems.push_back(value);
                    }
                }
                if (const auto *architectures = FindChild(*section, "Architectures"))
                {
                    for (const auto *entry : ChildElements(*architectures, "Architecture"))
                    {
                        const auto value = RequireAttribute(*entry, "Name", path);
                        if (!IsValidArchitecture(value))
                        {
                            throw std::runtime_error(path.string() + ": unknown architecture '" + value + "'");
                        }
                        compatibility.architectures.push_back(value);
                    }
                }
            }
            return compatibility;
        }

        auto ParseConfigInputs(const XmlElement &parent, const fs::path &path, std::vector<std::string> &out) -> void
        {
            if (const auto *inputs = FindChild(parent, "Inputs"))
            {
                for (const auto *config : ChildElements(*inputs, "Config"))
                {
                    const auto source = Attribute(*config, "Path").value_or(Attribute(*config, "Pattern").value_or(""));
                    if (source.empty())
                    {
                        throw std::runtime_error(path.string() + ": <Inputs><Config> requires Path or Pattern");
                    }
                    out.push_back(source);
                }
            }
        }

        auto ParseReferences(
            const XmlElement &referencesElement,
            const fs::path &baseDirectory,
            const fs::path &path,
            std::vector<ProjectReference> &projectRefs,
            std::vector<PackageReference> &packageRefs) -> void
        {
            for (const auto *node : ChildElements(referencesElement, "Project"))
            {
                ProjectReference reference{};
                reference.path = (baseDirectory / RequireAttribute(*node, "Path", path)).lexically_normal();
                const auto profile = Attribute(*node, "Profile").value_or("");
                if (!profile.empty())
                {
                    reference.profile = profile;
                }
                projectRefs.push_back(std::move(reference));
            }
            for (const auto *node : ChildElements(referencesElement, "Package"))
            {
                PackageReference packageReference{};
                packageReference.name = RequireAttribute(*node, "Name", path);
                packageReference.versionRange = Attribute(*node, "Version").value_or(Attribute(*node, "VersionRange").value_or(""));
                packageReference.optional = BoolAttribute(*node, "Optional");
                packageRefs.push_back(std::move(packageReference));
            }
        }

        auto ParseLocalSettingsImports(const XmlElement &parent, const fs::path &path, std::vector<LocalSettingsImport> &out) -> void
        {
            if (const auto *localSettings = FindChild(parent, "LocalSettings"))
            {
                for (const auto *item : ChildElements(*localSettings, "Import"))
                {
                    LocalSettingsImport import{};
                    import.path = RequireAttribute(*item, "Path", path);
                    import.optional = BoolAttribute(*item, "Optional");
                    out.push_back(std::move(import));
                }
            }
        }

        auto ParseContents(const XmlElement &parent, const fs::path &path, std::vector<ContentFile> &out) -> void
        {
            if (const auto *contents = FindChild(parent, "Contents"))
            {
                for (const auto *node : ChildElements(*contents, "File"))
                {
                    ContentFile content{};
                    content.source = RequireAttribute(*node, "Source", path);
                    content.kind = Attribute(*node, "Kind").value_or("other");
                    content.target = Attribute(*node, "Target").value_or("");
                    out.push_back(std::move(content));
                }
            }
        }

        auto ParseVariables(const XmlElement &parent, const fs::path &path, std::vector<EnvironmentVariable> &out) -> void
        {
            if (const auto *variables = FindChild(parent, "Variables"))
            {
                for (const auto *node : ChildElements(*variables, "Variable"))
                {
                    EnvironmentVariable variable{};
                    variable.name = RequireAttribute(*node, "Name", path);
                    variable.value = Attribute(*node, "Value").value_or("");
                    variable.fromEnvironment = Attribute(*node, "FromEnvironment").value_or("");
                    variable.fromLocalSetting = Attribute(*node, "FromLocalSetting").value_or("");
                    variable.required = BoolAttribute(*node, "Required");
                    variable.secret = BoolAttribute(*node, "Secret");

                    const auto sourceCount = static_cast<int>(Attribute(*node, "Value").has_value())
                                             + static_cast<int>(Attribute(*node, "FromEnvironment").has_value())
                                             + static_cast<int>(Attribute(*node, "FromLocalSetting").has_value());
                    if (sourceCount != 1)
                    {
                        throw std::runtime_error(path.string() + ": variable '" + variable.name + "' must declare exactly one of Value, FromEnvironment, or FromLocalSetting");
                    }
                    if (Attribute(*node, "FromEnvironment").has_value() && variable.fromEnvironment.empty())
                    {
                        throw std::runtime_error(path.string() + ": variable '" + variable.name + "' has empty FromEnvironment");
                    }
                    if (Attribute(*node, "FromLocalSetting").has_value() && variable.fromLocalSetting.empty())
                    {
                        throw std::runtime_error(path.string() + ": variable '" + variable.name + "' has empty FromLocalSetting");
                    }
                    if (variable.secret && Attribute(*node, "Value").has_value())
                    {
                        throw std::runtime_error(path.string() + ": variable '" + variable.name + "' may not combine Secret=\"true\" with a literal Value in a project manifest");
                    }
                    out.push_back(std::move(variable));
                }
            }
        }

        auto ParseFeatures(const XmlElement &parent, const fs::path &path, std::vector<FeatureFlag> &out) -> void
        {
            if (const auto *features = FindChild(parent, "Features"))
            {
                for (const auto *node : ChildElements(*features, "Feature"))
                {
                    FeatureFlag feature{};
                    feature.name = RequireAttribute(*node, "Name", path);
                    feature.enabled = !Attribute(*node, "Enabled").has_value() || BoolAttribute(*node, "Enabled", true);
                    out.push_back(std::move(feature));
                }
            }
        }

        [[nodiscard]] auto ParseModuleDefinition(const XmlElement &node, const fs::path &path) -> ModuleDescriptor;

        auto ParseRuntimeDefinition(const XmlElement &runtime, const fs::path &path, RuntimeDefinition &target, const bool allowModules) -> void
        {
            if (allowModules)
            {
                if (const auto *modules = FindChild(runtime, "Modules"))
                {
                    for (const auto *node : ChildElements(*modules, "Module"))
                    {
                        target.modules.push_back(ParseModuleDefinition(*node, path));
                    }
                }
            }
            if (const auto *enableModules = FindChild(runtime, "EnableModules"))
            {
                for (const auto *node : ChildElements(*enableModules, "ModuleRef"))
                {
                    target.enableModules.push_back(RequireAttribute(*node, "Name", path));
                }
            }
            if (const auto *disableModules = FindChild(runtime, "DisableModules"))
            {
                for (const auto *node : ChildElements(*disableModules, "ModuleRef"))
                {
                    target.disableModules.push_back(RequireAttribute(*node, "Name", path));
                }
            }
            if (const auto *enablePlugins = FindChild(runtime, "EnablePlugins"))
            {
                for (const auto *node : ChildElements(*enablePlugins, "PluginRef"))
                {
                    target.enablePlugins.push_back(RequireAttribute(*node, "Name", path));
                }
            }
            if (const auto *disablePlugins = FindChild(runtime, "DisablePlugins"))
            {
                for (const auto *node : ChildElements(*disablePlugins, "PluginRef"))
                {
                    target.disablePlugins.push_back(RequireAttribute(*node, "Name", path));
                }
            }
        }

        [[nodiscard]] auto ResolveStartupStage(const XmlElement &node, std::string_view defaultStage) -> std::string
        {
            if (const auto startupStage = Attribute(node, "StartupStage"); startupStage.has_value() && !startupStage->empty())
            {
                return *startupStage;
            }
            return std::string(defaultStage);
        }

        auto ValidateModuleDescriptor(const ModuleDescriptor &module, const fs::path &path) -> void
        {
            if (!IsValidModuleFamily(module.family))
            {
                throw std::runtime_error(path.string() + ": unknown module family '" + module.family + "'");
            }
            if (!IsValidStartupStage(module.startupStage))
            {
                throw std::runtime_error(path.string() + ": unknown startup stage '" + module.startupStage + "'");
            }
        }

        [[nodiscard]] auto IsValidPackageBootstrapMode(const std::string_view value) -> bool
        {
            return value == "BuilderHookV1";
        }

        [[nodiscard]] auto DiscoverPackageSourceRoots(const fs::path &start) -> std::vector<fs::path>
        {
            std::vector<fs::path> roots;
            std::set<fs::path> unique;
            auto current = fs::weakly_canonical(fs::is_regular_file(start) ? start.parent_path() : start);
            while (true)
            {
                const auto candidate = current / "Packages";
                if (fs::exists(candidate) && fs::is_directory(candidate))
                {
                    const auto normalized = candidate.lexically_normal();
                    if (unique.insert(normalized).second)
                    {
                        roots.push_back(normalized);
                    }
                }
                if (current == current.parent_path())
                {
                    break;
                }
                current = current.parent_path();
            }
            return roots;
        }

        [[nodiscard]] auto ParseModuleDefinition(const XmlElement &node, const fs::path &path) -> ModuleDescriptor
        {
            ModuleDescriptor module{};
            module.name = RequireAttribute(node, "Name", path);
            module.family = Attribute(node, "Family").value_or("App");
            module.type = Attribute(node, "Type").value_or("Runtime");
            module.startupStage = ResolveStartupStage(node, "Features");
            module.version = Attribute(node, "Version").value_or("");
            module.compatiblePlatformRange = Attribute(node, "CompatiblePlatformRange").value_or("");
            module.requiresReflection = BoolAttribute(node, "ReflectionRequired");
            ValidateModuleDescriptor(module, path);
            module.compatibility = ParseCompatibility(node, path);

            if (const auto *dependencies = FindChild(node, "Dependencies"))
            {
                for (const auto *dep : ChildElements(*dependencies, "Dependency"))
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

            if (const auto *providesServices = FindChild(node, "ProvidesServices"))
            {
                for (const auto *service : ChildElements(*providesServices, "Service"))
                {
                    module.providesServices.push_back(RequireAttribute(*service, "Name", path));
                }
            }

            if (const auto *requiresServices = FindChild(node, "RequiresServices"))
            {
                for (const auto *service : ChildElements(*requiresServices, "Service"))
                {
                    module.requiresServices.push_back(RequireAttribute(*service, "Name", path));
                }
            }

            if (const auto *capabilities = FindChild(node, "Capabilities"))
            {
                for (const auto *capability : ChildElements(*capabilities, "Capability"))
                {
                    module.capabilities.push_back(RequireAttribute(*capability, "Name", path));
                }
            }

            return module;
        }

        [[nodiscard]] auto ParseBuildSetting(const XmlElement &node, const fs::path &path, std::string_view valueAttribute, const SelectorSet &inheritedSelection = {}) -> BuildSetting
        {
            BuildSetting setting{};
            setting.value = RequireAttribute(node, valueAttribute, path);
            setting.visibility = Attribute(node, "Visibility").value_or("Private");
            if (!IsSupportedBuildVisibility(setting.visibility))
            {
                throw std::runtime_error(path.string() + ": unknown build visibility '" + setting.visibility + "'");
            }
            setting.selectors = MergeSelectors(inheritedSelection, ParseSelection(node, path));
            return setting;
        }

        [[nodiscard]] auto ParseSourceEntry(const XmlElement &node, const fs::path &path, const SelectorSet &inheritedSelection = {}) -> SourceEntry
        {
            SourceEntry entry{};
            entry.path = RequireAttribute(node, "Path", path);
            entry.selectors = MergeSelectors(inheritedSelection, ParseSelection(node, path));
            entry.includePatterns = OptionalPathListAttribute(node, "Include");
            entry.excludePatterns = OptionalPathListAttribute(node, "Exclude");
            return entry;
        }

        [[nodiscard]] auto ParseSourceFiles(const XmlElement &node, const fs::path &path, const SelectorSet &inheritedSelection = {}) -> std::vector<SourceEntry>
        {
            if (!ChildElements(node).empty())
            {
                throw std::runtime_error(path.string() + ": <Files> may contain only a line-separated file list");
            }

            std::vector<SourceEntry> entries{};
            const auto selectors = MergeSelectors(inheritedSelection, ParseSelection(node, path));
            for (const auto &filePath : SplitPathList(TextContent(node)))
            {
                SourceEntry entry{};
                entry.path = filePath;
                entry.selectors = selectors;
                entries.push_back(std::move(entry));
            }
            if (entries.empty())
            {
                throw std::runtime_error(path.string() + ": <Files> must contain at least one file path");
            }
            return entries;
        }

        auto ParseSourceGroup(const XmlElement &group, const fs::path &path, SourceGroup &out) -> void
        {
            const auto inheritedSelection = ParseSelection(group, path);
            for (const auto *child : ChildElements(group))
            {
                if (child->name == "Root")
                {
                    out.roots.push_back(ParseSourceEntry(*child, path, inheritedSelection));
                    continue;
                }
                if (child->name == "File")
                {
                    out.files.push_back(ParseSourceEntry(*child, path, inheritedSelection));
                    continue;
                }
                if (child->name == "Files")
                {
                    auto entries = ParseSourceFiles(*child, path, inheritedSelection);
                    out.files.insert(out.files.end(), entries.begin(), entries.end());
                    continue;
                }
                throw std::runtime_error(path.string() + ": unsupported <Sources><" + std::string(group.name) + "> child <" + std::string(child->name) + ">");
            }
        }

        [[nodiscard]] auto ParseProjectSources(const XmlElement &sources, const fs::path &path) -> ProjectSources
        {
            ProjectSources parsed{};
            for (const auto *child : ChildElements(sources))
            {
                if (child->name == "Public")
                {
                    ParseSourceGroup(*child, path, parsed.publicSources);
                    continue;
                }
                if (child->name == "Private")
                {
                    ParseSourceGroup(*child, path, parsed.privateSources);
                    continue;
                }
                throw std::runtime_error(path.string() + ": unsupported <Sources> child <" + std::string(child->name) + ">");
            }
            return parsed;
        }

        [[nodiscard]] auto IsConditionNodeName(const std::string_view name) -> bool
        {
            return name == "Match" || name == "All" || name == "Any" || name == "Not" || name == "ConditionRef";
        }

        [[nodiscard]] auto ParseConditionNode(const XmlElement &node, const fs::path &path) -> ConditionNode
        {
            if (!Trim(TextContent(node)).empty())
            {
                throw std::runtime_error(path.string() + ": <" + std::string(node.name) + "> condition nodes may not contain text");
            }

            ConditionNode parsed{};
            if (node.name == "Match")
            {
                ValidateAllowedAttributes(node, path, {"Profile", "Platform", "OperatingSystem", "Architecture", "BuildType", "Environment"});
                if (!HasSelectorAttributes(node))
                {
                    throw std::runtime_error(path.string() + ": <Match> must declare at least one selector attribute");
                }
                parsed.kind = ConditionNode::Kind::Match;
                parsed.match = ParseSelectors(node, path);
                return parsed;
            }
            if (node.name == "ConditionRef")
            {
                ValidateAllowedAttributes(node, path, {"Name"});
                parsed.kind = ConditionNode::Kind::ConditionRef;
                parsed.conditionName = RequireAttribute(node, "Name", path);
                if (!IsValidManifestIdentifier(parsed.conditionName))
                {
                    throw std::runtime_error(path.string() + ": invalid condition reference '" + parsed.conditionName + "'");
                }
                return parsed;
            }
            if (node.name == "All" || node.name == "Any" || node.name == "Not")
            {
                ValidateAllowedAttributes(node, path, {});
                parsed.kind = node.name == "All" ? ConditionNode::Kind::All : node.name == "Any" ? ConditionNode::Kind::Any : ConditionNode::Kind::Not;
                for (const auto *child : ChildElements(node))
                {
                    if (!IsConditionNodeName(child->name))
                    {
                        throw std::runtime_error(path.string() + ": unsupported condition child <" + std::string(child->name) + ">");
                    }
                    parsed.children.push_back(ParseConditionNode(*child, path));
                }
                if ((parsed.kind == ConditionNode::Kind::All || parsed.kind == ConditionNode::Kind::Any) && parsed.children.empty())
                {
                    throw std::runtime_error(path.string() + ": <" + std::string(node.name) + "> must contain at least one condition node");
                }
                if (parsed.kind == ConditionNode::Kind::Not && parsed.children.size() != 1)
                {
                    throw std::runtime_error(path.string() + ": <Not> must contain exactly one condition node");
                }
                return parsed;
            }
            throw std::runtime_error(path.string() + ": unsupported condition node <" + std::string(node.name) + ">");
        }

        [[nodiscard]] auto ParseConditionDefinition(const XmlElement &node, const fs::path &path) -> ConditionDefinition
        {
            ConditionDefinition condition{};
            condition.name = RequireAttribute(node, "Name", path);
            if (!IsValidManifestIdentifier(condition.name))
            {
                throw std::runtime_error(path.string() + ": invalid condition name '" + condition.name + "'");
            }

            std::vector<std::string_view> allowedAttributes{"Name", "Profile", "Platform", "OperatingSystem", "Architecture", "BuildType", "Environment"};
            ValidateAllowedAttributes(node, path, allowedAttributes);

            const auto hasSelectors = HasSelectorAttributes(node);
            const auto children = ChildElements(node);
            if (hasSelectors && !children.empty())
            {
                throw std::runtime_error(path.string() + ": condition '" + condition.name + "' may not mix selector attributes with child condition nodes");
            }
            if (!hasSelectors && children.empty())
            {
                throw std::runtime_error(path.string() + ": condition '" + condition.name + "' must define exactly one body");
            }
            if (children.size() > 1)
            {
                throw std::runtime_error(path.string() + ": condition '" + condition.name + "' must define exactly one body");
            }
            if (!Trim(TextContent(node)).empty())
            {
                throw std::runtime_error(path.string() + ": <Condition> may not contain text");
            }

            if (hasSelectors)
            {
                condition.body.kind = ConditionNode::Kind::Match;
                condition.body.match = ParseSelectors(node, path);
                return condition;
            }

            if (!IsConditionNodeName(children.front()->name))
            {
                throw std::runtime_error(path.string() + ": unsupported <Condition> child <" + std::string(children.front()->name) + ">");
            }
            condition.body = ParseConditionNode(*children.front(), path);
            return condition;
        }

        [[nodiscard]] auto ParseConditions(const XmlElement &root, const fs::path &path) -> std::vector<ConditionDefinition>
        {
            const auto conditionSections = ChildElements(root, "Conditions");
            if (conditionSections.size() > 1)
            {
                throw std::runtime_error(path.string() + ": project may declare at most one <Conditions> section");
            }
            if (conditionSections.empty())
            {
                return {};
            }

            std::vector<ConditionDefinition> conditions{};
            std::set<std::string> seen{};
            std::set<std::string> seenLower{};
            for (const auto *child : ChildElements(*conditionSections.front()))
            {
                if (child->name != "Condition")
                {
                    throw std::runtime_error(path.string() + ": unsupported <Conditions> child <" + std::string(child->name) + ">");
                }
                auto condition = ParseConditionDefinition(*child, path);
                if (!seen.insert(condition.name).second)
                {
                    throw std::runtime_error(path.string() + ": duplicate condition name '" + condition.name + "'");
                }
                if (!seenLower.insert(Lower(condition.name)).second)
                {
                    throw std::runtime_error(path.string() + ": condition names may not differ only by case");
                }
                conditions.push_back(std::move(condition));
            }
            return conditions;
        }

        auto LoadProjectBuildDescriptor(ProjectBuildDescriptor &build, const XmlElement *buildElement, const fs::path &path) -> void
        {
            if (buildElement == nullptr)
            {
                return;
            }

            if (const auto backend = Attribute(*buildElement, "Backend"); backend.has_value() && !backend->empty())
            {
                build.backend = *backend;
            }
            if (const auto mode = Attribute(*buildElement, "Mode"); mode.has_value() && !mode->empty())
            {
                build.mode = *mode;
            }
            if (!IsSupportedProjectBuildMode(build.mode))
            {
                throw std::runtime_error(path.string() + ": unknown project build mode '" + build.mode + "'");
            }
            if (const auto language = Attribute(*buildElement, "Language"); language.has_value() && !language->empty())
            {
                build.language = *language;
            }
            if (const auto languageStandard = Attribute(*buildElement, "LanguageStandard"); languageStandard.has_value() && !languageStandard->empty())
            {
                build.languageStandard = *languageStandard;
            }
            if (const auto *metaGen = FindChild(*buildElement, "MetaGen"))
            {
                build.metaGenEnabled = BoolAttribute(*metaGen, "Enabled");
            }

            if (const auto *sources = FindChild(*buildElement, "Sources"))
            {
                for (const auto *item : ChildElements(*sources, "Source"))
                {
                    build.sources.push_back(RequireAttribute(*item, "Path", path));
                }
            }

            if (const auto *includeDirectories = FindChild(*buildElement, "IncludeDirectories"))
            {
                const auto inheritedSelection = ParseSelection(*includeDirectories, path);
                for (const auto *item : ChildElements(*includeDirectories, "IncludeDirectory"))
                {
                    build.includeDirectories.push_back(ParseBuildSetting(*item, path, "Path", inheritedSelection));
                }
            }

            if (const auto *compileDefinitions = FindChild(*buildElement, "CompileDefinitions"))
            {
                const auto inheritedSelection = ParseSelection(*compileDefinitions, path);
                for (const auto *item : ChildElements(*compileDefinitions, "Definition"))
                {
                    build.compileDefinitions.push_back(ParseBuildSetting(*item, path, "Value", inheritedSelection));
                }
            }

            if (const auto *compileOptions = FindChild(*buildElement, "CompileOptions"))
            {
                const auto inheritedSelection = ParseSelection(*compileOptions, path);
                for (const auto *item : ChildElements(*compileOptions, "Option"))
                {
                    build.compileOptions.push_back(ParseBuildSetting(*item, path, "Value", inheritedSelection));
                }
            }

            if (const auto *linkOptions = FindChild(*buildElement, "LinkOptions"))
            {
                const auto inheritedSelection = ParseSelection(*linkOptions, path);
                for (const auto *item : ChildElements(*linkOptions, "Option"))
                {
                    build.linkOptions.push_back(ParseBuildSetting(*item, path, "Value", inheritedSelection));
                }
            }
        }

        auto LoadPackageBuildDescriptor(PackageBuildDescriptor &build, const XmlElement *buildElement, const fs::path &path) -> void
        {
            if (buildElement == nullptr)
            {
                return;
            }
            build.backend = Attribute(*buildElement, "Backend").value_or("");
            build.mode = Attribute(*buildElement, "Mode").value_or("");
            if (!build.mode.empty() && !IsSupportedPackageBuildMode(build.mode))
            {
                throw std::runtime_error(path.string() + ": unknown package build mode '" + build.mode + "'");
            }
            if (const auto *options = FindChild(*buildElement, "Options"))
            {
                for (const auto *item : ChildElements(*options, "Option"))
                {
                    BuildVariable variable{};
                    variable.name = RequireAttribute(*item, "Name", path);
                    variable.value = RequireAttribute(*item, "Value", path);
                    build.options.push_back(std::move(variable));
                }
            }
        }

        [[nodiscard]] auto ConditionMap(const ProjectManifest &project) -> std::unordered_map<std::string, const ConditionDefinition *>
        {
            std::unordered_map<std::string, const ConditionDefinition *> conditions{};
            for (const auto &condition : project.conditions)
            {
                conditions.emplace(condition.name, &condition);
            }
            return conditions;
        }

        auto CollectConditionRefs(const ConditionNode &node, std::vector<std::string> &refs) -> void
        {
            if (node.kind == ConditionNode::Kind::ConditionRef)
            {
                refs.push_back(node.conditionName);
            }
            for (const auto &child : node.children)
            {
                CollectConditionRefs(child, refs);
            }
        }

        auto ValidateSelectionConditionRefs(
            const SelectorSet &selectors,
            const std::unordered_map<std::string, const ConditionDefinition *> &conditions,
            const fs::path &path) -> void
        {
            for (const auto &conditionName : selectors.conditionRefs)
            {
                if (!conditions.contains(conditionName))
                {
                    throw std::runtime_error(path.string() + ": unknown condition '" + conditionName + "'");
                }
            }
        }

        auto ValidateSourceGroupConditionRefs(
            const SourceGroup &group,
            const std::unordered_map<std::string, const ConditionDefinition *> &conditions,
            const fs::path &path) -> void
        {
            for (const auto &root : group.roots)
            {
                ValidateSelectionConditionRefs(root.selectors, conditions, path);
            }
            for (const auto &file : group.files)
            {
                ValidateSelectionConditionRefs(file.selectors, conditions, path);
            }
        }

        auto ValidateBuildSettingConditionRefs(
            const std::vector<BuildSetting> &settings,
            const std::unordered_map<std::string, const ConditionDefinition *> &conditions,
            const fs::path &path) -> void
        {
            for (const auto &setting : settings)
            {
                ValidateSelectionConditionRefs(setting.selectors, conditions, path);
            }
        }

        auto ValidateConditionReferences(const ProjectManifest &project, const fs::path &path) -> void
        {
            const auto conditions = ConditionMap(project);
            for (const auto &condition : project.conditions)
            {
                std::vector<std::string> refs{};
                CollectConditionRefs(condition.body, refs);
                for (const auto &ref : refs)
                {
                    if (!conditions.contains(ref))
                    {
                        throw std::runtime_error(path.string() + ": condition '" + condition.name + "' references unknown condition '" + ref + "'");
                    }
                }
            }

            enum class VisitState
            {
                Visiting,
                Visited
            };
            std::unordered_map<std::string, VisitState> state{};
            std::function<void(const std::string &)> visit = [&](const std::string &name)
            {
                if (const auto it = state.find(name); it != state.end())
                {
                    if (it->second == VisitState::Visiting)
                    {
                        throw std::runtime_error(path.string() + ": condition reference cycle involving '" + name + "'");
                    }
                    return;
                }
                state.emplace(name, VisitState::Visiting);
                std::vector<std::string> refs{};
                CollectConditionRefs(conditions.at(name)->body, refs);
                for (const auto &ref : refs)
                {
                    visit(ref);
                }
                state[name] = VisitState::Visited;
            };
            for (const auto &condition : project.conditions)
            {
                visit(condition.name);
            }

            if (project.sources.has_value())
            {
                ValidateSourceGroupConditionRefs(project.sources->publicSources, conditions, path);
                ValidateSourceGroupConditionRefs(project.sources->privateSources, conditions, path);
            }
            ValidateBuildSettingConditionRefs(project.build.includeDirectories, conditions, path);
            ValidateBuildSettingConditionRefs(project.build.compileDefinitions, conditions, path);
            ValidateBuildSettingConditionRefs(project.build.compileOptions, conditions, path);
            ValidateBuildSettingConditionRefs(project.build.linkOptions, conditions, path);
        }

        [[nodiscard]] auto DirectSelectorsMatch(const SelectorSet &selectors, const ProfileDefinition &profile) -> bool
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

        [[nodiscard]] auto ConditionNodeMatches(
            const ConditionNode &node,
            const ProjectManifest &project,
            const ProfileDefinition &profile,
            const std::unordered_map<std::string, const ConditionDefinition *> &conditions) -> bool;

        [[nodiscard]] auto ConditionRefMatches(
            const std::string &name,
            const ProjectManifest &project,
            const ProfileDefinition &profile,
            const std::unordered_map<std::string, const ConditionDefinition *> &conditions) -> bool
        {
            const auto it = conditions.find(name);
            if (it == conditions.end())
            {
                return false;
            }
            return ConditionNodeMatches(it->second->body, project, profile, conditions);
        }

        [[nodiscard]] auto ConditionNodeMatches(
            const ConditionNode &node,
            const ProjectManifest &project,
            const ProfileDefinition &profile,
            const std::unordered_map<std::string, const ConditionDefinition *> &conditions) -> bool
        {
            switch (node.kind)
            {
            case ConditionNode::Kind::Match:
                return DirectSelectorsMatch(node.match, profile);
            case ConditionNode::Kind::All:
                return std::all_of(
                    node.children.begin(),
                    node.children.end(),
                    [&](const ConditionNode &child)
                    { return ConditionNodeMatches(child, project, profile, conditions); });
            case ConditionNode::Kind::Any:
                return std::any_of(
                    node.children.begin(),
                    node.children.end(),
                    [&](const ConditionNode &child)
                    { return ConditionNodeMatches(child, project, profile, conditions); });
            case ConditionNode::Kind::Not:
                return node.children.size() == 1 && !ConditionNodeMatches(node.children.front(), project, profile, conditions);
            case ConditionNode::Kind::ConditionRef:
                return ConditionRefMatches(node.conditionName, project, profile, conditions);
            }
            return false;
        }
    }

    [[nodiscard]] auto IsSupportedBuildType(std::string_view value) -> bool
    {
        return value == "Debug" || value == "Release" || value == "RelWithDebInfo" || value == "MinSizeRel";
    }

    [[nodiscard]] auto IsSupportedProjectBuildMode(std::string_view value) -> bool
    {
        return value == "Generated" || value == "Manual";
    }

    [[nodiscard]] auto IsValidOperatingSystem(std::string_view value) -> bool
    {
        return value == "linux" || value == "windows" || value == "macos";
    }

    [[nodiscard]] auto IsValidArchitecture(std::string_view value) -> bool
    {
        return value == "x64" || value == "arm64";
    }

    [[nodiscard]] auto IsSupportedProjectType(std::string_view value) -> bool
    {
        return value == "Application" || value == "Tool" || value == "Library";
    }

    [[nodiscard]] auto IsSupportedOutputKind(std::string_view value) -> bool
    {
        return value == "Executable" || value == "StaticLibrary" || value == "SharedLibrary";
    }

    [[nodiscard]] auto IsValidProjectOutputPairing(std::string_view projectType, std::string_view outputKind) -> bool
    {
        if (projectType == "Application" || projectType == "Tool")
        {
            return outputKind == "Executable";
        }
        if (projectType == "Library")
        {
            return outputKind == "StaticLibrary" || outputKind == "SharedLibrary";
        }
        return false;
    }

    [[nodiscard]] auto SelectionMatches(const ProjectManifest &project, const SelectorSet &selectors, const ProfileDefinition &profile) -> bool
    {
        if (!DirectSelectorsMatch(selectors, profile))
        {
            return false;
        }
        const auto conditions = ConditionMap(project);
        return std::all_of(
            selectors.conditionRefs.begin(),
            selectors.conditionRefs.end(),
            [&](const std::string &conditionName)
            { return ConditionRefMatches(conditionName, project, profile, conditions); });
    }

    [[nodiscard]] auto WorkspaceFilePath(const fs::path &root) -> std::optional<fs::path>
    {
        if (!fs::exists(root))
        {
            return std::nullopt;
        }
        std::vector<fs::path> candidates;
        for (const auto &entry : fs::directory_iterator(root))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".ngin")
            {
                candidates.push_back(entry.path());
            }
        }
        if (candidates.empty())
        {
            return std::nullopt;
        }
        std::sort(candidates.begin(), candidates.end());
        return candidates.front();
    }

    [[nodiscard]] auto RootDirFrom(const fs::path &start) -> std::optional<fs::path>
    {
        auto current = fs::weakly_canonical(start);
        if (fs::is_regular_file(current))
        {
            current = current.parent_path();
        }
        while (!current.empty())
        {
            if (WorkspaceFilePath(current).has_value())
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

    [[nodiscard]] auto RootDir(const char *argv0) -> fs::path
    {
        if (const auto fromExe = RootDirFrom(fs::absolute(argv0)); fromExe.has_value())
        {
            return *fromExe;
        }
        if (const auto fromCwd = RootDirFrom(fs::current_path()); fromCwd.has_value())
        {
            return *fromCwd;
        }
        return fs::current_path();
    }

    [[nodiscard]] auto LoadWorkspaceManifest(const fs::path &root) -> WorkspaceManifest
    {
        const auto path = WorkspaceFilePath(root);
        if (!path.has_value())
        {
            throw std::runtime_error(root.string() + ": no .ngin workspace file found");
        }
        const auto doc = LoadXml(*path);
        const auto *rootElement = doc.document.Root();
        if (rootElement == nullptr || rootElement->name != "Workspace")
        {
            throw std::runtime_error(path->string() + ": root element must be <Workspace>");
        }
        ValidateSchemaVersion(*rootElement, *path, "3");

        WorkspaceManifest workspace{};
        workspace.path = fs::weakly_canonical(*path);
        workspace.name = RequireAttribute(*rootElement, "Name", *path);
        workspace.platformVersion = Attribute(*rootElement, "PlatformVersion").value_or("0.1.0");

        const auto *packageSourcesNode = FindChild(*rootElement, "PackageSources");
        if (packageSourcesNode == nullptr)
        {
            throw std::runtime_error(path->string() + ": missing <PackageSources>");
        }
        for (const auto *child : ChildElements(*packageSourcesNode, "PackageSource"))
        {
            workspace.packageSources.push_back((workspace.path.parent_path() / RequireAttribute(*child, "Path", *path)).lexically_normal());
        }
        if (const auto *providersNode = FindChild(*rootElement, "PackageProviders"))
        {
            for (const auto *child : ChildElements(*providersNode, "PackageProvider"))
            {
                const auto name = RequireAttribute(*child, "Name", *path);
                const auto providerRoot = (workspace.path.parent_path() / RequireAttribute(*child, "Root", *path)).lexically_normal();
                workspace.packageProviders[name] = providerRoot;
            }
        }

        const auto *projectsNode = FindChild(*rootElement, "Projects");
        if (projectsNode == nullptr)
        {
            throw std::runtime_error(path->string() + ": missing <Projects>");
        }
        for (const auto *node : ChildElements(*projectsNode, "Project"))
        {
            workspace.projects.push_back((workspace.path.parent_path() / RequireAttribute(*node, "Path", *path)).lexically_normal());
        }

        return workspace;
    }

    [[nodiscard]] auto TryLoadWorkspaceManifest(const fs::path &root) -> std::optional<WorkspaceManifest>
    {
        if (!WorkspaceFilePath(root).has_value())
        {
            return std::nullopt;
        }
        return LoadWorkspaceManifest(root);
    }

    [[nodiscard]] auto LoadPackageCatalog(
        const std::optional<WorkspaceManifest> &workspace,
        const fs::path &projectPath) -> std::unordered_map<std::string, PackageCatalogEntry>
    {
        std::unordered_map<std::string, PackageCatalogEntry> out;
        const auto packageRoots = workspace.has_value() ? workspace->packageSources : DiscoverPackageSourceRoots(projectPath);
        for (const auto &packageRoot : packageRoots)
        {
            if (!fs::exists(packageRoot))
            {
                continue;
            }
            for (const auto &entry : fs::recursive_directory_iterator(packageRoot))
            {
                if (!entry.is_regular_file() || entry.path().extension() != ".nginpkg")
                {
                    continue;
                }
                const auto manifestPath = fs::weakly_canonical(entry.path());
                const auto manifest = LoadPackageManifest(manifestPath);
                fs::path providerRoot{};
                if (workspace.has_value())
                {
                    if (const auto provider = workspace->packageProviders.find(manifest.name); provider != workspace->packageProviders.end())
                    {
                        providerRoot = provider->second;
                    }
                }
                out.emplace(manifest.name, PackageCatalogEntry{
                                               .name = manifest.name,
                                               .manifestPath = manifestPath,
                                               .providerRoot = providerRoot,
                                           });
            }
        }
        return out;
    }

    [[nodiscard]] auto LoadPackageManifest(const fs::path &path) -> PackageManifest
    {
        const auto doc = LoadXml(path);
        const auto *rootElement = doc.document.Root();
        if (rootElement == nullptr || rootElement->name != "Package")
        {
            throw std::runtime_error(path.string() + ": root element must be <Package>");
        }
        ValidateSchemaVersion(*rootElement, path);

        PackageManifest package{};
        package.path = path;
        package.name = RequireAttribute(*rootElement, "Name", path);
        package.version = RequireAttribute(*rootElement, "Version", path);
        package.compatiblePlatformRange = Attribute(*rootElement, "CompatiblePlatformRange").value_or("");

        if (const auto *artifacts = FindChild(*rootElement, "Artifacts"))
        {
            if (const auto *libraries = FindChild(*artifacts, "Libraries"))
            {
                for (const auto *node : ChildElements(*libraries, "Library"))
                {
                    LibraryArtifact artifact{};
                    artifact.name = RequireAttribute(*node, "Name", path);
                    artifact.target = Attribute(*node, "Target").value_or("");
                    artifact.linkage = Attribute(*node, "Linkage").value_or("");
                    artifact.origin = Attribute(*node, "Origin").value_or("");
                    artifact.exported = !Attribute(*node, "Exported").has_value() || BoolAttribute(*node, "Exported", true);
                    package.artifacts.libraries.push_back(std::move(artifact));
                }
            }
            if (const auto *executables = FindChild(*artifacts, "Executables"))
            {
                for (const auto *node : ChildElements(*executables, "Executable"))
                {
                    ExecutableArtifact artifact{};
                    artifact.name = RequireAttribute(*node, "Name", path);
                    artifact.target = Attribute(*node, "Target").value_or("");
                    artifact.origin = Attribute(*node, "Origin").value_or("");
                    artifact.exported = !Attribute(*node, "Exported").has_value() || BoolAttribute(*node, "Exported", true);
                    package.artifacts.executables.push_back(std::move(artifact));
                }
            }
        }

        LoadPackageBuildDescriptor(package.build, FindChild(*rootElement, "Build"), path);
        package.compatibility = ParseCompatibility(*rootElement, path);

        if (const auto *deps = FindChild(*rootElement, "Dependencies"))
        {
            if (FindChild(*deps, "Dependency") != nullptr)
            {
                throw std::runtime_error(path.string() + ": package <Dependencies> uses legacy <Dependency>; use <PackageRef>");
            }
            for (const auto *node : ChildElements(*deps, "PackageRef"))
            {
                PackageDependency dependency{};
                dependency.name = RequireAttribute(*node, "Name", path);
                dependency.versionRange = Attribute(*node, "VersionRange").value_or(Attribute(*node, "Version").value_or(""));
                dependency.optional = BoolAttribute(*node, "Optional");
                package.dependencies.push_back(std::move(dependency));
            }
        }

        if (const auto *bootstrap = FindChild(*rootElement, "Bootstrap"))
        {
            PackageBootstrapDescriptor descriptor{};
            descriptor.mode = RequireAttribute(*bootstrap, "Mode", path);
            if (!IsValidPackageBootstrapMode(descriptor.mode))
            {
                throw std::runtime_error(path.string() + ": unknown package bootstrap mode '" + descriptor.mode + "'");
            }
            descriptor.entryPoint = RequireAttribute(*bootstrap, "EntryPoint", path);
            descriptor.autoApply = BoolAttribute(*bootstrap, "AutoApply");
            package.bootstrap = std::move(descriptor);
        }

        ParseContents(*rootElement, path, package.contents);

        if (const auto *modules = FindChild(*rootElement, "Modules"))
        {
            for (const auto *node : ChildElements(*modules, "Module"))
            {
                package.modules.push_back(ParseModuleDefinition(*node, path));
            }
        }

        if (const auto *plugins = FindChild(*rootElement, "Plugins"))
        {
            for (const auto *node : ChildElements(*plugins, "Plugin"))
            {
                PluginDescriptor plugin{};
                plugin.name = RequireAttribute(*node, "Name", path);
                plugin.optional = BoolAttribute(*node, "Optional");
                plugin.compatibility = ParseCompatibility(*node, path);

                if (const auto *modulesElement = FindChild(*node, "Modules"))
                {
                    if (const auto *required = FindChild(*modulesElement, "Required"))
                    {
                        for (const auto *dep : ChildElements(*required, "ModuleRef"))
                        {
                            plugin.requiredModules.push_back(RequireAttribute(*dep, "Name", path));
                        }
                    }
                    if (const auto *optional = FindChild(*modulesElement, "Optional"))
                    {
                        for (const auto *dep : ChildElements(*optional, "ModuleRef"))
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

    [[nodiscard]] auto LoadLocalSettingsManifest(const fs::path &path) -> LocalSettingsManifest
    {
        const auto doc = LoadXml(path);
        const auto *rootElement = doc.document.Root();
        if (rootElement == nullptr || rootElement->name != "LocalSettings")
        {
            throw std::runtime_error(path.string() + ": root element must be <LocalSettings>");
        }
        ValidateLocalSettingsSchemaVersion(*rootElement, path);

        LocalSettingsManifest localSettings{};
        localSettings.path = path;
        const auto *settings = FindChild(*rootElement, "Settings");
        if (settings == nullptr)
        {
            return localSettings;
        }

        std::set<std::string> seenKeys{};
        for (const auto *node : ChildElements(*settings, "Setting"))
        {
            LocalSetting setting{};
            setting.key = RequireAttribute(*node, "Key", path);
            setting.value = RequireAttribute(*node, "Value", path);
            setting.secret = BoolAttribute(*node, "Secret");
            if (!seenKeys.insert(setting.key).second)
            {
                throw std::runtime_error(path.string() + ": duplicate local setting key '" + setting.key + "'");
            }
            localSettings.settings.push_back(std::move(setting));
        }

        return localSettings;
    }

    namespace
    {
        struct ProjectDefaults
        {
            std::string buildType{"Debug"};
            std::string platform{"linux-x64"};
            std::string environment{};
            std::string language{};
            std::string languageStandard{};
            std::string backend{};
            std::string buildMode{};
        };

        struct PlatformDefinition
        {
            std::string name{};
            std::string operatingSystem{"linux"};
            std::string architecture{"x64"};
        };

        struct ProjectTemplateDefinition
        {
            std::string name{};
            std::string type{"Application"};
            std::string outputKind{"Executable"};
            ProjectDefaults defaults{};
        };

        struct ProfileTemplateDefinition
        {
            std::string name{};
            std::string extends{};
            std::optional<std::string> buildType{};
            std::optional<std::string> platform{};
            std::optional<std::string> operatingSystem{};
            std::optional<std::string> architecture{};
            std::optional<std::string> environmentName{};
            std::optional<bool> enableReflection{};
            std::optional<LaunchDefinition> launch{};
            std::vector<ProjectReference> projectRefs{};
            std::vector<PackageReference> packageRefs{};
            std::vector<std::string> configInputs{};
            RuntimeDefinition runtime{};
        };

        struct ModelContext
        {
            ProjectDefaults defaults{};
            std::unordered_map<std::string, PlatformDefinition> platforms{};
            std::unordered_map<std::string, ProjectTemplateDefinition> projectTemplates{};
            std::unordered_map<std::string, ProfileTemplateDefinition> profileTemplates{};
            std::set<std::string> authoredPlatforms{};
            std::set<std::string> authoredProjectTemplates{};
            std::set<std::string> authoredProfileTemplates{};
        };

        [[nodiscard]] auto ProjectTypeFromTemplate(const std::string &projectTemplate) -> std::string
        {
            if (projectTemplate == "Tool")
            {
                return "Tool";
            }
            if (projectTemplate == "Library")
            {
                return "Library";
            }
            return "Application";
        }

        [[nodiscard]] auto DefaultOutputKindForProjectType(const std::string &projectType) -> std::string
        {
            return projectType == "Library" ? "StaticLibrary" : "Executable";
        }

        auto MergeRuntime(RuntimeDefinition &target, const RuntimeDefinition &source) -> void
        {
            target.modules.insert(target.modules.end(), source.modules.begin(), source.modules.end());
            target.enableModules.insert(target.enableModules.end(), source.enableModules.begin(), source.enableModules.end());
            target.disableModules.insert(target.disableModules.end(), source.disableModules.begin(), source.disableModules.end());
            target.enablePlugins.insert(target.enablePlugins.end(), source.enablePlugins.begin(), source.enablePlugins.end());
            target.disablePlugins.insert(target.disablePlugins.end(), source.disablePlugins.begin(), source.disablePlugins.end());
        }

        auto ParseDefaultsElement(const XmlElement &defaultsElement, const fs::path &path, ProjectDefaults &defaults) -> void
        {
            defaults.buildType = Attribute(defaultsElement, "BuildType").value_or(defaults.buildType);
            defaults.platform = Attribute(defaultsElement, "Platform").value_or(defaults.platform);
            defaults.environment = Attribute(defaultsElement, "Environment").value_or(defaults.environment);
            defaults.language = Attribute(defaultsElement, "Language").value_or(defaults.language);
            defaults.languageStandard = Attribute(defaultsElement, "LanguageStandard").value_or(defaults.languageStandard);
            defaults.backend = Attribute(defaultsElement, "Backend").value_or(defaults.backend);
            defaults.buildMode = Attribute(defaultsElement, "BuildMode").value_or(defaults.buildMode);
            if (!IsSupportedBuildType(defaults.buildType))
            {
                throw std::runtime_error(path.string() + ": unknown default build type '" + defaults.buildType + "'");
            }
        }

        auto AddPlatform(ModelContext &context, PlatformDefinition platform, const fs::path &path, const bool builtin = false) -> void
        {
            if (!IsValidManifestIdentifier(platform.name))
            {
                throw std::runtime_error(path.string() + ": invalid platform name '" + platform.name + "'");
            }
            if (!IsValidOperatingSystem(platform.operatingSystem))
            {
                throw std::runtime_error(path.string() + ": unknown operating system '" + platform.operatingSystem + "'");
            }
            if (!IsValidArchitecture(platform.architecture))
            {
                throw std::runtime_error(path.string() + ": unknown architecture '" + platform.architecture + "'");
            }
            if (!builtin && !context.authoredPlatforms.insert(platform.name).second)
            {
                throw std::runtime_error(path.string() + ": duplicate platform '" + platform.name + "'");
            }
            context.platforms[platform.name] = std::move(platform);
        }

        auto ParsePlatformsInto(const XmlElement &root, const fs::path &path, ModelContext &context) -> void
        {
            if (const auto *platformsElement = FindChild(root, "Platforms"))
            {
                for (const auto *node : ChildElements(*platformsElement, "Platform"))
                {
                    PlatformDefinition platform{};
                    platform.name = RequireAttribute(*node, "Name", path);
                    platform.operatingSystem = RequireAttribute(*node, "OperatingSystem", path);
                    platform.architecture = RequireAttribute(*node, "Architecture", path);
                    AddPlatform(context, std::move(platform), path);
                }
            }
        }

        auto AddProjectTemplate(ModelContext &context, ProjectTemplateDefinition projectTemplate, const fs::path &path, const bool builtin = false) -> void
        {
            if (!IsValidManifestIdentifier(projectTemplate.name))
            {
                throw std::runtime_error(path.string() + ": invalid project template name '" + projectTemplate.name + "'");
            }
            if (!IsSupportedProjectType(projectTemplate.type))
            {
                throw std::runtime_error(path.string() + ": project template '" + projectTemplate.name + "' uses unknown project type '" + projectTemplate.type + "'");
            }
            if (!IsSupportedOutputKind(projectTemplate.outputKind))
            {
                throw std::runtime_error(path.string() + ": project template '" + projectTemplate.name + "' uses unknown output kind '" + projectTemplate.outputKind + "'");
            }
            if (!IsValidProjectOutputPairing(projectTemplate.type, projectTemplate.outputKind))
            {
                throw std::runtime_error(path.string() + ": project template '" + projectTemplate.name + "' has incompatible type/output pairing");
            }
            if (!builtin && !context.authoredProjectTemplates.insert(projectTemplate.name).second)
            {
                throw std::runtime_error(path.string() + ": duplicate project template '" + projectTemplate.name + "'");
            }
            context.projectTemplates[projectTemplate.name] = std::move(projectTemplate);
        }

        auto ParseProjectTemplatesInto(const XmlElement &root, const fs::path &path, ModelContext &context) -> void
        {
            if (const auto *templates = FindChild(root, "ProjectTemplates"))
            {
                for (const auto *node : ChildElements(*templates, "ProjectTemplate"))
                {
                    ProjectTemplateDefinition projectTemplate{};
                    projectTemplate.name = RequireAttribute(*node, "Name", path);
                    projectTemplate.type = Attribute(*node, "Type").value_or(ProjectTypeFromTemplate(projectTemplate.name));
                    projectTemplate.outputKind = Attribute(*node, "OutputKind").value_or(DefaultOutputKindForProjectType(projectTemplate.type));
                    projectTemplate.defaults = context.defaults;
                    if (const auto *defaults = FindChild(*node, "Defaults"))
                    {
                        ParseDefaultsElement(*defaults, path, projectTemplate.defaults);
                    }
                    AddProjectTemplate(context, std::move(projectTemplate), path);
                }
            }
        }

        [[nodiscard]] auto ParseProfileTemplate(const XmlElement &node, const fs::path &path) -> ProfileTemplateDefinition
        {
            ProfileTemplateDefinition profileTemplate{};
            profileTemplate.name = RequireAttribute(node, "Name", path);
            if (!IsValidManifestIdentifier(profileTemplate.name))
            {
                throw std::runtime_error(path.string() + ": invalid profile template name '" + profileTemplate.name + "'");
            }
            profileTemplate.extends = Attribute(node, "Extends").value_or("");
            profileTemplate.buildType = Attribute(node, "BuildType");
            profileTemplate.platform = Attribute(node, "Platform");
            profileTemplate.operatingSystem = Attribute(node, "OperatingSystem");
            profileTemplate.architecture = Attribute(node, "Architecture");
            profileTemplate.environmentName = Attribute(node, "Environment");
            if (Attribute(node, "EnableReflection").has_value())
            {
                profileTemplate.enableReflection = BoolAttribute(node, "EnableReflection");
            }
            if (profileTemplate.buildType.has_value() && !IsSupportedBuildType(*profileTemplate.buildType))
            {
                throw std::runtime_error(path.string() + ": unknown build type '" + *profileTemplate.buildType + "'");
            }
            if (profileTemplate.operatingSystem.has_value() && !IsValidOperatingSystem(*profileTemplate.operatingSystem))
            {
                throw std::runtime_error(path.string() + ": unknown operating system '" + *profileTemplate.operatingSystem + "'");
            }
            if (profileTemplate.architecture.has_value() && !IsValidArchitecture(*profileTemplate.architecture))
            {
                throw std::runtime_error(path.string() + ": unknown architecture '" + *profileTemplate.architecture + "'");
            }
            if (const auto *launch = FindChild(node, "Launch"))
            {
                LaunchDefinition parsedLaunch{};
                if (const auto executable = Attribute(*launch, "Executable"); executable.has_value() && !executable->empty())
                {
                    parsedLaunch.executable = *executable;
                }
                parsedLaunch.workingDirectory = Attribute(*launch, "WorkingDirectory").value_or(".");
                profileTemplate.launch = std::move(parsedLaunch);
            }
            if (const auto *references = FindChild(node, "References"))
            {
                ParseReferences(*references, path.parent_path(), path, profileTemplate.projectRefs, profileTemplate.packageRefs);
            }
            ParseConfigInputs(node, path, profileTemplate.configInputs);
            if (const auto *runtime = FindChild(node, "Runtime"))
            {
                ParseRuntimeDefinition(*runtime, path, profileTemplate.runtime, true);
            }
            return profileTemplate;
        }

        auto AddProfileTemplate(ModelContext &context, ProfileTemplateDefinition profileTemplate, const fs::path &path) -> void
        {
            if (!context.authoredProfileTemplates.insert(profileTemplate.name).second)
            {
                throw std::runtime_error(path.string() + ": duplicate profile template '" + profileTemplate.name + "'");
            }
            context.profileTemplates[profileTemplate.name] = std::move(profileTemplate);
        }

        auto ParseProfileTemplatesInto(const XmlElement &root, const fs::path &path, ModelContext &context) -> void
        {
            if (const auto *templates = FindChild(root, "ProfileTemplates"))
            {
                for (const auto *node : ChildElements(*templates, "ProfileTemplate"))
                {
                    AddProfileTemplate(context, ParseProfileTemplate(*node, path), path);
                }
            }
        }

        auto ParseModelContributionsInto(const XmlElement &root, const fs::path &path, ModelContext &context, const bool includeDefaults) -> void
        {
            if (includeDefaults)
            {
                if (const auto *defaults = FindChild(root, "Defaults"))
                {
                    ParseDefaultsElement(*defaults, path, context.defaults);
                }
            }
            ParsePlatformsInto(root, path, context);
            ParseProjectTemplatesInto(root, path, context);
            ParseProfileTemplatesInto(root, path, context);
        }

        [[nodiscard]] auto BuiltinModelContext() -> ModelContext
        {
            ModelContext context{};
            AddPlatform(context, PlatformDefinition{.name = "linux-x64", .operatingSystem = "linux", .architecture = "x64"}, {}, true);
            AddPlatform(context, PlatformDefinition{.name = "windows-x64", .operatingSystem = "windows", .architecture = "x64"}, {}, true);
            AddPlatform(context, PlatformDefinition{.name = "macos-x64", .operatingSystem = "macos", .architecture = "x64"}, {}, true);
            AddPlatform(context, PlatformDefinition{.name = "macos-arm64", .operatingSystem = "macos", .architecture = "arm64"}, {}, true);
            AddProjectTemplate(context, ProjectTemplateDefinition{.name = "Application", .type = "Application", .outputKind = "Executable", .defaults = context.defaults}, {}, true);
            AddProjectTemplate(context, ProjectTemplateDefinition{.name = "Library", .type = "Library", .outputKind = "StaticLibrary", .defaults = context.defaults}, {}, true);
            AddProjectTemplate(context, ProjectTemplateDefinition{.name = "Tool", .type = "Tool", .outputKind = "Executable", .defaults = context.defaults}, {}, true);
            return context;
        }

        auto LoadModelFileInto(const fs::path &path, ModelContext &context, std::vector<fs::path> &stack) -> void;

        auto LoadIncludesInto(const XmlElement &root, const fs::path &path, ModelContext &context, std::vector<fs::path> &stack) -> void
        {
            if (const auto *includes = FindChild(root, "Includes"))
            {
                for (const auto *include : ChildElements(*includes, "Include"))
                {
                    const auto includePath = (path.parent_path() / RequireAttribute(*include, "Path", path)).lexically_normal();
                    if (!fs::exists(includePath))
                    {
                        throw std::runtime_error(path.string() + ": included model file does not exist: " + includePath.string());
                    }
                    LoadModelFileInto(fs::weakly_canonical(includePath), context, stack);
                }
            }
        }

        auto LoadModelFileInto(const fs::path &path, ModelContext &context, std::vector<fs::path> &stack) -> void
        {
            if (std::find(stack.begin(), stack.end(), path) != stack.end())
            {
                std::ostringstream chain{};
                for (const auto &entry : stack)
                {
                    if (!chain.str().empty())
                    {
                        chain << " -> ";
                    }
                    chain << entry.string();
                }
                chain << " -> " << path.string();
                throw std::runtime_error(path.string() + ": model include cycle: " + chain.str());
            }
            stack.push_back(path);
            const auto doc = LoadXml(path);
            const auto *root = doc.document.Root();
            if (root == nullptr || root->name != "Model")
            {
                throw std::runtime_error(path.string() + ": root element must be <Model>");
            }
            ValidateSchemaVersion(*root, path, "3");
            LoadIncludesInto(*root, path, context, stack);
            ParseModelContributionsInto(*root, path, context, true);
            stack.pop_back();
        }

        auto LoadWorkspaceModelInto(const fs::path &workspacePath, ModelContext &context) -> void
        {
            const auto doc = LoadXml(workspacePath);
            const auto *root = doc.document.Root();
            if (root == nullptr || root->name != "Workspace")
            {
                throw std::runtime_error(workspacePath.string() + ": root element must be <Workspace>");
            }
            ValidateSchemaVersion(*root, workspacePath, "3");
            std::vector<fs::path> stack{fs::weakly_canonical(workspacePath)};
            LoadIncludesInto(*root, workspacePath, context, stack);
            ParseModelContributionsInto(*root, workspacePath, context, true);
        }

        [[nodiscard]] auto ResolveProjectModelContext(const fs::path &projectPath, const XmlElement &projectRoot) -> ModelContext
        {
            auto context = BuiltinModelContext();
            if (const auto workspaceRoot = RootDirFrom(projectPath); workspaceRoot.has_value())
            {
                if (const auto workspacePath = WorkspaceFilePath(*workspaceRoot); workspacePath.has_value())
                {
                    LoadWorkspaceModelInto(fs::weakly_canonical(*workspacePath), context);
                }
            }

            std::vector<fs::path> stack{fs::weakly_canonical(projectPath)};
            LoadIncludesInto(projectRoot, projectPath, context, stack);
            ParseModelContributionsInto(projectRoot, projectPath, context, false);
            return context;
        }

        auto ApplyPlatform(ProfileDefinition &profile, const std::unordered_map<std::string, PlatformDefinition> &platforms, const fs::path &path) -> void
        {
            const auto it = platforms.find(profile.platform);
            if (it == platforms.end())
            {
                throw std::runtime_error(path.string() + ": profile '" + profile.name + "' selects unknown platform '" + profile.platform + "'");
            }
            profile.operatingSystem = it->second.operatingSystem;
            profile.architecture = it->second.architecture;
        }

        auto ApplyProfileTemplate(
            ProfileDefinition &profile,
            const std::string &templateName,
            const ModelContext &context,
            const fs::path &path,
            std::vector<std::string> &stack) -> void
        {
            const auto it = context.profileTemplates.find(templateName);
            if (it == context.profileTemplates.end())
            {
                throw std::runtime_error(path.string() + ": unknown profile template '" + templateName + "'");
            }
            if (std::find(stack.begin(), stack.end(), templateName) != stack.end())
            {
                std::ostringstream chain{};
                for (const auto &entry : stack)
                {
                    if (!chain.str().empty())
                    {
                        chain << " -> ";
                    }
                    chain << entry;
                }
                chain << " -> " << templateName;
                throw std::runtime_error(path.string() + ": profile template cycle: " + chain.str());
            }
            stack.push_back(templateName);
            const auto &profileTemplate = it->second;
            if (!profileTemplate.extends.empty())
            {
                ApplyProfileTemplate(profile, profileTemplate.extends, context, path, stack);
            }
            if (profileTemplate.buildType.has_value())
            {
                profile.buildType = *profileTemplate.buildType;
            }
            if (profileTemplate.platform.has_value())
            {
                profile.platform = *profileTemplate.platform;
                ApplyPlatform(profile, context.platforms, path);
            }
            if (profileTemplate.operatingSystem.has_value())
            {
                profile.operatingSystem = *profileTemplate.operatingSystem;
            }
            if (profileTemplate.architecture.has_value())
            {
                profile.architecture = *profileTemplate.architecture;
            }
            if (profileTemplate.environmentName.has_value())
            {
                profile.environmentName = *profileTemplate.environmentName;
            }
            if (profileTemplate.enableReflection.has_value())
            {
                profile.enableReflection = *profileTemplate.enableReflection;
            }
            if (profileTemplate.launch.has_value())
            {
                profile.launch = *profileTemplate.launch;
            }
            profile.projectRefs.insert(profile.projectRefs.end(), profileTemplate.projectRefs.begin(), profileTemplate.projectRefs.end());
            profile.packageRefs.insert(profile.packageRefs.end(), profileTemplate.packageRefs.begin(), profileTemplate.packageRefs.end());
            profile.configInputs.insert(profile.configInputs.end(), profileTemplate.configInputs.begin(), profileTemplate.configInputs.end());
            MergeRuntime(profile.runtime, profileTemplate.runtime);
            stack.pop_back();
        }

        auto ApplyProfileTemplate(ProfileDefinition &profile, const std::string &templateName, const ModelContext &context, const fs::path &path) -> void
        {
            std::vector<std::string> stack{};
            ApplyProfileTemplate(profile, templateName, context, path, stack);
        }
    }

    [[nodiscard]] auto LoadProjectManifest(const fs::path &path) -> ProjectManifest
    {
        const auto doc = LoadXml(path);
        const auto *rootElement = doc.document.Root();
        if (rootElement == nullptr || rootElement->name != "Project")
        {
            throw std::runtime_error(path.string() + ": root element must be <Project>");
        }
        ValidateProjectSchemaVersion(*rootElement, path);
        const auto context = ResolveProjectModelContext(path, *rootElement);
        const auto requestedTemplate = Attribute(*rootElement, "Template").value_or("Application");
        const auto templateIt = context.projectTemplates.find(requestedTemplate);
        if (templateIt == context.projectTemplates.end())
        {
            throw std::runtime_error(path.string() + ": unknown project template '" + requestedTemplate + "'");
        }
        const auto &projectTemplate = templateIt->second;
        auto defaults = projectTemplate.defaults;
        if (const auto *defaultsElement = FindChild(*rootElement, "Defaults"))
        {
            ParseDefaultsElement(*defaultsElement, path, defaults);
        }

        ProjectManifest project{};
        project.path = path;
        project.name = RequireAttribute(*rootElement, "Name", path);
        project.type = Attribute(*rootElement, "Type").value_or(projectTemplate.type);
        project.defaultProfile = Attribute(*rootElement, "DefaultProfile").value_or("Runtime");
        if (!IsSupportedProjectType(project.type))
        {
            throw std::runtime_error(path.string() + ": unknown project type '" + project.type + "'");
        }
        if (Lower(project.type) == "library" && FindChild(*rootElement, "Launch") != nullptr)
        {
            throw std::runtime_error(path.string() + ": library projects may not declare root <Launch>");
        }
        if (FindChild(*rootElement, "Host") != nullptr)
        {
            throw std::runtime_error(path.string() + ": legacy <Host> is no longer supported");
        }
        project.conditions = ParseConditions(*rootElement, path);

        const auto *sourceRoots = FindChild(*rootElement, "SourceRoots");
        const auto *sources = FindChild(*rootElement, "Sources");
        if (sourceRoots != nullptr && sources != nullptr)
        {
            throw std::runtime_error(path.string() + ": project may not declare both <SourceRoots> and <Sources>; use one source declaration model");
        }

        if (sourceRoots != nullptr)
        {
            for (const auto *node : ChildElements(*sourceRoots, "SourceRoot"))
            {
                project.sourceRoots.push_back(RequireAttribute(*node, "Path", path));
            }
        }
        if (sources != nullptr)
        {
            project.sources = ParseProjectSources(*sources, path);
        }

        const auto *output = FindChild(*rootElement, "Output");
        project.output.kind = output != nullptr ? Attribute(*output, "Kind").value_or(projectTemplate.outputKind) : projectTemplate.outputKind;
        project.output.name = output != nullptr ? Attribute(*output, "Name").value_or(project.name) : project.name;
        project.output.target = output != nullptr ? Attribute(*output, "Target").value_or(project.name) : project.name;
        if (!IsSupportedOutputKind(project.output.kind))
        {
            throw std::runtime_error(path.string() + ": unknown output kind '" + project.output.kind + "'");
        }
        if (!IsValidProjectOutputPairing(project.type, project.output.kind))
        {
            throw std::runtime_error(path.string() + ": project type '" + project.type + "' is not compatible with output kind '" + project.output.kind + "'");
        }

        if (!defaults.backend.empty())
        {
            project.build.backend = defaults.backend;
        }
        if (!defaults.buildMode.empty())
        {
            project.build.mode = defaults.buildMode;
        }
        if (!defaults.language.empty())
        {
            project.build.language = defaults.language;
        }
        if (!defaults.languageStandard.empty())
        {
            project.build.languageStandard = defaults.languageStandard;
        }
        LoadProjectBuildDescriptor(project.build, FindChild(*rootElement, "Build"), path);
        if (!IsSupportedProjectBuildMode(project.build.mode))
        {
            throw std::runtime_error(path.string() + ": unknown project build mode '" + project.build.mode + "'");
        }

        if (const auto *references = FindChild(*rootElement, "References"))
        {
            ParseReferences(*references, path.parent_path(), path, project.projectRefs, project.packageRefs);
        }

        ParseConfigInputs(*rootElement, path, project.configInputs);
        ParseLocalSettingsImports(*rootElement, path, project.localSettingsImports);

        if (const auto *runtime = FindChild(*rootElement, "Runtime"))
        {
            ParseRuntimeDefinition(*runtime, path, project.runtime, true);
        }

        if (const auto *environments = FindChild(*rootElement, "Environments"))
        {
            for (const auto *node : ChildElements(*environments, "Environment"))
            {
                EnvironmentDefinition environment{};
                environment.name = RequireAttribute(*node, "Name", path);
                if (const auto *references = FindChild(*node, "References"))
                {
                    ParseReferences(*references, path.parent_path(), path, environment.projectRefs, environment.packageRefs);
                }
                ParseConfigInputs(*node, path, environment.configInputs);
                ParseContents(*node, path, environment.contents);
                ParseVariables(*node, path, environment.variables);
                ParseFeatures(*node, path, environment.features);
                if (const auto *runtime = FindChild(*node, "Runtime"))
                {
                    ParseRuntimeDefinition(*runtime, path, environment.runtime, true);
                }
                project.environments.push_back(std::move(environment));
            }
        }

        const auto *profilesNode = FindChild(*rootElement, "Profiles");
        if (profilesNode == nullptr)
        {
            throw std::runtime_error(path.string() + ": missing <Profiles>");
        }
        const auto rootLaunch = FindChild(*rootElement, "Launch");
        std::unordered_map<std::string, std::size_t> profileIndexes{};
        for (const auto *node : ChildElements(*profilesNode, "Profile"))
        {
            ValidateAllowedAttributes(*node, path, {"Name", "Extends", "Template", "BuildType", "Platform", "OperatingSystem", "Architecture", "Environment", "EnableReflection"});
            ProfileDefinition profile{};
            const auto profileName = RequireAttribute(*node, "Name", path);
            if (!IsValidManifestIdentifier(profileName))
            {
                throw std::runtime_error(path.string() + ": invalid profile name '" + profileName + "'");
            }
            if (profileIndexes.contains(profileName))
            {
                throw std::runtime_error(path.string() + ": duplicate profile '" + profileName + "'");
            }
            if (const auto extends = Attribute(*node, "Extends"); extends.has_value() && !extends->empty())
            {
                const auto parent = profileIndexes.find(*extends);
                if (parent == profileIndexes.end())
                {
                    throw std::runtime_error(path.string() + ": profile '" + profileName + "' extends unknown or later profile '" + *extends + "'");
                }
                profile = project.profiles[parent->second];
            }
            else
            {
                profile.buildType = defaults.buildType;
                profile.platform = defaults.platform;
                ApplyPlatform(profile, context.platforms, path);
                profile.environmentName = defaults.environment;
                if (rootLaunch != nullptr)
                {
                    if (const auto executable = Attribute(*rootLaunch, "Executable"); executable.has_value() && !executable->empty())
                    {
                        profile.launch.executable = *executable == "$(OutputName)" ? project.output.name : *executable;
                    }
                    profile.launch.workingDirectory = Attribute(*rootLaunch, "WorkingDirectory").value_or(".");
                }
            }
            if (const auto profileTemplate = Attribute(*node, "Template"); profileTemplate.has_value() && !profileTemplate->empty())
            {
                ApplyProfileTemplate(profile, *profileTemplate, context, path);
            }
            profile.name = profileName;
            profile.buildType = Attribute(*node, "BuildType").value_or(profile.buildType);
            if (!IsSupportedBuildType(profile.buildType))
            {
                throw std::runtime_error(path.string() + ": unknown build type '" + profile.buildType + "'");
            }
            if (const auto platform = Attribute(*node, "Platform"); platform.has_value() && !platform->empty())
            {
                profile.platform = *platform;
                ApplyPlatform(profile, context.platforms, path);
            }
            profile.operatingSystem = Attribute(*node, "OperatingSystem").value_or(profile.operatingSystem);
            profile.architecture = Attribute(*node, "Architecture").value_or(profile.architecture);
            if (!IsValidOperatingSystem(profile.operatingSystem))
            {
                throw std::runtime_error(path.string() + ": unknown operating system '" + profile.operatingSystem + "'");
            }
            if (!IsValidArchitecture(profile.architecture))
            {
                throw std::runtime_error(path.string() + ": unknown architecture '" + profile.architecture + "'");
            }
            if (Attribute(*node, "EnableReflection").has_value())
            {
                profile.enableReflection = BoolAttribute(*node, "EnableReflection");
            }
            profile.environmentName = Attribute(*node, "Environment").value_or(profile.environmentName);
            if (profile.environmentName.empty())
            {
                throw std::runtime_error(path.string() + ": profile '" + profile.name + "' must select an Environment or inherit one from <Defaults>");
            }

            if (const auto *launch = FindChild(*node, "Launch"))
            {
                if (const auto executable = Attribute(*launch, "Executable"); executable.has_value() && !executable->empty())
                {
                    profile.launch.executable = *executable == "$(OutputName)" ? project.output.name : *executable;
                }
                profile.launch.workingDirectory = Attribute(*launch, "WorkingDirectory").value_or(profile.launch.workingDirectory);
            }
            if (FindChild(*node, "EnableModules") != nullptr || FindChild(*node, "DisableModules") != nullptr
                || FindChild(*node, "EnablePlugins") != nullptr || FindChild(*node, "DisablePlugins") != nullptr)
            {
                throw std::runtime_error(path.string() + ": profile runtime selections must be nested under <Runtime>");
            }
            ParseConfigInputs(*node, path, profile.configInputs);
            if (const auto *references = FindChild(*node, "References"))
            {
                ParseReferences(*references, path.parent_path(), path, profile.projectRefs, profile.packageRefs);
            }
            if (const auto *runtime = FindChild(*node, "Runtime"))
            {
                ParseRuntimeDefinition(*runtime, path, profile.runtime, true);
            }
            if (profile.launch.executable.has_value() && *profile.launch.executable == "$(OutputName)")
            {
                profile.launch.executable = project.output.name;
            }
            if (Lower(project.type) == "library" && FindChild(*node, "Launch") != nullptr)
            {
                throw std::runtime_error(path.string() + ": library projects may not declare <Launch> in profiles");
            }
            profileIndexes.emplace(profile.name, project.profiles.size());
            project.profiles.push_back(std::move(profile));
        }

        std::set<std::string> knownEnvironments{};
        for (const auto &environment : project.environments)
        {
            if (!knownEnvironments.insert(environment.name).second)
            {
                throw std::runtime_error(path.string() + ": duplicate environment '" + environment.name + "'");
            }
        }
        for (const auto &profile : project.profiles)
        {
            if (!knownEnvironments.contains(profile.environmentName))
            {
                throw std::runtime_error(path.string() + ": profile '" + profile.name + "' selects unknown environment '" + profile.environmentName + "'");
            }
        }
        ValidateConditionReferences(project, path);

        return project;
    }

    [[nodiscard]] auto FindProjectFile(const fs::path &start) -> std::optional<fs::path>
    {
        auto current = fs::weakly_canonical(start);
        while (true)
        {
            std::vector<fs::path> candidates;
            if (fs::exists(current))
            {
                for (const auto &entry : fs::directory_iterator(current))
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

    [[nodiscard]] auto ResolveProjectPath(const std::optional<std::string> &explicitPath) -> fs::path
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

    [[nodiscard]] auto ProfileByName(const ProjectManifest &project, const std::optional<std::string> &profileName) -> const ProfileDefinition &
    {
        const auto desired = profileName.value_or(project.defaultProfile);
        for (const auto &profile : project.profiles)
        {
            if (profile.name == desired)
            {
                return profile;
            }
        }
        throw std::runtime_error("unknown profile '" + desired + "'");
    }
}
