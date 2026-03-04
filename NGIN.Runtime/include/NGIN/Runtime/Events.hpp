#pragma once

/// @file Events.hpp
/// @brief Immediate and deferred event bus contracts.

#include <NGIN/Primitives.hpp>
#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/Runtime/Errors.hpp>
#include <NGIN/Runtime/Export.hpp>
#include <NGIN/Utilities/Any.hpp>

#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace NGIN::Runtime
{
    /// @brief Event subscription scope owner.
    struct EventScope
    {
        std::string owner {};
    };

    /// @brief Event record delivered to subscribers.
    struct EventRecord
    {
        std::string            channel {};
        NGIN::Utilities::Any<> payload {};
        NGIN::UInt64           sequence {0};
    };

    /// @brief Event subscription token.
    struct EventSubscriptionToken
    {
        NGIN::UInt64 value {0};
    };

    using EventCallback = std::function<void(const EventRecord&)>;

    /// @brief Event bus public interface.
    class NGIN_RUNTIME_API IEventBus
    {
    public:
        virtual ~IEventBus() = default;

        virtual auto Subscribe(
            std::string channel,
            EventCallback callback,
            EventScope scope,
            NGIN::Int32 priority = 0) noexcept -> RuntimeResult<EventSubscriptionToken> = 0;

        virtual auto Unsubscribe(EventSubscriptionToken token) noexcept -> RuntimeResult<void> = 0;

        virtual auto PublishImmediate(EventRecord eventRecord) noexcept -> RuntimeResult<void> = 0;

        virtual auto EnqueueDeferred(EventRecord eventRecord) noexcept -> RuntimeResult<void> = 0;

        virtual auto FlushDeferred(std::string_view channel = {}) noexcept -> RuntimeResult<void> = 0;

        virtual void ClearScope(const EventScope& scope) noexcept = 0;
    };

    /// @brief Default in-process event bus implementation.
    class NGIN_RUNTIME_API EventBus final : public IEventBus
    {
    public:
        EventBus() = default;

        auto Subscribe(
            std::string channel,
            EventCallback callback,
            EventScope scope,
            NGIN::Int32 priority = 0) noexcept -> RuntimeResult<EventSubscriptionToken> override;

        auto Unsubscribe(EventSubscriptionToken token) noexcept -> RuntimeResult<void> override;

        auto PublishImmediate(EventRecord eventRecord) noexcept -> RuntimeResult<void> override;

        auto EnqueueDeferred(EventRecord eventRecord) noexcept -> RuntimeResult<void> override;

        auto FlushDeferred(std::string_view channel = {}) noexcept -> RuntimeResult<void> override;

        void ClearScope(const EventScope& scope) noexcept override;

    private:
        struct Subscription
        {
            EventSubscriptionToken token {};
            std::string            channel {};
            EventCallback          callback {};
            EventScope             scope {};
            NGIN::Int32            priority {0};
        };

        mutable std::mutex        m_mutex;
        std::vector<Subscription> m_subscriptions;
        std::deque<EventRecord>   m_deferred;
        NGIN::UInt64              m_nextToken {1};
        NGIN::UInt64              m_sequence {0};
    };

    /// @brief Create a default event bus instance.
    NGIN_RUNTIME_API auto CreateEventBus() noexcept -> NGIN::Memory::Shared<IEventBus>;
}
