#pragma once

/// @file Events.hpp
/// @brief Typed-first event bus contracts with raw escape hatches.

#include <NGIN/Core/Errors.hpp>
#include <NGIN/Core/Export.hpp>
#include <NGIN/Hashing/FNV.hpp>
#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/Meta/TypeId.hpp>
#include <NGIN/Meta/TypeName.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Utilities/Any.hpp>

#include <deque>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace NGIN::Core {
struct ConfigChangeEvent;

/// @brief Reserved kernel event identifiers.
enum class ReservedKernelEvent : NGIN::UInt8 {
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
enum class EventQueue : NGIN::UInt8 { Main, IO, Worker, Background, Render };

[[nodiscard]] constexpr auto ToString(const ReservedKernelEvent value) noexcept
    -> std::string_view {
  switch (value) {
  case ReservedKernelEvent::None:
    return "None";
  case ReservedKernelEvent::KernelStarting:
    return "KernelStarting";
  case ReservedKernelEvent::KernelRunning:
    return "KernelRunning";
  case ReservedKernelEvent::KernelStopping:
    return "KernelStopping";
  case ReservedKernelEvent::ModuleLoaded:
    return "ModuleLoaded";
  case ReservedKernelEvent::ModuleStarted:
    return "ModuleStarted";
  case ReservedKernelEvent::ModuleFailed:
    return "ModuleFailed";
  case ReservedKernelEvent::ConfigChanged:
    return "ConfigChanged";
  }
  return "Unknown";
}

[[nodiscard]] constexpr auto ToString(const EventQueue value) noexcept
    -> std::string_view {
  switch (value) {
  case EventQueue::Main:
    return "Main";
  case EventQueue::IO:
    return "IO";
  case EventQueue::Worker:
    return "Worker";
  case EventQueue::Background:
    return "Background";
  case EventQueue::Render:
    return "Render";
  }
  return "Unknown";
}

/// @brief Event subscription scope owner.
struct EventScope {
  std::string owner{};
};

/// @brief Delivery metadata attached to each dispatched event.
struct EventMetadata {
  std::string channel{};
  NGIN::UInt64 channelId{0};
  NGIN::UInt64 sequence{0};
  EventQueue queue{EventQueue::Main};
  ReservedKernelEvent reserved{ReservedKernelEvent::None};
};

/// @brief Low-level event record used by raw event bus operations.
struct RawEventRecord {
  std::string channel{};
  NGIN::UInt64 channelId{0};
  NGIN::Utilities::Any<> payload{};
  NGIN::UInt64 sequence{0};
  EventQueue queue{EventQueue::Main};
  ReservedKernelEvent reserved{ReservedKernelEvent::None};
};

/// @brief Event subscription token.
struct EventSubscriptionToken {
  NGIN::UInt64 value{0};
};

/// @brief Typed payload for kernel start notifications.
struct KernelStartingEvent {
  std::string hostName{};
};

/// @brief Typed payload for kernel running notifications.
struct KernelRunningEvent {
  std::string hostName{};
};

/// @brief Typed payload for kernel stop notifications.
struct KernelStoppingEvent {
  std::string hostName{};
};

/// @brief Typed payload for module loaded notifications.
struct ModuleLoadedEvent {
  std::string moduleName{};
};

/// @brief Typed payload for module started notifications.
struct ModuleStartedEvent {
  std::string moduleName{};
};

/// @brief Typed payload for module failure notifications.
struct ModuleFailedEvent {
  std::string moduleName{};
};

using RawEventCallback = std::function<void(const RawEventRecord &)>;

template <typename TEvent> struct TypedEventRecord {
  const TEvent &event;
  EventMetadata metadata{};
};

template <typename TEvent>
using TypedEventCallback =
    std::function<void(const TypedEventRecord<TEvent> &)>;

template <typename TEvent> struct EventTraits {
  using EventType = std::remove_cvref_t<TEvent>;

  inline static constexpr std::string_view Channel =
      NGIN::Meta::TypeName<EventType>::qualifiedName;
  inline static constexpr NGIN::UInt64 ChannelId =
      NGIN::Meta::GetTypeId<EventType>();
  inline static constexpr EventQueue DefaultQueue = EventQueue::Main;
  inline static constexpr ReservedKernelEvent Reserved =
      ReservedKernelEvent::None;
};

template <> struct EventTraits<KernelStartingEvent> {
  inline static constexpr std::string_view Channel = "KernelStarting";
  inline static constexpr NGIN::UInt64 ChannelId =
      NGIN::Hashing::FNV1a64(Channel);
  inline static constexpr EventQueue DefaultQueue = EventQueue::Main;
  inline static constexpr ReservedKernelEvent Reserved =
      ReservedKernelEvent::KernelStarting;
};

template <> struct EventTraits<KernelRunningEvent> {
  inline static constexpr std::string_view Channel = "KernelRunning";
  inline static constexpr NGIN::UInt64 ChannelId =
      NGIN::Hashing::FNV1a64(Channel);
  inline static constexpr EventQueue DefaultQueue = EventQueue::Main;
  inline static constexpr ReservedKernelEvent Reserved =
      ReservedKernelEvent::KernelRunning;
};

template <> struct EventTraits<KernelStoppingEvent> {
  inline static constexpr std::string_view Channel = "KernelStopping";
  inline static constexpr NGIN::UInt64 ChannelId =
      NGIN::Hashing::FNV1a64(Channel);
  inline static constexpr EventQueue DefaultQueue = EventQueue::Main;
  inline static constexpr ReservedKernelEvent Reserved =
      ReservedKernelEvent::KernelStopping;
};

template <> struct EventTraits<ModuleLoadedEvent> {
  inline static constexpr std::string_view Channel = "ModuleLoaded";
  inline static constexpr NGIN::UInt64 ChannelId =
      NGIN::Hashing::FNV1a64(Channel);
  inline static constexpr EventQueue DefaultQueue = EventQueue::Main;
  inline static constexpr ReservedKernelEvent Reserved =
      ReservedKernelEvent::ModuleLoaded;
};

template <> struct EventTraits<ModuleStartedEvent> {
  inline static constexpr std::string_view Channel = "ModuleStarted";
  inline static constexpr NGIN::UInt64 ChannelId =
      NGIN::Hashing::FNV1a64(Channel);
  inline static constexpr EventQueue DefaultQueue = EventQueue::Main;
  inline static constexpr ReservedKernelEvent Reserved =
      ReservedKernelEvent::ModuleStarted;
};

template <> struct EventTraits<ModuleFailedEvent> {
  inline static constexpr std::string_view Channel = "ModuleFailed";
  inline static constexpr NGIN::UInt64 ChannelId =
      NGIN::Hashing::FNV1a64(Channel);
  inline static constexpr EventQueue DefaultQueue = EventQueue::Main;
  inline static constexpr ReservedKernelEvent Reserved =
      ReservedKernelEvent::ModuleFailed;
};

template <> struct EventTraits<ConfigChangeEvent> {
  inline static constexpr std::string_view Channel = "ConfigChanged";
  inline static constexpr NGIN::UInt64 ChannelId =
      NGIN::Hashing::FNV1a64(Channel);
  inline static constexpr EventQueue DefaultQueue = EventQueue::Main;
  inline static constexpr ReservedKernelEvent Reserved =
      ReservedKernelEvent::ConfigChanged;
};

template <typename TEvent>
[[nodiscard]] constexpr auto EventChannelName() noexcept -> std::string_view {
  return EventTraits<std::remove_cvref_t<TEvent>>::Channel;
}

template <typename TEvent>
[[nodiscard]] constexpr auto EventChannelId() noexcept -> NGIN::UInt64 {
  return EventTraits<std::remove_cvref_t<TEvent>>::ChannelId;
}

[[nodiscard]] inline auto MakeEventMetadata(const RawEventRecord &record)
    -> EventMetadata {
  return EventMetadata{
      .channel = record.channel,
      .channelId = record.channelId,
      .sequence = record.sequence,
      .queue = record.queue,
      .reserved = record.reserved,
  };
}

namespace detail {
template <typename TEvent>
[[nodiscard]] inline auto MakeTypedEventRecord(TEvent &&event)
    -> CoreResult<RawEventRecord> {
  using EventType = std::remove_cvref_t<TEvent>;

  try {
    return RawEventRecord{
        .channel = std::string(EventChannelName<EventType>()),
        .channelId = EventChannelId<EventType>(),
        .payload = NGIN::Utilities::Any<>(std::forward<TEvent>(event)),
        .sequence = 0,
        .queue = EventTraits<EventType>::DefaultQueue,
        .reserved = EventTraits<EventType>::Reserved,
    };
  } catch (const std::exception &exception) {
    return NGIN::Utilities::Unexpected<KernelError>(
        MakeKernelError(KernelErrorCode::InternalError, "Events",
                        std::string(EventChannelName<EventType>()),
                        "failed to wrap typed event payload: " +
                            std::string(exception.what())));
  } catch (...) {
    return NGIN::Utilities::Unexpected<KernelError>(
        MakeKernelError(KernelErrorCode::InternalError, "Events",
                        std::string(EventChannelName<EventType>()),
                        "failed to wrap typed event payload"));
  }
}
} // namespace detail

/// @brief Event bus public interface.
class NGIN_CORE_API IEventBus {
public:
  virtual ~IEventBus() = default;

  virtual auto SubscribeRaw(std::string channel, RawEventCallback callback,
                            EventScope scope, NGIN::Int32 priority = 0) noexcept
      -> CoreResult<EventSubscriptionToken> = 0;

  virtual auto Unsubscribe(EventSubscriptionToken token) noexcept
      -> CoreResult<void> = 0;

  virtual auto PublishRawImmediate(RawEventRecord eventRecord) noexcept
      -> CoreResult<void> = 0;

  virtual auto EnqueueRaw(RawEventRecord eventRecord) noexcept
      -> CoreResult<void> = 0;

  virtual auto EnqueueRawTo(EventQueue queue,
                            RawEventRecord eventRecord) noexcept
      -> CoreResult<void> = 0;

  virtual auto FlushRaw(std::string_view channel = {}) noexcept
      -> CoreResult<void> = 0;

  virtual auto FlushRawFrom(EventQueue queue,
                            std::string_view channel = {}) noexcept
      -> CoreResult<void> = 0;

  virtual void ClearScope(const EventScope &scope) noexcept = 0;

  template <typename TEvent>
  auto Subscribe(TypedEventCallback<std::remove_cvref_t<TEvent>> callback,
                 EventScope scope, NGIN::Int32 priority = 0) noexcept
      -> CoreResult<EventSubscriptionToken> {
    using EventType = std::remove_cvref_t<TEvent>;

    if (!callback) {
      return NGIN::Utilities::Unexpected<KernelError>(
          MakeKernelError(KernelErrorCode::InvalidArgument, "Events", {},
                          "callback cannot be empty"));
    }

    return SubscribeRaw(
        std::string(EventChannelName<EventType>()),
        [typedCallback = std::move(callback)](const RawEventRecord &rawRecord) {
          const auto *typedEvent =
              rawRecord.payload.template TryCast<EventType>();
          if (typedEvent == nullptr) {
            throw std::runtime_error(
                "typed event payload mismatch for channel: " +
                rawRecord.channel);
          }

          typedCallback(TypedEventRecord<EventType>{
              .event = *typedEvent,
              .metadata = MakeEventMetadata(rawRecord),
          });
        },
        std::move(scope), priority);
  }

  template <typename TEvent>
  auto Publish(TEvent &&event) noexcept -> CoreResult<void> {
    auto rawRecord = detail::MakeTypedEventRecord(std::forward<TEvent>(event));
    if (!rawRecord) {
      return NGIN::Utilities::Unexpected<KernelError>(rawRecord.Error());
    }
    return PublishRawImmediate(std::move(rawRecord.Value()));
  }

  template <typename TEvent>
  auto Enqueue(TEvent &&event) noexcept -> CoreResult<void> {
    auto rawRecord = detail::MakeTypedEventRecord(std::forward<TEvent>(event));
    if (!rawRecord) {
      return NGIN::Utilities::Unexpected<KernelError>(rawRecord.Error());
    }
    return EnqueueRaw(std::move(rawRecord.Value()));
  }

  template <typename TEvent>
  auto EnqueueTo(EventQueue queue, TEvent &&event) noexcept
      -> CoreResult<void> {
    auto rawRecord = detail::MakeTypedEventRecord(std::forward<TEvent>(event));
    if (!rawRecord) {
      return NGIN::Utilities::Unexpected<KernelError>(rawRecord.Error());
    }
    return EnqueueRawTo(queue, std::move(rawRecord.Value()));
  }

  template <typename TEvent> auto Flush() noexcept -> CoreResult<void> {
    return FlushRaw(EventChannelName<std::remove_cvref_t<TEvent>>());
  }

  template <typename TEvent>
  auto FlushFrom(EventQueue queue) noexcept -> CoreResult<void> {
    return FlushRawFrom(queue, EventChannelName<std::remove_cvref_t<TEvent>>());
  }
};

/// @brief Default in-process event bus implementation.
class NGIN_CORE_API EventBus final : public IEventBus {
public:
  EventBus() = default;

  auto SubscribeRaw(std::string channel, RawEventCallback callback,
                    EventScope scope, NGIN::Int32 priority = 0) noexcept
      -> CoreResult<EventSubscriptionToken> override;

  auto Unsubscribe(EventSubscriptionToken token) noexcept
      -> CoreResult<void> override;

  auto PublishRawImmediate(RawEventRecord eventRecord) noexcept
      -> CoreResult<void> override;

  auto EnqueueRaw(RawEventRecord eventRecord) noexcept
      -> CoreResult<void> override;

  auto EnqueueRawTo(EventQueue queue, RawEventRecord eventRecord) noexcept
      -> CoreResult<void> override;

  auto FlushRaw(std::string_view channel = {}) noexcept
      -> CoreResult<void> override;

  auto FlushRawFrom(EventQueue queue, std::string_view channel = {}) noexcept
      -> CoreResult<void> override;

  void ClearScope(const EventScope &scope) noexcept override;

private:
  struct Subscription {
    EventSubscriptionToken token{};
    std::string channel{};
    NGIN::UInt64 channelId{0};
    RawEventCallback callback{};
    EventScope scope{};
    NGIN::Int32 priority{0};
  };

  using SubscriptionBucket = std::vector<Subscription>;

  [[nodiscard]] static auto ChannelIdFor(std::string_view channel) noexcept
      -> NGIN::UInt64;
  [[nodiscard]] static auto ValidateChannel(std::string_view channel,
                                            std::string_view operation) noexcept
      -> CoreResult<void>;
  static void SortBucket(SubscriptionBucket &bucket) noexcept;

  mutable std::mutex m_mutex;
  std::unordered_map<NGIN::UInt64, SubscriptionBucket> m_subscriptionsByChannel;
  std::unordered_map<NGIN::UInt64, NGIN::UInt64> m_subscriptionChannelByToken;
  std::unordered_map<NGIN::UInt8, std::deque<RawEventRecord>> m_deferredByQueue;
  NGIN::UInt64 m_nextToken{1};
  NGIN::UInt64 m_sequence{0};
};

/// @brief Create a default event bus instance.
NGIN_CORE_API auto CreateEventBus() noexcept -> NGIN::Memory::Shared<IEventBus>;
} // namespace NGIN::Core
