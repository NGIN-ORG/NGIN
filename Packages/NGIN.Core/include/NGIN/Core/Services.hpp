#pragma once

/// @file Services.hpp
/// @brief Typed service registration/query contracts.

#include <NGIN/Core/Errors.hpp>
#include <NGIN/Core/Export.hpp>
#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/Meta/TypeId.hpp>
#include <NGIN/Meta/TypeName.hpp>
#if defined(NGIN_CORE_FEATURE_REFLECTION)
#include <NGIN/Reflection/Reflection.hpp>
#endif

#include <atomic>
#include <concepts>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
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

    /// @brief Compile-time constructor dependency list for automatic services.
    template<typename... TDependencies>
    struct ServiceDependencies final
    {
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

    /// @brief Read-only activation and lifetime details for one registration.
    struct ServiceRegistrationDiagnostics
    {
        ServiceKey              key {};
        ServiceLifetime         lifetime {ServiceLifetime::Singleton};
        ServiceScopeId          ownerScope {ServiceScopeId::Global()};
        ServiceMetadata         metadata {};
        std::vector<ServiceKey> dependencies {};
        NGIN::UInt64            activationCount {0};
        NGIN::UInt64            failureCount {0};
        NGIN::UInt64            cachedInstanceCount {0};
    };

    /// @brief Snapshot of the registered service graph and active scopes.
    struct ServiceRegistryDiagnostics
    {
        std::vector<ServiceRegistrationDiagnostics> registrations {};
        std::vector<ServiceScopeInfo>                scopes {};
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
            using RegistrationValidator = std::function<CoreResult<void>()>;

            ServiceProviderBase(
                ServiceKey key,
                ServiceRegistrationOptions options,
                std::vector<ServiceKey> dependencies = {},
                RegistrationValidator registrationValidator = {})
                : m_key(std::move(key))
                , m_options(std::move(options))
                , m_dependencies(std::move(dependencies))
                , m_registrationValidator(std::move(registrationValidator))
            {
            }

            virtual ~ServiceProviderBase() = default;

            [[nodiscard]] auto Key() const noexcept -> const ServiceKey& { return m_key; }
            [[nodiscard]] auto Options() const noexcept -> const ServiceRegistrationOptions& { return m_options; }
            [[nodiscard]] auto Dependencies() const noexcept -> const std::vector<ServiceKey>& { return m_dependencies; }
            [[nodiscard]] auto RegistrationValidation() const -> CoreResult<void>
            {
                return m_registrationValidator ? m_registrationValidator() : CoreResult<void> {};
            }
            [[nodiscard]] auto Validator() const -> const RegistrationValidator& { return m_registrationValidator; }
            [[nodiscard]] auto ContractName() const -> std::string { return m_key.ContractName(); }
            [[nodiscard]] auto MatchesContract(std::string_view contract) const -> bool
            {
                return m_key.ContractName() == contract;
            }

            virtual void RemoveScope(ServiceScopeId scopeId) noexcept = 0;
            [[nodiscard]] virtual auto Diagnostics() const -> ServiceRegistrationDiagnostics = 0;
#if defined(NGIN_CORE_FEATURE_REFLECTION)
            [[nodiscard]] virtual auto ReflectionParameterTypeName() const noexcept -> std::string_view = 0;
            [[nodiscard]] virtual auto ResolveReflectionArgument(ServiceResolutionContext& context)
                -> CoreResult<NGIN::Reflection::Value> = 0;
#endif
            [[nodiscard]] virtual auto CloneWithOptions(ServiceRegistrationOptions options) const
                -> std::shared_ptr<ServiceProviderBase> = 0;

        private:
            ServiceKey                 m_key {};
            ServiceRegistrationOptions m_options {};
            std::vector<ServiceKey>     m_dependencies {};
            RegistrationValidator       m_registrationValidator {};
        };

        template<typename T>
        class TypedServiceProvider final : public ServiceProviderBase
        {
        public:
            using ServiceType = std::remove_cvref_t<T>;

            TypedServiceProvider(
                ServiceKey key,
                NGIN::Memory::Shared<ServiceType> instance,
                ServiceRegistrationOptions options,
                std::vector<ServiceKey> dependencies = {},
                RegistrationValidator registrationValidator = {})
                : ServiceProviderBase(
                      std::move(key), std::move(options), std::move(dependencies), std::move(registrationValidator))
                , m_instance(std::move(instance))
            {
            }

            TypedServiceProvider(
                ServiceKey key,
                ServiceProviderFactory<ServiceType> factory,
                ServiceRegistrationOptions options,
                std::vector<ServiceKey> dependencies = {},
                RegistrationValidator registrationValidator = {})
                : ServiceProviderBase(
                      std::move(key), std::move(options), std::move(dependencies), std::move(registrationValidator))
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
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_activationChanged.wait(lock, [this]
                    {
                        return !m_singletonActivating || static_cast<bool>(m_instance);
                    });
                    if (m_instance)
                    {
                        return m_instance;
                    }
                    m_singletonActivating = true;
                    lock.unlock();
                    auto created = Create(context);
                    lock.lock();
                    m_singletonActivating = false;
                    if (!created)
                    {
                        lock.unlock();
                        m_activationChanged.notify_all();
                        return NGIN::Utilities::Unexpected<KernelError>(created.Error());
                    }
                    m_instance = created.Value();
                    auto result = m_instance;
                    lock.unlock();
                    m_activationChanged.notify_all();
                    return result;
                }

                if (lifetime == ServiceLifetime::Scoped)
                {
                    const ServiceScopeId activeScope =
                        context.scope.IsGlobal() ? Options().ownerScope : context.scope;
                    std::unique_lock<std::mutex> lock(m_mutex);
                    while (true)
                    {
                        const auto cacheIt = m_scopedCache.find(activeScope.value);
                        if (cacheIt != m_scopedCache.end())
                        {
                            return cacheIt->second;
                        }
                        if (!m_activatingScopes.contains(activeScope.value))
                        {
                            m_activatingScopes.emplace(activeScope.value);
                            break;
                        }
                        m_activationChanged.wait(lock);
                    }
                    lock.unlock();
                    auto created = Create(context);
                    lock.lock();
                    m_activatingScopes.erase(activeScope.value);
                    if (!created)
                    {
                        lock.unlock();
                        m_activationChanged.notify_all();
                        return NGIN::Utilities::Unexpected<KernelError>(created.Error());
                    }
                    auto result = m_scopedCache.emplace(activeScope.value, created.Value()).first->second;
                    lock.unlock();
                    m_activationChanged.notify_all();
                    return result;
                }

                return Create(context);
            }

            void RemoveScope(ServiceScopeId scopeId) noexcept override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_scopedCache.erase(scopeId.value);
            }

            [[nodiscard]] auto Diagnostics() const -> ServiceRegistrationDiagnostics override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return ServiceRegistrationDiagnostics {
                    .key = Key(),
                    .lifetime = Options().lifetime,
                    .ownerScope = Options().ownerScope,
                    .metadata = Options().metadata,
                    .dependencies = Dependencies(),
                    .activationCount = m_activationCount.load(std::memory_order_relaxed),
                    .failureCount = m_failureCount.load(std::memory_order_relaxed),
                    .cachedInstanceCount = static_cast<NGIN::UInt64>((m_instance ? 1U : 0U) + m_scopedCache.size()),
                };
            }

#if defined(NGIN_CORE_FEATURE_REFLECTION)
            [[nodiscard]] auto ReflectionParameterTypeName() const noexcept -> std::string_view override
            {
                return NGIN::Meta::TypeName<NGIN::Memory::Shared<ServiceType>>::qualifiedName;
            }

            [[nodiscard]] auto ResolveReflectionArgument(ServiceResolutionContext& context)
                -> CoreResult<NGIN::Reflection::Value> override
            {
                auto resolved = Resolve(context);
                if (!resolved)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(resolved.Error());
                }
                return NGIN::Reflection::MakeInstanceValue(resolved.Value());
            }
#endif

            [[nodiscard]] auto CloneWithOptions(ServiceRegistrationOptions options) const
                -> std::shared_ptr<ServiceProviderBase> override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_factory)
                {
                    return std::make_shared<TypedServiceProvider<ServiceType>>(
                        Key(), m_factory, std::move(options), Dependencies(), Validator());
                }

                return std::make_shared<TypedServiceProvider<ServiceType>>(
                    Key(), m_instance, std::move(options), Dependencies(), Validator());
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

                    ++m_failureCount;
                    return NGIN::Utilities::Unexpected<KernelError>(
                        MakeKernelError(KernelErrorCode::InvalidState, "Services", Key().ContractName(), "service provider has no instance or factory"));
                }

                auto created = m_factory(context);
                if (!created)
                {
                    ++m_failureCount;
                    return NGIN::Utilities::Unexpected<KernelError>(created.Error());
                }
                if (!created.Value())
                {
                    ++m_failureCount;
                    return NGIN::Utilities::Unexpected<KernelError>(
                        MakeKernelError(KernelErrorCode::InvalidState, "Services", Key().ContractName(), "service factory returned null"));
                }
                ++m_activationCount;
                return created.Value();
            }

            mutable std::mutex m_mutex {};
            mutable std::condition_variable m_activationChanged {};
            NGIN::Memory::Shared<ServiceType> m_instance {};
            ServiceProviderFactory<ServiceType> m_factory {};
            std::unordered_map<NGIN::UInt64, NGIN::Memory::Shared<ServiceType>> m_scopedCache {};
            std::unordered_set<NGIN::UInt64> m_activatingScopes {};
            bool m_singletonActivating {false};
            std::atomic<NGIN::UInt64> m_activationCount {0};
            std::atomic<NGIN::UInt64> m_failureCount {0};
        };

        class ServiceProviderReference;

        class ServiceResolutionGuard final
        {
        public:
            [[nodiscard]] static auto Validate(const ServiceProviderBase& provider) noexcept -> CoreResult<void>
            {
                for (const auto& frame : s_stack)
                {
                    if (frame.provider == &provider)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
                            KernelErrorCode::DependencyCycle,
                            "Services",
                            provider.ContractName(),
                            "service dependency cycle: " + BuildPath(provider),
                            BuildPath(provider)));
                    }
                }

                if (provider.Options().lifetime == ServiceLifetime::Scoped)
                {
                    for (const auto& frame : s_stack)
                    {
                        if (frame.lifetime == ServiceLifetime::Singleton)
                        {
                            return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
                                KernelErrorCode::InvalidArgument,
                                "Services",
                                provider.ContractName(),
                                "singleton service cannot capture scoped dependency: " + BuildPath(provider),
                                BuildPath(provider)));
                        }
                    }
                }
                return CoreResult<void> {};
            }

            explicit ServiceResolutionGuard(const ServiceProviderBase& provider)
            {
                s_stack.push_back(Frame {
                    .provider = &provider,
                    .key = provider.Key(),
                    .lifetime = provider.Options().lifetime,
                });
            }

            ~ServiceResolutionGuard()
            {
                s_stack.pop_back();
            }

            ServiceResolutionGuard(const ServiceResolutionGuard&) = delete;
            auto operator=(const ServiceResolutionGuard&) -> ServiceResolutionGuard& = delete;

            [[nodiscard]] static auto CurrentPath() -> std::string
            {
                std::string path;
                for (const auto& frame : s_stack)
                {
                    if (!path.empty())
                    {
                        path += " -> ";
                    }
                    path += frame.key.ContractName();
                }
                return path;
            }

        private:
            struct Frame
            {
                const ServiceProviderBase* provider {nullptr};
                ServiceKey                 key {};
                ServiceLifetime            lifetime {ServiceLifetime::Singleton};
            };

            [[nodiscard]] static auto BuildPath(const ServiceProviderBase& provider) -> std::string
            {
                auto path = CurrentPath();
                if (!path.empty())
                {
                    path += " -> ";
                }
                path += provider.ContractName();
                return path;
            }

            inline static thread_local std::vector<Frame> s_stack {};
        };

        template<typename T, typename = void>
        struct DeclaredServiceDependencies
        {
            using ServiceType = std::remove_cvref_t<T>;
            using Type = ServiceDependencies<>;
            static constexpr bool declared = false;
        };

        template<typename T>
        struct DeclaredServiceDependencies<T, std::void_t<typename std::remove_cvref_t<T>::Dependencies>>
        {
            using Type = typename std::remove_cvref_t<T>::Dependencies;
            static constexpr bool declared = true;
        };

        template<typename T>
        struct IsServiceDependencies : std::false_type
        {
        };

        template<typename... TDependencies>
        struct IsServiceDependencies<ServiceDependencies<TDependencies...>> : std::true_type
        {
        };

        template<typename T>
        struct CanAutoConstructService
        {
            using ServiceType = std::remove_cvref_t<T>;
            static constexpr bool value =
                DeclaredServiceDependencies<ServiceType>::declared ||
                std::is_constructible_v<ServiceType, NGIN::Memory::Shared<IServiceProvider>> ||
                std::is_default_constructible_v<ServiceType>;
        };

        template<typename T>
        [[nodiscard]] auto MakeAutoFactory() -> ServiceProviderFactory<std::remove_cvref_t<T>>;

        template<typename TService, typename TImplementation>
        [[nodiscard]] auto MakeAutoFactoryAs() -> ServiceProviderFactory<std::remove_cvref_t<TService>>;

        template<typename T>
        [[nodiscard]] auto AutoDependencyKeys() -> std::vector<ServiceKey>;

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
            ServiceRegistrationOptions options,
            std::vector<ServiceKey> dependencies = {},
            ServiceProviderBase::RegistrationValidator registrationValidator = {})
            -> std::shared_ptr<ServiceProviderBase>
        {
            using ServiceType = std::remove_cvref_t<T>;
            return std::make_shared<TypedServiceProvider<ServiceType>>(
                TypeServiceKey<ServiceType>(std::move(name)),
                std::move(factory),
                std::move(options),
                std::move(dependencies),
                std::move(registrationValidator));
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
        [[nodiscard]] virtual auto Diagnostics() const -> ServiceRegistryDiagnostics = 0;
#if defined(NGIN_CORE_FEATURE_REFLECTION)
        [[nodiscard]] virtual auto FindReflectionProvider(
            std::string_view parameterTypeName,
            std::string_view name) noexcept -> std::shared_ptr<detail::ServiceProviderBase> = 0;
#endif

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

            auto resolutionValid = detail::ServiceResolutionGuard::Validate(*provider);
            if (!resolutionValid)
            {
                return NGIN::Utilities::Unexpected<KernelError>(resolutionValid.Error());
            }
            detail::ServiceResolutionGuard resolutionGuard(*provider);

            ServiceResolutionContext context {
                .services = *this,
                .scope = effectiveScope,
            };
            auto resolved = typed->Resolve(context);
            if (!resolved)
            {
                auto error = resolved.Error();
                if (error.dependencyPath.empty())
                {
                    error.dependencyPath = detail::ServiceResolutionGuard::CurrentPath();
                }
                return NGIN::Utilities::Unexpected<KernelError>(std::move(error));
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
                auto dependencyPath = detail::ServiceResolutionGuard::CurrentPath();
                if (!dependencyPath.empty())
                {
                    dependencyPath += " -> ";
                }
                dependencyPath += key.ContractName();
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(
                        KernelErrorCode::NotFound,
                        "Services",
                        key.ContractName(),
                        "service not found: " + key.ContractName(),
                        std::move(dependencyPath)));
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

            [[nodiscard]] auto Diagnostics() const -> ServiceRegistryDiagnostics override
            {
                return m_provider != nullptr ? m_provider->Diagnostics() : ServiceRegistryDiagnostics {};
            }

#if defined(NGIN_CORE_FEATURE_REFLECTION)
            [[nodiscard]] auto FindReflectionProvider(
                std::string_view parameterTypeName,
                std::string_view name) noexcept -> std::shared_ptr<ServiceProviderBase> override
            {
                return m_provider != nullptr ? m_provider->FindReflectionProvider(parameterTypeName, name) : nullptr;
            }
#endif

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

#if defined(NGIN_CORE_FEATURE_REFLECTION)
        struct ReflectedParameterPlan
        {
            std::string parameterTypeName {};
            std::string serviceName {};
            bool optional {false};
        };

        [[nodiscard]] inline auto ReflectionError(
            const std::string_view service,
            const NGIN::Reflection::Error& error) -> KernelError
        {
            const auto code = error.code == NGIN::Reflection::ErrorCode::StaleHandle
                                  ? KernelErrorCode::InvalidState
                                  : KernelErrorCode::ServiceRegistrationFailure;
            return MakeKernelError(code, "Services.Reflection", std::string(service), error.message);
        }

        template<typename TService, typename TImplementation>
        class ReflectedActivationPlan final
        {
        public:
            using ServiceType = std::remove_cvref_t<TService>;
            using ImplementationType = std::remove_cvref_t<TImplementation>;

            [[nodiscard]] auto Validate() -> CoreResult<void>
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return Rebuild();
            }

            [[nodiscard]] auto Activate(ServiceResolutionContext& context)
                -> CoreResult<NGIN::Memory::Shared<ServiceType>>
            {
                NGIN::Reflection::Constructor constructor;
                std::vector<ReflectedParameterPlan> parameters;
                NGIN::UInt64 generation = 0;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    const auto currentGeneration = NGIN::Reflection::GetRegistrySnapshot().generation;
                    if (!m_initialized || currentGeneration != m_generation)
                    {
                        auto rebuilt = Rebuild();
                        if (!rebuilt)
                        {
                            return NGIN::Utilities::Unexpected<KernelError>(rebuilt.Error());
                        }
                    }
                    constructor = m_constructor;
                    parameters = m_parameters;
                    generation = m_generation;
                }

                std::vector<NGIN::Reflection::Value> arguments;
                arguments.reserve(parameters.size());
                for (const auto& parameter : parameters)
                {
                    auto provider = context.services.FindReflectionProvider(
                        parameter.parameterTypeName, parameter.serviceName);
                    if (!provider)
                    {
                        if (parameter.optional)
                        {
                            arguments.emplace_back();
                            continue;
                        }

                        auto dependencyPath = ServiceResolutionGuard::CurrentPath();
                        if (!dependencyPath.empty())
                        {
                            dependencyPath += " -> ";
                        }
                        dependencyPath += parameter.serviceName.empty()
                                              ? parameter.parameterTypeName
                                              : parameter.serviceName;
                        return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
                            KernelErrorCode::NotFound,
                            "Services.Reflection",
                            TypeServiceKey<ServiceType>().ContractName(),
                            "reflected constructor dependency not found",
                            std::move(dependencyPath)));
                    }

                    const auto effectiveScope = context.services.EffectiveResolveScope(*provider, context.scope);
                    auto valid = context.services.ValidateResolve(*provider, effectiveScope);
                    if (!valid)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(valid.Error());
                    }
                    auto resolutionValid = ServiceResolutionGuard::Validate(*provider);
                    if (!resolutionValid)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(resolutionValid.Error());
                    }
                    ServiceResolutionGuard guard(*provider);
                    ServiceResolutionContext dependencyContext {
                        .services = context.services,
                        .scope = effectiveScope,
                    };
                    auto argument = provider->ResolveReflectionArgument(dependencyContext);
                    if (!argument)
                    {
                        auto error = argument.Error();
                        if (error.dependencyPath.empty())
                        {
                            error.dependencyPath = ServiceResolutionGuard::CurrentPath();
                        }
                        return NGIN::Utilities::Unexpected<KernelError>(std::move(error));
                    }
                    arguments.push_back(std::move(argument.Value()));
                }

                if (generation != NGIN::Reflection::GetRegistrySnapshot().generation)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
                        KernelErrorCode::InvalidState,
                        "Services.Reflection",
                        TypeServiceKey<ServiceType>().ContractName(),
                        "reflection metadata changed during service activation"));
                }

                auto reflected = constructor.Invoke(arguments);
                if (!reflected)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(
                        ReflectionError(TypeServiceKey<ServiceType>().ContractName(), reflected.error()));
                }
                auto instance = std::move(reflected.value());
                auto* implementation = instance.template TryAs<ImplementationType>();
                if (implementation == nullptr)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
                        KernelErrorCode::InvalidState,
                        "Services.Reflection",
                        TypeServiceKey<ServiceType>().ContractName(),
                        "reflected constructor returned an incompatible instance"));
                }
                return NGIN::Memory::MakeSharedAlias<ServiceType>(
                    static_cast<ServiceType*>(implementation), std::move(instance));
            }

        private:
            [[nodiscard]] auto Rebuild() -> CoreResult<void>
            {
                const auto snapshot = NGIN::Reflection::GetRegistrySnapshot();
                auto reflectedType = NGIN::Reflection::GetType<ImplementationType>();
                if (!reflectedType)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(
                        ReflectionError(TypeServiceKey<ServiceType>().ContractName(), reflectedType.error()));
                }

                std::optional<NGIN::Reflection::Constructor> injectable;
                for (std::size_t index = 0; index < reflectedType->ConstructorCount(); ++index)
                {
                    auto candidate = reflectedType->ConstructorAt(index);
                    if (!candidate)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(
                            ReflectionError(TypeServiceKey<ServiceType>().ContractName(), candidate.error()));
                    }
                    if (candidate->IsInjectable())
                    {
                        if (injectable.has_value())
                        {
                            return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
                                KernelErrorCode::ServiceRegistrationFailure,
                                "Services.Reflection",
                                TypeServiceKey<ServiceType>().ContractName(),
                                "reflected service has more than one injectable constructor"));
                        }
                        injectable = *candidate;
                    }
                }
                if (!injectable.has_value())
                {
                    return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
                        KernelErrorCode::ServiceRegistrationFailure,
                        "Services.Reflection",
                        TypeServiceKey<ServiceType>().ContractName(),
                        "reflected service has no injectable constructor"));
                }

                std::vector<ReflectedParameterPlan> parameters;
                parameters.reserve(injectable->ParameterCount());
                for (std::size_t index = 0; index < injectable->ParameterCount(); ++index)
                {
                    auto binding = injectable->ParameterBindingAt(index);
                    if (!binding)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(
                            ReflectionError(TypeServiceKey<ServiceType>().ContractName(), binding.error()));
                    }
                    auto parameterTypeName = std::string(injectable->ParameterTypeName(index));
                    if (parameterTypeName.find("NGIN::Memory::Shared") == std::string::npos)
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
                            KernelErrorCode::ServiceRegistrationFailure,
                            "Services.Reflection",
                            TypeServiceKey<ServiceType>().ContractName(),
                            "injectable constructor parameters must use NGIN::Memory::Shared<T>"));
                    }
                    parameters.push_back(ReflectedParameterPlan {
                        .parameterTypeName = std::move(parameterTypeName),
                        .serviceName = std::move(binding->name),
                        .optional = binding->optional,
                    });
                }

                m_constructor = *injectable;
                m_parameters = std::move(parameters);
                m_generation = snapshot.generation;
                m_initialized = true;
                return CoreResult<void> {};
            }

            std::mutex m_mutex {};
            NGIN::Reflection::Constructor m_constructor {};
            std::vector<ReflectedParameterPlan> m_parameters {};
            NGIN::UInt64 m_generation {0};
            bool m_initialized {false};
        };

        template<typename TService, typename TImplementation>
        [[nodiscard]] auto TryMakeReflectedFactory()
            -> std::optional<ServiceProviderFactory<std::remove_cvref_t<TService>>>
        {
            using ImplementationType = std::remove_cvref_t<TImplementation>;
            auto reflectedType = NGIN::Reflection::FindType<ImplementationType>();
            if (!reflectedType.has_value())
            {
                return std::nullopt;
            }

            bool hasInjectableConstructor = false;
            for (std::size_t index = 0; index < reflectedType->ConstructorCount(); ++index)
            {
                auto constructor = reflectedType->ConstructorAt(index);
                hasInjectableConstructor = hasInjectableConstructor ||
                                           (constructor.has_value() && constructor->IsInjectable());
            }
            if (!hasInjectableConstructor)
            {
                return std::nullopt;
            }

            auto plan = std::make_shared<ReflectedActivationPlan<TService, TImplementation>>();
            return ServiceProviderFactory<std::remove_cvref_t<TService>> {
                [plan](ServiceResolutionContext& context)
                {
                    return plan->Activate(context);
                }};
        }

        template<typename TService, typename TImplementation>
        [[nodiscard]] auto MakeAutoRegistrationValidator()
            -> ServiceProviderBase::RegistrationValidator
        {
            using ImplementationType = std::remove_cvref_t<TImplementation>;
            auto reflectedType = NGIN::Reflection::FindType<ImplementationType>();
            if (!reflectedType.has_value())
            {
                if constexpr (CanAutoConstructService<ImplementationType>::value)
                {
                    return {};
                }
                else
                {
                    return []() -> CoreResult<void>
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
                            KernelErrorCode::ReflectionRequired,
                            "Services.Reflection",
                            TypeServiceKey<TService>().ContractName(),
                            "service has no reflected metadata or reflection-free constructor"));
                    };
                }
            }

            bool hasInjectableConstructor = false;
            for (std::size_t index = 0; index < reflectedType->ConstructorCount(); ++index)
            {
                auto constructor = reflectedType->ConstructorAt(index);
                hasInjectableConstructor = hasInjectableConstructor ||
                                           (constructor.has_value() && constructor->IsInjectable());
            }
            if (!hasInjectableConstructor)
            {
                if constexpr (CanAutoConstructService<ImplementationType>::value)
                {
                    return {};
                }
                else
                {
                    return []() -> CoreResult<void>
                    {
                        return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
                            KernelErrorCode::ServiceRegistrationFailure,
                            "Services.Reflection",
                            TypeServiceKey<TService>().ContractName(),
                            "reflected service has no injectable constructor"));
                    };
                }
            }

            auto plan = std::make_shared<ReflectedActivationPlan<TService, TImplementation>>();
            return [plan]
            {
                return plan->Validate();
            };
        }
#endif

        template<typename TService, typename TImplementation>
        [[nodiscard]] auto MakeAutomaticRegistrationValidator()
            -> ServiceProviderBase::RegistrationValidator
        {
#if defined(NGIN_CORE_FEATURE_REFLECTION)
            return MakeAutoRegistrationValidator<TService, TImplementation>();
#else
            return {};
#endif
        }

        template<std::size_t TIndex = 0, typename... TDependencies>
        [[nodiscard]] auto ResolveDeclaredDependencies(
            ServiceResolutionContext& context,
            std::tuple<NGIN::Memory::Shared<std::remove_cvref_t<TDependencies>>...>& values)
            -> CoreResult<void>
        {
            if constexpr (TIndex == sizeof...(TDependencies))
            {
                return CoreResult<void> {};
            }
            else
            {
                using DependencyType = std::tuple_element_t<TIndex, std::tuple<TDependencies...>>;
                auto dependency = context.services.ResolveRequired<std::remove_cvref_t<DependencyType>>(context.scope);
                if (!dependency)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(dependency.Error());
                }
                std::get<TIndex>(values) = dependency.Value();
                return ResolveDeclaredDependencies<TIndex + 1, TDependencies...>(context, values);
            }
        }

        template<typename TService, typename TImplementation, typename... TDependencies>
        [[nodiscard]] auto MakeDeclaredDependencyFactory(ServiceDependencies<TDependencies...>)
            -> ServiceProviderFactory<TService>
        {
            static_assert(
                std::is_constructible_v<TImplementation, NGIN::Memory::Shared<std::remove_cvref_t<TDependencies>>...>,
                "service Dependencies must match a constructible implementation constructor");

            return [](ServiceResolutionContext& context) -> CoreResult<NGIN::Memory::Shared<TService>>
            {
                std::tuple<NGIN::Memory::Shared<std::remove_cvref_t<TDependencies>>...> dependencies {};
                auto resolved = ResolveDeclaredDependencies<0, TDependencies...>(context, dependencies);
                if (!resolved)
                {
                    return NGIN::Utilities::Unexpected<KernelError>(resolved.Error());
                }

                if constexpr (std::is_same_v<TService, TImplementation>)
                {
                    return std::apply(
                        [](auto&&... arguments)
                        {
                            return NGIN::Memory::MakeShared<TImplementation>(std::move(arguments)...);
                        },
                        std::move(dependencies));
                }
                else
                {
                    static_assert(std::derived_from<TImplementation, TService>);
                    static_assert(std::has_virtual_destructor_v<TService>);
                    return std::apply(
                        [](auto&&... arguments)
                        {
                            return NGIN::Memory::MakeSharedAs<TService, TImplementation>(std::move(arguments)...);
                        },
                        std::move(dependencies));
                }
            };
        }

        template<typename... TDependencies>
        [[nodiscard]] auto DependencyKeys(ServiceDependencies<TDependencies...>) -> std::vector<ServiceKey>
        {
            return {TypeServiceKey<std::remove_cvref_t<TDependencies>>()...};
        }

        template<typename TService, typename TImplementation>
        [[nodiscard]] auto MakeAutoFactoryAs() -> ServiceProviderFactory<std::remove_cvref_t<TService>>
        {
            using ServiceType = std::remove_cvref_t<TService>;
            using ImplementationType = std::remove_cvref_t<TImplementation>;
            using DependencyList = typename DeclaredServiceDependencies<ImplementationType>::Type;

            static_assert(
                std::is_same_v<ServiceType, ImplementationType> || std::derived_from<ImplementationType, ServiceType>,
                "service implementation must derive from its service interface");
            static_assert(
                IsServiceDependencies<DependencyList>::value,
                "T::Dependencies must be NGIN::Core::ServiceDependencies<...>");
#if !defined(NGIN_CORE_FEATURE_REFLECTION)
            static_assert(
                CanAutoConstructService<ImplementationType>::value,
                "service auto-registration requires Dependencies, T(), or T(NGIN::Memory::Shared<IServiceProvider>)");
#endif

            if constexpr (DeclaredServiceDependencies<ImplementationType>::declared)
            {
                return MakeDeclaredDependencyFactory<ServiceType, ImplementationType>(DependencyList {});
            }
#if defined(NGIN_CORE_FEATURE_REFLECTION)
            else if (auto reflectedFactory = TryMakeReflectedFactory<ServiceType, ImplementationType>();
                     reflectedFactory.has_value())
            {
                return std::move(*reflectedFactory);
            }
#endif
            else if constexpr (std::is_constructible_v<ImplementationType, NGIN::Memory::Shared<IServiceProvider>>)
            {
                return [](ServiceResolutionContext& context) -> CoreResult<NGIN::Memory::Shared<ServiceType>>
                {
                    auto provider = NGIN::Memory::MakeSharedAs<IServiceProvider, ServiceProviderReference>(
                        &context.services,
                        context.scope);
                    if constexpr (std::is_same_v<ServiceType, ImplementationType>)
                    {
                        return NGIN::Memory::MakeShared<ImplementationType>(std::move(provider));
                    }
                    else
                    {
                        static_assert(std::has_virtual_destructor_v<ServiceType>);
                        return NGIN::Memory::MakeSharedAs<ServiceType, ImplementationType>(std::move(provider));
                    }
                };
            }
            else if constexpr (std::is_default_constructible_v<ImplementationType>)
            {
                return []([[maybe_unused]] ServiceResolutionContext& context) -> CoreResult<NGIN::Memory::Shared<ServiceType>>
                {
                    if constexpr (std::is_same_v<ServiceType, ImplementationType>)
                    {
                        return NGIN::Memory::MakeShared<ImplementationType>();
                    }
                    else
                    {
                        static_assert(std::has_virtual_destructor_v<ServiceType>);
                        return NGIN::Memory::MakeSharedAs<ServiceType, ImplementationType>();
                    }
                };
            }
#if defined(NGIN_CORE_FEATURE_REFLECTION)
            else
            {
                return [](ServiceResolutionContext&) -> CoreResult<NGIN::Memory::Shared<ServiceType>>
                {
                    return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
                        KernelErrorCode::ReflectionRequired,
                        "Services.Reflection",
                        TypeServiceKey<ServiceType>().ContractName(),
                        "service has no injectable reflected constructor or reflection-free constructor"));
                };
            }
#endif
        }

        template<typename T>
        [[nodiscard]] auto MakeAutoFactory() -> ServiceProviderFactory<std::remove_cvref_t<T>>
        {
            return MakeAutoFactoryAs<T, T>();
        }

        template<typename T>
        [[nodiscard]] auto AutoDependencyKeys() -> std::vector<ServiceKey>
        {
            using ServiceType = std::remove_cvref_t<T>;
            using DependencyList = typename DeclaredServiceDependencies<ServiceType>::Type;
            static_assert(IsServiceDependencies<DependencyList>::value);
            return DependencyKeys(DependencyList {});
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
            return RegisterProvider(detail::MakeFactoryProvider<T>(
                {}, detail::MakeAutoFactory<T>(), std::move(options), detail::AutoDependencyKeys<T>(),
                detail::MakeAutomaticRegistrationValidator<T, T>()));
        }

        template<typename T>
        auto RegisterSingleton(std::string name, ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            options.lifetime = ServiceLifetime::Singleton;
            return RegisterProvider(detail::MakeFactoryProvider<T>(
                std::move(name), detail::MakeAutoFactory<T>(), std::move(options), detail::AutoDependencyKeys<T>(),
                detail::MakeAutomaticRegistrationValidator<T, T>()));
        }

        template<typename TService, typename TImplementation>
        auto RegisterSingleton(ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            options.lifetime = ServiceLifetime::Singleton;
            return RegisterProvider(detail::MakeFactoryProvider<TService>(
                {},
                detail::MakeAutoFactoryAs<TService, TImplementation>(),
                std::move(options),
                detail::AutoDependencyKeys<TImplementation>(),
                detail::MakeAutomaticRegistrationValidator<TService, TImplementation>()));
        }

        template<typename TService, typename TImplementation>
        auto RegisterSingleton(std::string name, ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            options.lifetime = ServiceLifetime::Singleton;
            return RegisterProvider(detail::MakeFactoryProvider<TService>(
                std::move(name),
                detail::MakeAutoFactoryAs<TService, TImplementation>(),
                std::move(options),
                detail::AutoDependencyKeys<TImplementation>(),
                detail::MakeAutomaticRegistrationValidator<TService, TImplementation>()));
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
            return RegisterProvider(detail::MakeFactoryProvider<T>(
                {}, detail::MakeAutoFactory<T>(), std::move(options), detail::AutoDependencyKeys<T>(),
                detail::MakeAutomaticRegistrationValidator<T, T>()));
        }

        template<typename T>
        auto RegisterScoped(
            std::string name,
            ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            options.lifetime = ServiceLifetime::Scoped;
            return RegisterProvider(detail::MakeFactoryProvider<T>(
                std::move(name), detail::MakeAutoFactory<T>(), std::move(options), detail::AutoDependencyKeys<T>(),
                detail::MakeAutomaticRegistrationValidator<T, T>()));
        }

        template<typename TService, typename TImplementation>
        auto RegisterScoped(ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            options.lifetime = ServiceLifetime::Scoped;
            return RegisterProvider(detail::MakeFactoryProvider<TService>(
                {},
                detail::MakeAutoFactoryAs<TService, TImplementation>(),
                std::move(options),
                detail::AutoDependencyKeys<TImplementation>(),
                detail::MakeAutomaticRegistrationValidator<TService, TImplementation>()));
        }

        template<typename TService, typename TImplementation>
        auto RegisterScoped(std::string name, ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            options.lifetime = ServiceLifetime::Scoped;
            return RegisterProvider(detail::MakeFactoryProvider<TService>(
                std::move(name),
                detail::MakeAutoFactoryAs<TService, TImplementation>(),
                std::move(options),
                detail::AutoDependencyKeys<TImplementation>(),
                detail::MakeAutomaticRegistrationValidator<TService, TImplementation>()));
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
            return RegisterProvider(detail::MakeFactoryProvider<T>(
                {}, detail::MakeAutoFactory<T>(), std::move(options), detail::AutoDependencyKeys<T>(),
                detail::MakeAutomaticRegistrationValidator<T, T>()));
        }

        template<typename T>
        auto RegisterTransient(std::string name, ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            options.lifetime = ServiceLifetime::Transient;
            return RegisterProvider(detail::MakeFactoryProvider<T>(
                std::move(name), detail::MakeAutoFactory<T>(), std::move(options), detail::AutoDependencyKeys<T>(),
                detail::MakeAutomaticRegistrationValidator<T, T>()));
        }

        template<typename TService, typename TImplementation>
        auto RegisterTransient(ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            options.lifetime = ServiceLifetime::Transient;
            return RegisterProvider(detail::MakeFactoryProvider<TService>(
                {},
                detail::MakeAutoFactoryAs<TService, TImplementation>(),
                std::move(options),
                detail::AutoDependencyKeys<TImplementation>(),
                detail::MakeAutomaticRegistrationValidator<TService, TImplementation>()));
        }

        template<typename TService, typename TImplementation>
        auto RegisterTransient(std::string name, ServiceRegistrationOptions options = {}) noexcept -> CoreResult<void>
        {
            options.lifetime = ServiceLifetime::Transient;
            return RegisterProvider(detail::MakeFactoryProvider<TService>(
                std::move(name),
                detail::MakeAutoFactoryAs<TService, TImplementation>(),
                std::move(options),
                detail::AutoDependencyKeys<TImplementation>(),
                detail::MakeAutomaticRegistrationValidator<TService, TImplementation>()));
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
        [[nodiscard]] auto Diagnostics() const -> ServiceRegistryDiagnostics override;
#if defined(NGIN_CORE_FEATURE_REFLECTION)
        [[nodiscard]] auto FindReflectionProvider(
            std::string_view parameterTypeName,
            std::string_view name) noexcept -> std::shared_ptr<detail::ServiceProviderBase> override;
#endif

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
