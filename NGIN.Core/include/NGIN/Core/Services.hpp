#pragma once

/// @file Services.hpp
/// @brief Typed and key-based service registration/query contracts.

#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/Meta/TypeName.hpp>
#include <NGIN/Core/Errors.hpp>
#include <NGIN/Core/Export.hpp>
#include <NGIN/Utilities/Any.hpp>

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace NGIN::Core
{
    /// @brief DI lifetime for service providers.
    enum class ServiceLifetime : NGIN::UInt8
    {
        Singleton,
        Scoped,
        Transient
    };

    /// @brief Scope kind for scoped services.
    enum class ServiceScopeKind : NGIN::UInt8
    {
        Host,
        Package,
        Module,
        Operation,
        Plugin
    };

    /// @brief Opaque service scope identifier.
    struct ServiceScopeId
    {
        NGIN::UInt64 value {0};

        [[nodiscard]] static constexpr auto Global() noexcept -> ServiceScopeId { return ServiceScopeId {0}; }
        [[nodiscard]] constexpr auto IsGlobal() const noexcept -> bool { return value == 0; }

        friend constexpr auto operator==(const ServiceScopeId&, const ServiceScopeId&) noexcept -> bool = default;
    };

    /// @brief Scope owner details.
    struct ServiceScopeInfo
    {
        ServiceScopeId   id {ServiceScopeId::Global()};
        ServiceScopeKind kind {ServiceScopeKind::Host};
        std::string      owner {};
    };

    /// @brief Metadata associated with a service registration.
    struct ServiceMetadata
    {
        std::vector<std::string> tags {};
    };

    using ServiceFactory = std::function<CoreResult<NGIN::Utilities::Any<>>()>;

    /// @brief Registration options for service providers.
    struct ServiceRegistrationOptions
    {
        ServiceLifetime lifetime {ServiceLifetime::Singleton};
        ServiceScopeId  ownerScope {ServiceScopeId::Global()};
        ServiceMetadata metadata {};
    };

    /// @brief Public service-registry interface.
    class NGIN_CORE_API IServiceRegistry
    {
    public:
        virtual ~IServiceRegistry() = default;

        virtual auto BeginScope(ServiceScopeKind kind, std::string owner) noexcept -> CoreResult<ServiceScopeId> = 0;
        virtual auto EndScope(ServiceScopeId scopeId) noexcept -> CoreResult<void> = 0;

        virtual auto RegisterInstance(
            std::string key,
            NGIN::Utilities::Any<> service,
            ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void> = 0;

        virtual auto RegisterFactory(
            std::string key,
            ServiceFactory factory,
            ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void> = 0;

        [[nodiscard]] virtual auto ResolveOptional(
            std::string_view key,
            ServiceScopeId resolveScope = ServiceScopeId::Global()) noexcept
            -> CoreResult<std::optional<NGIN::Utilities::Any<>>> = 0;

        [[nodiscard]] virtual auto ResolveRequired(
            std::string_view key,
            ServiceScopeId resolveScope = ServiceScopeId::Global()) noexcept
            -> CoreResult<NGIN::Utilities::Any<>> = 0;

        [[nodiscard]] virtual auto EnumerateKeys() const -> std::vector<std::string> = 0;

        [[nodiscard]] virtual auto GetScopeInfo(ServiceScopeId scopeId) const noexcept -> CoreResult<ServiceScopeInfo> = 0;
    };

    /// @brief Default in-process service registry implementation.
    class NGIN_CORE_API ServiceRegistry final : public IServiceRegistry
    {
    public:
        ServiceRegistry();

        auto BeginScope(ServiceScopeKind kind, std::string owner) noexcept -> CoreResult<ServiceScopeId> override;
        auto EndScope(ServiceScopeId scopeId) noexcept -> CoreResult<void> override;

        auto RegisterInstance(
            std::string key,
            NGIN::Utilities::Any<> service,
            ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void> override;

        auto RegisterFactory(
            std::string key,
            ServiceFactory factory,
            ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void> override;

        [[nodiscard]] auto ResolveOptional(
            std::string_view key,
            ServiceScopeId resolveScope = ServiceScopeId::Global()) noexcept
            -> CoreResult<std::optional<NGIN::Utilities::Any<>>> override;

        [[nodiscard]] auto ResolveRequired(
            std::string_view key,
            ServiceScopeId resolveScope = ServiceScopeId::Global()) noexcept
            -> CoreResult<NGIN::Utilities::Any<>> override;

        [[nodiscard]] auto EnumerateKeys() const -> std::vector<std::string> override;

        [[nodiscard]] auto GetScopeInfo(ServiceScopeId scopeId) const noexcept -> CoreResult<ServiceScopeInfo> override;

    private:
        struct Entry
        {
            std::optional<NGIN::Utilities::Any<>> singletonInstance {};
            std::optional<ServiceFactory>          factory {};
            ServiceRegistrationOptions             options {};
            std::unordered_map<NGIN::UInt64, NGIN::Utilities::Any<>> scopedCache {};
        };

        [[nodiscard]] auto ValidateOptions(const ServiceRegistrationOptions& options) const noexcept -> CoreResult<void>;

        mutable std::mutex                                m_mutex;
        std::unordered_map<std::string, Entry>           m_entries;
        std::unordered_map<NGIN::UInt64, ServiceScopeInfo> m_scopes;
        NGIN::UInt64                                     m_nextScopeId {1};
    };

    /// @brief Create a default service registry.
    NGIN_CORE_API auto CreateServiceRegistry() noexcept -> NGIN::Memory::Shared<IServiceRegistry>;

    template<typename T>
    [[nodiscard]] auto TypeServiceKey() -> std::string
    {
        return std::string(NGIN::Meta::TypeName<T>::qualifiedName);
    }

    template<typename T>
    auto RegisterTyped(
        IServiceRegistry& registry,
        T value,
        ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
    {
        return registry.RegisterInstance(
            TypeServiceKey<T>(),
            NGIN::Utilities::Any<>(std::move(value)),
            std::move(options));
    }

    template<typename T>
    [[nodiscard]] auto ResolveTypedOptional(
        IServiceRegistry& registry,
        ServiceScopeId resolveScope = ServiceScopeId::Global()) noexcept -> CoreResult<std::optional<T>>
    {
        auto value = registry.ResolveOptional(TypeServiceKey<T>(), resolveScope);
        if (!value)
        {
            return NGIN::Utilities::Unexpected<KernelError>(value.ErrorUnsafe());
        }

        if (!value.ValueUnsafe().has_value())
        {
            return std::optional<T> {};
        }

        const auto& anyValue = *value.ValueUnsafe();
        const auto* typed = anyValue.template TryCast<T>();
        if (typed == nullptr)
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Services", TypeServiceKey<T>(), "resolved service type mismatch"));
        }

        return std::optional<T> {*typed};
    }
}
