#include <NGIN/Core/Loader.hpp>

#include <NGIN/Serialization/XML/XmlParser.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>

namespace NGIN::Core
{
    namespace
    {
        using XmlDocument = NGIN::Serialization::XmlDocument;
        using XmlElement = NGIN::Serialization::XmlElement;
        using XmlNode = NGIN::Serialization::XmlNode;
        using XmlParseOptions = NGIN::Serialization::XmlParseOptions;
        using XmlParser = NGIN::Serialization::XmlParser;

        [[nodiscard]] auto ParseModuleType(const std::string_view text) -> ModuleType
        {
            if (text == "Runtime") return ModuleType::Runtime;
            if (text == "Editor") return ModuleType::Editor;
            if (text == "Program") return ModuleType::Program;
            if (text == "Developer") return ModuleType::Developer;
            if (text == "ThirdParty") return ModuleType::ThirdParty;
            return ModuleType::Runtime;
        }

        [[nodiscard]] auto ParseModuleFamily(const std::string_view text) -> ModuleFamily
        {
            if (text == "Base") return ModuleFamily::Base;
            if (text == "Reflection") return ModuleFamily::Reflection;
            if (text == "Core") return ModuleFamily::Core;
            if (text == "Platform") return ModuleFamily::Platform;
            if (text == "Editor") return ModuleFamily::Editor;
            if (text == "Domain") return ModuleFamily::Domain;
            if (text == "App") return ModuleFamily::App;
            return ModuleFamily::Core;
        }

        [[nodiscard]] auto ParseStartupStage(const std::string_view text) -> std::optional<StartupStage>
        {
            if (text == "Foundation") return StartupStage::Foundation;
            if (text == "Platform") return StartupStage::Platform;
            if (text == "Services") return StartupStage::Services;
            if (text == "Features") return StartupStage::Features;
            if (text == "Presentation") return StartupStage::Presentation;
            return std::nullopt;
        }

        [[nodiscard]] auto ParseHostType(const std::string_view text) -> std::optional<HostType>
        {
            if (text == "GuiApp") return HostType::GuiApp;
            if (text == "Game") return HostType::Game;
            if (text == "Editor") return HostType::Editor;
            if (text == "Service") return HostType::Service;
            if (text == "ConsoleApp") return HostType::ConsoleApp;
            if (text == "TestHost") return HostType::TestHost;
            return std::nullopt;
        }

        [[nodiscard]] auto Attribute(const XmlElement& element, const std::string_view key) -> std::optional<std::string>
        {
            const auto* attribute = element.FindAttribute(key);
            if (attribute == nullptr)
            {
                return std::nullopt;
            }
            return std::string(attribute->value);
        }

        [[nodiscard]] auto FindChild(const XmlElement& element, const std::string_view name) -> const XmlElement*
        {
            for (NGIN::UIntSize index = 0; index < element.children.Size(); ++index)
            {
                const auto& child = element.children[index];
                if (child.type == XmlNode::Type::Element && child.element != nullptr && child.element->name == name)
                {
                    return child.element;
                }
            }
            return nullptr;
        }

        [[nodiscard]] auto ChildElements(const XmlElement& element, const std::string_view name = {}) -> std::vector<const XmlElement*>
        {
            std::vector<const XmlElement*> out {};
            out.reserve(static_cast<std::size_t>(element.children.Size()));
            for (NGIN::UIntSize index = 0; index < element.children.Size(); ++index)
            {
                const auto& child = element.children[index];
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

        [[nodiscard]] auto ParseBoolAttribute(const XmlElement& element, const std::string_view key, const bool defaultValue = false) -> bool
        {
            const auto value = Attribute(element, key);
            if (!value.has_value())
            {
                return defaultValue;
            }
            return value.value() == "true" || value.value() == "1" || value.value() == "yes";
        }

        template<typename T>
        void ReadStringRefs(const XmlElement* element, const std::string_view childName, T& out)
        {
            if (element == nullptr)
            {
                return;
            }
            for (const auto* child : ChildElements(*element, childName))
            {
                const auto value = Attribute(*child, "Name");
                if (value.has_value())
                {
                    out.emplace_back(value.value());
                }
            }
        }

        [[nodiscard]] auto ParseDescriptorFromXml(
            const std::string& filePath,
            const std::string_view xmlText,
            ModuleDescriptor& out) noexcept -> CoreResult<void>
        {
            XmlParseOptions options {};
            options.decodeEntities = true;
            options.arenaBytes = std::max<NGIN::UIntSize>(8192, static_cast<NGIN::UIntSize>(xmlText.size() * 8 + 4096));
            auto parsed = XmlParser::Parse(xmlText, options);
            if (!parsed)
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(
                        KernelErrorCode::ConfigFailure,
                        "Loader",
                        {},
                        "failed to parse plugin descriptor: " + filePath));
            }

            const XmlDocument& doc = parsed.ValueUnsafe();
            const auto* root = doc.Root();
            if (root == nullptr || root->name != "Module")
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(
                        KernelErrorCode::InvalidArgument,
                        "Loader",
                        {},
                        "plugin descriptor root must be <Module>: " + filePath));
            }

            const auto nameValue = Attribute(*root, "Name");
            if (!nameValue.has_value())
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(
                        KernelErrorCode::InvalidArgument,
                        "Loader",
                        {},
                        "plugin descriptor missing attribute 'Name': " + filePath));
            }

            out.name = nameValue.value();
            out.entryKind = ModuleEntryKind::Dynamic;

            if (const auto family = Attribute(*root, "Family"); family.has_value())
            {
                out.family = ParseModuleFamily(family.value());
            }

            if (const auto type = Attribute(*root, "Type"); type.has_value())
            {
                out.type = ParseModuleType(type.value());
            }

            if (const auto stage = Attribute(*root, "StartupStage"); stage.has_value())
            {
                const auto parsedStage = ParseStartupStage(stage.value());
                if (!parsedStage.has_value())
                {
                    return NGIN::Utilities::Unexpected<KernelError>(
                        MakeKernelError(
                            KernelErrorCode::InvalidArgument,
                            "Loader",
                            {},
                            "plugin descriptor contains unknown startup stage '" + stage.value() + "': " + filePath));
                }
                out.startupStage = parsedStage.value();
            }

            if (const auto version = Attribute(*root, "Version"); version.has_value())
            {
                auto parsedVersion = ParseSemanticVersion(version.value());
                if (!parsedVersion)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(parsedVersion.ErrorUnsafe());
                }
                out.version = parsedVersion.ValueUnsafe();
            }

            if (const auto range = Attribute(*root, "CompatiblePlatformRange"); range.has_value())
            {
                auto parsedRange = ParseVersionRange(range.value());
                if (!parsedRange)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(parsedRange.ErrorUnsafe());
                }
                out.compatiblePlatformRange = parsedRange.ValueUnsafe();
            }

            ReadStringRefs(FindChild(*root, "Platforms"), "Platform", out.platforms);
            ReadStringRefs(FindChild(*root, "ProvidesServices"), "Service", out.providesServices);
            ReadStringRefs(FindChild(*root, "RequiresServices"), "Service", out.requiresServices);
            ReadStringRefs(FindChild(*root, "Capabilities"), "Capability", out.capabilities);

            if (const auto* supportedHosts = FindChild(*root, "SupportedHosts"))
            {
                for (const auto* hostElement : ChildElements(*supportedHosts, "Host"))
                {
                    const auto hostName = Attribute(*hostElement, "Name");
                    if (!hostName.has_value())
                    {
                        continue;
                    }

                    const auto parsedHost = ParseHostType(hostName.value());
                    if (!parsedHost.has_value())
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(
                            MakeKernelError(
                                KernelErrorCode::InvalidArgument,
                                "Loader",
                                {},
                                "plugin descriptor contains unknown supported host '" + hostName.value() + "': " + filePath));
                    }

                    out.supportedHosts.push_back(parsedHost.value());
                }
            }

            out.reflectionRequired = ParseBoolAttribute(*root, "ReflectionRequired", false);

            if (const auto* dependencies = FindChild(*root, "Dependencies"))
            {
                for (const auto* dependencyElement : ChildElements(*dependencies, "Dependency"))
                {
                    const auto dependencyName = Attribute(*dependencyElement, "Name");
                    if (!dependencyName.has_value())
                    {
                        continue;
                    }

                    DependencyDescriptor dep {};
                    dep.name = dependencyName.value();

                    dep.optional = ParseBoolAttribute(*dependencyElement, "Optional", false);

                    if (const auto depRange = Attribute(*dependencyElement, "RequiredVersion"); depRange.has_value())
                    {
                        auto parsedDepRange = ParseVersionRange(depRange.value());
                        if (!parsedDepRange)
                        {
                            return NGIN::Utilities::Unexpected<KernelError>(parsedDepRange.ErrorUnsafe());
                        }
                        dep.requiredVersion = parsedDepRange.ValueUnsafe();
                    }

                    out.dependencies.push_back(std::move(dep));
                }
            }

            return CoreResult<void> {};
        }
    }

    auto StaticModuleCatalog::Register(StaticModuleRegistration registration) noexcept -> CoreResult<void>
    {
        if (registration.descriptor.name.empty())
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Loader", {}, "static module name cannot be empty"));
        }
        if (!registration.factory)
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Loader", registration.descriptor.name, "static module factory is empty"));
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& existing : m_entries)
        {
            if (existing.descriptor.name == registration.descriptor.name)
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(KernelErrorCode::AlreadyExists, "Loader", registration.descriptor.name, "duplicate static module registration"));
            }
        }

        m_entries.emplace_back(std::move(registration));
        return CoreResult<void> {};
    }

    void StaticModuleCatalog::Clear() noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_entries.clear();
    }

    auto StaticModuleCatalog::Snapshot() const -> std::vector<StaticModuleRegistration>
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_entries;
    }

    FilesystemPluginCatalog::FilesystemPluginCatalog(std::vector<std::string> searchPaths)
        : m_searchPaths(std::move(searchPaths))
    {
    }

    auto FilesystemPluginCatalog::CollectDescriptors(std::vector<ModuleDescriptor>& out) noexcept -> CoreResult<void>
    {
        try
        {
            for (const auto& rawPath : m_searchPaths)
            {
                if (rawPath.empty())
                {
                    continue;
                }

                std::error_code ec;
                const std::filesystem::path searchPath(rawPath);
                if (!std::filesystem::exists(searchPath, ec) || !std::filesystem::is_directory(searchPath, ec))
                {
                    continue;
                }

                for (std::filesystem::recursive_directory_iterator it(searchPath, ec); it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
                {
                    if (ec)
                    {
                        break;
                    }
                    if (!it->is_regular_file())
                    {
                        continue;
                    }

                    const auto& path = it->path();
                    if (path.extension() != ".xml")
                    {
                        continue;
                    }

                    const auto filename = path.filename().string();
                    if (!(filename.ends_with(".module.xml") || filename.ends_with(".plugin-module.xml")))
                    {
                        continue;
                    }

                    std::ifstream input(path, std::ios::binary);
                    if (!input.good())
                    {
                        continue;
                    }

                    std::ostringstream stream;
                    stream << input.rdbuf();

                    ModuleDescriptor descriptor {};
                    auto parse = ParseDescriptorFromXml(path.string(), stream.str(), descriptor);
                    if (!parse)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(parse.ErrorUnsafe());
                    }

                    if (descriptor.pluginName.empty())
                    {
                        descriptor.pluginName = path.parent_path().filename().string();
                    }
                    descriptor.entryKind = ModuleEntryKind::Dynamic;
                    out.push_back(std::move(descriptor));
                }
            }

            std::sort(out.begin(), out.end(), [](const ModuleDescriptor& lhs, const ModuleDescriptor& rhs) {
                return lhs.name < rhs.name;
            });
        }
        catch (...)
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InternalError, "Loader", {}, "filesystem plugin scan failed"));
        }

        return CoreResult<void> {};
    }

    auto CreateStaticModuleCatalog() noexcept -> NGIN::Memory::Shared<IModuleCatalog>
    {
        return NGIN::Memory::MakeSharedAs<IModuleCatalog, StaticModuleCatalog>();
    }
}
