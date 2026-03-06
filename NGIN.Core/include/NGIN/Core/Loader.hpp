#pragma once

/// @file Loader.hpp
/// @brief Static module catalog and dynamic plugin seam contracts.

#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/Core/Descriptors.hpp>
#include <NGIN/Core/Errors.hpp>
#include <NGIN/Core/Export.hpp>

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace NGIN::Core
{
    class IModule;

    /// @brief Factory callable for creating a module instance.
    using ModuleFactory = std::function<CoreResult<NGIN::Memory::Shared<IModule>>()>;

    /// @brief Static module registration record.
    struct StaticModuleRegistration
    {
        ModuleDescriptor descriptor {};
        ModuleFactory    factory {};
    };

    /// @brief Module catalog abstraction used by kernel resolution.
    class NGIN_CORE_API IModuleCatalog
    {
    public:
        virtual ~IModuleCatalog() = default;
        virtual auto Register(StaticModuleRegistration registration) noexcept -> CoreResult<void> = 0;
        virtual void Clear() noexcept = 0;
        [[nodiscard]] virtual auto Snapshot() const -> std::vector<StaticModuleRegistration> = 0;
    };

    /// @brief Thread-safe static module catalog implementation.
    class NGIN_CORE_API StaticModuleCatalog final : public IModuleCatalog
    {
    public:
        auto Register(StaticModuleRegistration registration) noexcept -> CoreResult<void> override;
        void Clear() noexcept override;
        [[nodiscard]] auto Snapshot() const -> std::vector<StaticModuleRegistration> override;

    private:
        mutable std::mutex                      m_mutex;
        std::vector<StaticModuleRegistration> m_entries;
    };

    /// @brief Dynamic plugin catalog abstraction (Spec 003 seam).
    class NGIN_CORE_API IPluginCatalog
    {
    public:
        virtual ~IPluginCatalog() = default;

        /// @brief Enumerate dynamic module descriptors without loading binaries.
        virtual auto CollectDescriptors(std::vector<ModuleDescriptor>& out) noexcept -> CoreResult<void> = 0;
    };

    /// @brief Filesystem-backed dynamic descriptor catalog.
    class NGIN_CORE_API FilesystemPluginCatalog final : public IPluginCatalog
    {
    public:
        explicit FilesystemPluginCatalog(std::vector<std::string> searchPaths);

        auto CollectDescriptors(std::vector<ModuleDescriptor>& out) noexcept -> CoreResult<void> override;

    private:
        std::vector<std::string> m_searchPaths;
    };

    /// @brief Dynamic plugin binary-loader abstraction (Spec 003 seam).
    class NGIN_CORE_API IPluginBinaryLoader
    {
    public:
        virtual ~IPluginBinaryLoader() = default;

        /// @brief Load module binary for the provided descriptor.
        virtual auto LoadBinary(const ModuleDescriptor& descriptor) noexcept -> CoreResult<void> = 0;
    };

    /// @brief Create a default per-kernel static module catalog.
    NGIN_CORE_API auto CreateStaticModuleCatalog() noexcept -> NGIN::Memory::Shared<IModuleCatalog>;
}
