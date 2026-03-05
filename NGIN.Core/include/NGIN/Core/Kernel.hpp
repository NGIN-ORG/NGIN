#pragma once

/// @file Kernel.hpp
/// @brief Core host public interface and startup diagnostics.

#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/Core/Config.hpp>
#include <NGIN/Core/Descriptors.hpp>
#include <NGIN/Core/Errors.hpp>
#include <NGIN/Core/Events.hpp>
#include <NGIN/Core/Export.hpp>
#include <NGIN/Core/HostConfig.hpp>
#include <NGIN/Core/Loader.hpp>
#include <NGIN/Core/Module.hpp>
#include <NGIN/Core/Services.hpp>
#include <NGIN/Core/Tasks.hpp>
#include <NGIN/Core/Types.hpp>

#include <string>
#include <vector>

namespace NGIN::Core
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
        std::vector<std::string>  resolvedModules {};
        std::vector<std::string>  skippedOptionalModules {};
        std::vector<StartupWarning> warnings {};
        std::vector<KernelError>  failures {};
    };

    /// @brief Module state snapshot for the current core host.
    struct ModuleRuntimeInfo
    {
        ModuleDescriptor descriptor {};
        ModuleState      state {ModuleState::Discovered};
        bool             optional {false};
        std::string      lastError {};
        bool             registered {false};
        bool             initialized {false};
        bool             started {false};
    };

    /// @brief Process-level core host interface.
    class NGIN_CORE_API IKernel
    {
    public:
        virtual ~IKernel() = default;

        virtual auto Start() noexcept -> CoreResult<void> = 0;
        virtual auto Run() noexcept -> CoreResult<void> = 0;
        virtual auto Tick() noexcept -> CoreResult<void> = 0;
        virtual void RequestStop(std::string reason) noexcept = 0;
        virtual auto Shutdown() noexcept -> CoreResult<void> = 0;

        [[nodiscard]] virtual auto GetState() const noexcept -> KernelState = 0;
        [[nodiscard]] virtual auto GetStartupReport() const -> StartupReport = 0;
        [[nodiscard]] virtual auto GetModuleStates() const -> std::vector<ModuleRuntimeInfo> = 0;

        [[nodiscard]] virtual auto GetServices() noexcept -> NGIN::Memory::Shared<IServiceRegistry> = 0;
        [[nodiscard]] virtual auto GetEvents() noexcept -> NGIN::Memory::Shared<IEventBus> = 0;
        [[nodiscard]] virtual auto GetTasks() noexcept -> NGIN::Memory::Shared<ITaskRuntime> = 0;
        [[nodiscard]] virtual auto GetConfig() noexcept -> NGIN::Memory::Shared<IConfigStore> = 0;
    };

    /// @brief Construct a new core host instance.
    NGIN_CORE_API auto CreateKernel(const KernelHostConfig& config) noexcept -> CoreResult<NGIN::Memory::Shared<IKernel>>;
}
