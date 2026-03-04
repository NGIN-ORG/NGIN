#pragma once

/// @file Services.hpp
/// @brief Typed and key-based service registration/query contracts.

#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/Meta/TypeName.hpp>
#include <NGIN/Runtime/Errors.hpp>
#include <NGIN/Runtime/Export.hpp>
#include <NGIN/Utilities/Any.hpp>

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace NGIN::Runtime
{
    /// @brief Service lifetime scope.
    enum class ServiceLifetime : NGIN::UInt8
    {
        Kernel,
        Plugin,
        Module
    };

    /// @brief Scope owner for deterministic teardown.
    struct ServiceScope
    {
        ServiceLifetime lifetime {ServiceLifetime::Kernel};
        std::string     owner {};
    };

    /// @brief Metadata associated with a service registration.
    struct ServiceMetadata
    {
        std::vector<std::string> tags {};
    };

    using ServiceFactory = std::function<RuntimeResult<NGIN::Utilities::Any<>>()>;

    /// @brief Public service-registry interface.
    class NGIN_RUNTIME_API IServiceRegistry
    {
    public:
        virtual ~IServiceRegistry() = default;

        virtual auto RegisterInstance(
            std::string key,
            NGIN::Utilities::Any<> service,
            ServiceScope scope,
            ServiceMetadata metadata) noexcept -> RuntimeResult<void> = 0;

        virtual auto RegisterFactory(
            std::string key,
            ServiceFactory factory,
            ServiceScope scope,
            ServiceMetadata metadata) noexcept -> RuntimeResult<void> = 0;

        [[nodiscard]] virtual auto ResolveOptional(std::string_view key) noexcept
            -> RuntimeResult<std::optional<NGIN::Utilities::Any<>>> = 0;

        [[nodiscard]] virtual auto ResolveRequired(std::string_view key) noexcept
            -> RuntimeResult<NGIN::Utilities::Any<>> = 0;

        [[nodiscard]] virtual auto EnumerateKeys() const -> std::vector<std::string> = 0;

        virtual void ClearScope(const ServiceScope& scope) noexcept = 0;
    };

    /// @brief Default in-process service registry implementation.
    class NGIN_RUNTIME_API ServiceRegistry final : public IServiceRegistry
    {
    public:
        ServiceRegistry() = default;

        auto RegisterInstance(
            std::string key,
            NGIN::Utilities::Any<> service,
            ServiceScope scope,
            ServiceMetadata metadata) noexcept -> RuntimeResult<void> override;

        auto RegisterFactory(
            std::string key,
            ServiceFactory factory,
            ServiceScope scope,
            ServiceMetadata metadata) noexcept -> RuntimeResult<void> override;

        [[nodiscard]] auto ResolveOptional(std::string_view key) noexcept
            -> RuntimeResult<std::optional<NGIN::Utilities::Any<>>> override;

        [[nodiscard]] auto ResolveRequired(std::string_view key) noexcept
            -> RuntimeResult<NGIN::Utilities::Any<>> override;

        [[nodiscard]] auto EnumerateKeys() const -> std::vector<std::string> override;

        void ClearScope(const ServiceScope& scope) noexcept override;

    private:
        struct Entry
        {
            std::optional<NGIN::Utilities::Any<>> instance {};
            std::optional<ServiceFactory>         factory {};
            ServiceScope                          scope {};
            ServiceMetadata                       metadata {};
        };

        mutable std::mutex                        m_mutex;
        std::unordered_map<std::string, Entry> m_entries;
    };

    /// @brief Create a default service registry.
    NGIN_RUNTIME_API auto CreateServiceRegistry() noexcept -> NGIN::Memory::Shared<IServiceRegistry>;

    template<typename T>
    [[nodiscard]] auto TypeServiceKey() -> std::string
    {
        return std::string(NGIN::Meta::TypeName<T>::qualifiedName);
    }

    template<typename T>
    auto RegisterTyped(
        IServiceRegistry& registry,
        T value,
        const ServiceScope& scope,
        ServiceMetadata metadata = {}) noexcept -> RuntimeResult<void>
    {
        return registry.RegisterInstance(
            TypeServiceKey<T>(),
            NGIN::Utilities::Any<>(std::move(value)),
            scope,
            std::move(metadata));
    }
}
