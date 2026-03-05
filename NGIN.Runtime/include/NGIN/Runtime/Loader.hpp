#pragma once

/// @file Loader.hpp
/// @brief Static module catalog and dynamic plugin seam contracts.

#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/Runtime/Descriptors.hpp>
#include <NGIN/Runtime/Errors.hpp>
#include <NGIN/Runtime/Export.hpp>

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace NGIN::Runtime
{
    class IModule;

    /// @brief Factory callable for creating a module instance.
    using ModuleFactory = std::function<RuntimeResult<NGIN::Memory::Shared<IModule>>()>;

    /// @brief Static module registration record.
    struct StaticModuleRegistration
    {
        ModuleDescriptor descriptor {};
        ModuleFactory    factory {};
    };

    /// @brief Module catalog abstraction used by kernel resolution.
    class NGIN_RUNTIME_API IModuleCatalog
    {
    public:
        virtual ~IModuleCatalog() = default;
        virtual auto Register(StaticModuleRegistration registration) noexcept -> RuntimeResult<void> = 0;
        virtual void Clear() noexcept = 0;
        [[nodiscard]] virtual auto Snapshot() const -> std::vector<StaticModuleRegistration> = 0;
    };

    /// @brief Thread-safe static module catalog implementation.
    class NGIN_RUNTIME_API StaticModuleCatalog final : public IModuleCatalog
    {
    public:
        auto Register(StaticModuleRegistration registration) noexcept -> RuntimeResult<void> override;
        void Clear() noexcept override;
        [[nodiscard]] auto Snapshot() const -> std::vector<StaticModuleRegistration> override;

    private:
        mutable std::mutex                      m_mutex;
        std::vector<StaticModuleRegistration> m_entries;
    };

    /// @brief Dynamic plugin catalog abstraction (Spec 003 seam).
    class NGIN_RUNTIME_API IPluginCatalog
    {
    public:
        virtual ~IPluginCatalog() = default;

        /// @brief Enumerate dynamic module descriptors without loading binaries.
        virtual auto CollectDescriptors(std::vector<ModuleDescriptor>& out) noexcept -> RuntimeResult<void> = 0;
    };

    /// @brief Filesystem-backed dynamic descriptor catalog.
    class NGIN_RUNTIME_API FilesystemPluginCatalog final : public IPluginCatalog
    {
    public:
        explicit FilesystemPluginCatalog(std::vector<std::string> searchPaths);

        auto CollectDescriptors(std::vector<ModuleDescriptor>& out) noexcept -> RuntimeResult<void> override;

    private:
        std::vector<std::string> m_searchPaths;
    };

    /// @brief Dynamic plugin binary-loader abstraction (Spec 003 seam).
    class NGIN_RUNTIME_API IPluginBinaryLoader
    {
    public:
        virtual ~IPluginBinaryLoader() = default;

        /// @brief Load module binary for the provided descriptor.
        virtual auto LoadBinary(const ModuleDescriptor& descriptor) noexcept -> RuntimeResult<void> = 0;
    };

    /// @brief Create default module catalog.
    NGIN_RUNTIME_API auto CreateStaticModuleCatalog() noexcept -> NGIN::Memory::Shared<IModuleCatalog>;

    /// @brief Register a static module descriptor/factory pair on the legacy process-global catalog.
    [[deprecated("Use IModuleCatalog/StaticModuleCatalog instead")]]
    NGIN_RUNTIME_API auto RegisterStaticModule(StaticModuleRegistration registration) noexcept -> RuntimeResult<void>;

    /// @brief Clear all static module registrations (legacy process-global catalog).
    [[deprecated("Use IModuleCatalog/StaticModuleCatalog instead")]]
    NGIN_RUNTIME_API void ClearStaticModules() noexcept;

    /// @brief Snapshot all currently registered static modules (legacy process-global catalog).
    [[deprecated("Use IModuleCatalog/StaticModuleCatalog instead")]]
    NGIN_RUNTIME_API auto GetStaticModules() noexcept -> std::vector<StaticModuleRegistration>;
}
