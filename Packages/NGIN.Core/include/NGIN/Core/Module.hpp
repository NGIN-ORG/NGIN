#pragma once

/// @file Module.hpp
/// @brief Module lifecycle interface and context object.

#include <NGIN/Log/LoggerRegistry.hpp>
#include <NGIN/Core/Config.hpp>
#include <NGIN/Core/Descriptors.hpp>
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
            const ModuleDescriptor& descriptor,
            ServiceScopeId moduleScopeId,
            IServiceRegistry& services,
            IEventBus& events,
            ITaskRuntime& tasks,
            IConfigStore& config,
            NGIN::Log::LoggerRegistry& loggerRegistry,
            std::function<bool()> stopRequestedFn)
            : m_descriptor(descriptor)
            , m_moduleScopeId(moduleScopeId)
            , m_services(services)
            , m_events(events)
            , m_tasks(tasks)
            , m_config(config)
            , m_loggerRegistry(loggerRegistry)
            , m_stopRequestedFn(std::move(stopRequestedFn))
        {
        }

        [[nodiscard]] auto Descriptor() const noexcept -> const ModuleDescriptor& { return m_descriptor; }
        [[nodiscard]] auto ModuleName() const noexcept -> std::string_view { return m_descriptor.name; }
        [[nodiscard]] auto ModuleRoot() const noexcept -> std::string_view { return m_descriptor.moduleRoot; }
        [[nodiscard]] auto DescriptorPath() const noexcept -> std::string_view { return m_descriptor.descriptorPath; }
        [[nodiscard]] auto LibraryPath() const noexcept -> std::string_view { return m_descriptor.pluginLibrary; }
        [[nodiscard]] auto PluginName() const noexcept -> std::string_view { return m_descriptor.pluginName; }
        [[nodiscard]] auto IsDynamicModule() const noexcept -> bool { return m_descriptor.entryKind == ModuleEntryKind::Dynamic; }
        [[nodiscard]] auto ModuleScope() const noexcept -> ServiceScopeId { return m_moduleScopeId; }
        [[nodiscard]] auto Services() noexcept -> IServiceRegistry& { return m_services; }
        [[nodiscard]] auto Events() noexcept -> IEventBus& { return m_events; }
        [[nodiscard]] auto Tasks() noexcept -> ITaskRuntime& { return m_tasks; }
        [[nodiscard]] auto Config() noexcept -> IConfigStore& { return m_config; }
        [[nodiscard]] auto IsStopRequested() const noexcept -> bool { return m_stopRequestedFn ? m_stopRequestedFn() : false; }

        [[nodiscard]] auto GetLogger(std::string_view category) -> NGIN::Log::LoggerRegistry::LoggerPtr
        {
            std::string loggerName = "Module.";
            loggerName += m_descriptor.name;
            loggerName += ".";
            loggerName += category;
            return m_loggerRegistry.GetOrCreate(std::move(loggerName), NGIN::Log::LogLevel::Info);
        }

        template<typename T>
        auto RegisterSingleton(ServiceMetadata metadata = {}) noexcept -> CoreResult<void>
        {
            return m_services.RegisterSingleton<T>(
                ServiceRegistrationOptions {
                    .lifetime = ServiceLifetime::Singleton,
                    .ownerScope = m_moduleScopeId,
                    .metadata = std::move(metadata)});
        }

        template<typename T>
        auto RegisterSingleton(std::string name, ServiceMetadata metadata = {}) noexcept -> CoreResult<void>
        {
            return m_services.RegisterSingleton<T>(
                std::move(name),
                ServiceRegistrationOptions {
                    .lifetime = ServiceLifetime::Singleton,
                    .ownerScope = m_moduleScopeId,
                    .metadata = std::move(metadata)});
        }

        template<typename T>
        auto RegisterSingleton(
            std::string name,
            NGIN::Memory::Shared<std::remove_cvref_t<T>> service,
            ServiceMetadata metadata = {}) noexcept -> CoreResult<void>
        {
            return m_services.RegisterSingleton<T>(
                std::move(name),
                std::move(service),
                ServiceRegistrationOptions {
                    .lifetime = ServiceLifetime::Singleton,
                    .ownerScope = m_moduleScopeId,
                    .metadata = std::move(metadata)});
        }

        template<typename T>
        auto RegisterSingletonValue(
            std::string name,
            T value,
            ServiceMetadata metadata = {}) noexcept -> CoreResult<void>
        {
            return m_services.RegisterSingletonValue<std::remove_cvref_t<T>>(
                std::move(name),
                std::move(value),
                ServiceRegistrationOptions {
                    .lifetime = ServiceLifetime::Singleton,
                    .ownerScope = m_moduleScopeId,
                    .metadata = std::move(metadata)});
        }

        template<typename T>
        auto RegisterFactory(
            std::string name,
            ServiceProviderFactory<std::remove_cvref_t<T>> factory,
            ServiceLifetime lifetime,
            ServiceMetadata metadata = {}) noexcept -> CoreResult<void>
        {
            return m_services.RegisterFactory<T>(
                std::move(name),
                std::move(factory),
                ServiceRegistrationOptions {
                    .lifetime = lifetime,
                    .ownerScope = m_moduleScopeId,
                    .metadata = std::move(metadata)});
        }

        template<typename T>
        auto RegisterScoped(ServiceMetadata metadata = {}) noexcept -> CoreResult<void>
        {
            return m_services.RegisterScoped<T>(
                ServiceRegistrationOptions {
                    .lifetime = ServiceLifetime::Scoped,
                    .ownerScope = m_moduleScopeId,
                    .metadata = std::move(metadata)});
        }

        template<typename T>
        auto RegisterScoped(std::string name, ServiceMetadata metadata = {}) noexcept -> CoreResult<void>
        {
            return m_services.RegisterScoped<T>(
                std::move(name),
                ServiceRegistrationOptions {
                    .lifetime = ServiceLifetime::Scoped,
                    .ownerScope = m_moduleScopeId,
                    .metadata = std::move(metadata)});
        }

        template<typename T>
        auto RegisterTransient(ServiceMetadata metadata = {}) noexcept -> CoreResult<void>
        {
            return m_services.RegisterTransient<T>(
                ServiceRegistrationOptions {
                    .lifetime = ServiceLifetime::Transient,
                    .ownerScope = m_moduleScopeId,
                    .metadata = std::move(metadata)});
        }

        template<typename T>
        auto RegisterTransient(std::string name, ServiceMetadata metadata = {}) noexcept -> CoreResult<void>
        {
            return m_services.RegisterTransient<T>(
                std::move(name),
                ServiceRegistrationOptions {
                    .lifetime = ServiceLifetime::Transient,
                    .ownerScope = m_moduleScopeId,
                    .metadata = std::move(metadata)});
        }

    private:
        const ModuleDescriptor&    m_descriptor;
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
