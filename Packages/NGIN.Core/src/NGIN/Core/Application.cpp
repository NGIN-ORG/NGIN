#include <NGIN/Core/Application.hpp>

#include <NGIN/IO/FileSystemUtilities.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>
#include <NGIN/Serialization/XML/XmlParser.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace NGIN::Core
{
  namespace
  {
    using XmlDocument = NGIN::Serialization::XmlDocument;
    using XmlElement = NGIN::Serialization::XmlElement;
    using XmlNode = NGIN::Serialization::XmlNode;
    using XmlParseOptions = NGIN::Serialization::XmlParseOptions;
    using XmlParser = NGIN::Serialization::XmlParser;
    using IoError = NGIN::IO::IOError;
    using IoErrorCode = NGIN::IO::IOErrorCode;
    using IoFileSystem = NGIN::IO::IFileSystem;
    using IoPath = NGIN::IO::Path;

    struct PendingServiceRegistration
    {
      std::shared_ptr<detail::ServiceProviderBase> provider{};
    };

    struct PackageBootstrapRequest
    {
      std::string packageName{};
      std::optional<std::string> entryPoint{};
    };

    struct BootstrapCandidate
    {
      PackageReference reference{};
      const PackageManifest *manifest{nullptr};
      std::string entryPoint{};
      bool explicitRequest{false};
      std::size_t orderIndex{0};
    };

    struct LoadedXmlDocument
    {
      std::string text{};
      XmlDocument document{0};
    };

    [[nodiscard]] auto
    MakeBuilderError(const std::string &message, std::string subject = {},
                     const KernelErrorCode code = KernelErrorCode::InvalidArgument)
        -> KernelError
    {
      return MakeKernelError(code, "ApplicationBuilder", std::move(subject),
                             message);
    }

    [[nodiscard]] auto ToString(const NGIN::Text::String &text) -> std::string
    {
      return std::string(text.Data(), text.Size());
    }

    [[nodiscard]] auto ToString(const IoPath &path) -> std::string
    {
      return std::string(path.View());
    }

    [[nodiscard]] auto MakeBuilderFileSystemError(
        const std::string_view message, const IoPath &path, const IoError &error,
        const KernelErrorCode fallbackCode = KernelErrorCode::ConfigFailure)
        -> KernelError
    {
      const auto code = error.code == IoErrorCode::NotFound
                            ? KernelErrorCode::NotFound
                            : fallbackCode;
      const auto &subjectPath = error.path.IsEmpty() ? path : error.path;
      return MakeBuilderError(std::string(message), ToString(subjectPath), code);
    }

    [[nodiscard]] auto ReadTextFile(IoFileSystem &fileSystem, const IoPath &path,
                                    const std::string_view kind)
        -> CoreResult<std::string>
    {
      auto input = NGIN::IO::ReadAllText(fileSystem, path);
      if (!input)
      {
        const auto message = input.Error().code == IoErrorCode::NotFound
                                 ? "failed to open " + std::string(kind) + " file"
                                 : "failed to read " + std::string(kind) + " file";
        return NGIN::Utilities::Unexpected<KernelError>(
            MakeBuilderFileSystemError(message, path, input.Error()));
      }

      return ToString(input.Value());
    }

    [[nodiscard]] auto AbsolutePath(IoFileSystem &fileSystem, const IoPath &path)
        -> CoreResult<IoPath>
    {
      auto absolute = fileSystem.Absolute(path);
      if (!absolute)
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderFileSystemError(
            "failed to resolve project path", path, absolute.Error()));
      }
      return absolute.Value();
    }

    [[nodiscard]] auto WeaklyCanonicalPath(IoFileSystem &fileSystem,
                                           const IoPath &path)
        -> CoreResult<IoPath>
    {
      auto canonical = fileSystem.WeaklyCanonical(path);
      if (!canonical)
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderFileSystemError(
            "failed to resolve project path", path, canonical.Error()));
      }
      return canonical.Value();
    }

    [[nodiscard]] auto ToString(const NGIN::Serialization::ParseError &error)
        -> std::string
    {
      return std::string(error.message.Data(), error.message.Size());
    }

    [[nodiscard]] auto LoadXmlDocument(const std::string &text,
                                       const std::string_view kind)
        -> CoreResult<LoadedXmlDocument>
    {
      LoadedXmlDocument loaded{};
      loaded.text = text;

      XmlParseOptions options{};
      options.decodeEntities = true;
      options.arenaBytes = std::max<NGIN::UIntSize>(
          16384, static_cast<NGIN::UIntSize>(loaded.text.size() * 8 + 4096));

      auto parsed = XmlParser::Parse(loaded.text, options);
      if (!parsed)
      {
        return NGIN::Utilities::Unexpected<KernelError>(
            MakeBuilderError("failed to parse " + std::string(kind) +
                             " manifest: " + ToString(parsed.Error())));
      }

      loaded.document = std::move(parsed.Value());
      return loaded;
    }

    [[nodiscard]] auto ChildElements(const XmlElement &node,
                                     const std::string_view name = {})
        -> std::vector<const XmlElement *>
    {
      std::vector<const XmlElement *> out{};
      out.reserve(static_cast<std::size_t>(node.children.Size()));
      for (NGIN::UIntSize index = 0; index < node.children.Size(); ++index)
      {
        const auto &child = node.children[index];
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

    [[nodiscard]] auto FindChild(const XmlElement &node,
                                 const std::string_view name)
        -> const XmlElement *
    {
      for (NGIN::UIntSize index = 0; index < node.children.Size(); ++index)
      {
        const auto &child = node.children[index];
        if (child.type == XmlNode::Type::Element && child.element != nullptr &&
            child.element->name == name)
        {
          return child.element;
        }
      }
      return nullptr;
    }

    [[nodiscard]] auto Attribute(const XmlElement &node, const std::string_view key)
        -> std::optional<std::string>
    {
      const auto *attribute = node.FindAttribute(key);
      if (attribute == nullptr)
      {
        return std::nullopt;
      }
      return std::string(attribute->value);
    }

    [[nodiscard]] auto RequireAttribute(const XmlElement &element,
                                        const std::string_view key,
                                        const std::string_view context)
        -> CoreResult<std::string>
    {
      const auto value = Attribute(element, key);
      if (!value.has_value())
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            std::string(context) + " missing required attribute '" +
            std::string(key) + "'"));
      }
      return value.value();
    }

    [[nodiscard]] auto OptionalAttribute(const XmlElement &element,
                                         const std::string_view key,
                                         const std::string_view context,
                                         const std::string &defaultValue = {})
        -> CoreResult<std::string>
    {
      (void)context;
      const auto value = Attribute(element, key);
      if (!value.has_value())
      {
        return defaultValue;
      }
      return value.value();
    }

    [[nodiscard]] auto ParseBoolValue(const std::string &value,
                                      const std::string_view context)
        -> CoreResult<bool>
    {
      if (value == "true" || value == "1" || value == "yes")
      {
        return true;
      }
      if (value == "false" || value == "0" || value == "no")
      {
        return false;
      }
      return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
          std::string(context) + " must be a boolean-compatible value"));
    }

    [[nodiscard]] auto OptionalBoolAttribute(const XmlElement &element,
                                             const std::string_view key,
                                             const std::string_view context,
                                             const bool defaultValue = false)
        -> CoreResult<bool>
    {
      const auto value = Attribute(element, key);
      if (!value.has_value())
      {
        return defaultValue;
      }
      return ParseBoolValue(value.value(), context);
    }

    [[nodiscard]] auto ReadCompatibility(const XmlElement &element,
                                         const std::string_view context,
                                         std::vector<std::string> &operatingSystems,
                                         std::vector<std::string> &architectures)
        -> CoreResult<void>
    {
      if (FindChild(element, "Platforms") != nullptr)
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            "<Platforms> is no longer supported", std::string(context),
            KernelErrorCode::SchemaValidationFailure));
      }
      if (FindChild(element, "SupportedHosts") != nullptr)
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            "<SupportedHosts> is no longer supported",
            std::string(context), KernelErrorCode::SchemaValidationFailure));
      }
      if (const auto *compatibility = FindChild(element, "Compatibility"))
      {
        if (const auto *operatingSystemsElement =
                FindChild(*compatibility, "OperatingSystems"))
        {
          for (const auto *entry :
               ChildElements(*operatingSystemsElement, "OperatingSystem"))
          {
            auto value =
                RequireAttribute(*entry, "Name", std::string(context) + ".OS");
            if (!value)
            {
              return NGIN::Utilities::Unexpected<KernelError>(value.Error());
            }
            operatingSystems.push_back(value.Value());
          }
        }
        if (const auto *architecturesElement =
                FindChild(*compatibility, "Architectures"))
        {
          for (const auto *entry :
               ChildElements(*architecturesElement, "Architecture"))
          {
            auto value =
                RequireAttribute(*entry, "Name", std::string(context) + ".Arch");
            if (!value)
            {
              return NGIN::Utilities::Unexpected<KernelError>(value.Error());
            }
            architectures.push_back(value.Value());
          }
        }
      }
      return CoreResult<void>{};
    }

    [[nodiscard]] auto ParseUInt32Value(const std::string &value,
                                        const std::string_view context)
        -> CoreResult<NGIN::UInt32>
    {
      try
      {
        const auto parsed = static_cast<unsigned long>(std::stoul(value));
        return static_cast<NGIN::UInt32>(parsed);
      }
      catch (...)
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            std::string(context) + " must be an unsigned integer"));
      }
    }

    [[nodiscard]] auto ParsePackageBootstrapMode(const std::string_view text)
        -> CoreResult<PackageBootstrapMode>
    {
      if (text == "BuilderHook")
        return PackageBootstrapMode::BuilderHook;
      return NGIN::Utilities::Unexpected<KernelError>(
          MakeBuilderError("unknown package bootstrap mode", std::string(text),
                           KernelErrorCode::SchemaValidationFailure));
    }

    [[nodiscard]] auto ParseModuleFamilyText(const std::string_view text)
        -> CoreResult<ModuleFamily>
    {
      if (text == "Base")
        return ModuleFamily::Base;
      if (text == "Reflection")
        return ModuleFamily::Reflection;
      if (text == "Core")
        return ModuleFamily::Core;
      if (text == "Platform")
        return ModuleFamily::Platform;
      if (text == "Editor")
        return ModuleFamily::Editor;
      if (text == "Domain")
        return ModuleFamily::Domain;
      if (text == "App")
        return ModuleFamily::App;
      return NGIN::Utilities::Unexpected<KernelError>(
          MakeBuilderError("unknown module family", std::string(text),
                           KernelErrorCode::SchemaValidationFailure));
    }

    [[nodiscard]] auto ParseModuleTypeText(const std::string_view text)
        -> CoreResult<ModuleType>
    {
      if (text == "Runtime")
        return ModuleType::Runtime;
      if (text == "Editor")
        return ModuleType::Editor;
      if (text == "Program")
        return ModuleType::Program;
      if (text == "Developer")
        return ModuleType::Developer;
      if (text == "ThirdParty")
        return ModuleType::ThirdParty;
      return NGIN::Utilities::Unexpected<KernelError>(
          MakeBuilderError("unknown module type", std::string(text),
                           KernelErrorCode::SchemaValidationFailure));
    }

    [[nodiscard]] auto ParseStartupStageText(const std::string_view text)
        -> CoreResult<StartupStage>
    {
      if (text == "Foundation")
        return StartupStage::Foundation;
      if (text == "Platform")
        return StartupStage::Platform;
      if (text == "Services")
        return StartupStage::Services;
      if (text == "Features")
        return StartupStage::Features;
      if (text == "Presentation")
        return StartupStage::Presentation;
      return NGIN::Utilities::Unexpected<KernelError>(
          MakeBuilderError("unknown startup stage", std::string(text),
                           KernelErrorCode::SchemaValidationFailure));
    }

    [[nodiscard]] auto ParsePackageModuleDescriptor(const XmlElement &element,
                                                    const std::string_view context)
        -> CoreResult<ModuleDescriptor>
    {
      ModuleDescriptor descriptor{};

      auto name = RequireAttribute(element, "Name", context);
      if (!name)
      {
        return NGIN::Utilities::Unexpected<KernelError>(name.Error());
      }
      descriptor.name = name.Value();

      auto familyText = OptionalAttribute(element, "Family", context, "Core");
      if (!familyText)
      {
        return NGIN::Utilities::Unexpected<KernelError>(familyText.Error());
      }
      auto family = ParseModuleFamilyText(familyText.Value());
      if (!family)
      {
        return NGIN::Utilities::Unexpected<KernelError>(family.Error());
      }
      descriptor.family = family.Value();

      auto typeText = OptionalAttribute(element, "Type", context, "Runtime");
      if (!typeText)
      {
        return NGIN::Utilities::Unexpected<KernelError>(typeText.Error());
      }
      auto type = ParseModuleTypeText(typeText.Value());
      if (!type)
      {
        return NGIN::Utilities::Unexpected<KernelError>(type.Error());
      }
      descriptor.type = type.Value();

      auto stageText =
          OptionalAttribute(element, "Stage", context,
                            Attribute(element, "StartupStage").value_or("Features"));
      if (!stageText)
      {
        return NGIN::Utilities::Unexpected<KernelError>(stageText.Error());
      }
      auto stage = ParseStartupStageText(stageText.Value());
      if (!stage)
      {
        return NGIN::Utilities::Unexpected<KernelError>(stage.Error());
      }
      descriptor.startupStage = stage.Value();

      auto versionText = OptionalAttribute(element, "Version", context, {});
      if (!versionText)
      {
        return NGIN::Utilities::Unexpected<KernelError>(versionText.Error());
      }
      if (!versionText.Value().empty())
      {
        auto version = ParseSemanticVersion(versionText.Value());
        if (!version)
        {
          return NGIN::Utilities::Unexpected<KernelError>(version.Error());
        }
        descriptor.version = version.Value();
      }

      auto rangeText =
          OptionalAttribute(element, "CompatiblePlatformRange", context, {});
      if (!rangeText)
      {
        return NGIN::Utilities::Unexpected<KernelError>(rangeText.Error());
      }
      if (!rangeText.Value().empty())
      {
        auto range = ParseVersionRange(rangeText.Value());
        if (!range)
        {
          return NGIN::Utilities::Unexpected<KernelError>(range.Error());
        }
        descriptor.compatiblePlatformRange = range.Value();
      }

      auto reflectionRequired = OptionalBoolAttribute(
          element, "ReflectionRequired",
          std::string(context) + ".ReflectionRequired", false);
      if (!reflectionRequired)
      {
        return NGIN::Utilities::Unexpected<KernelError>(reflectionRequired.Error());
      }
      descriptor.reflectionRequired = reflectionRequired.Value();
      descriptor.profile = Attribute(element, "Profile").value_or("");
      descriptor.platform = Attribute(element, "Platform").value_or("");
      descriptor.operatingSystem = Attribute(element, "OperatingSystem").value_or("");
      descriptor.architecture = Attribute(element, "Architecture").value_or("");
      descriptor.toolchain = Attribute(element, "Toolchain").value_or("");
      descriptor.environment = Attribute(element, "Environment").value_or("");
      descriptor.condition = Attribute(element, "Condition").value_or("");

      auto compatibility = ReadCompatibility(element, context,
                                             descriptor.operatingSystems,
                                             descriptor.architectures);
      if (!compatibility)
      {
        return NGIN::Utilities::Unexpected<KernelError>(compatibility.Error());
      }

      if (const auto *dependencies = FindChild(element, "Dependencies"))
      {
        for (const auto *dependencyElement :
             ChildElements(*dependencies, "Dependency"))
        {
          auto dependencyName = RequireAttribute(
              *dependencyElement, "Name", std::string(context) + ".Dependencies");
          if (!dependencyName)
          {
            return NGIN::Utilities::Unexpected<KernelError>(dependencyName.Error());
          }

          auto optional = OptionalBoolAttribute(
              *dependencyElement, "Optional",
              std::string(context) + ".Dependencies.Optional", false);
          if (!optional)
          {
            return NGIN::Utilities::Unexpected<KernelError>(optional.Error());
          }

          DependencyDescriptor dependency{};
          dependency.name = dependencyName.Value();
          dependency.optional = optional.Value();

          auto requiredVersion = OptionalAttribute(
              *dependencyElement, "RequiredVersion",
              std::string(context) + ".Dependencies.RequiredVersion", {});
          if (!requiredVersion)
          {
            return NGIN::Utilities::Unexpected<KernelError>(
                requiredVersion.Error());
          }
          if (!requiredVersion.Value().empty())
          {
            auto range = ParseVersionRange(requiredVersion.Value());
            if (!range)
            {
              return NGIN::Utilities::Unexpected<KernelError>(range.Error());
            }
            dependency.requiredVersion = range.Value();
          }

          descriptor.dependencies.push_back(std::move(dependency));
        }
      }

      if (const auto *providesServices = FindChild(element, "ProvidesServices"))
      {
        for (const auto *serviceElement :
             ChildElements(*providesServices, "Service"))
        {
          auto service = RequireAttribute(
              *serviceElement, "Name", std::string(context) + ".ProvidesServices");
          if (!service)
          {
            return NGIN::Utilities::Unexpected<KernelError>(service.Error());
          }
          descriptor.providesServices.push_back(service.Value());
        }
      }

      if (const auto *requiresServices = FindChild(element, "RequiresServices"))
      {
        for (const auto *serviceElement :
             ChildElements(*requiresServices, "Service"))
        {
          auto service = RequireAttribute(
              *serviceElement, "Name", std::string(context) + ".RequiresServices");
          if (!service)
          {
            return NGIN::Utilities::Unexpected<KernelError>(service.Error());
          }
          descriptor.requiresServices.push_back(service.Value());
        }
      }

      if (const auto *capabilities = FindChild(element, "Capabilities"))
      {
        for (const auto *capabilityElement :
             ChildElements(*capabilities, "Capability"))
        {
          auto capability = RequireAttribute(
              *capabilityElement, "Name", std::string(context) + ".Capabilities");
          if (!capability)
          {
            return NGIN::Utilities::Unexpected<KernelError>(capability.Error());
          }
          auto exclusive = OptionalBoolAttribute(
              *capabilityElement, "Exclusive",
              std::string(context) + ".Capabilities.Exclusive", false);
          if (!exclusive)
          {
            return NGIN::Utilities::Unexpected<KernelError>(exclusive.Error());
          }
          descriptor.capabilities.push_back(ModuleCapability{
              .name = capability.Value(),
              .exclusive = exclusive.Value(),
          });
        }
      }

      return descriptor;
    }

    [[nodiscard]] auto ParsePackageReference(const XmlElement &element,
                                             const std::string_view context)
        -> CoreResult<PackageReference>
    {
      auto name = RequireAttribute(element, "Name", context);
      if (!name)
      {
        return NGIN::Utilities::Unexpected<KernelError>(name.Error());
      }

      auto versionRange = OptionalAttribute(element, "Version", context);
      if (!versionRange)
      {
        return NGIN::Utilities::Unexpected<KernelError>(versionRange.Error());
      }
      if (versionRange.Value().empty())
      {
        versionRange = OptionalAttribute(element, "VersionRange", context);
      }
      if (!versionRange)
      {
        return NGIN::Utilities::Unexpected<KernelError>(versionRange.Error());
      }

      auto optional = OptionalBoolAttribute(
          element, "Optional", std::string(context) + " attribute 'Optional'",
          false);
      if (!optional)
      {
        return NGIN::Utilities::Unexpected<KernelError>(optional.Error());
      }

      return PackageReference{
          .name = name.Value(),
          .versionRange = versionRange.Value(),
          .optional = optional.Value(),
          .profile = Attribute(element, "Profile").value_or(""),
          .platform = Attribute(element, "Platform").value_or(""),
          .operatingSystem = Attribute(element, "OperatingSystem").value_or(""),
          .architecture = Attribute(element, "Architecture").value_or(""),
          .toolchain = Attribute(element, "Toolchain").value_or(""),
          .environment = Attribute(element, "Environment").value_or(""),
          .condition = Attribute(element, "Condition").value_or(""),
      };
    }

    [[nodiscard]] auto ParseProjectReference(const XmlElement &element,
                                             const std::string_view context)
        -> CoreResult<ProjectReference>
    {
      ProjectReference reference{};

      auto path = RequireAttribute(element, "Path", context);
      if (!path)
      {
        return NGIN::Utilities::Unexpected<KernelError>(path.Error());
      }
      reference.path = path.Value();

      auto profile = OptionalAttribute(element, "Profile", context, {});
      if (!profile)
      {
        return NGIN::Utilities::Unexpected<KernelError>(profile.Error());
      }
      if (!profile.Value().empty())
      {
        reference.profile = profile.Value();
      }
      reference.platform = Attribute(element, "Platform").value_or("");
      reference.operatingSystem = Attribute(element, "OperatingSystem").value_or("");
      reference.architecture = Attribute(element, "Architecture").value_or("");
      reference.toolchain = Attribute(element, "Toolchain").value_or("");
      reference.environment = Attribute(element, "Environment").value_or("");
      reference.condition = Attribute(element, "Condition").value_or("");

      return reference;
    }

    [[nodiscard]] auto ReadConditionMatch(const XmlElement &element) -> ConditionNode
    {
      ConditionNode node{};
      node.kind = "Match";
      node.profile = Attribute(element, "Profile").value_or("");
      node.platform = Attribute(element, "Platform").value_or("");
      node.operatingSystem = Attribute(element, "OperatingSystem").value_or("");
      node.architecture = Attribute(element, "Architecture").value_or("");
      node.toolchain = Attribute(element, "Toolchain").value_or("");
      node.environment = Attribute(element, "Environment").value_or("");
      return node;
    }

    [[nodiscard]] auto ParseConditionNode(const XmlElement &element) -> ConditionNode
    {
      if (element.name == "Match")
      {
        return ReadConditionMatch(element);
      }
      ConditionNode node{};
      node.kind = std::string(element.name);
      if (element.name == "ConditionRef")
      {
        node.conditionName = Attribute(element, "Name").value_or("");
        return node;
      }
      for (const auto *child : ChildElements(element))
      {
        if (child->name == "Match" || child->name == "All" ||
            child->name == "Any" || child->name == "Not" ||
            child->name == "ConditionRef")
        {
          node.children.push_back(ParseConditionNode(*child));
        }
      }
      return node;
    }

    [[nodiscard]] auto ParseConditionDefinitions(const XmlElement &root)
        -> std::vector<ConditionDefinition>
    {
      std::vector<ConditionDefinition> conditions{};
      const auto *conditionsElement = FindChild(root, "Conditions");
      if (conditionsElement == nullptr)
      {
        return conditions;
      }
      for (const auto *conditionElement : ChildElements(*conditionsElement, "Condition"))
      {
        ConditionDefinition condition{};
        condition.name = Attribute(*conditionElement, "Name").value_or("");
        condition.body = ReadConditionMatch(*conditionElement);
        if (condition.body.profile.empty() && condition.body.platform.empty() &&
            condition.body.operatingSystem.empty() && condition.body.architecture.empty() &&
            condition.body.toolchain.empty() && condition.body.environment.empty())
        {
          for (const auto *child : ChildElements(*conditionElement))
          {
            if (child->name == "Match" || child->name == "All" ||
                child->name == "Any" || child->name == "Not" ||
                child->name == "ConditionRef")
            {
              condition.body = ParseConditionNode(*child);
              break;
            }
          }
        }
        if (!condition.name.empty())
        {
          conditions.push_back(std::move(condition));
        }
      }
      return conditions;
    }

    [[nodiscard]] auto DirectConditionMatches(const ConditionNode &condition,
                                              const ProfileDefinition &profile)
        -> bool
    {
      return (condition.profile.empty() || condition.profile == profile.name) &&
             (condition.platform.empty() || condition.platform == profile.platform) &&
             (condition.operatingSystem.empty() ||
              condition.operatingSystem == profile.operatingSystem) &&
             (condition.architecture.empty() ||
              condition.architecture == profile.architecture) &&
             (condition.toolchain.empty() || condition.toolchain == profile.toolchain) &&
             (condition.environment.empty() ||
              condition.environment == profile.environmentName);
    }

    [[nodiscard]] auto ConditionNodeMatches(
        const ConditionNode &node,
        const std::vector<ConditionDefinition> &conditions,
        const ProfileDefinition &profile) -> bool;

    [[nodiscard]] auto NamedConditionMatches(
        const std::string &name,
        const std::vector<ConditionDefinition> &conditions,
        const ProfileDefinition &profile) -> bool
    {
      if (name.empty()) { return true; }
      if (name == "Debug" || name == "Release" ||
          name == "RelWithDebInfo" || name == "MinSizeRel")
      {
        return profile.name == name;
      }
      if (name == "Windows") { return profile.operatingSystem == "windows"; }
      if (name == "Linux") { return profile.operatingSystem == "linux"; }
      if (name == "MacOS") { return profile.operatingSystem == "macos"; }
      if (name == "X64") { return profile.architecture == "x64"; }
      if (name == "Arm64") { return profile.architecture == "arm64"; }
      if (name == "Desktop")
      {
        return profile.operatingSystem == "windows" ||
               profile.operatingSystem == "linux" ||
               profile.operatingSystem == "macos";
      }
      if (name == "Local") { return profile.environmentName == "local"; }
      if (name == "Development") { return profile.environmentName == "development"; }
      if (name == "Production") { return profile.environmentName == "production"; }
      const auto it = std::find_if(
          conditions.begin(), conditions.end(),
          [&](const ConditionDefinition &condition)
          {
            return condition.name == name;
          });
      return it != conditions.end() && ConditionNodeMatches(it->body, conditions, profile);
    }

    [[nodiscard]] auto ConditionNodeMatches(
        const ConditionNode &node,
        const std::vector<ConditionDefinition> &conditions,
        const ProfileDefinition &profile) -> bool
    {
      if (node.kind == "Match")
      {
        return DirectConditionMatches(node, profile);
      }
      if (node.kind == "ConditionRef")
      {
        return NamedConditionMatches(node.conditionName, conditions, profile);
      }
      if (node.kind == "All")
      {
        return std::all_of(node.children.begin(), node.children.end(), [&](const ConditionNode &child)
        {
          return ConditionNodeMatches(child, conditions, profile);
        });
      }
      if (node.kind == "Any")
      {
        return std::any_of(node.children.begin(), node.children.end(), [&](const ConditionNode &child)
        {
          return ConditionNodeMatches(child, conditions, profile);
        });
      }
      if (node.kind == "Not")
      {
        return node.children.size() == 1 && !ConditionNodeMatches(node.children.front(), conditions, profile);
      }
      return false;
    }

    template <typename T>
    [[nodiscard]] auto SelectorsMatch(const T &value,
                                      const std::vector<ConditionDefinition> &conditions,
                                      const ProfileDefinition &profile,
                                      const bool useProfileSelector = true)
        -> bool
    {
      return (!useProfileSelector || value.profile.empty() || value.profile == profile.name) &&
             (value.platform.empty() || value.platform == profile.platform) &&
             (value.operatingSystem.empty() || value.operatingSystem == profile.operatingSystem) &&
             (value.architecture.empty() || value.architecture == profile.architecture) &&
             (value.toolchain.empty() || value.toolchain == profile.toolchain) &&
             (value.environment.empty() || value.environment == profile.environmentName) &&
             NamedConditionMatches(value.condition, conditions, profile);
    }

    [[nodiscard]] auto ProjectReferenceSelectorsMatch(
        const ProjectReference &reference,
        const std::vector<ConditionDefinition> &conditions,
        const ProfileDefinition &profile) -> bool
    {
      return (reference.platform.empty() || reference.platform == profile.platform) &&
             (reference.operatingSystem.empty() ||
              reference.operatingSystem == profile.operatingSystem) &&
             (reference.architecture.empty() ||
              reference.architecture == profile.architecture) &&
             (reference.toolchain.empty() || reference.toolchain == profile.toolchain) &&
             (reference.environment.empty() ||
              reference.environment == profile.environmentName) &&
             NamedConditionMatches(reference.condition, conditions, profile);
    }

    [[nodiscard]] auto ParseToolDeclaration(const XmlElement &element,
                                            const std::string_view context,
                                            const bool requireName)
        -> CoreResult<ToolDeclaration>
    {
      ToolDeclaration tool{};
      if (requireName)
      {
        auto name = RequireAttribute(element, "Name", context);
        if (!name)
        {
          return NGIN::Utilities::Unexpected<KernelError>(name.Error());
        }
        tool.name = name.Value();
      }
      else
      {
        tool.name = Attribute(element, "Name").value_or("");
      }
      tool.kind = Attribute(element, "Kind").value_or("Generator");
      tool.executable = Attribute(element, "Executable").value_or("");
      tool.profile = Attribute(element, "Profile").value_or("");
      tool.platform = Attribute(element, "Platform").value_or("");
      tool.operatingSystem = Attribute(element, "OperatingSystem").value_or("");
      tool.architecture = Attribute(element, "Architecture").value_or("");
      tool.toolchain = Attribute(element, "Toolchain").value_or("");
      tool.environment = Attribute(element, "Environment").value_or("");
      tool.condition = Attribute(element, "Condition").value_or("");
      if (Attribute(element, "BuiltIn").has_value())
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            std::string(context) + " no longer supports BuiltIn tools",
            tool.name, KernelErrorCode::SchemaValidationFailure));
      }

      if (tool.kind != "Generator")
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            std::string(context) + " uses unsupported tool kind",
            tool.kind, KernelErrorCode::SchemaValidationFailure));
      }
      if (tool.executable.empty())
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            std::string(context) + " must declare Executable",
            tool.name, KernelErrorCode::SchemaValidationFailure));
      }
      return tool;
    }

    [[nodiscard]] auto ParseRuntimeDefinition(const XmlElement *runtimeElement,
                                              const std::string_view context)
        -> CoreResult<RuntimeDefinition>
    {
      RuntimeDefinition runtime{};
      if (runtimeElement == nullptr)
      {
        return runtime;
      }

      auto parseModule = [&](const XmlElement &moduleElement,
                             const std::string &moduleContext) -> CoreResult<void>
      {
        auto descriptor = ParsePackageModuleDescriptor(moduleElement, moduleContext);
        if (!descriptor)
        {
          return NGIN::Utilities::Unexpected<KernelError>(descriptor.Error());
        }
        for (const auto *provides : ChildElements(moduleElement, "Provides"))
        {
          if (const auto service = Attribute(*provides, "Service"); service.has_value())
          {
            descriptor.Value().providesServices.push_back(*service);
          }
        }
        for (const auto *requirement : ChildElements(moduleElement, "Requires"))
        {
          if (const auto service = Attribute(*requirement, "Service"); service.has_value())
          {
            descriptor.Value().requiresServices.push_back(*service);
          }
        }
        runtime.enableModules.push_back(descriptor.Value().name);
        runtime.modules.push_back(descriptor.Value());
        return {};
      };

      std::size_t moduleIndex = 0;
      for (const auto *moduleElement : ChildElements(*runtimeElement, "Module"))
      {
        auto parsed = parseModule(
            *moduleElement,
            std::string(context) + ".Module[" + std::to_string(moduleIndex++) + "]");
        if (!parsed)
        {
          return NGIN::Utilities::Unexpected<KernelError>(parsed.Error());
        }
      }

      for (const auto *moduleRef : ChildElements(*runtimeElement, "ModuleRef"))
      {
        auto moduleName = RequireAttribute(
            *moduleRef, "Name", std::string(context) + ".ModuleRef");
        if (!moduleName)
        {
          return NGIN::Utilities::Unexpected<KernelError>(moduleName.Error());
        }
        runtime.enableModules.push_back(moduleName.Value());
      }

      for (const auto *moduleRef : ChildElements(*runtimeElement, "DisableModule"))
      {
        auto moduleName = RequireAttribute(
            *moduleRef, "Name", std::string(context) + ".DisableModule");
        if (!moduleName)
        {
          return NGIN::Utilities::Unexpected<KernelError>(moduleName.Error());
        }
        runtime.disableModules.push_back(moduleName.Value());
      }

      return runtime;
    }

    [[nodiscard]] auto ParseProjectManifestText(const std::string &text,
                                                IoFileSystem *fileSystem = nullptr,
                                                const IoPath *manifestPath = nullptr)
        -> CoreResult<ProjectManifest>
    {
      (void)fileSystem;
      (void)manifestPath;

      auto loaded = LoadXmlDocument(text, "project");
      if (!loaded)
      {
        return NGIN::Utilities::Unexpected<KernelError>(loaded.Error());
      }

      const auto *root = loaded.Value().document.Root();
      if (root == nullptr || root->name != "Project")
      {
        return NGIN::Utilities::Unexpected<KernelError>(
            MakeBuilderError("project manifest root element must be <Project>"));
      }

      ProjectManifest manifest{};

      auto schemaVersion =
          OptionalAttribute(*root, "SchemaVersion", "project", "4");
      if (!schemaVersion)
      {
        return NGIN::Utilities::Unexpected<KernelError>(schemaVersion.Error());
      }
      auto parsedSchema =
          ParseUInt32Value(schemaVersion.Value(), "project.SchemaVersion");
      if (!parsedSchema)
      {
        return NGIN::Utilities::Unexpected<KernelError>(parsedSchema.Error());
      }
      manifest.schemaVersion = parsedSchema.Value();
      if (manifest.schemaVersion != 4)
      {
        return NGIN::Utilities::Unexpected<KernelError>(
            MakeBuilderError("unsupported project schema version",
                             std::to_string(manifest.schemaVersion),
                             KernelErrorCode::InvalidArgument));
      }

      auto name = RequireAttribute(*root, "Name", "project");
      if (!name)
      {
        return NGIN::Utilities::Unexpected<KernelError>(name.Error());
      }
      manifest.name = name.Value();
      manifest.conditions = ParseConditionDefinitions(*root);

      const XmlElement *product = nullptr;
      for (const auto *child : ChildElements(*root))
      {
        if (child->name == "Application" || child->name == "Library" ||
            child->name == "Tool" || child->name == "Test" ||
            child->name == "Benchmark" || child->name == "Plugin" ||
            child->name == "Module" || child->name == "External")
        {
          if (product != nullptr)
          {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeBuilderError("project must declare exactly one product element"));
          }
          product = child;
        }
      }
      if (product == nullptr)
      {
        return NGIN::Utilities::Unexpected<KernelError>(
            MakeBuilderError("project must declare a product element"));
      }

      manifest.type = product->name == "Library" || product->name == "Plugin" ||
                              product->name == "Module" || product->name == "External"
                          ? "Library"
                          : product->name == "Tool" ? "Tool" : "Application";
      manifest.defaultProfile = Attribute(*root, "DefaultProfile").value_or("dev");
      manifest.output = OutputDefinition{
          .kind = product->name == "Library" || product->name == "Module" ||
                          product->name == "External"
                      ? "StaticLibrary"
                      : product->name == "Plugin" ? "SharedLibrary" : "Executable",
          .name = manifest.name,
          .target = manifest.name,
      };
      manifest.build = ProjectBuildDescriptor{};

      auto addEnvironment = [&](const std::string &environmentName)
      {
        if (environmentName.empty())
        {
          return;
        }
        const auto exists = std::any_of(
            manifest.environments.begin(), manifest.environments.end(),
            [&](const EnvironmentDefinition &environment)
            {
              return environment.name == environmentName;
            });
        if (!exists)
        {
          manifest.environments.push_back(EnvironmentDefinition{.name = environmentName});
        }
      };

      auto applyTargetPlatform = [](ProfileDefinition &profile,
                                    const std::string &platform)
      {
        profile.platform = platform;
        profile.operatingSystem = "linux";
        profile.architecture = "x64";
        if (platform == "windows-x64")
        {
          profile.operatingSystem = "windows";
        }
        else if (platform == "macos-x64")
        {
          profile.operatingSystem = "macos";
        }
        else if (platform == "macos-arm64" || platform == "linux-arm64")
        {
          profile.operatingSystem = platform == "macos-arm64" ? "macos" : "linux";
          profile.architecture = "arm64";
        }
      };

      auto parseUses = [&](const XmlElement &owner, const std::string &context)
          -> CoreResult<void>
      {
        const auto *uses = FindChild(owner, "Uses");
        if (uses == nullptr)
        {
          return {};
        }
        for (const auto *dependency : ChildElements(*uses))
        {
          if (dependency->name == "Project")
          {
            auto reference = ParseProjectReference(
                *dependency, context + ".Uses.Project");
            if (!reference)
            {
              return NGIN::Utilities::Unexpected<KernelError>(reference.Error());
            }
            manifest.projectRefs.push_back(reference.Value());
            continue;
          }
          if (dependency->name != "Package" && dependency->name != "Tool" &&
              dependency->name != "Runtime")
          {
            continue;
          }
          auto package = ParsePackageReference(*dependency, context + ".Uses");
          if (!package)
          {
            return NGIN::Utilities::Unexpected<KernelError>(package.Error());
          }
          manifest.packageRefs.push_back(package.Value());
          for (const auto *feature : ChildElements(*dependency, "Feature"))
          {
            auto featureName = RequireAttribute(
                *feature, "Name", context + ".Uses.Feature");
            if (!featureName)
            {
              return NGIN::Utilities::Unexpected<KernelError>(featureName.Error());
            }
            manifest.packageFeatureUses.push_back(PackageFeatureUse{
                .packageName = package.Value().name,
                .featureName = featureName.Value(),
                .versionRange = package.Value().versionRange,
            });
          }
        }
        return {};
      };

      auto addPathInput = [](std::vector<InputDeclaration> &target,
                             const XmlElement &node,
                             const std::string &kind,
                             const std::string &role,
                             const std::string &context) -> CoreResult<void>
      {
        auto path = RequireAttribute(node, "Path", context);
        if (!path)
        {
          return NGIN::Utilities::Unexpected<KernelError>(path.Error());
        }
        InputDeclaration input{};
        input.kind = kind;
        input.role = role;
        input.visibility = Attribute(node, "Visibility").value_or(
            role == "Header" ? "Public" : "Private");
        input.declaringScope = "project";
        input.mode = "Glob";
        input.includePatterns.push_back(path.Value());
        input.pattern = path.Value();
        target.push_back(std::move(input));
        return {};
      };

      auto parseBuild = [&](const XmlElement &owner, std::vector<InputDeclaration> &inputs,
                            ProjectBuildDescriptor &build,
                            const std::string &context) -> CoreResult<void>
      {
        const auto *buildElement = FindChild(owner, "Build");
        if (buildElement == nullptr)
        {
          return {};
        }
        if (const auto *language = FindChild(*buildElement, "Language"))
        {
          build.language = "CXX";
          build.languageStandard = Attribute(*language, "Standard").value_or("C++23");
          if (build.languageStandard.starts_with("C++"))
          {
            build.languageStandard = build.languageStandard.substr(3);
          }
        }
        for (const auto *node : ChildElements(*buildElement))
        {
          if (node->name == "Sources")
          {
            auto added = addPathInput(inputs, *node, "Source", "Source",
                                      context + ".Build.Sources");
            if (!added)
            {
              return NGIN::Utilities::Unexpected<KernelError>(added.Error());
            }
          }
          else if (node->name == "Headers")
          {
            auto added = addPathInput(inputs, *node, "Source", "Header",
                                      context + ".Build.Headers");
            if (!added)
            {
              return NGIN::Utilities::Unexpected<KernelError>(added.Error());
            }
          }
          else if (node->name == "IncludePath")
          {
            auto path = RequireAttribute(*node, "Path", context + ".Build.IncludePath");
            if (!path)
            {
              return NGIN::Utilities::Unexpected<KernelError>(path.Error());
            }
            build.includeDirectories.push_back(BuildSetting{
                .value = path.Value(),
                .visibility = Attribute(*node, "Visibility").value_or("Private"),
            });
          }
          else if (node->name == "Define")
          {
            auto defineName = RequireAttribute(*node, "Name", context + ".Build.Define");
            if (!defineName)
            {
              return NGIN::Utilities::Unexpected<KernelError>(defineName.Error());
            }
            BuildSetting setting{};
            setting.value = defineName.Value();
            if (const auto value = Attribute(*node, "Value"); value.has_value())
            {
              setting.value += "=" + *value;
            }
            setting.visibility = Attribute(*node, "Visibility").value_or("Private");
            build.compileDefinitions.push_back(std::move(setting));
          }
          else if (node->name == "CompileOption")
          {
            auto value = RequireAttribute(*node, "Value", context + ".Build.CompileOption");
            if (!value)
            {
              return NGIN::Utilities::Unexpected<KernelError>(value.Error());
            }
            build.compileOptions.push_back(BuildSetting{.value = value.Value()});
          }
          else if (node->name == "LinkOption" || node->name == "LinkLibrary")
          {
            auto value = RequireAttribute(
                *node, node->name == "LinkLibrary" ? "Name" : "Value",
                context + ".Build." + std::string(node->name));
            if (!value)
            {
              return NGIN::Utilities::Unexpected<KernelError>(value.Error());
            }
            build.linkOptions.push_back(BuildSetting{.value = value.Value()});
          }
        }
        return {};
      };

      auto parseStage = [&](const XmlElement &owner,
                            std::vector<InputDeclaration> &inputs,
                            const std::string &context) -> CoreResult<void>
      {
        const auto *stage = FindChild(owner, "Stage");
        if (stage == nullptr)
        {
          return {};
        }
        for (const auto *node : ChildElements(*stage))
        {
          if (node->name != "Config" && node->name != "Content" &&
              node->name != "Asset")
          {
            continue;
          }
          auto source = RequireAttribute(*node, "Source", context + ".Stage");
          if (!source)
          {
            return NGIN::Utilities::Unexpected<KernelError>(source.Error());
          }
          InputDeclaration input{};
          input.kind = node->name == "Config" ? "Config" : std::string(node->name);
          input.mode = source.Value().find('*') != std::string::npos ? "Glob" : "File";
          input.visibility = "Private";
          input.declaringScope = "project";
          input.path = source.Value();
          input.pattern = source.Value();
          input.target = Attribute(*node, "Target").value_or("");
          input.overrideExisting = Attribute(*node, "Collision").value_or("") == "Override";
          inputs.push_back(std::move(input));
        }
        return {};
      };

      auto parseRuntimeInto = [&](const XmlElement &owner,
                                  RuntimeDefinition &runtime,
                                  const std::string &context) -> CoreResult<void>
      {
        auto parsed = ParseRuntimeDefinition(FindChild(owner, "Runtime"),
                                             context + ".Runtime");
        if (!parsed)
        {
          return NGIN::Utilities::Unexpected<KernelError>(parsed.Error());
        }
        runtime.modules.insert(runtime.modules.end(),
                               parsed.Value().modules.begin(),
                               parsed.Value().modules.end());
        runtime.enableModules.insert(runtime.enableModules.end(),
                                     parsed.Value().enableModules.begin(),
                                     parsed.Value().enableModules.end());
        runtime.disableModules.insert(runtime.disableModules.end(),
                                      parsed.Value().disableModules.begin(),
                                      parsed.Value().disableModules.end());
        return {};
      };

      auto parseEnvironmentInto = [&](const XmlElement &owner,
                                      EnvironmentDefinition &environment) -> void
      {
        if (const auto *environmentElement = FindChild(owner, "Environment"))
        {
          for (const auto *env : ChildElements(*environmentElement, "Env"))
          {
            environment.variables.push_back(EnvironmentVariable{
                .name = Attribute(*env, "Name").value_or(""),
                .value = Attribute(*env, "Value").value_or(""),
            });
          }
        }
      };

      auto parseLaunch = [&](const XmlElement &owner,
                             LaunchDefinition current) -> LaunchDefinition
      {
        if (const auto *launchElement = FindChild(owner, "Launch"))
        {
          current.executable =
              Attribute(*launchElement, "Executable").value_or(current.executable);
          if (current.executable == "$(OutputName)")
          {
            current.executable = manifest.output.name;
          }
          current.workingDirectory =
              Attribute(*launchElement, "WorkingDirectory").value_or(current.workingDirectory);
        }
        return current;
      };

      auto parsedUses = parseUses(*product, "project." + std::string(product->name));
      if (!parsedUses)
      {
        return NGIN::Utilities::Unexpected<KernelError>(parsedUses.Error());
      }
      auto parsedBuild = parseBuild(*product, manifest.inputs, manifest.build,
                                    "project." + std::string(product->name));
      if (!parsedBuild)
      {
        return NGIN::Utilities::Unexpected<KernelError>(parsedBuild.Error());
      }
      auto parsedStage = parseStage(*product, manifest.inputs,
                                    "project." + std::string(product->name));
      if (!parsedStage)
      {
        return NGIN::Utilities::Unexpected<KernelError>(parsedStage.Error());
      }
      auto parsedRuntime = parseRuntimeInto(*product, manifest.runtime,
                                            "project." + std::string(product->name));
      if (!parsedRuntime)
      {
        return NGIN::Utilities::Unexpected<KernelError>(parsedRuntime.Error());
      }

      LaunchDefinition defaultLaunch{};
      defaultLaunch.executable = manifest.output.name;
      defaultLaunch.workingDirectory = "$(StageDir)";
      defaultLaunch = parseLaunch(*product, defaultLaunch);

      EnvironmentDefinition productEnvironment{};
      productEnvironment.name = "development";
      parseEnvironmentInto(*product, productEnvironment);

      auto parseProfileTraits = [&](const XmlElement &owner,
                                    ProfileDefinition &profile,
                                    const std::string &context)
          -> CoreResult<void>
      {
        for (const auto *node : ChildElements(owner))
        {
          if (node->name == "Optimization")
          {
            auto mode = RequireAttribute(*node, "Mode", context + ".Optimization");
            if (!mode)
            {
              return NGIN::Utilities::Unexpected<KernelError>(mode.Error());
            }
            if (mode.Value() != "Off" && mode.Value() != "Speed" &&
                mode.Value() != "Size")
            {
              return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
                  context + " uses unsupported optimization mode", mode.Value(),
                  KernelErrorCode::SchemaValidationFailure));
            }
            profile.optimization = mode.Value();
          }
          else if (node->name == "DebugSymbols" ||
                   node->name == "LinkTimeOptimization")
          {
            const auto traitContext = context + "." + std::string{node->name};
            auto enabled = RequireAttribute(*node, "Enabled", traitContext);
            if (!enabled)
            {
              return NGIN::Utilities::Unexpected<KernelError>(enabled.Error());
            }
            auto parsed = ParseBoolValue(enabled.Value(), traitContext);
            if (!parsed)
            {
              return NGIN::Utilities::Unexpected<KernelError>(parsed.Error());
            }
            if (node->name == "DebugSymbols")
            {
              profile.debugSymbols = parsed.Value();
            }
            else
            {
              profile.linkTimeOptimization = parsed.Value();
            }
          }
          else if (node->name == "Toolchain")
          {
            auto name = RequireAttribute(*node, "Name", context + ".Toolchain");
            if (!name)
            {
              return NGIN::Utilities::Unexpected<KernelError>(name.Error());
            }
            profile.toolchain = name.Value();
          }
        }
        return CoreResult<void>{};
      };

      std::unordered_map<std::string, std::size_t> profileIndexes{};
      for (const auto *profileElement : ChildElements(*root, "Profile"))
      {
        ProfileDefinition profile{};
        auto profileName = RequireAttribute(*profileElement, "Name", "project.Profile");
        if (!profileName)
        {
          return NGIN::Utilities::Unexpected<KernelError>(profileName.Error());
        }
        profile.name = profileName.Value();
        if (const auto extends = Attribute(*profileElement, "Extends");
            extends.has_value() && !extends->empty())
        {
          const auto parent = profileIndexes.find(*extends);
          if (parent == profileIndexes.end())
          {
            return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
                "profile extends unknown or later profile", *extends,
                KernelErrorCode::SchemaValidationFailure));
          }
          profile = manifest.profiles[parent->second];
          profile.name = profileName.Value();
        }
        else
        {
          profile.environmentName = "development";
          profile.launch = defaultLaunch;
        }
        if (const auto *defaults = FindChild(*profileElement, "Defaults"))
        {
          auto parsedTraits = parseProfileTraits(*defaults, profile, "project.Profile.Defaults");
          if (!parsedTraits)
          {
            return NGIN::Utilities::Unexpected<KernelError>(parsedTraits.Error());
          }
          for (const auto *node : ChildElements(*defaults))
          {
            if (node->name == "Environment")
            {
              profile.environmentName =
                  Attribute(*node, "Name").value_or(profile.environmentName);
            }
            else if (node->name == "TargetPlatform")
            {
              applyTargetPlatform(profile,
                                  Attribute(*node, "Name").value_or(profile.platform));
            }
          }
        }
        if (const auto *overlayProduct = FindChild(*profileElement, product->name))
        {
          if (const auto *build = FindChild(*overlayProduct, "Build"))
          {
            auto parsedTraits = parseProfileTraits(
                *build, profile,
                "project.Profile." + std::string(product->name) + ".Build");
            if (!parsedTraits)
            {
              return NGIN::Utilities::Unexpected<KernelError>(parsedTraits.Error());
            }
          }
          auto overlayBuild = parseBuild(*overlayProduct, profile.inputs,
                                         manifest.build,
                                         "project.Profile." + std::string(product->name));
          if (!overlayBuild)
          {
            return NGIN::Utilities::Unexpected<KernelError>(overlayBuild.Error());
          }
          auto overlayStage = parseStage(*overlayProduct, profile.inputs,
                                         "project.Profile." + std::string(product->name));
          if (!overlayStage)
          {
            return NGIN::Utilities::Unexpected<KernelError>(overlayStage.Error());
          }
          auto overlayRuntime = parseRuntimeInto(
              *overlayProduct, profile.runtime,
              "project.Profile." + std::string(product->name));
          if (!overlayRuntime)
          {
            return NGIN::Utilities::Unexpected<KernelError>(overlayRuntime.Error());
          }
          profile.launch = parseLaunch(*overlayProduct,
                                       profile.launch.value_or(defaultLaunch));
        }
        addEnvironment(profile.environmentName);
        profileIndexes.emplace(profile.name, manifest.profiles.size());
        manifest.profiles.push_back(std::move(profile));
      }

      if (manifest.profiles.empty())
      {
        ProfileDefinition profile{};
        profile.name = manifest.defaultProfile;
        profile.environmentName = productEnvironment.name;
        profile.launch = defaultLaunch;
        addEnvironment(profile.environmentName);
        manifest.profiles.push_back(std::move(profile));
      }
      else
      {
        addEnvironment(productEnvironment.name);
      }
      for (auto &environment : manifest.environments)
      {
        if (environment.name == productEnvironment.name)
        {
          environment.variables.insert(environment.variables.end(),
                                       productEnvironment.variables.begin(),
                                       productEnvironment.variables.end());
          break;
        }
      }

      return manifest;
    }

    struct ResolvedProjectUnit
    {
      ProjectManifest manifest{};
      ProfileDefinition profile{};
      std::optional<EnvironmentDefinition> environment{};
      IoPath directory{};
    };

    [[nodiscard]] auto FindEnvironment(const ProjectManifest &manifest,
                                       const std::string &environmentName)
        -> CoreResult<std::optional<EnvironmentDefinition>>
    {
      if (environmentName.empty())
      {
        return std::optional<EnvironmentDefinition>{};
      }

      const auto it = std::find_if(
          manifest.environments.begin(), manifest.environments.end(),
          [&](const EnvironmentDefinition &candidate)
          {
            return candidate.name == environmentName;
          });
      if (it == manifest.environments.end())
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            "selected environment was not found", environmentName,
            KernelErrorCode::NotFound));
      }

      return std::optional<EnvironmentDefinition>{*it};
    }

    [[nodiscard]] auto FindProfile(
        const ProjectManifest &manifest,
        const std::optional<std::string> &overrideProfile = std::nullopt,
        const bool allowDefaultFallback = false)
        -> CoreResult<ProfileDefinition>
    {
      auto findByName = [&](const std::string &name)
          -> std::optional<ProfileDefinition>
      {
        const auto it =
            std::find_if(manifest.profiles.begin(), manifest.profiles.end(),
                         [&](const ProfileDefinition &candidate)
                         {
                           return candidate.name == name;
                         });
        if (it == manifest.profiles.end())
        {
          return std::nullopt;
        }
        return *it;
      };

      if (overrideProfile.has_value() && !overrideProfile->empty())
      {
        if (const auto selected = findByName(*overrideProfile);
            selected.has_value())
        {
          return *selected;
        }
        if (!allowDefaultFallback)
        {
          return NGIN::Utilities::Unexpected<KernelError>(
              MakeBuilderError("selected profile was not found",
                               *overrideProfile,
                               KernelErrorCode::NotFound));
        }
      }

      if (manifest.defaultProfile.empty())
      {
        return NGIN::Utilities::Unexpected<KernelError>(
            MakeBuilderError("project has no selected profile", manifest.name,
                             KernelErrorCode::InvalidArgument));
      }

      if (const auto selected = findByName(manifest.defaultProfile);
          selected.has_value())
      {
        return *selected;
      }

      if (overrideProfile.has_value() && !overrideProfile->empty())
      {
        return NGIN::Utilities::Unexpected<KernelError>(
            MakeBuilderError("selected profile was not found",
                             *overrideProfile, KernelErrorCode::NotFound));
      }
      return NGIN::Utilities::Unexpected<KernelError>(
          MakeBuilderError("selected profile was not found",
                           manifest.defaultProfile,
                           KernelErrorCode::NotFound));
    }

    [[nodiscard]] auto LoadProjectManifestFile(IoFileSystem &fileSystem,
                                               const IoPath &filePath)
        -> CoreResult<std::pair<ProjectManifest, IoPath>>
    {
      auto fileText = ReadTextFile(fileSystem, filePath, "project");
      if (!fileText)
      {
        return NGIN::Utilities::Unexpected<KernelError>(fileText.Error());
      }

      auto absolutePath = AbsolutePath(fileSystem, filePath);
      if (!absolutePath)
      {
        return NGIN::Utilities::Unexpected<KernelError>(absolutePath.Error());
      }

      auto manifest =
          ParseProjectManifestText(fileText.Value(), &fileSystem,
                                   &absolutePath.Value());
      if (!manifest)
      {
        return NGIN::Utilities::Unexpected<KernelError>(manifest.Error());
      }

      return std::pair<ProjectManifest, IoPath>{
          manifest.Value(),
          absolutePath.Value().Parent(),
      };
    }

    auto CollectProjectClosure(
        IoFileSystem &fileSystem, const IoPath &filePath,
        const std::optional<std::string> &selectedProfile,
        const bool allowDefaultFallbackForSelection,
        std::vector<ResolvedProjectUnit> &out, std::set<std::string> &visiting,
        std::set<std::string> &visited) -> CoreResult<void>
    {
      auto canonicalPath = WeaklyCanonicalPath(fileSystem, filePath);
      if (!canonicalPath)
      {
        return NGIN::Utilities::Unexpected<KernelError>(canonicalPath.Error());
      }

      const std::string canonicalKey = ToString(canonicalPath.Value());
      if (visited.contains(canonicalKey))
      {
        return {};
      }
      if (visiting.contains(canonicalKey))
      {
        return NGIN::Utilities::Unexpected<KernelError>(
            MakeBuilderError("project reference cycle detected", canonicalKey,
                             KernelErrorCode::DependencyCycle));
      }
      visiting.insert(canonicalKey);

      auto loaded = LoadProjectManifestFile(fileSystem, canonicalPath.Value());
      if (!loaded)
      {
        return NGIN::Utilities::Unexpected<KernelError>(loaded.Error());
      }

      auto manifest = std::move(loaded.Value().first);
      auto directory = std::move(loaded.Value().second);

      auto profile = FindProfile(manifest, selectedProfile,
                                             allowDefaultFallbackForSelection);
      if (!profile)
      {
        return NGIN::Utilities::Unexpected<KernelError>(profile.Error());
      }

      auto environment =
          FindEnvironment(manifest, profile.Value().environmentName);
      if (!environment)
      {
        return NGIN::Utilities::Unexpected<KernelError>(environment.Error());
      }

      auto collectReference =
          [&](const ProjectReference &reference) -> CoreResult<void>
      {
        IoPath referencedPath(reference.path);
        if (referencedPath.IsRelative() && !directory.IsEmpty())
        {
          referencedPath = directory.Join(referencedPath.View());
        }
        referencedPath = referencedPath.LexicallyNormal();

        const std::optional<std::string> referencedProfile =
            reference.profile.has_value()
                ? reference.profile
                : std::optional<std::string>{profile.Value().name};

        auto result =
            CollectProjectClosure(fileSystem, referencedPath,
                                  referencedProfile,
                                  !reference.profile.has_value(), out,
                                  visiting, visited);
        if (!result)
        {
          return NGIN::Utilities::Unexpected<KernelError>(result.Error());
        }
        return {};
      };

      for (const auto &reference : manifest.projectRefs)
      {
        if (!ProjectReferenceSelectorsMatch(reference, manifest.conditions, profile.Value()))
        {
          continue;
        }
        auto result = collectReference(reference);
        if (!result)
        {
          return result;
        }
      }
      if (environment.Value().has_value())
      {
        for (const auto &reference : environment.Value()->projectRefs)
        {
          if (!ProjectReferenceSelectorsMatch(reference, manifest.conditions, profile.Value()))
          {
            continue;
          }
          auto result = collectReference(reference);
          if (!result)
          {
            return result;
          }
        }
      }
      for (const auto &reference : profile.Value().projectRefs)
      {
        if (!ProjectReferenceSelectorsMatch(reference, manifest.conditions, profile.Value()))
        {
          continue;
        }
        auto result = collectReference(reference);
        if (!result)
        {
          return result;
        }
      }

      out.push_back(ResolvedProjectUnit{
          .manifest = std::move(manifest),
          .profile = profile.Value(),
          .environment = environment.Value(),
          .directory = std::move(directory),
      });

      visiting.erase(canonicalKey);
      visited.insert(canonicalKey);
      return {};
    }

    [[nodiscard]] auto ParsePackageManifestText(const std::string &text)
        -> CoreResult<PackageManifest>
    {
      auto loaded = LoadXmlDocument(text, "package");
      if (!loaded)
      {
        return NGIN::Utilities::Unexpected<KernelError>(loaded.Error());
      }

      const auto *root = loaded.Value().document.Root();
      if (root == nullptr || root->name != "Package")
      {
        return NGIN::Utilities::Unexpected<KernelError>(
            MakeBuilderError("package manifest root element must be <Package>"));
      }

      PackageManifest manifest{};

      auto schemaVersion =
          OptionalAttribute(*root, "SchemaVersion", "package", "4");
      if (!schemaVersion)
      {
        return NGIN::Utilities::Unexpected<KernelError>(schemaVersion.Error());
      }
      auto parsedSchema =
          ParseUInt32Value(schemaVersion.Value(), "package.SchemaVersion");
      if (!parsedSchema)
      {
        return NGIN::Utilities::Unexpected<KernelError>(parsedSchema.Error());
      }
      manifest.schemaVersion = parsedSchema.Value();
      if (manifest.schemaVersion != 4)
      {
        return NGIN::Utilities::Unexpected<KernelError>(
            MakeBuilderError("unsupported package schema version",
                             std::to_string(manifest.schemaVersion),
                             KernelErrorCode::InvalidArgument));
      }

      auto name = RequireAttribute(*root, "Name", "package");
      if (!name)
      {
        return NGIN::Utilities::Unexpected<KernelError>(name.Error());
      }
      manifest.name = name.Value();
      manifest.conditions = ParseConditionDefinitions(*root);

      auto version = RequireAttribute(*root, "Version", "package");
      if (!version)
      {
        return NGIN::Utilities::Unexpected<KernelError>(version.Error());
      }
      manifest.version = version.Value();

      auto platformRange =
          OptionalAttribute(*root, "CompatiblePlatformRange", "package", ">=0.0.0");
      if (!platformRange)
      {
        return NGIN::Utilities::Unexpected<KernelError>(platformRange.Error());
      }
      manifest.compatiblePlatformRange = platformRange.Value();

      auto compatibility = ReadCompatibility(*root, "package",
                                             manifest.operatingSystems,
                                             manifest.architectures);
      if (!compatibility)
      {
        return NGIN::Utilities::Unexpected<KernelError>(compatibility.Error());
      }

      auto parseUses = [&](const XmlElement &owner,
                           std::vector<PackageReference> &dependencies,
                           const std::string &context) -> CoreResult<void>
      {
        const auto *uses = FindChild(owner, "Uses");
        if (uses == nullptr)
        {
          return {};
        }
        for (const auto *dependency : ChildElements(*uses))
        {
          if (dependency->name != "Package" && dependency->name != "Tool" &&
              dependency->name != "Runtime")
          {
            continue;
          }
          auto package = ParsePackageReference(*dependency, context + ".Uses");
          if (!package)
          {
            return NGIN::Utilities::Unexpected<KernelError>(package.Error());
          }
          dependencies.push_back(package.Value());
        }
        return {};
      };

      auto rootUses = parseUses(*root, manifest.dependencies, "package");
      if (!rootUses)
      {
        return NGIN::Utilities::Unexpected<KernelError>(rootUses.Error());
      }

      if (const auto *runtimeElement = FindChild(*root, "Runtime"))
      {
        if (const auto *bootstrapElement = FindChild(*runtimeElement, "Bootstrap"))
        {
          auto modeText =
              OptionalAttribute(*bootstrapElement, "Mode",
                                "package.Runtime.Bootstrap", "BuilderHook");
          if (!modeText)
          {
            return NGIN::Utilities::Unexpected<KernelError>(modeText.Error());
          }
          auto mode = ParsePackageBootstrapMode(modeText.Value());
          if (!mode)
          {
            return NGIN::Utilities::Unexpected<KernelError>(mode.Error());
          }
          auto entryPoint =
              RequireAttribute(*bootstrapElement, "EntryPoint",
                               "package.Runtime.Bootstrap");
          if (!entryPoint)
          {
            return NGIN::Utilities::Unexpected<KernelError>(entryPoint.Error());
          }
          auto autoApply = OptionalBoolAttribute(
              *bootstrapElement, "AutoApply",
              "package.Runtime.Bootstrap.AutoApply", false);
          if (!autoApply)
          {
            return NGIN::Utilities::Unexpected<KernelError>(autoApply.Error());
          }
          manifest.bootstrap = PackageBootstrapDescriptor{
              .mode = mode.Value(),
              .entryPoint = entryPoint.Value(),
              .autoApply = autoApply.Value(),
          };
        }

        auto runtime =
            ParseRuntimeDefinition(runtimeElement, "package.Runtime");
        if (!runtime)
        {
          return NGIN::Utilities::Unexpected<KernelError>(runtime.Error());
        }
        manifest.modules = std::move(runtime.Value().modules);
      }

      if (const auto *library = FindChild(*root, "Library"))
      {
        if (const auto *exports = FindChild(*library, "Exports"))
        {
          for (const auto *headers : ChildElements(*exports, "Headers"))
          {
            if (const auto path = Attribute(*headers, "Path"); path.has_value())
            {
              manifest.inputs.push_back(InputDeclaration{
                  .kind = "Source",
                  .role = "Header",
                  .path = *path,
                  .pattern = *path,
                  .mode = "Glob",
                  .visibility = "Public",
                  .declaringScope = "package",
              });
            }
          }
        }
      }

      if (const auto *toolProduct = FindChild(*root, "Tool"))
      {
        if (const auto *exports = FindChild(*toolProduct, "Exports"))
        {
          for (const auto *tool : ChildElements(*exports, "Tool"))
          {
            auto parsed = ParseToolDeclaration(
                *tool, "package.Tool.Exports.Tool", true);
            if (!parsed)
            {
              return NGIN::Utilities::Unexpected<KernelError>(parsed.Error());
            }
            manifest.tools.push_back(parsed.Value());
          }
        }
      }

      if (const auto *featuresElement = FindChild(*root, "Features"))
      {
        std::set<std::string> featureNames{};
        for (const auto *featureElement : ChildElements(*featuresElement, "Feature"))
        {
          auto featureName = RequireAttribute(*featureElement, "Name", "package.Features.Feature");
          if (!featureName)
          {
            return NGIN::Utilities::Unexpected<KernelError>(featureName.Error());
          }
          PackageManifest::Feature feature{};
          feature.name = featureName.Value();
          feature.description = Attribute(*featureElement, "Description").value_or("");
          feature.profile = Attribute(*featureElement, "Profile").value_or("");
          feature.platform = Attribute(*featureElement, "Platform").value_or("");
          feature.operatingSystem = Attribute(*featureElement, "OperatingSystem").value_or("");
          feature.architecture = Attribute(*featureElement, "Architecture").value_or("");
          feature.toolchain = Attribute(*featureElement, "Toolchain").value_or("");
          feature.environment = Attribute(*featureElement, "Environment").value_or("");
          feature.condition = Attribute(*featureElement, "Condition").value_or("");
          if (!featureNames.insert(feature.name).second)
          {
            return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
                "duplicate package feature declaration", feature.name,
                KernelErrorCode::AlreadyExists));
          }
          auto featureUses =
              parseUses(*featureElement, feature.dependencies,
                        "package.Features.Feature");
          if (!featureUses)
          {
            return NGIN::Utilities::Unexpected<KernelError>(featureUses.Error());
          }
          if (const auto *provides = FindChild(*featureElement, "Provides"))
          {
            for (const auto *capabilityElement : ChildElements(*provides, "Capability"))
            {
              auto capabilityName = RequireAttribute(*capabilityElement, "Name", "package.Features.Feature.Provides");
              if (!capabilityName)
              {
                return NGIN::Utilities::Unexpected<KernelError>(capabilityName.Error());
              }
              auto exclusive = OptionalBoolAttribute(*capabilityElement, "Exclusive", "package.Features.Feature.Provides", false);
              if (!exclusive)
              {
                return NGIN::Utilities::Unexpected<KernelError>(exclusive.Error());
              }
              feature.provides.push_back(CapabilityProvision{capabilityName.Value(), exclusive.Value()});
            }
          }
          if (const auto *requiresElement = FindChild(*featureElement, "Requires"))
          {
            for (const auto *capabilityElement : ChildElements(*requiresElement, "Capability"))
            {
              auto capabilityName = RequireAttribute(*capabilityElement, "Name", "package.Features.Feature.Requires");
              if (!capabilityName)
              {
                return NGIN::Utilities::Unexpected<KernelError>(capabilityName.Error());
              }
              feature.requiredCapabilities.push_back(CapabilityRequirement{capabilityName.Value()});
            }
          }
          if (const auto *runtimeElement = FindChild(*featureElement, "Runtime"))
          {
            auto runtime = ParseRuntimeDefinition(runtimeElement, "package.Features.Feature.Runtime");
            if (!runtime)
            {
              return NGIN::Utilities::Unexpected<KernelError>(runtime.Error());
            }
            feature.runtime = std::move(runtime.Value());
          }
          if (const auto *variables = FindChild(*featureElement, "Variables"))
          {
            for (const auto *variableElement : ChildElements(*variables, "Variable"))
            {
              auto variableName = RequireAttribute(*variableElement, "Name", "package.Features.Feature.Variables");
              if (!variableName)
              {
                return NGIN::Utilities::Unexpected<KernelError>(variableName.Error());
              }
              auto variableValue = RequireAttribute(*variableElement, "Value", "package.Features.Feature.Variables");
              if (!variableValue)
              {
                return NGIN::Utilities::Unexpected<KernelError>(variableValue.Error());
              }
              feature.variables.push_back(EnvironmentVariable{variableName.Value(), variableValue.Value()});
            }
          }
          manifest.features.push_back(std::move(feature));
        }
      }

      return manifest;
    }

    template <typename T>
    void AppendUnique(std::vector<T> &target, const T &value)
    {
      if (std::find(target.begin(), target.end(), value) == target.end())
      {
        target.push_back(value);
      }
    }

    void AppendUniqueStrings(std::vector<std::string> &target,
                             const std::vector<std::string> &values)
    {
      for (const auto &value : values)
      {
        AppendUnique(target, value);
      }
    }

    void MergePackageReferences(std::vector<PackageReference> &target,
                                const std::vector<PackageReference> &source)
    {
      std::unordered_map<std::string, std::size_t> indexByName{};
      for (std::size_t index = 0; index < target.size(); ++index)
      {
        indexByName[target[index].name] = index;
      }

      for (const auto &reference : source)
      {
        const auto it = indexByName.find(reference.name);
        if (it == indexByName.end())
        {
          indexByName.emplace(reference.name, target.size());
          target.push_back(reference);
          continue;
        }
        target[it->second] = reference;
      }
    }

    void MergeSelectedPackageReferences(std::vector<PackageReference> &target,
                                        const std::vector<PackageReference> &source,
                                        const ProjectManifest &manifest,
                                        const ProfileDefinition &profile)
    {
      std::vector<PackageReference> selected{};
      for (const auto &reference : source)
      {
        if (SelectorsMatch(reference, manifest.conditions, profile))
        {
          selected.push_back(reference);
        }
      }
      MergePackageReferences(target, selected);
    }

    void MergeSelectedPackageFeatureUses(std::vector<PackageFeatureUse> &target,
                                         const std::vector<PackageFeatureUse> &source,
                                         const ProjectManifest &manifest,
                                         const ProfileDefinition &profile)
    {
      std::unordered_map<std::string, std::size_t> indexByKey{};
      for (std::size_t index = 0; index < target.size(); ++index)
      {
        indexByKey[target[index].packageName + "::" + target[index].featureName] = index;
      }
      for (const auto &use : source)
      {
        if (!SelectorsMatch(use, manifest.conditions, profile))
        {
          continue;
        }
        const auto key = use.packageName + "::" + use.featureName;
        if (use.disabled)
        {
          if (const auto it = indexByKey.find(key); it != indexByKey.end())
          {
            target.erase(target.begin() + static_cast<std::ptrdiff_t>(it->second));
            indexByKey.clear();
            for (std::size_t index = 0; index < target.size(); ++index)
            {
              indexByKey[target[index].packageName + "::" + target[index].featureName] = index;
            }
          }
          continue;
        }
        if (const auto it = indexByKey.find(key); it != indexByKey.end())
        {
          target[it->second] = use;
          continue;
        }
        indexByKey[key] = target.size();
        target.push_back(use);
      }
    }

    void AppendOrReplaceModuleRegistration(
        std::vector<StaticModuleRegistration> &registrations,
        StaticModuleRegistration registration)
    {
      const auto it = std::find_if(registrations.begin(), registrations.end(),
                                   [&](const StaticModuleRegistration &candidate)
                                   {
                                     return candidate.descriptor.name ==
                                            registration.descriptor.name;
                                   });

      if (it == registrations.end())
      {
        registrations.push_back(std::move(registration));
        return;
      }

      *it = std::move(registration);
    }

    [[nodiscard]] auto
    BuildCatalogFrom(const std::vector<StaticModuleRegistration> &registrations,
                     const std::set<std::string> &disabledModules)
        -> CoreResult<NGIN::Memory::Shared<IModuleCatalog>>
    {
      auto catalog = CreateStaticModuleCatalog();
      if (!catalog)
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            "failed to create module catalog", {}, KernelErrorCode::InternalError));
      }

      for (const auto &registration : registrations)
      {
        if (disabledModules.contains(registration.descriptor.name))
        {
          continue;
        }

        auto result = catalog->Register(registration);
        if (!result)
        {
          return NGIN::Utilities::Unexpected<KernelError>(result.Error());
        }
      }

      return catalog;
    }

    [[nodiscard]] auto ResolveWorkingDirectory(std::string workingDirectory,
                                               const IoPath &projectDirectory)
        -> std::string
    {
      if (workingDirectory.empty())
      {
        return workingDirectory;
      }

      IoPath workPath(workingDirectory);
      if (workPath.IsRelative() && !projectDirectory.IsEmpty())
      {
        return ToString(projectDirectory.Join(workPath.View()).LexicallyNormal());
      }
      return ToString(workPath.LexicallyNormal());
    }

    class PackageBootstrapRegistryImpl final : public PackageBootstrapRegistry
    {
    public:
      explicit PackageBootstrapRegistryImpl(
          std::unordered_map<std::string, std::string> defaultEntryPoints)
          : m_defaultEntryPoints(std::move(defaultEntryPoints)) {}

      auto Register(PackageBootstrapEntry entry) -> CoreResult<void> override
      {
        if (entry.packageName.empty() || entry.entryPoint.empty() ||
            entry.fn == nullptr)
        {
          m_lastError =
              MakeBuilderError("package bootstrap entry must have package name, "
                               "entry point, and function",
                               entry.packageName, KernelErrorCode::InvalidArgument);
          return NGIN::Utilities::Unexpected<KernelError>(*m_lastError);
        }

        const std::string key = entry.packageName + "::" + entry.entryPoint;
        if (m_indexByKey.contains(key))
        {
          m_lastError = MakeBuilderError("duplicate package bootstrap entry", key,
                                         KernelErrorCode::AlreadyExists);
          return NGIN::Utilities::Unexpected<KernelError>(*m_lastError);
        }

        m_entries.push_back(std::move(entry));
        m_indexByKey.emplace(key, m_entries.size() - 1);
        m_lastError.reset();
        return {};
      }

      [[nodiscard]] auto Find(const std::string_view packageName,
                              const std::string_view entryPoint) const noexcept
          -> const PackageBootstrapEntry * override
      {
        const auto it = m_indexByKey.find(std::string(packageName) +
                                          "::" + std::string(entryPoint));
        if (it == m_indexByKey.end())
        {
          return nullptr;
        }
        return &m_entries[it->second];
      }

      [[nodiscard]] auto
      FindDefault(const std::string_view packageName) const noexcept
          -> const PackageBootstrapEntry * override
      {
        const auto defaultIt = m_defaultEntryPoints.find(std::string(packageName));
        if (defaultIt != m_defaultEntryPoints.end())
        {
          return Find(packageName, defaultIt->second);
        }

        const PackageBootstrapEntry *match = nullptr;
        for (const auto &entry : m_entries)
        {
          if (entry.packageName != packageName)
          {
            continue;
          }

          if (match != nullptr)
          {
            return nullptr;
          }
          match = &entry;
        }
        return match;
      }

      [[nodiscard]] auto LastError() const noexcept
          -> const std::optional<KernelError> &
      {
        return m_lastError;
      }

    private:
      std::vector<PackageBootstrapEntry> m_entries{};
      std::unordered_map<std::string, std::size_t> m_indexByKey{};
      std::unordered_map<std::string, std::string> m_defaultEntryPoints{};
      std::optional<KernelError> m_lastError{};
    };

    class ApplicationHostImpl final : public IApplicationHost
    {
    public:
      ApplicationHostImpl(NGIN::Memory::Shared<IKernel> kernel,
                          StartupReport metadataReport)
          : m_kernel(std::move(kernel)), m_metadataReport(std::move(metadataReport))
      {
      }

      auto Start() noexcept -> CoreResult<void> override
      {
        return m_kernel->Start();
      }

      auto Run() noexcept -> CoreResult<void> override { return m_kernel->Run(); }

      auto Tick() noexcept -> CoreResult<void> override { return m_kernel->Tick(); }

      void RequestStop(std::string reason) noexcept override
      {
        m_kernel->RequestStop(std::move(reason));
      }

      auto Shutdown() noexcept -> CoreResult<void> override
      {
        return m_kernel->Shutdown();
      }

      [[nodiscard]] auto GetProfileName() const -> std::string override
      {
        return m_metadataReport.targetName;
      }

      [[nodiscard]] auto GetStartupReport() const -> StartupReport override
      {
        auto report = m_kernel->GetStartupReport();
        if (report.hostName.empty())
        {
          report.hostName = m_metadataReport.hostName;
        }
        if (report.targetName.empty())
        {
          report.targetName = m_metadataReport.targetName;
        }
        if (report.hostType.empty())
        {
          report.hostType = m_metadataReport.hostType;
        }

        report.resolvedPackages = m_metadataReport.resolvedPackages;
        report.resolvedPlugins = m_metadataReport.resolvedPlugins;

        report.warnings.insert(report.warnings.end(),
                               m_metadataReport.warnings.begin(),
                               m_metadataReport.warnings.end());

        report.failures.insert(report.failures.end(),
                               m_metadataReport.failures.begin(),
                               m_metadataReport.failures.end());

        return report;
      }

      [[nodiscard]] auto GetServices() noexcept
          -> NGIN::Memory::Shared<IServiceRegistry> override
      {
        return m_kernel->GetServices();
      }

      [[nodiscard]] auto GetConfig() noexcept
          -> NGIN::Memory::Shared<IConfigStore> override
      {
        return m_kernel->GetConfig();
      }

    private:
      NGIN::Memory::Shared<IKernel> m_kernel{};
      StartupReport m_metadataReport{};
    };

    class ApplicationBuilderImpl;

    class ServiceCollectionImpl final : public ServiceCollection
    {
    public:
      explicit ServiceCollectionImpl(ApplicationBuilderImpl &owner)
          : m_owner(owner) {}

      auto AddDefaults() -> ServiceCollection & override;
      auto AddLogging() -> ServiceCollection & override;
      auto AddConfiguration() -> ServiceCollection & override;
      auto Clear() -> ServiceCollection & override;

    private:
      auto AddProvider(std::shared_ptr<detail::ServiceProviderBase> provider)
          -> ServiceCollection & override;

      ApplicationBuilderImpl &m_owner;
    };

    class PackageCollectionImpl final : public PackageCollection
    {
    public:
      explicit PackageCollectionImpl(ApplicationBuilderImpl &owner)
          : m_owner(owner) {}

      auto Add(PackageReference reference) -> PackageCollection & override;
      auto AddManifest(PackageManifest manifest) -> PackageCollection & override;
      auto AddManifestFile(std::string path) -> PackageCollection & override;
      auto RegisterLinkedRegistrar(PackageBootstrapRegistrarFn registrar)
          -> PackageCollection & override;
      auto ApplyBootstrap(std::string packageName) -> PackageCollection & override;
      auto ApplyBootstrap(std::string packageName, std::string entryPoint)
          -> PackageCollection & override;
      auto Clear() -> PackageCollection & override;

    private:
      ApplicationBuilderImpl &m_owner;
    };

    class ModuleCollectionImpl final : public ModuleCollection
    {
    public:
      explicit ModuleCollectionImpl(ApplicationBuilderImpl &owner)
          : m_owner(owner) {}

      auto Register(StaticModuleRegistration registration)
          -> ModuleCollection & override;
      auto Enable(std::string moduleName) -> ModuleCollection & override;
      auto Disable(std::string moduleName) -> ModuleCollection & override;
      auto Clear() -> ModuleCollection & override;

    private:
      ApplicationBuilderImpl &m_owner;
    };

    class PluginCollectionImpl final : public PluginCollection
    {
    public:
      explicit PluginCollectionImpl(ApplicationBuilderImpl &owner)
          : m_owner(owner) {}

      auto Enable(std::string pluginName) -> PluginCollection & override;
      auto Disable(std::string pluginName) -> PluginCollection & override;
      auto AddSearchPath(std::string path) -> PluginCollection & override;
      auto Clear() -> PluginCollection & override;

    private:
      ApplicationBuilderImpl &m_owner;
    };

    class ConfigurationBuilderImpl final : public ConfigurationBuilder
    {
    public:
      explicit ConfigurationBuilderImpl(ApplicationBuilderImpl &owner)
          : m_owner(owner) {}

      auto AddSource(std::string path) -> ConfigurationBuilder & override;
      auto SetEnvironmentName(std::string environmentName)
          -> ConfigurationBuilder & override;
      auto SetWorkingDirectory(std::string workingDirectory)
          -> ConfigurationBuilder & override;
      auto Clear() -> ConfigurationBuilder & override;

    private:
      ApplicationBuilderImpl &m_owner;
    };

    class PackageBootstrapContextImpl final : public PackageBootstrapContext
    {
    public:
      PackageBootstrapContextImpl(
          std::string_view packageName, std::string_view profileName,
          ServiceCollection &services, PackageCollection &packages,
          ModuleCollection &modules, PluginCollection &plugins,
          ConfigurationBuilder &configuration)
          : m_packageName(packageName), m_profileName(profileName),
            m_services(services), m_packages(packages), m_modules(modules),
            m_plugins(plugins), m_configuration(configuration)
      {
      }

      [[nodiscard]] auto PackageName() const noexcept -> std::string_view override
      {
        return m_packageName;
      }
      [[nodiscard]] auto ProfileName() const noexcept
          -> std::string_view override
      {
        return m_profileName;
      }

      [[nodiscard]] auto Services() noexcept -> ServiceCollection & override
      {
        return m_services;
      }
      [[nodiscard]] auto Packages() noexcept -> PackageCollection & override
      {
        return m_packages;
      }
      [[nodiscard]] auto Modules() noexcept -> ModuleCollection & override
      {
        return m_modules;
      }
      [[nodiscard]] auto Plugins() noexcept -> PluginCollection & override
      {
        return m_plugins;
      }
      [[nodiscard]] auto Configuration() noexcept
          -> ConfigurationBuilder & override
      {
        return m_configuration;
      }

    private:
      std::string_view m_packageName;
      std::string_view m_profileName;
      ServiceCollection &m_services;
      PackageCollection &m_packages;
      ModuleCollection &m_modules;
      PluginCollection &m_plugins;
      ConfigurationBuilder &m_configuration;
    };

    class ApplicationBuilderImpl final : public ApplicationBuilder
    {
    public:
      ApplicationBuilderImpl(const int argc, char **argv)
          : m_services(*this), m_packages(*this), m_modules(*this),
            m_plugins(*this), m_configuration(*this)
      {
        for (int index = 1; index < argc; ++index)
        {
          if (argv[index] != nullptr)
          {
            m_commandLineArgs.emplace_back(argv[index]);
          }
        }
      }

      [[nodiscard]] auto FileSystem() -> IoFileSystem &
      {
        if (!m_fileSystem)
        {
          m_fileSystem =
              NGIN::Memory::MakeSharedAs<IoFileSystem, NGIN::IO::LocalFileSystem>();
        }
        return *m_fileSystem;
      }

      auto UseProjectFile(std::string path) -> ApplicationBuilder & override
      {
        MarkMutating();
        if (HasStickyError())
        {
          return *this;
        }

        const IoPath filePath(path);
        auto fileText = ReadTextFile(FileSystem(), filePath, "project");
        if (!fileText)
        {
          m_stickyError = fileText.Error();
          return *this;
        }

        auto absolutePath = AbsolutePath(FileSystem(), filePath);
        if (!absolutePath)
        {
          m_stickyError = absolutePath.Error();
          return *this;
        }

        auto manifest =
            ParseProjectManifestText(fileText.Value(), &FileSystem(),
                                     &absolutePath.Value());
        if (!manifest)
        {
          m_stickyError = manifest.Error();
          return *this;
        }

        m_project = manifest.Value();
        m_projectPath = absolutePath.Value();
        m_projectDirectory = absolutePath.Value().Parent();
        return *this;
      }

      auto UseProject(ProjectManifest manifest) -> ApplicationBuilder & override
      {
        MarkMutating();
        if (!HasStickyError())
        {
          m_project = std::move(manifest);
          m_projectPath = {};
          m_projectDirectory = {};
        }
        return *this;
      }

      auto UseFileSystem(NGIN::Memory::Shared<NGIN::IO::IFileSystem> fileSystem)
          -> ApplicationBuilder & override
      {
        MarkMutating();
        if (!HasStickyError())
        {
          m_fileSystem = std::move(fileSystem);
        }
        return *this;
      }

      auto SetApplicationName(std::string applicationName)
          -> ApplicationBuilder & override
      {
        MarkMutating();
        if (!HasStickyError())
        {
          m_applicationName = std::move(applicationName);
        }
        return *this;
      }

      auto SetProfile(std::string profileName)
          -> ApplicationBuilder & override
      {
        MarkMutating();
        if (!HasStickyError())
        {
          m_profileOverride = std::move(profileName);
        }
        return *this;
      }

      auto AddConfigSource(std::string path) -> ApplicationBuilder & override
      {
        MarkMutating();
        if (!HasStickyError())
        {
          AppendUnique(m_configInputs, path);
        }
        return *this;
      }

      auto AddDefaultServices() -> ApplicationBuilder & override
      {
        MarkMutating();
        if (!HasStickyError())
        {
          m_addDefaultServices = true;
        }
        return *this;
      }

      auto AddLogging() -> ApplicationBuilder & override
      {
        MarkMutating();
        if (!HasStickyError())
        {
          m_addLogging = true;
        }
        return *this;
      }

      auto AddConfiguration() -> ApplicationBuilder & override
      {
        MarkMutating();
        if (!HasStickyError())
        {
          m_addConfiguration = true;
        }
        return *this;
      }

      auto AddPluginSearchPath(std::string path) -> ApplicationBuilder & override
      {
        MarkMutating();
        if (!HasStickyError())
        {
          AppendUnique(m_pluginSearchPaths, path);
          m_enableDynamicPlugins = true;
        }
        return *this;
      }

      auto EnableDynamicPlugins(const bool enabled = true)
          -> ApplicationBuilder & override
      {
        MarkMutating();
        if (!HasStickyError())
        {
          m_enableDynamicPlugins = enabled;
        }
        return *this;
      }

      auto AddModule(std::string name, ModuleOptions options,
                     ModuleFactory factory) -> ApplicationBuilder & override
      {
        MarkMutating();
        if (HasStickyError())
        {
          return *this;
        }
        if (name.empty() || !factory)
        {
          m_stickyError = MakeBuilderError(
              "module registration must include a name and factory",
              name, KernelErrorCode::InvalidArgument);
          return *this;
        }

        AppendOrReplaceModuleRegistration(
            m_moduleRegistrations,
            StaticModuleRegistration{
                .descriptor = MakeModuleDescriptor(std::move(name), options),
                .factory = std::move(factory),
            });
        return *this;
      }

      [[nodiscard]] auto Services() noexcept -> ServiceCollection & override
      {
        return m_services;
      }
      [[nodiscard]] auto Packages() noexcept -> PackageCollection & override
      {
        return m_packages;
      }
      [[nodiscard]] auto Modules() noexcept -> ModuleCollection & override
      {
        return m_modules;
      }
      [[nodiscard]] auto Plugins() noexcept -> PluginCollection & override
      {
        return m_plugins;
      }
      [[nodiscard]] auto Configuration() noexcept
          -> ConfigurationBuilder & override
      {
        return m_configuration;
      }

      [[nodiscard]] auto Build()
          -> CoreResult<std::shared_ptr<IApplicationHost>> override
      {
        if (m_built)
        {
          return NGIN::Utilities::Unexpected<KernelError>(
              MakeBuilderError("Build() may only be called once", {},
                               KernelErrorCode::InvalidState));
        }
        if (m_stickyError.has_value())
        {
          return NGIN::Utilities::Unexpected<KernelError>(*m_stickyError);
        }

        std::vector<ResolvedProjectUnit> projectUnits{};
        const ResolvedProjectUnit *rootProjectUnit = nullptr;
        if (m_project.has_value())
        {
          if (!m_projectPath.IsEmpty())
          {
            std::set<std::string> visiting{};
            std::set<std::string> visited{};
            auto closure = CollectProjectClosure(FileSystem(), m_projectPath,
                                                 m_profileOverride, false,
                                                 projectUnits, visiting, visited);
            if (!closure)
            {
              return NGIN::Utilities::Unexpected<KernelError>(closure.Error());
            }
          }
          else
          {
            auto selectedProfile =
                FindProfile(*m_project, m_profileOverride);
            if (!selectedProfile)
            {
              return NGIN::Utilities::Unexpected<KernelError>(
                  selectedProfile.Error());
            }
            auto selectedEnvironment =
                FindEnvironment(*m_project, selectedProfile.Value().environmentName);
            if (!selectedEnvironment)
            {
              return NGIN::Utilities::Unexpected<KernelError>(
                  selectedEnvironment.Error());
            }

            projectUnits.push_back(ResolvedProjectUnit{
                .manifest = *m_project,
                .profile = selectedProfile.Value(),
                .environment = selectedEnvironment.Value(),
                .directory = m_projectDirectory,
            });
          }

          if (!projectUnits.empty())
          {
            rootProjectUnit = &projectUnits.back();
          }
        }

        const ProfileDefinition *selectedProfile =
            rootProjectUnit != nullptr ? &rootProjectUnit->profile : nullptr;

        if (selectedProfile != nullptr)
        {
          for (const auto &unit : projectUnits)
          {
            if (unit.profile.operatingSystem !=
                    selectedProfile->operatingSystem ||
                unit.profile.architecture !=
                    selectedProfile->architecture)
            {
              return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
                  "referenced project target does not match the selected root "
                  "profile",
                  unit.manifest.name, KernelErrorCode::SchemaValidationFailure));
            }
          }
        }

        const HostType hostType = HostType::ConsoleApp;

        std::string profileName = !m_profileOverride.empty()
                                            ? m_profileOverride
                                            : std::string{};
        if (profileName.empty() && selectedProfile != nullptr)
        {
          profileName = selectedProfile->name;
        }
        if (profileName.empty())
        {
          profileName =
              !m_applicationName.empty() ? m_applicationName : "NGIN.Configuration";
        }

        std::string applicationName = m_applicationName;
        if (applicationName.empty() && rootProjectUnit != nullptr)
        {
          applicationName = rootProjectUnit->manifest.name;
        }
        if (applicationName.empty())
        {
          applicationName = "NGIN.Application";
        }

        std::vector<PackageReference> bootstrapPackages{};
        for (const auto &unit : projectUnits)
        {
          MergeSelectedPackageReferences(bootstrapPackages, unit.manifest.packageRefs, unit.manifest, unit.profile);
          if (unit.environment.has_value())
          {
            MergeSelectedPackageReferences(bootstrapPackages, unit.environment->packageRefs, unit.manifest, unit.profile);
          }
          MergeSelectedPackageReferences(bootstrapPackages, unit.profile.packageRefs, unit.manifest, unit.profile);
        }
        MergePackageReferences(bootstrapPackages, m_packageReferences);

        std::unordered_map<std::string, std::size_t> packageOrder{};
        std::unordered_map<std::string, PackageReference> directPackagesByName{};
        for (std::size_t index = 0; index < bootstrapPackages.size(); ++index)
        {
          packageOrder[bootstrapPackages[index].name] = index;
          directPackagesByName[bootstrapPackages[index].name] =
              bootstrapPackages[index];
        }

        std::unordered_map<std::string, std::string> defaultBootstrapEntryPoints{};
        for (const auto &[packageName, manifest] : m_packageManifests)
        {
          if (manifest.bootstrap.has_value())
          {
            defaultBootstrapEntryPoints.emplace(packageName,
                                                manifest.bootstrap->entryPoint);
          }
        }

        PackageBootstrapRegistryImpl bootstrapRegistry(
            std::move(defaultBootstrapEntryPoints));
        for (const auto registrar : m_packageRegistrars)
        {
          registrar(bootstrapRegistry);
          if (bootstrapRegistry.LastError().has_value())
          {
            return NGIN::Utilities::Unexpected<KernelError>(
                *bootstrapRegistry.LastError());
          }
        }

        std::unordered_map<std::string, BootstrapCandidate> candidatesByName{};
        std::vector<StartupWarning> bootstrapWarnings{};

        auto makeWarning = [](const std::string &packageName,
                              const std::string &detail)
        {
          return StartupWarning{
              .subsystem = "ApplicationBuilder",
              .module = packageName,
              .message =
                  "package bootstrap skipped for '" + packageName + "': " + detail,
          };
        };

        for (const auto &request : m_explicitBootstrapRequests)
        {
          const auto packageIt = directPackagesByName.find(request.packageName);
          if (packageIt == directPackagesByName.end())
          {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeBuilderError("explicit bootstrap requested for package that is "
                                 "not a direct reference",
                                 request.packageName, KernelErrorCode::NotFound));
          }

          const PackageManifest *manifest = nullptr;
          const auto manifestIt = m_packageManifests.find(request.packageName);
          if (manifestIt != m_packageManifests.end())
          {
            manifest = &manifestIt->second;
          }

          std::string entryPoint{};
          if (request.entryPoint.has_value())
          {
            entryPoint = *request.entryPoint;
          }
          else if (manifest != nullptr && manifest->bootstrap.has_value())
          {
            entryPoint = manifest->bootstrap->entryPoint;
          }
          else
          {
            const auto *defaultEntry =
                bootstrapRegistry.FindDefault(request.packageName);
            if (defaultEntry == nullptr)
            {
              return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
                  "unable to resolve default bootstrap entry point for package",
                  request.packageName, KernelErrorCode::NotFound));
            }
            entryPoint = defaultEntry->entryPoint;
          }

          if (bootstrapRegistry.Find(request.packageName, entryPoint) == nullptr)
          {
            return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
                "explicit package bootstrap entry was not registered",
                request.packageName + "::" + entryPoint,
                KernelErrorCode::NotFound));
          }

          candidatesByName[request.packageName] = BootstrapCandidate{
              .reference = packageIt->second,
              .manifest = manifest,
              .entryPoint = std::move(entryPoint),
              .explicitRequest = true,
              .orderIndex = packageOrder[request.packageName],
          };
        }

        for (const auto &reference : bootstrapPackages)
        {
          if (candidatesByName.contains(reference.name))
          {
            continue;
          }

          const auto manifestIt = m_packageManifests.find(reference.name);
          if (manifestIt == m_packageManifests.end())
          {
            continue;
          }

          const PackageManifest &manifest = manifestIt->second;
          if (!manifest.bootstrap.has_value() || !manifest.bootstrap->autoApply)
          {
            continue;
          }

          const auto *entry = bootstrapRegistry.Find(
              reference.name, manifest.bootstrap->entryPoint);
          if (entry == nullptr)
          {
            if (reference.optional)
            {
              bootstrapWarnings.push_back(
                  makeWarning(reference.name, "manifest entry point '" +
                                                  manifest.bootstrap->entryPoint +
                                                  "' was not registered"));
              continue;
            }

            return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
                "required package bootstrap entry was not registered",
                reference.name + "::" + manifest.bootstrap->entryPoint,
                KernelErrorCode::NotFound));
          }

          candidatesByName.emplace(reference.name,
                                   BootstrapCandidate{
                                       .reference = reference,
                                       .manifest = &manifest,
                                       .entryPoint = manifest.bootstrap->entryPoint,
                                       .explicitRequest = false,
                                       .orderIndex = packageOrder[reference.name],
                                   });
        }

        std::unordered_map<std::string, std::size_t> indegree{};
        std::unordered_map<std::string, std::vector<std::string>> adjacency{};
        std::vector<std::string> ready{};
        std::vector<std::string> orderedCandidateNames{};
        orderedCandidateNames.reserve(candidatesByName.size());

        for (const auto &[packageName, candidate] : candidatesByName)
        {
          (void)candidate;
          indegree.emplace(packageName, 0);
        }

        for (const auto &[packageName, candidate] : candidatesByName)
        {
          if (candidate.manifest == nullptr)
          {
            continue;
          }

          for (const auto &dependency : candidate.manifest->dependencies)
          {
            if (!candidatesByName.contains(dependency.name))
            {
              continue;
            }

            adjacency[dependency.name].push_back(packageName);
            ++indegree[packageName];
          }
        }

        for (const auto &[packageName, count] : indegree)
        {
          if (count == 0)
          {
            ready.push_back(packageName);
          }
        }

        auto compareByOrder = [&](const std::string &lhs, const std::string &rhs)
        {
          return packageOrder[lhs] < packageOrder[rhs];
        };

        std::sort(ready.begin(), ready.end(), compareByOrder);

        while (!ready.empty())
        {
          const std::string current = ready.front();
          ready.erase(ready.begin());
          orderedCandidateNames.push_back(current);

          auto adjacencyIt = adjacency.find(current);
          if (adjacencyIt == adjacency.end())
          {
            continue;
          }

          for (const auto &dependent : adjacencyIt->second)
          {
            auto &count = indegree[dependent];
            --count;
            if (count == 0)
            {
              ready.push_back(dependent);
            }
          }
          std::sort(ready.begin(), ready.end(), compareByOrder);
        }

        if (orderedCandidateNames.size() != candidatesByName.size())
        {
          return NGIN::Utilities::Unexpected<KernelError>(
              MakeBuilderError("package bootstrap dependency cycle detected", {},
                               KernelErrorCode::DependencyCycle));
        }

        for (const auto &packageName : orderedCandidateNames)
        {
          const auto &candidate = candidatesByName.at(packageName);
          const auto *entry = bootstrapRegistry.Find(candidate.reference.name,
                                                     candidate.entryPoint);
          if (entry == nullptr || entry->fn == nullptr)
          {
            const std::string detail =
                "bootstrap entry '" + candidate.entryPoint + "' was not registered";
            if (candidate.explicitRequest || !candidate.reference.optional)
            {
              return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
                  detail, candidate.reference.name, KernelErrorCode::NotFound));
            }

            bootstrapWarnings.push_back(
                makeWarning(candidate.reference.name, detail));
            continue;
          }

          PackageBootstrapContextImpl context(
              candidate.reference.name, profileName, m_services, m_packages,
              m_modules, m_plugins, m_configuration);

          auto bootstrapResult = entry->fn(context);
          if (bootstrapResult)
          {
            continue;
          }

          if (candidate.explicitRequest || !candidate.reference.optional)
          {
            return NGIN::Utilities::Unexpected<KernelError>(
                bootstrapResult.Error());
          }

          bootstrapWarnings.push_back(makeWarning(candidate.reference.name,
                                                  bootstrapResult.Error().message));
        }

        std::vector<PackageReference> packages{};
        std::vector<PackageFeatureUse> packageFeatureUses{};
        for (const auto &unit : projectUnits)
        {
          MergeSelectedPackageReferences(packages, unit.manifest.packageRefs, unit.manifest, unit.profile);
          MergeSelectedPackageFeatureUses(packageFeatureUses, unit.manifest.packageFeatureUses, unit.manifest, unit.profile);
          if (unit.environment.has_value())
          {
            MergeSelectedPackageReferences(packages, unit.environment->packageRefs, unit.manifest, unit.profile);
            MergeSelectedPackageFeatureUses(packageFeatureUses, unit.environment->packageFeatureUses, unit.manifest, unit.profile);
          }
          MergeSelectedPackageReferences(packages, unit.profile.packageRefs, unit.manifest, unit.profile);
          MergeSelectedPackageFeatureUses(packageFeatureUses, unit.profile.packageFeatureUses, unit.manifest, unit.profile);
        }
        MergePackageReferences(packages, m_packageReferences);

        std::vector<const PackageManifest::Feature *> selectedPackageFeatures{};
        std::vector<const PackageManifest *> selectedPackageFeatureManifests{};
        for (const auto &use : packageFeatureUses)
        {
          PackageReference reference{};
          reference.name = use.packageName;
          reference.versionRange = use.versionRange;
          MergePackageReferences(packages, {reference});
        }
        for (std::size_t index = 0; index < packageFeatureUses.size(); ++index)
        {
          const auto &use = packageFeatureUses[index];
          const auto manifestIt = m_packageManifests.find(use.packageName);
          if (manifestIt == m_packageManifests.end())
          {
            continue;
          }
          const auto featureIt = std::find_if(
              manifestIt->second.features.begin(), manifestIt->second.features.end(),
              [&](const PackageManifest::Feature &feature)
              {
                return feature.name == use.featureName;
              });
          if (featureIt == manifestIt->second.features.end())
          {
            return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
                "selected package feature does not exist",
                use.packageName + "::" + use.featureName, KernelErrorCode::NotFound));
          }
          if (!SelectorsMatch(*featureIt, manifestIt->second.conditions, *selectedProfile))
          {
            continue;
          }
          selectedPackageFeatures.push_back(&*featureIt);
          selectedPackageFeatureManifests.push_back(&manifestIt->second);
          for (const auto &dependency : featureIt->dependencies)
          {
            if (SelectorsMatch(dependency, manifestIt->second.conditions, *selectedProfile))
            {
              MergePackageReferences(packages, {dependency});
            }
          }
        }

        std::vector<std::string> enabledModules{};
        for (const auto &unit : projectUnits)
        {
          AppendUniqueStrings(enabledModules, unit.manifest.runtime.enableModules);
          if (unit.environment.has_value())
          {
            AppendUniqueStrings(enabledModules, unit.environment->runtime.enableModules);
          }
          AppendUniqueStrings(enabledModules, unit.profile.runtime.enableModules);
        }
        for (const auto *feature : selectedPackageFeatures)
        {
          AppendUniqueStrings(enabledModules, feature->runtime.enableModules);
        }
        AppendUniqueStrings(enabledModules, m_enabledModules);

        std::vector<std::string> disabledModules{};
        for (const auto &unit : projectUnits)
        {
          AppendUniqueStrings(disabledModules,
                              unit.manifest.runtime.disableModules);
          if (unit.environment.has_value())
          {
            AppendUniqueStrings(disabledModules,
                                unit.environment->runtime.disableModules);
          }
          AppendUniqueStrings(disabledModules,
                              unit.profile.runtime.disableModules);
        }
        for (const auto *feature : selectedPackageFeatures)
        {
          AppendUniqueStrings(disabledModules, feature->runtime.disableModules);
        }
        AppendUniqueStrings(disabledModules, m_disabledModules);
        const std::set<std::string> disabledModuleSet(disabledModules.begin(),
                                                      disabledModules.end());

        enabledModules.erase(
            std::remove_if(enabledModules.begin(), enabledModules.end(),
                           [&](const std::string &name)
                           {
                             return disabledModuleSet.contains(name);
                           }),
            enabledModules.end());

        std::vector<std::string> enabledPlugins{};
        AppendUniqueStrings(enabledPlugins, m_enabledPlugins);

        std::vector<std::string> disabledPlugins{};
        AppendUniqueStrings(disabledPlugins, m_disabledPlugins);
        const std::set<std::string> disabledPluginSet(disabledPlugins.begin(),
                                                      disabledPlugins.end());

        enabledPlugins.erase(
            std::remove_if(enabledPlugins.begin(), enabledPlugins.end(),
                           [&](const std::string &name)
                           {
                             return disabledPluginSet.contains(name);
                           }),
            enabledPlugins.end());

        std::vector<std::string> configInputs{};
        const auto inputMatchesProfile = [](const InputDeclaration &input,
                                            const ProfileDefinition &profile,
                                            const std::vector<ConditionDefinition> &conditions)
        {
          return (input.profile.empty() || input.profile == profile.name) &&
                 (input.platform.empty() || input.platform == profile.platform) &&
                 (input.operatingSystem.empty() ||
                  input.operatingSystem == profile.operatingSystem) &&
                 (input.architecture.empty() ||
                  input.architecture == profile.architecture) &&
                 (input.toolchain.empty() || input.toolchain == profile.toolchain) &&
                 (input.environment.empty() ||
                  input.environment == profile.environmentName) &&
                 NamedConditionMatches(input.condition, conditions, profile);
        };
        const auto appendConfigInputs =
            [&](const std::vector<InputDeclaration> &inputs,
                const ProfileDefinition &profile,
                const IoPath &directory,
                const std::vector<ConditionDefinition> &conditions)
        {
          for (const auto &input : inputs)
          {
            if (input.kind != "Config" ||
                !inputMatchesProfile(input, profile, conditions))
            {
              continue;
            }
            const auto source = input.path.empty() ? input.pattern : input.path;
            const auto resolved = ResolveWorkingDirectory(source, directory);
            AppendUnique(configInputs, resolved);
          }
        };
        for (const auto &unit : projectUnits)
        {
          appendConfigInputs(unit.manifest.inputs, unit.profile, unit.directory, unit.manifest.conditions);
          if (unit.environment.has_value())
          {
            appendConfigInputs(unit.environment->inputs, unit.profile, unit.directory, unit.manifest.conditions);
          }
          appendConfigInputs(unit.profile.inputs, unit.profile, unit.directory, unit.manifest.conditions);
        }
        if (selectedProfile != nullptr)
        {
          for (const auto &reference : packages)
          {
            const auto manifestIt = m_packageManifests.find(reference.name);
            if (manifestIt == m_packageManifests.end())
            {
              continue;
            }
            const auto directory = manifestIt->second.directory.empty()
                                       ? m_projectDirectory
                                       : IoPath{manifestIt->second.directory};
            appendConfigInputs(manifestIt->second.inputs, *selectedProfile, directory, manifestIt->second.conditions);
          }
          for (std::size_t index = 0; index < selectedPackageFeatures.size(); ++index)
          {
            const auto *feature = selectedPackageFeatures[index];
            const auto *manifest = selectedPackageFeatureManifests[index];
            const auto directory = manifest->directory.empty()
                                       ? m_projectDirectory
                                       : IoPath{manifest->directory};
            appendConfigInputs(feature->inputs, *selectedProfile, directory, manifest->conditions);
          }
        }
        AppendUniqueStrings(configInputs, m_configInputs);

        std::vector<std::string> pluginSearchPaths{};
        AppendUniqueStrings(pluginSearchPaths, m_pluginSearchPaths);

        std::string environmentName = m_environmentName;
        if (environmentName.empty() && selectedProfile != nullptr)
        {
          environmentName = selectedProfile->environmentName;
        }

        std::string workingDirectory = m_workingDirectory;
        if (workingDirectory.empty() && selectedProfile != nullptr &&
            selectedProfile->launch.has_value())
        {
          workingDirectory = selectedProfile->launch->workingDirectory;
        }
        if (workingDirectory.empty() && !m_projectDirectory.IsEmpty())
        {
          workingDirectory = ToString(m_projectDirectory);
        }
        workingDirectory =
            ResolveWorkingDirectory(workingDirectory, m_projectDirectory);

        auto moduleCatalog =
            BuildCatalogFrom(m_moduleRegistrations, disabledModuleSet);
        if (!moduleCatalog)
        {
          return NGIN::Utilities::Unexpected<KernelError>(moduleCatalog.Error());
        }

        StartupReport metadataReport{};
        metadataReport.hostName = applicationName;
        metadataReport.targetName = profileName;
        metadataReport.hostType = std::string(ToString(hostType));
        metadataReport.warnings = bootstrapWarnings;
        for (const auto &reference : packages)
        {
          metadataReport.resolvedPackages.push_back(reference.name);
        }
        metadataReport.resolvedPlugins = enabledPlugins;

        KernelHostConfig hostConfig{};
        hostConfig.hostName = applicationName;
        hostConfig.hostType = hostType;
        hostConfig.operatingSystemName =
            selectedProfile != nullptr ? selectedProfile->operatingSystem
                                             : "linux";
        hostConfig.architectureName =
            selectedProfile != nullptr ? selectedProfile->architecture
                                             : "x64";
        hostConfig.platformName =
            hostConfig.operatingSystemName + "-" + hostConfig.architectureName;
        hostConfig.platformVersion = SemanticVersion{0, 1, 0, {}};
        hostConfig.targetName = profileName;
        hostConfig.workingDirectory = workingDirectory;
        hostConfig.configInputs = configInputs;
        hostConfig.pluginSearchPaths = pluginSearchPaths;
        hostConfig.enableDynamicPlugins =
            m_enableDynamicPlugins || !pluginSearchPaths.empty();
        hostConfig.enableReflection = selectedProfile != nullptr
                                          ? selectedProfile->enableReflection
                                          : false;
        hostConfig.commandLineArgs = m_commandLineArgs;
        hostConfig.environmentName = environmentName;
        hostConfig.requestedModules = enabledModules;
        hostConfig.fileSystem = m_fileSystem;
        hostConfig.moduleCatalog = moduleCatalog.Value();

        const auto pendingServices = m_pendingServices;
        const bool addDefaults = m_addDefaultServices;
        const bool addLogging = m_addLogging;
        const bool addConfiguration = m_addConfiguration;

        hostConfig.configureServices =
            [pendingServices, addDefaults, addLogging, addConfiguration,
             profileName](
                KernelBootstrapContext &context) -> CoreResult<void>
        {
          ServiceScopeId hostScope = ServiceScopeId::Global();
          const bool requiresHostScope = std::any_of(
              pendingServices.begin(), pendingServices.end(),
              [](const PendingServiceRegistration &registration)
              {
                return registration.provider &&
                       registration.provider->Options().lifetime !=
                           ServiceLifetime::Singleton;
              });

          if (requiresHostScope)
          {
            auto scope = context.services->BeginScope(ServiceScopeKind::Host,
                                                      profileName);
            if (!scope)
            {
              return NGIN::Utilities::Unexpected<KernelError>(scope.Error());
            }
            hostScope = scope.Value();
          }

          if (addDefaults)
          {
            auto serviceProvider =
                NGIN::Memory::MakeSharedAs<IServiceProvider, detail::ServiceProviderReference>(
                    context.services.Get(), ServiceScopeId::Global());
            auto result = context.services->RegisterSingleton<IServiceProvider>(
                "Core.Services", serviceProvider);
            if (!result)
            {
              return NGIN::Utilities::Unexpected<KernelError>(result.Error());
            }

            result = context.services->RegisterSingleton<IServiceRegistry>(
                "Core.ServiceRegistry", context.services);
            if (!result)
            {
              return NGIN::Utilities::Unexpected<KernelError>(result.Error());
            }

            result = context.services->RegisterSingleton<IEventBus>(
                "Core.Events", context.events);
            if (!result)
            {
              return NGIN::Utilities::Unexpected<KernelError>(result.Error());
            }

            result = context.services->RegisterSingleton<ITaskRuntime>(
                "Core.Tasks", context.tasks);
            if (!result)
            {
              return NGIN::Utilities::Unexpected<KernelError>(result.Error());
            }
          }

          if (addConfiguration)
          {
            auto result = context.services->RegisterSingleton<IConfigStore>(
                "Core.Configuration", context.config);
            if (!result)
            {
              return NGIN::Utilities::Unexpected<KernelError>(result.Error());
            }
          }

          if (addLogging && context.loggerRegistry != nullptr)
          {
            auto result = context.services->RegisterSingletonValue<NGIN::Log::LoggerRegistry *>(
                "Core.Logging", context.loggerRegistry);
            if (!result)
            {
              return NGIN::Utilities::Unexpected<KernelError>(result.Error());
            }
          }

          for (const auto &registration : pendingServices)
          {
            if (!registration.provider)
            {
              continue;
            }

            ServiceRegistrationOptions options{};
            options.lifetime = registration.provider->Options().lifetime;
            options.ownerScope = options.lifetime == ServiceLifetime::Singleton
                                     ? ServiceScopeId::Global()
                                     : hostScope;
            options.metadata = registration.provider->Options().metadata;

            auto result = context.services->RegisterProvider(
                registration.provider->CloneWithOptions(std::move(options)));

            if (!result)
            {
              return NGIN::Utilities::Unexpected<KernelError>(result.Error());
            }
          }

          return {};
        };

        auto kernel = CreateKernel(hostConfig);
        if (!kernel)
        {
          return NGIN::Utilities::Unexpected<KernelError>(kernel.Error());
        }

        m_built = true;
        std::shared_ptr<IApplicationHost> host =
            std::make_shared<ApplicationHostImpl>(kernel.Value(),
                                                  std::move(metadataReport));
        return host;
      }

      void MarkMutating()
      {
        if (m_built && !m_stickyError.has_value())
        {
          m_stickyError =
              MakeBuilderError("builder can no longer be modified after Build()",
                               {}, KernelErrorCode::InvalidState);
        }
      }

      [[nodiscard]] auto HasStickyError() const noexcept -> bool
      {
        return m_stickyError.has_value();
      }

      std::vector<PendingServiceRegistration> m_pendingServices{};
      std::vector<PackageReference> m_packageReferences{};
      std::unordered_map<std::string, PackageManifest> m_packageManifests{};
      std::vector<PackageBootstrapRegistrarFn> m_packageRegistrars{};
      std::vector<PackageBootstrapRequest> m_explicitBootstrapRequests{};
      std::vector<StaticModuleRegistration> m_moduleRegistrations{};
      std::vector<std::string> m_enabledModules{};
      std::vector<std::string> m_disabledModules{};
      std::vector<std::string> m_enabledPlugins{};
      std::vector<std::string> m_disabledPlugins{};
      std::vector<std::string> m_pluginSearchPaths{};
      bool m_enableDynamicPlugins{false};
      std::vector<std::string> m_configInputs{};
      std::vector<std::string> m_commandLineArgs{};
      std::optional<ProjectManifest> m_project{};
      IoPath m_projectPath{};
      IoPath m_projectDirectory{};
      NGIN::Memory::Shared<IoFileSystem> m_fileSystem{};
      std::optional<KernelError> m_stickyError{};
      std::string m_applicationName{};
      std::string m_profileOverride{};
      std::string m_environmentName{};
      std::string m_workingDirectory{};
      bool m_addDefaultServices{false};
      bool m_addLogging{false};
      bool m_addConfiguration{false};
      bool m_built{false};
      ServiceCollectionImpl m_services;
      PackageCollectionImpl m_packages;
      ModuleCollectionImpl m_modules;
      PluginCollectionImpl m_plugins;
      ConfigurationBuilderImpl m_configuration;
    };

    auto ServiceCollectionImpl::AddProvider(
        std::shared_ptr<detail::ServiceProviderBase> provider)
        -> ServiceCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        m_owner.m_pendingServices.push_back(PendingServiceRegistration{
            .provider = std::move(provider),
        });
      }
      return *this;
    }

    auto ServiceCollectionImpl::AddDefaults() -> ServiceCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        m_owner.m_addDefaultServices = true;
      }
      return *this;
    }

    auto ServiceCollectionImpl::AddLogging() -> ServiceCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        m_owner.m_addLogging = true;
      }
      return *this;
    }

    auto ServiceCollectionImpl::AddConfiguration() -> ServiceCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        m_owner.m_addConfiguration = true;
      }
      return *this;
    }

    auto ServiceCollectionImpl::Clear() -> ServiceCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        m_owner.m_pendingServices.clear();
        m_owner.m_addDefaultServices = false;
        m_owner.m_addLogging = false;
        m_owner.m_addConfiguration = false;
      }
      return *this;
    }

    auto PackageCollectionImpl::Add(PackageReference reference)
        -> PackageCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        m_owner.m_packageReferences.push_back(std::move(reference));
      }
      return *this;
    }

    auto PackageCollectionImpl::AddManifest(PackageManifest manifest)
        -> PackageCollection &
    {
      m_owner.MarkMutating();
      if (m_owner.HasStickyError())
      {
        return *this;
      }

      if (manifest.name.empty())
      {
        m_owner.m_stickyError =
            MakeBuilderError("package manifest name must not be empty", {},
                             KernelErrorCode::InvalidArgument);
        return *this;
      }

      m_owner.m_packageManifests[manifest.name] = std::move(manifest);
      return *this;
    }

    auto PackageCollectionImpl::AddManifestFile(std::string path)
        -> PackageCollection &
    {
      m_owner.MarkMutating();
      if (m_owner.HasStickyError())
      {
        return *this;
      }

      IoPath manifestPath(path);
      if (manifestPath.IsRelative() && !m_owner.m_projectDirectory.IsEmpty())
      {
        manifestPath =
            m_owner.m_projectDirectory.Join(manifestPath.View()).LexicallyNormal();
      }

      auto fileText = ReadTextFile(m_owner.FileSystem(), manifestPath, "package");
      if (!fileText)
      {
        m_owner.m_stickyError = fileText.Error();
        return *this;
      }

      auto manifest = ParsePackageManifestText(fileText.Value());
      if (!manifest)
      {
        m_owner.m_stickyError = manifest.Error();
        return *this;
      }

      auto loadedManifest = manifest.Value();
      loadedManifest.path = ToString(manifestPath);
      loadedManifest.directory = ToString(manifestPath.Parent());
      m_owner.m_packageManifests[loadedManifest.name] = std::move(loadedManifest);
      return *this;
    }

    auto PackageCollectionImpl::RegisterLinkedRegistrar(
        PackageBootstrapRegistrarFn registrar) -> PackageCollection &
    {
      m_owner.MarkMutating();
      if (m_owner.HasStickyError())
      {
        return *this;
      }

      if (registrar == nullptr)
      {
        m_owner.m_stickyError =
            MakeBuilderError("package bootstrap registrar must not be null", {},
                             KernelErrorCode::InvalidArgument);
        return *this;
      }

      m_owner.m_packageRegistrars.push_back(registrar);
      return *this;
    }

    auto PackageCollectionImpl::ApplyBootstrap(std::string packageName)
        -> PackageCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        const auto duplicate =
            std::find_if(m_owner.m_explicitBootstrapRequests.begin(),
                         m_owner.m_explicitBootstrapRequests.end(),
                         [&](const PackageBootstrapRequest &request)
                         {
                           return request.packageName == packageName &&
                                  !request.entryPoint.has_value();
                         });

        if (duplicate == m_owner.m_explicitBootstrapRequests.end())
        {
          m_owner.m_explicitBootstrapRequests.push_back(PackageBootstrapRequest{
              .packageName = std::move(packageName),
              .entryPoint = std::nullopt,
          });
        }
      }
      return *this;
    }

    auto PackageCollectionImpl::ApplyBootstrap(std::string packageName,
                                               std::string entryPoint)
        -> PackageCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        const auto duplicate =
            std::find_if(m_owner.m_explicitBootstrapRequests.begin(),
                         m_owner.m_explicitBootstrapRequests.end(),
                         [&](const PackageBootstrapRequest &request)
                         {
                           return request.packageName == packageName &&
                                  request.entryPoint.has_value() &&
                                  *request.entryPoint == entryPoint;
                         });

        if (duplicate == m_owner.m_explicitBootstrapRequests.end())
        {
          m_owner.m_explicitBootstrapRequests.push_back(PackageBootstrapRequest{
              .packageName = std::move(packageName),
              .entryPoint = std::move(entryPoint),
          });
        }
      }
      return *this;
    }

    auto PackageCollectionImpl::Clear() -> PackageCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        m_owner.m_packageReferences.clear();
        m_owner.m_packageManifests.clear();
        m_owner.m_packageRegistrars.clear();
        m_owner.m_explicitBootstrapRequests.clear();
      }
      return *this;
    }

    auto ModuleCollectionImpl::Register(StaticModuleRegistration registration)
        -> ModuleCollection &
    {
      m_owner.MarkMutating();
      if (m_owner.HasStickyError())
      {
        return *this;
      }

      if (registration.descriptor.name.empty() || !registration.factory)
      {
        m_owner.m_stickyError = MakeBuilderError(
            "module registration must include a descriptor name and factory",
            registration.descriptor.name, KernelErrorCode::InvalidArgument);
        return *this;
      }

      AppendOrReplaceModuleRegistration(m_owner.m_moduleRegistrations,
                                        std::move(registration));
      return *this;
    }

    auto ModuleCollectionImpl::Enable(std::string moduleName)
        -> ModuleCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        AppendUnique(m_owner.m_enabledModules, moduleName);
      }
      return *this;
    }

    auto ModuleCollectionImpl::Disable(std::string moduleName)
        -> ModuleCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        AppendUnique(m_owner.m_disabledModules, moduleName);
      }
      return *this;
    }

    auto ModuleCollectionImpl::Clear() -> ModuleCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        m_owner.m_moduleRegistrations.clear();
        m_owner.m_enabledModules.clear();
        m_owner.m_disabledModules.clear();
      }
      return *this;
    }

    auto PluginCollectionImpl::Enable(std::string pluginName)
        -> PluginCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        AppendUnique(m_owner.m_enabledPlugins, pluginName);
      }
      return *this;
    }

    auto PluginCollectionImpl::Disable(std::string pluginName)
        -> PluginCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        AppendUnique(m_owner.m_disabledPlugins, pluginName);
      }
      return *this;
    }

    auto PluginCollectionImpl::AddSearchPath(std::string path)
        -> PluginCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        AppendUnique(m_owner.m_pluginSearchPaths, path);
        m_owner.m_enableDynamicPlugins = true;
      }
      return *this;
    }

    auto PluginCollectionImpl::Clear() -> PluginCollection &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        m_owner.m_enabledPlugins.clear();
        m_owner.m_disabledPlugins.clear();
        m_owner.m_pluginSearchPaths.clear();
        m_owner.m_enableDynamicPlugins = false;
      }
      return *this;
    }

    auto ConfigurationBuilderImpl::AddSource(std::string path)
        -> ConfigurationBuilder &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        AppendUnique(m_owner.m_configInputs, path);
      }
      return *this;
    }

    auto ConfigurationBuilderImpl::SetEnvironmentName(std::string environmentName)
        -> ConfigurationBuilder &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        m_owner.m_environmentName = std::move(environmentName);
      }
      return *this;
    }

    auto ConfigurationBuilderImpl::SetWorkingDirectory(std::string workingDirectory)
        -> ConfigurationBuilder &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        m_owner.m_workingDirectory = std::move(workingDirectory);
      }
      return *this;
    }

    auto ConfigurationBuilderImpl::Clear() -> ConfigurationBuilder &
    {
      m_owner.MarkMutating();
      if (!m_owner.HasStickyError())
      {
        m_owner.m_configInputs.clear();
        m_owner.m_environmentName.clear();
        m_owner.m_workingDirectory.clear();
      }
      return *this;
    }
  } // namespace

  auto CreateApplicationBuilder(const int argc, char **argv)
      -> std::unique_ptr<ApplicationBuilder>
  {
    return std::make_unique<ApplicationBuilderImpl>(argc, argv);
  }
} // namespace NGIN::Core
