#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/UI/Error.hpp>
#include <NGIN/UI/Platform.hpp>
#include <NGIN/UI/Semantics.hpp>

#include <optional>
#include <vector>

namespace NGIN::UI {
/// @brief Native accessibility facilities implemented by a provider.
enum class AccessibilityCapabilityFlags : UInt16 {
  None = 0,
  NativeBridge = 1U << 0U,
  Actions = 1U << 1U,
  Events = 1U << 2U,
  MultipleWindows = 1U << 3U,
  VirtualizedItems = 1U << 4U,
};

[[nodiscard]] constexpr auto
operator|(const AccessibilityCapabilityFlags left,
          const AccessibilityCapabilityFlags right) noexcept
    -> AccessibilityCapabilityFlags {
  return static_cast<AccessibilityCapabilityFlags>(static_cast<UInt16>(left) |
                                                   static_cast<UInt16>(right));
}

[[nodiscard]] constexpr auto HasAccessibilityCapability(
    const AccessibilityCapabilityFlags value,
    const AccessibilityCapabilityFlags capability) noexcept -> bool {
  return (static_cast<UInt16>(value) & static_cast<UInt16>(capability)) ==
         static_cast<UInt16>(capability);
}

/// @brief Immutable provider-facing projection for one native window.
struct AccessibilitySnapshot final {
  PlatformWindowHandle window{};
  UInt64 revision{0};
  SemanticNodeId root{};
  SemanticNodeId focused{};
  std::vector<SemanticNode> nodes{};

  [[nodiscard]] auto Find(SemanticNodeId id) const noexcept
      -> const SemanticNode *;
};

/// @brief Provider attachment data for one native application window.
struct AccessibilityWindowInfo final {
  PlatformWindowHandle window{};
  NativeWindowInfo nativeWindow{};
  NGIN::Text::String title{};
};

/// @brief Semantic fields changed between two provider snapshots.
enum class AccessibilityPropertyFlags : UInt16 {
  None = 0,
  Role = 1U << 0U,
  Name = 1U << 1U,
  Description = 1U << 2U,
  Value = 1U << 3U,
  Range = 1U << 4U,
  State = 1U << 5U,
  Bounds = 1U << 6U,
  Actions = 1U << 7U,
  Collection = 1U << 8U,
};

[[nodiscard]] constexpr auto
operator|(const AccessibilityPropertyFlags left,
          const AccessibilityPropertyFlags right) noexcept
    -> AccessibilityPropertyFlags {
  return static_cast<AccessibilityPropertyFlags>(static_cast<UInt16>(left) |
                                                 static_cast<UInt16>(right));
}

constexpr auto operator|=(AccessibilityPropertyFlags &left,
                          const AccessibilityPropertyFlags right) noexcept
    -> AccessibilityPropertyFlags & {
  left = left | right;
  return left;
}

[[nodiscard]] constexpr auto
HasAccessibilityProperty(const AccessibilityPropertyFlags value,
                         const AccessibilityPropertyFlags property) noexcept
    -> bool {
  return (static_cast<UInt16>(value) & static_cast<UInt16>(property)) != 0;
}

/// @brief Property change associated with one stable semantic node.
struct AccessibilityNodeChange final {
  SemanticNodeId node{};
  AccessibilityPropertyFlags properties{AccessibilityPropertyFlags::None};
};

/// @brief Added, removed, changed, focused, selected, and live-region nodes.
struct AccessibilitySnapshotDiff final {
  UInt64 previousRevision{0};
  UInt64 revision{0};
  std::vector<SemanticNodeId> added{};
  std::vector<SemanticNodeId> removed{};
  std::vector<AccessibilityNodeChange> changed{};
  std::vector<SemanticNodeId> selectionChanged{};
  std::vector<SemanticNodeId> liveRegionChanged{};
  SemanticNodeId previousFocus{};
  SemanticNodeId focus{};
  bool structureChanged{false};

  [[nodiscard]] auto Empty() const noexcept -> bool;
};

/// @brief Computes provider events from stable immutable semantic snapshots.
[[nodiscard]] auto
DiffAccessibilitySnapshots(const AccessibilitySnapshot &previous,
                           const AccessibilitySnapshot &current)
    -> AccessibilitySnapshotDiff;

/// @brief Window-qualified action posted by a native accessibility provider.
struct AccessibilityActionRequest final {
  PlatformWindowHandle window{};
  SemanticActionRequest semantic{};
};

/// @brief Thread-safe action queue implemented by the UI application.
class IAccessibilityActionSink {
public:
  virtual ~IAccessibilityActionSink() = default;
  virtual auto
  PostAccessibilityAction(AccessibilityActionRequest request) noexcept
      -> UIResult<void> = 0;
};

/// @brief Current provider capability, lifecycle, event, and failure counters.
struct AccessibilityDiagnostics final {
  NGIN::Text::String providerName{"None"};
  AccessibilityCapabilityFlags capabilities{AccessibilityCapabilityFlags::None};
  UIntSize attachedWindowCount{0};
  UInt64 publishedSnapshotCount{0};
  UInt64 raisedEventCount{0};
  UInt64 postedActionCount{0};
  UInt64 failedActionCount{0};
  std::optional<UIError> lastError{};
  bool configured{false};
  bool available{false};
};

/// @brief Platform provider consuming snapshots without runtime-tree access.
class IAccessibilityBackend {
public:
  virtual ~IAccessibilityBackend() = default;

  [[nodiscard]] virtual auto Name() const noexcept -> const char * = 0;
  [[nodiscard]] virtual auto Capabilities() const noexcept
      -> AccessibilityCapabilityFlags = 0;
  virtual auto Initialize(IAccessibilityActionSink &sink) noexcept
      -> UIResult<void> = 0;
  virtual auto AttachWindow(const AccessibilityWindowInfo &info) noexcept
      -> UIResult<void> = 0;
  virtual auto DetachWindow(PlatformWindowHandle window) noexcept
      -> UIResult<void> = 0;
  virtual auto Publish(AccessibilitySnapshot snapshot) noexcept
      -> UIResult<void> = 0;
  [[nodiscard]] virtual auto Diagnostics() const noexcept
      -> AccessibilityDiagnostics = 0;
};
} // namespace NGIN::UI
