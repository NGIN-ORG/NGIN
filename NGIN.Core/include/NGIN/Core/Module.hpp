#pragma once

/// @file Module.hpp
/// @brief Module lifecycle interface and context object.

#include <NGIN/Log/LoggerRegistry.hpp>
#include <NGIN/Core/Config.hpp>
#include <NGIN/Core/Errors.hpp>
#include <NGIN/Core/Events.hpp>
#include <NGIN/Core/Services.hpp>
#include <NGIN/Core/Tasks.hpp>
#include <NGIN/Core/Types.hpp>

#include <functional>
#include <string>
#include <string_view>

namespace NGIN::Core
{
    /// @brief Context passed to module lifecycle callbacks.
    class ModuleContext
    {
    public:
        ModuleContext(
            std::string moduleName,
            ServiceScopeId moduleScopeId,
            IServiceRegistry& services,
            IEventBus& events,
            ITaskRuntime& tasks,
            IConfigStore& config,
            NGIN::Log::LoggerRegistry& loggerRegistry,
            std::function<bool()> stopRequestedFn)
            : m_moduleName(std::move(moduleName))
            , m_moduleScopeId(moduleScopeId)
            , m_services(services)
            , m_events(events)
            , m_tasks(tasks)
            , m_config(config)
            , m_loggerRegistry(loggerRegistry)
            , m_stopRequestedFn(std::move(stopRequestedFn))
        {
        }

        [[nodiscard]] auto ModuleName() const noexcept -> std::string_view { return m_moduleName; }
        [[nodiscard]] auto ModuleScope() const noexcept -> ServiceScopeId { return m_moduleScopeId; }
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

        auto RegisterSingleton(
            std::string key,
            NGIN::Utilities::Any<> service,
            ServiceMetadata metadata = {}) noexcept -> CoreResult<void>
        {
            return m_services.RegisterInstance(
                std::move(key),
                std::move(service),
                ServiceRegistrationOptions {
                    .lifetime = ServiceLifetime::Singleton,
                    .ownerScope = m_moduleScopeId,
                    .metadata = std::move(metadata)});
        }

        auto RegisterFactory(
            std::string key,
            ServiceFactory factory,
            ServiceLifetime lifetime,
            ServiceMetadata metadata = {}) noexcept -> CoreResult<void>
        {
            return m_services.RegisterFactory(
                std::move(key),
                std::move(factory),
                ServiceRegistrationOptions {
                    .lifetime = lifetime,
                    .ownerScope = m_moduleScopeId,
                    .metadata = std::move(metadata)});
        }

    private:
        std::string                m_moduleName {};
        ServiceScopeId             m_moduleScopeId {ServiceScopeId::Global()};
        IServiceRegistry&          m_services;
        IEventBus&                 m_events;
        ITaskRuntime&              m_tasks;
        IConfigStore&              m_config;
        NGIN::Log::LoggerRegistry& m_loggerRegistry;
        std::function<bool()>      m_stopRequestedFn;
    };

    /// @brief Module lifecycle interface for the core host.
    class IModule
    {
    public:
        virtual ~IModule() = default;

        virtual auto OnRegister(ModuleContext&) noexcept -> CoreResult<void> { return CoreResult<void> {}; }
        virtual auto OnInit(ModuleContext&) noexcept -> CoreResult<void> { return CoreResult<void> {}; }
        virtual auto OnStart(ModuleContext&) noexcept -> CoreResult<void> { return CoreResult<void> {}; }
        virtual auto OnStop(ModuleContext&) noexcept -> CoreResult<void> { return CoreResult<void> {}; }
        virtual auto OnShutdown(ModuleContext&) noexcept -> CoreResult<void> { return CoreResult<void> {}; }
    };
}
