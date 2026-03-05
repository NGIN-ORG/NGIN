#pragma once

/// @file Config.hpp
/// @brief Layered configuration store contract and default implementation.

#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/Core/Errors.hpp>
#include <NGIN/Core/Export.hpp>

#include <charconv>
#include <functional>
#include <mutex>
#include <optional>
#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace NGIN::Core
{
    /// @brief Configuration source layer.
    enum class ConfigLayer : NGIN::UInt8
    {
        BuiltInDefaults,
        HostTarget,
        PluginModule,
        Environment,
        LocalOverride,
        CommandLine,
        RuntimeMutable
    };

    /// @brief Config change payload.
    struct ConfigChangeEvent
    {
        std::string key {};
        std::string oldValue {};
        std::string newValue {};
        ConfigLayer sourceLayer {ConfigLayer::BuiltInDefaults};
    };

    /// @brief Snapshot of effective config key-values with provenance.
    struct ConfigSnapshot
    {
        std::unordered_map<std::string, std::string> values {};
        std::unordered_map<std::string, ConfigLayer> provenance {};
    };

    /// @brief Subscription token for config change stream.
    struct ConfigSubscriptionToken
    {
        NGIN::UInt64 value {0};
    };

    using ConfigChangeCallback = std::function<void(const ConfigChangeEvent&)>;

    /// @brief Configuration store interface.
    class NGIN_CORE_API IConfigStore
    {
    public:
        virtual ~IConfigStore() = default;

        virtual auto SetValue(ConfigLayer layer, std::string key, std::string value) noexcept -> CoreResult<void> = 0;
        virtual auto GetRaw(std::string_view key) const noexcept -> CoreResult<std::string> = 0;
        virtual auto GetProvenance(std::string_view key) const noexcept -> CoreResult<ConfigLayer> = 0;
        [[nodiscard]] virtual auto Snapshot() const -> ConfigSnapshot = 0;
        [[nodiscard]] virtual auto Enumerate(std::string_view prefix) const -> std::vector<std::pair<std::string, std::string>> = 0;
        virtual auto Subscribe(ConfigChangeCallback callback) noexcept -> CoreResult<ConfigSubscriptionToken> = 0;
        virtual auto Unsubscribe(ConfigSubscriptionToken token) noexcept -> CoreResult<void> = 0;
    };

    /// @brief Default layered config-store implementation.
    class NGIN_CORE_API ConfigStore final : public IConfigStore
    {
    public:
        ConfigStore() = default;

        auto SetValue(ConfigLayer layer, std::string key, std::string value) noexcept -> CoreResult<void> override;

        auto GetRaw(std::string_view key) const noexcept -> CoreResult<std::string> override;

        auto GetProvenance(std::string_view key) const noexcept -> CoreResult<ConfigLayer> override;

        [[nodiscard]] auto Snapshot() const -> ConfigSnapshot override;

        [[nodiscard]] auto Enumerate(std::string_view prefix) const -> std::vector<std::pair<std::string, std::string>> override;

        auto Subscribe(ConfigChangeCallback callback) noexcept -> CoreResult<ConfigSubscriptionToken> override;

        auto Unsubscribe(ConfigSubscriptionToken token) noexcept -> CoreResult<void> override;

    private:
        struct EffectiveEntry
        {
            std::string value {};
            ConfigLayer sourceLayer {ConfigLayer::BuiltInDefaults};
        };

        struct Subscription
        {
            ConfigSubscriptionToken token {};
            ConfigChangeCallback    callback {};
        };

        [[nodiscard]] static constexpr auto LayerPriority(const ConfigLayer layer) noexcept -> NGIN::UInt8
        {
            switch (layer)
            {
                case ConfigLayer::BuiltInDefaults: return 0;
                case ConfigLayer::HostTarget: return 1;
                case ConfigLayer::PluginModule: return 2;
                case ConfigLayer::Environment: return 3;
                case ConfigLayer::LocalOverride: return 4;
                case ConfigLayer::CommandLine: return 5;
                case ConfigLayer::RuntimeMutable: return 6;
            }
            return 0;
        }

        mutable std::mutex                                      m_mutex;
        std::unordered_map<std::string, EffectiveEntry>         m_effective;
        std::unordered_map<std::string, std::array<std::optional<std::string>, 7>> m_layers;
        std::vector<Subscription>                               m_subscriptions;
        NGIN::UInt64                                            m_nextToken {1};
    };

    /// @brief Create default config store.
    NGIN_CORE_API auto CreateConfigStore() noexcept -> NGIN::Memory::Shared<IConfigStore>;

    template<typename T>
    [[nodiscard]] auto ConvertConfigValue(std::string_view text) -> std::optional<T>
    {
        T value {};
        const auto* begin = text.data();
        const auto* end = begin + text.size();
        const auto [ptr, ec] = std::from_chars(begin, end, value);
        if (ec != std::errc{} || ptr != end)
        {
            return std::nullopt;
        }
        return value;
    }

    template<>
    [[nodiscard]] inline auto ConvertConfigValue<bool>(std::string_view text) -> std::optional<bool>
    {
        if (text == "1" || text == "true" || text == "TRUE" || text == "on" || text == "ON")
        {
            return true;
        }
        if (text == "0" || text == "false" || text == "FALSE" || text == "off" || text == "OFF")
        {
            return false;
        }
        return std::nullopt;
    }
}
