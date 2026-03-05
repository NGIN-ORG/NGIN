#include <NGIN/Runtime/Events.hpp>

#include <algorithm>

namespace NGIN::Runtime
{
    auto EventBus::Subscribe(
        std::string channel,
        EventCallback callback,
        EventScope scope,
        const NGIN::Int32 priority) noexcept -> RuntimeResult<EventSubscriptionToken>
    {
        if (channel.empty())
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Events", {}, "channel cannot be empty"));
        }
        if (!callback)
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Events", {}, "callback cannot be empty"));
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        const EventSubscriptionToken token {m_nextToken++};
        m_subscriptions.push_back(Subscription {
            .token = token,
            .channel = std::move(channel),
            .callback = std::move(callback),
            .scope = std::move(scope),
            .priority = priority,
        });
        return token;
    }

    auto EventBus::Unsubscribe(const EventSubscriptionToken token) noexcept -> RuntimeResult<void>
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto before = m_subscriptions.size();
        std::erase_if(m_subscriptions, [&](const Subscription& sub) { return sub.token.value == token.value; });
        if (before == m_subscriptions.size())
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::NotFound, "Events", {}, "subscription token not found"));
        }
        return RuntimeResult<void> {};
    }

    auto EventBus::PublishImmediate(EventRecord eventRecord) noexcept -> RuntimeResult<void>
    {
        std::vector<Subscription> callbacks;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            eventRecord.sequence = ++m_sequence;
            callbacks.reserve(m_subscriptions.size());
            for (const auto& sub : m_subscriptions)
            {
                if (sub.channel == eventRecord.channel)
                {
                    callbacks.push_back(sub);
                }
            }
        }

        std::sort(
            callbacks.begin(),
            callbacks.end(),
            [](const Subscription& lhs, const Subscription& rhs) {
                if (lhs.priority != rhs.priority)
                {
                    return lhs.priority > rhs.priority;
                }
                return lhs.token.value < rhs.token.value;
            });

        for (const auto& callback : callbacks)
        {
            try
            {
                callback.callback(eventRecord);
            }
            catch (...)
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(
                        KernelErrorCode::EventDispatchFailure,
                        "Events",
                        {},
                        "event callback threw for channel: " + eventRecord.channel));
            }
        }

        return RuntimeResult<void> {};
    }

    auto EventBus::EnqueueDeferred(EventRecord eventRecord) noexcept -> RuntimeResult<void>
    {
        return EnqueueDeferredTo(eventRecord.queue, std::move(eventRecord));
    }

    auto EventBus::EnqueueDeferredTo(const EventQueue queue, EventRecord eventRecord) noexcept -> RuntimeResult<void>
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        eventRecord.queue = queue;
        eventRecord.sequence = ++m_sequence;
        m_deferredByQueue[static_cast<NGIN::UInt8>(queue)].push_back(std::move(eventRecord));
        return RuntimeResult<void> {};
    }

    auto EventBus::FlushDeferred(const std::string_view channel) noexcept -> RuntimeResult<void>
    {
        for (const auto queue : {EventQueue::Main, EventQueue::IO, EventQueue::Worker, EventQueue::Background, EventQueue::Render})
        {
            auto flush = FlushDeferredFrom(queue, channel);
            if (!flush)
            {
                return NGIN::Utilities::Unexpected<KernelError>(flush.ErrorUnsafe());
            }
        }
        return RuntimeResult<void> {};
    }

    auto EventBus::FlushDeferredFrom(const EventQueue queue, const std::string_view channel) noexcept -> RuntimeResult<void>
    {
        while (true)
        {
            EventRecord next {};
            bool found = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto itQueue = m_deferredByQueue.find(static_cast<NGIN::UInt8>(queue));
                if (itQueue == m_deferredByQueue.end() || itQueue->second.empty())
                {
                    break;
                }

                auto& queueEvents = itQueue->second;
                if (channel.empty())
                {
                    next = std::move(queueEvents.front());
                    queueEvents.pop_front();
                    found = true;
                }
                else
                {
                    const auto it = std::find_if(
                        queueEvents.begin(),
                        queueEvents.end(),
                        [&](const EventRecord& rec) { return rec.channel == channel; });
                    if (it != queueEvents.end())
                    {
                        next = std::move(*it);
                        queueEvents.erase(it);
                        found = true;
                    }
                }
            }

            if (!found)
            {
                break;
            }

            auto dispatch = PublishImmediate(std::move(next));
            if (!dispatch)
            {
                return NGIN::Utilities::Unexpected<KernelError>(dispatch.ErrorUnsafe());
            }
        }

        return RuntimeResult<void> {};
    }

    void EventBus::ClearScope(const EventScope& scope) noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::erase_if(
            m_subscriptions,
            [&](const Subscription& sub) {
                return sub.scope.owner == scope.owner;
            });
    }

    auto CreateEventBus() noexcept -> NGIN::Memory::Shared<IEventBus>
    {
        return NGIN::Memory::MakeSharedAs<IEventBus, EventBus>();
    }
}
