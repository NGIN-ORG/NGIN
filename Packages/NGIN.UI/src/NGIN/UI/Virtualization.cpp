#include <NGIN/UI/Virtualization.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <utility>

namespace NGIN::UI {
namespace {
[[nodiscard]] auto NormalizedOptions(FixedVirtualizationOptions options)
    -> FixedVirtualizationOptions {
  options.itemExtent = std::max(1.0F, options.itemExtent);
  options.itemGap = std::max(0.0F, options.itemGap);
  options.initialViewportExtent =
      std::max(options.itemExtent, options.initialViewportExtent);
  return options;
}

[[nodiscard]] auto LowerAscii(const std::string_view value) -> std::string {
  std::string result;
  result.reserve(value.size());
  for (const auto character : value) {
    result.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }
  return result;
}

[[nodiscard]] auto FnvMix(UInt64 value, const std::string_view text) noexcept
    -> UInt64 {
  constexpr UInt64 prime = 1099511628211ULL;
  for (const auto character : text) {
    value ^= static_cast<UInt8>(character);
    value *= prime;
  }
  return value;
}
} // namespace

FixedVirtualizedListController::FixedVirtualizedListController(
    FixedVirtualizationOptions options, InvalidationScheduler scheduler)
    : m_options(NormalizedOptions(options)), m_scheduler(std::move(scheduler)),
      m_viewportExtent(m_options.initialViewportExtent) {}

FixedVirtualizedListController::~FixedVirtualizedListController() {
  if (m_requestedRange && m_source.cancelRange) {
    m_source.cancelRange(*m_requestedRange);
  }
}

void FixedVirtualizedListController::Synchronize(
    VirtualizedSourceBinding source) {
  const auto changed = source.revision != m_source.revision ||
                       source.logicalItemCount != m_source.logicalItemCount;
  const auto previousAnchor = m_anchorKey;
  const auto previousWithin = m_anchorWithinItem;
  m_source = std::move(source);

  if (changed && !previousAnchor.Empty() && m_source.indexOfKey) {
    if (const auto index = m_source.indexOfKey(previousAnchor); index) {
      m_pendingScrollOffset =
          static_cast<F32>(*index) * ItemStride() + previousWithin;
      m_viewportOffset = *m_pendingScrollOffset;
    }
  }
  if (m_source.logicalItemCount == 0) {
    m_viewportOffset = 0.0F;
    m_pendingScrollOffset = 0.0F;
  } else {
    const auto maximum = std::max(0.0F, TotalExtent() - m_viewportExtent);
    m_viewportOffset = std::clamp(m_viewportOffset, 0.0F, maximum);
    if (m_pendingScrollOffset) {
      *m_pendingScrollOffset =
          std::clamp(*m_pendingScrollOffset, 0.0F, maximum);
    }
  }
  RecalculateRange(false, changed);
  UpdateAnchor();
}

auto FixedVirtualizedListController::UpdateViewport(const F32 offset,
                                                    const F32 extent) -> bool {
  const auto nextExtent = std::max(0.0F, extent);
  const auto maximum = std::max(0.0F, TotalExtent() - nextExtent);
  const auto nextOffset = std::clamp(offset, 0.0F, maximum);
  const auto changed =
      nextOffset != m_viewportOffset || nextExtent != m_viewportExtent;
  m_viewportOffset = nextOffset;
  m_viewportExtent = nextExtent;
  const auto previousRange = m_range;
  RecalculateRange(true);
  UpdateAnchor();
  return changed || previousRange != m_range;
}

auto FixedVirtualizedListController::TakePendingScrollOffset() noexcept
    -> std::optional<F32> {
  return std::exchange(m_pendingScrollOffset, std::nullopt);
}

auto FixedVirtualizedListController::EnsureVisible(
    const UIntSize index, const F32 currentOffset,
    const F32 viewportExtent) const noexcept -> F32 {
  if (m_source.logicalItemCount == 0 || index >= m_source.logicalItemCount) {
    return currentOffset;
  }
  const auto top = static_cast<F32>(index) * ItemStride();
  const auto bottom = top + ItemExtent();
  auto result = currentOffset;
  if (top < result) {
    result = top;
  } else if (bottom > result + viewportExtent) {
    result = bottom - viewportExtent;
  }
  return std::clamp(result, 0.0F,
                    std::max(0.0F, TotalExtent() - viewportExtent));
}

auto FixedVirtualizedListController::Navigate(
    const VirtualizedNavigation navigation, const F32 currentOffset,
    const F32 viewportExtent) -> UIResult<F32> {
  if (m_source.logicalItemCount == 0) {
    return currentOffset;
  }
  const auto selected = SelectedIndex();
  UIntSize target = selected.value_or(
      std::min(m_source.logicalItemCount - 1,
               static_cast<UIntSize>(
                   std::floor(std::max(0.0F, currentOffset) / ItemStride()))));
  switch (navigation) {
  case VirtualizedNavigation::Previous:
    target = target == 0 ? 0 : target - 1;
    break;
  case VirtualizedNavigation::Next:
    target = std::min(m_source.logicalItemCount - 1, target + 1);
    break;
  case VirtualizedNavigation::First:
    target = 0;
    break;
  case VirtualizedNavigation::Last:
    target = m_source.logicalItemCount - 1;
    break;
  }
  auto activated = Activate(target);
  if (!activated) {
    return std::move(activated).Error();
  }
  return EnsureVisible(target, currentOffset, viewportExtent);
}

auto FixedVirtualizedListController::TypeAhead(const std::string_view prefix,
                                               const F32 currentOffset,
                                               const F32 viewportExtent)
    -> UIResult<F32> {
  if (prefix.empty() || m_source.logicalItemCount == 0) {
    return currentOffset;
  }
  const auto wanted = LowerAscii(prefix);
  const auto selected = SelectedIndex();
  const auto start = selected.value_or(m_source.logicalItemCount - 1);
  for (UIntSize offset = 1; offset <= m_source.logicalItemCount; ++offset) {
    const auto index = (start + offset) % m_source.logicalItemCount;
    auto label = LabelAt(index);
    if (!label) {
      continue;
    }
    const auto candidate = LowerAscii(label.Value().View());
    if (!candidate.starts_with(wanted)) {
      continue;
    }
    auto activated = Activate(index);
    if (!activated) {
      return std::move(activated).Error();
    }
    return EnsureVisible(index, currentOffset, viewportExtent);
  }
  return currentOffset;
}

auto FixedVirtualizedListController::LogicalItemCount() const noexcept
    -> UIntSize {
  return m_source.logicalItemCount;
}

auto FixedVirtualizedListController::RealizedRange() const noexcept
    -> VirtualizedRange {
  return m_range;
}

auto FixedVirtualizedListController::ItemExtent() const noexcept -> F32 {
  return m_options.itemExtent;
}

auto FixedVirtualizedListController::ItemGap() const noexcept -> F32 {
  return m_options.itemGap;
}

auto FixedVirtualizedListController::ItemStride() const noexcept -> F32 {
  return m_options.itemExtent + m_options.itemGap;
}

auto FixedVirtualizedListController::TotalExtent() const noexcept -> F32 {
  if (m_source.logicalItemCount == 0) {
    return 0.0F;
  }
  return static_cast<F32>(m_source.logicalItemCount) * ItemStride() -
         m_options.itemGap;
}

auto FixedVirtualizedListController::SelectedIndex()
    -> std::optional<UIntSize> {
  if (!m_source.selectedIndex) {
    return std::nullopt;
  }
  const auto selected = m_source.selectedIndex();
  return selected && *selected < m_source.logicalItemCount ? selected
                                                           : std::nullopt;
}

auto FixedVirtualizedListController::KeyAt(const UIntSize index)
    -> UIResult<NGIN::Text::String> {
  if (index >= m_source.logicalItemCount || !m_source.keyAt) {
    return MakeUIError(UIErrorCode::InvalidArgument,
                       "Virtualized item key index is out of range", "NGIN.UI",
                       "FixedVirtualizedListController::KeyAt");
  }
  return m_source.keyAt(index);
}

auto FixedVirtualizedListController::LabelAt(const UIntSize index)
    -> UIResult<NGIN::Text::String> {
  if (index >= m_source.logicalItemCount || !m_source.labelAt) {
    return MakeUIError(UIErrorCode::InvalidArgument,
                       "Virtualized item label index is out of range",
                       "NGIN.UI", "FixedVirtualizedListController::LabelAt");
  }
  return m_source.labelAt(index);
}

auto FixedVirtualizedListController::StableSemanticId(const ElementId list,
                                                      const UIntSize index)
    -> UInt64 {
  auto key = KeyAt(index);
  if (!key) {
    Report(key.Error());
    return list.value ^ (0x8000000000000000ULL + index + 1);
  }
  auto value = FnvMix(1469598103934665603ULL ^ list.value, key.Value().View());
  value |= 0x8000000000000000ULL;
  return value == 0 ? 1 : value;
}

auto FixedVirtualizedListController::SemanticProxyIndex(const ElementId list,
                                                        const UInt64 semanticId)
    -> std::optional<UIntSize> {
  const auto selected = SelectedIndex();
  if (selected && StableSemanticId(list, *selected) == semanticId) {
    return selected;
  }
  return std::nullopt;
}

auto FixedVirtualizedListController::Activate(const UIntSize index)
    -> UIResult<void> {
  if (index >= m_source.logicalItemCount || !m_source.activate) {
    return MakeUIError(UIErrorCode::InvalidArgument,
                       "Virtualized activation index is out of range",
                       "NGIN.UI", "FixedVirtualizedListController::Activate");
  }
  auto activated = m_source.activate(index);
  if (!activated) {
    Report(activated.Error());
  }
  return activated;
}

void FixedVirtualizedListController::RecordRealized(
    std::vector<VirtualizedItemMapping> mappings) {
  m_mappings = std::move(mappings);
}

auto FixedVirtualizedListController::Diagnostics() const
    -> VirtualizedListDiagnostics {
  return VirtualizedListDiagnostics{
      .logicalItemCount = m_source.logicalItemCount,
      .realized = m_range,
      .realizedNodeCount = m_mappings.size(),
      .overscanItems = m_options.overscanItems,
      .itemExtent = m_options.itemExtent,
      .viewportOffset = m_viewportOffset,
      .viewportExtent = m_viewportExtent,
      .totalExtent = TotalExtent(),
      .sourceRevision = m_source.revision,
      .rangeRequestCount = m_rangeRequestCount,
      .rangeCancellationCount = m_rangeCancellationCount,
      .mappings = m_mappings,
  };
}

void FixedVirtualizedListController::RecalculateRange(const bool schedule,
                                                      const bool forceRequest) {
  VirtualizedRange next{};
  if (m_source.logicalItemCount != 0) {
    const auto stride = ItemStride();
    const auto firstVisible =
        std::min(m_source.logicalItemCount - 1,
                 static_cast<UIntSize>(std::floor(m_viewportOffset / stride)));
    const auto visibleEnd = std::min(
        m_source.logicalItemCount,
        static_cast<UIntSize>(std::ceil(
            (m_viewportOffset + std::max(1.0F, m_viewportExtent)) / stride)));
    next.first = firstVisible > m_options.overscanItems
                     ? firstVisible - m_options.overscanItems
                     : 0;
    next.count = std::min(m_source.logicalItemCount,
                          visibleEnd + m_options.overscanItems) -
                 next.first;
  }
  if (!forceRequest && next == m_range && m_requestedRange == next) {
    return;
  }

  const auto previous = m_range;
  m_range = next;
  if (m_requestedRange && (forceRequest || *m_requestedRange != next) &&
      m_source.cancelRange) {
    m_source.cancelRange(*m_requestedRange);
    ++m_rangeCancellationCount;
  }
  if ((forceRequest || !m_requestedRange || *m_requestedRange != next) &&
      next.count != 0 && m_source.requestRange) {
    auto requested = m_source.requestRange(next);
    ++m_rangeRequestCount;
    if (!requested) {
      Report(requested.Error());
    }
  }
  m_requestedRange = next.count == 0 ? std::optional<VirtualizedRange>{}
                                     : std::optional<VirtualizedRange>{next};
  if (schedule && previous != next && m_scheduler) {
    m_scheduler(InvalidationKind::All);
  }
}

void FixedVirtualizedListController::UpdateAnchor() {
  if (m_source.logicalItemCount == 0 || !m_source.keyAt) {
    m_anchorKey = {};
    m_anchorWithinItem = 0.0F;
    return;
  }
  const auto index = std::min(
      m_source.logicalItemCount - 1,
      static_cast<UIntSize>(std::floor(m_viewportOffset / ItemStride())));
  auto key = m_source.keyAt(index);
  if (!key) {
    Report(key.Error());
    return;
  }
  m_anchorKey = std::move(key).Value();
  m_anchorWithinItem =
      m_viewportOffset - static_cast<F32>(index) * ItemStride();
}

void FixedVirtualizedListController::Report(const UIError &error) {
  if (m_source.onError) {
    m_source.onError(error);
  }
}
} // namespace NGIN::UI
