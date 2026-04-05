#include <NGIN/Core/Events.hpp>

#include <algorithm>
#include <exception>

namespace NGIN::Core {
auto EventBus::ChannelIdFor(const std::string_view channel) noexcept
    -> NGIN::UInt64 {
  return NGIN::Hashing::FNV1a64(channel);
}

auto EventBus::ValidateChannel(const std::string_view channel,
                               const std::string_view operation) noexcept
    -> CoreResult<void> {
  if (channel.empty()) {
    return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
        KernelErrorCode::InvalidArgument, "Events", {},
        std::string(operation) + " requires a non-empty channel"));
  }

  return CoreResult<void>{};
}

void EventBus::SortBucket(SubscriptionBucket &bucket) noexcept {
  std::sort(bucket.begin(), bucket.end(),
            [](const Subscription &lhs, const Subscription &rhs) {
              if (lhs.priority != rhs.priority) {
                return lhs.priority > rhs.priority;
              }
              return lhs.token.value < rhs.token.value;
            });
}

auto EventBus::SubscribeRaw(std::string channel, RawEventCallback callback,
                            EventScope scope,
                            const NGIN::Int32 priority) noexcept
    -> CoreResult<EventSubscriptionToken> {
  if (auto validation = ValidateChannel(channel, "SubscribeRaw"); !validation) {
    return NGIN::Utilities::Unexpected<KernelError>(validation.Error());
  }
  if (!callback) {
    return NGIN::Utilities::Unexpected<KernelError>(
        MakeKernelError(KernelErrorCode::InvalidArgument, "Events", {},
                        "callback cannot be empty"));
  }

  try {
    const auto channelId = ChannelIdFor(channel);

    std::lock_guard<std::mutex> lock(m_mutex);
    const EventSubscriptionToken token{m_nextToken++};
    auto &bucket = m_subscriptionsByChannel[channelId];
    bucket.push_back(Subscription{
        .token = token,
        .channel = std::move(channel),
        .channelId = channelId,
        .callback = std::move(callback),
        .scope = std::move(scope),
        .priority = priority,
    });
    SortBucket(bucket);
    m_subscriptionChannelByToken.emplace(token.value, channelId);
    return token;
  } catch (const std::exception &exception) {
    return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
        KernelErrorCode::InternalError, "Events", {},
        "failed to create subscription: " + std::string(exception.what())));
  } catch (...) {
    return NGIN::Utilities::Unexpected<KernelError>(
        MakeKernelError(KernelErrorCode::InternalError, "Events", {},
                        "failed to create subscription"));
  }
}

auto EventBus::Unsubscribe(const EventSubscriptionToken token) noexcept
    -> CoreResult<void> {
  try {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto tokenIt = m_subscriptionChannelByToken.find(token.value);
    if (tokenIt == m_subscriptionChannelByToken.end()) {
      return NGIN::Utilities::Unexpected<KernelError>(
          MakeKernelError(KernelErrorCode::NotFound, "Events", {},
                          "subscription token not found"));
    }

    const auto bucketIt = m_subscriptionsByChannel.find(tokenIt->second);
    if (bucketIt == m_subscriptionsByChannel.end()) {
      m_subscriptionChannelByToken.erase(tokenIt);
      return NGIN::Utilities::Unexpected<KernelError>(
          MakeKernelError(KernelErrorCode::NotFound, "Events", {},
                          "subscription bucket not found"));
    }

    auto &bucket = bucketIt->second;
    const auto before = bucket.size();
    std::erase_if(bucket, [&](const Subscription &sub) {
      return sub.token.value == token.value;
    });
    if (before == bucket.size()) {
      m_subscriptionChannelByToken.erase(tokenIt);
      return NGIN::Utilities::Unexpected<KernelError>(
          MakeKernelError(KernelErrorCode::NotFound, "Events", {},
                          "subscription token not found"));
    }

    if (bucket.empty()) {
      m_subscriptionsByChannel.erase(bucketIt);
    }
    m_subscriptionChannelByToken.erase(tokenIt);
    return CoreResult<void>{};
  } catch (const std::exception &exception) {
    return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
        KernelErrorCode::InternalError, "Events", {},
        "failed to unsubscribe: " + std::string(exception.what())));
  } catch (...) {
    return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
        KernelErrorCode::InternalError, "Events", {}, "failed to unsubscribe"));
  }
}

auto EventBus::PublishRawImmediate(RawEventRecord eventRecord) noexcept
    -> CoreResult<void> {
  if (auto validation =
          ValidateChannel(eventRecord.channel, "PublishRawImmediate");
      !validation) {
    return NGIN::Utilities::Unexpected<KernelError>(validation.Error());
  }

  std::vector<Subscription> callbacks;

  try {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      eventRecord.channelId = ChannelIdFor(eventRecord.channel);
      if (eventRecord.sequence == 0) {
        eventRecord.sequence = ++m_sequence;
      }

      const auto bucketIt =
          m_subscriptionsByChannel.find(eventRecord.channelId);
      if (bucketIt != m_subscriptionsByChannel.end()) {
        callbacks.reserve(bucketIt->second.size());
        for (const auto &sub : bucketIt->second) {
          // Keep the channel string check to avoid cross-talk if two distinct
          // channels ever collide to the same hash.
          if (sub.channel == eventRecord.channel) {
            callbacks.push_back(sub);
          }
        }
      }
    }

    for (const auto &callback : callbacks) {
      callback.callback(eventRecord);
    }

    return CoreResult<void>{};
  } catch (const std::exception &exception) {
    return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
        KernelErrorCode::EventDispatchFailure, "Events", eventRecord.channel,
        "event callback threw: " + std::string(exception.what())));
  } catch (...) {
    return NGIN::Utilities::Unexpected<KernelError>(
        MakeKernelError(KernelErrorCode::EventDispatchFailure, "Events",
                        eventRecord.channel, "event callback threw"));
  }
}

auto EventBus::EnqueueRaw(RawEventRecord eventRecord) noexcept
    -> CoreResult<void> {
  return EnqueueRawTo(eventRecord.queue, std::move(eventRecord));
}

auto EventBus::EnqueueRawTo(const EventQueue queue,
                            RawEventRecord eventRecord) noexcept
    -> CoreResult<void> {
  if (auto validation = ValidateChannel(eventRecord.channel, "EnqueueRawTo");
      !validation) {
    return NGIN::Utilities::Unexpected<KernelError>(validation.Error());
  }

  try {
    std::lock_guard<std::mutex> lock(m_mutex);
    eventRecord.channelId = ChannelIdFor(eventRecord.channel);
    eventRecord.queue = queue;
    if (eventRecord.sequence == 0) {
      eventRecord.sequence = ++m_sequence;
    }
    m_deferredByQueue[static_cast<NGIN::UInt8>(queue)].push_back(
        std::move(eventRecord));
    return CoreResult<void>{};
  } catch (const std::exception &exception) {
    return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
        KernelErrorCode::InternalError, "Events", {},
        "failed to enqueue event: " + std::string(exception.what())));
  } catch (...) {
    return NGIN::Utilities::Unexpected<KernelError>(
        MakeKernelError(KernelErrorCode::InternalError, "Events", {},
                        "failed to enqueue event"));
  }
}

auto EventBus::FlushRaw(const std::string_view channel) noexcept
    -> CoreResult<void> {
  for (const auto queue : {EventQueue::Main, EventQueue::IO, EventQueue::Worker,
                           EventQueue::Background, EventQueue::Render}) {
    auto flush = FlushRawFrom(queue, channel);
    if (!flush) {
      return NGIN::Utilities::Unexpected<KernelError>(flush.Error());
    }
  }
  return CoreResult<void>{};
}

auto EventBus::FlushRawFrom(const EventQueue queue,
                            const std::string_view channel) noexcept
    -> CoreResult<void> {
  if (!channel.empty()) {
    if (auto validation = ValidateChannel(channel, "FlushRawFrom");
        !validation) {
      return NGIN::Utilities::Unexpected<KernelError>(validation.Error());
    }
  }

  while (true) {
    RawEventRecord next{};
    bool found = false;

    try {
      std::lock_guard<std::mutex> lock(m_mutex);
      auto itQueue = m_deferredByQueue.find(static_cast<NGIN::UInt8>(queue));
      if (itQueue == m_deferredByQueue.end() || itQueue->second.empty()) {
        break;
      }

      auto &queueEvents = itQueue->second;
      if (channel.empty()) {
        next = std::move(queueEvents.front());
        queueEvents.pop_front();
        found = true;
      } else {
        const auto it = std::find_if(
            queueEvents.begin(), queueEvents.end(),
            [&](const RawEventRecord &rec) { return rec.channel == channel; });
        if (it != queueEvents.end()) {
          next = std::move(*it);
          queueEvents.erase(it);
          found = true;
        }
      }
    } catch (const std::exception &exception) {
      return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
          KernelErrorCode::InternalError, "Events", std::string(channel),
          "failed to flush deferred events: " + std::string(exception.what())));
    } catch (...) {
      return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
          KernelErrorCode::InternalError, "Events", std::string(channel),
          "failed to flush deferred events"));
    }

    if (!found) {
      break;
    }

    auto dispatch = PublishRawImmediate(std::move(next));
    if (!dispatch) {
      return NGIN::Utilities::Unexpected<KernelError>(dispatch.Error());
    }
  }

  return CoreResult<void>{};
}

void EventBus::ClearScope(const EventScope &scope) noexcept {
  try {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto bucketIt = m_subscriptionsByChannel.begin();
         bucketIt != m_subscriptionsByChannel.end();) {
      auto &bucket = bucketIt->second;
      for (const auto &sub : bucket) {
        if (sub.scope.owner == scope.owner) {
          m_subscriptionChannelByToken.erase(sub.token.value);
        }
      }

      std::erase_if(bucket, [&](const Subscription &sub) {
        return sub.scope.owner == scope.owner;
      });

      if (bucket.empty()) {
        bucketIt = m_subscriptionsByChannel.erase(bucketIt);
      } else {
        ++bucketIt;
      }
    }
  } catch (...) {
  }
}

auto CreateEventBus() noexcept -> NGIN::Memory::Shared<IEventBus> {
  return NGIN::Memory::MakeSharedAs<IEventBus, EventBus>();
}
} // namespace NGIN::Core
