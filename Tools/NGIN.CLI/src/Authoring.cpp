#include "Authoring.hpp"

#include "Support.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <set>
#include <sstream>
#include <unordered_map>

namespace NGIN::CLI
{
    namespace
    {
        [[nodiscard]] auto IsProductElementName(std::string_view name) -> bool;

        [[nodiscard]] auto SchemaVersion(const XmlElement &node, const fs::path &path) -> std::string
        {
            return RequireAttribute(node, "SchemaVersion", path);
        }

        auto ValidateProjectSchemaVersion(const XmlElement &node, const fs::path &path) -> std::string
        {
            const auto schemaVersion = SchemaVersion(node, path);
            if (schemaVersion != "4")
            {
                throw std::runtime_error(path.string() + ": unsupported project SchemaVersion '" + schemaVersion + "' (expected '4')");
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

        [[nodiscard]] auto ScopeMutation(const XmlElement &node, const std::string &defaultScope = {}) -> std::string
        {
            auto scope = Attribute(node, "Scope").value_or(defaultScope);
            if (const auto addScope = Attribute(node, "AddScope"); addScope.has_value() && !addScope->empty())
            {
                if (!scope.empty())
                {
                    scope += ";";
                }
                scope += *addScope;
            }
            return scope;
        }

        [[nodiscard]] auto ParseCompatibility(const XmlElement &node, const fs::path &path) -> CompatibilityDefinition
        {
            CompatibilityDefinition compatibility{};
            if (FindChild(node, "Platforms") != nullptr)
            {
                throw std::runtime_error(path.string() + ": <Platforms> is no longer supported; use <Compatibility>");
            }
            if (FindChild(node, "SupportedHosts") != nullptr)
            {
                throw std::runtime_error(path.string() + ": <SupportedHosts> is no longer supported");
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

        [[nodiscard]] auto IsSupportedInputKind(const std::string_view value) -> bool
        {
            return value == "Source" || value == "Config" || value == "Content"
                   || value == "Asset" || value == "Generated" || value == "ToolInput";
        }

        [[nodiscard]] auto IsSupportedInputMode(const std::string_view value) -> bool
        {
            return value == "Directory" || value == "File" || value == "Glob";
        }

        [[nodiscard]] auto IsSupportedGeneratedRole(const std::string_view value) -> bool
        {
            return value == "Source" || value == "Header" || value == "Content" || value == "Asset" || value == "ToolInput";
        }

        [[nodiscard]] auto IsTypedInputBlock(const std::string_view name) -> bool
        {
            return name == "Sources" || name == "Headers" || name == "Configs" || name == "Contents"
                   || name == "Assets" || name == "Generated" || name == "ToolInputs";
        }

        [[nodiscard]] auto IsStructuredInputEntry(const std::string_view name) -> bool
        {
            return name == "File" || name == "Directory" || name == "Glob";
        }

        [[nodiscard]] auto JoinPathList(const std::vector<std::string> &entries) -> std::string
        {
            std::string joined{};
            for (const auto &entry : entries)
            {
                if (!joined.empty())
                {
                    joined += ";";
                }
                joined += entry;
            }
            return joined;
        }

        [[nodiscard]] auto SplitTextPathLines(std::string_view text) -> std::vector<std::string>
        {
            std::vector<std::string> entries{};
            std::string current{};
            auto flush = [&]()
            {
                auto value = Trim(current);
                current.clear();
                if (value.empty() || value.front() == '#')
                {
                    return;
                }
                entries.push_back(std::move(value));
            };

            for (const char ch : text)
            {
                if (ch == '\n' || ch == '\r')
                {
                    flush();
                    continue;
                }
                current.push_back(ch);
            }
            flush();
            return entries;
        }

        [[nodiscard]] auto NormalizeRemoveKind(const std::string &kind) -> std::pair<std::string, std::string>
        {
            if (kind == "Header")
            {
                return {"Source", "Header"};
            }
            return {kind, {}};
        }

        auto MergeInputMetadata(std::vector<InputMetadataProperty> &target, const std::vector<InputMetadataProperty> &source) -> void
        {
            for (const auto &property : source)
            {
                const auto existing = std::find_if(
                    target.begin(),
                    target.end(),
                    [&](const InputMetadataProperty &candidate)
                    {
                        return candidate.name == property.name;
                    });
                if (existing != target.end())
                {
                    *existing = property;
                }
                else
                {
                    target.push_back(property);
                }
            }
        }

        [[nodiscard]] auto ParseInputMetadata(const XmlElement &node, const fs::path &path) -> std::vector<InputMetadataProperty>
        {
            std::vector<InputMetadataProperty> metadata{};
            if (const auto *metadataElement = FindChild(node, "Metadata"))
            {
                for (const auto *propertyElement : ChildElements(*metadataElement, "Property"))
                {
                    InputMetadataProperty property{};
                    property.name = RequireAttribute(*propertyElement, "Name", path);
                    if (!IsValidManifestIdentifier(property.name))
                    {
                        throw std::runtime_error(path.string() + ": invalid input metadata property name '" + property.name + "'");
                    }
                    property.value = RequireAttribute(*propertyElement, "Value", path);
                    MergeInputMetadata(metadata, {property});
                }
            }
            return metadata;
        }

        auto ReadCommonInputAttributes(InputDeclaration &input, const XmlElement &node, const fs::path &path, const bool allowIdentityAttributes) -> void
        {
            if (const auto value = Attribute(node, "Path"); value.has_value())
            {
                input.path = *value;
            }
            if (const auto value = Attribute(node, "Visibility"); value.has_value() && !value->empty())
            {
                input.visibility = *value;
            }
            if (const auto value = Attribute(node, "Target"); value.has_value())
            {
                input.target = *value;
            }
            if (const auto value = Attribute(node, "TargetRoot"); value.has_value())
            {
                input.targetRoot = *value;
            }
            if (const auto value = Attribute(node, "BasePath"); value.has_value())
            {
                input.basePath = *value;
            }
            if (const auto value = Attribute(node, "ContentKind"); value.has_value() && !value->empty())
            {
                input.contentKind = *value;
            }
            if (Attribute(node, "Required").has_value())
            {
                input.required = BoolAttribute(node, "Required", true);
            }
            input.includePatterns = OptionalPathListAttribute(node, "Include");
            input.excludePatterns = OptionalPathListAttribute(node, "Exclude");
            input.selectors = MergeSelectors(input.selectors, ParseSelection(node, path));
            auto metadata = ParseInputMetadata(node, path);
            MergeInputMetadata(input.metadata, metadata);
            if (allowIdentityAttributes)
            {
                if (const auto value = Attribute(node, "Name"); value.has_value() && !value->empty())
                {
                    if (!IsValidManifestIdentifier(*value))
                    {
                        throw std::runtime_error(path.string() + ": invalid input name '" + *value + "'");
                    }
                    input.name = *value;
                }
                if (Attribute(node, "Override").has_value())
                {
                    input.overrideExisting = BoolAttribute(node, "Override");
                }
            }
        }

        auto ValidateInputDeclaration(const InputDeclaration &input, const fs::path &path) -> void
        {
            if (!IsSupportedInputKind(input.kind))
            {
                throw std::runtime_error(path.string() + ": unsupported input kind '" + input.kind + "'");
            }
            if (input.kind == "Generated" && !IsSupportedGeneratedRole(input.role))
            {
                throw std::runtime_error(path.string() + ": <Generated> requires Role=\"Source\", Role=\"Content\", Role=\"Asset\", or Role=\"ToolInput\"");
            }
            if (!input.mode.empty() && !IsSupportedInputMode(input.mode))
            {
                throw std::runtime_error(path.string() + ": unsupported input mode '" + input.mode + "'");
            }
            if (input.mode.empty())
            {
                throw std::runtime_error(path.string() + ": input declarations must resolve to Mode=\"Directory\", Mode=\"File\", or Mode=\"Glob\"");
            }
            if (!input.visibility.empty() && !IsSupportedBuildVisibility(input.visibility))
            {
                throw std::runtime_error(path.string() + ": unsupported input visibility '" + input.visibility + "'");
            }
            if (input.mode == "File")
            {
                if (input.path.empty())
                {
                    throw std::runtime_error(path.string() + ": <File> input entries require Path");
                }
                if (!input.includePatterns.empty() || !input.excludePatterns.empty() || !input.basePath.empty())
                {
                    throw std::runtime_error(path.string() + ": Include, Exclude, and BasePath are not valid on file input entries");
                }
            }
            else if (input.mode == "Directory")
            {
                if (input.path.empty())
                {
                    throw std::runtime_error(path.string() + ": directory input entries require Path");
                }
                if (!input.target.empty())
                {
                    throw std::runtime_error(path.string() + ": Target is valid only on file input entries; use TargetRoot for directories");
                }
            }
            else if (input.mode == "Glob")
            {
                if (input.includePatterns.empty())
                {
                    throw std::runtime_error(path.string() + ": glob input entries require Include");
                }
                if (!input.path.empty() || !input.target.empty())
                {
                    throw std::runtime_error(path.string() + ": glob input entries use Include/BasePath/TargetRoot, not Path or Target");
                }
            }
            if (input.kind != "Content" && !(input.kind == "Generated" && input.role == "Content") && !input.contentKind.empty())
            {
                throw std::runtime_error(path.string() + ": ContentKind is valid only on Content inputs");
            }
        }

        [[nodiscard]] auto InputMatchesRemove(const InputDeclaration &input, const InputRemove &remove) -> bool
        {
            if (!remove.name.empty())
            {
                return input.name == remove.name || input.setName == remove.name;
            }
            if (!remove.kind.empty() && input.kind != remove.kind)
            {
                return false;
            }
            if (!remove.role.empty() && input.role != remove.role)
            {
                return false;
            }
            if (!remove.path.empty() && input.path != remove.path)
            {
                return false;
            }
            if (!remove.pattern.empty() && input.pattern != remove.pattern)
            {
                return false;
            }
            if (!remove.target.empty() && input.target != remove.target)
            {
                return false;
            }
            if (!remove.mode.empty() && input.mode != remove.mode)
            {
                return false;
            }
            if (!remove.visibility.empty() && input.visibility != remove.visibility)
            {
                return false;
            }
            return !remove.kind.empty() || !remove.path.empty() || !remove.pattern.empty()
                   || !remove.target.empty() || !remove.mode.empty() || !remove.visibility.empty();
        }

        [[nodiscard]] auto InputIdentityMatches(const InputDeclaration &left, const InputDeclaration &right) -> bool
        {
            if (!right.name.empty())
            {
                return left.name == right.name;
            }
            return left.kind == right.kind
                   && left.role == right.role
                   && left.path == right.path
                   && left.pattern == right.pattern
                   && left.target == right.target
                   && left.targetRoot == right.targetRoot
                   && left.basePath == right.basePath
                   && left.mode == right.mode
                   && left.visibility == right.visibility;
        }

        auto RemoveMatchingInputs(std::vector<InputDeclaration> &inputs, const InputRemove &remove) -> void
        {
            inputs.erase(
                std::remove_if(
                    inputs.begin(),
                    inputs.end(),
                    [&](const InputDeclaration &input)
                    {
                        return InputMatchesRemove(input, remove);
                    }),
                inputs.end());
        }

        auto AddInputDeclaration(std::vector<InputDeclaration> &inputs, InputDeclaration input) -> void
        {
            if (input.overrideExisting)
            {
                inputs.erase(
                    std::remove_if(
                        inputs.begin(),
                        inputs.end(),
                        [&](const InputDeclaration &candidate)
                        {
                            return InputIdentityMatches(candidate, input);
                        }),
                    inputs.end());
            }
            inputs.push_back(std::move(input));
        }

        [[nodiscard]] auto ParseInputRemove(const XmlElement &node, const fs::path &path) -> InputRemove
        {
            ValidateAllowedAttributes(node, path, {"Name", "Kind", "Role", "Path", "Pattern", "Mode", "Visibility", "Target"});
            InputRemove remove{};
            remove.name = Attribute(node, "Name").value_or("");
            auto [kind, role] = NormalizeRemoveKind(Attribute(node, "Kind").value_or(""));
            remove.kind = std::move(kind);
            remove.role = Attribute(node, "Role").value_or(role);
            remove.path = Attribute(node, "Path").value_or("");
            remove.pattern = Attribute(node, "Pattern").value_or("");
            remove.mode = Attribute(node, "Mode").value_or("");
            remove.visibility = Attribute(node, "Visibility").value_or("");
            remove.target = Attribute(node, "Target").value_or("");
            if (!remove.name.empty() && !IsValidManifestIdentifier(remove.name))
            {
                throw std::runtime_error(path.string() + ": invalid input remove name '" + remove.name + "'");
            }
            if (!remove.kind.empty() && !IsSupportedInputKind(remove.kind))
            {
                throw std::runtime_error(path.string() + ": unsupported input kind '" + remove.kind + "'");
            }
            if (!remove.mode.empty() && !IsSupportedInputMode(remove.mode))
            {
                throw std::runtime_error(path.string() + ": unsupported input mode '" + remove.mode + "'");
            }
            if (!remove.visibility.empty() && !IsSupportedBuildVisibility(remove.visibility))
            {
                throw std::runtime_error(path.string() + ": unsupported input visibility '" + remove.visibility + "'");
            }
            if (remove.name.empty() && remove.kind.empty() && remove.path.empty() && remove.pattern.empty()
                && remove.mode.empty() && remove.visibility.empty() && remove.target.empty())
            {
                throw std::runtime_error(path.string() + ": <Remove> must declare Name or at least one input matcher");
            }
            return remove;
        }

        [[nodiscard]] auto TypedBlockBase(const XmlElement &node, const fs::path &path) -> InputDeclaration
        {
            InputDeclaration input{};
            if (node.name == "Sources")
            {
                input.kind = "Source";
                input.role = "Source";
                input.visibility = "Private";
            }
            else if (node.name == "Headers")
            {
                input.kind = "Source";
                input.role = "Header";
                input.visibility = "Public";
            }
            else if (node.name == "Configs")
            {
                input.kind = "Config";
            }
            else if (node.name == "Contents")
            {
                input.kind = "Content";
            }
            else if (node.name == "Assets")
            {
                input.kind = "Asset";
            }
            else if (node.name == "ToolInputs")
            {
                input.kind = "ToolInput";
            }
            else if (node.name == "Generated")
            {
                input.kind = "Generated";
                input.role = RequireAttribute(node, "Role", path);
                if (!IsSupportedGeneratedRole(input.role))
                {
                    throw std::runtime_error(path.string() + ": unsupported generated input role '" + input.role + "'");
                }
            }
            if (const auto name = Attribute(node, "Name"); name.has_value() && !name->empty())
            {
                if (!IsValidManifestIdentifier(*name))
                {
                    throw std::runtime_error(path.string() + ": invalid input block name '" + *name + "'");
                }
                input.setName = *name;
            }
            ReadCommonInputAttributes(input, node, path, false);
            return input;
        }

        auto AddTypedInput(std::vector<InputDeclaration> &inputs, InputDeclaration input, const fs::path &path, const std::string &declaringScope) -> void
        {
            input.declaringScope = declaringScope;
            ValidateInputDeclaration(input, path);
            AddInputDeclaration(inputs, std::move(input));
        }

        [[nodiscard]] auto InputFromTextLine(const std::string &line, const InputDeclaration &base) -> InputDeclaration
        {
            auto input = base;
            input.path = line;
            input.pattern.clear();
            input.mode = "File";
            input.name.clear();
            input.target.clear();
            input.overrideExisting = false;
            return input;
        }

        [[nodiscard]] auto InputFromBlockAttributes(const InputDeclaration &base) -> std::optional<InputDeclaration>
        {
            const auto hasPath = !base.path.empty();
            const auto hasInclude = !base.includePatterns.empty();
            if (!hasPath && !hasInclude)
            {
                return std::nullopt;
            }
            auto input = base;
            input.name.clear();
            input.target.clear();
            input.overrideExisting = false;
            input.mode = hasPath ? "Directory" : "Glob";
            if (!hasPath)
            {
                input.pattern = JoinPathList(input.includePatterns);
            }
            return input;
        }

        [[nodiscard]] auto ParseStructuredInputEntry(const XmlElement &node, const fs::path &path, const InputDeclaration &base) -> InputDeclaration
        {
            ValidateAllowedAttributes(node, path, {"Path", "Include", "Exclude", "BasePath", "Name", "Visibility", "Target", "TargetRoot", "ContentKind", "Required", "Override", "Profile", "Platform", "OperatingSystem", "Architecture", "BuildType", "Environment", "Condition"});
            InputDeclaration input = base;
            input.path.clear();
            input.pattern.clear();
            input.name.clear();
            input.target.clear();
            input.overrideExisting = false;
            if (node.name == "File")
            {
                input.mode = "File";
                input.path = RequireAttribute(node, "Path", path);
            }
            else if (node.name == "Directory")
            {
                input.mode = "Directory";
                input.path = RequireAttribute(node, "Path", path);
            }
            else if (node.name == "Glob")
            {
                input.mode = "Glob";
            }
            ReadCommonInputAttributes(input, node, path, true);
            if (input.mode == "Glob")
            {
                input.pattern = JoinPathList(input.includePatterns);
            }
            return input;
        }

        auto ApplyTypedInputBlock(const XmlElement &node, const fs::path &path, std::vector<InputDeclaration> &inputs, const std::string &declaringScope) -> void
        {
            ValidateAllowedAttributes(node, path, {"Name", "Role", "Path", "Include", "Exclude", "BasePath", "Visibility", "TargetRoot", "ContentKind", "Required", "Profile", "Platform", "OperatingSystem", "Architecture", "BuildType", "Environment", "Condition"});
            auto base = TypedBlockBase(node, path);
            if (const auto blockInput = InputFromBlockAttributes(base); blockInput.has_value())
            {
                AddTypedInput(inputs, *blockInput, path, declaringScope);
            }
            for (const auto &line : SplitTextPathLines(TextContent(node)))
            {
                AddTypedInput(inputs, InputFromTextLine(line, base), path, declaringScope);
            }
            for (const auto *child : ChildElements(node))
            {
                if (child->name == "Metadata")
                {
                    continue;
                }
                if (!IsStructuredInputEntry(child->name))
                {
                    throw std::runtime_error(path.string() + ": unsupported <" + std::string(node.name) + "> child <" + std::string(child->name) + ">");
                }
                AddTypedInput(inputs, ParseStructuredInputEntry(*child, path, base), path, declaringScope);
            }
        }

        auto ApplyInputBlock(const XmlElement &parent, const fs::path &path, std::vector<InputDeclaration> &inputs, const std::string &declaringScope) -> void
        {
            if (FindChild(parent, "Sources") != nullptr)
            {
                throw std::runtime_error(path.string() + ": top-level <Sources> is no longer supported; use <Inputs><Sources ... />");
            }
            if (FindChild(parent, "SourceRoots") != nullptr)
            {
                throw std::runtime_error(path.string() + ": <SourceRoots> is no longer supported; use <Inputs><Sources Path=\"...\" />");
            }
            if (FindChild(parent, "Contents") != nullptr)
            {
                throw std::runtime_error(path.string() + ": top-level <Contents> is no longer supported; use <Inputs><Contents ... />");
            }
            const auto *inputsElement = FindChild(parent, "Inputs");
            if (inputsElement == nullptr)
            {
                return;
            }
            for (const auto *child : ChildElements(*inputsElement, "Remove"))
            {
                RemoveMatchingInputs(inputs, ParseInputRemove(*child, path));
            }
            for (const auto *child : ChildElements(*inputsElement))
            {
                if (child->name == "Remove")
                {
                    continue;
                }
                if (child->name == "Input" || child->name == "InputSet")
                {
                    throw std::runtime_error(path.string() + ": <Inputs><" + std::string(child->name) + "> is no longer supported; use typed input blocks");
                }
                if (child->name == "Config")
                {
                    throw std::runtime_error(path.string() + ": <Inputs><Config> is no longer supported; use <Inputs><Configs>...</Configs></Inputs>");
                }
                if (IsTypedInputBlock(child->name))
                {
                    ApplyTypedInputBlock(*child, path, inputs, declaringScope);
                    continue;
                }
                throw std::runtime_error(path.string() + ": unsupported <Inputs> child <" + std::string(child->name) + ">");
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

        [[nodiscard]] auto ParseToolDeclaration(const XmlElement &node, const fs::path &path, const bool requireName) -> ToolDeclaration
        {
            ValidateAllowedAttributes(node, path, {"Name", "Kind", "Executable", "Profile", "Platform", "OperatingSystem", "Architecture", "BuildType", "Environment", "Condition"});
            ToolDeclaration tool{};
            if (requireName)
            {
                tool.name = RequireAttribute(node, "Name", path);
                if (!IsValidManifestIdentifier(tool.name))
                {
                    throw std::runtime_error(path.string() + ": invalid tool name '" + tool.name + "'");
                }
            }
            else
            {
                tool.name = Attribute(node, "Name").value_or("");
            }
            tool.kind = Attribute(node, "Kind").value_or("Generator");
            tool.executable = Attribute(node, "Executable").value_or("");
            tool.selectors = ParseSelection(node, path);
            if (tool.kind != "Generator")
            {
                throw std::runtime_error(path.string() + ": unsupported tool kind '" + tool.kind + "'");
            }
            if (tool.executable.empty())
            {
                throw std::runtime_error(path.string() + ": tool '" + (tool.name.empty() ? std::string{"<inline>"} : tool.name) + "' must declare Executable");
            }
            return tool;
        }

        [[nodiscard]] auto ParseGeneratorOutput(const XmlElement &node, const fs::path &path, const std::string &declaringScope) -> InputDeclaration
        {
            if (node.name != "Generated")
            {
                throw std::runtime_error(path.string() + ": unsupported <Outputs> child <" + std::string(node.name) + ">");
            }
            ValidateAllowedAttributes(node, path, {"Name", "Role", "Path", "Visibility", "Target", "TargetRoot", "ContentKind", "Required", "Profile", "Platform", "OperatingSystem", "Architecture", "BuildType", "Environment", "Condition"});
            InputDeclaration output{};
            output.kind = "Generated";
            output.role = RequireAttribute(node, "Role", path);
            if (!IsSupportedGeneratedRole(output.role))
            {
                throw std::runtime_error(path.string() + ": unsupported generated output role '" + output.role + "'");
            }
            output.path = RequireAttribute(node, "Path", path);
            output.mode = "File";
            output.visibility = output.role == "Header" ? "Public" : "Private";
            output.required = BoolAttribute(node, "Required", true);
            output.declaringScope = declaringScope;
            ReadCommonInputAttributes(output, node, path, false);
            if (output.role == "Header")
            {
                output.visibility = Attribute(node, "Visibility").value_or(output.visibility);
            }
            ValidateInputDeclaration(output, path);
            return output;
        }

        auto ParseGenerators(const XmlElement &parent, const fs::path &path, std::vector<GeneratorDeclaration> &out, const std::string &declaringScope) -> void
        {
            const auto *generators = FindChild(parent, "Generators");
            if (generators == nullptr)
            {
                return;
            }
            std::set<std::string> names{};
            for (const auto *node : ChildElements(*generators, "Generator"))
            {
                ValidateAllowedAttributes(*node, path, {"Name", "Kind", "Package", "Tool", "Profile", "Platform", "OperatingSystem", "Architecture", "BuildType", "Environment", "Condition"});
                GeneratorDeclaration generator{};
                generator.name = RequireAttribute(*node, "Name", path);
                generator.kind = RequireAttribute(*node, "Kind", path);
                generator.packageName = Attribute(*node, "Package").value_or("");
                generator.toolName = Attribute(*node, "Tool").value_or("");
                generator.selectors = ParseSelection(*node, path);
                if (!IsValidManifestIdentifier(generator.name))
                {
                    throw std::runtime_error(path.string() + ": invalid generator name '" + generator.name + "'");
                }
                if (!names.insert(generator.name).second)
                {
                    throw std::runtime_error(path.string() + ": duplicate generator '" + generator.name + "'");
                }
                if (generator.kind != "Command")
                {
                    throw std::runtime_error(path.string() + ": unsupported generator kind '" + generator.kind + "'");
                }
                if (const auto *tool = FindChild(*node, "Tool"))
                {
                    generator.inlineTool = ParseToolDeclaration(*tool, path, false);
                    generator.hasInlineTool = true;
                }
                if (generator.toolName.empty() && !generator.hasInlineTool)
                {
                    throw std::runtime_error(path.string() + ": command generator '" + generator.name + "' must declare Tool or inline <Tool>");
                }
                if (const auto *arguments = FindChild(*node, "Arguments"))
                {
                    for (const auto *arg : ChildElements(*arguments, "Arg"))
                    {
                        ValidateAllowedAttributes(*arg, path, {"Value", "Path", "Profile", "Platform", "OperatingSystem", "Architecture", "BuildType", "Environment", "Condition"});
                        GeneratorArgument argument{};
                        argument.value = Attribute(*arg, "Value").value_or("");
                        argument.path = Attribute(*arg, "Path").value_or("");
                        argument.selectors = ParseSelection(*arg, path);
                        if (argument.value.empty() == argument.path.empty())
                        {
                            throw std::runtime_error(path.string() + ": generator argument must declare exactly one of Value or Path");
                        }
                        generator.arguments.push_back(std::move(argument));
                    }
                }
                ApplyInputBlock(*node, path, generator.inputs, declaringScope + ":" + generator.name);
                const auto *outputs = FindChild(*node, "Outputs");
                if (outputs == nullptr)
                {
                    throw std::runtime_error(path.string() + ": generator '" + generator.name + "' must declare <Outputs>");
                }
                for (const auto *output : ChildElements(*outputs))
                {
                    generator.outputs.push_back(ParseGeneratorOutput(*output, path, declaringScope + ":" + generator.name));
                }
                if (generator.outputs.empty())
                {
                    throw std::runtime_error(path.string() + ": generator '" + generator.name + "' must declare at least one output");
                }
                out.push_back(std::move(generator));
            }
        }

        [[nodiscard]] auto ParseModuleDefinition(const XmlElement &node, const fs::path &path) -> ModuleDescriptor;

        auto ParseRuntimeDefinition(const XmlElement &runtime, const fs::path &path, RuntimeDefinition &target, const bool allowModules) -> void
        {
            const auto parseRuntimeRef = [&](const XmlElement &node) -> RuntimeReference
            {
                ValidateAllowedAttributes(node, path, {"Name", "Profile", "Platform", "OperatingSystem", "Architecture", "BuildType", "Environment", "Condition"});
                RuntimeReference ref{};
                ref.name = RequireAttribute(node, "Name", path);
                ref.selectors = ParseSelection(node, path);
                return ref;
            };
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
                    target.enableModules.push_back(parseRuntimeRef(*node));
                }
            }
            if (const auto *disableModules = FindChild(runtime, "DisableModules"))
            {
                for (const auto *node : ChildElements(*disableModules, "ModuleRef"))
                {
                    target.disableModules.push_back(parseRuntimeRef(*node));
                }
            }
            if (const auto *enablePlugins = FindChild(runtime, "EnablePlugins"))
            {
                for (const auto *node : ChildElements(*enablePlugins, "PluginRef"))
                {
                    target.enablePlugins.push_back(parseRuntimeRef(*node));
                }
            }
            if (const auto *disablePlugins = FindChild(runtime, "DisablePlugins"))
            {
                for (const auto *node : ChildElements(*disablePlugins, "PluginRef"))
                {
                    target.disablePlugins.push_back(parseRuntimeRef(*node));
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

        [[nodiscard]] auto IsHttpUrl(const std::string &value) -> bool
        {
            return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
        }

        [[nodiscard]] auto ShellQuoteString(const std::string &value) -> std::string
        {
            std::string quoted{"'"};
            for (const char ch : value)
            {
                if (ch == '\'')
                {
                    quoted += "'\\''";
                }
                else
                {
                    quoted.push_back(ch);
                }
            }
            quoted.push_back('\'');
            return quoted;
        }

        [[nodiscard]] auto UrlParent(const std::string &url) -> std::string
        {
            const auto query = url.find_first_of("?#");
            const auto withoutQuery = url.substr(0, query);
            const auto slash = withoutQuery.find_last_of('/');
            if (slash == std::string::npos)
            {
                return url;
            }
            return withoutQuery.substr(0, slash + 1);
        }

        [[nodiscard]] auto JoinUrl(const std::string &base, const std::string &path) -> std::string
        {
            if (IsHttpUrl(path))
            {
                return path;
            }
            if (!base.empty() && base.back() == '/')
            {
                return base + path;
            }
            return base + "/" + path;
        }

        [[nodiscard]] auto SafeCacheFileName(const std::string &value, const std::string &defaultExtension) -> std::string
        {
            std::string out{};
            out.reserve(value.size() + defaultExtension.size());
            for (const char ch : value)
            {
                const auto byte = static_cast<unsigned char>(ch);
                out.push_back((std::isalnum(byte) != 0 || ch == '.' || ch == '-' || ch == '_') ? ch : '_');
            }
            if (fs::path(out).extension().empty())
            {
                out += defaultExtension;
            }
            return out;
        }

        auto DownloadUrlToFile(const std::string &url, const fs::path &destination) -> void
        {
            if (!destination.parent_path().empty())
            {
                fs::create_directories(destination.parent_path());
            }
            const auto command = std::string{"curl -fsSL --retry 2 --connect-timeout 10 --output "}
                                 + ShellQuoteString(destination.string())
                                 + " " + ShellQuoteString(url);
            if (std::system(command.c_str()) != 0)
            {
                throw std::runtime_error("failed to download package source URL '" + url + "'");
            }
        }

        [[nodiscard]] auto NetworkPackageCacheRoot(
            const std::optional<WorkspaceManifest> &workspace,
            const fs::path &projectPath) -> fs::path
        {
            if (workspace.has_value())
            {
                return workspace->path.parent_path() / ".ngin/package-cache";
            }
            return projectPath.parent_path() / ".ngin/package-cache";
        }

        [[nodiscard]] auto ParseModuleDefinition(const XmlElement &node, const fs::path &path) -> ModuleDescriptor
        {
            ValidateAllowedAttributes(node, path, {"Name", "Family", "Type", "StartupStage", "Version", "CompatiblePlatformRange", "ReflectionRequired", "Profile", "Platform", "OperatingSystem", "Architecture", "BuildType", "Environment", "Condition"});
            ModuleDescriptor module{};
            module.name = RequireAttribute(node, "Name", path);
            module.family = Attribute(node, "Family").value_or("App");
            module.type = Attribute(node, "Type").value_or("Runtime");
            module.startupStage = ResolveStartupStage(node, "Features");
            module.version = Attribute(node, "Version").value_or("");
            module.compatiblePlatformRange = Attribute(node, "CompatiblePlatformRange").value_or("");
            module.requiresReflection = BoolAttribute(node, "ReflectionRequired");
            module.selectors = ParseSelection(node, path);
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

        [[nodiscard]] auto IsConditionNodeName(const std::string_view name) -> bool
        {
            return name == "Match" || name == "When" || name == "All" || name == "Any" || name == "Not" || name == "ConditionRef";
        }

        [[nodiscard]] auto ParseConditionNode(const XmlElement &node, const fs::path &path) -> ConditionNode
        {
            if (!Trim(TextContent(node)).empty())
            {
                throw std::runtime_error(path.string() + ": <" + std::string(node.name) + "> condition nodes may not contain text");
            }

            ConditionNode parsed{};
            if (node.name == "Match" || node.name == "When")
            {
                ValidateAllowedAttributes(node, path, {"Profile", "Platform", "OperatingSystem", "Architecture", "BuildType", "Environment"});
                if (!HasSelectorAttributes(node))
                {
                    throw std::runtime_error(path.string() + ": <" + std::string(node.name) + "> must declare at least one selector attribute");
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
                condition.manifestPath = path;
                condition.sourceKind = std::string(root.name);
                condition.sourceName = Attribute(root, "Name").value_or("");
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

        [[nodiscard]] auto MatchCondition(
            std::string name,
            const std::optional<std::string> &profile = {},
            const std::optional<std::string> &platform = {},
            const std::optional<std::string> &operatingSystem = {},
            const std::optional<std::string> &architecture = {},
            const std::optional<std::string> &buildType = {},
            const std::optional<std::string> &environment = {}) -> ConditionDefinition
        {
            ConditionDefinition condition{};
            condition.name = std::move(name);
            condition.builtin = true;
            condition.sourceKind = "built-in";
            condition.sourceName = "manifest";
            condition.body.kind = ConditionNode::Kind::Match;
            condition.body.match.profile = profile;
            condition.body.match.platform = platform;
            condition.body.match.operatingSystem = operatingSystem;
            condition.body.match.architecture = architecture;
            condition.body.match.buildType = buildType;
            condition.body.match.environment = environment;
            return condition;
        }

        [[nodiscard]] auto DesktopCondition() -> ConditionDefinition
        {
            ConditionDefinition condition{};
            condition.name = "Desktop";
            condition.builtin = true;
            condition.sourceKind = "built-in";
            condition.sourceName = "manifest";
            condition.body.kind = ConditionNode::Kind::Any;
            condition.body.children.push_back(MatchCondition("", {}, {}, "windows").body);
            condition.body.children.push_back(MatchCondition("", {}, {}, "linux").body);
            condition.body.children.push_back(MatchCondition("", {}, {}, "macos").body);
            return condition;
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
                build.backendExplicit = true;
            }
            if (const auto mode = Attribute(*buildElement, "Mode"); mode.has_value() && !mode->empty())
            {
                build.mode = *mode;
                build.backendExplicit = true;
            }
            if (!IsSupportedProjectBuildMode(build.mode))
            {
                throw std::runtime_error(path.string() + ": unknown project build mode '" + build.mode + "'");
            }
            if (const auto language = Attribute(*buildElement, "Language"); language.has_value() && !language->empty())
            {
                build.language = *language;
                build.languageExplicit = true;
            }
            if (const auto languageStandard = Attribute(*buildElement, "LanguageStandard"); languageStandard.has_value() && !languageStandard->empty())
            {
                build.languageStandard = *languageStandard;
                build.languageExplicit = true;
            }
            if (const auto *metaGen = FindChild(*buildElement, "MetaGen"))
            {
                (void)metaGen;
                throw std::runtime_error(path.string() + ": <Build><MetaGen> is no longer supported; use a package-provided command generator");
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
            for (const auto *item : ChildElements(*buildElement, "Define"))
            {
                BuildSetting setting{};
                setting.value = RequireAttribute(*item, "Name", path);
                if (const auto value = Attribute(*item, "Value"); value.has_value())
                {
                    setting.value += "=" + *value;
                }
                setting.visibility = Attribute(*item, "Visibility").value_or("Public");
                setting.selectors = ParseSelection(*item, path);
                build.compileDefinitions.push_back(std::move(setting));
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

        auto ParsePackageFeatureDeclarations(const XmlElement &root, const fs::path &path, PackageManifest &package) -> void
        {
            const auto *features = FindChild(root, "Features");
            if (features == nullptr)
            {
                return;
            }
            std::set<std::string> names{};
            for (const auto *node : ChildElements(*features, "Feature"))
            {
                ValidateAllowedAttributes(*node, path, {"Name", "Description", "Profile", "Platform", "OperatingSystem", "Architecture", "BuildType", "Environment", "Condition"});
                PackageManifest::Feature feature{};
                feature.name = RequireAttribute(*node, "Name", path);
                feature.description = Attribute(*node, "Description").value_or("");
                feature.selectors = ParseSelection(*node, path);
                if (!names.insert(feature.name).second)
                {
                    throw std::runtime_error(path.string() + ": duplicate package feature '" + feature.name + "'");
                }
                if (const auto *provides = FindChild(*node, "Provides"))
                {
                    for (const auto *capability : ChildElements(*provides, "Capability"))
                    {
                        CapabilityProvision provided{};
                        provided.name = RequireAttribute(*capability, "Name", path);
                        provided.exclusive = BoolAttribute(*capability, "Exclusive");
                        feature.provides.push_back(std::move(provided));
                    }
                }
                if (const auto *requiresElement = FindChild(*node, "Requires"))
                {
                    for (const auto *capability : ChildElements(*requiresElement, "Capability"))
                    {
                        CapabilityRequirement required{};
                        required.name = RequireAttribute(*capability, "Name", path);
                        feature.requiredCapabilities.push_back(std::move(required));
                    }
                }
                if (const auto *uses = FindChild(*node, "Uses"))
                {
                    for (const auto *dependency : ChildElements(*uses))
                    {
                        if (dependency->name != "Package" && dependency->name != "Tool" && dependency->name != "Runtime")
                        {
                            continue;
                        }
                        ValidateAllowedAttributes(*dependency, path, {"Name", "Version", "VersionRange", "Optional", "Scope", "Profile", "Platform", "OperatingSystem", "Architecture", "BuildType", "Environment", "Condition"});
                        PackageReference reference{};
                        reference.name = RequireAttribute(*dependency, "Name", path);
                        reference.versionRange = Attribute(*dependency, "Version").value_or(Attribute(*dependency, "VersionRange").value_or(""));
                        reference.optional = BoolAttribute(*dependency, "Optional");
                        reference.selectors = ParseSelection(*dependency, path);
                        reference.scope = Attribute(*dependency, "Scope").value_or(dependency->name == "Tool" ? "Build" : "");
                        feature.packageRefs.push_back(std::move(reference));
                    }
                }
                ApplyInputBlock(*node, path, feature.inputs, "package-feature:" + package.name + ":" + feature.name);
                LoadProjectBuildDescriptor(feature.build, FindChild(*node, "Build"), path);
                ParseGenerators(*node, path, feature.generators, "package-feature:" + package.name + ":" + feature.name);
                ParseVariables(*node, path, feature.variables);
                if (const auto *runtime = FindChild(*node, "Runtime"))
                {
                    ParseRuntimeDefinition(*runtime, path, feature.runtime, true);
                }
                package.features.push_back(std::move(feature));
            }
        }

        [[nodiscard]] auto ConditionMap(const std::vector<ConditionDefinition> &source) -> std::unordered_map<std::string, const ConditionDefinition *>
        {
            std::unordered_map<std::string, const ConditionDefinition *> conditions{};
            for (const auto &condition : source)
            {
                conditions.emplace(condition.name, &condition);
            }
            return conditions;
        }

        [[nodiscard]] auto ConditionMap(const ProjectManifest &project) -> std::unordered_map<std::string, const ConditionDefinition *>
        {
            return ConditionMap(project.conditions);
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
            const auto availableNames = [&]()
            {
                std::vector<std::string> names{};
                names.reserve(conditions.size());
                for (const auto &[name, _] : conditions)
                {
                    names.push_back(name);
                }
                std::sort(names.begin(), names.end());
                std::ostringstream text{};
                for (std::size_t index = 0; index < names.size(); ++index)
                {
                    if (index != 0)
                    {
                        text << ", ";
                    }
                    text << names[index];
                }
                return text.str();
            };
            for (const auto &conditionName : selectors.conditionRefs)
            {
                if (!conditions.contains(conditionName))
                {
                    throw std::runtime_error(path.string() + ": unknown condition '" + conditionName + "' (available: " + availableNames() + ")");
                }
            }
        }

        auto ValidateInputConditionRefs(
            const std::vector<InputDeclaration> &inputs,
            const std::unordered_map<std::string, const ConditionDefinition *> &conditions,
            const fs::path &path) -> void
        {
            for (const auto &input : inputs)
            {
                ValidateSelectionConditionRefs(input.selectors, conditions, path);
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

        auto ValidateRuntimeConditionRefs(
            const RuntimeDefinition &runtime,
            const std::unordered_map<std::string, const ConditionDefinition *> &conditions,
            const fs::path &path) -> void
        {
            for (const auto &module : runtime.modules)
            {
                ValidateSelectionConditionRefs(module.selectors, conditions, path);
            }
            for (const auto &ref : runtime.enableModules)
            {
                ValidateSelectionConditionRefs(ref.selectors, conditions, path);
            }
            for (const auto &ref : runtime.disableModules)
            {
                ValidateSelectionConditionRefs(ref.selectors, conditions, path);
            }
            for (const auto &ref : runtime.enablePlugins)
            {
                ValidateSelectionConditionRefs(ref.selectors, conditions, path);
            }
            for (const auto &ref : runtime.disablePlugins)
            {
                ValidateSelectionConditionRefs(ref.selectors, conditions, path);
            }
        }

        auto ValidateGeneratorConditionRefs(
            const std::vector<GeneratorDeclaration> &generators,
            const std::unordered_map<std::string, const ConditionDefinition *> &conditions,
            const fs::path &path) -> void
        {
            for (const auto &generator : generators)
            {
                ValidateSelectionConditionRefs(generator.selectors, conditions, path);
                if (generator.hasInlineTool)
                {
                    ValidateSelectionConditionRefs(generator.inlineTool.selectors, conditions, path);
                }
                for (const auto &argument : generator.arguments)
                {
                    ValidateSelectionConditionRefs(argument.selectors, conditions, path);
                }
                ValidateInputConditionRefs(generator.inputs, conditions, path);
                ValidateInputConditionRefs(generator.outputs, conditions, path);
            }
        }

        auto ValidateReferenceConditionRefs(
            const std::vector<ProjectReference> &projectRefs,
            const std::vector<PackageReference> &packageRefs,
            const std::unordered_map<std::string, const ConditionDefinition *> &conditions,
            const fs::path &path) -> void
        {
            for (const auto &reference : projectRefs)
            {
                ValidateSelectionConditionRefs(reference.selectors, conditions, path);
            }
            for (const auto &reference : packageRefs)
            {
                ValidateSelectionConditionRefs(reference.selectors, conditions, path);
            }
        }

        auto ValidateFeatureConditionRefs(
            const std::vector<FeatureFlag> &features,
            const std::unordered_map<std::string, const ConditionDefinition *> &conditions,
            const fs::path &path) -> void
        {
            for (const auto &feature : features)
            {
                ValidateSelectionConditionRefs(feature.selectors, conditions, path);
            }
        }

        auto ValidatePackageFeatureUseConditionRefs(
            const std::vector<PackageFeatureUse> &uses,
            const std::unordered_map<std::string, const ConditionDefinition *> &conditions,
            const fs::path &path) -> void
        {
            for (const auto &use : uses)
            {
                ValidateSelectionConditionRefs(use.selectors, conditions, path);
            }
        }

        auto ValidateConditionReferences(const std::vector<ConditionDefinition> &conditionDefinitions, const fs::path &path) -> void
        {
            const auto conditions = ConditionMap(conditionDefinitions);
            for (const auto &condition : conditionDefinitions)
            {
                std::vector<std::string> refs{};
                CollectConditionRefs(condition.body, refs);
                for (const auto &ref : refs)
                {
                    if (!conditions.contains(ref))
                    {
                        SelectorSet selector{};
                        selector.conditionRefs.push_back(ref);
                        ValidateSelectionConditionRefs(selector, conditions, path);
                    }
                }
            }

            enum class VisitState
            {
                Visiting,
                Visited
            };
            std::unordered_map<std::string, VisitState> state{};
            std::vector<std::string> chain{};
            std::function<void(const std::string &)> visit = [&](const std::string &name)
            {
                if (const auto it = state.find(name); it != state.end())
                {
                    if (it->second == VisitState::Visiting)
                    {
                        std::ostringstream text{};
                        const auto begin = std::find(chain.begin(), chain.end(), name);
                        for (auto current = begin; current != chain.end(); ++current)
                        {
                            if (current != begin)
                            {
                                text << " -> ";
                            }
                            text << *current;
                        }
                        text << " -> " << name;
                        throw std::runtime_error(path.string() + ": condition reference cycle: " + text.str());
                    }
                    return;
                }
                state.emplace(name, VisitState::Visiting);
                chain.push_back(name);
                std::vector<std::string> refs{};
                CollectConditionRefs(conditions.at(name)->body, refs);
                for (const auto &ref : refs)
                {
                    visit(ref);
                }
                chain.pop_back();
                state[name] = VisitState::Visited;
            };
            for (const auto &condition : conditionDefinitions)
            {
                visit(condition.name);
            }
        }

        auto ValidateConditionReferences(const ProjectManifest &project, const fs::path &path) -> void
        {
            ValidateConditionReferences(project.conditions, path);
            const auto conditions = ConditionMap(project.conditions);

            ValidateInputConditionRefs(project.inputs, conditions, path);
            ValidateGeneratorConditionRefs(project.generators, conditions, path);
            ValidateReferenceConditionRefs(project.projectRefs, project.packageRefs, conditions, path);
            ValidatePackageFeatureUseConditionRefs(project.packageFeatureUses, conditions, path);
            ValidateRuntimeConditionRefs(project.runtime, conditions, path);
            for (const auto &environment : project.environments)
            {
                ValidateInputConditionRefs(environment.inputs, conditions, path);
                ValidateGeneratorConditionRefs(environment.generators, conditions, path);
                ValidateReferenceConditionRefs(environment.projectRefs, environment.packageRefs, conditions, path);
                ValidatePackageFeatureUseConditionRefs(environment.packageFeatureUses, conditions, path);
                ValidateFeatureConditionRefs(environment.features, conditions, path);
                ValidateRuntimeConditionRefs(environment.runtime, conditions, path);
            }
            for (const auto &profile : project.profiles)
            {
                ValidateInputConditionRefs(profile.inputs, conditions, path);
                ValidateGeneratorConditionRefs(profile.generators, conditions, path);
                ValidateReferenceConditionRefs(profile.projectRefs, profile.packageRefs, conditions, path);
                ValidatePackageFeatureUseConditionRefs(profile.packageFeatureUses, conditions, path);
                ValidateRuntimeConditionRefs(profile.runtime, conditions, path);
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

    [[nodiscard]] auto BuiltinConditions() -> std::vector<ConditionDefinition>
    {
        return {
            MatchCondition("Debug", {}, {}, {}, {}, "Debug"),
            MatchCondition("Release", {}, {}, {}, {}, "Release"),
            MatchCondition("RelWithDebInfo", {}, {}, {}, {}, "RelWithDebInfo"),
            MatchCondition("MinSizeRel", {}, {}, {}, {}, "MinSizeRel"),
            MatchCondition("Windows", {}, {}, "windows"),
            MatchCondition("Linux", {}, {}, "linux"),
            MatchCondition("MacOS", {}, {}, "macos"),
            MatchCondition("X64", {}, {}, {}, "x64"),
            MatchCondition("Arm64", {}, {}, {}, "arm64"),
            DesktopCondition(),
            MatchCondition("Local", {}, {}, {}, {}, {}, "local"),
            MatchCondition("Development", {}, {}, {}, {}, {}, "development"),
            MatchCondition("Production", {}, {}, {}, {}, {}, "production"),
        };
    }

    [[nodiscard]] auto SelectionMatches(const std::vector<ConditionDefinition> &conditions, const SelectorSet &selectors, const ProfileDefinition &profile) -> bool
    {
        if (!DirectSelectorsMatch(selectors, profile))
        {
            return false;
        }
        const auto conditionMap = ConditionMap(conditions);
        return std::all_of(
            selectors.conditionRefs.begin(),
            selectors.conditionRefs.end(),
            [&](const std::string &conditionName)
            { return ConditionRefMatches(conditionName, ProjectManifest{}, profile, conditionMap); });
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

    auto ParseDefinitionFragment(const fs::path &path, WorkspaceManifest &workspace) -> void
    {
        const auto doc = LoadXml(path);
        const auto *rootElement = doc.document.Root();
        if (rootElement == nullptr || rootElement->name != "Definitions")
        {
            throw std::runtime_error(path.string() + ": root element must be <Definitions>");
        }
        const auto schemaVersion = SchemaVersion(*rootElement, path);
        if (schemaVersion != "4")
        {
            throw std::runtime_error(path.string() + ": unsupported definitions SchemaVersion '" + schemaVersion + "' (expected '4')");
        }
        if (const auto *platforms = FindChild(*rootElement, "Platforms"))
        {
            for (const auto *node : ChildElements(*platforms, "Platform"))
            {
                WorkspaceManifest::Platform platform{};
                platform.name = RequireAttribute(*node, "Name", path);
                platform.operatingSystem = RequireAttribute(*node, "OperatingSystem", path);
                platform.architecture = RequireAttribute(*node, "Architecture", path);
                platform.abi = Attribute(*node, "Abi").value_or("");
                workspace.platforms.push_back(std::move(platform));
            }
        }
        if (const auto *toolchains = FindChild(*rootElement, "Toolchains"))
        {
            for (const auto *node : ChildElements(*toolchains, "Toolchain"))
            {
                WorkspaceManifest::Toolchain toolchain{};
                toolchain.name = RequireAttribute(*node, "Name", path);
                toolchain.compiler = Attribute(*node, "Compiler").value_or("");
                toolchain.compilerVersion = Attribute(*node, "CompilerVersion").value_or("");
                toolchain.linker = Attribute(*node, "Linker").value_or("");
                toolchain.generator = Attribute(*node, "Generator").value_or("");
                toolchain.cppStandardLibrary = Attribute(*node, "CppStandardLibrary").value_or("");
                toolchain.runtimeLibrary = Attribute(*node, "RuntimeLibrary").value_or("");
                workspace.toolchains.push_back(std::move(toolchain));
            }
        }
    }

    auto ApplyWorkspacePolicyPlatform(
        WorkspaceManifest::ProfilePolicy &policy,
        const WorkspaceManifest &workspace,
        std::string platformName) -> void
    {
        if (platformName.empty())
        {
            return;
        }
        policy.targetPlatform = platformName;
        if (platformName == "host")
        {
            return;
        }
        const auto workspacePlatform = std::find_if(
            workspace.platforms.begin(), workspace.platforms.end(),
            [&](const WorkspaceManifest::Platform &platform)
            {
                return platform.name == platformName;
            });
        if (workspacePlatform != workspace.platforms.end())
        {
            policy.operatingSystem = workspacePlatform->operatingSystem;
            policy.architecture = workspacePlatform->architecture;
            return;
        }
        const auto dash = platformName.find('-');
        if (dash != std::string::npos)
        {
            policy.operatingSystem = platformName.substr(0, dash);
            policy.architecture = platformName.substr(dash + 1);
        }
    }

    auto ParseWorkspaceDefaults(
        const XmlElement &defaultsNode,
        const fs::path &path,
        const WorkspaceManifest &workspace,
        WorkspaceManifest::ProfilePolicy &policy) -> void
    {
        for (const auto *node : ChildElements(defaultsNode))
        {
            if (node->name == "BuildType")
            {
                const auto value = RequireAttribute(*node, "Name", path);
                if (!IsSupportedBuildType(value))
                {
                    throw std::runtime_error(path.string() + ": unknown build type '" + value + "'");
                }
                policy.buildType = value;
            }
            else if (node->name == "TargetPlatform")
            {
                ApplyWorkspacePolicyPlatform(policy, workspace, RequireAttribute(*node, "Name", path));
            }
            else if (node->name == "HostPlatform")
            {
                policy.hostPlatform = RequireAttribute(*node, "Name", path);
            }
            else if (node->name == "Environment")
            {
                policy.environmentName = RequireAttribute(*node, "Name", path);
            }
            else if (node->name == "Toolchain")
            {
                policy.toolchain = RequireAttribute(*node, "Name", path);
            }
            else if (node->name == "Language")
            {
                auto standard = Attribute(*node, "Standard").value_or("");
                if (standard.rfind("C++", 0) == 0)
                {
                    standard.erase(0, 3);
                }
                if (!standard.empty())
                {
                    policy.language = "CXX";
                    policy.languageStandard = standard;
                }
            }
            else if (node->name == "Backend")
            {
                if (const auto name = Attribute(*node, "Name"); name.has_value() && !name->empty())
                {
                    policy.backend = *name;
                }
                if (const auto mode = Attribute(*node, "Mode"); mode.has_value() && !mode->empty())
                {
                    if (!IsSupportedProjectBuildMode(*mode))
                    {
                        throw std::runtime_error(path.string() + ": unknown project build mode '" + *mode + "'");
                    }
                    policy.buildMode = *mode;
                }
            }
        }
    }

    auto ParseWorkspaceBuildPolicy(
        const XmlElement &buildNode,
        const fs::path &path,
        WorkspaceManifest::ProfilePolicy &policy,
        const std::string &productKind = {}) -> void
    {
        for (const auto *node : ChildElements(buildNode))
        {
            WorkspaceManifest::ProfilePolicy::BuildSettingPolicy setting{};
            setting.productKind = productKind;
            if (node->name == "IncludePath")
            {
                setting.kind = "IncludePath";
                setting.value = RequireAttribute(*node, "Path", path);
                setting.visibility = Attribute(*node, "Visibility").value_or(setting.visibility);
            }
            else if (node->name == "Define")
            {
                const auto name = Attribute(*node, "Name").value_or("");
                if (name.empty())
                {
                    continue;
                }
                setting.kind = "Define";
                setting.value = name;
                if (const auto value = Attribute(*node, "Value"); value.has_value())
                {
                    setting.value += "=" + *value;
                }
                setting.visibility = Attribute(*node, "Visibility").value_or(setting.visibility);
            }
            else if (node->name == "CompileOption")
            {
                setting.kind = "CompileOption";
                setting.value = RequireAttribute(*node, "Value", path);
                setting.visibility = Attribute(*node, "Visibility").value_or(setting.visibility);
            }
            else if (node->name == "LinkOption" || node->name == "LinkLibrary")
            {
                setting.kind = "LinkOption";
                setting.value = Attribute(*node, "Value").value_or(Attribute(*node, "Name").value_or(""));
                setting.visibility = Attribute(*node, "Visibility").value_or(setting.visibility);
            }
            if (!setting.kind.empty() && !setting.value.empty())
            {
                policy.buildSettings.push_back(std::move(setting));
            }
        }
    }

    auto ParseWorkspaceQualityPolicy(
        const XmlElement &qualityNode,
        const fs::path &path,
        WorkspaceManifest::ProfilePolicy &policy,
        const std::string &productKind = {}) -> void
    {
        for (const auto *node : ChildElements(qualityNode, "Analyzer"))
        {
            WorkspaceManifest::ProfilePolicy::AnalyzerPolicy analyzer{};
            analyzer.productKind = productKind;
            analyzer.name = RequireAttribute(*node, "Name", path);
            analyzer.scope = Attribute(*node, "Scope").value_or(analyzer.scope);
            analyzer.enabled = BoolAttribute(*node, "Enabled", analyzer.enabled);
            analyzer.severity = Attribute(*node, "Severity").value_or(analyzer.severity);
            if (const auto *config = FindChild(*node, "Config"))
            {
                analyzer.configPath = RequireAttribute(*config, "Path", path);
            }
            policy.analyzers.push_back(std::move(analyzer));
        }
    }

    auto ParseWorkspaceEnvironmentPolicy(
        const XmlElement &environmentNode,
        const fs::path &path,
        WorkspaceManifest::ProfilePolicy &policy,
        const std::string &productKind = {}) -> void
    {
        for (const auto *node : ChildElements(environmentNode))
        {
            if (node->name != "Env" && node->name != "Secret")
            {
                continue;
            }

            WorkspaceManifest::ProfilePolicy::EnvironmentVariablePolicy variable{};
            variable.productKind = productKind;
            if (const auto remove = Attribute(*node, "Remove"); remove.has_value() && !remove->empty())
            {
                variable.name = *remove;
                variable.remove = true;
                policy.environmentVariables.push_back(std::move(variable));
                continue;
            }

            variable.name = RequireAttribute(*node, "Name", path);
            variable.required = BoolAttribute(*node, "Required", variable.required);
            if (node->name == "Env")
            {
                variable.value = Attribute(*node, "Value").value_or("");
            }
            else
            {
                variable.secret = true;
                variable.required = BoolAttribute(*node, "Required", false);
                const auto source = RequireAttribute(*node, "From", path);
                constexpr std::string_view localPrefix{"local:"};
                if (source.rfind(localPrefix, 0) != 0)
                {
                    throw std::runtime_error(path.string() + ": workspace Secret '" + variable.name + "' currently requires From=\"local:<key>\"");
                }
                variable.fromLocalSetting = source.substr(localPrefix.size());
            }
            policy.environmentVariables.push_back(std::move(variable));
        }
    }

    auto ParseWorkspaceStagePolicy(
        const XmlElement &stageNode,
        const fs::path &path,
        WorkspaceManifest::ProfilePolicy &policy,
        const std::string &productKind = {}) -> void
    {
        for (const auto *node : ChildElements(stageNode))
        {
            if (node->name != "Config" && node->name != "Content")
            {
                continue;
            }

            WorkspaceManifest::ProfilePolicy::StageInputPolicy input{};
            input.productKind = productKind;
            input.kind = std::string{node->name};
            if (const auto remove = Attribute(*node, "Remove"); remove.has_value() && !remove->empty())
            {
                input.target = *remove;
                input.remove = true;
                policy.stageInputs.push_back(std::move(input));
                continue;
            }
            input.source = RequireAttribute(*node, "Source", path);
            input.target = Attribute(*node, "Target").value_or("");
            input.collision = Attribute(*node, "Collision").value_or("");
            policy.stageInputs.push_back(std::move(input));
        }
    }

    auto ParseWorkspaceUsesPolicy(
        const XmlElement &usesNode,
        const fs::path &path,
        const WorkspaceManifest &workspace,
        WorkspaceManifest::ProfilePolicy &policy,
        const std::string &productKind = {}) -> void
    {
        for (const auto *node : ChildElements(usesNode))
        {
            if (node->name != "Project" && node->name != "Package" && node->name != "Tool" && node->name != "Runtime")
            {
                continue;
            }

            WorkspaceManifest::ProfilePolicy::DependencyUsePolicy use{};
            use.productKind = productKind;
            use.kind = std::string{node->name};
            if (const auto remove = Attribute(*node, "Remove"); remove.has_value() && !remove->empty())
            {
                use.name = *remove;
                use.remove = true;
                policy.dependencyUses.push_back(std::move(use));
                continue;
            }

            use.name = RequireAttribute(*node, "Name", path);
            use.versionRange = Attribute(*node, "Version").value_or(Attribute(*node, "VersionRange").value_or(""));
            use.scope = ScopeMutation(*node, use.kind == "Tool" ? "Build" : "");
            use.removeScope = Attribute(*node, "RemoveScope").value_or("");
            if (const auto dependencyPath = Attribute(*node, "Path"); dependencyPath.has_value() && !dependencyPath->empty())
            {
                use.path = (workspace.path.parent_path() / *dependencyPath).lexically_normal();
            }
            for (const auto *feature : ChildElements(*node, "Feature"))
            {
                if (const auto featureName = Attribute(*feature, "Name"); featureName.has_value() && !featureName->empty())
                {
                    use.features.push_back(*featureName);
                }
            }
            policy.dependencyUses.push_back(std::move(use));
        }
    }

    auto ParseWorkspaceRuntimePolicy(
        const XmlElement &runtimeNode,
        const fs::path &path,
        WorkspaceManifest::ProfilePolicy &policy,
        const std::string &productKind = {}) -> void
    {
        for (const auto *node : ChildElements(runtimeNode, "Module"))
        {
            WorkspaceManifest::ProfilePolicy::RuntimeModulePolicy module{};
            module.productKind = productKind;
            if (const auto remove = Attribute(*node, "Remove"); remove.has_value() && !remove->empty())
            {
                module.name = *remove;
                module.remove = true;
                policy.runtimeModules.push_back(std::move(module));
                continue;
            }

            module.name = RequireAttribute(*node, "Name", path);
            module.stage = Attribute(*node, "Stage").value_or(module.stage);
            for (const auto *provides : ChildElements(*node, "Provides"))
            {
                if (const auto service = Attribute(*provides, "Service"); service.has_value() && !service->empty())
                {
                    module.providesServices.push_back(*service);
                }
            }
            for (const auto *requirement : ChildElements(*node, "Requires"))
            {
                if (const auto service = Attribute(*requirement, "Service"); service.has_value() && !service->empty())
                {
                    module.requiresServices.push_back(*service);
                }
            }
            policy.runtimeModules.push_back(std::move(module));
        }
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
        const auto schemaVersion = SchemaVersion(*rootElement, *path);
        if (schemaVersion != "4")
        {
            throw std::runtime_error(path->string() + ": unsupported workspace SchemaVersion '" + schemaVersion + "' (expected '4')");
        }

        WorkspaceManifest workspace{};
        workspace.path = fs::weakly_canonical(*path);
        workspace.name = RequireAttribute(*rootElement, "Name", *path);
        workspace.defaultProfile = Attribute(*rootElement, "DefaultProfile").value_or("");
        workspace.platformVersion = Attribute(*rootElement, "PlatformVersion").value_or("0.1.0");

        if (const auto *imports = FindChild(*rootElement, "Imports"))
        {
            for (const auto *import : ChildElements(*imports, "Import"))
            {
                const auto importPath = (workspace.path.parent_path() / RequireAttribute(*import, "Path", *path)).lexically_normal();
                workspace.imports.push_back(importPath);
                ParseDefinitionFragment(importPath, workspace);
            }
        }
        if (const auto *defaults = FindChild(*rootElement, "Defaults"))
        {
            ParseWorkspaceDefaults(*defaults, *path, workspace, workspace.defaults);
        }
        if (const auto *packagesNode = FindChild(*rootElement, "Packages"))
        {
            for (const auto *source : ChildElements(*packagesNode, "Source"))
            {
                if (const auto sourcePath = Attribute(*source, "Path"); sourcePath.has_value() && !sourcePath->empty())
                {
                    workspace.packageSources.push_back((workspace.path.parent_path() / *sourcePath).lexically_normal());
                }
                if (const auto sourceUrl = Attribute(*source, "Url"); sourceUrl.has_value() && !sourceUrl->empty())
                {
                    workspace.packageSourceUrls.push_back(*sourceUrl);
                }
            }
            for (const auto *provider : ChildElements(*packagesNode, "PackageProvider"))
            {
                const auto name = RequireAttribute(*provider, "Name", *path);
                const auto providerRoot = Attribute(*provider, "Root").value_or(Attribute(*provider, "Path").value_or(""));
                if (!providerRoot.empty())
                {
                    workspace.packageProviders[name] = (workspace.path.parent_path() / providerRoot).lexically_normal();
                }
            }
            for (const auto *version : ChildElements(*packagesNode, "Version"))
            {
                workspace.dependencyVersions[RequireAttribute(*version, "Name", *path)] = Attribute(*version, "Range").value_or(Attribute(*version, "Version").value_or(""));
            }
        }

        if (const auto *projectsNode = FindChild(*rootElement, "Projects"))
        {
            for (const auto *node : ChildElements(*projectsNode, "Project"))
            {
                workspace.projects.push_back((workspace.path.parent_path() / RequireAttribute(*node, "Path", *path)).lexically_normal());
            }
        }
        if (const auto *profiles = FindChild(*rootElement, "Profiles"))
        {
            for (const auto *node : ChildElements(*profiles, "Profile"))
            {
                WorkspaceManifest::ProfilePolicy profile{};
                profile.name = RequireAttribute(*node, "Name", *path);
                if (const auto *defaults = FindChild(*node, "Defaults"))
                {
                    ParseWorkspaceDefaults(*defaults, *path, workspace, profile);
                }
                if (const auto *build = FindChild(*node, "Build"))
                {
                    ParseWorkspaceBuildPolicy(*build, *path, profile);
                }
                if (const auto *quality = FindChild(*node, "Quality"))
                {
                    ParseWorkspaceQualityPolicy(*quality, *path, profile);
                }
                if (const auto *environment = FindChild(*node, "Environment"))
                {
                    ParseWorkspaceEnvironmentPolicy(*environment, *path, profile);
                }
                if (const auto *stage = FindChild(*node, "Stage"))
                {
                    ParseWorkspaceStagePolicy(*stage, *path, profile);
                }
                if (const auto *uses = FindChild(*node, "Uses"))
                {
                    ParseWorkspaceUsesPolicy(*uses, *path, workspace, profile);
                }
                if (const auto *runtime = FindChild(*node, "Runtime"))
                {
                    ParseWorkspaceRuntimePolicy(*runtime, *path, profile);
                }
                for (const auto *productOverlay : ChildElements(*node))
                {
                    if (!IsProductElementName(productOverlay->name))
                    {
                        continue;
                    }
                    if (const auto *build = FindChild(*productOverlay, "Build"))
                    {
                        ParseWorkspaceBuildPolicy(*build, *path, profile, std::string{productOverlay->name});
                    }
                    if (const auto *quality = FindChild(*productOverlay, "Quality"))
                    {
                        ParseWorkspaceQualityPolicy(*quality, *path, profile, std::string{productOverlay->name});
                    }
                    if (const auto *environment = FindChild(*productOverlay, "Environment"))
                    {
                        ParseWorkspaceEnvironmentPolicy(*environment, *path, profile, std::string{productOverlay->name});
                    }
                    if (const auto *stage = FindChild(*productOverlay, "Stage"))
                    {
                        ParseWorkspaceStagePolicy(*stage, *path, profile, std::string{productOverlay->name});
                    }
                    if (const auto *uses = FindChild(*productOverlay, "Uses"))
                    {
                        ParseWorkspaceUsesPolicy(*uses, *path, workspace, profile, std::string{productOverlay->name});
                    }
                    if (const auto *runtime = FindChild(*productOverlay, "Runtime"))
                    {
                        ParseWorkspaceRuntimePolicy(*runtime, *path, profile, std::string{productOverlay->name});
                    }
                }
                workspace.profiles.push_back(std::move(profile));
            }
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
        const auto networkCacheRoot = NetworkPackageCacheRoot(workspace, projectPath);
        auto addManifest = [&](const fs::path &manifestPath)
        {
            const auto canonicalManifestPath = fs::weakly_canonical(manifestPath);
            const auto manifest = LoadPackageManifest(canonicalManifestPath);
            fs::path providerRoot{};
            if (canonicalManifestPath.extension() == ".nginpack")
            {
                providerRoot = networkCacheRoot / "extracted" / manifest.name / manifest.version;
                fs::remove_all(providerRoot);
                ExtractZipFile(canonicalManifestPath, providerRoot);
            }
            if (workspace.has_value())
            {
                if (const auto provider = workspace->packageProviders.find(manifest.name); provider != workspace->packageProviders.end())
                {
                    providerRoot = provider->second;
                }
            }
            out.emplace(manifest.name, PackageCatalogEntry{
                                           .name = manifest.name,
                                           .manifestPath = canonicalManifestPath,
                                           .providerRoot = providerRoot,
                                       });
        };
        auto addFeedIndex = [&](const fs::path &feedPath, const std::optional<std::string> &sourceUrl = std::nullopt)
        {
            const auto feed = LoadXml(feedPath);
            const auto *rootElement = feed.document.Root();
            if (rootElement == nullptr || rootElement->name != "PackageFeed")
            {
                throw std::runtime_error(feedPath.string() + ": root element must be <PackageFeed>");
            }
            const auto schemaVersion = SchemaVersion(*rootElement, feedPath);
            if (schemaVersion != "4")
            {
                throw std::runtime_error(feedPath.string() + ": unsupported package feed SchemaVersion '" + schemaVersion + "' (expected '4')");
            }
            const auto *packagesElement = FindChild(*rootElement, "Packages");
            if (packagesElement == nullptr)
            {
                throw std::runtime_error(feedPath.string() + ": missing <Packages>");
            }
            for (const auto *packageElement : ChildElements(*packagesElement, "Package"))
            {
                const auto relativeManifestPath = RequireAttribute(*packageElement, "Path", feedPath);
                fs::path manifestPath{};
                if (sourceUrl.has_value())
                {
                    const auto artifactUrl = JoinUrl(UrlParent(*sourceUrl), relativeManifestPath);
                    const auto packageName = Attribute(*packageElement, "Name").value_or(SafeCacheFileName(artifactUrl, ""));
                    const auto packageVersion = Attribute(*packageElement, "Version").value_or("unknown");
                    auto fileName = fs::path(relativeManifestPath).filename();
                    if (fileName.empty())
                    {
                        fileName = SafeCacheFileName(artifactUrl, ".nginpack");
                    }
                    manifestPath = networkCacheRoot / "artifacts" / packageName / packageVersion / fileName;
                    DownloadUrlToFile(artifactUrl, manifestPath);
                }
                else
                {
                    manifestPath = fs::path(relativeManifestPath).is_absolute()
                                       ? fs::path(relativeManifestPath).lexically_normal()
                                       : (feedPath.parent_path() / relativeManifestPath).lexically_normal();
                }
                addManifest(manifestPath);
            }
        };
        auto packageRoots = workspace.has_value() ? workspace->packageSources : DiscoverPackageSourceRoots(projectPath);
        if (workspace.has_value())
        {
            for (const auto &sourceUrl : workspace->packageSourceUrls)
            {
                constexpr std::string_view fileScheme{"file://"};
                if (sourceUrl.rfind(fileScheme, 0) == 0)
                {
                    packageRoots.push_back(fs::path(sourceUrl.substr(fileScheme.size())).lexically_normal());
                }
                else if (IsHttpUrl(sourceUrl))
                {
                    const auto feedPath = networkCacheRoot / "feeds" / SafeCacheFileName(sourceUrl, ".nginfeed");
                    DownloadUrlToFile(sourceUrl, feedPath);
                    addFeedIndex(feedPath, sourceUrl);
                }
            }
        }
        for (const auto &packageRoot : packageRoots)
        {
            if (!fs::exists(packageRoot))
            {
                continue;
            }
            if (fs::is_regular_file(packageRoot))
            {
                if (packageRoot.extension() == ".nginpkg" || packageRoot.extension() == ".nginpack")
                {
                    addManifest(packageRoot);
                    continue;
                }
                addFeedIndex(packageRoot);
                continue;
            }
            for (const auto &entry : fs::recursive_directory_iterator(packageRoot))
            {
                if (!entry.is_regular_file() || (entry.path().extension() != ".nginpkg" && entry.path().extension() != ".nginpack"))
                {
                    continue;
                }
                addManifest(entry.path());
            }
        }
        return out;
    }

    [[nodiscard]] auto ExtractNginPackManifest(const fs::path &path) -> std::string
    {
        return ReadZipEntry(path, "package.nginpkg");
    }

    [[nodiscard]] auto LoadPackageManifest(const fs::path &path) -> PackageManifest
    {
        const auto doc = path.extension() == ".nginpack"
                             ? LoadXmlText(ExtractNginPackManifest(path), path.string() + ":package.nginpkg")
                             : LoadXml(path);
        const auto *rootElement = doc.document.Root();
        if (rootElement == nullptr || rootElement->name != "Package")
        {
            throw std::runtime_error(path.string() + ": root element must be <Package>");
        }
        const auto schemaVersion = SchemaVersion(*rootElement, path);
        if (schemaVersion != "4")
        {
            throw std::runtime_error(path.string() + ": unsupported package SchemaVersion '" + schemaVersion + "' (expected '4')");
        }

        PackageManifest package{};
        package.path = path;
        package.name = RequireAttribute(*rootElement, "Name", path);
        package.version = RequireAttribute(*rootElement, "Version", path);
        package.compatiblePlatformRange = Attribute(*rootElement, "CompatiblePlatformRange").value_or("");

        package.conditions = BuiltinConditions();
        LoadPackageBuildDescriptor(package.build, FindChild(*rootElement, "Build"), path);
        package.compatibility = ParseCompatibility(*rootElement, path);

        if (const auto *uses = FindChild(*rootElement, "Uses"))
        {
            for (const auto *dependency : ChildElements(*uses))
            {
                if (dependency->name != "Package" && dependency->name != "Tool" && dependency->name != "Runtime")
                {
                    continue;
                }
                PackageDependency parsed{};
                parsed.name = RequireAttribute(*dependency, "Name", path);
                parsed.versionRange = Attribute(*dependency, "Version").value_or(Attribute(*dependency, "VersionRange").value_or(""));
                parsed.optional = BoolAttribute(*dependency, "Optional");
                parsed.scope = Attribute(*dependency, "Scope").value_or(dependency->name == "Tool" ? "Build" : "");
                package.dependencies.push_back(std::move(parsed));
            }
        }

        if (const auto *library = FindChild(*rootElement, "Library"))
        {
            const auto libraryName = Attribute(*library, "Name").value_or(package.name);
            if (package.build.mode.empty())
            {
                LoadPackageBuildDescriptor(package.build, FindChild(*library, "Build"), path);
            }
            if (const auto *exports = FindChild(*library, "Exports"))
            {
                for (const auto *target : ChildElements(*exports, "LibraryTarget"))
                {
                    LibraryArtifact artifact{};
                    artifact.name = libraryName;
                    artifact.target = RequireAttribute(*target, "Name", path);
                    artifact.linkage = Attribute(*target, "Linkage").value_or("");
                    artifact.exported = true;
                    package.artifacts.libraries.push_back(std::move(artifact));
                }
                for (const auto *binary : ChildElements(*exports, "Binary"))
                {
                    LibraryArtifact artifact{};
                    artifact.name = libraryName;
                    artifact.origin = RequireAttribute(*binary, "Path", path);
                    artifact.linkage = Attribute(*binary, "Linkage").value_or("");
                    artifact.exported = true;
                    package.artifacts.libraries.push_back(std::move(artifact));
                }
                for (const auto *headers : ChildElements(*exports, "Headers"))
                {
                    InputDeclaration input{};
                    input.kind = "Source";
                    input.role = "Header";
                    input.visibility = "Public";
                    input.declaringScope = "package:" + package.name;
                    const auto headerPath = RequireAttribute(*headers, "Path", path);
                    if (headerPath.find('*') != std::string::npos || headerPath.find('?') != std::string::npos)
                    {
                        input.pattern = headerPath;
                        input.includePatterns.push_back(headerPath);
                        input.mode = "Glob";
                    }
                    else
                    {
                        input.path = headerPath;
                        input.mode = "Directory";
                    }
                    ValidateInputDeclaration(input, path);
                    package.inputs.push_back(std::move(input));
                }
            }
        }

        if (const auto *toolProduct = FindChild(*rootElement, "Tool"))
        {
            const auto toolProductName = Attribute(*toolProduct, "Name").value_or(package.name);
            if (package.build.mode.empty())
            {
                LoadPackageBuildDescriptor(package.build, FindChild(*toolProduct, "Build"), path);
            }
            if (const auto *exports = FindChild(*toolProduct, "Exports"))
            {
                for (const auto *tool : ChildElements(*exports, "Tool"))
                {
                    ToolDeclaration declaration{};
                    declaration.name = Attribute(*tool, "Name").value_or(toolProductName);
                    declaration.kind = Attribute(*tool, "Kind").value_or("Generator");
                    declaration.executable = RequireAttribute(*tool, "Executable", path);
                    package.tools.push_back(declaration);

                    ExecutableArtifact artifact{};
                    artifact.name = declaration.name;
                    artifact.target = declaration.executable;
                    artifact.exported = true;
                    package.artifacts.executables.push_back(std::move(artifact));
                }
            }
        }

        ParsePackageFeatureDeclarations(*rootElement, path, package);

        ValidateConditionReferences(package.conditions, path);
        {
            const auto conditions = ConditionMap(package.conditions);
            ValidateInputConditionRefs(package.inputs, conditions, path);
            for (const auto &tool : package.tools)
            {
                ValidateSelectionConditionRefs(tool.selectors, conditions, path);
            }
            for (const auto &module : package.modules)
            {
                ValidateSelectionConditionRefs(module.selectors, conditions, path);
            }
            for (const auto &plugin : package.plugins)
            {
                ValidateSelectionConditionRefs(plugin.selectors, conditions, path);
            }
            for (const auto &feature : package.features)
            {
                ValidateSelectionConditionRefs(feature.selectors, conditions, path);
                ValidateReferenceConditionRefs({}, feature.packageRefs, conditions, path);
                ValidateInputConditionRefs(feature.inputs, conditions, path);
                ValidateBuildSettingConditionRefs(feature.build.includeDirectories, conditions, path);
                ValidateBuildSettingConditionRefs(feature.build.compileDefinitions, conditions, path);
                ValidateBuildSettingConditionRefs(feature.build.compileOptions, conditions, path);
                ValidateBuildSettingConditionRefs(feature.build.linkOptions, conditions, path);
                ValidateRuntimeConditionRefs(feature.runtime, conditions, path);
                ValidateGeneratorConditionRefs(feature.generators, conditions, path);
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
        [[nodiscard]] auto IsProductElementName(const std::string_view name) -> bool
        {
            return name == "Application" || name == "Library" || name == "Tool" || name == "Test"
                   || name == "Benchmark" || name == "Plugin" || name == "Module" || name == "External";
        }

        [[nodiscard]] auto DefaultOutputKind(const std::string_view productKind, const XmlElement &product) -> std::string
        {
            if (const auto output = Attribute(product, "Output"); output.has_value() && !output->empty())
            {
                if (*output == "Static")
                {
                    return "StaticLibrary";
                }
                if (*output == "Shared")
                {
                    return "SharedLibrary";
                }
                if (*output == "Executable")
                {
                    return "Executable";
                }
                return *output;
            }
            if (productKind == "Library" || productKind == "Module" || productKind == "External")
            {
                return "StaticLibrary";
            }
            if (productKind == "Plugin")
            {
                return "SharedLibrary";
            }
            return "Executable";
        }

        [[nodiscard]] auto ProjectType(const std::string_view productKind) -> std::string
        {
            if (productKind == "Library" || productKind == "Plugin" || productKind == "Module" || productKind == "External")
            {
                return "Library";
            }
            if (productKind == "Tool")
            {
                return "Tool";
            }
            return "Application";
        }

        [[nodiscard]] auto ProfileWithPlatform(
            std::string name,
            std::string buildType,
            std::string platform,
            std::string environment) -> ProfileDefinition
        {
            ProfileDefinition profile{};
            profile.name = std::move(name);
            profile.buildType = buildType.empty() ? "Debug" : std::move(buildType);
            profile.platform = platform.empty() || platform == "host" ? "linux-x64" : std::move(platform);
            if (profile.platform == "windows-x64")
            {
                profile.operatingSystem = "windows";
                profile.architecture = "x64";
            }
            else if (profile.platform == "macos-x64")
            {
                profile.operatingSystem = "macos";
                profile.architecture = "x64";
            }
            else if (profile.platform == "macos-arm64")
            {
                profile.operatingSystem = "macos";
                profile.architecture = "arm64";
            }
            else if (profile.platform == "linux-arm64")
            {
                profile.operatingSystem = "linux";
                profile.architecture = "arm64";
            }
            else
            {
                profile.platform = "linux-x64";
                profile.operatingSystem = "linux";
                profile.architecture = "x64";
            }
            profile.environmentName = environment.empty() ? "development" : std::move(environment);
            return profile;
        }

        [[nodiscard]] auto PathInput(
            std::string kind,
            std::string role,
            const std::string &pathValue,
            std::string visibility,
            const std::string &target,
            const std::string &scope) -> InputDeclaration
        {
            InputDeclaration input{};
            input.kind = std::move(kind);
            input.role = std::move(role);
            input.visibility = std::move(visibility);
            input.declaringScope = scope;
            input.target = target;
            if (pathValue.find('*') != std::string::npos || pathValue.find('?') != std::string::npos)
            {
                input.includePatterns.push_back(pathValue);
                input.target.clear();
                input.targetRoot = target;
                input.mode = "Glob";
            }
            else if (input.kind == "Source")
            {
                input.path = pathValue;
                input.mode = "Directory";
            }
            else
            {
                input.path = pathValue;
                input.mode = "File";
            }
            return input;
        }

        auto ApplyScopeSelector(const std::string &scope, SelectorSet &selectors) -> void
        {
            constexpr std::string_view profilePrefix{"profile:"};
            if (scope.rfind(profilePrefix, 0) != 0)
            {
                return;
            }
            auto profileName = scope.substr(profilePrefix.size());
            if (const auto separator = profileName.find(':'); separator != std::string::npos)
            {
                profileName = profileName.substr(0, separator);
            }
            if (!profileName.empty())
            {
                selectors.profile = std::move(profileName);
            }
        }

        [[nodiscard]] auto DefineIdentity(const std::string &value) -> std::string
        {
            if (const auto separator = value.find('='); separator != std::string::npos)
            {
                return value.substr(0, separator);
            }
            return value;
        }

        auto RequireUniqueLocalName(
            std::set<std::string> &names,
            const std::string &kind,
            const std::string &name,
            const fs::path &path) -> void
        {
            if (!names.insert(name).second)
            {
                throw std::runtime_error(path.string() + ": duplicate " + kind + " '" + name + "' in the same overlay scope");
            }
        }

        auto RemoveDefine(std::vector<BuildSetting> &settings, const std::string &name, const std::string &scope) -> void
        {
            settings.erase(
                std::remove_if(
                    settings.begin(),
                    settings.end(),
                    [&](const BuildSetting &setting)
                    {
                        if (DefineIdentity(setting.value) != name)
                        {
                            return false;
                        }
                        if (scope.rfind("profile:", 0) != 0)
                        {
                            return true;
                        }
                        SelectorSet scopeSelectors{};
                        ApplyScopeSelector(scope, scopeSelectors);
                        return setting.selectors.profile == scopeSelectors.profile;
                    }),
                settings.end());
        }

        auto UpsertDefine(std::vector<BuildSetting> &settings, BuildSetting setting, const std::string &scope) -> void
        {
            const auto identity = DefineIdentity(setting.value);
            ApplyScopeSelector(scope, setting.selectors);
            RemoveDefine(settings, identity, scope);
            settings.push_back(std::move(setting));
        }

        [[nodiscard]] auto StageIdentity(const InputDeclaration &input) -> std::string
        {
            if (!input.target.empty())
            {
                return input.kind + ":" + input.target;
            }
            if (!input.targetRoot.empty())
            {
                return input.kind + ":" + input.targetRoot;
            }
            if (!input.path.empty())
            {
                return input.kind + ":" + input.path;
            }
            if (!input.includePatterns.empty())
            {
                return input.kind + ":" + input.includePatterns.front();
            }
            return input.kind + ":";
        }

        auto RemoveStageInput(std::vector<InputDeclaration> &inputs, const std::string &kind, const std::string &identity, const std::string &scope) -> void
        {
            inputs.erase(
                std::remove_if(
                    inputs.begin(),
                    inputs.end(),
                    [&](const InputDeclaration &input)
                    {
                        if (input.kind != kind)
                        {
                            return false;
                        }
                        const auto inputIdentity = StageIdentity(input);
                        const auto matchesIdentity = inputIdentity == kind + ":" + identity
                                                     || input.target == identity
                                                     || input.targetRoot == identity
                                                     || input.path == identity
                                                     || (!input.includePatterns.empty() && input.includePatterns.front() == identity);
                        if (!matchesIdentity)
                        {
                            return false;
                        }
                        if (scope.rfind("profile:", 0) != 0)
                        {
                            return true;
                        }
                        SelectorSet scopeSelectors{};
                        ApplyScopeSelector(scope, scopeSelectors);
                        return !input.selectors.profile.has_value() || input.selectors.profile == scopeSelectors.profile;
                    }),
                inputs.end());
        }

        auto UpsertStageInput(std::vector<InputDeclaration> &inputs, InputDeclaration input, const std::string &scope) -> void
        {
            ApplyScopeSelector(scope, input.selectors);
            inputs.push_back(std::move(input));
        }

        auto RemoveEnvironmentVariable(std::vector<EnvironmentVariable> &variables, const std::string &name) -> void
        {
            variables.erase(
                std::remove_if(
                    variables.begin(),
                    variables.end(),
                    [&](const EnvironmentVariable &variable)
                    { return variable.name == name; }),
                variables.end());
        }

        auto UpsertEnvironmentVariable(std::vector<EnvironmentVariable> &variables, EnvironmentVariable variable) -> void
        {
            RemoveEnvironmentVariable(variables, variable.name);
            variables.push_back(std::move(variable));
        }

        auto RemoveRuntimeModule(RuntimeDefinition &runtime, const std::string &name) -> void
        {
            runtime.modules.erase(
                std::remove_if(
                    runtime.modules.begin(),
                    runtime.modules.end(),
                    [&](const ModuleDescriptor &module)
                    { return module.name == name; }),
                runtime.modules.end());
            runtime.enableModules.erase(
                std::remove_if(
                    runtime.enableModules.begin(),
                    runtime.enableModules.end(),
                    [&](const RuntimeReference &reference)
                    { return reference.name == name; }),
                runtime.enableModules.end());
            runtime.disableModules.push_back(RuntimeReference{.name = name});
        }

        auto UpsertRuntimeModule(RuntimeDefinition &runtime, ModuleDescriptor module) -> void
        {
            const auto name = module.name;
            RemoveRuntimeModule(runtime, name);
            runtime.disableModules.erase(
                std::remove_if(
                    runtime.disableModules.begin(),
                    runtime.disableModules.end(),
                    [&](const RuntimeReference &reference)
                    { return reference.name == name; }),
                runtime.disableModules.end());
            runtime.enableModules.push_back(RuntimeReference{.name = name});
            runtime.modules.push_back(std::move(module));
        }

        auto RemoveGenerator(std::vector<GeneratorDeclaration> &generators, const std::string &name) -> void
        {
            generators.erase(
                std::remove_if(
                    generators.begin(),
                    generators.end(),
                    [&](const GeneratorDeclaration &generator)
                    {
                        return generator.name == name;
                    }),
                generators.end());
        }

        auto UpsertGenerator(std::vector<GeneratorDeclaration> &generators, GeneratorDeclaration generator) -> void
        {
            RemoveGenerator(generators, generator.name);
            generators.push_back(std::move(generator));
        }

        auto AddPathInput(
            std::vector<InputDeclaration> &inputs,
            const fs::path &manifestPath,
            std::string kind,
            std::string role,
            const std::string &pathValue,
            std::string visibility,
            const std::string &target,
            const std::string &scope) -> void
        {
            auto input = PathInput(std::move(kind), std::move(role), pathValue, std::move(visibility), target, scope);
            ApplyScopeSelector(scope, input.selectors);
            ValidateInputDeclaration(input, manifestPath);
            inputs.push_back(std::move(input));
        }

        auto ParseLanguage(const XmlElement &node, const fs::path &path, ProjectBuildDescriptor &build) -> void
        {
            ValidateAllowedAttributes(node, path, {"Standard", "Required", "Extensions"});
            auto standard = Attribute(node, "Standard").value_or("");
            if (standard.rfind("C++", 0) == 0)
            {
                standard.erase(0, 3);
            }
            if (!standard.empty())
            {
                build.language = "CXX";
                build.languageStandard = standard;
                build.languageExplicit = true;
            }
        }

        auto ApplyWhenSelector(const XmlElement &node, SelectorSet &selectors) -> void
        {
            if (const auto when = Attribute(node, "When"); when.has_value() && !when->empty())
            {
                selectors.conditionRefs.push_back(*when);
            }
        }

        auto ParseBuildSection(const XmlElement &buildNode, const fs::path &path, ProjectManifest &project, const std::string &scope) -> void
        {
            for (const auto *node : ChildElements(buildNode))
            {
                if (node->name == "Language")
                {
                    ParseLanguage(*node, path, project.build);
                    continue;
                }
                if (node->name == "Sources")
                {
                    AddPathInput(project.inputs, path, "Source", "Source", RequireAttribute(*node, "Path", path), "Private", "", scope);
                    continue;
                }
                if (node->name == "Headers")
                {
                    AddPathInput(project.inputs, path, "Source", "Header", RequireAttribute(*node, "Path", path), Attribute(*node, "Visibility").value_or("Public"), "", scope);
                    continue;
                }
                if (node->name == "IncludePath")
                {
                    BuildSetting setting{};
                    setting.value = RequireAttribute(*node, "Path", path);
                    setting.visibility = Attribute(*node, "Visibility").value_or("Private");
                    ApplyScopeSelector(scope, setting.selectors);
                    ApplyWhenSelector(*node, setting.selectors);
                    project.build.includeDirectories.push_back(std::move(setting));
                    continue;
                }
                if (node->name == "Define")
                {
                    BuildSetting setting{};
                    if (const auto remove = Attribute(*node, "Remove"); remove.has_value() && !remove->empty())
                    {
                        RemoveDefine(project.build.compileDefinitions, *remove, scope);
                        continue;
                    }
                    const auto name = Attribute(*node, "Name").value_or("");
                    if (name.empty())
                    {
                        continue;
                    }
                    setting.value = name;
                    if (const auto value = Attribute(*node, "Value"); value.has_value())
                    {
                        setting.value += "=" + *value;
                    }
                    setting.visibility = Attribute(*node, "Visibility").value_or("Private");
                    ApplyWhenSelector(*node, setting.selectors);
                    UpsertDefine(project.build.compileDefinitions, std::move(setting), scope);
                    continue;
                }
                if (node->name == "CompileOption")
                {
                    BuildSetting setting{};
                    setting.value = RequireAttribute(*node, "Value", path);
                    setting.visibility = Attribute(*node, "Visibility").value_or("Private");
                    ApplyScopeSelector(scope, setting.selectors);
                    ApplyWhenSelector(*node, setting.selectors);
                    project.build.compileOptions.push_back(std::move(setting));
                    continue;
                }
                if (node->name == "LinkOption" || node->name == "LinkLibrary")
                {
                    BuildSetting setting{};
                    setting.value = Attribute(*node, "Value").value_or(Attribute(*node, "Name").value_or(""));
                    if (!setting.value.empty())
                    {
                        setting.visibility = Attribute(*node, "Visibility").value_or("Private");
                        ApplyScopeSelector(scope, setting.selectors);
                        ApplyWhenSelector(*node, setting.selectors);
                        project.build.linkOptions.push_back(std::move(setting));
                    }
                    continue;
                }
            }
        }

        auto ParseExportsSection(const XmlElement &exportsNode, const fs::path &path, ProjectManifest &project, const std::string &scope) -> void
        {
            for (const auto *node : ChildElements(exportsNode))
            {
                if (node->name == "LibraryTarget")
                {
                    project.output.target = RequireAttribute(*node, "Name", path);
                    continue;
                }
                if (node->name == "IncludePath")
                {
                    BuildSetting setting{};
                    setting.value = RequireAttribute(*node, "Path", path);
                    setting.visibility = "Interface";
                    ApplyScopeSelector(scope, setting.selectors);
                    ApplyWhenSelector(*node, setting.selectors);
                    project.build.includeDirectories.push_back(std::move(setting));
                    continue;
                }
                if (node->name == "Headers")
                {
                    AddPathInput(project.inputs, path, "Source", "Header", RequireAttribute(*node, "Path", path), "Public", "", scope);
                    continue;
                }
                if (node->name == "Define")
                {
                    const auto name = Attribute(*node, "Name").value_or("");
                    if (name.empty())
                    {
                        continue;
                    }
                    BuildSetting setting{};
                    setting.value = name;
                    if (const auto value = Attribute(*node, "Value"); value.has_value())
                    {
                        setting.value += "=" + *value;
                    }
                    setting.visibility = "Interface";
                    ApplyWhenSelector(*node, setting.selectors);
                    UpsertDefine(project.build.compileDefinitions, std::move(setting), scope);
                    continue;
                }
                if (node->name == "LinkOption")
                {
                    BuildSetting setting{};
                    setting.value = RequireAttribute(*node, "Value", path);
                    setting.visibility = "Interface";
                    ApplyScopeSelector(scope, setting.selectors);
                    ApplyWhenSelector(*node, setting.selectors);
                    project.build.linkOptions.push_back(std::move(setting));
                    continue;
                }
            }
        }

        auto ParseUsesSection(
            const XmlElement &usesNode,
            const fs::path &path,
            ProjectManifest &project,
            const std::string &scope = {}) -> void
        {
            for (const auto *node : ChildElements(usesNode))
            {
                if (node->name == "Project")
                {
                    ProjectReference reference{};
                    if (const auto remove = Attribute(*node, "Remove"); remove.has_value() && !remove->empty())
                    {
                        reference.path = (path.parent_path() / *remove).lexically_normal();
                        reference.disabled = true;
                        ApplyScopeSelector(scope, reference.selectors);
                        project.projectRefs.push_back(std::move(reference));
                        continue;
                    }
                    reference.path = (path.parent_path() / RequireAttribute(*node, "Path", path)).lexically_normal();
                    if (const auto profile = Attribute(*node, "Profile"); profile.has_value() && !profile->empty())
                    {
                        reference.profile = *profile;
                    }
                    ApplyScopeSelector(scope, reference.selectors);
                    project.projectRefs.push_back(std::move(reference));
                    continue;
                }
                if (node->name == "Package" || node->name == "Tool" || node->name == "Runtime")
                {
                    if (const auto remove = Attribute(*node, "Remove"); remove.has_value() && !remove->empty())
                    {
                        PackageReference package{};
                        package.name = *remove;
                        package.disabled = true;
                        ApplyScopeSelector(scope, package.selectors);
                        project.packageRefs.push_back(std::move(package));
                        continue;
                    }
                    if (const auto packagePath = Attribute(*node, "Path"); packagePath.has_value() && node->name == "Tool")
                    {
                        ProjectReference reference{};
                        reference.path = (path.parent_path() / *packagePath).lexically_normal();
                        ApplyScopeSelector(scope, reference.selectors);
                        project.projectRefs.push_back(std::move(reference));
                        continue;
                    }
                    PackageReference package{};
                    package.name = RequireAttribute(*node, "Name", path);
                    package.versionRange = Attribute(*node, "Version").value_or(Attribute(*node, "VersionRange").value_or(""));
                    package.scope = ScopeMutation(*node, node->name == "Runtime" ? "Target;Runtime" : (node->name == "Tool" ? "Build" : ""));
                    package.removeScope = Attribute(*node, "RemoveScope").value_or("");
                    ApplyScopeSelector(scope, package.selectors);
                    project.packageRefs.push_back(package);
                    for (const auto *feature : ChildElements(*node, "Feature"))
                    {
                        PackageFeatureUse use{};
                        use.packageName = package.name;
                        if (const auto remove = Attribute(*feature, "Remove"); remove.has_value() && !remove->empty())
                        {
                            use.featureName = *remove;
                            use.disabled = true;
                        }
                        else
                        {
                            use.featureName = RequireAttribute(*feature, "Name", path);
                        }
                        use.versionRange = package.versionRange;
                        ApplyScopeSelector(scope, use.selectors);
                        project.packageFeatureUses.push_back(std::move(use));
                    }
                    continue;
                }
            }
        }

        auto ParseStageSection(const XmlElement &stageNode, const fs::path &path, ProjectManifest &project, const std::string &scope) -> void
        {
            for (const auto *node : ChildElements(stageNode))
            {
                if (node->name == "Config")
                {
                    if (const auto remove = Attribute(*node, "Remove"); remove.has_value() && !remove->empty())
                    {
                        RemoveStageInput(project.inputs, "Config", *remove, scope);
                        continue;
                    }
                    auto input = PathInput("Config", "", RequireAttribute(*node, "Source", path), "Private", Attribute(*node, "Target").value_or(""), scope);
                    input.overrideExisting = Attribute(*node, "Collision").value_or("") == "Override";
                    ValidateInputDeclaration(input, path);
                    UpsertStageInput(project.inputs, std::move(input), scope);
                }
                else if (node->name == "Content")
                {
                    if (const auto remove = Attribute(*node, "Remove"); remove.has_value() && !remove->empty())
                    {
                        RemoveStageInput(project.inputs, "Content", *remove, scope);
                        continue;
                    }
                    auto input = PathInput("Content", "", RequireAttribute(*node, "Source", path), "Private", Attribute(*node, "Target").value_or(""), scope);
                    input.overrideExisting = Attribute(*node, "Collision").value_or("") == "Override";
                    ValidateInputDeclaration(input, path);
                    UpsertStageInput(project.inputs, std::move(input), scope);
                }
            }
        }

        auto ParseEnvironmentSection(const XmlElement &environmentNode, const fs::path &path, EnvironmentDefinition &environment) -> void
        {
            std::set<std::string> localNames{};
            for (const auto *node : ChildElements(environmentNode))
            {
                if (node->name == "Env")
                {
                    if (const auto remove = Attribute(*node, "Remove"); remove.has_value() && !remove->empty())
                    {
                        RequireUniqueLocalName(localNames, "environment variable", *remove, path);
                        RemoveEnvironmentVariable(environment.variables, *remove);
                        continue;
                    }
                    EnvironmentVariable variable{};
                    variable.name = RequireAttribute(*node, "Name", path);
                    RequireUniqueLocalName(localNames, "environment variable", variable.name, path);
                    variable.value = RequireAttribute(*node, "Value", path);
                    UpsertEnvironmentVariable(environment.variables, std::move(variable));
                }
                else if (node->name == "Secret")
                {
                    if (const auto remove = Attribute(*node, "Remove"); remove.has_value() && !remove->empty())
                    {
                        RequireUniqueLocalName(localNames, "environment variable", *remove, path);
                        RemoveEnvironmentVariable(environment.variables, *remove);
                        continue;
                    }
                    EnvironmentVariable variable{};
                    variable.name = RequireAttribute(*node, "Name", path);
                    RequireUniqueLocalName(localNames, "environment variable", variable.name, path);
                    variable.fromLocalSetting = RequireAttribute(*node, "From", path);
                    if (variable.fromLocalSetting.rfind("local:", 0) == 0)
                    {
                        variable.fromLocalSetting.erase(0, 6);
                    }
                    variable.required = BoolAttribute(*node, "Required");
                    variable.secret = true;
                    UpsertEnvironmentVariable(environment.variables, std::move(variable));
                }
            }
        }

        auto UpsertAnalyzer(std::vector<AnalyzerDefinition> &analyzers, AnalyzerDefinition analyzer) -> void
        {
            analyzers.erase(
                std::remove_if(
                    analyzers.begin(),
                    analyzers.end(),
                    [&](const AnalyzerDefinition &existing)
                    {
                        return existing.name == analyzer.name;
                    }),
                analyzers.end());
            analyzers.push_back(std::move(analyzer));
        }

        auto RemoveAnalyzer(std::vector<AnalyzerDefinition> &analyzers, const std::string &name) -> void
        {
            analyzers.erase(
                std::remove_if(
                    analyzers.begin(),
                    analyzers.end(),
                    [&](const AnalyzerDefinition &existing)
                    {
                        return existing.name == name;
                    }),
                analyzers.end());
        }

        auto ParseQualitySection(const XmlElement &qualityNode, const fs::path &path, QualityDefinition &quality) -> void
        {
            std::set<std::string> localNames{};
            for (const auto *node : ChildElements(qualityNode, "Analyzer"))
            {
                if (const auto remove = Attribute(*node, "Remove"); remove.has_value() && !remove->empty())
                {
                    RequireUniqueLocalName(localNames, "analyzer", *remove, path);
                    RemoveAnalyzer(quality.analyzers, *remove);
                    continue;
                }
                AnalyzerDefinition analyzer{};
                analyzer.name = RequireAttribute(*node, "Name", path);
                RequireUniqueLocalName(localNames, "analyzer", analyzer.name, path);
                analyzer.scope = Attribute(*node, "Scope").value_or(analyzer.scope);
                analyzer.enabled = BoolAttribute(*node, "Enabled", analyzer.enabled);
                analyzer.severity = Attribute(*node, "Severity").value_or(analyzer.severity);
                analyzer.selectors = ParseSelection(*node, path);
                if (const auto *config = FindChild(*node, "Config"))
                {
                    analyzer.configPath = RequireAttribute(*config, "Path", path);
                }
                UpsertAnalyzer(quality.analyzers, std::move(analyzer));
            }
        }

        auto ParseRuntimeSection(const XmlElement &runtimeNode, const fs::path &path, RuntimeDefinition &runtime) -> void
        {
            std::set<std::string> localNames{};
            for (const auto *node : ChildElements(runtimeNode, "Module"))
            {
                if (const auto remove = Attribute(*node, "Remove"); remove.has_value() && !remove->empty())
                {
                    RequireUniqueLocalName(localNames, "runtime module", *remove, path);
                    RemoveRuntimeModule(runtime, *remove);
                    continue;
                }
                ModuleDescriptor module{};
                module.name = RequireAttribute(*node, "Name", path);
                RequireUniqueLocalName(localNames, "runtime module", module.name, path);
                module.startupStage = Attribute(*node, "Stage").value_or("Features");
                for (const auto *provides : ChildElements(*node, "Provides"))
                {
                    if (const auto service = Attribute(*provides, "Service"); service.has_value() && !service->empty())
                    {
                        module.providesServices.push_back(*service);
                    }
                }
                for (const auto *requirement : ChildElements(*node, "Requires"))
                {
                    if (const auto service = Attribute(*requirement, "Service"); service.has_value() && !service->empty())
                    {
                        module.requiresServices.push_back(*service);
                    }
                }
                UpsertRuntimeModule(runtime, std::move(module));
            }
        }

        auto ApplyLaunchNode(const XmlElement &node, const fs::path &, const ProjectManifest &project, LaunchDefinition &launch) -> void
        {
            launch.name = Attribute(node, "Name").value_or(launch.name);
            if (const auto executable = Attribute(node, "Executable"); executable.has_value() && !executable->empty())
            {
                launch.executable = *executable == "$(OutputName)" ? project.output.name : *executable;
            }
            launch.workingDirectory = Attribute(node, "WorkingDirectory").value_or(launch.workingDirectory);
            launch.args = Attribute(node, "Args").value_or(launch.args);
        }

        auto UpsertPackageOutput(std::vector<PackageOutputDefinition> &outputs, PackageOutputDefinition output) -> void
        {
            outputs.erase(
                std::remove_if(
                    outputs.begin(),
                    outputs.end(),
                    [&](const PackageOutputDefinition &existing)
                    {
                        return existing.name == output.name;
                    }),
                outputs.end());
            outputs.push_back(std::move(output));
        }

        auto ParsePackageOutputSection(
            const XmlElement &node,
            const fs::path &path,
            const ProjectManifest &project,
            std::vector<PackageOutputDefinition> &outputs) -> void
        {
            PackageOutputDefinition output{};
            if (const auto remove = Attribute(node, "Remove"); remove.has_value() && !remove->empty())
            {
                output.name = *remove;
                output.disabled = true;
                UpsertPackageOutput(outputs, std::move(output));
                return;
            }
            output.name = RequireAttribute(node, "Name", path);
            output.version = RequireAttribute(node, "Version", path);
            output.from = Attribute(node, "From").value_or(project.name);
            if (const auto *metadata = FindChild(node, "Metadata"))
            {
                if (const auto *description = FindChild(*metadata, "Description"))
                {
                    output.description = Trim(TextContent(*description));
                }
                if (const auto *license = FindChild(*metadata, "License"))
                {
                    output.license = Trim(TextContent(*license));
                }
            }
            if (const auto *exports = FindChild(node, "Exports"))
            {
                for (const auto *headers : ChildElements(*exports, "Headers"))
                {
                    output.headers.push_back(RequireAttribute(*headers, "Path", path));
                }
                for (const auto *library : ChildElements(*exports, "Library"))
                {
                    output.libraries.push_back(RequireAttribute(*library, "Name", path));
                }
                for (const auto *tool : ChildElements(*exports, "Tool"))
                {
                    output.tools.push_back(RequireAttribute(*tool, "Name", path));
                }
                for (const auto *capability : ChildElements(*exports, "Capability"))
                {
                    output.capabilities.push_back(RequireAttribute(*capability, "Name", path));
                }
            }
            if (const auto *compatibility = FindChild(node, "Compatibility"))
            {
                for (const auto *platform : ChildElements(*compatibility, "TargetPlatform"))
                {
                    output.targetPlatforms.push_back(RequireAttribute(*platform, "Name", path));
                }
                if (const auto *abi = FindChild(*compatibility, "Abi"))
                {
                    output.abiTag = RequireAttribute(*abi, "Tag", path);
                }
            }
            UpsertPackageOutput(outputs, std::move(output));
        }

        auto ParsePublishSection(const XmlElement &node, const fs::path &path, std::vector<PublishDefinition> &publishes) -> void
        {
            PublishDefinition publish{};
            if (const auto remove = Attribute(node, "Remove"); remove.has_value() && !remove->empty())
            {
                publish.name = *remove;
                publish.disabled = true;
                publishes.erase(
                    std::remove_if(
                        publishes.begin(),
                        publishes.end(),
                        [&](const PublishDefinition &existing)
                        {
                            return existing.name == publish.name;
                        }),
                    publishes.end());
                publishes.push_back(std::move(publish));
                return;
            }
            publish.name = Attribute(node, "Name").value_or("default");
            publish.kind = Attribute(node, "Kind").value_or("Folder");
            publish.format = Attribute(node, "Format").value_or("");
            publish.output = RequireAttribute(node, "Output", path);
            if (const auto *include = FindChild(node, "Include"))
            {
                if (const auto stage = Attribute(*include, "Stage"); stage.has_value() && *stage == "none")
                {
                    publish.includeStage = false;
                }
                publish.includeRuntimeDependencies = BoolAttribute(*include, "RuntimeDependencies", publish.includeRuntimeDependencies);
                publish.includeSymbols = BoolAttribute(*include, "Symbols", publish.includeSymbols);
            }
            publishes.erase(
                std::remove_if(
                    publishes.begin(),
                    publishes.end(),
                    [&](const PublishDefinition &existing)
                    {
                        return existing.name == publish.name;
                    }),
                publishes.end());
            publishes.push_back(std::move(publish));
        }

        [[nodiscard]] auto ParseProductGeneratorOutput(const XmlElement &node, const fs::path &path, const std::string &scope) -> InputDeclaration
        {
            std::string role;
            if (node.name == "Sources")
            {
                role = "Source";
            }
            else if (node.name == "Headers")
            {
                role = "Header";
            }
            else if (node.name == "Files")
            {
                role = "Content";
            }
            else
            {
                throw std::runtime_error(path.string() + ": unsupported generator output <" + std::string(node.name) + ">");
            }
            auto output = PathInput("Generated", role, RequireAttribute(node, "Path", path), role == "Header" ? "Public" : "Private", "", scope);
            ValidateInputDeclaration(output, path);
            return output;
        }

        auto ParseGenerateSection(const XmlElement &generateNode, const fs::path &path, std::vector<GeneratorDeclaration> &generators, const std::string &scope) -> void
        {
            std::set<std::string> localNames{};
            for (const auto *node : ChildElements(generateNode, "Generator"))
            {
                GeneratorDeclaration generator{};
                if (const auto remove = Attribute(*node, "Remove"); remove.has_value() && !remove->empty())
                {
                    RequireUniqueLocalName(localNames, "generator", *remove, path);
                    generator.name = *remove;
                    generator.disabled = true;
                    ApplyScopeSelector(scope, generator.selectors);
                    UpsertGenerator(generators, std::move(generator));
                    continue;
                }
                generator.name = RequireAttribute(*node, "Name", path);
                RequireUniqueLocalName(localNames, "generator", generator.name, path);
                generator.kind = "Command";
                ApplyScopeSelector(scope, generator.selectors);
                ApplyWhenSelector(*node, generator.selectors);
                if (const auto *tool = FindChild(*node, "Tool"))
                {
                    generator.toolName = Attribute(*tool, "Name").value_or("");
                    generator.inlineTool.name = generator.toolName;
                    generator.inlineTool.executable = Attribute(*tool, "Executable").value_or("");
                    if (!generator.inlineTool.executable.empty())
                    {
                        generator.inlineTool.kind = "Generator";
                        generator.hasInlineTool = true;
                    }
                }
                if (generator.toolName.empty() && !generator.hasInlineTool)
                {
                    throw std::runtime_error(path.string() + ": generator '" + generator.name + "' must declare <Tool Name=\"...\"> or Executable");
                }
                if (const auto *args = FindChild(*node, "Args"))
                {
                    for (const auto *arg : ChildElements(*args, "Arg"))
                    {
                        GeneratorArgument argument{};
                        argument.value = Attribute(*arg, "Value").value_or("");
                        argument.path = Attribute(*arg, "Path").value_or("");
                        if (argument.value.empty() == argument.path.empty())
                        {
                            throw std::runtime_error(path.string() + ": generator argument must declare exactly one of Value or Path");
                        }
                        generator.arguments.push_back(std::move(argument));
                    }
                }
                if (const auto *inputs = FindChild(*node, "Inputs"))
                {
                    for (const auto *input : ChildElements(*inputs))
                    {
                        if (input->name == "Headers")
                        {
                            AddPathInput(generator.inputs, path, "Source", "Header", RequireAttribute(*input, "Path", path), "Public", "", scope + ":" + generator.name);
                        }
                        else if (input->name == "Sources" || input->name == "Files" || input->name == "File")
                        {
                            AddPathInput(generator.inputs, path, "ToolInput", "", RequireAttribute(*input, "Path", path), "Private", "", scope + ":" + generator.name);
                        }
                    }
                }
                const auto *outputs = FindChild(*node, "Outputs");
                if (outputs == nullptr)
                {
                    throw std::runtime_error(path.string() + ": generator '" + generator.name + "' must declare <Outputs>");
                }
                for (const auto *output : ChildElements(*outputs))
                {
                    generator.outputs.push_back(ParseProductGeneratorOutput(*output, path, scope + ":" + generator.name));
                }
                UpsertGenerator(generators, std::move(generator));
            }
        }

        auto ApplyDefaults(const XmlElement &defaultsNode, const fs::path &path, ProjectBuildDescriptor &build, ProfileDefinition &profile) -> void
        {
            for (const auto *node : ChildElements(defaultsNode))
            {
                if (node->name == "Language")
                {
                    ParseLanguage(*node, path, build);
                }
                else if (node->name == "Backend")
                {
                    if (const auto name = Attribute(*node, "Name"); name.has_value() && !name->empty())
                    {
                        build.backend = *name;
                        build.backendExplicit = true;
                    }
                    if (const auto mode = Attribute(*node, "Mode"); mode.has_value() && !mode->empty())
                    {
                        build.mode = *mode;
                        build.backendExplicit = true;
                    }
                }
                else if (node->name == "BuildType")
                {
                    profile.buildType = RequireAttribute(*node, "Name", path);
                }
                else if (node->name == "TargetPlatform")
                {
                    const auto updated = ProfileWithPlatform(profile.name, profile.buildType, RequireAttribute(*node, "Name", path), profile.environmentName);
                    profile.buildType = updated.buildType;
                    profile.platform = updated.platform;
                    profile.operatingSystem = updated.operatingSystem;
                    profile.architecture = updated.architecture;
                    profile.environmentName = updated.environmentName;
                }
                else if (node->name == "HostPlatform")
                {
                    profile.hostPlatform = RequireAttribute(*node, "Name", path);
                }
                else if (node->name == "Toolchain")
                {
                    profile.toolchain = RequireAttribute(*node, "Name", path);
                }
                else if (node->name == "Environment")
                {
                    profile.environmentName = RequireAttribute(*node, "Name", path);
                }
            }
        }

        [[nodiscard]] auto FindProductElement(const XmlElement &root, const fs::path &path) -> const XmlElement &
        {
            const XmlElement *product = nullptr;
            for (const auto *child : ChildElements(root))
            {
                if (!IsProductElementName(child->name))
                {
                    continue;
                }
                if (product != nullptr)
                {
                    throw std::runtime_error(path.string() + ": project must declare exactly one primary product element");
                }
                product = child;
            }
            if (product == nullptr)
            {
                throw std::runtime_error(path.string() + ": project must declare one product element such as <Application /> or <Library />");
            }
            return *product;
        }

        [[nodiscard]] auto LoadProjectManifest(const fs::path &path, const XmlElement &rootElement) -> ProjectManifest
        {
            const auto &product = FindProductElement(rootElement, path);
            const std::string productKind{product.name};
            ProjectManifest project{};
            project.path = path;
            project.name = RequireAttribute(rootElement, "Name", path);
            project.type = ProjectType(productKind);
            project.productKind = productKind;
            project.defaultProfile = Attribute(rootElement, "DefaultProfile").value_or("dev");
            project.output.kind = DefaultOutputKind(productKind, product);
            project.output.name = project.name;
            project.output.target = project.name;
            project.hasExplicitProfiles = !ChildElements(rootElement, "Profile").empty();
            project.conditions = BuiltinConditions();
            {
                std::set<std::string> builtinNames{};
                for (const auto &condition : project.conditions)
                {
                    builtinNames.insert(Lower(condition.name));
                }
                for (auto condition : ParseConditions(rootElement, path))
                {
                    if (builtinNames.contains(Lower(condition.name)))
                    {
                        throw std::runtime_error(path.string() + ": condition '" + condition.name + "' cannot replace built-in condition");
                    }
                    project.conditions.push_back(std::move(condition));
                }
            }

            ProfileDefinition baseProfile = ProfileWithPlatform(project.defaultProfile, "Debug", "linux-x64", "development");
            baseProfile.launch.executable = project.output.kind == "Executable" ? std::optional<std::string>{project.output.name} : std::nullopt;
            baseProfile.launch.name = "default";
            baseProfile.launch.workingDirectory = "$(StageDir)";

            if (const auto *defaults = FindChild(rootElement, "Defaults"))
            {
                ApplyDefaults(*defaults, path, project.build, baseProfile);
            }
            if (const auto *uses = FindChild(product, "Uses"))
            {
                ParseUsesSection(*uses, path, project);
            }
            if (const auto *build = FindChild(product, "Build"))
            {
                ParseBuildSection(*build, path, project, "product:" + productKind);
            }
            else if (productKind != "External")
            {
                AddPathInput(project.inputs, path, "Source", "Source", "src", "Private", "", "product:" + productKind);
            }
            if (const auto *exports = FindChild(product, "Exports"))
            {
                ParseExportsSection(*exports, path, project, "product:" + productKind);
            }
            if (const auto *generate = FindChild(product, "Generate"))
            {
                ParseGenerateSection(*generate, path, project.generators, "product:" + productKind);
            }
            if (const auto *stage = FindChild(product, "Stage"))
            {
                ParseStageSection(*stage, path, project, "product:" + productKind);
            }
            if (const auto *runtime = FindChild(product, "Runtime"))
            {
                ParseRuntimeSection(*runtime, path, project.runtime);
            }
            std::set<std::string> packageOutputNames{};
            for (const auto *packageOutput : ChildElements(product, "PackageOutput"))
            {
                if (const auto remove = Attribute(*packageOutput, "Remove"); remove.has_value() && !remove->empty())
                {
                    RequireUniqueLocalName(packageOutputNames, "package output", *remove, path);
                }
                else
                {
                    RequireUniqueLocalName(packageOutputNames, "package output", RequireAttribute(*packageOutput, "Name", path), path);
                }
                ParsePackageOutputSection(*packageOutput, path, project, project.packageOutputs);
            }
            std::set<std::string> publishNames{};
            for (const auto *publish : ChildElements(product, "Publish"))
            {
                if (const auto remove = Attribute(*publish, "Remove"); remove.has_value() && !remove->empty())
                {
                    RequireUniqueLocalName(publishNames, "publish", *remove, path);
                }
                else
                {
                    RequireUniqueLocalName(publishNames, "publish", Attribute(*publish, "Name").value_or("default"), path);
                }
                ParsePublishSection(*publish, path, project.publishes);
            }
            EnvironmentDefinition baseEnvironment{};
            baseEnvironment.name = baseProfile.environmentName;
            if (const auto *environment = FindChild(product, "Environment"))
            {
                ParseEnvironmentSection(*environment, path, baseEnvironment);
            }
            if (const auto *quality = FindChild(rootElement, "Quality"))
            {
                ParseQualitySection(*quality, path, project.quality);
            }
            if (const auto *quality = FindChild(product, "Quality"))
            {
                ParseQualitySection(*quality, path, project.quality);
            }
            if (const auto *launch = FindChild(product, "Launch"))
            {
                ApplyLaunchNode(*launch, path, project, baseProfile.launch);
            }
            else if (const auto *run = FindChild(product, "Run"))
            {
                ApplyLaunchNode(*run, path, project, baseProfile.launch);
            }

            std::unordered_map<std::string, std::size_t> profileIndexes{};

            for (const auto *profileNode : ChildElements(rootElement, "Profile"))
            {
                ProfileDefinition profile = baseProfile;
                profile.name = RequireAttribute(*profileNode, "Name", path);
                if (const auto extends = Attribute(*profileNode, "Extends"); extends.has_value() && !extends->empty())
                {
                    const auto parent = profileIndexes.find(*extends);
                    if (parent == profileIndexes.end())
                    {
                        throw std::runtime_error(path.string() + ": profile '" + profile.name + "' extends unknown or later profile '" + *extends + "'");
                    }
                    profile = project.profiles[parent->second];
                    profile.name = RequireAttribute(*profileNode, "Name", path);
                }
                if (const auto *defaults = FindChild(*profileNode, "Defaults"))
                {
                    ApplyDefaults(*defaults, path, project.build, profile);
                }
                if (const auto *productOverlay = FindChild(*profileNode, productKind))
                {
                    if (const auto *build = FindChild(*productOverlay, "Build"))
                    {
                        ParseBuildSection(*build, path, project, "profile:" + profile.name);
                    }
                    if (const auto *uses = FindChild(*productOverlay, "Uses"))
                    {
                        ParseUsesSection(*uses, path, project, "profile:" + profile.name);
                    }
                    if (const auto *stage = FindChild(*productOverlay, "Stage"))
                    {
                        ParseStageSection(*stage, path, project, "profile:" + profile.name);
                    }
                    if (const auto *generate = FindChild(*productOverlay, "Generate"))
                    {
                        ParseGenerateSection(*generate, path, profile.generators, "profile:" + profile.name);
                    }
                    if (const auto *runtime = FindChild(*productOverlay, "Runtime"))
                    {
                        ParseRuntimeSection(*runtime, path, profile.runtime);
                    }
                    if (const auto *launch = FindChild(*productOverlay, "Launch"))
                    {
                        ApplyLaunchNode(*launch, path, project, profile.launch);
                    }
                    else if (const auto *run = FindChild(*productOverlay, "Run"))
                    {
                        ApplyLaunchNode(*run, path, project, profile.launch);
                    }
                    if (const auto *environment = FindChild(*productOverlay, "Environment"))
                    {
                        EnvironmentDefinition env{};
                        env.name = profile.environmentName;
                        env.variables = baseEnvironment.variables;
                        ParseEnvironmentSection(*environment, path, env);
                        project.environments.push_back(std::move(env));
                    }
                    if (const auto *quality = FindChild(*productOverlay, "Quality"))
                    {
                        profile.quality = project.quality;
                        ParseQualitySection(*quality, path, profile.quality);
                    }
                    std::set<std::string> packageOutputNames{};
                    for (const auto *packageOutput : ChildElements(*productOverlay, "PackageOutput"))
                    {
                        if (const auto remove = Attribute(*packageOutput, "Remove"); remove.has_value() && !remove->empty())
                        {
                            RequireUniqueLocalName(packageOutputNames, "package output", *remove, path);
                        }
                        else
                        {
                            RequireUniqueLocalName(packageOutputNames, "package output", RequireAttribute(*packageOutput, "Name", path), path);
                        }
                        ParsePackageOutputSection(*packageOutput, path, project, profile.packageOutputs);
                    }
                    std::set<std::string> publishNames{};
                    for (const auto *publish : ChildElements(*productOverlay, "Publish"))
                    {
                        if (const auto remove = Attribute(*publish, "Remove"); remove.has_value() && !remove->empty())
                        {
                            RequireUniqueLocalName(publishNames, "publish", *remove, path);
                        }
                        else
                        {
                            RequireUniqueLocalName(publishNames, "publish", Attribute(*publish, "Name").value_or("default"), path);
                        }
                        ParsePublishSection(*publish, path, profile.publishes);
                    }
                }
                profileIndexes.emplace(profile.name, project.profiles.size());
                project.profiles.push_back(std::move(profile));
            }
            if (project.profiles.empty() || !profileIndexes.contains(project.defaultProfile))
            {
                baseProfile.name = project.defaultProfile;
                profileIndexes.emplace(baseProfile.name, project.profiles.size());
                project.profiles.push_back(baseProfile);
            }

            project.environments.push_back(std::move(baseEnvironment));
            for (const auto &profile : project.profiles)
            {
                EnvironmentDefinition environment{};
                environment.name = profile.environmentName;
                environment.variables = baseEnvironment.variables;
                project.environments.push_back(std::move(environment));
            }
            std::set<std::string> seenEnvironments{};
            std::vector<EnvironmentDefinition> uniqueEnvironments{};
            for (auto &environment : project.environments)
            {
                if (seenEnvironments.insert(environment.name).second)
                {
                    uniqueEnvironments.push_back(std::move(environment));
                }
            }
            project.environments = std::move(uniqueEnvironments);

            ValidateConditionReferences(project, path);
            return project;
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
        return LoadProjectManifest(path, *rootElement);
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

    [[nodiscard]] auto ProfileWithWorkspacePolicy(
        const ProjectManifest &project,
        const std::optional<WorkspaceManifest> &workspace,
        const std::optional<std::string> &profileName) -> ProfileDefinition
    {
        const auto desired = profileName.value_or(project.defaultProfile);
        const auto projectProfile = std::find_if(
            project.profiles.begin(), project.profiles.end(),
            [&](const ProfileDefinition &profile)
            {
                return profile.name == desired;
            });

        const WorkspaceManifest::ProfilePolicy *workspaceProfile = nullptr;
        if (workspace.has_value())
        {
            const auto it = std::find_if(
                workspace->profiles.begin(), workspace->profiles.end(),
                [&](const WorkspaceManifest::ProfilePolicy &profile)
                {
                    return profile.name == desired;
                });
            if (it != workspace->profiles.end())
            {
                workspaceProfile = &*it;
            }
        }

        ProfileDefinition effective{};
        if (projectProfile != project.profiles.end())
        {
            effective = *projectProfile;
        }
        else if (workspaceProfile != nullptr)
        {
            effective = ProfileByName(project, std::nullopt);
            effective.name = desired;
        }
        else
        {
            return ProfileByName(project, profileName);
        }

        const auto applyPolicy = [](ProfileDefinition &profile, const WorkspaceManifest::ProfilePolicy &policy)
        {
            if (policy.buildType.has_value())
            {
                profile.buildType = *policy.buildType;
            }
            if (policy.hostPlatform.has_value())
            {
                profile.hostPlatform = *policy.hostPlatform;
            }
            if (policy.targetPlatform.has_value() && *policy.targetPlatform != "host")
            {
                profile.platform = *policy.targetPlatform;
            }
            if (policy.operatingSystem.has_value())
            {
                profile.operatingSystem = *policy.operatingSystem;
            }
            if (policy.architecture.has_value())
            {
                profile.architecture = *policy.architecture;
            }
            if (policy.environmentName.has_value())
            {
                profile.environmentName = *policy.environmentName;
            }
            if (policy.toolchain.has_value())
            {
                profile.toolchain = *policy.toolchain;
            }
        };

        if (workspace.has_value() && (!project.hasExplicitProfiles || projectProfile == project.profiles.end()))
        {
            applyPolicy(effective, workspace->defaults);
            if (workspaceProfile != nullptr)
            {
                applyPolicy(effective, *workspaceProfile);
            }
        }
        return effective;
    }

    [[nodiscard]] auto ProjectWithWorkspacePolicy(
        ProjectManifest project,
        const std::optional<WorkspaceManifest> &workspace) -> ProjectManifest
    {
        if (!workspace.has_value())
        {
            return project;
        }

        const auto &defaults = workspace->defaults;
        if (!project.build.languageExplicit)
        {
            if (defaults.language.has_value())
            {
                project.build.language = *defaults.language;
            }
            if (defaults.languageStandard.has_value())
            {
                project.build.languageStandard = *defaults.languageStandard;
            }
        }
        if (!project.build.backendExplicit)
        {
            if (defaults.backend.has_value())
            {
                project.build.backend = *defaults.backend;
            }
            if (defaults.buildMode.has_value())
            {
                project.build.mode = *defaults.buildMode;
            }
        }

        const auto applyProfilePolicyContributions = [&](const WorkspaceManifest::ProfilePolicy &policy)
        {
            const auto scope = policy.name.empty() ? std::string{} : "profile:" + policy.name;
            for (const auto &buildSetting : policy.buildSettings)
            {
                if (!buildSetting.productKind.empty() && buildSetting.productKind != project.productKind)
                {
                    continue;
                }
                BuildSetting setting{};
                setting.value = buildSetting.value;
                setting.visibility = buildSetting.visibility;
                ApplyScopeSelector(scope, setting.selectors);

                if (buildSetting.kind == "IncludePath")
                {
                    project.build.includeDirectories.push_back(std::move(setting));
                }
                else if (buildSetting.kind == "Define")
                {
                    UpsertDefine(project.build.compileDefinitions, std::move(setting), scope);
                }
                else if (buildSetting.kind == "CompileOption")
                {
                    project.build.compileOptions.push_back(std::move(setting));
                }
                else if (buildSetting.kind == "LinkOption")
                {
                    project.build.linkOptions.push_back(std::move(setting));
                }
            }

            for (const auto &analyzerPolicy : policy.analyzers)
            {
                if (!analyzerPolicy.productKind.empty() && analyzerPolicy.productKind != project.productKind)
                {
                    continue;
                }
                AnalyzerDefinition analyzer{};
                analyzer.name = analyzerPolicy.name;
                analyzer.scope = analyzerPolicy.scope;
                analyzer.enabled = analyzerPolicy.enabled;
                analyzer.severity = analyzerPolicy.severity;
                analyzer.configPath = analyzerPolicy.configPath;
                ApplyScopeSelector(scope, analyzer.selectors);
                UpsertAnalyzer(project.quality.analyzers, std::move(analyzer));
            }

            if (!policy.environmentVariables.empty())
            {
                const auto environmentName = policy.environmentName.value_or(policy.name.empty() ? "development" : policy.name);
                auto environmentIt = std::find_if(
                    project.environments.begin(),
                    project.environments.end(),
                    [&](const EnvironmentDefinition &environment)
                    {
                        return environment.name == environmentName;
                    });
                if (environmentIt == project.environments.end())
                {
                    EnvironmentDefinition environment{};
                    environment.name = environmentName;
                    if (!project.environments.empty())
                    {
                        environment.variables = project.environments.front().variables;
                    }
                    project.environments.push_back(std::move(environment));
                    environmentIt = std::prev(project.environments.end());
                }

                for (const auto &variablePolicy : policy.environmentVariables)
                {
                    if (!variablePolicy.productKind.empty() && variablePolicy.productKind != project.productKind)
                    {
                        continue;
                    }
                    if (variablePolicy.remove)
                    {
                        RemoveEnvironmentVariable(environmentIt->variables, variablePolicy.name);
                        continue;
                    }
                    EnvironmentVariable variable{};
                    variable.name = variablePolicy.name;
                    variable.value = variablePolicy.value;
                    variable.fromLocalSetting = variablePolicy.fromLocalSetting;
                    variable.required = variablePolicy.required;
                    variable.secret = variablePolicy.secret;
                    UpsertEnvironmentVariable(environmentIt->variables, std::move(variable));
                }
            }

            for (const auto &stagePolicy : policy.stageInputs)
            {
                if (!stagePolicy.productKind.empty() && stagePolicy.productKind != project.productKind)
                {
                    continue;
                }
                if (stagePolicy.remove)
                {
                    RemoveStageInput(project.inputs, stagePolicy.kind, stagePolicy.target, scope);
                    continue;
                }
                auto input = PathInput(stagePolicy.kind, "", stagePolicy.source, "Private", stagePolicy.target, scope);
                input.overrideExisting = stagePolicy.collision == "Override";
                ValidateInputDeclaration(input, project.path);
                UpsertStageInput(project.inputs, std::move(input), scope);
            }

            for (const auto &dependencyUse : policy.dependencyUses)
            {
                if (!dependencyUse.productKind.empty() && dependencyUse.productKind != project.productKind)
                {
                    continue;
                }
                if (dependencyUse.kind == "Project" || (dependencyUse.kind == "Tool" && !dependencyUse.path.empty()))
                {
                    ProjectReference reference{};
                    reference.path = dependencyUse.path.empty() ? fs::path{dependencyUse.name} : dependencyUse.path;
                    reference.disabled = dependencyUse.remove;
                    ApplyScopeSelector(scope, reference.selectors);
                    project.projectRefs.push_back(std::move(reference));
                    continue;
                }

                PackageReference package{};
                package.name = dependencyUse.name;
                package.versionRange = dependencyUse.versionRange;
                package.scope = dependencyUse.scope;
                package.removeScope = dependencyUse.removeScope;
                package.disabled = dependencyUse.remove;
                if (package.scope.empty() && dependencyUse.kind == "Runtime")
                {
                    package.scope = "Target;Runtime";
                }
                if (package.scope.empty() && dependencyUse.kind == "Tool")
                {
                    package.scope = "Build";
                }
                ApplyScopeSelector(scope, package.selectors);
                project.packageRefs.push_back(package);

                if (!dependencyUse.remove)
                {
                    for (const auto &featureName : dependencyUse.features)
                    {
                        PackageFeatureUse use{};
                        use.packageName = package.name;
                        use.featureName = featureName;
                        use.versionRange = package.versionRange;
                        ApplyScopeSelector(scope, use.selectors);
                        project.packageFeatureUses.push_back(std::move(use));
                    }
                }
            }

            for (const auto &runtimePolicy : policy.runtimeModules)
            {
                if (!runtimePolicy.productKind.empty() && runtimePolicy.productKind != project.productKind)
                {
                    continue;
                }
                SelectorSet scopeSelectors{};
                ApplyScopeSelector(scope, scopeSelectors);
                const auto sameProfileSelector = [&](const SelectorSet &selectors)
                {
                    return selectors.profile == scopeSelectors.profile;
                };
                if (runtimePolicy.remove)
                {
                    RuntimeReference reference{};
                    reference.name = runtimePolicy.name;
                    reference.selectors = scopeSelectors;
                    project.runtime.disableModules.erase(
                        std::remove_if(
                            project.runtime.disableModules.begin(),
                            project.runtime.disableModules.end(),
                            [&](const RuntimeReference &existing)
                            {
                                return existing.name == reference.name && sameProfileSelector(existing.selectors);
                            }),
                        project.runtime.disableModules.end());
                    project.runtime.disableModules.push_back(std::move(reference));
                    continue;
                }

                project.runtime.modules.erase(
                    std::remove_if(
                        project.runtime.modules.begin(),
                        project.runtime.modules.end(),
                        [&](const ModuleDescriptor &existing)
                        {
                            return existing.name == runtimePolicy.name && sameProfileSelector(existing.selectors);
                        }),
                    project.runtime.modules.end());
                project.runtime.enableModules.erase(
                    std::remove_if(
                        project.runtime.enableModules.begin(),
                        project.runtime.enableModules.end(),
                        [&](const RuntimeReference &existing)
                        {
                            return existing.name == runtimePolicy.name && sameProfileSelector(existing.selectors);
                        }),
                    project.runtime.enableModules.end());

                ModuleDescriptor module{};
                module.name = runtimePolicy.name;
                module.startupStage = runtimePolicy.stage;
                module.providesServices = runtimePolicy.providesServices;
                module.requiresServices = runtimePolicy.requiresServices;
                module.selectors = scopeSelectors;
                project.runtime.modules.push_back(std::move(module));

                RuntimeReference reference{};
                reference.name = runtimePolicy.name;
                reference.selectors = scopeSelectors;
                project.runtime.enableModules.push_back(std::move(reference));
            }
        };

        for (const auto &profilePolicy : workspace->profiles)
        {
            applyProfilePolicyContributions(profilePolicy);
        }
        return project;
    }
}
