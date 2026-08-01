#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/UI/Error.hpp>
#include <NGIN/UI/Handles.hpp>
#include <NGIN/UI/State.hpp>
#include <NGIN/Utilities/Callable.hpp>

#include <optional>
#include <string_view>
#include <vector>

namespace NGIN::UI {
/// @brief Half-open logical item range realized by a virtualized collection.
struct VirtualizedRange final {
  UIntSize first{0};
  UIntSize count{0};

  [[nodiscard]] constexpr auto End() const noexcept -> UIntSize {
    return first + count;
  }

  [[nodiscard]] constexpr auto Contains(const UIntSize index) const noexcept
      -> bool {
    return index >= first && index < End();
  }

  [[nodiscard]] constexpr auto
  operator<=>(const VirtualizedRange &) const noexcept = default;
};

/// @brief Stable source index and key for one currently realized item.
struct VirtualizedItemMapping final {
  UIntSize sourceIndex{0};
  NGIN::Text::String key{};
};

/// @brief Fixed-size virtualization contract used by version 0.2 lists.
struct FixedVirtualizationOptions final {
  F32 itemExtent{28.0F};
  F32 itemGap{0.0F};
  UIntSize overscanItems{3};
  F32 initialViewportExtent{280.0F};
};

/// @brief Logical and realized work reported by one virtualized collection.
struct VirtualizedListDiagnostics final {
  ElementId element{};
  UIntSize logicalItemCount{0};
  VirtualizedRange realized{};
  UIntSize realizedNodeCount{0};
  UIntSize overscanItems{0};
  F32 itemExtent{0.0F};
  F32 viewportOffset{0.0F};
  F32 viewportExtent{0.0F};
  F32 totalExtent{0.0F};
  UInt64 sourceRevision{0};
  UInt64 rangeRequestCount{0};
  UInt64 rangeCancellationCount{0};
  std::vector<VirtualizedItemMapping> mappings{};
};

/// @brief Logical movement requested from a virtualized collection.
enum class VirtualizedNavigation : UInt8 {
  Previous,
  Next,
  First,
  Last,
};

/// @brief Type-erased stable-key source and selection behavior for a
/// controller.
struct VirtualizedSourceBinding final {
  UIntSize logicalItemCount{0};
  UInt64 revision{0};
  NGIN::Utilities::Callable<UIResult<NGIN::Text::String>(UIntSize)> keyAt{};
  NGIN::Utilities::Callable<UIResult<NGIN::Text::String>(UIntSize)> labelAt{};
  NGIN::Utilities::Callable<std::optional<UIntSize>(const NGIN::Text::String &)>
      indexOfKey{};
  NGIN::Utilities::Callable<std::optional<UIntSize>()> selectedIndex{};
  NGIN::Utilities::Callable<UIResult<void>(UIntSize)> activate{};
  NGIN::Utilities::Callable<UIResult<void>(VirtualizedRange)> requestRange{};
  NGIN::Utilities::Callable<void(VirtualizedRange)> cancelRange{};
  NGIN::Utilities::Callable<void(const UIError &)> onError{};
};

/// @brief Retains viewport, anchoring, navigation, and range-request state.
class FixedVirtualizedListController final {
public:
  explicit FixedVirtualizedListController(
      FixedVirtualizationOptions options = {},
      InvalidationScheduler scheduler = {});
  ~FixedVirtualizedListController();

  FixedVirtualizedListController(const FixedVirtualizedListController &) =
      delete;
  FixedVirtualizedListController(FixedVirtualizedListController &&) = delete;
  auto operator=(const FixedVirtualizedListController &)
      -> FixedVirtualizedListController & = delete;
  auto operator=(FixedVirtualizedListController &&)
      -> FixedVirtualizedListController & = delete;

  void Synchronize(VirtualizedSourceBinding source);
  [[nodiscard]] auto UpdateViewport(F32 offset, F32 extent) -> bool;
  [[nodiscard]] auto TakePendingScrollOffset() noexcept -> std::optional<F32>;
  [[nodiscard]] auto EnsureVisible(UIntSize index, F32 currentOffset,
                                   F32 viewportExtent) const noexcept -> F32;
  [[nodiscard]] auto Navigate(VirtualizedNavigation navigation,
                              F32 currentOffset, F32 viewportExtent)
      -> UIResult<F32>;
  [[nodiscard]] auto TypeAhead(std::string_view prefix, F32 currentOffset,
                               F32 viewportExtent) -> UIResult<F32>;

  [[nodiscard]] auto LogicalItemCount() const noexcept -> UIntSize;
  [[nodiscard]] auto RealizedRange() const noexcept -> VirtualizedRange;
  [[nodiscard]] auto ItemExtent() const noexcept -> F32;
  [[nodiscard]] auto ItemGap() const noexcept -> F32;
  [[nodiscard]] auto ItemStride() const noexcept -> F32;
  [[nodiscard]] auto TotalExtent() const noexcept -> F32;
  [[nodiscard]] auto SelectedIndex() -> std::optional<UIntSize>;
  [[nodiscard]] auto KeyAt(UIntSize index) -> UIResult<NGIN::Text::String>;
  [[nodiscard]] auto LabelAt(UIntSize index) -> UIResult<NGIN::Text::String>;
  [[nodiscard]] auto StableSemanticId(ElementId list, UIntSize index) -> UInt64;
  [[nodiscard]] auto SemanticProxyIndex(ElementId list, UInt64 semanticId)
      -> std::optional<UIntSize>;
  [[nodiscard]] auto Activate(UIntSize index) -> UIResult<void>;
  void RecordRealized(std::vector<VirtualizedItemMapping> mappings);
  [[nodiscard]] auto Diagnostics() const -> VirtualizedListDiagnostics;

private:
  void RecalculateRange(bool schedule, bool forceRequest = false);
  void UpdateAnchor();
  void Report(const UIError &error);

  FixedVirtualizationOptions m_options{};
  InvalidationScheduler m_scheduler{};
  VirtualizedSourceBinding m_source{};
  VirtualizedRange m_range{};
  std::optional<VirtualizedRange> m_requestedRange{};
  std::vector<VirtualizedItemMapping> m_mappings{};
  NGIN::Text::String m_anchorKey{};
  F32 m_anchorWithinItem{0.0F};
  F32 m_viewportOffset{0.0F};
  F32 m_viewportExtent{0.0F};
  std::optional<F32> m_pendingScrollOffset{};
  UInt64 m_rangeRequestCount{0};
  UInt64 m_rangeCancellationCount{0};
};

/// @brief Controller attached to a ListView element to enable virtualization.
struct VirtualizedListProperties final {
  FixedVirtualizedListController *controller{nullptr};
};

/// @brief Logical source identity attached to one realized ListItem.
struct VirtualizedItemProperties final {
  bool enabled{false};
  UIntSize sourceIndex{0};
  NGIN::Text::String key{};
};
} // namespace NGIN::UI
