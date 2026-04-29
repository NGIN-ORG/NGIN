#include <NGIN/Core/Application.hpp>

#include <NGIN/IO/FileSystemUtilities.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>
#include <NGIN/Serialization/XML/XmlParser.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <set>
#include <sstream>
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

    [[nodiscard]] auto IsValidOperatingSystem(const std::string_view value) -> bool
    {
      return value == "linux" || value == "windows" || value == "macos";
    }

    [[nodiscard]] auto IsValidArchitecture(const std::string_view value) -> bool
    {
      return value == "x64" || value == "arm64";
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
            "legacy <Platforms> is no longer supported", std::string(context),
            KernelErrorCode::SchemaValidationFailure));
      }
      if (FindChild(element, "SupportedHosts") != nullptr)
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            "legacy <SupportedHosts> is no longer supported",
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
      if (text == "BuilderHookV1")
        return PackageBootstrapMode::BuilderHookV1;
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
          OptionalAttribute(element, "StartupStage", context, "Features");
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
          descriptor.capabilities.push_back(capability.Value());
        }
      }

      return descriptor;
    }

    [[nodiscard]] auto ParsePackagePluginManifest(const XmlElement &element,
                                                  const std::string_view context)
        -> CoreResult<PackagePluginManifest>
    {
      PackagePluginManifest plugin{};

      auto name = RequireAttribute(element, "Name", context);
      if (!name)
      {
        return NGIN::Utilities::Unexpected<KernelError>(name.Error());
      }
      plugin.name = name.Value();

      auto optional = OptionalBoolAttribute(
          element, "Optional", std::string(context) + ".Optional", false);
      if (!optional)
      {
        return NGIN::Utilities::Unexpected<KernelError>(optional.Error());
      }
      plugin.optional = optional.Value();

      auto compatibility = ReadCompatibility(element, context,
                                             plugin.operatingSystems,
                                             plugin.architectures);
      if (!compatibility)
      {
        return NGIN::Utilities::Unexpected<KernelError>(compatibility.Error());
      }

      if (const auto *modules = FindChild(element, "Modules"))
      {
        if (const auto *required = FindChild(*modules, "Required"))
        {
          for (const auto *moduleElement : ChildElements(*required, "ModuleRef"))
          {
            auto moduleName = RequireAttribute(
                *moduleElement, "Name", std::string(context) + ".Modules.Required");
            if (!moduleName)
            {
              return NGIN::Utilities::Unexpected<KernelError>(moduleName.Error());
            }
            plugin.requiredModules.push_back(moduleName.Value());
          }
        }
        if (const auto *optionalModules = FindChild(*modules, "Optional"))
        {
          for (const auto *moduleElement :
               ChildElements(*optionalModules, "ModuleRef"))
          {
            auto moduleName = RequireAttribute(
                *moduleElement, "Name", std::string(context) + ".Modules.Optional");
            if (!moduleName)
            {
              return NGIN::Utilities::Unexpected<KernelError>(moduleName.Error());
            }
            plugin.optionalModules.push_back(moduleName.Value());
          }
        }
      }

      return plugin;
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
      };
    }

    [[nodiscard]] auto ParseModuleRefs(const XmlElement *parentElement,
                                       const std::string_view childName,
                                       const std::string_view context)
        -> CoreResult<std::vector<std::string>>
    {
      std::vector<std::string> out{};
      if (parentElement == nullptr)
      {
        return out;
      }

      for (const auto *moduleElement : ChildElements(*parentElement, childName))
      {
        auto name =
            RequireAttribute(*moduleElement, "Name",
                             std::string(context) + "." + std::string(childName));
        if (!name)
        {
          return NGIN::Utilities::Unexpected<KernelError>(name.Error());
        }
        out.push_back(name.Value());
      }
      return out;
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

      return reference;
    }

    [[nodiscard]] auto ParseOutputDefinition(const XmlElement &element,
                                             const std::string_view context)
        -> CoreResult<OutputDefinition>
    {
      OutputDefinition output{};

      auto kind = RequireAttribute(element, "Kind", context);
      if (!kind)
      {
        return NGIN::Utilities::Unexpected<KernelError>(kind.Error());
      }
      output.kind = kind.Value();

      auto name = RequireAttribute(element, "Name", context);
      if (!name)
      {
        return NGIN::Utilities::Unexpected<KernelError>(name.Error());
      }
      output.name = name.Value();

      auto target = RequireAttribute(element, "Target", context);
      if (!target)
      {
        return NGIN::Utilities::Unexpected<KernelError>(target.Error());
      }
      output.target = target.Value();

      return output;
    }

    [[nodiscard]] auto SplitPathList(const std::string &value)
        -> std::vector<std::string>
    {
      std::vector<std::string> items{};
      std::string current{};
      std::istringstream stream(value);
      while (std::getline(stream, current, ','))
      {
        current.erase(current.begin(),
                      std::find_if(current.begin(), current.end(),
                                   [](const unsigned char ch)
                                   { return !std::isspace(ch); }));
        current.erase(std::find_if(current.rbegin(), current.rend(),
                                   [](const unsigned char ch)
                                   { return !std::isspace(ch); })
                          .base(),
                      current.end());
        if (!current.empty())
        {
          items.push_back(current);
        }
      }
      return items;
    }

    [[nodiscard]] auto IsSupportedInputKind(const std::string &kind) -> bool
    {
      return kind == "Source" || kind == "Config" || kind == "Content" ||
             kind == "Asset" || kind == "Generated" || kind == "ToolInput";
    }

    [[nodiscard]] auto IsSupportedInputMode(const std::string &mode) -> bool
    {
      return mode == "Directory" || mode == "File" || mode == "Glob";
    }

    [[nodiscard]] auto IsSupportedGeneratedRole(const std::string &role) -> bool
    {
      return role == "Source" || role == "Content" || role == "Asset" ||
             role == "ToolInput";
    }

    [[nodiscard]] auto IsTypedInputBlock(const std::string_view name) -> bool
    {
      return name == "Sources" || name == "Headers" || name == "Configs" ||
             name == "Contents" || name == "Assets" || name == "Generated" ||
             name == "ToolInputs";
    }

    [[nodiscard]] auto TextContent(const XmlElement &element) -> std::string
    {
      std::string text{};
      for (NGIN::UIntSize index = 0; index < element.children.Size(); ++index)
      {
        const auto &child = element.children[index];
        if (child.type == XmlNode::Type::Text || child.type == XmlNode::Type::CData)
        {
          text.append(child.text.data(), child.text.size());
        }
      }
      return text;
    }

    [[nodiscard]] auto SplitTextPathLines(const std::string_view text)
        -> std::vector<std::string>
    {
      std::vector<std::string> entries{};
      std::string current{};
      auto flush = [&]()
      {
        auto first = std::find_if(current.begin(), current.end(),
                                  [](const unsigned char ch)
                                  { return !std::isspace(ch); });
        auto last = std::find_if(current.rbegin(), current.rend(),
                                 [](const unsigned char ch)
                                 { return !std::isspace(ch); })
                        .base();
        std::string value = first < last ? std::string(first, last) : std::string{};
        current.clear();
        if (!value.empty() && value.front() != '#')
        {
          entries.push_back(std::move(value));
        }
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

    [[nodiscard]] auto JoinPathList(const std::vector<std::string> &entries)
        -> std::string
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

    [[nodiscard]] auto ReadInputAttributes(const XmlElement &element,
                                           const std::string_view context,
                                           InputDeclaration input = {})
        -> CoreResult<InputDeclaration>
    {
      if (auto value = OptionalAttribute(element, "Path", context, input.path); !value) { return NGIN::Utilities::Unexpected<KernelError>(value.Error()); } else { input.path = value.Value(); }
      if (auto value = OptionalAttribute(element, "Name", context, input.name); !value) { return NGIN::Utilities::Unexpected<KernelError>(value.Error()); } else { input.name = value.Value(); }
      if (auto value = OptionalAttribute(element, "Mode", context, input.mode); !value) { return NGIN::Utilities::Unexpected<KernelError>(value.Error()); } else { input.mode = value.Value(); }
      if (auto value = OptionalAttribute(element, "Visibility", context, input.visibility); !value) { return NGIN::Utilities::Unexpected<KernelError>(value.Error()); } else { input.visibility = value.Value().empty() ? input.visibility : value.Value(); }
      if (auto value = OptionalAttribute(element, "Target", context, input.target); !value) { return NGIN::Utilities::Unexpected<KernelError>(value.Error()); } else { input.target = value.Value(); }
      if (auto value = OptionalAttribute(element, "TargetRoot", context, input.targetRoot); !value) { return NGIN::Utilities::Unexpected<KernelError>(value.Error()); } else { input.targetRoot = value.Value(); }
      if (auto value = OptionalAttribute(element, "BasePath", context, input.basePath); !value) { return NGIN::Utilities::Unexpected<KernelError>(value.Error()); } else { input.basePath = value.Value(); }
      if (auto value = OptionalAttribute(element, "ContentKind", context, input.contentKind); !value) { return NGIN::Utilities::Unexpected<KernelError>(value.Error()); } else { input.contentKind = value.Value(); }
      if (auto required = OptionalBoolAttribute(element, "Required", context, input.required); !required) { return NGIN::Utilities::Unexpected<KernelError>(required.Error()); } else { input.required = required.Value(); }
      if (auto overrideExisting = OptionalBoolAttribute(element, "Override", context, input.overrideExisting); !overrideExisting) { return NGIN::Utilities::Unexpected<KernelError>(overrideExisting.Error()); } else { input.overrideExisting = overrideExisting.Value(); }

      input.profile = Attribute(element, "Profile").value_or(input.profile);
      input.platform = Attribute(element, "Platform").value_or(input.platform);
      input.operatingSystem = Attribute(element, "OperatingSystem").value_or(input.operatingSystem);
      input.architecture = Attribute(element, "Architecture").value_or(input.architecture);
      input.buildType = Attribute(element, "BuildType").value_or(input.buildType);
      input.environment = Attribute(element, "Environment").value_or(input.environment);
      input.condition = Attribute(element, "Condition").value_or(input.condition);
      if (const auto include = Attribute(element, "Include"); include.has_value()) { input.includePatterns = SplitPathList(*include); }
      if (const auto exclude = Attribute(element, "Exclude"); exclude.has_value()) { input.excludePatterns = SplitPathList(*exclude); }
      return input;
    }

    [[nodiscard]] auto ValidateInputDeclaration(const InputDeclaration &input,
                                                const std::string_view context)
        -> CoreResult<void>
    {
      if (input.kind.empty() || !IsSupportedInputKind(input.kind))
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(std::string(context) + ".Input has unsupported Kind", input.kind, KernelErrorCode::SchemaValidationFailure));
      }
      if (input.kind == "Generated" && !IsSupportedGeneratedRole(input.role))
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(std::string(context) + ".Generated requires Role", input.role, KernelErrorCode::SchemaValidationFailure));
      }
      if (input.mode.empty() || !IsSupportedInputMode(input.mode))
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(std::string(context) + ".Input has unsupported Mode", input.mode, KernelErrorCode::SchemaValidationFailure));
      }
      if ((input.mode == "File" || input.mode == "Directory") && input.path.empty())
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(std::string(context) + ".Input requires Path", {}, KernelErrorCode::SchemaValidationFailure));
      }
      if (input.mode == "Glob" && input.includePatterns.empty())
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(std::string(context) + ".Glob requires Include", {}, KernelErrorCode::SchemaValidationFailure));
      }
      if (input.visibility != "Public" && input.visibility != "Private" && input.visibility != "Interface")
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(std::string(context) + ".Input has unsupported Visibility", input.visibility, KernelErrorCode::SchemaValidationFailure));
      }
      return {};
    }

    [[nodiscard]] auto InputMatchesIdentity(const InputDeclaration &left,
                                            const InputDeclaration &right) -> bool
    {
      if (!right.name.empty())
      {
        return left.name == right.name;
      }
      return left.kind == right.kind && left.role == right.role && left.path == right.path &&
             left.pattern == right.pattern && left.mode == right.mode && left.target == right.target &&
             left.targetRoot == right.targetRoot && left.basePath == right.basePath &&
             left.visibility == right.visibility;
    }

    auto AddInput(std::vector<InputDeclaration> &out, InputDeclaration input) -> void
    {
      if (input.overrideExisting)
      {
        out.erase(std::remove_if(out.begin(), out.end(), [&](const InputDeclaration &existing) { return InputMatchesIdentity(existing, input); }), out.end());
      }
      out.push_back(std::move(input));
    }

    auto ParseInputDeclarations(const XmlElement &element,
                                const std::string_view context,
                                std::vector<InputDeclaration> &out)
        -> CoreResult<void>
    {
      if (FindChild(element, "Sources") != nullptr || FindChild(element, "SourceRoots") != nullptr || FindChild(element, "Contents") != nullptr)
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(std::string(context) + " uses removed top-level input vocabulary; use <Inputs>", {}, KernelErrorCode::SchemaValidationFailure));
      }
      if (const auto *inputs = FindChild(element, "Inputs"))
      {
        for (const auto *child : ChildElements(*inputs))
        {
          if (child->name == "Input" || child->name == "InputSet" || child->name == "Config")
          {
            return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(std::string(context) + ".Inputs uses removed generic input vocabulary", std::string(child->name), KernelErrorCode::SchemaValidationFailure));
          }
          if (child->name == "Remove")
          {
            auto name = Attribute(*child, "Name");
            auto kind = Attribute(*child, "Kind");
            auto role = Attribute(*child, "Role");
            if (kind.has_value() && *kind == "Header")
            {
              kind = "Source";
              role = "Header";
            }
            auto path = Attribute(*child, "Path");
            out.erase(std::remove_if(out.begin(), out.end(), [&](const InputDeclaration &input) {
                        if (name.has_value()) { return input.name == *name || input.setName == *name; }
                        return (!kind.has_value() || input.kind == *kind) &&
                               (!role.has_value() || input.role == *role) &&
                               (!path.has_value() || input.path == *path);
                      }), out.end());
            continue;
          }
          if (!IsTypedInputBlock(child->name))
          {
            return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(std::string(context) + ".Inputs contains unsupported child element", std::string(child->name), KernelErrorCode::SchemaValidationFailure));
          }

          InputDeclaration base{};
          if (child->name == "Sources") { base.kind = "Source"; base.role = "Source"; base.visibility = "Private"; }
          else if (child->name == "Headers") { base.kind = "Source"; base.role = "Header"; base.visibility = "Public"; }
          else if (child->name == "Configs") { base.kind = "Config"; }
          else if (child->name == "Contents") { base.kind = "Content"; }
          else if (child->name == "Assets") { base.kind = "Asset"; }
          else if (child->name == "ToolInputs") { base.kind = "ToolInput"; }
          else if (child->name == "Generated") { base.kind = "Generated"; base.role = Attribute(*child, "Role").value_or({}); }
          base.setName = Attribute(*child, "Name").value_or({});
          auto parsedBase = ReadInputAttributes(*child, context, base);
          if (!parsedBase) { return NGIN::Utilities::Unexpected<KernelError>(parsedBase.Error()); }
          base = std::move(parsedBase.Value());

          auto addParsed = [&](InputDeclaration input) -> CoreResult<void> {
            input.declaringScope = std::string(context);
            if (auto validated = ValidateInputDeclaration(input, context); !validated)
            {
              return NGIN::Utilities::Unexpected<KernelError>(validated.Error());
            }
            AddInput(out, std::move(input));
            return {};
          };

          if (!base.path.empty() || !base.includePatterns.empty())
          {
            auto input = base;
            input.name.clear();
            input.target.clear();
            input.overrideExisting = false;
            input.mode = !input.path.empty() ? "Directory" : "Glob";
            if (input.mode == "Glob") { input.pattern = JoinPathList(input.includePatterns); }
            if (auto added = addParsed(std::move(input)); !added) { return NGIN::Utilities::Unexpected<KernelError>(added.Error()); }
          }
          for (const auto &line : SplitTextPathLines(TextContent(*child)))
          {
            auto input = base;
            input.path = line;
            input.pattern.clear();
            input.mode = "File";
            input.name.clear();
            input.target.clear();
            input.overrideExisting = false;
            if (auto added = addParsed(std::move(input)); !added) { return NGIN::Utilities::Unexpected<KernelError>(added.Error()); }
          }
          for (const auto *entry : ChildElements(*child))
          {
            if (entry->name == "Metadata") { continue; }
            if (entry->name != "File" && entry->name != "Directory" && entry->name != "Glob")
            {
              return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(std::string(context) + ".Inputs contains unsupported typed entry", std::string(entry->name), KernelErrorCode::SchemaValidationFailure));
            }
            auto input = base;
            input.path.clear();
            input.pattern.clear();
            input.name.clear();
            input.target.clear();
            input.overrideExisting = false;
            if (entry->name == "File") { input.mode = "File"; input.path = Attribute(*entry, "Path").value_or({}); }
            else if (entry->name == "Directory") { input.mode = "Directory"; input.path = Attribute(*entry, "Path").value_or({}); }
            else { input.mode = "Glob"; }
            auto parsed = ReadInputAttributes(*entry, context, input);
            if (!parsed) { return NGIN::Utilities::Unexpected<KernelError>(parsed.Error()); }
            input = std::move(parsed.Value());
            if (input.mode == "Glob") { input.pattern = JoinPathList(input.includePatterns); }
            if (auto added = addParsed(std::move(input)); !added) { return NGIN::Utilities::Unexpected<KernelError>(added.Error()); }
          }
        }
      }
      return {};
    }

    [[nodiscard]] auto ParseBuildSetting(const XmlElement &element,
                                         const std::string_view context,
                                         const std::string_view valueAttribute)
        -> CoreResult<BuildSetting>
    {
      BuildSetting setting{};

      auto value = RequireAttribute(element, valueAttribute, context);
      if (!value)
      {
        return NGIN::Utilities::Unexpected<KernelError>(value.Error());
      }
      setting.value = value.Value();

      auto visibility =
          OptionalAttribute(element, "Visibility", context, "Private");
      if (!visibility)
      {
        return NGIN::Utilities::Unexpected<KernelError>(visibility.Error());
      }
      if (visibility.Value() != "Private" && visibility.Value() != "Public" &&
          visibility.Value() != "Interface")
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            std::string(context) + " uses unknown build visibility",
            visibility.Value()));
      }
      setting.visibility = visibility.Value();
      return setting;
    }

    [[nodiscard]] auto ParseProjectBuildDescriptor(const XmlElement *buildElement,
                                                   const std::string_view context)
        -> CoreResult<ProjectBuildDescriptor>
    {
      ProjectBuildDescriptor build{};
      if (buildElement == nullptr)
      {
        return build;
      }

      auto backend = OptionalAttribute(*buildElement, "Backend", context, "CMake");
      if (!backend)
      {
        return NGIN::Utilities::Unexpected<KernelError>(backend.Error());
      }
      build.backend = backend.Value();

      auto mode = OptionalAttribute(*buildElement, "Mode", context, "Generated");
      if (!mode)
      {
        return NGIN::Utilities::Unexpected<KernelError>(mode.Error());
      }
      if (mode.Value() != "Generated" && mode.Value() != "Manual")
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            std::string(context) + " uses unknown build mode", mode.Value()));
      }
      build.mode = mode.Value();

      auto language = OptionalAttribute(*buildElement, "Language", context, "CXX");
      if (!language)
      {
        return NGIN::Utilities::Unexpected<KernelError>(language.Error());
      }
      build.language = language.Value();

      auto languageStandard =
          OptionalAttribute(*buildElement, "LanguageStandard", context, "23");
      if (!languageStandard)
      {
        return NGIN::Utilities::Unexpected<KernelError>(languageStandard.Error());
      }
      build.languageStandard = languageStandard.Value();

      if (const auto *sourcesElement = FindChild(*buildElement, "Sources"))
      {
        for (const auto *sourceElement : ChildElements(*sourcesElement, "Source"))
        {
          auto path = RequireAttribute(*sourceElement, "Path",
                                       std::string(context) + ".Sources");
          if (!path)
          {
            return NGIN::Utilities::Unexpected<KernelError>(path.Error());
          }
          build.sources.push_back(path.Value());
        }
      }

      auto parseSettings =
          [&](const XmlElement *section, const std::string_view childName,
              const std::string_view attrName, std::vector<BuildSetting> &output,
              const std::string &sectionContext) -> CoreResult<void>
      {
        if (section == nullptr)
        {
          return {};
        }
        for (const auto *child : ChildElements(*section, childName))
        {
          auto setting = ParseBuildSetting(*child, sectionContext, attrName);
          if (!setting)
          {
            return NGIN::Utilities::Unexpected<KernelError>(setting.Error());
          }
          output.push_back(setting.Value());
        }
        return {};
      };

      auto includeDirectories =
          parseSettings(FindChild(*buildElement, "IncludeDirectories"),
                        "IncludeDirectory", "Path", build.includeDirectories,
                        std::string(context) + ".IncludeDirectories");
      if (!includeDirectories)
      {
        return NGIN::Utilities::Unexpected<KernelError>(includeDirectories.Error());
      }

      auto compileDefinitions = parseSettings(
          FindChild(*buildElement, "CompileDefinitions"), "Definition", "Value",
          build.compileDefinitions, std::string(context) + ".CompileDefinitions");
      if (!compileDefinitions)
      {
        return NGIN::Utilities::Unexpected<KernelError>(compileDefinitions.Error());
      }

      auto compileOptions = parseSettings(
          FindChild(*buildElement, "CompileOptions"), "Option", "Value",
          build.compileOptions, std::string(context) + ".CompileOptions");
      if (!compileOptions)
      {
        return NGIN::Utilities::Unexpected<KernelError>(compileOptions.Error());
      }

      auto linkOptions =
          parseSettings(FindChild(*buildElement, "LinkOptions"), "Option", "Value",
                        build.linkOptions, std::string(context) + ".LinkOptions");
      if (!linkOptions)
      {
        return NGIN::Utilities::Unexpected<KernelError>(linkOptions.Error());
      }

      return build;
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

      if (const auto *modulesElement = FindChild(*runtimeElement, "Modules"))
      {
        std::size_t index = 0;
        for (const auto *moduleElement : ChildElements(*modulesElement, "Module"))
        {
          auto descriptor = ParsePackageModuleDescriptor(
              *moduleElement,
              std::string(context) + ".Modules[" + std::to_string(index++) + "]");
          if (!descriptor)
          {
            return NGIN::Utilities::Unexpected<KernelError>(descriptor.Error());
          }
          runtime.modules.push_back(descriptor.Value());
        }
      }

      auto enabled =
          ParseModuleRefs(FindChild(*runtimeElement, "EnableModules"), "ModuleRef",
                          std::string(context) + ".EnableModules");
      if (!enabled)
      {
        return NGIN::Utilities::Unexpected<KernelError>(enabled.Error());
      }
      runtime.enableModules = std::move(enabled.Value());

      auto disabled =
          ParseModuleRefs(FindChild(*runtimeElement, "DisableModules"), "ModuleRef",
                          std::string(context) + ".DisableModules");
      if (!disabled)
      {
        return NGIN::Utilities::Unexpected<KernelError>(disabled.Error());
      }
      runtime.disableModules = std::move(disabled.Value());

      return runtime;
    }

    struct ProjectDefaults
    {
      std::optional<std::string> buildType{};
      std::optional<std::string> platform{};
      std::optional<std::string> environment{};
      std::optional<std::string> backend{};
      std::optional<std::string> buildMode{};
      std::optional<std::string> language{};
      std::optional<std::string> languageStandard{};
    };

    struct PlatformDefinition
    {
      std::string name{};
      std::string operatingSystem{};
      std::string architecture{};
      bool builtIn{false};
    };

    struct ProjectTemplateDefinition
    {
      std::string name{};
      std::string type{"Application"};
      std::string outputKind{"Executable"};
      bool builtIn{false};
      ProjectDefaults defaults{};
    };

    struct ProfileTemplateDefinition
    {
      std::string name{};
      std::optional<std::string> extends{};
      std::optional<std::string> buildType{};
      std::optional<std::string> platform{};
      std::optional<std::string> operatingSystem{};
      std::optional<std::string> architecture{};
      std::optional<std::string> environment{};
      std::optional<bool> enableReflection{};
      std::optional<LaunchDefinition> launch{};
      std::vector<ProjectReference> projectRefs{};
      std::vector<PackageReference> packageRefs{};
      std::vector<InputDeclaration> inputs{};
      RuntimeDefinition runtime{};
    };

    struct ModelContext
    {
      ProjectDefaults defaults{};
      std::unordered_map<std::string, PlatformDefinition> platforms{};
      std::unordered_map<std::string, ProjectTemplateDefinition> projectTemplates{};
      std::unordered_map<std::string, ProfileTemplateDefinition> profileTemplates{};
    };

    auto MergeRuntime(RuntimeDefinition &target, const RuntimeDefinition &source)
        -> void
    {
      target.modules.insert(target.modules.end(), source.modules.begin(),
                            source.modules.end());
      target.enableModules.insert(target.enableModules.end(),
                                  source.enableModules.begin(),
                                  source.enableModules.end());
      target.disableModules.insert(target.disableModules.end(),
                                   source.disableModules.begin(),
                                   source.disableModules.end());
    }

    [[nodiscard]] auto ResolveChildPath(const IoPath &baseDirectory,
                                        const std::string &includePath) -> IoPath
    {
      const IoPath path(includePath);
      if (path.IsAbsolute())
      {
        return path;
      }
      return baseDirectory.Join(includePath);
    }

    auto MergeDefaultsFromElement(const XmlElement &element, ModelContext &context)
        -> CoreResult<void>
    {
      if (const auto value = Attribute(element, "BuildType"))
      {
        context.defaults.buildType = *value;
      }
      if (const auto value = Attribute(element, "Platform"))
      {
        context.defaults.platform = *value;
      }
      if (const auto value = Attribute(element, "Environment"))
      {
        context.defaults.environment = *value;
      }
      if (const auto value = Attribute(element, "Backend"))
      {
        context.defaults.backend = *value;
      }
      if (const auto value = Attribute(element, "BuildMode"))
      {
        context.defaults.buildMode = *value;
      }
      if (const auto value = Attribute(element, "Mode"))
      {
        context.defaults.buildMode = *value;
      }
      if (const auto value = Attribute(element, "Language"))
      {
        context.defaults.language = *value;
      }
      if (const auto value = Attribute(element, "LanguageStandard"))
      {
        context.defaults.languageStandard = *value;
      }
      return {};
    }

    auto AddPlatform(ModelContext &context, PlatformDefinition platform)
        -> CoreResult<void>
    {
      if (auto it = context.platforms.find(platform.name);
          it != context.platforms.end())
      {
        if (!it->second.builtIn)
        {
          return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
              "duplicate platform definition", platform.name,
              KernelErrorCode::AlreadyExists));
        }
      }
      context.platforms[platform.name] = std::move(platform);
      return {};
    }

    auto ParsePlatformsInto(const XmlElement &root, ModelContext &context)
        -> CoreResult<void>
    {
      const auto *platforms = FindChild(root, "Platforms");
      if (platforms == nullptr)
      {
        return {};
      }
      for (const auto *platformElement : ChildElements(*platforms, "Platform"))
      {
        auto name = RequireAttribute(*platformElement, "Name", "model.Platform");
        if (!name)
        {
          return NGIN::Utilities::Unexpected<KernelError>(name.Error());
        }
        auto operatingSystem = RequireAttribute(*platformElement, "OperatingSystem",
                                                "model.Platform");
        if (!operatingSystem)
        {
          return NGIN::Utilities::Unexpected<KernelError>(operatingSystem.Error());
        }
        auto architecture =
            RequireAttribute(*platformElement, "Architecture", "model.Platform");
        if (!architecture)
        {
          return NGIN::Utilities::Unexpected<KernelError>(architecture.Error());
        }
        auto added = AddPlatform(context, PlatformDefinition{
                                              .name = name.Value(),
                                              .operatingSystem = operatingSystem.Value(),
                                              .architecture = architecture.Value(),
                                              .builtIn = false});
        if (!added)
        {
          return NGIN::Utilities::Unexpected<KernelError>(added.Error());
        }
      }
      return {};
    }

    auto AddProjectTemplate(ModelContext &context,
                            ProjectTemplateDefinition projectTemplate)
        -> CoreResult<void>
    {
      if (auto it = context.projectTemplates.find(projectTemplate.name);
          it != context.projectTemplates.end())
      {
        if (!it->second.builtIn)
        {
          return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
              "duplicate project template", projectTemplate.name,
              KernelErrorCode::AlreadyExists));
        }
      }
      context.projectTemplates[projectTemplate.name] = std::move(projectTemplate);
      return {};
    }

    auto ParseProjectTemplatesInto(const XmlElement &root, ModelContext &context)
        -> CoreResult<void>
    {
      const auto *templates = FindChild(root, "ProjectTemplates");
      if (templates == nullptr)
      {
        return {};
      }
      for (const auto *templateElement :
           ChildElements(*templates, "ProjectTemplate"))
      {
        auto name =
            RequireAttribute(*templateElement, "Name", "model.ProjectTemplate");
        if (!name)
        {
          return NGIN::Utilities::Unexpected<KernelError>(name.Error());
        }
        ProjectTemplateDefinition projectTemplate{};
        projectTemplate.name = name.Value();
        projectTemplate.defaults = context.defaults;
        projectTemplate.type =
            Attribute(*templateElement, "Type").value_or("Application");
        projectTemplate.outputKind =
            Attribute(*templateElement, "OutputKind").value_or(
                projectTemplate.type == "Library" ? "StaticLibrary"
                                                  : "Executable");
        if (const auto *outputElement = FindChild(*templateElement, "Output"))
        {
          if (const auto kind = Attribute(*outputElement, "Kind"))
          {
            projectTemplate.outputKind = *kind;
          }
        }
        if (const auto *defaults = FindChild(*templateElement, "Defaults"))
        {
          ModelContext local{.defaults = projectTemplate.defaults};
          auto merged = MergeDefaultsFromElement(*defaults, local);
          if (!merged)
          {
            return NGIN::Utilities::Unexpected<KernelError>(merged.Error());
          }
          projectTemplate.defaults = local.defaults;
        }
        auto added = AddProjectTemplate(context, std::move(projectTemplate));
        if (!added)
        {
          return NGIN::Utilities::Unexpected<KernelError>(added.Error());
        }
      }
      return {};
    }

    [[nodiscard]] auto ParseLaunchDefinition(const XmlElement &element,
                                             const std::string_view context)
        -> CoreResult<LaunchDefinition>
    {
      auto executable = RequireAttribute(element, "Executable", context);
      if (!executable)
      {
        return NGIN::Utilities::Unexpected<KernelError>(executable.Error());
      }
      auto workingDirectory =
          OptionalAttribute(element, "WorkingDirectory", context, ".");
      if (!workingDirectory)
      {
        return NGIN::Utilities::Unexpected<KernelError>(workingDirectory.Error());
      }
      return LaunchDefinition{.executable = executable.Value(),
                              .workingDirectory = workingDirectory.Value()};
    }

    [[nodiscard]] auto ParseProfileTemplateDefinition(
        const XmlElement &element, const std::string_view context)
        -> CoreResult<ProfileTemplateDefinition>
    {
      ProfileTemplateDefinition profileTemplate{};
      auto name = RequireAttribute(element, "Name", context);
      if (!name)
      {
        return NGIN::Utilities::Unexpected<KernelError>(name.Error());
      }
      profileTemplate.name = name.Value();
      if (const auto extends = Attribute(element, "Extends");
          extends.has_value() && !extends->empty())
      {
        profileTemplate.extends = *extends;
      }
      profileTemplate.buildType = Attribute(element, "BuildType");
      profileTemplate.platform = Attribute(element, "Platform");
      profileTemplate.operatingSystem = Attribute(element, "OperatingSystem");
      profileTemplate.architecture = Attribute(element, "Architecture");
      profileTemplate.environment = Attribute(element, "Environment");
      if (const auto reflection = Attribute(element, "EnableReflection"))
      {
        auto parsed = ParseBoolValue(*reflection,
                                     std::string(context) + ".EnableReflection");
        if (!parsed)
        {
          return NGIN::Utilities::Unexpected<KernelError>(parsed.Error());
        }
        profileTemplate.enableReflection = parsed.Value();
      }
      if (const auto *launch = FindChild(element, "Launch"))
      {
        auto parsedLaunch =
            ParseLaunchDefinition(*launch, std::string(context) + ".Launch");
        if (!parsedLaunch)
        {
          return NGIN::Utilities::Unexpected<KernelError>(parsedLaunch.Error());
        }
        profileTemplate.launch = parsedLaunch.Value();
      }
      if (const auto *references = FindChild(element, "References"))
      {
        for (const auto *projectElement : ChildElements(*references, "Project"))
        {
          auto reference = ParseProjectReference(
              *projectElement, std::string(context) + ".References.Project");
          if (!reference)
          {
            return NGIN::Utilities::Unexpected<KernelError>(reference.Error());
          }
          profileTemplate.projectRefs.push_back(reference.Value());
        }
        for (const auto *packageElement : ChildElements(*references, "Package"))
        {
          auto package = ParsePackageReference(
              *packageElement, std::string(context) + ".References.Package");
          if (!package)
          {
            return NGIN::Utilities::Unexpected<KernelError>(package.Error());
          }
          profileTemplate.packageRefs.push_back(package.Value());
        }
      }
      if (auto inputs =
              ParseInputDeclarations(element, context, profileTemplate.inputs);
          !inputs)
      {
        return NGIN::Utilities::Unexpected<KernelError>(inputs.Error());
      }
      auto runtime =
          ParseRuntimeDefinition(FindChild(element, "Runtime"),
                                 std::string(context) + ".Runtime");
      if (!runtime)
      {
        return NGIN::Utilities::Unexpected<KernelError>(runtime.Error());
      }
      profileTemplate.runtime = std::move(runtime.Value());
      return profileTemplate;
    }

    auto AddProfileTemplate(ModelContext &context,
                            ProfileTemplateDefinition profileTemplate)
        -> CoreResult<void>
    {
      if (context.profileTemplates.contains(profileTemplate.name))
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            "duplicate profile template", profileTemplate.name,
            KernelErrorCode::AlreadyExists));
      }
      context.profileTemplates[profileTemplate.name] = std::move(profileTemplate);
      return {};
    }

    auto ParseProfileTemplatesInto(const XmlElement &root, ModelContext &context)
        -> CoreResult<void>
    {
      const auto *templates = FindChild(root, "ProfileTemplates");
      if (templates == nullptr)
      {
        return {};
      }
      std::size_t index = 0;
      for (const auto *templateElement :
           ChildElements(*templates, "ProfileTemplate"))
      {
        auto profileTemplate = ParseProfileTemplateDefinition(
            *templateElement,
            "model.ProfileTemplates[" + std::to_string(index++) + "]");
        if (!profileTemplate)
        {
          return NGIN::Utilities::Unexpected<KernelError>(profileTemplate.Error());
        }
        auto added = AddProfileTemplate(context, std::move(profileTemplate.Value()));
        if (!added)
        {
          return NGIN::Utilities::Unexpected<KernelError>(added.Error());
        }
      }
      return {};
    }

    [[nodiscard]] auto BuiltinModelContext() -> ModelContext
    {
      ModelContext context{};
      (void)AddPlatform(context, {"linux-x64", "linux", "x64", true});
      (void)AddPlatform(context, {"windows-x64", "windows", "x64", true});
      (void)AddPlatform(context, {"macos-x64", "macos", "x64", true});
      (void)AddPlatform(context, {"macos-arm64", "macos", "arm64", true});
      (void)AddProjectTemplate(context, {"Application", "Application",
                                         "Executable", true, context.defaults});
      (void)AddProjectTemplate(context, {"Library", "Library",
                                         "StaticLibrary", true,
                                         context.defaults});
      (void)AddProjectTemplate(context,
                               {"Tool", "Tool", "Executable", true,
                                context.defaults});
      return context;
    }

    auto ParseModelContributionsInto(const XmlElement &root, ModelContext &context,
                                     const bool includeDefaults) -> CoreResult<void>
    {
      if (includeDefaults)
      {
        if (const auto *defaults = FindChild(root, "Defaults"))
        {
          auto merged = MergeDefaultsFromElement(*defaults, context);
          if (!merged)
          {
            return NGIN::Utilities::Unexpected<KernelError>(merged.Error());
          }
        }
      }
      if (auto platforms = ParsePlatformsInto(root, context); !platforms)
      {
        return NGIN::Utilities::Unexpected<KernelError>(platforms.Error());
      }
      if (auto projectTemplates = ParseProjectTemplatesInto(root, context);
          !projectTemplates)
      {
        return NGIN::Utilities::Unexpected<KernelError>(projectTemplates.Error());
      }
      if (auto profileTemplates = ParseProfileTemplatesInto(root, context);
          !profileTemplates)
      {
        return NGIN::Utilities::Unexpected<KernelError>(profileTemplates.Error());
      }
      return {};
    }

    auto LoadIncludesInto(IoFileSystem &fileSystem, const XmlElement &root,
                          const IoPath &declaringDirectory, ModelContext &context,
                          std::vector<std::string> &includeStack)
        -> CoreResult<void>;

    auto LoadModelFileInto(IoFileSystem &fileSystem, const IoPath &modelPath,
                           ModelContext &context,
                           std::vector<std::string> &includeStack)
        -> CoreResult<void>
    {
      auto canonical = WeaklyCanonicalPath(fileSystem, modelPath);
      if (!canonical)
      {
        return NGIN::Utilities::Unexpected<KernelError>(canonical.Error());
      }
      const auto key = ToString(canonical.Value());
      if (std::find(includeStack.begin(), includeStack.end(), key) !=
          includeStack.end())
      {
        std::ostringstream chain{};
        for (const auto &entry : includeStack)
        {
          chain << entry << " -> ";
        }
        chain << key;
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            "model include cycle: " + chain.str(), key,
            KernelErrorCode::SchemaValidationFailure));
      }

      auto text = ReadTextFile(fileSystem, canonical.Value(), "model");
      if (!text)
      {
        return NGIN::Utilities::Unexpected<KernelError>(text.Error());
      }
      auto loaded = LoadXmlDocument(text.Value(), "model");
      if (!loaded)
      {
        return NGIN::Utilities::Unexpected<KernelError>(loaded.Error());
      }
      const auto *root = loaded.Value().document.Root();
      if (root == nullptr || root->name != "Model")
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            "model manifest root element must be <Model>", key,
            KernelErrorCode::SchemaValidationFailure));
      }
      auto schemaVersion = OptionalAttribute(*root, "SchemaVersion", "model", "3");
      if (!schemaVersion)
      {
        return NGIN::Utilities::Unexpected<KernelError>(schemaVersion.Error());
      }
      if (schemaVersion.Value() != "3")
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            "unsupported model schema version", schemaVersion.Value(),
            KernelErrorCode::SchemaValidationFailure));
      }

      includeStack.push_back(key);
      auto includes = LoadIncludesInto(fileSystem, *root, canonical.Value().Parent(),
                                       context, includeStack);
      if (!includes)
      {
        return NGIN::Utilities::Unexpected<KernelError>(includes.Error());
      }
      auto contributions = ParseModelContributionsInto(*root, context, true);
      includeStack.pop_back();
      if (!contributions)
      {
        return NGIN::Utilities::Unexpected<KernelError>(contributions.Error());
      }
      return {};
    }

    auto LoadIncludesInto(IoFileSystem &fileSystem, const XmlElement &root,
                          const IoPath &declaringDirectory, ModelContext &context,
                          std::vector<std::string> &includeStack)
        -> CoreResult<void>
    {
      const auto *includes = FindChild(root, "Includes");
      if (includes == nullptr)
      {
        return {};
      }
      for (const auto *includeElement : ChildElements(*includes, "Include"))
      {
        auto includePath =
            RequireAttribute(*includeElement, "Path", "model.Includes.Include");
        if (!includePath)
        {
          return NGIN::Utilities::Unexpected<KernelError>(includePath.Error());
        }
        auto loaded = LoadModelFileInto(
            fileSystem, ResolveChildPath(declaringDirectory, includePath.Value()),
            context, includeStack);
        if (!loaded)
        {
          return NGIN::Utilities::Unexpected<KernelError>(loaded.Error());
        }
      }
      return {};
    }

    auto LoadWorkspaceModelInto(IoFileSystem &fileSystem,
                                const IoPath &workspacePath,
                                ModelContext &context) -> CoreResult<void>
    {
      auto text = ReadTextFile(fileSystem, workspacePath, "workspace");
      if (!text)
      {
        return NGIN::Utilities::Unexpected<KernelError>(text.Error());
      }
      auto loaded = LoadXmlDocument(text.Value(), "workspace");
      if (!loaded)
      {
        return NGIN::Utilities::Unexpected<KernelError>(loaded.Error());
      }
      const auto *root = loaded.Value().document.Root();
      if (root == nullptr || root->name != "Workspace")
      {
        return NGIN::Utilities::Unexpected<KernelError>(
            MakeBuilderError("workspace manifest root element must be <Workspace>"));
      }
      std::vector<std::string> includeStack{};
      if (auto includes = LoadIncludesInto(fileSystem, *root, workspacePath.Parent(),
                                           context, includeStack);
          !includes)
      {
        return NGIN::Utilities::Unexpected<KernelError>(includes.Error());
      }
      return ParseModelContributionsInto(*root, context, true);
    }

    [[nodiscard]] auto FindNearestWorkspaceManifest(const IoPath &projectPath)
        -> std::optional<IoPath>
    {
      try
      {
        std::filesystem::path current(ToString(projectPath.Parent()));
        while (!current.empty())
        {
          std::vector<std::filesystem::path> candidates{};
          for (const auto &entry : std::filesystem::directory_iterator(current))
          {
            if (entry.is_regular_file() && entry.path().extension() == ".ngin")
            {
              candidates.push_back(entry.path());
            }
          }
          if (!candidates.empty())
          {
            std::sort(candidates.begin(), candidates.end());
            return IoPath(candidates.front().string());
          }
          const auto parent = current.parent_path();
          if (parent == current)
          {
            break;
          }
          current = parent;
        }
      }
      catch (...)
      {
      }
      return std::nullopt;
    }

    auto ResolveProjectModelContext(IoFileSystem *fileSystem,
                                    const IoPath *projectPath,
                                    const XmlElement &projectRoot)
        -> CoreResult<ModelContext>
    {
      auto context = BuiltinModelContext();
      if (fileSystem != nullptr && projectPath != nullptr)
      {
        if (const auto workspacePath = FindNearestWorkspaceManifest(*projectPath);
            workspacePath.has_value())
        {
          if (auto workspace =
                  LoadWorkspaceModelInto(*fileSystem, *workspacePath, context);
              !workspace)
          {
            return NGIN::Utilities::Unexpected<KernelError>(workspace.Error());
          }
        }
        std::vector<std::string> includeStack{};
        if (auto includes = LoadIncludesInto(*fileSystem, projectRoot,
                                             projectPath->Parent(), context,
                                             includeStack);
            !includes)
        {
          return NGIN::Utilities::Unexpected<KernelError>(includes.Error());
        }
      }
      if (auto projectContributions =
              ParseModelContributionsInto(projectRoot, context, false);
          !projectContributions)
      {
        return NGIN::Utilities::Unexpected<KernelError>(
            projectContributions.Error());
      }
      if (const auto *defaults = FindChild(projectRoot, "Defaults"))
      {
        auto merged = MergeDefaultsFromElement(*defaults, context);
        if (!merged)
        {
          return NGIN::Utilities::Unexpected<KernelError>(merged.Error());
        }
      }
      return context;
    }

    auto ApplyProjectDefaults(ProjectManifest &manifest,
                              ProjectBuildDescriptor &build,
                              const ProjectDefaults &defaults) -> void
    {
      if (defaults.backend)
      {
        build.backend = *defaults.backend;
      }
      if (defaults.buildMode)
      {
        build.mode = *defaults.buildMode;
      }
      if (defaults.language)
      {
        build.language = *defaults.language;
      }
      if (defaults.languageStandard)
      {
        build.languageStandard = *defaults.languageStandard;
      }
      (void)manifest;
    }

    auto ApplyProfileDefaults(ProfileDefinition &profile,
                              const ProjectDefaults &defaults) -> void
    {
      if (defaults.buildType)
      {
        profile.buildType = *defaults.buildType;
      }
      if (defaults.platform)
      {
        profile.platform = *defaults.platform;
      }
      if (defaults.environment)
      {
        profile.environmentName = *defaults.environment;
      }
    }

    auto ApplyPlatformDefinition(ProfileDefinition &profile,
                                 const ModelContext &context) -> void
    {
      const auto platform = context.platforms.find(profile.platform);
      if (platform == context.platforms.end())
      {
        return;
      }
      profile.operatingSystem = platform->second.operatingSystem;
      profile.architecture = platform->second.architecture;
    }

    auto ApplyProfileTemplate(ProfileDefinition &profile,
                              const ProfileTemplateDefinition &profileTemplate,
                              const ModelContext &context,
                              std::vector<std::string> &templateStack)
        -> CoreResult<void>
    {
      if (std::find(templateStack.begin(), templateStack.end(),
                    profileTemplate.name) != templateStack.end())
      {
        std::ostringstream chain{};
        for (const auto &entry : templateStack)
        {
          chain << entry << " -> ";
        }
        chain << profileTemplate.name;
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            "profile template cycle: " + chain.str(), profileTemplate.name,
            KernelErrorCode::SchemaValidationFailure));
      }
      templateStack.push_back(profileTemplate.name);
      if (profileTemplate.extends)
      {
        const auto parent = context.profileTemplates.find(*profileTemplate.extends);
        if (parent == context.profileTemplates.end())
        {
          return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
              "profile template extends unknown template",
              *profileTemplate.extends,
              KernelErrorCode::SchemaValidationFailure));
        }
        auto applied =
            ApplyProfileTemplate(profile, parent->second, context, templateStack);
        if (!applied)
        {
          return NGIN::Utilities::Unexpected<KernelError>(applied.Error());
        }
      }
      if (profileTemplate.buildType)
      {
        profile.buildType = *profileTemplate.buildType;
      }
      if (profileTemplate.platform)
      {
        profile.platform = *profileTemplate.platform;
        ApplyPlatformDefinition(profile, context);
      }
      if (profileTemplate.operatingSystem)
      {
        profile.operatingSystem = *profileTemplate.operatingSystem;
      }
      if (profileTemplate.architecture)
      {
        profile.architecture = *profileTemplate.architecture;
      }
      if (profileTemplate.environment)
      {
        profile.environmentName = *profileTemplate.environment;
      }
      if (profileTemplate.enableReflection)
      {
        profile.enableReflection = *profileTemplate.enableReflection;
      }
      if (profileTemplate.launch)
      {
        profile.launch = *profileTemplate.launch;
      }
      profile.projectRefs.insert(profile.projectRefs.end(),
                                 profileTemplate.projectRefs.begin(),
                                 profileTemplate.projectRefs.end());
      profile.packageRefs.insert(profile.packageRefs.end(),
                                 profileTemplate.packageRefs.begin(),
                                 profileTemplate.packageRefs.end());
      profile.inputs.insert(profile.inputs.end(),
                            profileTemplate.inputs.begin(),
                            profileTemplate.inputs.end());
      MergeRuntime(profile.runtime, profileTemplate.runtime);
      templateStack.pop_back();
      return {};
    }

    [[nodiscard]] auto ParseEnvironmentDefinition(const XmlElement &element,
                                                  const std::string_view context)
        -> CoreResult<EnvironmentDefinition>
    {
      EnvironmentDefinition environment{};

      auto name = RequireAttribute(element, "Name", context);
      if (!name)
      {
        return NGIN::Utilities::Unexpected<KernelError>(name.Error());
      }
      environment.name = name.Value();

      if (const auto *referencesElement = FindChild(element, "References"))
      {
        std::size_t projectIndex = 0;
        for (const auto *projectElement :
             ChildElements(*referencesElement, "Project"))
        {
          auto project = ParseProjectReference(
              *projectElement, std::string(context) + ".References.Project[" +
                                   std::to_string(projectIndex++) + "]");
          if (!project)
          {
            return NGIN::Utilities::Unexpected<KernelError>(project.Error());
          }
          environment.projectRefs.push_back(project.Value());
        }
        for (const auto *packageElement :
             ChildElements(*referencesElement, "Package"))
        {
          auto package = ParsePackageReference(
              *packageElement, std::string(context) + ".References.Package");
          if (!package)
          {
            return NGIN::Utilities::Unexpected<KernelError>(package.Error());
          }
          environment.packageRefs.push_back(package.Value());
        }
      }

      if (auto inputs = ParseInputDeclarations(element, context, environment.inputs);
          !inputs)
      {
        return NGIN::Utilities::Unexpected<KernelError>(inputs.Error());
      }

      if (const auto *variables = FindChild(element, "Variables"))
      {
        for (const auto *variableElement : ChildElements(*variables, "Variable"))
        {
          auto variableName =
              RequireAttribute(*variableElement, "Name",
                               std::string(context) + ".Variables.Variable");
          if (!variableName)
          {
            return NGIN::Utilities::Unexpected<KernelError>(variableName.Error());
          }

          auto value =
              RequireAttribute(*variableElement, "Value",
                               std::string(context) + ".Variables.Variable");
          if (!value)
          {
            return NGIN::Utilities::Unexpected<KernelError>(value.Error());
          }

          environment.variables.push_back(EnvironmentVariable{
              .name = variableName.Value(),
              .value = value.Value(),
          });
        }
      }

      if (const auto *features = FindChild(element, "Features"))
      {
        for (const auto *featureElement : ChildElements(*features, "Feature"))
        {
          auto featureName =
              RequireAttribute(*featureElement, "Name",
                               std::string(context) + ".Features.Feature");
          if (!featureName)
          {
            return NGIN::Utilities::Unexpected<KernelError>(featureName.Error());
          }

          auto enabled = OptionalBoolAttribute(
              *featureElement, "Enabled",
              std::string(context) + ".Features.Feature.Enabled", false);
          if (!enabled)
          {
            return NGIN::Utilities::Unexpected<KernelError>(enabled.Error());
          }

          environment.features.push_back(FeatureFlag{
              .name = featureName.Value(),
              .enabled = enabled.Value(),
          });
        }
      }

      auto runtime =
          ParseRuntimeDefinition(FindChild(element, "Runtime"),
                                 std::string(context) + ".Runtime");
      if (!runtime)
      {
        return NGIN::Utilities::Unexpected<KernelError>(runtime.Error());
      }
      environment.runtime = std::move(runtime.Value());

      return environment;
    }

    [[nodiscard]] auto ParseProfileDefinition(const XmlElement &element,
                                                    const std::string_view context,
                                                    const ProfileDefinition *base = nullptr)
        -> CoreResult<ProfileDefinition>
    {
      ProfileDefinition profile = base != nullptr
                                                ? *base
                                                : ProfileDefinition{};

      auto name = RequireAttribute(element, "Name", context);
      if (!name)
      {
        return NGIN::Utilities::Unexpected<KernelError>(name.Error());
      }
      profile.name = name.Value();

      auto buildType = OptionalAttribute(
          element, "BuildType", context,
          base != nullptr ? profile.buildType : "Debug");
      if (!buildType)
      {
        return NGIN::Utilities::Unexpected<KernelError>(buildType.Error());
      }
      profile.buildType = buildType.Value();

      if (const auto platform = Attribute(element, "Platform");
          platform.has_value() || base == nullptr)
      {
        profile.platform = platform.value_or("linux-x64");

        std::string defaultOperatingSystem = "linux";
        std::string defaultArchitecture = "x64";
        if (profile.platform == "windows-x64")
        {
          defaultOperatingSystem = "windows";
        }
        else if (profile.platform == "macos-x64")
        {
          defaultOperatingSystem = "macos";
        }
        else if (profile.platform == "macos-arm64")
        {
          defaultOperatingSystem = "macos";
          defaultArchitecture = "arm64";
        }
        profile.operatingSystem = defaultOperatingSystem;
        profile.architecture = defaultArchitecture;
      }

      auto operatingSystem =
          OptionalAttribute(element, "OperatingSystem", context, profile.operatingSystem);
      if (!operatingSystem)
      {
        return NGIN::Utilities::Unexpected<KernelError>(operatingSystem.Error());
      }
      profile.operatingSystem = operatingSystem.Value();
      if (!IsValidOperatingSystem(profile.operatingSystem))
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            "profile uses unknown operating system",
            profile.operatingSystem,
            KernelErrorCode::SchemaValidationFailure));
      }

      auto architecture =
          OptionalAttribute(element, "Architecture", context, profile.architecture);
      if (!architecture)
      {
        return NGIN::Utilities::Unexpected<KernelError>(architecture.Error());
      }
      profile.architecture = architecture.Value();
      if (!IsValidArchitecture(profile.architecture))
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            "profile uses unknown architecture", profile.architecture,
            KernelErrorCode::SchemaValidationFailure));
      }

      if (element.FindAttribute("EnableReflection") != nullptr || base == nullptr)
      {
        auto reflection =
            OptionalBoolAttribute(element, "EnableReflection",
                                  std::string(context) + ".EnableReflection",
                                  profile.enableReflection);
        if (!reflection)
        {
          return NGIN::Utilities::Unexpected<KernelError>(reflection.Error());
        }
        profile.enableReflection = reflection.Value();
      }

      auto environment = OptionalAttribute(element, "Environment", context,
                                           profile.environmentName);
      if (!environment)
      {
        return NGIN::Utilities::Unexpected<KernelError>(environment.Error());
      }
      profile.environmentName = environment.Value();
      if (profile.environmentName.empty())
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            std::string(context) + " must define Environment", {},
            KernelErrorCode::SchemaValidationFailure));
      }

      if (const auto *launchElement = FindChild(element, "Launch"))
      {
        auto executable = RequireAttribute(*launchElement, "Executable",
                                           std::string(context) + ".Launch");
        if (!executable)
        {
          return NGIN::Utilities::Unexpected<KernelError>(executable.Error());
        }
        auto workingDirectory =
            OptionalAttribute(*launchElement, "WorkingDirectory",
                              std::string(context) + ".Launch", ".");
        if (!workingDirectory)
        {
          return NGIN::Utilities::Unexpected<KernelError>(workingDirectory.Error());
        }
        profile.launch = LaunchDefinition{
            .executable = executable.Value(),
            .workingDirectory = workingDirectory.Value(),
        };
      }

      if (const auto *referencesElement = FindChild(element, "References"))
      {
        std::size_t projectIndex = 0;
        for (const auto *projectElement :
             ChildElements(*referencesElement, "Project"))
        {
          auto project = ParseProjectReference(
              *projectElement, std::string(context) + ".References.Project[" +
                                   std::to_string(projectIndex++) + "]");
          if (!project)
          {
            return NGIN::Utilities::Unexpected<KernelError>(project.Error());
          }
          profile.projectRefs.push_back(project.Value());
        }
        for (const auto *packageElement :
             ChildElements(*referencesElement, "Package"))
        {
          auto package = ParsePackageReference(
              *packageElement, std::string(context) + ".References.Package");
          if (!package)
          {
            return NGIN::Utilities::Unexpected<KernelError>(package.Error());
          }
          profile.packageRefs.push_back(package.Value());
        }
      }

      if (auto inputs = ParseInputDeclarations(element, context, profile.inputs);
          !inputs)
      {
        return NGIN::Utilities::Unexpected<KernelError>(inputs.Error());
      }

      if (FindChild(element, "EnableModules") != nullptr ||
          FindChild(element, "DisableModules") != nullptr ||
          FindChild(element, "Modules") != nullptr)
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            std::string(context) +
                " must place runtime module selections inside <Runtime>",
            {}, KernelErrorCode::SchemaValidationFailure));
      }

      if (const auto *runtimeElement = FindChild(element, "Runtime"))
      {
        auto runtime =
            ParseRuntimeDefinition(runtimeElement, std::string(context) + ".Runtime");
        if (!runtime)
        {
          return NGIN::Utilities::Unexpected<KernelError>(runtime.Error());
        }
        profile.runtime.modules.insert(profile.runtime.modules.end(),
                                             runtime.Value().modules.begin(),
                                             runtime.Value().modules.end());
        profile.runtime.enableModules.insert(
            profile.runtime.enableModules.end(),
            runtime.Value().enableModules.begin(),
            runtime.Value().enableModules.end());
        profile.runtime.disableModules.insert(
            profile.runtime.disableModules.end(),
            runtime.Value().disableModules.begin(),
            runtime.Value().disableModules.end());
      }

      return profile;
    }

    [[nodiscard]] auto ParseProjectManifestText(const std::string &text,
                                                IoFileSystem *fileSystem = nullptr,
                                                const IoPath *manifestPath = nullptr)
        -> CoreResult<ProjectManifest>
    {
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
          OptionalAttribute(*root, "SchemaVersion", "project", "3");
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
      if (manifest.schemaVersion != 3)
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

      auto modelContext =
          ResolveProjectModelContext(fileSystem, manifestPath, *root);
      if (!modelContext)
      {
        return NGIN::Utilities::Unexpected<KernelError>(modelContext.Error());
      }

      auto projectTemplateName =
          OptionalAttribute(*root, "Template", "project", "Application");
      if (!projectTemplateName)
      {
        return NGIN::Utilities::Unexpected<KernelError>(
            projectTemplateName.Error());
      }
      const auto projectTemplateIt =
          modelContext.Value().projectTemplates.find(projectTemplateName.Value());
      if (projectTemplateIt == modelContext.Value().projectTemplates.end())
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            "project uses unknown template", projectTemplateName.Value(),
            KernelErrorCode::SchemaValidationFailure));
      }
      const auto &projectTemplate = projectTemplateIt->second;

      auto type = OptionalAttribute(*root, "Type", "project", {});
      if (!type)
      {
        return NGIN::Utilities::Unexpected<KernelError>(type.Error());
      }
      if (!type.Value().empty())
      {
        manifest.type = type.Value();
      }
      else
      {
        manifest.type = projectTemplate.type;
      }

      auto defaultProfile =
          OptionalAttribute(*root, "DefaultProfile", "project", {});
      if (!defaultProfile)
      {
        return NGIN::Utilities::Unexpected<KernelError>(
            defaultProfile.Error());
      }
      manifest.defaultProfile = defaultProfile.Value().empty()
                                          ? "Runtime"
                                          : defaultProfile.Value();

      if (auto inputs = ParseInputDeclarations(*root, "project", manifest.inputs);
          !inputs)
      {
        return NGIN::Utilities::Unexpected<KernelError>(inputs.Error());
      }

      if (FindChild(*root, "Host") != nullptr)
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            "project manifest root <Host> is no longer supported", {},
            KernelErrorCode::SchemaValidationFailure));
      }

      const auto *outputElement = FindChild(*root, "Output");
      if (outputElement == nullptr)
      {
        manifest.output = OutputDefinition{
            .kind = projectTemplate.outputKind,
            .name = manifest.name,
            .target = manifest.name,
        };
      }
      else
      {
        auto output = ParseOutputDefinition(*outputElement, "project.Output");
        if (!output)
        {
          return NGIN::Utilities::Unexpected<KernelError>(output.Error());
        }
        manifest.output = output.Value();
      }

      const bool validPairing =
          ((manifest.type == "Application" || manifest.type == "Tool") &&
           manifest.output.kind == "Executable") ||
          (manifest.type == "Library" &&
           (manifest.output.kind == "StaticLibrary" ||
            manifest.output.kind == "SharedLibrary"));
      if (!validPairing)
      {
        return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
            "project type and output kind are incompatible",
            manifest.type + ":" + manifest.output.kind,
            KernelErrorCode::SchemaValidationFailure));
      }

      ProjectBuildDescriptor effectiveBuild{};
      ApplyProjectDefaults(manifest, effectiveBuild, projectTemplate.defaults);
      ApplyProjectDefaults(manifest, effectiveBuild, modelContext.Value().defaults);
      const auto *buildElement = FindChild(*root, "Build");
      auto parsedBuild =
          ParseProjectBuildDescriptor(buildElement, "project.Build");
      if (!parsedBuild)
      {
        return NGIN::Utilities::Unexpected<KernelError>(parsedBuild.Error());
      }
      if (buildElement != nullptr)
      {
        if (Attribute(*buildElement, "Backend"))
        {
          effectiveBuild.backend = parsedBuild.Value().backend;
        }
        if (Attribute(*buildElement, "Mode"))
        {
          effectiveBuild.mode = parsedBuild.Value().mode;
        }
        if (Attribute(*buildElement, "Language"))
        {
          effectiveBuild.language = parsedBuild.Value().language;
        }
        if (Attribute(*buildElement, "LanguageStandard"))
        {
          effectiveBuild.languageStandard = parsedBuild.Value().languageStandard;
        }
        effectiveBuild.sources = std::move(parsedBuild.Value().sources);
        effectiveBuild.includeDirectories =
            std::move(parsedBuild.Value().includeDirectories);
        effectiveBuild.compileDefinitions =
            std::move(parsedBuild.Value().compileDefinitions);
        effectiveBuild.compileOptions =
            std::move(parsedBuild.Value().compileOptions);
        effectiveBuild.linkOptions = std::move(parsedBuild.Value().linkOptions);
      }
      manifest.build = std::move(effectiveBuild);

      if (const auto *referencesElement = FindChild(*root, "References"))
      {
        std::size_t index = 0;
        for (const auto *projectRefElement :
             ChildElements(*referencesElement, "Project"))
        {
          auto reference = ParseProjectReference(*projectRefElement,
                                                 "project.References.Project[" +
                                                     std::to_string(index++) + "]");
          if (!reference)
          {
            return NGIN::Utilities::Unexpected<KernelError>(reference.Error());
          }
          manifest.projectRefs.push_back(reference.Value());
        }
        for (const auto *packageElement :
             ChildElements(*referencesElement, "Package"))
        {
          auto package =
              ParsePackageReference(*packageElement, "project.References.Package");
          if (!package)
          {
            return NGIN::Utilities::Unexpected<KernelError>(package.Error());
          }
          manifest.packageRefs.push_back(package.Value());
        }
      }

      if (const auto *environmentsElement = FindChild(*root, "Environments"))
      {
        std::set<std::string> environmentNames{};
        std::size_t environmentIndex = 0;
        for (const auto *environmentElement :
             ChildElements(*environmentsElement, "Environment"))
        {
          auto environment = ParseEnvironmentDefinition(
              *environmentElement,
              "project.Environments[" + std::to_string(environmentIndex++) + "]");
          if (!environment)
          {
            return NGIN::Utilities::Unexpected<KernelError>(environment.Error());
          }
          if (!environmentNames.insert(environment.Value().name).second)
          {
            return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
                "duplicate environment name", environment.Value().name,
                KernelErrorCode::AlreadyExists));
          }
          manifest.environments.push_back(environment.Value());
        }
      }

      auto runtime =
          ParseRuntimeDefinition(FindChild(*root, "Runtime"), "project.Runtime");
      if (!runtime)
      {
        return NGIN::Utilities::Unexpected<KernelError>(runtime.Error());
      }
      manifest.runtime = std::move(runtime.Value());

      std::optional<LaunchDefinition> rootLaunch{};
      if (const auto *launchElement = FindChild(*root, "Launch"))
      {
        if (manifest.type == "Library")
        {
          return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
              "library projects may not define root <Launch>", manifest.name,
              KernelErrorCode::SchemaValidationFailure));
        }
        auto parsedLaunch =
            ParseLaunchDefinition(*launchElement, "project.Launch");
        if (!parsedLaunch)
        {
          return NGIN::Utilities::Unexpected<KernelError>(parsedLaunch.Error());
        }
        rootLaunch = parsedLaunch.Value();
      }

      const auto *profilesElement = FindChild(*root, "Profiles");
      if (profilesElement == nullptr)
      {
        return NGIN::Utilities::Unexpected<KernelError>(
            MakeBuilderError("project must contain a <Profiles> element"));
      }

      std::unordered_map<std::string, std::size_t> profileIndexes{};
      std::size_t index = 0;
      for (const auto *configurationElement :
           ChildElements(*profilesElement, "Profile"))
      {
        auto profileName =
            RequireAttribute(*configurationElement, "Name",
                             "project.Profiles[" + std::to_string(index) + "]");
        if (!profileName)
        {
          return NGIN::Utilities::Unexpected<KernelError>(profileName.Error());
        }
        if (profileIndexes.contains(profileName.Value()))
        {
          return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
              "duplicate profile name", profileName.Value(),
              KernelErrorCode::AlreadyExists));
        }
        const ProfileDefinition *baseProfile = nullptr;
        if (const auto extends = Attribute(*configurationElement, "Extends");
            extends.has_value() && !extends->empty())
        {
          const auto parent = profileIndexes.find(*extends);
          if (parent == profileIndexes.end())
          {
            return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
                "profile extends unknown or later profile", *extends,
                KernelErrorCode::SchemaValidationFailure));
          }
          baseProfile = &manifest.profiles[parent->second];
        }
        ProfileDefinition effectiveBase =
            baseProfile != nullptr ? *baseProfile : ProfileDefinition{};
        if (baseProfile == nullptr)
        {
          ApplyProfileDefaults(effectiveBase, modelContext.Value().defaults);
          ApplyPlatformDefinition(effectiveBase, modelContext.Value());
        }
        if (const auto profileTemplateName =
                Attribute(*configurationElement, "Template");
            profileTemplateName.has_value() && !profileTemplateName->empty())
        {
          const auto profileTemplateIt =
              modelContext.Value().profileTemplates.find(*profileTemplateName);
          if (profileTemplateIt == modelContext.Value().profileTemplates.end())
          {
            return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
                "profile uses unknown template", *profileTemplateName,
                KernelErrorCode::SchemaValidationFailure));
          }
          std::vector<std::string> templateStack{};
          auto applied = ApplyProfileTemplate(effectiveBase,
                                              profileTemplateIt->second,
                                              modelContext.Value(),
                                              templateStack);
          if (!applied)
          {
            return NGIN::Utilities::Unexpected<KernelError>(applied.Error());
          }
        }
        auto profile = ParseProfileDefinition(
            *configurationElement,
            "project.Profiles[" + std::to_string(index++) + "]",
            &effectiveBase);
        if (!profile)
        {
          return NGIN::Utilities::Unexpected<KernelError>(profile.Error());
        }
        if (!profile.Value().launch.has_value() && rootLaunch.has_value())
        {
          profile.Value().launch = rootLaunch;
        }
        if (profile.Value().launch.has_value() &&
            profile.Value().launch->executable == "$(OutputName)")
        {
          profile.Value().launch->executable = manifest.output.name;
        }
        if (!profile.Value().environmentName.empty())
        {
          const auto environmentIt = std::find_if(
              manifest.environments.begin(), manifest.environments.end(),
              [&](const EnvironmentDefinition &environment)
              {
                return environment.name == profile.Value().environmentName;
              });
          if (environmentIt == manifest.environments.end())
          {
            return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
                "profile selects unknown environment",
                profile.Value().environmentName,
                KernelErrorCode::SchemaValidationFailure));
          }
        }
        if (manifest.type == "Library" && profile.Value().launch.has_value())
        {
          return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
              "library profiles may not define <Launch>",
              profile.Value().name,
              KernelErrorCode::SchemaValidationFailure));
        }
        profileIndexes.emplace(profile.Value().name,
                                     manifest.profiles.size());
        manifest.profiles.push_back(profile.Value());
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
          auto result = collectReference(reference);
          if (!result)
          {
            return result;
          }
        }
      }
      for (const auto &reference : profile.Value().projectRefs)
      {
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
          OptionalAttribute(*root, "SchemaVersion", "package", "3");
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
      if (manifest.schemaVersion != 3)
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

      auto version = RequireAttribute(*root, "Version", "package");
      if (!version)
      {
        return NGIN::Utilities::Unexpected<KernelError>(version.Error());
      }
      manifest.version = version.Value();

      auto platformRange =
          RequireAttribute(*root, "CompatiblePlatformRange", "package");
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

      if (const auto *dependenciesElement = FindChild(*root, "Dependencies"))
      {
        if (FindChild(*dependenciesElement, "Dependency") != nullptr)
        {
          return NGIN::Utilities::Unexpected<KernelError>(
              MakeBuilderError("package dependencies use legacy <Dependency>; use <PackageRef>",
                               "package.Dependencies", KernelErrorCode::InvalidArgument));
        }
        for (const auto *dependencyElement :
             ChildElements(*dependenciesElement, "PackageRef"))
        {
          auto reference =
              ParsePackageReference(*dependencyElement, "package.Dependencies");
          if (!reference)
          {
            return NGIN::Utilities::Unexpected<KernelError>(reference.Error());
          }
          manifest.dependencies.push_back(reference.Value());
        }
      }

      if (const auto *bootstrapElement = FindChild(*root, "Bootstrap"))
      {
        auto modeText =
            RequireAttribute(*bootstrapElement, "Mode", "package.Bootstrap");
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
            RequireAttribute(*bootstrapElement, "EntryPoint", "package.Bootstrap");
        if (!entryPoint)
        {
          return NGIN::Utilities::Unexpected<KernelError>(entryPoint.Error());
        }

        auto autoApply = OptionalBoolAttribute(
            *bootstrapElement, "AutoApply", "package.Bootstrap.AutoApply", false);
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

      if (auto inputs = ParseInputDeclarations(*root, "package", manifest.inputs);
          !inputs)
      {
        return NGIN::Utilities::Unexpected<KernelError>(inputs.Error());
      }

      std::set<std::string> moduleNames{};
      if (const auto *modulesElement = FindChild(*root, "Modules"))
      {
        for (const auto *moduleElement :
             ChildElements(*modulesElement, "Module"))
        {
          auto module = ParsePackageModuleDescriptor(
              *moduleElement, "package.Modules.Module");
          if (!module)
          {
            return NGIN::Utilities::Unexpected<KernelError>(module.Error());
          }

          if (!moduleNames.insert(module.Value().name).second)
          {
            return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
                "duplicate package module declaration", module.Value().name,
                KernelErrorCode::AlreadyExists));
          }

          manifest.modules.push_back(module.Value());
        }
      }

      if (const auto *pluginsElement = FindChild(*root, "Plugins"))
      {
        std::set<std::string> pluginNames{};
        for (const auto *pluginElement : ChildElements(*pluginsElement, "Plugin"))
        {
          auto plugin =
              ParsePackagePluginManifest(*pluginElement, "package.Plugins.Plugin");
          if (!plugin)
          {
            return NGIN::Utilities::Unexpected<KernelError>(plugin.Error());
          }

          if (!pluginNames.insert(plugin.Value().name).second)
          {
            return NGIN::Utilities::Unexpected<KernelError>(MakeBuilderError(
                "duplicate package plugin declaration", plugin.Value().name,
                KernelErrorCode::AlreadyExists));
          }

          manifest.plugins.push_back(plugin.Value());
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
          MergePackageReferences(bootstrapPackages, unit.manifest.packageRefs);
          if (unit.environment.has_value())
          {
            MergePackageReferences(bootstrapPackages, unit.environment->packageRefs);
          }
          MergePackageReferences(bootstrapPackages, unit.profile.packageRefs);
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
        for (const auto &unit : projectUnits)
        {
          MergePackageReferences(packages, unit.manifest.packageRefs);
          if (unit.environment.has_value())
          {
            MergePackageReferences(packages, unit.environment->packageRefs);
          }
          MergePackageReferences(packages, unit.profile.packageRefs);
        }
        MergePackageReferences(packages, m_packageReferences);

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
                                            const ProfileDefinition &profile)
        {
          return (input.profile.empty() || input.profile == profile.name) &&
                 (input.platform.empty() || input.platform == profile.platform) &&
                 (input.operatingSystem.empty() ||
                  input.operatingSystem == profile.operatingSystem) &&
                 (input.architecture.empty() ||
                  input.architecture == profile.architecture) &&
                 (input.buildType.empty() || input.buildType == profile.buildType) &&
                 (input.environment.empty() ||
                  input.environment == profile.environmentName);
        };
        const auto appendConfigInputs =
            [&](const std::vector<InputDeclaration> &inputs,
                const ProfileDefinition &profile,
                const IoPath &directory)
        {
          for (const auto &input : inputs)
          {
            if (input.kind != "Config" ||
                !inputMatchesProfile(input, profile))
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
          appendConfigInputs(unit.manifest.inputs, unit.profile, unit.directory);
          if (unit.environment.has_value())
          {
            appendConfigInputs(unit.environment->inputs, unit.profile, unit.directory);
          }
          appendConfigInputs(unit.profile.inputs, unit.profile, unit.directory);
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
            appendConfigInputs(manifestIt->second.inputs, *selectedProfile, directory);
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
        hostConfig.enableDynamicPlugins = false;
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
