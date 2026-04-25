#include <NGIN/Core/Services.hpp>

#include <algorithm>

namespace NGIN::Core
{
    ServiceRegistry::ServiceRegistry()
    {
        m_scopes.emplace(ServiceScopeId::Global().value, ServiceScopeInfo {
            .id = ServiceScopeId::Global(),
            .kind = ServiceScopeKind::Host,
            .owner = "Host"});
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
            entryIt->second->RemoveScope(scopeId);
            if (entryIt->second->Options().ownerScope == scopeId)
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

    auto ServiceRegistry::RegisterProvider(std::shared_ptr<detail::ServiceProviderBase> provider) noexcept -> CoreResult<void>
    {
        if (!provider)
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Services", {}, "service provider cannot be null"));
        }

        if (provider->Key().typeId == 0 || provider->Key().typeName.empty())
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Services", provider->Key().ContractName(), "service key cannot be empty"));
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        if (auto valid = ValidateOptions(provider->Options()); !valid)
        {
            return NGIN::Utilities::Unexpected<KernelError>(valid.Error());
        }

        if (m_entries.contains(provider->Key()))
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::AlreadyExists, "Services", provider->Key().ContractName(), "service key already registered: " + provider->Key().ContractName()));
        }

        if (!provider->Key().name.empty())
        {
            const auto contractName = provider->Key().ContractName();
            const auto contractIt = std::find_if(
                m_entries.begin(),
                m_entries.end(),
                [&contractName](const auto& entry)
                {
                    return entry.second->MatchesContract(contractName);
                });
            if (contractIt != m_entries.end())
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(KernelErrorCode::AlreadyExists, "Services", contractName, "service contract already registered: " + contractName));
            }
        }

        m_entries.emplace(provider->Key(), std::move(provider));
        return CoreResult<void> {};
    }

    auto ServiceRegistry::FindProvider(const ServiceKey& key) noexcept -> std::shared_ptr<detail::ServiceProviderBase>
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_entries.find(key);
        if (it == m_entries.end())
        {
            return {};
        }
        return it->second;
    }

    auto ServiceRegistry::EffectiveResolveScope(
        const detail::ServiceProviderBase& provider,
        const ServiceScopeId requestedScope) const noexcept -> ServiceScopeId
    {
        return requestedScope.IsGlobal() ? provider.Options().ownerScope : requestedScope;
    }

    auto ServiceRegistry::ValidateResolve(
        const detail::ServiceProviderBase& provider,
        const ServiceScopeId resolveScope) const noexcept -> CoreResult<void>
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto& options = provider.Options();
        if (options.lifetime == ServiceLifetime::Singleton)
        {
            return CoreResult<void> {};
        }

        const ServiceScopeId activeScope = resolveScope.IsGlobal() ? options.ownerScope : resolveScope;
        if (activeScope.IsGlobal())
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Services", provider.Key().ContractName(), "scoped resolve requires non-global scope"));
        }
        if (!m_scopes.contains(activeScope.value))
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::NotFound, "Services", provider.Key().ContractName(), "resolve scope not found"));
        }

        return CoreResult<void> {};
    }

    auto ServiceRegistry::HasServiceContract(const std::string_view contractName) const noexcept -> bool
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return std::any_of(
            m_entries.begin(),
            m_entries.end(),
            [contractName](const auto& entry)
            {
                return entry.second->MatchesContract(contractName);
            });
    }

    auto ServiceRegistry::EnumerateKeys() const -> std::vector<std::string>
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::string> keys;
        keys.reserve(m_entries.size());
        for (const auto& [_, entry] : m_entries)
        {
            keys.push_back(entry->Key().ContractName());
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
