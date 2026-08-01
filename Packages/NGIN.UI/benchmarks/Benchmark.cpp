#include "Budgets.hpp"

#include <NGIN/UI/Collections.hpp>
#include <NGIN/UI/Composer.hpp>
#include <NGIN/UI/Layout.hpp>
#include <NGIN/UI/NativeText.hpp>
#include <NGIN/UI/Testing/RecordingRenderBackend.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace {
std::atomic_bool g_trackAllocations{false};
std::atomic<NGIN::UInt64> g_allocationCount{0};

void CountAllocation() noexcept {
  if (g_trackAllocations.load(std::memory_order_relaxed)) {
    g_allocationCount.fetch_add(1, std::memory_order_relaxed);
  }
}
} // namespace

void *operator new(const std::size_t size) {
  CountAllocation();
  if (void *allocation = std::malloc(std::max<std::size_t>(size, 1))) {
    return allocation;
  }
  throw std::bad_alloc{};
}

void *operator new[](const std::size_t size) { return ::operator new(size); }

void operator delete(void *allocation) noexcept { std::free(allocation); }
void operator delete[](void *allocation) noexcept { std::free(allocation); }
void operator delete(void *allocation, std::size_t) noexcept {
  std::free(allocation);
}
void operator delete[](void *allocation, std::size_t) noexcept {
  std::free(allocation);
}

namespace {
using Clock = std::chrono::steady_clock;

struct Sample final {
  NGIN::F64 milliseconds{0.0};
  NGIN::UInt64 allocations{0};
};

template <typename Operation> auto Measure(Operation &&operation) -> Sample {
  g_allocationCount.store(0, std::memory_order_relaxed);
  g_trackAllocations.store(true, std::memory_order_release);
  const auto started = Clock::now();
  operation();
  const auto stopped = Clock::now();
  g_trackAllocations.store(false, std::memory_order_release);
  return Sample{
      .milliseconds =
          std::chrono::duration<NGIN::F64, std::milli>{stopped - started}
              .count(),
      .allocations = g_allocationCount.load(std::memory_order_relaxed),
  };
}

template <typename Operation>
auto Median(const NGIN::UIntSize iterations, Operation &&operation) -> Sample {
  std::vector<Sample> samples;
  samples.reserve(iterations);
  for (NGIN::UIntSize iteration = 0; iteration < iterations; ++iteration) {
    samples.push_back(Measure(operation));
  }
  std::ranges::sort(samples, {}, &Sample::milliseconds);
  const auto milliseconds = samples[samples.size() / 2].milliseconds;
  std::ranges::sort(samples, {}, &Sample::allocations);
  return {milliseconds, samples[samples.size() / 2].allocations};
}

auto ComposeItems(NGIN::UI::Composer &composer, const NGIN::UIntSize count,
                  const bool listItems) -> void {
  using namespace NGIN::UI;
  NodeProperties root{};
  root.layout.gap = 1.0F;
  auto scope = composer.Begin(
      listItems ? ElementType::ListView : ElementType::Column, root, "root");
  NodeProperties item{};
  item.layout.preferredSize = {120.0F, 18.0F};
  for (NGIN::UIntSize index = 0; index < count; ++index) {
    const auto key = std::to_string(index);
    if (listItems) {
      composer.ListItem([] {}, item, key);
    } else {
      composer.Leaf(ElementType::Border, item, key);
    }
  }
}

class GeneratedVirtualizedSource final
    : public NGIN::UI::IVirtualizedDataSource<NGIN::UIntSize> {
public:
  [[nodiscard]] auto Count() const noexcept -> NGIN::UIntSize override {
    return 100'000;
  }
  [[nodiscard]] auto Revision() const noexcept -> NGIN::UInt64 override {
    return 1;
  }
  [[nodiscard]] auto ItemAt(const NGIN::UIntSize index) const
      -> NGIN::UI::UIResult<NGIN::UIntSize> override {
    if (!m_requested.Contains(index)) {
      return NGIN::UI::MakeUIError(NGIN::UI::UIErrorCode::ResourceFailed,
                                   "Benchmark item was not requested",
                                   "NGIN.UI.Benchmarks",
                                   "GeneratedVirtualizedSource::ItemAt");
    }
    return index;
  }
  auto RequestRange(const NGIN::UI::IncrementalRange range)
      -> NGIN::UI::UIResult<void> override {
    m_requested = {range.first, range.count};
    return {};
  }
  [[nodiscard]] auto KeyAt(const NGIN::UIntSize index) const
      -> NGIN::UI::UIResult<NGIN::Text::String> override {
    const auto value = std::to_string(index);
    return NGIN::Text::String{value.c_str()};
  }
  [[nodiscard]] auto LabelAt(const NGIN::UIntSize index) const
      -> NGIN::UI::UIResult<NGIN::Text::String> override {
    return KeyAt(index);
  }
  [[nodiscard]] auto IndexOfKey(const NGIN::Text::String &) const
      -> std::optional<NGIN::UIntSize> override {
    return std::nullopt;
  }

private:
  NGIN::UI::VirtualizedRange m_requested{};
};

auto Report(const NGIN::UI::Benchmarks::Budget &budget, const Sample sample)
    -> bool {
  const auto passed = sample.milliseconds <= budget.maximumMedianMilliseconds &&
                      sample.allocations <= budget.maximumMedianAllocations;
  std::cout << "{\"name\":\"" << budget.name
            << "\",\"medianMs\":" << sample.milliseconds
            << ",\"medianAllocations\":" << sample.allocations
            << ",\"budgetMs\":" << budget.maximumMedianMilliseconds
            << ",\"allocationBudget\":" << budget.maximumMedianAllocations
            << ",\"passed\":" << (passed ? "true" : "false") << "}\n";
  return passed;
}
} // namespace

auto main() -> int {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Benchmarks;

  bool passed = true;
  const auto composition = Median(7, [] {
    Composer composer;
    ComposeItems(composer, 2000, false);
    if (!composer.IsBalanced() || composer.Declarations().empty()) {
      std::abort();
    }
  });
  passed = Report(CompositionBudget, composition) && passed;

  RuntimeTree layoutTree;
  {
    Composer composer;
    ComposeItems(composer, 2000, false);
    Reconciler reconciler{layoutTree};
    static_cast<void>(reconciler.Reconcile(composer.Declarations()));
  }
  LayoutEngine layout{layoutTree};
  const auto layoutSample = Median(7, [&] {
    static_cast<void>(
        layout.Perform(SizeConstraints{.minimum = {1280.0F, 720.0F},
                                       .maximum = {1280.0F, 720.0F}},
                       Rect{0.0F, 0.0F, 1280.0F, 720.0F}, 1.0F));
  });
  passed = Report(LayoutBudget, layoutSample) && passed;

  Testing::RecordingRenderBackend renderer;
  if (!renderer.Initialize({}).HasValue()) {
    return 2;
  }
  auto createdText = NativeTextSystem::Create(
      renderer, NativeTextCreateInfo{.atlasSize = {512, 512}});
  if (!createdText) {
    std::cerr << "Text benchmark setup failed: "
              << createdText.Error().message.CStr() << '\n';
    return 2;
  }
  auto text = std::move(createdText).Value();
  std::string paragraphSource;
  for (NGIN::UIntSize index = 0; index < 32; ++index) {
    paragraphSource +=
        "Retained Unicode text wraps deterministically across lines. ";
  }
  const ParagraphRequest paragraph{
      .runs =
          {
              TextRun{.text = NGIN::Text::String{paragraphSource.c_str()},
                      .fontSize = 16.0F},
          },
      .maximumWidth = 720.0F,
      .wrapping = TextWrapping::Wrap,
  };
  const auto textSample = Median(7, [&] {
    auto result = text->LayoutParagraph(paragraph);
    if (!result || result.Value().lines.empty()) {
      std::abort();
    }
  });
  passed = Report(TextBudget, textSample) && passed;

  const auto largeList = Median(3, [] {
    Composer composer;
    ComposeItems(composer, 10000, true);
    RuntimeTree tree;
    Reconciler reconciler{tree};
    static_cast<void>(reconciler.Reconcile(composer.Declarations()));
    LayoutEngine engine{tree};
    static_cast<void>(
        engine.Perform(SizeConstraints{.minimum = {1280.0F, 720.0F},
                                       .maximum = {1280.0F, 720.0F}},
                       Rect{0.0F, 0.0F, 1280.0F, 720.0F}, 1.0F));
  });
  passed = Report(LargeListBudget, largeList) && passed;

  const auto virtualizedList = Median(7, [] {
    GeneratedVirtualizedSource source;
    FixedVirtualizedListController controller{FixedVirtualizationOptions{
        .itemExtent = 24.0F,
        .overscanItems = 3,
        .initialViewportExtent = 720.0F,
    }};
    Composer composer;
    VirtualizedListPresentation presentation{};
    presentation.list.layout.preferredSize = {1280.0F, 720.0F};
    presentation.list.layout.maximumSize = {1280.0F, 720.0F};
    VirtualizedListView<NGIN::UIntSize>(
        composer, controller, source,
        [](Composer &, const NGIN::UIntSize &, const NGIN::UIntSize) {},
        presentation, "virtual-list");
    RuntimeTree tree;
    Reconciler reconciler{tree};
    static_cast<void>(reconciler.Reconcile(composer.Declarations()));
    LayoutEngine engine{tree};
    const auto stats =
        engine.Perform(SizeConstraints{.minimum = {1280.0F, 720.0F},
                                       .maximum = {1280.0F, 720.0F}},
                       Rect{0.0F, 0.0F, 1280.0F, 720.0F}, 1.0F);
    if (tree.LiveCount() > 40 || stats.virtualizedLists.size() != 1 ||
        stats.virtualizedLists.front().logicalItemCount != 100'000) {
      std::abort();
    }
  });
  passed = Report(VirtualizedListBudget, virtualizedList) && passed;
  return passed ? 0 : 1;
}
