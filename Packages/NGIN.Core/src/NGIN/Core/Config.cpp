#include <NGIN/Core/Config.hpp>

#include <algorithm>

namespace NGIN::Core
{
    auto ConfigStore::SetValue(const ConfigLayer layer, std::string key, std::string value) noexcept -> CoreResult<void>
    {
        if (key.empty())
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Config", {}, "key cannot be empty"));
        }

        std::vector<ConfigChangeCallback> callbacks;
        ConfigChangeEvent change {};

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            auto& slot = m_layers[key];
            slot[static_cast<std::size_t>(layer)] = value;

            auto oldIt = m_effective.find(key);
            const std::string oldValue = (oldIt != m_effective.end()) ? oldIt->second.value : std::string {};

            std::optional<EffectiveEntry> best {};
            for (std::size_t i = 0; i < slot.size(); ++i)
            {
                if (!slot[i].has_value())
                {
                    continue;
                }

                const ConfigLayer candidateLayer = static_cast<ConfigLayer>(i);
                if (!best.has_value() || LayerPriority(candidateLayer) >= LayerPriority(best->sourceLayer))
                {
                    best = EffectiveEntry {
                        .value = *slot[i],
                        .sourceLayer = candidateLayer,
                    };
                }
            }

            if (!best.has_value())
            {
                return CoreResult<void> {};
            }

            const bool changed = (oldIt == m_effective.end()) || oldIt->second.value != best->value || oldIt->second.sourceLayer != best->sourceLayer;
            m_effective[key] = *best;
            if (!changed)
            {
                return CoreResult<void> {};
            }

            change.key = key;
            change.oldValue = oldValue;
            change.newValue = best->value;
            change.sourceLayer = best->sourceLayer;

            callbacks.reserve(m_subscriptions.size());
            for (const auto& sub : m_subscriptions)
            {
                callbacks.push_back(sub.callback);
            }
        }

        for (auto& callback : callbacks)
        {
            if (callback)
            {
                callback(change);
            }
        }

        return CoreResult<void> {};
    }

    auto ConfigStore::GetRaw(const std::string_view key) const noexcept -> CoreResult<std::string>
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_effective.find(std::string(key));
        if (it == m_effective.end())
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::NotFound, "Config", {}, "key not found: " + std::string(key)));
        }

        return it->second.value;
    }

    auto ConfigStore::GetProvenance(const std::string_view key) const noexcept -> CoreResult<ConfigLayer>
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_effective.find(std::string(key));
        if (it == m_effective.end())
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::NotFound, "Config", {}, "key not found: " + std::string(key)));
        }

        return it->second.sourceLayer;
    }

    auto ConfigStore::Snapshot() const -> ConfigSnapshot
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ConfigSnapshot snapshot {};
        snapshot.values.reserve(m_effective.size());
        snapshot.provenance.reserve(m_effective.size());

        for (const auto& [key, value] : m_effective)
        {
            snapshot.values.emplace(key, value.value);
            snapshot.provenance.emplace(key, value.sourceLayer);
        }

        return snapshot;
    }

    auto ConfigStore::Enumerate(const std::string_view prefix) const -> std::vector<std::pair<std::string, std::string>>
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::pair<std::string, std::string>> entries;
        for (const auto& [key, value] : m_effective)
        {
            if (prefix.empty() || key.rfind(prefix, 0) == 0)
            {
                entries.emplace_back(key, value.value);
            }
        }

        std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
        return entries;
    }

    auto ConfigStore::Subscribe(const ConfigChangeCallback callback) noexcept -> CoreResult<ConfigSubscriptionToken>
    {
        if (!callback)
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Config", {}, "subscription callback cannot be empty"));
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        const ConfigSubscriptionToken token {m_nextToken++};
        m_subscriptions.push_back(Subscription {
            .token = token,
            .callback = callback,
        });
        return token;
    }

    auto ConfigStore::Unsubscribe(const ConfigSubscriptionToken token) noexcept -> CoreResult<void>
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto before = m_subscriptions.size();
        std::erase_if(m_subscriptions, [&](const Subscription& sub) { return sub.token.value == token.value; });
        if (before == m_subscriptions.size())
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::NotFound, "Config", {}, "subscription token not found"));
        }

        return CoreResult<void> {};
    }

    auto CreateConfigStore() noexcept -> NGIN::Memory::Shared<IConfigStore>
    {
        return NGIN::Memory::MakeSharedAs<IConfigStore, ConfigStore>();
    }
}

