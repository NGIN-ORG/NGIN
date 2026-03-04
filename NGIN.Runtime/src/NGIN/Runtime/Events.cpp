#include <NGIN/Runtime/Events.hpp>

#include <algorithm>
#include <mutex>

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
        std::lock_guard<std::mutex> lock(m_mutex);
        eventRecord.sequence = ++m_sequence;
        m_deferred.push_back(std::move(eventRecord));
        return RuntimeResult<void> {};
    }

    auto EventBus::FlushDeferred(const std::string_view channel) noexcept -> RuntimeResult<void>
    {
        while (true)
        {
            EventRecord next {};
            bool found = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_deferred.empty())
                {
                    break;
                }

                if (channel.empty())
                {
                    next = std::move(m_deferred.front());
                    m_deferred.pop_front();
                    found = true;
                }
                else
                {
                    const auto it = std::find_if(
                        m_deferred.begin(),
                        m_deferred.end(),
                        [&](const EventRecord& rec) { return rec.channel == channel; });
                    if (it != m_deferred.end())
                    {
                        next = std::move(*it);
                        m_deferred.erase(it);
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
