#include <NGIN/Core/Services.hpp>

#include <algorithm>

namespace NGIN::Core
{
    ServiceRegistry::ServiceRegistry()
    {
        m_scopes.emplace(ServiceScopeId::Global().value, ServiceScopeInfo {
            .id = ServiceScopeId::Global(),
            .kind = ServiceScopeKind::Kernel,
            .owner = "Kernel"});
    }

    auto ServiceRegistry::BeginScope(const ServiceScopeKind kind, std::string owner) noexcept -> CoreResult<ServiceScopeId>
    {
        ServiceScopeInfo info {};
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const ServiceScopeId id {m_nextScopeId++};
            info = ServiceScopeInfo {
                .id = id,
                .kind = kind,
                .owner = std::move(owner),
            };
            m_scopes.emplace(id.value, info);
        }
        return info.id;
    }

    auto ServiceRegistry::EndScope(const ServiceScopeId scopeId) noexcept -> CoreResult<void>
    {
        if (scopeId.IsGlobal())
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Services", {}, "global scope cannot be ended"));
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_scopes.erase(scopeId.value))
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::NotFound, "Services", {}, "scope not found"));
        }

        for (auto entryIt = m_entries.begin(); entryIt != m_entries.end();)
        {
            entryIt->second.scopedCache.erase(scopeId.value);
            if (entryIt->second.options.ownerScope == scopeId)
            {
                entryIt = m_entries.erase(entryIt);
            }
            else
            {
                ++entryIt;
            }
        }

        return CoreResult<void> {};
    }

    auto ServiceRegistry::ValidateOptions(const ServiceRegistrationOptions& options) const noexcept -> CoreResult<void>
    {
        if (options.lifetime != ServiceLifetime::Singleton && options.ownerScope.IsGlobal())
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(
                    KernelErrorCode::InvalidArgument,
                    "Services",
                    {},
                    "scoped/transient providers must declare non-global owner scope"));
        }

        if (!options.ownerScope.IsGlobal() && !m_scopes.contains(options.ownerScope.value))
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::NotFound, "Services", {}, "owner scope not found"));
        }

        return CoreResult<void> {};
    }

    auto ServiceRegistry::RegisterInstance(
        std::string key,
        NGIN::Utilities::Any<> service,
        ServiceRegistrationOptions options) noexcept -> CoreResult<void>
    {
        if (key.empty())
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Services", {}, "service key cannot be empty"));
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        if (auto valid = ValidateOptions(options); !valid)
        {
            return NGIN::Utilities::Unexpected<KernelError>(valid.ErrorUnsafe());
        }

        if (options.lifetime != ServiceLifetime::Singleton)
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(
                    KernelErrorCode::InvalidArgument,
                    "Services",
                    key,
                    "RegisterInstance currently supports singleton lifetime only"));
        }

        if (m_entries.contains(key))
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::AlreadyExists, "Services", {}, "service key already registered: " + key));
        }

        Entry entry {};
        entry.singletonInstance.emplace(std::move(service));
        entry.options = std::move(options);
        m_entries.emplace(std::move(key), std::move(entry));
        return CoreResult<void> {};
    }

    auto ServiceRegistry::RegisterFactory(
        std::string key,
        ServiceFactory factory,
        ServiceRegistrationOptions options) noexcept -> CoreResult<void>
    {
        if (key.empty())
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Services", {}, "service key cannot be empty"));
        }
        if (!factory)
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Services", {}, "service factory cannot be empty"));
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        if (auto valid = ValidateOptions(options); !valid)
        {
            return NGIN::Utilities::Unexpected<KernelError>(valid.ErrorUnsafe());
        }

        if (m_entries.contains(key))
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::AlreadyExists, "Services", {}, "service key already registered: " + key));
        }

        Entry entry {};
        entry.factory.emplace(std::move(factory));
        entry.options = std::move(options);
        m_entries.emplace(std::move(key), std::move(entry));
        return CoreResult<void> {};
    }

    auto ServiceRegistry::ResolveOptional(const std::string_view key, const ServiceScopeId resolveScope) noexcept
        -> CoreResult<std::optional<NGIN::Utilities::Any<>>>
    {
        ServiceFactory factory {};
        ServiceLifetime lifetime = ServiceLifetime::Singleton;
        ServiceScopeId ownerScope = ServiceScopeId::Global();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto it = m_entries.find(std::string(key));
            if (it == m_entries.end())
            {
                return std::optional<NGIN::Utilities::Any<>> {};
            }

            lifetime = it->second.options.lifetime;
            ownerScope = it->second.options.ownerScope;

            if (lifetime == ServiceLifetime::Singleton && it->second.singletonInstance.has_value())
            {
                return it->second.singletonInstance;
            }

            if (lifetime == ServiceLifetime::Scoped)
            {
                const ServiceScopeId activeScope = resolveScope.IsGlobal() ? ownerScope : resolveScope;
                if (activeScope.IsGlobal())
                {
                    return NGIN::Utilities::Unexpected<KernelError>(
                        MakeKernelError(KernelErrorCode::InvalidArgument, "Services", std::string(key), "scoped resolve requires non-global scope"));
                }
                if (!m_scopes.contains(activeScope.value))
                {
                    return NGIN::Utilities::Unexpected<KernelError>(
                        MakeKernelError(KernelErrorCode::NotFound, "Services", std::string(key), "resolve scope not found"));
                }
                const auto cacheIt = it->second.scopedCache.find(activeScope.value);
                if (cacheIt != it->second.scopedCache.end())
                {
                    return std::optional<NGIN::Utilities::Any<>> {cacheIt->second};
                }
            }

            if (!it->second.factory.has_value())
            {
                return std::optional<NGIN::Utilities::Any<>> {};
            }
            factory = *it->second.factory;
        }

        auto created = factory();
        if (!created)
        {
            return NGIN::Utilities::Unexpected<KernelError>(created.ErrorUnsafe());
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(std::string(key));
        if (it == m_entries.end())
        {
            return std::optional<NGIN::Utilities::Any<>> {created.ValueUnsafe()};
        }

        switch (lifetime)
        {
            case ServiceLifetime::Singleton:
                if (!it->second.singletonInstance.has_value())
                {
                    it->second.singletonInstance.emplace(created.ValueUnsafe());
                }
                return it->second.singletonInstance;
            case ServiceLifetime::Scoped:
            {
                const ServiceScopeId activeScope = resolveScope.IsGlobal() ? ownerScope : resolveScope;
                if (activeScope.IsGlobal())
                {
                    return NGIN::Utilities::Unexpected<KernelError>(
                        MakeKernelError(KernelErrorCode::InvalidArgument, "Services", std::string(key), "scoped resolve requires non-global scope"));
                }
                if (!m_scopes.contains(activeScope.value))
                {
                    return NGIN::Utilities::Unexpected<KernelError>(
                        MakeKernelError(KernelErrorCode::NotFound, "Services", std::string(key), "resolve scope not found"));
                }
                auto cacheIt = it->second.scopedCache.find(activeScope.value);
                if (cacheIt == it->second.scopedCache.end())
                {
                    cacheIt = it->second.scopedCache.emplace(activeScope.value, created.ValueUnsafe()).first;
                }
                return std::optional<NGIN::Utilities::Any<>> {cacheIt->second};
            }
            case ServiceLifetime::Transient:
                return std::optional<NGIN::Utilities::Any<>> {created.ValueUnsafe()};
        }

        return std::optional<NGIN::Utilities::Any<>> {created.ValueUnsafe()};
    }

    auto ServiceRegistry::ResolveRequired(const std::string_view key, const ServiceScopeId resolveScope) noexcept -> CoreResult<NGIN::Utilities::Any<>>
    {
        auto optionalValue = ResolveOptional(key, resolveScope);
        if (!optionalValue)
        {
            return NGIN::Utilities::Unexpected<KernelError>(optionalValue.ErrorUnsafe());
        }

        if (!optionalValue.ValueUnsafe().has_value())
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::NotFound, "Services", {}, "service not found: " + std::string(key)));
        }

        return *optionalValue.ValueUnsafe();
    }

    auto ServiceRegistry::EnumerateKeys() const -> std::vector<std::string>
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::string> keys;
        keys.reserve(m_entries.size());
        for (const auto& [key, _] : m_entries)
        {
            keys.push_back(key);
        }
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    auto ServiceRegistry::GetScopeInfo(const ServiceScopeId scopeId) const noexcept -> CoreResult<ServiceScopeInfo>
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_scopes.find(scopeId.value);
        if (it == m_scopes.end())
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::NotFound, "Services", {}, "scope not found"));
        }
        return it->second;
    }

    auto CreateServiceRegistry() noexcept -> NGIN::Memory::Shared<IServiceRegistry>
    {
        return NGIN::Memory::MakeSharedAs<IServiceRegistry, ServiceRegistry>();
    }
}
