#include <NGIN/Core/Application.hpp>

#include <NGIN/Serialization/XML/XmlParser.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
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

        struct PendingServiceRegistration
        {
            std::string                   key {};
            std::optional<NGIN::Utilities::Any<>> instance {};
            std::optional<ServiceFactory> factory {};
            ServiceLifetime               lifetime {ServiceLifetime::Singleton};
            ServiceMetadata               metadata {};
        };

        struct PackageBootstrapRequest
        {
            std::string               packageName {};
            std::optional<std::string> entryPoint {};
        };

        struct BootstrapCandidate
        {
            PackageReference       reference {};
            const PackageManifest* manifest {nullptr};
            std::string            entryPoint {};
            bool                   explicitRequest {false};
            std::size_t            orderIndex {0};
        };

        struct LoadedXmlDocument
        {
            std::string text {};
            XmlDocument document {0};
        };

        [[nodiscard]] auto MakeBuilderError(
            const std::string& message,
            std::string subject = {},
            const KernelErrorCode code = KernelErrorCode::InvalidArgument) -> KernelError
        {
            return MakeKernelError(code, "ApplicationBuilder", std::move(subject), message);
        }

        [[nodiscard]] auto ReadTextFile(const std::filesystem::path& path, const std::string_view kind) -> CoreResult<std::string>
        {
            std::ifstream input(path);
            if (!input.good())
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeBuilderError(
                        "failed to open " + std::string(kind) + " file",
                        path.string(),
                        KernelErrorCode::NotFound));
            }

            std::string text {
                std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>()};
            return text;
        }

        [[nodiscard]] auto ToString(const NGIN::Serialization::ParseError& error) -> std::string
        {
            return std::string(error.message.Data(), error.message.Size());
        }

        [[nodiscard]] auto LoadXmlDocument(
            const std::string& text,
            const std::string_view kind) -> CoreResult<LoadedXmlDocument>
        {
            LoadedXmlDocument loaded {};
            loaded.text = text;

            XmlParseOptions options {};
            options.decodeEntities = true;
            options.arenaBytes = std::max<NGIN::UIntSize>(
                16384,
                static_cast<NGIN::UIntSize>(loaded.text.size() * 8 + 4096));

            auto parsed = XmlParser::Parse(loaded.text, options);
            if (!parsed)
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeBuilderError("failed to parse " + std::string(kind) + " manifest: " + ToString(parsed.ErrorUnsafe())));
            }

            loaded.document = std::move(parsed.ValueUnsafe());
            return loaded;
        }

        [[nodiscard]] auto ChildElements(const XmlElement& node, const std::string_view name = {}) -> std::vector<const XmlElement*>
        {
            std::vector<const XmlElement*> out {};
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

        [[nodiscard]] auto FindChild(const XmlElement& node, const std::string_view name) -> const XmlElement*
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

        [[nodiscard]] auto Attribute(const XmlElement& node, const std::string_view key) -> std::optional<std::string>
        {
            const auto* attribute = node.FindAttribute(key);
            if (attribute == nullptr)
            {
                return std::nullopt;
            }
            return std::string(attribute->value);
        }

        [[nodiscard]] auto RequireAttribute(
            const XmlElement& element,
            const std::string_view key,
            const std::string_view context) -> CoreResult<std::string>
        {
            const auto value = Attribute(element, key);
            if (!value.has_value())
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeBuilderError(std::string(context) + " missing required attribute '" + std::string(key) + "'"));
            }
            return value.value();
        }

        [[nodiscard]] auto OptionalAttribute(
            const XmlElement& element,
            const std::string_view key,
            const std::string_view context,
            const std::string& defaultValue = {}) -> CoreResult<std::string>
        {
            (void)context;
            const auto value = Attribute(element, key);
            if (!value.has_value())
            {
                return defaultValue;
            }
            return value.value();
        }

        [[nodiscard]] auto ParseBoolValue(
            const std::string& value,
            const std::string_view context) -> CoreResult<bool>
        {
            if (value == "true" || value == "1" || value == "yes")
            {
                return true;
            }
            if (value == "false" || value == "0" || value == "no")
            {
                return false;
            }
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeBuilderError(std::string(context) + " must be a boolean-compatible value"));
        }

        [[nodiscard]] auto OptionalBoolAttribute(
            const XmlElement& element,
            const std::string_view key,
            const std::string_view context,
            const bool defaultValue = false) -> CoreResult<bool>
        {
            const auto value = Attribute(element, key);
            if (!value.has_value())
            {
                return defaultValue;
            }
            return ParseBoolValue(value.value(), context);
        }

        [[nodiscard]] auto ParseUInt32Value(
            const std::string& value,
            const std::string_view context) -> CoreResult<NGIN::UInt32>
        {
            try
            {
                const auto parsed = static_cast<unsigned long>(std::stoul(value));
                return static_cast<NGIN::UInt32>(parsed);
            }
            catch (...)
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeBuilderError(std::string(context) + " must be an unsigned integer"));
            }
        }

        [[nodiscard]] auto ParseHostProfile(const std::string_view text) -> CoreResult<HostProfile>
        {
            if (text == "ConsoleApp") return HostProfile::ConsoleApp;
            if (text == "GuiApp") return HostProfile::GuiApp;
            if (text == "Game") return HostProfile::Game;
            if (text == "Editor") return HostProfile::Editor;
            if (text == "Service") return HostProfile::Service;
            if (text == "TestHost") return HostProfile::TestHost;
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeBuilderError("unknown host profile", std::string(text)));
        }

        [[nodiscard]] auto ParsePackageBootstrapMode(const std::string_view text) -> CoreResult<PackageBootstrapMode>
        {
            if (text == "BuilderHookV1") return PackageBootstrapMode::BuilderHookV1;
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeBuilderError(
                    "unknown package bootstrap mode",
                    std::string(text),
                    KernelErrorCode::SchemaValidationFailure));
        }

        [[nodiscard]] auto ParseModuleFamilyText(const std::string_view text) -> CoreResult<ModuleFamily>
        {
            if (text == "Base") return ModuleFamily::Base;
            if (text == "Reflection") return ModuleFamily::Reflection;
            if (text == "Core") return ModuleFamily::Core;
            if (text == "Platform") return ModuleFamily::Platform;
            if (text == "Editor") return ModuleFamily::Editor;
            if (text == "Domain") return ModuleFamily::Domain;
            if (text == "App") return ModuleFamily::App;
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeBuilderError("unknown module family", std::string(text), KernelErrorCode::SchemaValidationFailure));
        }

        [[nodiscard]] auto ParseModuleTypeText(const std::string_view text) -> CoreResult<ModuleType>
        {
            if (text == "Runtime") return ModuleType::Runtime;
            if (text == "Editor") return ModuleType::Editor;
            if (text == "Program") return ModuleType::Program;
            if (text == "Developer") return ModuleType::Developer;
            if (text == "ThirdParty") return ModuleType::ThirdParty;
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeBuilderError("unknown module type", std::string(text), KernelErrorCode::SchemaValidationFailure));
        }

        [[nodiscard]] auto ParseHostTypeText(const std::string_view text) -> CoreResult<HostType>
        {
            if (text == "ConsoleApp") return HostType::ConsoleApp;
            if (text == "GuiApp") return HostType::GuiApp;
            if (text == "Game") return HostType::Game;
            if (text == "Editor") return HostType::Editor;
            if (text == "Service") return HostType::Service;
            if (text == "TestHost") return HostType::TestHost;
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeBuilderError("unknown supported host", std::string(text), KernelErrorCode::SchemaValidationFailure));
        }

        [[nodiscard]] auto ParseStartupStageText(const std::string_view text) -> CoreResult<StartupStage>
        {
            if (text == "Foundation") return StartupStage::Foundation;
            if (text == "Platform") return StartupStage::Platform;
            if (text == "Services") return StartupStage::Services;
            if (text == "Features") return StartupStage::Features;
            if (text == "Presentation") return StartupStage::Presentation;
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeBuilderError("unknown startup stage", std::string(text), KernelErrorCode::SchemaValidationFailure));
        }

        [[nodiscard]] auto ParsePackageModuleDescriptor(
            const XmlElement& element,
            const std::string_view context) -> CoreResult<ModuleDescriptor>
        {
            ModuleDescriptor descriptor {};

            auto name = RequireAttribute(element, "Name", context);
            if (!name)
            {
                return NGIN::Utilities::Unexpected<KernelError>(name.ErrorUnsafe());
            }
            descriptor.name = name.ValueUnsafe();

            auto familyText = OptionalAttribute(element, "Family", context, "Core");
            if (!familyText)
            {
                return NGIN::Utilities::Unexpected<KernelError>(familyText.ErrorUnsafe());
            }
            auto family = ParseModuleFamilyText(familyText.ValueUnsafe());
            if (!family)
            {
                return NGIN::Utilities::Unexpected<KernelError>(family.ErrorUnsafe());
            }
            descriptor.family = family.ValueUnsafe();

            auto typeText = OptionalAttribute(element, "Type", context, "Runtime");
            if (!typeText)
            {
                return NGIN::Utilities::Unexpected<KernelError>(typeText.ErrorUnsafe());
            }
            auto type = ParseModuleTypeText(typeText.ValueUnsafe());
            if (!type)
            {
                return NGIN::Utilities::Unexpected<KernelError>(type.ErrorUnsafe());
            }
            descriptor.type = type.ValueUnsafe();

            auto stageText = OptionalAttribute(element, "StartupStage", context, "Features");
            if (!stageText)
            {
                return NGIN::Utilities::Unexpected<KernelError>(stageText.ErrorUnsafe());
            }
            auto stage = ParseStartupStageText(stageText.ValueUnsafe());
            if (!stage)
            {
                return NGIN::Utilities::Unexpected<KernelError>(stage.ErrorUnsafe());
            }
            descriptor.startupStage = stage.ValueUnsafe();

            auto versionText = OptionalAttribute(element, "Version", context, {});
            if (!versionText)
            {
                return NGIN::Utilities::Unexpected<KernelError>(versionText.ErrorUnsafe());
            }
            if (!versionText.ValueUnsafe().empty())
            {
                auto version = ParseSemanticVersion(versionText.ValueUnsafe());
                if (!version)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(version.ErrorUnsafe());
                }
                descriptor.version = version.ValueUnsafe();
            }

            auto rangeText = OptionalAttribute(element, "CompatiblePlatformRange", context, {});
            if (!rangeText)
            {
                return NGIN::Utilities::Unexpected<KernelError>(rangeText.ErrorUnsafe());
            }
            if (!rangeText.ValueUnsafe().empty())
            {
                auto range = ParseVersionRange(rangeText.ValueUnsafe());
                if (!range)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(range.ErrorUnsafe());
                }
                descriptor.compatiblePlatformRange = range.ValueUnsafe();
            }

            auto reflectionRequired = OptionalBoolAttribute(
                element,
                "ReflectionRequired",
                std::string(context) + ".ReflectionRequired",
                false);
            if (!reflectionRequired)
            {
                return NGIN::Utilities::Unexpected<KernelError>(reflectionRequired.ErrorUnsafe());
            }
            descriptor.reflectionRequired = reflectionRequired.ValueUnsafe();

            if (const auto* platforms = FindChild(element, "Platforms"))
            {
                for (const auto* platformElement : ChildElements(*platforms, "Platform"))
                {
                    auto platform = RequireAttribute(*platformElement, "Name", std::string(context) + ".Platforms");
                    if (!platform)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(platform.ErrorUnsafe());
                    }
                    descriptor.platforms.push_back(platform.ValueUnsafe());
                }
            }

            if (const auto* dependencies = FindChild(element, "Dependencies"))
            {
                for (const auto* dependencyElement : ChildElements(*dependencies, "Dependency"))
                {
                    auto dependencyName = RequireAttribute(*dependencyElement, "Name", std::string(context) + ".Dependencies");
                    if (!dependencyName)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(dependencyName.ErrorUnsafe());
                    }

                    auto optional = OptionalBoolAttribute(
                        *dependencyElement,
                        "Optional",
                        std::string(context) + ".Dependencies.Optional",
                        false);
                    if (!optional)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(optional.ErrorUnsafe());
                    }

                    DependencyDescriptor dependency {};
                    dependency.name = dependencyName.ValueUnsafe();
                    dependency.optional = optional.ValueUnsafe();

                    auto requiredVersion = OptionalAttribute(
                        *dependencyElement,
                        "RequiredVersion",
                        std::string(context) + ".Dependencies.RequiredVersion",
                        {});
                    if (!requiredVersion)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(requiredVersion.ErrorUnsafe());
                    }
                    if (!requiredVersion.ValueUnsafe().empty())
                    {
                        auto range = ParseVersionRange(requiredVersion.ValueUnsafe());
                        if (!range)
                        {
                            return NGIN::Utilities::Unexpected<KernelError>(range.ErrorUnsafe());
                        }
                        dependency.requiredVersion = range.ValueUnsafe();
                    }

                    descriptor.dependencies.push_back(std::move(dependency));
                }
            }

            if (const auto* supportedHosts = FindChild(element, "SupportedHosts"))
            {
                for (const auto* hostElement : ChildElements(*supportedHosts, "Host"))
                {
                    auto hostName = RequireAttribute(*hostElement, "Name", std::string(context) + ".SupportedHosts");
                    if (!hostName)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(hostName.ErrorUnsafe());
                    }

                    auto host = ParseHostTypeText(hostName.ValueUnsafe());
                    if (!host)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(host.ErrorUnsafe());
                    }

                    descriptor.supportedHosts.push_back(host.ValueUnsafe());
                }
            }

            if (const auto* providesServices = FindChild(element, "ProvidesServices"))
            {
                for (const auto* serviceElement : ChildElements(*providesServices, "Service"))
                {
                    auto service = RequireAttribute(*serviceElement, "Name", std::string(context) + ".ProvidesServices");
                    if (!service)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(service.ErrorUnsafe());
                    }
                    descriptor.providesServices.push_back(service.ValueUnsafe());
                }
            }

            if (const auto* requiresServices = FindChild(element, "RequiresServices"))
            {
                for (const auto* serviceElement : ChildElements(*requiresServices, "Service"))
                {
                    auto service = RequireAttribute(*serviceElement, "Name", std::string(context) + ".RequiresServices");
                    if (!service)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(service.ErrorUnsafe());
                    }
                    descriptor.requiresServices.push_back(service.ValueUnsafe());
                }
            }

            if (const auto* capabilities = FindChild(element, "Capabilities"))
            {
                for (const auto* capabilityElement : ChildElements(*capabilities, "Capability"))
                {
                    auto capability = RequireAttribute(*capabilityElement, "Name", std::string(context) + ".Capabilities");
                    if (!capability)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(capability.ErrorUnsafe());
                    }
                    descriptor.capabilities.push_back(capability.ValueUnsafe());
                }
            }

            return descriptor;
        }

        [[nodiscard]] auto ParsePackagePluginManifest(
            const XmlElement& element,
            const std::string_view context) -> CoreResult<PackagePluginManifest>
        {
            PackagePluginManifest plugin {};

            auto name = RequireAttribute(element, "Name", context);
            if (!name)
            {
                return NGIN::Utilities::Unexpected<KernelError>(name.ErrorUnsafe());
            }
            plugin.name = name.ValueUnsafe();

            auto optional = OptionalBoolAttribute(element, "Optional", std::string(context) + ".Optional", false);
            if (!optional)
            {
                return NGIN::Utilities::Unexpected<KernelError>(optional.ErrorUnsafe());
            }
            plugin.optional = optional.ValueUnsafe();

            if (const auto* platforms = FindChild(element, "Platforms"))
            {
                for (const auto* platformElement : ChildElements(*platforms, "Platform"))
                {
                    auto platform = RequireAttribute(*platformElement, "Name", std::string(context) + ".Platforms");
                    if (!platform)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(platform.ErrorUnsafe());
                    }
                    plugin.platforms.push_back(platform.ValueUnsafe());
                }
            }

            if (const auto* modules = FindChild(element, "Modules"))
            {
                if (const auto* required = FindChild(*modules, "Required"))
                {
                    for (const auto* moduleElement : ChildElements(*required, "ModuleRef"))
                    {
                        auto moduleName = RequireAttribute(*moduleElement, "Name", std::string(context) + ".Modules.Required");
                        if (!moduleName)
                        {
                            return NGIN::Utilities::Unexpected<KernelError>(moduleName.ErrorUnsafe());
                        }
                        plugin.requiredModules.push_back(moduleName.ValueUnsafe());
                    }
                }
                if (const auto* optionalModules = FindChild(*modules, "Optional"))
                {
                    for (const auto* moduleElement : ChildElements(*optionalModules, "ModuleRef"))
                    {
                        auto moduleName = RequireAttribute(*moduleElement, "Name", std::string(context) + ".Modules.Optional");
                        if (!moduleName)
                        {
                            return NGIN::Utilities::Unexpected<KernelError>(moduleName.ErrorUnsafe());
                        }
                        plugin.optionalModules.push_back(moduleName.ValueUnsafe());
                    }
                }
            }

            return plugin;
        }

        [[nodiscard]] constexpr auto ToHostType(const HostProfile profile) noexcept -> HostType
        {
            switch (profile)
            {
                case HostProfile::ConsoleApp: return HostType::ConsoleApp;
                case HostProfile::GuiApp: return HostType::GuiApp;
                case HostProfile::Game: return HostType::Game;
                case HostProfile::Editor: return HostType::Editor;
                case HostProfile::Service: return HostType::Service;
                case HostProfile::TestHost: return HostType::TestHost;
            }
            return HostType::ConsoleApp;
        }

        [[nodiscard]] auto ParsePackageReference(
            const XmlElement& element,
            const std::string_view context) -> CoreResult<PackageReference>
        {
            auto name = RequireAttribute(element, "Name", context);
            if (!name)
            {
                return NGIN::Utilities::Unexpected<KernelError>(name.ErrorUnsafe());
            }

            auto versionRange = OptionalAttribute(element, "VersionRange", context);
            if (!versionRange)
            {
                return NGIN::Utilities::Unexpected<KernelError>(versionRange.ErrorUnsafe());
            }

            auto optional = OptionalBoolAttribute(element, "Optional", std::string(context) + " attribute 'Optional'", false);
            if (!optional)
            {
                return NGIN::Utilities::Unexpected<KernelError>(optional.ErrorUnsafe());
            }

            return PackageReference {
                .name = name.ValueUnsafe(),
                .versionRange = versionRange.ValueUnsafe(),
                .optional = optional.ValueUnsafe(),
            };
        }

        [[nodiscard]] auto ParseModuleRefs(
            const XmlElement* parentElement,
            const std::string_view childName,
            const std::string_view context) -> CoreResult<std::vector<std::string>>
        {
            std::vector<std::string> out {};
            if (parentElement == nullptr)
            {
                return out;
            }

            for (const auto* moduleElement : ChildElements(*parentElement, childName))
            {
                auto name = RequireAttribute(*moduleElement, "Name", std::string(context) + "." + std::string(childName));
                if (!name)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(name.ErrorUnsafe());
                }
                out.push_back(name.ValueUnsafe());
            }
            return out;
        }

        [[nodiscard]] auto ParseProjectReference(
            const XmlElement& element,
            const std::string_view context) -> CoreResult<ProjectReference>
        {
            ProjectReference reference {};

            auto path = RequireAttribute(element, "Path", context);
            if (!path)
            {
                return NGIN::Utilities::Unexpected<KernelError>(path.ErrorUnsafe());
            }
            reference.path = path.ValueUnsafe();

            auto variant = OptionalAttribute(element, "Variant", context, {});
            if (!variant)
            {
                return NGIN::Utilities::Unexpected<KernelError>(variant.ErrorUnsafe());
            }
            if (!variant.ValueUnsafe().empty())
            {
                reference.variant = variant.ValueUnsafe();
            }

            return reference;
        }

        [[nodiscard]] auto ParsePrimaryOutput(
            const XmlElement& element,
            const std::string_view context) -> CoreResult<PrimaryOutput>
        {
            PrimaryOutput output {};

            auto kind = RequireAttribute(element, "Kind", context);
            if (!kind)
            {
                return NGIN::Utilities::Unexpected<KernelError>(kind.ErrorUnsafe());
            }
            output.kind = kind.ValueUnsafe();

            auto name = RequireAttribute(element, "Name", context);
            if (!name)
            {
                return NGIN::Utilities::Unexpected<KernelError>(name.ErrorUnsafe());
            }
            output.name = name.ValueUnsafe();

            auto target = RequireAttribute(element, "Target", context);
            if (!target)
            {
                return NGIN::Utilities::Unexpected<KernelError>(target.ErrorUnsafe());
            }
            output.target = target.ValueUnsafe();

            return output;
        }

        [[nodiscard]] auto ParseRuntimeDefinition(
            const XmlElement* runtimeElement,
            const std::string_view context) -> CoreResult<RuntimeDefinition>
        {
            RuntimeDefinition runtime {};
            if (runtimeElement == nullptr)
            {
                return runtime;
            }

            if (const auto* modulesElement = FindChild(*runtimeElement, "Modules"))
            {
                std::size_t index = 0;
                for (const auto* moduleElement : ChildElements(*modulesElement, "Module"))
                {
                    auto descriptor = ParsePackageModuleDescriptor(
                        *moduleElement,
                        std::string(context) + ".Modules[" + std::to_string(index++) + "]");
                    if (!descriptor)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(descriptor.ErrorUnsafe());
                    }
                    runtime.modules.push_back(descriptor.ValueUnsafe());
                }
            }

            auto enabled = ParseModuleRefs(FindChild(*runtimeElement, "EnableModules"), "ModuleRef", std::string(context) + ".EnableModules");
            if (!enabled)
            {
                return NGIN::Utilities::Unexpected<KernelError>(enabled.ErrorUnsafe());
            }
            runtime.enableModules = std::move(enabled.ValueUnsafe());

            auto disabled = ParseModuleRefs(FindChild(*runtimeElement, "DisableModules"), "ModuleRef", std::string(context) + ".DisableModules");
            if (!disabled)
            {
                return NGIN::Utilities::Unexpected<KernelError>(disabled.ErrorUnsafe());
            }
            runtime.disableModules = std::move(disabled.ValueUnsafe());

            return runtime;
        }

        [[nodiscard]] auto ParseVariantDefinition(
            const XmlElement& element,
            const std::string_view context) -> CoreResult<VariantDefinition>
        {
            VariantDefinition variant {};

            auto name = RequireAttribute(element, "Name", context);
            if (!name)
            {
                return NGIN::Utilities::Unexpected<KernelError>(name.ErrorUnsafe());
            }
            variant.name = name.ValueUnsafe();

            auto profileText = RequireAttribute(element, "Profile", context);
            if (!profileText)
            {
                return NGIN::Utilities::Unexpected<KernelError>(profileText.ErrorUnsafe());
            }
            auto profile = ParseHostProfile(profileText.ValueUnsafe());
            if (!profile)
            {
                return NGIN::Utilities::Unexpected<KernelError>(profile.ErrorUnsafe());
            }
            variant.profile = profile.ValueUnsafe();

            auto platform = RequireAttribute(element, "Platform", context);
            if (!platform)
            {
                return NGIN::Utilities::Unexpected<KernelError>(platform.ErrorUnsafe());
            }
            variant.platform = platform.ValueUnsafe();

            auto reflection = OptionalBoolAttribute(element, "EnableReflection", std::string(context) + ".EnableReflection", false);
            if (!reflection)
            {
                return NGIN::Utilities::Unexpected<KernelError>(reflection.ErrorUnsafe());
            }
            variant.enableReflection = reflection.ValueUnsafe();

            auto environment = OptionalAttribute(element, "Environment", context, {});
            if (!environment)
            {
                return NGIN::Utilities::Unexpected<KernelError>(environment.ErrorUnsafe());
            }
            variant.environmentName = environment.ValueUnsafe();

            auto workingDirectory = OptionalAttribute(element, "WorkingDirectory", context, ".");
            if (!workingDirectory)
            {
                return NGIN::Utilities::Unexpected<KernelError>(workingDirectory.ErrorUnsafe());
            }
            variant.workingDirectory = workingDirectory.ValueUnsafe();

            if (const auto* launchElement = FindChild(element, "Launch"))
            {
                auto executable = RequireAttribute(*launchElement, "Executable", std::string(context) + ".Launch");
                if (!executable)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(executable.ErrorUnsafe());
                }
                variant.launchExecutable = executable.ValueUnsafe();
            }

            if (const auto* packagesElement = FindChild(element, "PackageRefs"))
            {
                for (const auto* packageElement : ChildElements(*packagesElement, "PackageRef"))
                {
                    auto package = ParsePackageReference(*packageElement, std::string(context) + ".PackageRefs");
                    if (!package)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(package.ErrorUnsafe());
                    }
                    variant.packageRefs.push_back(package.ValueUnsafe());
                }
            }

            if (const auto* configSources = FindChild(element, "ConfigSources"))
            {
                for (const auto* configElement : ChildElements(*configSources, "Config"))
                {
                    auto source = RequireAttribute(*configElement, "Source", std::string(context) + ".ConfigSources");
                    if (!source)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(source.ErrorUnsafe());
                    }
                    variant.configSources.push_back(source.ValueUnsafe());
                }
            }

            auto enabled = ParseModuleRefs(FindChild(element, "EnableModules"), "ModuleRef", std::string(context) + ".EnableModules");
            if (!enabled)
            {
                return NGIN::Utilities::Unexpected<KernelError>(enabled.ErrorUnsafe());
            }
            variant.enableModules = std::move(enabled.ValueUnsafe());

            auto disabled = ParseModuleRefs(FindChild(element, "DisableModules"), "ModuleRef", std::string(context) + ".DisableModules");
            if (!disabled)
            {
                return NGIN::Utilities::Unexpected<KernelError>(disabled.ErrorUnsafe());
            }
            variant.disableModules = std::move(disabled.ValueUnsafe());

            return variant;
        }

        [[nodiscard]] auto ParseProjectManifestText(const std::string& text) -> CoreResult<ProjectManifest>
        {
            auto loaded = LoadXmlDocument(text, "project");
            if (!loaded)
            {
                return NGIN::Utilities::Unexpected<KernelError>(loaded.ErrorUnsafe());
            }

            const auto* root = loaded.ValueUnsafe().document.Root();
            if (root == nullptr || root->name != "Project")
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeBuilderError("project manifest root element must be <Project>"));
            }

            ProjectManifest manifest {};

            auto schemaVersion = OptionalAttribute(*root, "SchemaVersion", "project", "1");
            if (!schemaVersion)
            {
                return NGIN::Utilities::Unexpected<KernelError>(schemaVersion.ErrorUnsafe());
            }
            auto parsedSchema = ParseUInt32Value(schemaVersion.ValueUnsafe(), "project.SchemaVersion");
            if (!parsedSchema)
            {
                return NGIN::Utilities::Unexpected<KernelError>(parsedSchema.ErrorUnsafe());
            }
            manifest.schemaVersion = parsedSchema.ValueUnsafe();

            auto name = RequireAttribute(*root, "Name", "project");
            if (!name)
            {
                return NGIN::Utilities::Unexpected<KernelError>(name.ErrorUnsafe());
            }
            manifest.name = name.ValueUnsafe();

            auto type = OptionalAttribute(*root, "Type", "project", "Application");
            if (!type)
            {
                return NGIN::Utilities::Unexpected<KernelError>(type.ErrorUnsafe());
            }
            manifest.type = type.ValueUnsafe();

            auto defaultVariant = RequireAttribute(*root, "DefaultVariant", "project");
            if (!defaultVariant)
            {
                return NGIN::Utilities::Unexpected<KernelError>(defaultVariant.ErrorUnsafe());
            }
            manifest.defaultVariant = defaultVariant.ValueUnsafe();

            if (const auto* sourceRootsElement = FindChild(*root, "SourceRoots"))
            {
                for (const auto* sourceRootElement : ChildElements(*sourceRootsElement, "SourceRoot"))
                {
                    auto path = RequireAttribute(*sourceRootElement, "Path", "project.SourceRoots");
                    if (!path)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(path.ErrorUnsafe());
                    }
                    manifest.sourceRoots.push_back(path.ValueUnsafe());
                }
            }

            const auto* primaryOutputElement = FindChild(*root, "PrimaryOutput");
            if (primaryOutputElement == nullptr)
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeBuilderError("project must contain a <PrimaryOutput> element"));
            }
            auto primaryOutput = ParsePrimaryOutput(*primaryOutputElement, "project.PrimaryOutput");
            if (!primaryOutput)
            {
                return NGIN::Utilities::Unexpected<KernelError>(primaryOutput.ErrorUnsafe());
            }
            manifest.primaryOutput = primaryOutput.ValueUnsafe();

            if (const auto* projectRefsElement = FindChild(*root, "ProjectRefs"))
            {
                std::size_t index = 0;
                for (const auto* projectRefElement : ChildElements(*projectRefsElement, "ProjectRef"))
                {
                    auto reference = ParseProjectReference(*projectRefElement, "project.ProjectRefs[" + std::to_string(index++) + "]");
                    if (!reference)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(reference.ErrorUnsafe());
                    }
                    manifest.projectRefs.push_back(reference.ValueUnsafe());
                }
            }

            if (const auto* packageRefsElement = FindChild(*root, "PackageRefs"))
            {
                for (const auto* packageElement : ChildElements(*packageRefsElement, "PackageRef"))
                {
                    auto package = ParsePackageReference(*packageElement, "project.PackageRefs");
                    if (!package)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(package.ErrorUnsafe());
                    }
                    manifest.packageRefs.push_back(package.ValueUnsafe());
                }
            }

            if (const auto* configSources = FindChild(*root, "ConfigSources"))
            {
                for (const auto* configElement : ChildElements(*configSources, "Config"))
                {
                    auto source = RequireAttribute(*configElement, "Source", "project.ConfigSources");
                    if (!source)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(source.ErrorUnsafe());
                    }
                    manifest.configSources.push_back(source.ValueUnsafe());
                }
            }

            auto runtime = ParseRuntimeDefinition(FindChild(*root, "Runtime"), "project.Runtime");
            if (!runtime)
            {
                return NGIN::Utilities::Unexpected<KernelError>(runtime.ErrorUnsafe());
            }
            manifest.runtime = std::move(runtime.ValueUnsafe());

            const auto* variantsElement = FindChild(*root, "Variants");
            if (variantsElement == nullptr)
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeBuilderError("project must contain a <Variants> element"));
            }

            std::set<std::string> variantNames {};
            std::size_t index = 0;
            for (const auto* variantElement : ChildElements(*variantsElement, "Variant"))
            {
                auto variant = ParseVariantDefinition(*variantElement, "project.Variants[" + std::to_string(index++) + "]");
                if (!variant)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(variant.ErrorUnsafe());
                }
                if (!variantNames.insert(variant.ValueUnsafe().name).second)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(
                        MakeBuilderError("duplicate variant name", variant.ValueUnsafe().name, KernelErrorCode::AlreadyExists));
                }
                manifest.variants.push_back(variant.ValueUnsafe());
            }

            return manifest;
        }

        struct ResolvedProjectUnit
        {
            ProjectManifest       manifest {};
            VariantDefinition     variant {};
            std::filesystem::path directory {};
        };

        [[nodiscard]] auto FindVariant(
            const ProjectManifest& manifest,
            const std::optional<std::string>& overrideVariant = std::nullopt) -> CoreResult<VariantDefinition>
        {
            const std::string selectedVariant = overrideVariant.has_value() && !overrideVariant->empty()
                ? *overrideVariant
                : manifest.defaultVariant;
            if (selectedVariant.empty())
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeBuilderError("project has no selected variant", manifest.name, KernelErrorCode::InvalidArgument));
            }

            const auto it = std::find_if(
                manifest.variants.begin(),
                manifest.variants.end(),
                [&](const VariantDefinition& candidate) {
                    return candidate.name == selectedVariant;
                });
            if (it == manifest.variants.end())
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeBuilderError("selected variant was not found", selectedVariant, KernelErrorCode::NotFound));
            }
            return *it;
        }

        [[nodiscard]] auto LoadProjectManifestFile(
            const std::filesystem::path& filePath) -> CoreResult<std::pair<ProjectManifest, std::filesystem::path>>
        {
            auto fileText = ReadTextFile(filePath, "project");
            if (!fileText)
            {
                return NGIN::Utilities::Unexpected<KernelError>(fileText.ErrorUnsafe());
            }

            auto manifest = ParseProjectManifestText(fileText.ValueUnsafe());
            if (!manifest)
            {
                return NGIN::Utilities::Unexpected<KernelError>(manifest.ErrorUnsafe());
            }

            return std::pair<ProjectManifest, std::filesystem::path> {
                manifest.ValueUnsafe(),
                std::filesystem::absolute(filePath).parent_path(),
            };
        }

        auto CollectProjectClosure(
            const std::filesystem::path& filePath,
            const std::optional<std::string>& selectedVariant,
            std::vector<ResolvedProjectUnit>& out,
            std::set<std::filesystem::path>& visiting,
            std::set<std::filesystem::path>& visited) -> CoreResult<void>
        {
            const auto canonicalPath = std::filesystem::weakly_canonical(filePath);
            if (visited.contains(canonicalPath))
            {
                return {};
            }
            if (visiting.contains(canonicalPath))
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeBuilderError("project reference cycle detected", canonicalPath.string(), KernelErrorCode::DependencyCycle));
            }
            visiting.insert(canonicalPath);

            auto loaded = LoadProjectManifestFile(canonicalPath);
            if (!loaded)
            {
                return NGIN::Utilities::Unexpected<KernelError>(loaded.ErrorUnsafe());
            }

            auto manifest = std::move(loaded.ValueUnsafe().first);
            auto directory = std::move(loaded.ValueUnsafe().second);

            for (const auto& reference : manifest.projectRefs)
            {
                auto referencedPath = std::filesystem::path(reference.path);
                if (referencedPath.is_relative())
                {
                    referencedPath = directory / referencedPath;
                }

                auto result = CollectProjectClosure(referencedPath, reference.variant, out, visiting, visited);
                if (!result)
                {
                    return result;
                }
            }

            auto variant = FindVariant(manifest, selectedVariant);
            if (!variant)
            {
                return NGIN::Utilities::Unexpected<KernelError>(variant.ErrorUnsafe());
            }

            out.push_back(ResolvedProjectUnit {
                .manifest = std::move(manifest),
                .variant = variant.ValueUnsafe(),
                .directory = std::move(directory),
            });

            visiting.erase(canonicalPath);
            visited.insert(canonicalPath);
            return {};
        }

        [[nodiscard]] auto ParsePackageManifestText(const std::string& text) -> CoreResult<PackageManifest>
        {
            auto loaded = LoadXmlDocument(text, "package");
            if (!loaded)
            {
                return NGIN::Utilities::Unexpected<KernelError>(loaded.ErrorUnsafe());
            }

            const auto* root = loaded.ValueUnsafe().document.Root();
            if (root == nullptr || root->name != "Package")
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeBuilderError("package manifest root element must be <Package>"));
            }

            PackageManifest manifest {};

            auto schemaVersion = OptionalAttribute(*root, "SchemaVersion", "package", "1");
            if (!schemaVersion)
            {
                return NGIN::Utilities::Unexpected<KernelError>(schemaVersion.ErrorUnsafe());
            }
            auto parsedSchema = ParseUInt32Value(schemaVersion.ValueUnsafe(), "package.SchemaVersion");
            if (!parsedSchema)
            {
                return NGIN::Utilities::Unexpected<KernelError>(parsedSchema.ErrorUnsafe());
            }
            manifest.schemaVersion = parsedSchema.ValueUnsafe();

            auto name = RequireAttribute(*root, "Name", "package");
            if (!name)
            {
                return NGIN::Utilities::Unexpected<KernelError>(name.ErrorUnsafe());
            }
            manifest.name = name.ValueUnsafe();

            auto version = RequireAttribute(*root, "Version", "package");
            if (!version)
            {
                return NGIN::Utilities::Unexpected<KernelError>(version.ErrorUnsafe());
            }
            manifest.version = version.ValueUnsafe();

            auto platformRange = RequireAttribute(*root, "CompatiblePlatformRange", "package");
            if (!platformRange)
            {
                return NGIN::Utilities::Unexpected<KernelError>(platformRange.ErrorUnsafe());
            }
            manifest.compatiblePlatformRange = platformRange.ValueUnsafe();

            const auto* platformsElement = FindChild(*root, "Platforms");
            if (platformsElement == nullptr)
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeBuilderError("package must contain a <Platforms> element"));
            }
            for (const auto* platformElement : ChildElements(*platformsElement, "Platform"))
            {
                auto platform = RequireAttribute(*platformElement, "Name", "package.Platforms");
                if (!platform)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(platform.ErrorUnsafe());
                }
                manifest.platforms.push_back(platform.ValueUnsafe());
            }

            if (const auto* dependenciesElement = FindChild(*root, "Dependencies"))
            {
                for (const auto* dependencyElement : ChildElements(*dependenciesElement, "Dependency"))
                {
                    auto reference = ParsePackageReference(*dependencyElement, "package.Dependencies");
                    if (!reference)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(reference.ErrorUnsafe());
                    }
                    manifest.dependencies.push_back(reference.ValueUnsafe());
                }
            }

            if (const auto* bootstrapElement = FindChild(*root, "Bootstrap"))
            {
                auto modeText = RequireAttribute(*bootstrapElement, "Mode", "package.Bootstrap");
                if (!modeText)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(modeText.ErrorUnsafe());
                }
                auto mode = ParsePackageBootstrapMode(modeText.ValueUnsafe());
                if (!mode)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(mode.ErrorUnsafe());
                }

                auto entryPoint = RequireAttribute(*bootstrapElement, "EntryPoint", "package.Bootstrap");
                if (!entryPoint)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(entryPoint.ErrorUnsafe());
                }

                auto autoApply = OptionalBoolAttribute(*bootstrapElement, "AutoApply", "package.Bootstrap.AutoApply", false);
                if (!autoApply)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(autoApply.ErrorUnsafe());
                }

                manifest.bootstrap = PackageBootstrapDescriptor {
                    .mode = mode.ValueUnsafe(),
                    .entryPoint = entryPoint.ValueUnsafe(),
                    .autoApply = autoApply.ValueUnsafe(),
                };
            }

            if (const auto* contentsElement = FindChild(*root, "Contents"))
            {
                for (const auto* fileElement : ChildElements(*contentsElement, "File"))
                {
                    auto source = RequireAttribute(*fileElement, "Source", "package.Contents.File");
                    if (!source)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(source.ErrorUnsafe());
                    }

                    auto target = OptionalAttribute(*fileElement, "Target", "package.Contents.File", {});
                    if (!target)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(target.ErrorUnsafe());
                    }

                    auto kind = OptionalAttribute(*fileElement, "Kind", "package.Contents.File", "other");
                    if (!kind)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(kind.ErrorUnsafe());
                    }

                    manifest.contents.push_back(PackageContentFile {
                        .source = source.ValueUnsafe(),
                        .target = target.ValueUnsafe(),
                        .kind = kind.ValueUnsafe(),
                    });
                }
            }

            const auto* modulesElement = FindChild(*root, "Modules");
            if (modulesElement == nullptr)
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeBuilderError("package must contain a <Modules> element"));
            }

            std::set<std::string> moduleNames {};
            for (const auto* moduleElement : ChildElements(*modulesElement, "Module"))
            {
                auto module = ParsePackageModuleDescriptor(*moduleElement, "package.Modules.Module");
                if (!module)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(module.ErrorUnsafe());
                }

                if (!moduleNames.insert(module.ValueUnsafe().name).second)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(
                        MakeBuilderError("duplicate package module declaration", module.ValueUnsafe().name, KernelErrorCode::AlreadyExists));
                }

                manifest.modules.push_back(module.ValueUnsafe());
            }

            if (const auto* pluginsElement = FindChild(*root, "Plugins"))
            {
                std::set<std::string> pluginNames {};
                for (const auto* pluginElement : ChildElements(*pluginsElement, "Plugin"))
                {
                    auto plugin = ParsePackagePluginManifest(*pluginElement, "package.Plugins.Plugin");
                    if (!plugin)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(plugin.ErrorUnsafe());
                    }

                    if (!pluginNames.insert(plugin.ValueUnsafe().name).second)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(
                            MakeBuilderError("duplicate package plugin declaration", plugin.ValueUnsafe().name, KernelErrorCode::AlreadyExists));
                    }

                    manifest.plugins.push_back(plugin.ValueUnsafe());
                }
            }

            return manifest;
        }

        template<typename T>
        void AppendUnique(std::vector<T>& target, const T& value)
        {
            if (std::find(target.begin(), target.end(), value) == target.end())
            {
                target.push_back(value);
            }
        }

        void AppendUniqueStrings(std::vector<std::string>& target, const std::vector<std::string>& values)
        {
            for (const auto& value : values)
            {
                AppendUnique(target, value);
            }
        }

        void MergePackageReferences(
            std::vector<PackageReference>& target,
            const std::vector<PackageReference>& source)
        {
            std::unordered_map<std::string, std::size_t> indexByName {};
            for (std::size_t index = 0; index < target.size(); ++index)
            {
                indexByName[target[index].name] = index;
            }

            for (const auto& reference : source)
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
            std::vector<StaticModuleRegistration>& registrations,
            StaticModuleRegistration registration)
        {
            const auto it = std::find_if(
                registrations.begin(),
                registrations.end(),
                [&](const StaticModuleRegistration& candidate) {
                    return candidate.descriptor.name == registration.descriptor.name;
                });

            if (it == registrations.end())
            {
                registrations.push_back(std::move(registration));
                return;
            }

            *it = std::move(registration);
        }

        [[nodiscard]] auto BuildCatalogFrom(
            const std::vector<StaticModuleRegistration>& registrations,
            const std::set<std::string>& disabledModules) -> CoreResult<NGIN::Memory::Shared<IModuleCatalog>>
        {
            auto catalog = CreateStaticModuleCatalog();
            if (!catalog)
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeBuilderError("failed to create module catalog", {}, KernelErrorCode::InternalError));
            }

            for (const auto& registration : registrations)
            {
                if (disabledModules.contains(registration.descriptor.name))
                {
                    continue;
                }

                auto result = catalog->Register(registration);
                if (!result)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(result.ErrorUnsafe());
                }
            }

            return catalog;
        }

        [[nodiscard]] auto ResolveWorkingDirectory(
            std::string workingDirectory,
            const std::filesystem::path& projectDirectory) -> std::string
        {
            if (workingDirectory.empty())
            {
                return workingDirectory;
            }

            std::filesystem::path workPath(workingDirectory);
            if (workPath.is_relative() && !projectDirectory.empty())
            {
                return (projectDirectory / workPath).lexically_normal().string();
            }
            return workPath.lexically_normal().string();
        }

        class PackageBootstrapRegistryImpl final : public PackageBootstrapRegistry
        {
        public:
            explicit PackageBootstrapRegistryImpl(std::unordered_map<std::string, std::string> defaultEntryPoints)
                : m_defaultEntryPoints(std::move(defaultEntryPoints))
            {
            }

            auto Register(PackageBootstrapEntry entry) -> CoreResult<void> override
            {
                if (entry.packageName.empty() || entry.entryPoint.empty() || entry.fn == nullptr)
                {
                    m_lastError = MakeBuilderError(
                        "package bootstrap entry must have package name, entry point, and function",
                        entry.packageName,
                        KernelErrorCode::InvalidArgument);
                    return NGIN::Utilities::Unexpected<KernelError>(*m_lastError);
                }

                const std::string key = entry.packageName + "::" + entry.entryPoint;
                if (m_indexByKey.contains(key))
                {
                    m_lastError = MakeBuilderError(
                        "duplicate package bootstrap entry",
                        key,
                        KernelErrorCode::AlreadyExists);
                    return NGIN::Utilities::Unexpected<KernelError>(*m_lastError);
                }

                m_entries.push_back(std::move(entry));
                m_indexByKey.emplace(key, m_entries.size() - 1);
                m_lastError.reset();
                return {};
            }

            [[nodiscard]] auto Find(
                const std::string_view packageName,
                const std::string_view entryPoint) const noexcept -> const PackageBootstrapEntry* override
            {
                const auto it = m_indexByKey.find(std::string(packageName) + "::" + std::string(entryPoint));
                if (it == m_indexByKey.end())
                {
                    return nullptr;
                }
                return &m_entries[it->second];
            }

            [[nodiscard]] auto FindDefault(const std::string_view packageName) const noexcept
                -> const PackageBootstrapEntry* override
            {
                const auto defaultIt = m_defaultEntryPoints.find(std::string(packageName));
                if (defaultIt != m_defaultEntryPoints.end())
                {
                    return Find(packageName, defaultIt->second);
                }

                const PackageBootstrapEntry* match = nullptr;
                for (const auto& entry : m_entries)
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

            [[nodiscard]] auto LastError() const noexcept -> const std::optional<KernelError>&
            {
                return m_lastError;
            }

        private:
            std::vector<PackageBootstrapEntry>     m_entries {};
            std::unordered_map<std::string, std::size_t> m_indexByKey {};
            std::unordered_map<std::string, std::string> m_defaultEntryPoints {};
            std::optional<KernelError>             m_lastError {};
        };

        class ApplicationHostImpl final : public IApplicationHost
        {
        public:
            ApplicationHostImpl(
                NGIN::Memory::Shared<IKernel> kernel,
                const HostProfile profile,
                StartupReport metadataReport)
                : m_kernel(std::move(kernel))
                , m_profile(profile)
                , m_metadataReport(std::move(metadataReport))
            {
            }

            auto Start() noexcept -> CoreResult<void> override
            {
                return m_kernel->Start();
            }

            auto Run() noexcept -> CoreResult<void> override
            {
                return m_kernel->Run();
            }

            auto Tick() noexcept -> CoreResult<void> override
            {
                return m_kernel->Tick();
            }

            void RequestStop(std::string reason) noexcept override
            {
                m_kernel->RequestStop(std::move(reason));
            }

            auto Shutdown() noexcept -> CoreResult<void> override
            {
                return m_kernel->Shutdown();
            }

            [[nodiscard]] auto GetProfile() const noexcept -> HostProfile override
            {
                return m_profile;
            }

            [[nodiscard]] auto GetVariantName() const -> std::string override
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

                report.warnings.insert(
                    report.warnings.end(),
                    m_metadataReport.warnings.begin(),
                    m_metadataReport.warnings.end());

                report.failures.insert(
                    report.failures.end(),
                    m_metadataReport.failures.begin(),
                    m_metadataReport.failures.end());

                return report;
            }

            [[nodiscard]] auto GetServices() noexcept -> NGIN::Memory::Shared<IServiceRegistry> override
            {
                return m_kernel->GetServices();
            }

            [[nodiscard]] auto GetConfig() noexcept -> NGIN::Memory::Shared<IConfigStore> override
            {
                return m_kernel->GetConfig();
            }

        private:
            NGIN::Memory::Shared<IKernel> m_kernel {};
            HostProfile                   m_profile {HostProfile::ConsoleApp};
            StartupReport                 m_metadataReport {};
        };

        class ApplicationBuilderImpl;

        class ServiceCollectionImpl final : public ServiceCollection
        {
        public:
            explicit ServiceCollectionImpl(ApplicationBuilderImpl& owner)
                : m_owner(owner)
            {
            }

            auto AddSingleton(
                std::string key,
                NGIN::Utilities::Any<> service,
                ServiceMetadata metadata = {}) -> ServiceCollection& override;

            auto AddFactory(
                std::string key,
                ServiceFactory factory,
                ServiceLifetime lifetime,
                ServiceMetadata metadata = {}) -> ServiceCollection& override;

            auto AddScoped(
                std::string key,
                ServiceFactory factory,
                ServiceMetadata metadata = {}) -> ServiceCollection& override;

            auto AddTransient(
                std::string key,
                ServiceFactory factory,
                ServiceMetadata metadata = {}) -> ServiceCollection& override;

            auto AddDefaults() -> ServiceCollection& override;
            auto AddLogging() -> ServiceCollection& override;
            auto AddConfiguration() -> ServiceCollection& override;
            auto Clear() -> ServiceCollection& override;

        private:
            ApplicationBuilderImpl& m_owner;
        };

        class PackageCollectionImpl final : public PackageCollection
        {
        public:
            explicit PackageCollectionImpl(ApplicationBuilderImpl& owner)
                : m_owner(owner)
            {
            }

            auto Add(PackageReference reference) -> PackageCollection& override;
            auto AddManifest(PackageManifest manifest) -> PackageCollection& override;
            auto AddManifestFile(std::string path) -> PackageCollection& override;
            auto RegisterLinkedRegistrar(PackageBootstrapRegistrarFn registrar) -> PackageCollection& override;
            auto ApplyBootstrap(std::string packageName) -> PackageCollection& override;
            auto ApplyBootstrap(std::string packageName, std::string entryPoint) -> PackageCollection& override;
            auto Clear() -> PackageCollection& override;

        private:
            ApplicationBuilderImpl& m_owner;
        };

        class ModuleCollectionImpl final : public ModuleCollection
        {
        public:
            explicit ModuleCollectionImpl(ApplicationBuilderImpl& owner)
                : m_owner(owner)
            {
            }

            auto Register(StaticModuleRegistration registration) -> ModuleCollection& override;
            auto Enable(std::string moduleName) -> ModuleCollection& override;
            auto Disable(std::string moduleName) -> ModuleCollection& override;
            auto Clear() -> ModuleCollection& override;

        private:
            ApplicationBuilderImpl& m_owner;
        };

        class PluginCollectionImpl final : public PluginCollection
        {
        public:
            explicit PluginCollectionImpl(ApplicationBuilderImpl& owner)
                : m_owner(owner)
            {
            }

            auto Enable(std::string pluginName) -> PluginCollection& override;
            auto Disable(std::string pluginName) -> PluginCollection& override;
            auto AddSearchPath(std::string path) -> PluginCollection& override;
            auto Clear() -> PluginCollection& override;

        private:
            ApplicationBuilderImpl& m_owner;
        };

        class ConfigurationBuilderImpl final : public ConfigurationBuilder
        {
        public:
            explicit ConfigurationBuilderImpl(ApplicationBuilderImpl& owner)
                : m_owner(owner)
            {
            }

            auto AddSource(std::string path) -> ConfigurationBuilder& override;
            auto SetEnvironmentName(std::string environmentName) -> ConfigurationBuilder& override;
            auto SetWorkingDirectory(std::string workingDirectory) -> ConfigurationBuilder& override;
            auto Clear() -> ConfigurationBuilder& override;

        private:
            ApplicationBuilderImpl& m_owner;
        };

        class PackageBootstrapContextImpl final : public PackageBootstrapContext
        {
        public:
            PackageBootstrapContextImpl(
                std::string_view packageName,
                std::string_view variantName,
                const HostProfile profile,
                ServiceCollection& services,
                PackageCollection& packages,
                ModuleCollection& modules,
                PluginCollection& plugins,
                ConfigurationBuilder& configuration)
                : m_packageName(packageName)
                , m_variantName(variantName)
                , m_profile(profile)
                , m_services(services)
                , m_packages(packages)
                , m_modules(modules)
                , m_plugins(plugins)
                , m_configuration(configuration)
            {
            }

            [[nodiscard]] auto PackageName() const noexcept -> std::string_view override { return m_packageName; }
            [[nodiscard]] auto VariantName() const noexcept -> std::string_view override { return m_variantName; }
            [[nodiscard]] auto Profile() const noexcept -> HostProfile override { return m_profile; }

            [[nodiscard]] auto Services() noexcept -> ServiceCollection& override { return m_services; }
            [[nodiscard]] auto Packages() noexcept -> PackageCollection& override { return m_packages; }
            [[nodiscard]] auto Modules() noexcept -> ModuleCollection& override { return m_modules; }
            [[nodiscard]] auto Plugins() noexcept -> PluginCollection& override { return m_plugins; }
            [[nodiscard]] auto Configuration() noexcept -> ConfigurationBuilder& override { return m_configuration; }

        private:
            std::string_view      m_packageName;
            std::string_view      m_variantName;
            HostProfile           m_profile {HostProfile::ConsoleApp};
            ServiceCollection&    m_services;
            PackageCollection&    m_packages;
            ModuleCollection&     m_modules;
            PluginCollection&     m_plugins;
            ConfigurationBuilder& m_configuration;
        };

        class ApplicationBuilderImpl final : public ApplicationBuilder
        {
        public:
            ApplicationBuilderImpl(const int argc, char** argv)
                : m_services(*this)
                , m_packages(*this)
                , m_modules(*this)
                , m_plugins(*this)
                , m_configuration(*this)
            {
                for (int index = 1; index < argc; ++index)
                {
                    if (argv[index] != nullptr)
                    {
                        m_commandLineArgs.emplace_back(argv[index]);
                    }
                }
            }

            auto UseProjectFile(std::string path) -> ApplicationBuilder& override
            {
                MarkMutating();
                if (HasStickyError())
                {
                    return *this;
                }

                const auto filePath = std::filesystem::path(path);
                auto fileText = ReadTextFile(filePath, "project");
                if (!fileText)
                {
                    m_stickyError = fileText.ErrorUnsafe();
                    return *this;
                }

                auto manifest = ParseProjectManifestText(fileText.ValueUnsafe());
                if (!manifest)
                {
                    m_stickyError = manifest.ErrorUnsafe();
                    return *this;
                }

                m_project = manifest.ValueUnsafe();
                m_projectPath = std::filesystem::absolute(filePath);
                m_projectDirectory = std::filesystem::absolute(filePath).parent_path();
                return *this;
            }

            auto UseProject(ProjectManifest manifest) -> ApplicationBuilder& override
            {
                MarkMutating();
                if (!HasStickyError())
                {
                    m_project = std::move(manifest);
                    m_projectPath.clear();
                    m_projectDirectory.clear();
                }
                return *this;
            }

            auto SetApplicationName(std::string applicationName) -> ApplicationBuilder& override
            {
                MarkMutating();
                if (!HasStickyError())
                {
                    m_applicationName = std::move(applicationName);
                }
                return *this;
            }

            auto UseProfile(const HostProfile profile) -> ApplicationBuilder& override
            {
                MarkMutating();
                if (!HasStickyError())
                {
                    m_profileOverride = profile;
                }
                return *this;
            }

            auto SetDefaultVariant(std::string variantName) -> ApplicationBuilder& override
            {
                MarkMutating();
                if (!HasStickyError())
                {
                    m_variantOverride = std::move(variantName);
                }
                return *this;
            }

            [[nodiscard]] auto Services() noexcept -> ServiceCollection& override { return m_services; }
            [[nodiscard]] auto Packages() noexcept -> PackageCollection& override { return m_packages; }
            [[nodiscard]] auto Modules() noexcept -> ModuleCollection& override { return m_modules; }
            [[nodiscard]] auto Plugins() noexcept -> PluginCollection& override { return m_plugins; }
            [[nodiscard]] auto Configuration() noexcept -> ConfigurationBuilder& override { return m_configuration; }

            [[nodiscard]] auto Build() -> CoreResult<std::shared_ptr<IApplicationHost>> override
            {
                if (m_built)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(
                        MakeBuilderError("Build() may only be called once", {}, KernelErrorCode::InvalidState));
                }
                if (m_stickyError.has_value())
                {
                    return NGIN::Utilities::Unexpected<KernelError>(*m_stickyError);
                }

                std::vector<ResolvedProjectUnit> projectUnits {};
                const ResolvedProjectUnit*       rootProjectUnit = nullptr;
                if (m_project.has_value())
                {
                    if (!m_projectPath.empty())
                    {
                        std::set<std::filesystem::path> visiting {};
                        std::set<std::filesystem::path> visited {};
                        auto closure = CollectProjectClosure(m_projectPath, m_variantOverride, projectUnits, visiting, visited);
                        if (!closure)
                        {
                            return NGIN::Utilities::Unexpected<KernelError>(closure.ErrorUnsafe());
                        }
                    }
                    else
                    {
                        auto selectedVariant = FindVariant(*m_project, m_variantOverride);
                        if (!selectedVariant)
                        {
                            return NGIN::Utilities::Unexpected<KernelError>(selectedVariant.ErrorUnsafe());
                        }

                        projectUnits.push_back(ResolvedProjectUnit {
                            .manifest = *m_project,
                            .variant = selectedVariant.ValueUnsafe(),
                            .directory = m_projectDirectory,
                        });
                    }

                    if (!projectUnits.empty())
                    {
                        rootProjectUnit = &projectUnits.back();
                    }
                }

                const VariantDefinition* selectedVariant = rootProjectUnit != nullptr ? &rootProjectUnit->variant : nullptr;

                const HostProfile profile = m_profileOverride.value_or(
                    selectedVariant != nullptr ? selectedVariant->profile : HostProfile::ConsoleApp);
                const HostType hostType = ToHostType(profile);

                std::string variantName = !m_variantOverride.empty() ? m_variantOverride : std::string {};
                if (variantName.empty() && selectedVariant != nullptr)
                {
                    variantName = selectedVariant->name;
                }
                if (variantName.empty())
                {
                    variantName = !m_applicationName.empty() ? m_applicationName : "NGIN.Variant";
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

                std::vector<PackageReference> bootstrapPackages {};
                for (const auto& unit : projectUnits)
                {
                    MergePackageReferences(bootstrapPackages, unit.manifest.packageRefs);
                    MergePackageReferences(bootstrapPackages, unit.variant.packageRefs);
                }
                MergePackageReferences(bootstrapPackages, m_packageReferences);

                std::unordered_map<std::string, std::size_t> packageOrder {};
                std::unordered_map<std::string, PackageReference> directPackagesByName {};
                for (std::size_t index = 0; index < bootstrapPackages.size(); ++index)
                {
                    packageOrder[bootstrapPackages[index].name] = index;
                    directPackagesByName[bootstrapPackages[index].name] = bootstrapPackages[index];
                }

                std::unordered_map<std::string, std::string> defaultBootstrapEntryPoints {};
                for (const auto& [packageName, manifest] : m_packageManifests)
                {
                    if (manifest.bootstrap.has_value())
                    {
                        defaultBootstrapEntryPoints.emplace(packageName, manifest.bootstrap->entryPoint);
                    }
                }

                PackageBootstrapRegistryImpl bootstrapRegistry(std::move(defaultBootstrapEntryPoints));
                for (const auto registrar : m_packageRegistrars)
                {
                    registrar(bootstrapRegistry);
                    if (bootstrapRegistry.LastError().has_value())
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(*bootstrapRegistry.LastError());
                    }
                }

                std::unordered_map<std::string, BootstrapCandidate> candidatesByName {};
                std::vector<StartupWarning> bootstrapWarnings {};

                auto makeWarning = [](const std::string& packageName, const std::string& detail) {
                    return StartupWarning {
                        .subsystem = "ApplicationBuilder",
                        .module = packageName,
                        .message = "package bootstrap skipped for '" + packageName + "': " + detail,
                    };
                };

                for (const auto& request : m_explicitBootstrapRequests)
                {
                    const auto packageIt = directPackagesByName.find(request.packageName);
                    if (packageIt == directPackagesByName.end())
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(
                            MakeBuilderError(
                                "explicit bootstrap requested for package that is not a direct reference",
                                request.packageName,
                                KernelErrorCode::NotFound));
                    }

                    const PackageManifest* manifest = nullptr;
                    const auto manifestIt = m_packageManifests.find(request.packageName);
                    if (manifestIt != m_packageManifests.end())
                    {
                        manifest = &manifestIt->second;
                    }

                    std::string entryPoint {};
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
                        const auto* defaultEntry = bootstrapRegistry.FindDefault(request.packageName);
                        if (defaultEntry == nullptr)
                        {
                            return NGIN::Utilities::Unexpected<KernelError>(
                                MakeBuilderError(
                                    "unable to resolve default bootstrap entry point for package",
                                    request.packageName,
                                    KernelErrorCode::NotFound));
                        }
                        entryPoint = defaultEntry->entryPoint;
                    }

                    if (bootstrapRegistry.Find(request.packageName, entryPoint) == nullptr)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(
                            MakeBuilderError(
                                "explicit package bootstrap entry was not registered",
                                request.packageName + "::" + entryPoint,
                                KernelErrorCode::NotFound));
                    }

                    candidatesByName[request.packageName] = BootstrapCandidate {
                        .reference = packageIt->second,
                        .manifest = manifest,
                        .entryPoint = std::move(entryPoint),
                        .explicitRequest = true,
                        .orderIndex = packageOrder[request.packageName],
                    };
                }

                for (const auto& reference : bootstrapPackages)
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

                    const PackageManifest& manifest = manifestIt->second;
                    if (!manifest.bootstrap.has_value() || !manifest.bootstrap->autoApply)
                    {
                        continue;
                    }

                    const auto* entry = bootstrapRegistry.Find(reference.name, manifest.bootstrap->entryPoint);
                    if (entry == nullptr)
                    {
                        if (reference.optional)
                        {
                            bootstrapWarnings.push_back(makeWarning(
                                reference.name,
                                "manifest entry point '" + manifest.bootstrap->entryPoint + "' was not registered"));
                            continue;
                        }

                        return NGIN::Utilities::Unexpected<KernelError>(
                            MakeBuilderError(
                                "required package bootstrap entry was not registered",
                                reference.name + "::" + manifest.bootstrap->entryPoint,
                                KernelErrorCode::NotFound));
                    }

                    candidatesByName.emplace(
                        reference.name,
                        BootstrapCandidate {
                            .reference = reference,
                            .manifest = &manifest,
                            .entryPoint = manifest.bootstrap->entryPoint,
                            .explicitRequest = false,
                            .orderIndex = packageOrder[reference.name],
                        });
                }

                std::unordered_map<std::string, std::size_t> indegree {};
                std::unordered_map<std::string, std::vector<std::string>> adjacency {};
                std::vector<std::string> ready {};
                std::vector<std::string> orderedCandidateNames {};
                orderedCandidateNames.reserve(candidatesByName.size());

                for (const auto& [packageName, candidate] : candidatesByName)
                {
                    (void)candidate;
                    indegree.emplace(packageName, 0);
                }

                for (const auto& [packageName, candidate] : candidatesByName)
                {
                    if (candidate.manifest == nullptr)
                    {
                        continue;
                    }

                    for (const auto& dependency : candidate.manifest->dependencies)
                    {
                        if (!candidatesByName.contains(dependency.name))
                        {
                            continue;
                        }

                        adjacency[dependency.name].push_back(packageName);
                        ++indegree[packageName];
                    }
                }

                for (const auto& [packageName, count] : indegree)
                {
                    if (count == 0)
                    {
                        ready.push_back(packageName);
                    }
                }

                auto compareByOrder = [&](const std::string& lhs, const std::string& rhs) {
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

                    for (const auto& dependent : adjacencyIt->second)
                    {
                        auto& count = indegree[dependent];
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
                        MakeBuilderError(
                            "package bootstrap dependency cycle detected",
                            {},
                            KernelErrorCode::DependencyCycle));
                }

                for (const auto& packageName : orderedCandidateNames)
                {
                    const auto& candidate = candidatesByName.at(packageName);
                    const auto* entry = bootstrapRegistry.Find(candidate.reference.name, candidate.entryPoint);
                    if (entry == nullptr || entry->fn == nullptr)
                    {
                        const std::string detail = "bootstrap entry '" + candidate.entryPoint + "' was not registered";
                        if (candidate.explicitRequest || !candidate.reference.optional)
                        {
                            return NGIN::Utilities::Unexpected<KernelError>(
                                MakeBuilderError(detail, candidate.reference.name, KernelErrorCode::NotFound));
                        }

                        bootstrapWarnings.push_back(makeWarning(candidate.reference.name, detail));
                        continue;
                    }

                    PackageBootstrapContextImpl context(
                        candidate.reference.name,
                        variantName,
                        profile,
                        m_services,
                        m_packages,
                        m_modules,
                        m_plugins,
                        m_configuration);

                    auto bootstrapResult = entry->fn(context);
                    if (bootstrapResult)
                    {
                        continue;
                    }

                    if (candidate.explicitRequest || !candidate.reference.optional)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(bootstrapResult.ErrorUnsafe());
                    }

                    bootstrapWarnings.push_back(makeWarning(
                        candidate.reference.name,
                        bootstrapResult.ErrorUnsafe().message));
                }

                std::vector<PackageReference> packages {};
                for (const auto& unit : projectUnits)
                {
                    MergePackageReferences(packages, unit.manifest.packageRefs);
                    MergePackageReferences(packages, unit.variant.packageRefs);
                }
                MergePackageReferences(packages, m_packageReferences);

                std::vector<std::string> enabledModules {};
                for (const auto& unit : projectUnits)
                {
                    AppendUniqueStrings(enabledModules, unit.manifest.runtime.enableModules);
                    AppendUniqueStrings(enabledModules, unit.variant.enableModules);
                }
                AppendUniqueStrings(enabledModules, m_enabledModules);

                std::vector<std::string> disabledModules {};
                for (const auto& unit : projectUnits)
                {
                    AppendUniqueStrings(disabledModules, unit.manifest.runtime.disableModules);
                    AppendUniqueStrings(disabledModules, unit.variant.disableModules);
                }
                AppendUniqueStrings(disabledModules, m_disabledModules);
                const std::set<std::string> disabledModuleSet(disabledModules.begin(), disabledModules.end());

                enabledModules.erase(
                    std::remove_if(
                        enabledModules.begin(),
                        enabledModules.end(),
                        [&](const std::string& name) { return disabledModuleSet.contains(name); }),
                    enabledModules.end());

                std::vector<std::string> enabledPlugins {};
                AppendUniqueStrings(enabledPlugins, m_enabledPlugins);

                std::vector<std::string> disabledPlugins {};
                AppendUniqueStrings(disabledPlugins, m_disabledPlugins);
                const std::set<std::string> disabledPluginSet(disabledPlugins.begin(), disabledPlugins.end());

                enabledPlugins.erase(
                    std::remove_if(
                        enabledPlugins.begin(),
                        enabledPlugins.end(),
                        [&](const std::string& name) { return disabledPluginSet.contains(name); }),
                    enabledPlugins.end());

                std::vector<std::string> configSources {};
                for (const auto& unit : projectUnits)
                {
                    for (const auto& source : unit.manifest.configSources)
                    {
                        const auto resolved = ResolveWorkingDirectory(source, unit.directory);
                        AppendUnique(configSources, resolved);
                    }
                    for (const auto& source : unit.variant.configSources)
                    {
                        const auto resolved = ResolveWorkingDirectory(source, unit.directory);
                        AppendUnique(configSources, resolved);
                    }
                }
                AppendUniqueStrings(configSources, m_configSources);

                std::vector<std::string> pluginSearchPaths {};
                AppendUniqueStrings(pluginSearchPaths, m_pluginSearchPaths);

                std::string environmentName = m_environmentName;
                if (environmentName.empty() && selectedVariant != nullptr)
                {
                    environmentName = selectedVariant->environmentName;
                }

                std::string workingDirectory = m_workingDirectory;
                if (workingDirectory.empty() && selectedVariant != nullptr)
                {
                    workingDirectory = selectedVariant->workingDirectory;
                }
                if (workingDirectory.empty() && !m_projectDirectory.empty())
                {
                    workingDirectory = m_projectDirectory.string();
                }
                workingDirectory = ResolveWorkingDirectory(workingDirectory, m_projectDirectory);

                auto moduleCatalog = BuildCatalogFrom(m_moduleRegistrations, disabledModuleSet);
                if (!moduleCatalog)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(moduleCatalog.ErrorUnsafe());
                }

                StartupReport metadataReport {};
                metadataReport.hostName = applicationName;
                metadataReport.targetName = variantName;
                metadataReport.hostType = std::string(ToString(hostType));
                metadataReport.warnings = bootstrapWarnings;
                for (const auto& reference : packages)
                {
                    metadataReport.resolvedPackages.push_back(reference.name);
                }
                metadataReport.resolvedPlugins = enabledPlugins;

                KernelHostConfig hostConfig {};
                hostConfig.hostName = applicationName;
                hostConfig.hostType = hostType;
                hostConfig.platformName = selectedVariant != nullptr ? selectedVariant->platform : "linux-x64";
                hostConfig.platformVersion = SemanticVersion {0, 1, 0, {}};
                hostConfig.targetName = variantName;
                hostConfig.workingDirectory = workingDirectory;
                hostConfig.configSources = configSources;
                hostConfig.pluginSearchPaths = pluginSearchPaths;
                hostConfig.enableDynamicPlugins = false;
                hostConfig.enableReflection = selectedVariant != nullptr ? selectedVariant->enableReflection : false;
                hostConfig.commandLineArgs = m_commandLineArgs;
                hostConfig.environmentName = environmentName;
                hostConfig.requestedModules = enabledModules;
                hostConfig.moduleCatalog = moduleCatalog.ValueUnsafe();

                const auto pendingServices = m_pendingServices;
                const bool addDefaults = m_addDefaultServices;
                const bool addLogging = m_addLogging;
                const bool addConfiguration = m_addConfiguration;

                hostConfig.configureServices =
                    [pendingServices, addDefaults, addLogging, addConfiguration, variantName](KernelBootstrapContext& context)
                    -> CoreResult<void>
                {
                    ServiceScopeId hostScope = ServiceScopeId::Global();
                    const bool requiresHostScope = std::any_of(
                        pendingServices.begin(),
                        pendingServices.end(),
                        [](const PendingServiceRegistration& registration) {
                            return registration.lifetime != ServiceLifetime::Singleton;
                        });

                    if (requiresHostScope)
                    {
                        auto scope = context.services->BeginScope(ServiceScopeKind::Host, variantName);
                        if (!scope)
                        {
                            return NGIN::Utilities::Unexpected<KernelError>(scope.ErrorUnsafe());
                        }
                        hostScope = scope.ValueUnsafe();
                    }

                    if (addDefaults)
                    {
                        auto result = context.services->RegisterInstance(
                            "Core.Services",
                            NGIN::Utilities::Any<>(context.services));
                        if (!result)
                        {
                            return NGIN::Utilities::Unexpected<KernelError>(result.ErrorUnsafe());
                        }

                        result = context.services->RegisterInstance(
                            "Core.Events",
                            NGIN::Utilities::Any<>(context.events));
                        if (!result)
                        {
                            return NGIN::Utilities::Unexpected<KernelError>(result.ErrorUnsafe());
                        }

                        result = context.services->RegisterInstance(
                            "Core.Tasks",
                            NGIN::Utilities::Any<>(context.tasks));
                        if (!result)
                        {
                            return NGIN::Utilities::Unexpected<KernelError>(result.ErrorUnsafe());
                        }
                    }

                    if (addConfiguration)
                    {
                        auto result = context.services->RegisterInstance(
                            "Core.Configuration",
                            NGIN::Utilities::Any<>(context.config));
                        if (!result)
                        {
                            return NGIN::Utilities::Unexpected<KernelError>(result.ErrorUnsafe());
                        }
                    }

                    if (addLogging && context.loggerRegistry != nullptr)
                    {
                        auto result = context.services->RegisterInstance(
                            "Core.Logging",
                            NGIN::Utilities::Any<>(context.loggerRegistry));
                        if (!result)
                        {
                            return NGIN::Utilities::Unexpected<KernelError>(result.ErrorUnsafe());
                        }
                    }

                    for (const auto& registration : pendingServices)
                    {
                        ServiceRegistrationOptions options {};
                        options.lifetime = registration.lifetime;
                        options.ownerScope = registration.lifetime == ServiceLifetime::Singleton
                            ? ServiceScopeId::Global()
                            : hostScope;
                        options.metadata = registration.metadata;

                        CoreResult<void> result {};
                        if (registration.instance.has_value())
                        {
                            result = context.services->RegisterInstance(
                                registration.key,
                                *registration.instance,
                                options);
                        }
                        else
                        {
                            result = context.services->RegisterFactory(
                                registration.key,
                                *registration.factory,
                                options);
                        }

                        if (!result)
                        {
                            return NGIN::Utilities::Unexpected<KernelError>(result.ErrorUnsafe());
                        }
                    }

                    return {};
                };

                auto kernel = CreateKernel(hostConfig);
                if (!kernel)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(kernel.ErrorUnsafe());
                }

                m_built = true;
                std::shared_ptr<IApplicationHost> host = std::make_shared<ApplicationHostImpl>(
                    kernel.ValueUnsafe(),
                    profile,
                    std::move(metadataReport));
                return host;
            }

            void MarkMutating()
            {
                if (m_built && !m_stickyError.has_value())
                {
                    m_stickyError = MakeBuilderError(
                        "builder can no longer be modified after Build()",
                        {},
                        KernelErrorCode::InvalidState);
                }
            }

            [[nodiscard]] auto HasStickyError() const noexcept -> bool
            {
                return m_stickyError.has_value();
            }

            std::vector<PendingServiceRegistration>      m_pendingServices {};
            std::vector<PackageReference>                m_packageReferences {};
            std::unordered_map<std::string, PackageManifest> m_packageManifests {};
            std::vector<PackageBootstrapRegistrarFn>     m_packageRegistrars {};
            std::vector<PackageBootstrapRequest>         m_explicitBootstrapRequests {};
            std::vector<StaticModuleRegistration>        m_moduleRegistrations {};
            std::vector<std::string>                     m_enabledModules {};
            std::vector<std::string>                     m_disabledModules {};
            std::vector<std::string>                     m_enabledPlugins {};
            std::vector<std::string>                     m_disabledPlugins {};
            std::vector<std::string>                     m_pluginSearchPaths {};
            std::vector<std::string>                     m_configSources {};
            std::vector<std::string>                     m_commandLineArgs {};
            std::optional<ProjectManifest>               m_project {};
            std::filesystem::path                        m_projectPath {};
            std::filesystem::path                        m_projectDirectory {};
            std::optional<KernelError>                   m_stickyError {};
            std::optional<HostProfile>                   m_profileOverride {};
            std::string                                  m_applicationName {};
            std::string                                  m_variantOverride {};
            std::string                                  m_environmentName {};
            std::string                                  m_workingDirectory {};
            bool                                         m_addDefaultServices {false};
            bool                                         m_addLogging {false};
            bool                                         m_addConfiguration {false};
            bool                                         m_built {false};
            ServiceCollectionImpl                        m_services;
            PackageCollectionImpl                        m_packages;
            ModuleCollectionImpl                         m_modules;
            PluginCollectionImpl                         m_plugins;
            ConfigurationBuilderImpl                     m_configuration;
        };

        auto ServiceCollectionImpl::AddSingleton(
            std::string key,
            NGIN::Utilities::Any<> service,
            ServiceMetadata metadata) -> ServiceCollection&
        {
            m_owner.MarkMutating();
            if (!m_owner.HasStickyError())
            {
                m_owner.m_pendingServices.push_back(PendingServiceRegistration {
                    .key = std::move(key),
                    .instance = std::move(service),
                    .factory = std::nullopt,
                    .lifetime = ServiceLifetime::Singleton,
                    .metadata = std::move(metadata),
                });
            }
            return *this;
        }

        auto ServiceCollectionImpl::AddFactory(
            std::string key,
            ServiceFactory factory,
            const ServiceLifetime lifetime,
            ServiceMetadata metadata) -> ServiceCollection&
        {
            m_owner.MarkMutating();
            if (!m_owner.HasStickyError())
            {
                m_owner.m_pendingServices.push_back(PendingServiceRegistration {
                    .key = std::move(key),
                    .instance = std::nullopt,
                    .factory = std::move(factory),
                    .lifetime = lifetime,
                    .metadata = std::move(metadata),
                });
            }
            return *this;
        }

        auto ServiceCollectionImpl::AddScoped(
            std::string key,
            ServiceFactory factory,
            ServiceMetadata metadata) -> ServiceCollection&
        {
            return AddFactory(std::move(key), std::move(factory), ServiceLifetime::Scoped, std::move(metadata));
        }

        auto ServiceCollectionImpl::AddTransient(
            std::string key,
            ServiceFactory factory,
            ServiceMetadata metadata) -> ServiceCollection&
        {
            return AddFactory(std::move(key), std::move(factory), ServiceLifetime::Transient, std::move(metadata));
        }

        auto ServiceCollectionImpl::AddDefaults() -> ServiceCollection&
        {
            m_owner.MarkMutating();
            if (!m_owner.HasStickyError())
            {
                m_owner.m_addDefaultServices = true;
            }
            return *this;
        }

        auto ServiceCollectionImpl::AddLogging() -> ServiceCollection&
        {
            m_owner.MarkMutating();
            if (!m_owner.HasStickyError())
            {
                m_owner.m_addLogging = true;
            }
            return *this;
        }

        auto ServiceCollectionImpl::AddConfiguration() -> ServiceCollection&
        {
            m_owner.MarkMutating();
            if (!m_owner.HasStickyError())
            {
                m_owner.m_addConfiguration = true;
            }
            return *this;
        }

        auto ServiceCollectionImpl::Clear() -> ServiceCollection&
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

        auto PackageCollectionImpl::Add(PackageReference reference) -> PackageCollection&
        {
            m_owner.MarkMutating();
            if (!m_owner.HasStickyError())
            {
                m_owner.m_packageReferences.push_back(std::move(reference));
            }
            return *this;
        }

        auto PackageCollectionImpl::AddManifest(PackageManifest manifest) -> PackageCollection&
        {
            m_owner.MarkMutating();
            if (m_owner.HasStickyError())
            {
                return *this;
            }

            if (manifest.name.empty())
            {
                m_owner.m_stickyError = MakeBuilderError(
                    "package manifest name must not be empty",
                    {},
                    KernelErrorCode::InvalidArgument);
                return *this;
            }

            m_owner.m_packageManifests[manifest.name] = std::move(manifest);
            return *this;
        }

        auto PackageCollectionImpl::AddManifestFile(std::string path) -> PackageCollection&
        {
            m_owner.MarkMutating();
            if (m_owner.HasStickyError())
            {
                return *this;
            }

            std::filesystem::path manifestPath(path);
            if (manifestPath.is_relative() && !m_owner.m_projectDirectory.empty())
            {
                manifestPath = m_owner.m_projectDirectory / manifestPath;
            }

            auto fileText = ReadTextFile(manifestPath, "package");
            if (!fileText)
            {
                m_owner.m_stickyError = fileText.ErrorUnsafe();
                return *this;
            }

            auto manifest = ParsePackageManifestText(fileText.ValueUnsafe());
            if (!manifest)
            {
                m_owner.m_stickyError = manifest.ErrorUnsafe();
                return *this;
            }

            m_owner.m_packageManifests[manifest.ValueUnsafe().name] = manifest.ValueUnsafe();
            return *this;
        }

        auto PackageCollectionImpl::RegisterLinkedRegistrar(PackageBootstrapRegistrarFn registrar) -> PackageCollection&
        {
            m_owner.MarkMutating();
            if (m_owner.HasStickyError())
            {
                return *this;
            }

            if (registrar == nullptr)
            {
                m_owner.m_stickyError = MakeBuilderError(
                    "package bootstrap registrar must not be null",
                    {},
                    KernelErrorCode::InvalidArgument);
                return *this;
            }

            m_owner.m_packageRegistrars.push_back(registrar);
            return *this;
        }

        auto PackageCollectionImpl::ApplyBootstrap(std::string packageName) -> PackageCollection&
        {
            m_owner.MarkMutating();
            if (!m_owner.HasStickyError())
            {
                const auto duplicate = std::find_if(
                    m_owner.m_explicitBootstrapRequests.begin(),
                    m_owner.m_explicitBootstrapRequests.end(),
                    [&](const PackageBootstrapRequest& request) {
                        return request.packageName == packageName && !request.entryPoint.has_value();
                    });

                if (duplicate == m_owner.m_explicitBootstrapRequests.end())
                {
                    m_owner.m_explicitBootstrapRequests.push_back(PackageBootstrapRequest {
                        .packageName = std::move(packageName),
                        .entryPoint = std::nullopt,
                    });
                }
            }
            return *this;
        }

        auto PackageCollectionImpl::ApplyBootstrap(std::string packageName, std::string entryPoint) -> PackageCollection&
        {
            m_owner.MarkMutating();
            if (!m_owner.HasStickyError())
            {
                const auto duplicate = std::find_if(
                    m_owner.m_explicitBootstrapRequests.begin(),
                    m_owner.m_explicitBootstrapRequests.end(),
                    [&](const PackageBootstrapRequest& request) {
                        return request.packageName == packageName
                            && request.entryPoint.has_value()
                            && *request.entryPoint == entryPoint;
                    });

                if (duplicate == m_owner.m_explicitBootstrapRequests.end())
                {
                    m_owner.m_explicitBootstrapRequests.push_back(PackageBootstrapRequest {
                        .packageName = std::move(packageName),
                        .entryPoint = std::move(entryPoint),
                    });
                }
            }
            return *this;
        }

        auto PackageCollectionImpl::Clear() -> PackageCollection&
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

        auto ModuleCollectionImpl::Register(StaticModuleRegistration registration) -> ModuleCollection&
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
                    registration.descriptor.name,
                    KernelErrorCode::InvalidArgument);
                return *this;
            }

            AppendOrReplaceModuleRegistration(m_owner.m_moduleRegistrations, std::move(registration));
            return *this;
        }

        auto ModuleCollectionImpl::Enable(std::string moduleName) -> ModuleCollection&
        {
            m_owner.MarkMutating();
            if (!m_owner.HasStickyError())
            {
                AppendUnique(m_owner.m_enabledModules, moduleName);
            }
            return *this;
        }

        auto ModuleCollectionImpl::Disable(std::string moduleName) -> ModuleCollection&
        {
            m_owner.MarkMutating();
            if (!m_owner.HasStickyError())
            {
                AppendUnique(m_owner.m_disabledModules, moduleName);
            }
            return *this;
        }

        auto ModuleCollectionImpl::Clear() -> ModuleCollection&
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

        auto PluginCollectionImpl::Enable(std::string pluginName) -> PluginCollection&
        {
            m_owner.MarkMutating();
            if (!m_owner.HasStickyError())
            {
                AppendUnique(m_owner.m_enabledPlugins, pluginName);
            }
            return *this;
        }

        auto PluginCollectionImpl::Disable(std::string pluginName) -> PluginCollection&
        {
            m_owner.MarkMutating();
            if (!m_owner.HasStickyError())
            {
                AppendUnique(m_owner.m_disabledPlugins, pluginName);
            }
            return *this;
        }

        auto PluginCollectionImpl::AddSearchPath(std::string path) -> PluginCollection&
        {
            m_owner.MarkMutating();
            if (!m_owner.HasStickyError())
            {
                AppendUnique(m_owner.m_pluginSearchPaths, path);
            }
            return *this;
        }

        auto PluginCollectionImpl::Clear() -> PluginCollection&
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

        auto ConfigurationBuilderImpl::AddSource(std::string path) -> ConfigurationBuilder&
        {
            m_owner.MarkMutating();
            if (!m_owner.HasStickyError())
            {
                AppendUnique(m_owner.m_configSources, path);
            }
            return *this;
        }

        auto ConfigurationBuilderImpl::SetEnvironmentName(std::string environmentName) -> ConfigurationBuilder&
        {
            m_owner.MarkMutating();
            if (!m_owner.HasStickyError())
            {
                m_owner.m_environmentName = std::move(environmentName);
            }
            return *this;
        }

        auto ConfigurationBuilderImpl::SetWorkingDirectory(std::string workingDirectory) -> ConfigurationBuilder&
        {
            m_owner.MarkMutating();
            if (!m_owner.HasStickyError())
            {
                m_owner.m_workingDirectory = std::move(workingDirectory);
            }
            return *this;
        }

        auto ConfigurationBuilderImpl::Clear() -> ConfigurationBuilder&
        {
            m_owner.MarkMutating();
            if (!m_owner.HasStickyError())
            {
                m_owner.m_configSources.clear();
                m_owner.m_environmentName.clear();
                m_owner.m_workingDirectory.clear();
            }
            return *this;
        }
    }

    auto CreateApplicationBuilder(const int argc, char** argv) -> std::unique_ptr<ApplicationBuilder>
    {
        return std::make_unique<ApplicationBuilderImpl>(argc, argv);
    }
}
