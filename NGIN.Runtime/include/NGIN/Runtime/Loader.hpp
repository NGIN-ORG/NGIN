#pragma once

/// @file Loader.hpp
/// @brief Static module catalog and dynamic plugin seam contracts.

#include <NGIN/Runtime/Descriptors.hpp>
#include <NGIN/Runtime/Errors.hpp>
#include <NGIN/Runtime/Export.hpp>
#include <NGIN/Memory/SmartPointers.hpp>

#include <functional>
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

    /// @brief Dynamic plugin catalog abstraction (Spec 003 seam).
    class NGIN_RUNTIME_API IPluginCatalog
    {
    public:
        virtual ~IPluginCatalog() = default;

        /// @brief Enumerate dynamic module descriptors without loading binaries.
        virtual auto CollectDescriptors(std::vector<ModuleDescriptor>& out) noexcept -> RuntimeResult<void> = 0;
    };

    /// @brief Dynamic plugin binary-loader abstraction (Spec 003 seam).
    class NGIN_RUNTIME_API IPluginBinaryLoader
    {
    public:
        virtual ~IPluginBinaryLoader() = default;

        /// @brief Load module binary for the provided descriptor.
        virtual auto LoadBinary(const ModuleDescriptor& descriptor) noexcept -> RuntimeResult<void> = 0;
    };

    /// @brief Register a static module descriptor/factory pair.
    NGIN_RUNTIME_API auto RegisterStaticModule(StaticModuleRegistration registration) noexcept -> RuntimeResult<void>;

    /// @brief Clear all static module registrations (intended for tests/process bootstrap).
    NGIN_RUNTIME_API void ClearStaticModules() noexcept;

    /// @brief Snapshot all currently registered static modules.
    NGIN_RUNTIME_API auto GetStaticModules() noexcept -> std::vector<StaticModuleRegistration>;
}
