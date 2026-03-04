#pragma once

/// @file Kernel.hpp
/// @brief Runtime-kernel public interface and startup diagnostics.

#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/Runtime/Config.hpp>
#include <NGIN/Runtime/Descriptors.hpp>
#include <NGIN/Runtime/Errors.hpp>
#include <NGIN/Runtime/Events.hpp>
#include <NGIN/Runtime/Export.hpp>
#include <NGIN/Runtime/HostConfig.hpp>
#include <NGIN/Runtime/Loader.hpp>
#include <NGIN/Runtime/Module.hpp>
#include <NGIN/Runtime/Services.hpp>
#include <NGIN/Runtime/Tasks.hpp>
#include <NGIN/Runtime/Types.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace NGIN::Runtime
{
    /// @brief Startup warning entry.
    struct StartupWarning
    {
        std::string subsystem {};
        std::string module {};
        std::string message {};
    };

    /// @brief Startup report summary.
    struct StartupReport
    {
        std::vector<std::string> resolvedModules {};
        std::vector<std::string> skippedOptionalModules {};
        std::vector<StartupWarning> warnings {};
    };

    /// @brief Runtime module state snapshot.
    struct ModuleRuntimeInfo
    {
        ModuleDescriptor descriptor {};
        ModuleState      state {ModuleState::Discovered};
        bool             optional {false};
        std::string      lastError {};
    };

    /// @brief Process-level runtime kernel interface.
    class NGIN_RUNTIME_API IKernel
    {
    public:
        virtual ~IKernel() = default;

        virtual auto Start() noexcept -> RuntimeResult<void> = 0;
        virtual auto Run() noexcept -> RuntimeResult<void> = 0;
        virtual auto Tick() noexcept -> RuntimeResult<void> = 0;
        virtual void RequestStop(std::string reason) noexcept = 0;
        virtual auto Shutdown() noexcept -> RuntimeResult<void> = 0;

        [[nodiscard]] virtual auto GetState() const noexcept -> KernelState = 0;
        [[nodiscard]] virtual auto GetStartupReport() const -> StartupReport = 0;
        [[nodiscard]] virtual auto GetModuleStates() const -> std::vector<ModuleRuntimeInfo> = 0;

        [[nodiscard]] virtual auto GetServices() noexcept -> NGIN::Memory::Shared<IServiceRegistry> = 0;
        [[nodiscard]] virtual auto GetEvents() noexcept -> NGIN::Memory::Shared<IEventBus> = 0;
        [[nodiscard]] virtual auto GetTasks() noexcept -> NGIN::Memory::Shared<ITaskRuntime> = 0;
        [[nodiscard]] virtual auto GetConfig() noexcept -> NGIN::Memory::Shared<IConfigStore> = 0;
    };

    /// @brief Construct a new runtime kernel instance.
    NGIN_RUNTIME_API auto CreateKernel(const KernelHostConfig& config) noexcept -> RuntimeResult<NGIN::Memory::Shared<IKernel>>;
}

