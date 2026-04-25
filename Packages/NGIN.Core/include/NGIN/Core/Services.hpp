#pragma once

/// @file Services.hpp
/// @brief Typed service registration/query contracts.

#include <NGIN/Core/Errors.hpp>
#include <NGIN/Core/Export.hpp>
#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/Meta/TypeId.hpp>
#include <NGIN/Meta/TypeName.hpp>

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace NGIN::Core
{
    class IServiceProvider;
    class IServiceRegistry;

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

    /// @brief Stable service identity: C++ type plus optional named contract.
    struct ServiceKey
    {
        NGIN::UInt64 typeId {0};
        std::string  typeName {};
        std::string  name {};

        [[nodiscard]] auto ContractName() const -> std::string
        {
            return name.empty() ? typeName : name;
        }

        friend auto operator==(const ServiceKey& lhs, const ServiceKey& rhs) noexcept -> bool
        {
            return lhs.typeId == rhs.typeId && lhs.name == rhs.name;
        }
    };

    struct ServiceKeyHash
    {
        [[nodiscard]] auto operator()(const ServiceKey& key) const noexcept -> std::size_t
        {
            const auto typeHash = static_cast<std::size_t>(key.typeId);
            const auto nameHash = std::hash<std::string> {}(key.name);
            return typeHash ^ (nameHash + 0x9e3779b97f4a7c15ULL + (typeHash << 6U) + (typeHash >> 2U));
        }
    };

    /// @brief Registration options for service providers.
    struct ServiceRegistrationOptions
    {
        ServiceLifetime lifetime {ServiceLifetime::Singleton};
        ServiceScopeId  ownerScope {ServiceScopeId::Global()};
        ServiceMetadata metadata {};
    };

    /// @brief Context passed to service factories.
    struct ServiceResolutionContext
    {
        IServiceProvider& services;
        ServiceScopeId    scope {ServiceScopeId::Global()};
    };

    template<typename T>
    using ServiceProviderFactory =
        std::function<CoreResult<NGIN::Memory::Shared<T>>(ServiceResolutionContext&)>;

    template<typename T>
    [[nodiscard]] auto TypeServiceKey(std::string name = {}) -> ServiceKey
    {
        using Stored = std::remove_cvref_t<T>;
        return ServiceKey {
            .typeId = NGIN::Meta::GetTypeId<Stored>(),
            .typeName = std::string(NGIN::Meta::TypeName<Stored>::qualifiedName),
            .name = std::move(name),
        };
    }

    namespace detail
    {
        class ServiceProviderBase
        {
        public:
            ServiceProviderBase(ServiceKey key, ServiceRegistrationOptions options)
                : m_key(std::move(key)), m_options(std::move(options))
            {
            }

            virtual ~ServiceProviderBase() = default;

            [[nodiscard]] auto Key() const noexcept -> const ServiceKey& { return m_key; }
            [[nodiscard]] auto Options() const noexcept -> const ServiceRegistrationOptions& { return m_options; }
            [[nodiscard]] auto ContractName() const -> std::string { return m_key.ContractName(); }
            [[nodiscard]] auto MatchesContract(std::string_view contract) const -> bool
            {
                return m_key.ContractName() == contract;
            }

            virtual void RemoveScope(ServiceScopeId scopeId) noexcept = 0;
            [[nodiscard]] virtual auto CloneWithOptions(ServiceRegistrationOptions options) const
                -> std::shared_ptr<ServiceProviderBase> = 0;

        private:
            ServiceKey                 m_key {};
            ServiceRegistrationOptions m_options {};
        };

        template<typename T>
        class TypedServiceProvider final : public ServiceProviderBase
        {
        public:
            using ServiceType = std::remove_cvref_t<T>;

            TypedServiceProvider(
                ServiceKey key,
                NGIN::Memory::Shared<ServiceType> instance,
                ServiceRegistrationOptions options)
                : ServiceProviderBase(std::move(key), std::move(options))
                , m_instance(std::move(instance))
            {
            }

            TypedServiceProvider(
                ServiceKey key,
                ServiceProviderFactory<ServiceType> factory,
                ServiceRegistrationOptions options)
                : ServiceProviderBase(std::move(key), std::move(options))
                , m_factory(std::move(factory))
            {
            }

            [[nodiscard]] auto Resolve(ServiceResolutionContext& context)
                -> CoreResult<NGIN::Memory::Shared<ServiceType>>
            {
                const auto lifetime = Options().lifetime;
                if (lifetime == ServiceLifetime::Transient)
                {
                    return Create(context);
                }

                if (lifetime == ServiceLifetime::Singleton)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_instance)
                    {
                        return m_instance;
                    }
                }

                if (lifetime == ServiceLifetime::Scoped)
                {
                    const ServiceScopeId activeScope =
                        context.scope.IsGlobal() ? Options().ownerScope : context.scope;
                    std::lock_guard<std::mutex> lock(m_mutex);
                    const auto cacheIt = m_scopedCache.find(activeScope.value);
                    if (cacheIt != m_scopedCache.end())
                    {
                        return cacheIt->second;
                    }
                }

                auto created = Create(context);
                if (!created)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(created.Error());
                }

                std::lock_guard<std::mutex> lock(m_mutex);
                if (lifetime == ServiceLifetime::Singleton)
                {
                    if (!m_instance)
                    {
                        m_instance = created.Value();
                    }
                    return m_instance;
                }

                const ServiceScopeId activeScope =
                    context.scope.IsGlobal() ? Options().ownerScope : context.scope;
                auto cacheIt = m_scopedCache.find(activeScope.value);
                if (cacheIt == m_scopedCache.end())
                {
                    cacheIt = m_scopedCache.emplace(activeScope.value, created.Value()).first;
                }
                return cacheIt->second;
            }

            void RemoveScope(ServiceScopeId scopeId) noexcept override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_scopedCache.erase(scopeId.value);
            }

            [[nodiscard]] auto CloneWithOptions(ServiceRegistrationOptions options) const
                -> std::shared_ptr<ServiceProviderBase> override
            {
                if (m_factory)
                {
                    return std::make_shared<TypedServiceProvider<ServiceType>>(
                        Key(), m_factory, std::move(options));
                }

                return std::make_shared<TypedServiceProvider<ServiceType>>(
                    Key(), m_instance, std::move(options));
            }

        private:
            [[nodiscard]] auto Create(ServiceResolutionContext& context)
                -> CoreResult<NGIN::Memory::Shared<ServiceType>>
            {
                if (!m_factory)
                {
                    if (m_instance)
                    {
                        return m_instance;
                    }

                    return NGIN::Utilities::Unexpected<KernelError>(
                        MakeKernelError(KernelErrorCode::InvalidState, "Services", Key().ContractName(), "service provider has no instance or factory"));
                }

                auto created = m_factory(context);
                if (!created)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(created.Error());
                }
                if (!created.Value())
                {
                    return NGIN::Utilities::Unexpected<KernelError>(
                        MakeKernelError(KernelErrorCode::InvalidState, "Services", Key().ContractName(), "service factory returned null"));
                }
                return created.Value();
            }

            mutable std::mutex m_mutex {};
            NGIN::Memory::Shared<ServiceType> m_instance {};
            ServiceProviderFactory<ServiceType> m_factory {};
            std::unordered_map<NGIN::UInt64, NGIN::Memory::Shared<ServiceType>> m_scopedCache {};
        };

        class ServiceProviderReference;

        template<typename T>
        struct CanAutoConstructService
        {
            using ServiceType = std::remove_cvref_t<T>;
            static constexpr bool value =
                std::is_constructible_v<ServiceType, NGIN::Memory::Shared<IServiceProvider>> ||
                std::is_default_constructible_v<ServiceType>;
        };

        template<typename T>
        [[nodiscard]] auto MakeAutoFactory() -> ServiceProviderFactory<std::remove_cvref_t<T>>;

        template<typename T>
        [[nodiscard]] auto MakeInstanceProvider(
            std::string name,
            NGIN::Memory::Shared<std::remove_cvref_t<T>> service,
            ServiceRegistrationOptions options = {}) -> std::shared_ptr<ServiceProviderBase>
        {
            using ServiceType = std::remove_cvref_t<T>;
            options.lifetime = ServiceLifetime::Singleton;
            return std::make_shared<TypedServiceProvider<ServiceType>>(
                TypeServiceKey<ServiceType>(std::move(name)),
                std::move(service),
                std::move(options));
        }

        template<typename T>
        [[nodiscard]] auto MakeFactoryProvider(
            std::string name,
            ServiceProviderFactory<std::remove_cvref_t<T>> factory,
            ServiceRegistrationOptions options) -> std::shared_ptr<ServiceProviderBase>
        {
            using ServiceType = std::remove_cvref_t<T>;
            return std::make_shared<TypedServiceProvider<ServiceType>>(
                TypeServiceKey<ServiceType>(std::move(name)),
                std::move(factory),
                std::move(options));
        }
    }

    /// @brief Resolve-only typed service provider passed to services.
    class NGIN_CORE_API IServiceProvider
    {
    public:
        virtual ~IServiceProvider() = default;

        [[nodiscard]] virtual auto HasServiceContract(std::string_view contractName) const noexcept -> bool = 0;
        [[nodiscard]] virtual auto EnumerateKeys() const -> std::vector<std::string> = 0;
        [[nodiscard]] virtual auto GetScopeInfo(ServiceScopeId scopeId) const noexcept -> CoreResult<ServiceScopeInfo> = 0;

        template<typename T>
        [[nodiscard]] auto ResolveOptional(
            ServiceScopeId resolveScope = ServiceScopeId::Global()) noexcept
            -> CoreResult<std::optional<NGIN::Memory::Shared<std::remove_cvref_t<T>>>>
        {
            return ResolveOptional<T>({}, resolveScope);
        }

        template<typename T>
        [[nodiscard]] auto ResolveOptional(
            std::string_view name,
            ServiceScopeId resolveScope = ServiceScopeId::Global()) noexcept
            -> CoreResult<std::optional<NGIN::Memory::Shared<std::remove_cvref_t<T>>>>
        {
            using ServiceType = std::remove_cvref_t<T>;
            const ServiceKey key = TypeServiceKey<ServiceType>(std::string(name));
            auto provider = FindProvider(key);
            if (!provider)
            {
                if (!name.empty() && HasServiceContract(name))
                {
                    return NGIN::Utilities::Unexpected<KernelError>(
                        MakeKernelError(KernelErrorCode::InvalidArgument, "Services", std::string(name), "resolved service type mismatch"));
                }
                return std::optional<NGIN::Memory::Shared<ServiceType>> {};
            }

            const ServiceScopeId effectiveScope = EffectiveResolveScope(*provider, resolveScope);
            auto valid = ValidateResolve(*provider, effectiveScope);
            if (!valid)
            {
                return NGIN::Utilities::Unexpected<KernelError>(valid.Error());
            }

            auto* typed = dynamic_cast<detail::TypedServiceProvider<ServiceType>*>(provider.get());
            if (typed == nullptr)
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(KernelErrorCode::InvalidArgument, "Services", key.ContractName(), "resolved service type mismatch"));
            }

            ServiceResolutionContext context {
                .services = *this,
                .scope = effectiveScope,
            };
            auto resolved = typed->Resolve(context);
            if (!resolved)
            {
                return NGIN::Utilities::Unexpected<KernelError>(resolved.Error());
            }
            return std::optional<NGIN::Memory::Shared<ServiceType>> {resolved.Value()};
        }

        template<typename T>
        [[nodiscard]] auto ResolveRequired(
            ServiceScopeId resolveScope = ServiceScopeId::Global()) noexcept
            -> CoreResult<NGIN::Memory::Shared<std::remove_cvref_t<T>>>
        {
            return ResolveRequired<T>({}, resolveScope);
        }

        template<typename T>
        [[nodiscard]] auto ResolveRequired(
            std::string_view name,
            ServiceScopeId resolveScope = ServiceScopeId::Global()) noexcept
            -> CoreResult<NGIN::Memory::Shared<std::remove_cvref_t<T>>>
        {
            auto optionalValue = ResolveOptional<T>(name, resolveScope);
            if (!optionalValue)
            {
                return NGIN::Utilities::Unexpected<KernelError>(optionalValue.Error());
            }
            if (!optionalValue.Value().has_value())
            {
                const auto key = TypeServiceKey<std::remove_cvref_t<T>>(std::string(name));
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(KernelErrorCode::NotFound, "Services", key.ContractName(), "service not found: " + key.ContractName()));
            }
            return *optionalValue.Value();
        }

        [[nodiscard]] virtual auto FindProvider(const ServiceKey& key) noexcept -> std::shared_ptr<detail::ServiceProviderBase> = 0;
        [[nodiscard]] virtual auto EffectiveResolveScope(
            const detail::ServiceProviderBase& provider,
            ServiceScopeId requestedScope) const noexcept -> ServiceScopeId = 0;
        [[nodiscard]] virtual auto ValidateResolve(
            const detail::ServiceProviderBase& provider,
            ServiceScopeId resolveScope) const noexcept -> CoreResult<void> = 0;
    };

    namespace detail
    {
        class ServiceProviderReference final : public IServiceProvider
        {
        public:
            ServiceProviderReference(IServiceProvider* provider, ServiceScopeId defaultScope) noexcept
                : m_provider(provider), m_defaultScope(defaultScope)
            {
            }

            [[nodiscard]] auto HasServiceContract(std::string_view contractName) const noexcept -> bool override
            {
                return m_provider != nullptr && m_provider->HasServiceContract(contractName);
            }

            [[nodiscard]] auto EnumerateKeys() const -> std::vector<std::string> override
            {
                return m_provider != nullptr ? m_provider->EnumerateKeys() : std::vector<std::string> {};
            }

            [[nodiscard]] auto GetScopeInfo(ServiceScopeId scopeId) const noexcept -> CoreResult<ServiceScopeInfo> override
            {
                if (m_provider == nullptr)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(
                        MakeKernelError(KernelErrorCode::InvalidState, "Services", {}, "service provider is unavailable"));
                }
                return m_provider->GetScopeInfo(scopeId);
            }

            [[nodiscard]] auto FindProvider(const ServiceKey& key) noexcept -> std::shared_ptr<ServiceProviderBase> override
            {
                return m_provider != nullptr ? m_provider->FindProvider(key) : nullptr;
            }

            [[nodiscard]] auto EffectiveResolveScope(
                const ServiceProviderBase& provider,
                ServiceScopeId requestedScope) const noexcept -> ServiceScopeId override
            {
                if (!requestedScope.IsGlobal())
                {
                    return requestedScope;
                }
                if (!m_defaultScope.IsGlobal())
                {
                    return m_defaultScope;
                }
                return m_provider != nullptr ? m_provider->EffectiveResolveScope(provider, requestedScope)
                                             : provider.Options().ownerScope;
            }

            [[nodiscard]] auto ValidateResolve(
                const ServiceProviderBase& provider,
                ServiceScopeId resolveScope) const noexcept -> CoreResult<void> override
            {
                if (m_provider == nullptr)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(
                        MakeKernelError(KernelErrorCode::InvalidState, "Services", provider.Key().ContractName(), "service provider is unavailable"));
                }
                return m_provider->ValidateResolve(provider, resolveScope);
            }

        private:
            IServiceProvider* m_provider {nullptr};
            ServiceScopeId m_defaultScope {ServiceScopeId::Global()};
        };

        template<typename T>
        [[nodiscard]] auto MakeAutoFactory() -> ServiceProviderFactory<std::remove_cvref_t<T>>
        {
            using ServiceType = std::remove_cvref_t<T>;
            static_assert(
                CanAutoConstructService<ServiceType>::value,
                "service auto-registration requires T() or T(NGIN::Memory::Shared<IServiceProvider>)");

            return [](ServiceResolutionContext& context) -> CoreResult<NGIN::Memory::Shared<ServiceType>>
            {
                if constexpr (std::is_constructible_v<ServiceType, NGIN::Memory::Shared<IServiceProvider>>)
                {
                    auto provider = NGIN::Memory::MakeSharedAs<IServiceProvider, ServiceProviderReference>(
                        &context.services,
                        context.scope);
                    return NGIN::Memory::MakeShared<ServiceType>(std::move(provider));
                }
                else
                {
                    return NGIN::Memory::MakeShared<ServiceType>();
                }
            };
        }
    }

    /// @brief Public typed service-registry interface.
    class NGIN_CORE_API IServiceRegistry : public IServiceProvider
    {
    public:
        ~IServiceRegistry() override = default;

        virtual auto BeginScope(ServiceScopeKind kind, std::string owner) noexcept -> CoreResult<ServiceScopeId> = 0;
        virtual auto EndScope(ServiceScopeId scopeId) noexcept -> CoreResult<void> = 0;

        template<typename T>
        auto RegisterSingleton(ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            options.lifetime = ServiceLifetime::Singleton;
            return RegisterFactory<T>({}, detail::MakeAutoFactory<T>(), std::move(options));
        }

        template<typename T>
        auto RegisterSingleton(std::string name, ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            options.lifetime = ServiceLifetime::Singleton;
            return RegisterFactory<T>(std::move(name), detail::MakeAutoFactory<T>(), std::move(options));
        }

        template<typename T>
        auto RegisterSingleton(
            NGIN::Memory::Shared<std::remove_cvref_t<T>> service,
            ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            return RegisterSingleton<T>({}, std::move(service), std::move(options));
        }

        template<typename T>
        auto RegisterSingleton(
            std::string name,
            NGIN::Memory::Shared<std::remove_cvref_t<T>> service,
            ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            if (!service)
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(KernelErrorCode::InvalidArgument, "Services", name, "singleton service cannot be null"));
            }
            return RegisterProvider(detail::MakeInstanceProvider<T>(std::move(name), std::move(service), std::move(options)));
        }

        template<typename T>
        auto RegisterSingletonValue(T value, ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            using ServiceType = std::remove_cvref_t<T>;
            return RegisterSingleton<ServiceType>(
                {},
                NGIN::Memory::MakeShared<ServiceType>(std::move(value)),
                std::move(options));
        }

        template<typename T>
        auto RegisterSingletonValue(std::string name, T value, ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            using ServiceType = std::remove_cvref_t<T>;
            return RegisterSingleton<ServiceType>(
                std::move(name),
                NGIN::Memory::MakeShared<ServiceType>(std::move(value)),
                std::move(options));
        }

        template<typename T>
        auto RegisterFactory(
            ServiceProviderFactory<std::remove_cvref_t<T>> factory,
            ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            return RegisterFactory<T>({}, std::move(factory), std::move(options));
        }

        template<typename T>
        auto RegisterFactory(
            std::string name,
            ServiceProviderFactory<std::remove_cvref_t<T>> factory,
            ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            if (!factory)
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(KernelErrorCode::InvalidArgument, "Services", name, "service factory cannot be empty"));
            }
            return RegisterProvider(detail::MakeFactoryProvider<T>(std::move(name), std::move(factory), std::move(options)));
        }

        template<typename T>
        auto RegisterScoped(ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            options.lifetime = ServiceLifetime::Scoped;
            return RegisterFactory<T>({}, detail::MakeAutoFactory<T>(), std::move(options));
        }

        template<typename T>
        auto RegisterScoped(
            std::string name,
            ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            options.lifetime = ServiceLifetime::Scoped;
            return RegisterFactory<T>(std::move(name), detail::MakeAutoFactory<T>(), std::move(options));
        }

        template<typename T>
        auto RegisterScoped(
            ServiceProviderFactory<std::remove_cvref_t<T>> factory,
            ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            options.lifetime = ServiceLifetime::Scoped;
            return RegisterFactory<T>({}, std::move(factory), std::move(options));
        }

        template<typename T>
        auto RegisterScoped(
            std::string name,
            ServiceProviderFactory<std::remove_cvref_t<T>> factory,
            ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            options.lifetime = ServiceLifetime::Scoped;
            return RegisterFactory<T>(std::move(name), std::move(factory), std::move(options));
        }

        template<typename T>
        auto RegisterTransient(ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            options.lifetime = ServiceLifetime::Transient;
            return RegisterFactory<T>({}, detail::MakeAutoFactory<T>(), std::move(options));
        }

        template<typename T>
        auto RegisterTransient(std::string name, ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            options.lifetime = ServiceLifetime::Transient;
            return RegisterFactory<T>(std::move(name), detail::MakeAutoFactory<T>(), std::move(options));
        }

        template<typename T>
        auto RegisterTransient(
            ServiceProviderFactory<std::remove_cvref_t<T>> factory,
            ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            options.lifetime = ServiceLifetime::Transient;
            return RegisterFactory<T>({}, std::move(factory), std::move(options));
        }

        template<typename T>
        auto RegisterTransient(
            std::string_view name,
            ServiceProviderFactory<std::remove_cvref_t<T>> factory,
            ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            options.lifetime = ServiceLifetime::Transient;
            return RegisterFactory<T>(std::string(name), std::move(factory), std::move(options));
        }

        virtual auto RegisterProvider(std::shared_ptr<detail::ServiceProviderBase> provider) noexcept -> CoreResult<void> = 0;
    };

    /// @brief Default in-process service registry implementation.
    class NGIN_CORE_API ServiceRegistry final : public IServiceRegistry
    {
    public:
        ServiceRegistry();

        auto BeginScope(ServiceScopeKind kind, std::string owner) noexcept -> CoreResult<ServiceScopeId> override;
        auto EndScope(ServiceScopeId scopeId) noexcept -> CoreResult<void> override;

        [[nodiscard]] auto HasServiceContract(std::string_view contractName) const noexcept -> bool override;
        [[nodiscard]] auto EnumerateKeys() const -> std::vector<std::string> override;
        [[nodiscard]] auto GetScopeInfo(ServiceScopeId scopeId) const noexcept -> CoreResult<ServiceScopeInfo> override;

        auto RegisterProvider(std::shared_ptr<detail::ServiceProviderBase> provider) noexcept -> CoreResult<void> override;

        [[nodiscard]] auto FindProvider(const ServiceKey& key) noexcept -> std::shared_ptr<detail::ServiceProviderBase> override;
        [[nodiscard]] auto EffectiveResolveScope(
            const detail::ServiceProviderBase& provider,
            ServiceScopeId requestedScope) const noexcept -> ServiceScopeId override;
        [[nodiscard]] auto ValidateResolve(
            const detail::ServiceProviderBase& provider,
            ServiceScopeId resolveScope) const noexcept -> CoreResult<void> override;

    private:
        [[nodiscard]] auto ValidateOptions(const ServiceRegistrationOptions& options) const noexcept -> CoreResult<void>;

        mutable std::mutex m_mutex;
        std::unordered_map<ServiceKey, std::shared_ptr<detail::ServiceProviderBase>, ServiceKeyHash> m_entries;
        std::unordered_map<NGIN::UInt64, ServiceScopeInfo> m_scopes;
        NGIN::UInt64 m_nextScopeId {1};
    };

    /// @brief Create a default service registry.
    NGIN_CORE_API auto CreateServiceRegistry() noexcept -> NGIN::Memory::Shared<IServiceRegistry>;
}
