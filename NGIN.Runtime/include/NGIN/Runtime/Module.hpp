#pragma once

/// @file Module.hpp
/// @brief Module lifecycle interface and context object.

#include <NGIN/Log/LoggerRegistry.hpp>
#include <NGIN/Runtime/Config.hpp>
#include <NGIN/Runtime/Errors.hpp>
#include <NGIN/Runtime/Events.hpp>
#include <NGIN/Runtime/Services.hpp>
#include <NGIN/Runtime/Tasks.hpp>
#include <NGIN/Runtime/Types.hpp>

#include <functional>
#include <string>
#include <string_view>

namespace NGIN::Runtime
{
    /// @brief Context passed to module lifecycle callbacks.
    class ModuleContext
    {
    public:
        ModuleContext(
            std::string moduleName,
            IServiceRegistry& services,
            IEventBus& events,
            ITaskRuntime& tasks,
            IConfigStore& config,
            NGIN::Log::LoggerRegistry& loggerRegistry,
            std::function<bool()> stopRequestedFn)
            : m_moduleName(std::move(moduleName))
            , m_services(services)
            , m_events(events)
            , m_tasks(tasks)
            , m_config(config)
            , m_loggerRegistry(loggerRegistry)
            , m_stopRequestedFn(std::move(stopRequestedFn))
        {
        }

        [[nodiscard]] auto ModuleName() const noexcept -> std::string_view { return m_moduleName; }
        [[nodiscard]] auto Services() noexcept -> IServiceRegistry& { return m_services; }
        [[nodiscard]] auto Events() noexcept -> IEventBus& { return m_events; }
        [[nodiscard]] auto Tasks() noexcept -> ITaskRuntime& { return m_tasks; }
        [[nodiscard]] auto Config() noexcept -> IConfigStore& { return m_config; }
        [[nodiscard]] auto IsStopRequested() const noexcept -> bool { return m_stopRequestedFn ? m_stopRequestedFn() : false; }

        [[nodiscard]] auto GetLogger(std::string_view category) -> NGIN::Log::LoggerRegistry::LoggerPtr
        {
            std::string loggerName = "Module.";
            loggerName += m_moduleName;
            loggerName += ".";
            loggerName += category;
            return m_loggerRegistry.GetOrCreate(std::move(loggerName), NGIN::Log::LogLevel::Info);
        }

    private:
        std::string             m_moduleName {};
        IServiceRegistry&       m_services;
        IEventBus&              m_events;
        ITaskRuntime&           m_tasks;
        IConfigStore&           m_config;
        NGIN::Log::LoggerRegistry& m_loggerRegistry;
        std::function<bool()>   m_stopRequestedFn;
    };

    /// @brief Runtime module lifecycle interface.
    class IModule
    {
    public:
        virtual ~IModule() = default;

        virtual auto OnRegister(ModuleContext&) noexcept -> RuntimeResult<void> { return RuntimeResult<void> {}; }
        virtual auto OnInit(ModuleContext&) noexcept -> RuntimeResult<void> { return RuntimeResult<void> {}; }
        virtual auto OnStart(ModuleContext&) noexcept -> RuntimeResult<void> { return RuntimeResult<void> {}; }
        virtual auto OnStop(ModuleContext&) noexcept -> RuntimeResult<void> { return RuntimeResult<void> {}; }
        virtual auto OnShutdown(ModuleContext&) noexcept -> RuntimeResult<void> { return RuntimeResult<void> {}; }
    };
}

