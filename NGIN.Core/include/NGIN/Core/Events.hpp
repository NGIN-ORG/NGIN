#pragma once

/// @file Events.hpp
/// @brief Immediate and deferred event bus contracts.

#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Core/Errors.hpp>
#include <NGIN/Core/Export.hpp>
#include <NGIN/Utilities/Any.hpp>

#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace NGIN::Core
{
    /// @brief Reserved kernel event identifiers.
    enum class ReservedKernelEvent : NGIN::UInt8
    {
        None,
        KernelStarting,
        KernelRunning,
        KernelStopping,
        ModuleLoaded,
        ModuleStarted,
        ModuleFailed,
        ConfigChanged
    };

    /// @brief Deferred queue ownership policy.
    enum class EventQueue : NGIN::UInt8
    {
        Main,
        IO,
        Worker,
        Background,
        Render
    };

    [[nodiscard]] constexpr auto ToString(const ReservedKernelEvent value) noexcept -> std::string_view
    {
        switch (value)
        {
            case ReservedKernelEvent::None: return "None";
            case ReservedKernelEvent::KernelStarting: return "KernelStarting";
            case ReservedKernelEvent::KernelRunning: return "KernelRunning";
            case ReservedKernelEvent::KernelStopping: return "KernelStopping";
            case ReservedKernelEvent::ModuleLoaded: return "ModuleLoaded";
            case ReservedKernelEvent::ModuleStarted: return "ModuleStarted";
            case ReservedKernelEvent::ModuleFailed: return "ModuleFailed";
            case ReservedKernelEvent::ConfigChanged: return "ConfigChanged";
        }
        return "Unknown";
    }

    [[nodiscard]] constexpr auto ToString(const EventQueue value) noexcept -> std::string_view
    {
        switch (value)
        {
            case EventQueue::Main: return "Main";
            case EventQueue::IO: return "IO";
            case EventQueue::Worker: return "Worker";
            case EventQueue::Background: return "Background";
            case EventQueue::Render: return "Render";
        }
        return "Unknown";
    }

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
        EventQueue             queue {EventQueue::Main};
        ReservedKernelEvent    reserved {ReservedKernelEvent::None};
    };

    /// @brief Event subscription token.
    struct EventSubscriptionToken
    {
        NGIN::UInt64 value {0};
    };

    using EventCallback = std::function<void(const EventRecord&)>;

    /// @brief Event bus public interface.
    class NGIN_CORE_API IEventBus
    {
    public:
        virtual ~IEventBus() = default;

        virtual auto Subscribe(
            std::string channel,
            EventCallback callback,
            EventScope scope,
            NGIN::Int32 priority = 0) noexcept -> CoreResult<EventSubscriptionToken> = 0;

        virtual auto Unsubscribe(EventSubscriptionToken token) noexcept -> CoreResult<void> = 0;

        virtual auto PublishImmediate(EventRecord eventRecord) noexcept -> CoreResult<void> = 0;

        virtual auto EnqueueDeferred(EventRecord eventRecord) noexcept -> CoreResult<void> = 0;

        virtual auto EnqueueDeferredTo(EventQueue queue, EventRecord eventRecord) noexcept -> CoreResult<void> = 0;

        virtual auto FlushDeferred(std::string_view channel = {}) noexcept -> CoreResult<void> = 0;

        virtual auto FlushDeferredFrom(EventQueue queue, std::string_view channel = {}) noexcept -> CoreResult<void> = 0;

        virtual void ClearScope(const EventScope& scope) noexcept = 0;
    };

    /// @brief Default in-process event bus implementation.
    class NGIN_CORE_API EventBus final : public IEventBus
    {
    public:
        EventBus() = default;

        auto Subscribe(
            std::string channel,
            EventCallback callback,
            EventScope scope,
            NGIN::Int32 priority = 0) noexcept -> CoreResult<EventSubscriptionToken> override;

        auto Unsubscribe(EventSubscriptionToken token) noexcept -> CoreResult<void> override;

        auto PublishImmediate(EventRecord eventRecord) noexcept -> CoreResult<void> override;

        auto EnqueueDeferred(EventRecord eventRecord) noexcept -> CoreResult<void> override;

        auto EnqueueDeferredTo(EventQueue queue, EventRecord eventRecord) noexcept -> CoreResult<void> override;

        auto FlushDeferred(std::string_view channel = {}) noexcept -> CoreResult<void> override;

        auto FlushDeferredFrom(EventQueue queue, std::string_view channel = {}) noexcept -> CoreResult<void> override;

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

        mutable std::mutex                                m_mutex;
        std::vector<Subscription>                         m_subscriptions;
        std::unordered_map<NGIN::UInt8, std::deque<EventRecord>> m_deferredByQueue;
        NGIN::UInt64                                      m_nextToken {1};
        NGIN::UInt64                                      m_sequence {0};
    };

    /// @brief Create a default event bus instance.
    NGIN_CORE_API auto CreateEventBus() noexcept -> NGIN::Memory::Shared<IEventBus>;
}
