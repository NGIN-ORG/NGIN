#include <NGIN/Runtime/Services.hpp>

#include <algorithm>

namespace NGIN::Runtime
{
    auto ServiceRegistry::RegisterInstance(
        std::string key,
        NGIN::Utilities::Any<> service,
        ServiceScope scope,
        ServiceMetadata metadata) noexcept -> RuntimeResult<void>
    {
        if (key.empty())
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Services", {}, "service key cannot be empty"));
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_entries.contains(key))
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::AlreadyExists, "Services", {}, "service key already registered: " + key));
        }

        Entry entry {};
        entry.instance.emplace(std::move(service));
        entry.scope = std::move(scope);
        entry.metadata = std::move(metadata);
        m_entries.emplace(std::move(key), std::move(entry));
        return RuntimeResult<void> {};
    }

    auto ServiceRegistry::RegisterFactory(
        std::string key,
        ServiceFactory factory,
        ServiceScope scope,
        ServiceMetadata metadata) noexcept -> RuntimeResult<void>
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
        if (m_entries.contains(key))
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::AlreadyExists, "Services", {}, "service key already registered: " + key));
        }

        Entry entry {};
        entry.factory.emplace(std::move(factory));
        entry.scope = std::move(scope);
        entry.metadata = std::move(metadata);
        m_entries.emplace(std::move(key), std::move(entry));
        return RuntimeResult<void> {};
    }

    auto ServiceRegistry::ResolveOptional(const std::string_view key) noexcept
        -> RuntimeResult<std::optional<NGIN::Utilities::Any<>>>
    {
        std::optional<NGIN::Utilities::Any<>> instance {};
        std::optional<ServiceFactory> factory {};

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto it = m_entries.find(std::string(key));
            if (it == m_entries.end())
            {
                return instance;
            }
            if (it->second.instance.has_value())
            {
                return it->second.instance;
            }
            if (it->second.factory.has_value())
            {
                factory = it->second.factory;
            }
        }

        if (!factory.has_value())
        {
            return instance;
        }

        auto created = (*factory)();
        if (!created)
        {
            return NGIN::Utilities::Unexpected<KernelError>(created.ErrorUnsafe());
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_entries.find(std::string(key));
            if (it != m_entries.end() && !it->second.instance.has_value())
            {
                it->second.instance.emplace(created.ValueUnsafe());
            }
            if (it != m_entries.end() && it->second.instance.has_value())
            {
                return it->second.instance;
            }
        }

        instance.emplace(created.ValueUnsafe());
        return instance;
    }

    auto ServiceRegistry::ResolveRequired(const std::string_view key) noexcept -> RuntimeResult<NGIN::Utilities::Any<>>
    {
        auto optionalValue = ResolveOptional(key);
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

    void ServiceRegistry::ClearScope(const ServiceScope& scope) noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::erase_if(
            m_entries,
            [&](const auto& pair) {
                return pair.second.scope.lifetime == scope.lifetime && pair.second.scope.owner == scope.owner;
            });
    }

    auto CreateServiceRegistry() noexcept -> NGIN::Memory::Shared<IServiceRegistry>
    {
        return NGIN::Memory::MakeSharedAs<IServiceRegistry, ServiceRegistry>();
    }
}
