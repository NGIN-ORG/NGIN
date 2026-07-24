#include <catch2/catch_test_macros.hpp>

#include <NGIN/UI/Composer.hpp>
#include <NGIN/UI/DisplayList.hpp>
#include <NGIN/UI/Input.hpp>
#include <NGIN/UI/Layout.hpp>
#include <NGIN/UI/Semantics.hpp>
#include <NGIN/UI/UIRenderer.hpp>

#include <memory>
#include <stdexcept>
#include <variant>

namespace {
using namespace NGIN::UI;

struct ProbeStats final {
  NGIN::UIntSize measures{0};
  NGIN::UIntSize arrangements{0};
  NGIN::UIntSize paints{0};
  NGIN::UIntSize pointerEvents{0};
  NGIN::UIntSize stateConstructions{0};
  NGIN::UIntSize stateDestructions{0};
  NGIN::UIntSize unmounts{0};
  Size arrangedSize{};
  ElementId identity{};
};

struct ProbeState final {
  explicit ProbeState(std::shared_ptr<ProbeStats> value)
      : stats(std::move(value)) {
    ++stats->stateConstructions;
  }

  ~ProbeState() { ++stats->stateDestructions; }

  std::shared_ptr<ProbeStats> stats;
  NGIN::UIntSize presses{0};
};

class ProbeElement final : public ICustomElement {
public:
  explicit ProbeElement(std::shared_ptr<ProbeStats> stats)
      : m_stats(std::move(stats)) {}

  auto Measure(CustomElementContext &context, const SizeConstraints constraints)
      -> UIResult<Size> override {
    ++m_stats->measures;
    m_stats->identity = context.Identity();
    auto state = context.State<ProbeState>("probe", m_stats);
    if (!state) {
      return state.Error();
    }
    return constraints.Constrain(Size{80.0F, 40.0F});
  }

  auto Arrange(CustomElementContext &, const Size finalSize)
      -> UIResult<void> override {
    ++m_stats->arrangements;
    m_stats->arrangedSize = finalSize;
    return {};
  }

  auto Paint(CustomElementContext &, PaintContext &paint)
      -> UIResult<void> override {
    ++m_stats->paints;
    paint.Fill(Rect{-20.0F, -10.0F, 140.0F, 80.0F},
               Color{0.2F, 0.5F, 0.9F, 1.0F});
    paint.StrokeRounded(paint.Bounds(), CornerRadius::Uniform(Dp{5.0F}), 2.0F,
                        Color{1.0F, 1.0F, 1.0F, 1.0F});
    return {};
  }

  auto Semantics(CustomElementContext &context)
      -> UIResult<SemanticProperties> override {
    NGIN::Text::String value{"0"};
    if (const auto *state = context.FindState<ProbeState>("probe");
        state != nullptr && state->presses > 0) {
      value = NGIN::Text::String{"1"};
    }
    return SemanticProperties{
        .role = SemanticRole::Button,
        .label = NGIN::Text::String{"Custom probe"},
        .value = std::move(value),
        .actions = SemanticActionFlags::Activate | SemanticActionFlags::Focus,
    };
  }

  auto PointerEvent(CustomElementContext &context, RoutedPointerEvent &event)
      -> UIResult<InvalidationKind> override {
    ++m_stats->pointerEvents;
    if (event.phase == EventPhase::Target &&
        event.eventKind == RoutedPointerEventKind::ButtonPressed) {
      auto state = context.State<ProbeState>("probe", m_stats);
      if (!state) {
        return state.Error();
      }
      ++state.Value()->presses;
      event.CapturePointer();
      event.Handle();
      return InvalidationKind::Paint | InvalidationKind::Semantics;
    }
    return InvalidationKind::None;
  }

  void Unmounted(CustomElementContext &) noexcept override {
    ++m_stats->unmounts;
  }

private:
  std::shared_ptr<ProbeStats> m_stats;
};

class FailingElement final : public ICustomElement {
public:
  auto Measure(CustomElementContext &, SizeConstraints)
      -> UIResult<Size> override {
    return MakeUIError(UIErrorCode::InvalidState,
                       "Intentional custom measurement failure");
  }

  auto Paint(CustomElementContext &, PaintContext &)
      -> UIResult<void> override {
    throw std::runtime_error{"Intentional custom paint exception"};
  }

  auto Semantics(CustomElementContext &)
      -> UIResult<SemanticProperties> override {
    return MakeUIError(UIErrorCode::InvalidState,
                       "Intentional custom semantics failure");
  }
};
} // namespace

TEST_CASE(
    "custom elements retain identity state and support the full pipeline") {
  using namespace NGIN::UI;

  auto stats = std::make_shared<ProbeStats>();
  auto element = std::make_shared<ProbeElement>(stats);
  NodeProperties properties{};
  properties.layout.horizontalAlignment = HorizontalAlignment::Start;
  properties.layout.verticalAlignment = VerticalAlignment::Start;
  properties.interaction.focusable = true;

  Composer composer;
  composer.Custom(element, properties, "probe");

  RuntimeTree tree;
  Reconciler reconciler{tree};
  auto first = reconciler.Reconcile(composer.Declarations());
  REQUIRE(first.created == 1);
  const auto handle = tree.Get(tree.Root())->children.front();
  const auto identity = tree.Get(handle)->id;

  LayoutEngine layout{tree};
  static_cast<void>(layout.Perform(
      SizeConstraints{
          .minimum = Size{200.0F, 100.0F},
          .maximum = Size{200.0F, 100.0F},
      },
      Rect{0.0F, 0.0F, 200.0F, 100.0F}, 1.5F));
  REQUIRE(tree.Get(handle)->measuredSize == Size{80.0F, 40.0F});
  REQUIRE(tree.Get(handle)->arrangedBounds == Rect{0.0F, 0.0F, 80.0F, 40.0F});
  REQUIRE(stats->arrangedSize == Size{80.0F, 40.0F});
  REQUIRE(stats->identity == identity);
  REQUIRE(stats->stateConstructions == 1);

  const auto displayList = BuildDisplayList(tree);
  REQUIRE(stats->paints == 1);
  REQUIRE(displayList.size() == 6);
  REQUIRE(std::get<PushClipRect>(displayList[0]).rect ==
          Rect{0.0F, 0.0F, 80.0F, 40.0F});
  REQUIRE(std::get<PushTransform>(displayList[1]).translateX == 0.0F);
  REQUIRE(std::get<FillRect>(displayList[2]).rect ==
          Rect{-20.0F, -10.0F, 140.0F, 80.0F});
  REQUIRE(std::holds_alternative<StrokeRoundedRect>(displayList[3]));
  REQUIRE(std::holds_alternative<PopTransform>(displayList[4]));
  REQUIRE(std::holds_alternative<PopClip>(displayList[5]));

  UIRenderer renderer;
  const auto packet = renderer.Build(displayList, PixelSize{200, 100}, 1.5F);
  REQUIRE_FALSE(packet.vertices.empty());
  REQUIRE_FALSE(packet.indices.empty());
  REQUIRE_FALSE(packet.batches.empty());
  REQUIRE(packet.batches.front().scissor.width == 120);
  REQUIRE(packet.batches.front().scissor.height == 60);

  InputRouter input{tree};
  input.Synchronize();
  const auto inputResult = input.Route(PlatformEvent{PointerButtonChanged{
      .pointerId = 5,
      .kind = PointerKind::Mouse,
      .button = PointerButton::Primary,
      .state = ButtonState::Pressed,
      .position = Point{10.0F, 10.0F},
  }});
  REQUIRE(inputResult.handled);
  REQUIRE(inputResult.invalidation ==
          (InvalidationKind::Paint | InvalidationKind::Semantics));
  REQUIRE(input.CapturedElement(5) == handle);
  REQUIRE(input.FocusedElement() == handle);

  const auto semantics = BuildSemanticTree(tree);
  const auto *semantic = semantics.FindByOwner(identity);
  REQUIRE(semantic != nullptr);
  REQUIRE(semantic->role == SemanticRole::Button);
  REQUIRE(semantic->label == NGIN::Text::String{"Custom probe"});
  REQUIRE(semantic->value == NGIN::Text::String{"1"});

  Composer retained;
  retained.Custom(element, properties, "probe");
  const auto second = reconciler.Reconcile(retained.Declarations());
  REQUIRE(second.preserved == 1);
  REQUIRE(tree.Get(tree.Root())->children.front() == handle);
  REQUIRE(tree.Get(handle)->id == identity);
  REQUIRE(stats->stateConstructions == 1);

  Composer empty;
  const auto removed = reconciler.Reconcile(empty.Declarations());
  REQUIRE(removed.removed == 1);
  REQUIRE(stats->unmounts == 1);
  REQUIRE(stats->stateDestructions == 1);
}

TEST_CASE(
    "custom state reports type mismatches without replacing retained data") {
  using namespace NGIN::UI;

  CustomStateStore state;
  auto integer = state.GetOrCreate<int>("value", 42);
  REQUIRE(integer.HasValue());
  REQUIRE(*integer.Value() == 42);

  auto mismatched = state.GetOrCreate<float>("value", 1.0F);
  REQUIRE_FALSE(mismatched.HasValue());
  REQUIRE(mismatched.Error().code == UIErrorCode::InvalidState);
  REQUIRE(*state.Find<int>("value") == 42);
  REQUIRE(state.Size() == 1);
}

TEST_CASE("custom callback failures report errors and preserve safe output") {
  using namespace NGIN::UI;

  NGIN::UIntSize reportedErrors = 0;
  NodeProperties properties{};
  properties.layout.preferredSize = Size{33.0F, 17.0F};
  properties.layout.horizontalAlignment = HorizontalAlignment::Start;
  properties.layout.verticalAlignment = VerticalAlignment::Start;
  properties.custom.onError = [&](const UIError &) { ++reportedErrors; };

  Composer composer;
  composer.Custom(std::make_shared<FailingElement>(), properties, "failure");
  RuntimeTree tree;
  Reconciler reconciler{tree};
  static_cast<void>(reconciler.Reconcile(composer.Declarations()));
  LayoutEngine layout{tree};
  static_cast<void>(layout.Perform(
      SizeConstraints{
          .minimum = Size{100.0F, 50.0F},
          .maximum = Size{100.0F, 50.0F},
      },
      Rect{0.0F, 0.0F, 100.0F, 50.0F}));

  const auto custom = tree.Get(tree.Root())->children.front();
  REQUIRE(tree.Get(custom)->measuredSize == Size{33.0F, 17.0F});
  const auto displayList = BuildDisplayList(tree);
  REQUIRE(displayList.size() == 4);
  REQUIRE(std::holds_alternative<PushClipRect>(displayList[0]));
  REQUIRE(std::holds_alternative<PushTransform>(displayList[1]));
  REQUIRE(std::holds_alternative<PopTransform>(displayList[2]));
  REQUIRE(std::holds_alternative<PopClip>(displayList[3]));
  REQUIRE(reportedErrors >= 4);
}
