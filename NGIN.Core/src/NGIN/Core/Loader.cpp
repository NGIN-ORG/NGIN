#include <NGIN/Core/Loader.hpp>

#include <NGIN/Serialization/JSON/JsonParser.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

namespace NGIN::Core
{
    namespace
    {
        using NGIN::Serialization::JsonDocument;
        using NGIN::Serialization::JsonObject;
        using NGIN::Serialization::JsonValue;

        std::mutex                               g_legacyCatalogMutex;
        NGIN::Memory::Shared<StaticModuleCatalog> g_legacyCatalog;

        [[nodiscard]] auto GetLegacyCatalog() -> NGIN::Memory::Shared<StaticModuleCatalog>
        {
            std::lock_guard<std::mutex> lock(g_legacyCatalogMutex);
            if (!g_legacyCatalog)
            {
                g_legacyCatalog = NGIN::Memory::MakeShared<StaticModuleCatalog>();
            }
            return g_legacyCatalog;
        }

        [[nodiscard]] auto FindMember(const JsonObject& object, const std::string_view key) -> const JsonValue*
        {
            return object.Find(key);
        }

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

        [[nodiscard]] auto ParseLoadPhase(const std::string_view text) -> LoadPhase
        {
            if (text == "Bootstrap") return LoadPhase::Bootstrap;
            if (text == "Platform") return LoadPhase::Platform;
            if (text == "CoreServices") return LoadPhase::CoreServices;
            if (text == "Data") return LoadPhase::Data;
            if (text == "Domain") return LoadPhase::Domain;
            if (text == "Application") return LoadPhase::Application;
            if (text == "Editor") return LoadPhase::Editor;
            return LoadPhase::CoreServices;
        }

        template<typename T>
        void ReadStringArray(const JsonObject& object, const std::string_view key, T& out)
        {
            const auto* value = FindMember(object, key);
            if (!value || !value->IsArray())
            {
                return;
            }
            for (const auto& item : value->AsArray().values)
            {
                if (item.IsString())
                {
                    out.emplace_back(item.AsString());
                }
            }
        }

        [[nodiscard]] auto ParseDescriptorFromJson(
            const std::string& filePath,
            const std::string_view jsonText,
            ModuleDescriptor& out) noexcept -> CoreResult<void>
        {
            auto parsed = NGIN::Serialization::JsonParser::Parse(jsonText);
            if (!parsed)
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(
                        KernelErrorCode::ConfigFailure,
                        "Loader",
                        {},
                        "failed to parse plugin descriptor: " + filePath));
            }

            const JsonDocument& doc = parsed.ValueUnsafe();
            const auto& root = doc.Root();
            if (!root.IsObject())
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(
                        KernelErrorCode::InvalidArgument,
                        "Loader",
                        {},
                        "plugin descriptor root must be object: " + filePath));
            }

            const auto& object = root.AsObject();
            const auto* nameValue = FindMember(object, "name");
            if (!nameValue || !nameValue->IsString())
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(
                        KernelErrorCode::InvalidArgument,
                        "Loader",
                        {},
                        "plugin descriptor missing string field 'name': " + filePath));
            }

            out.name = std::string(nameValue->AsString());
            out.entryKind = ModuleEntryKind::Dynamic;

            if (const auto* family = FindMember(object, "family"); family && family->IsString())
            {
                out.family = ParseModuleFamily(family->AsString());
            }

            if (const auto* type = FindMember(object, "type"); type && type->IsString())
            {
                out.type = ParseModuleType(type->AsString());
            }

            if (const auto* phase = FindMember(object, "loadPhase"); phase && phase->IsString())
            {
                out.loadPhase = ParseLoadPhase(phase->AsString());
            }

            if (const auto* version = FindMember(object, "version"); version && version->IsString())
            {
                auto parsedVersion = ParseSemanticVersion(version->AsString());
                if (!parsedVersion)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(parsedVersion.ErrorUnsafe());
                }
                out.version = parsedVersion.ValueUnsafe();
            }

            if (const auto* range = FindMember(object, "compatiblePlatformRange"); range && range->IsString())
            {
                auto parsedRange = ParseVersionRange(range->AsString());
                if (!parsedRange)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(parsedRange.ErrorUnsafe());
                }
                out.compatiblePlatformRange = parsedRange.ValueUnsafe();
            }

            ReadStringArray(object, "platforms", out.platforms);
            ReadStringArray(object, "providesServices", out.providesServices);
            ReadStringArray(object, "requiresServices", out.requiresServices);
            ReadStringArray(object, "capabilities", out.capabilities);

            if (const auto* reflection = FindMember(object, "reflectionRequired"); reflection && reflection->IsBool())
            {
                out.reflectionRequired = reflection->AsBool();
            }

            if (const auto* depsValue = FindMember(object, "dependencies"); depsValue && depsValue->IsArray())
            {
                for (const auto& depValue : depsValue->AsArray().values)
                {
                    if (!depValue.IsObject())
                    {
                        continue;
                    }

                    const auto& depObj = depValue.AsObject();
                    const auto* depName = FindMember(depObj, "name");
                    if (!depName || !depName->IsString())
                    {
                        continue;
                    }

                    DependencyDescriptor dep {};
                    dep.name = std::string(depName->AsString());

                    if (const auto* depOptional = FindMember(depObj, "optional"); depOptional && depOptional->IsBool())
                    {
                        dep.optional = depOptional->AsBool();
                    }

                    if (const auto* depRange = FindMember(depObj, "requiredVersion"); depRange && depRange->IsString())
                    {
                        auto parsedDepRange = ParseVersionRange(depRange->AsString());
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
                    if (path.extension() != ".json")
                    {
                        continue;
                    }

                    const auto filename = path.filename().string();
                    if (!(filename.ends_with(".module.json") || filename.ends_with(".plugin-module.json")))
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
                    auto parse = ParseDescriptorFromJson(path.string(), stream.str(), descriptor);
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

    auto RegisterStaticModule(StaticModuleRegistration registration) noexcept -> CoreResult<void>
    {
        return GetLegacyCatalog()->Register(std::move(registration));
    }

    void ClearStaticModules() noexcept
    {
        GetLegacyCatalog()->Clear();
    }

    auto GetStaticModules() noexcept -> std::vector<StaticModuleRegistration>
    {
        return GetLegacyCatalog()->Snapshot();
    }
}
