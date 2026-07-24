#include <catch2/catch_test_macros.hpp>

#include <NGIN/UI/Composer.hpp>
#include <NGIN/UI/Input.hpp>
#include <NGIN/UI/Layout.hpp>

#include <utility>
#include <vector>

namespace {
using namespace NGIN::UI;

struct InputFixture final {
  RuntimeTree tree{};
  Reconciler reconciler{tree};
  LayoutEngine layout{tree};
  InputRouter router{tree};

  void Compose(const Composer &composer) {
    static_cast<void>(reconciler.Reconcile(composer.Declarations()));
    router.Synchronize();
    static_cast<void>(layout.Perform(
        SizeConstraints{
            .minimum = Size{200.0F, 100.0F},
            .maximum = Size{200.0F, 100.0F},
        },
        Rect{0.0F, 0.0F, 200.0F, 100.0F}));
  }
};

auto ButtonProperties() -> NodeProperties {
  NodeProperties properties{};
  properties.layout.preferredSize = Size{100.0F, 40.0F};
  properties.layout.horizontalAlignment = HorizontalAlignment::Start;
  properties.layout.verticalAlignment = VerticalAlignment::Start;
  return properties;
}
} // namespace

TEST_CASE("pointer routing visits capture target and bubble phases") {
  using namespace NGIN::UI;

  InputFixture fixture;
  std::vector<std::pair<EventPhase, ElementHandle>> route;

  NodeProperties parentProperties{};
  parentProperties.interaction.onPointer = [&](RoutedPointerEvent &event) {
    if (event.eventKind == RoutedPointerEventKind::ButtonPressed) {
      route.emplace_back(event.phase, event.currentTarget);
    }
  };
  auto buttonProperties = ButtonProperties();
  buttonProperties.interaction.onPointer = [&](RoutedPointerEvent &event) {
    if (event.eventKind == RoutedPointerEventKind::ButtonPressed) {
      route.emplace_back(event.phase, event.currentTarget);
    }
  };

  Composer composer;
  composer.Element(
      ElementType::Overlay, parentProperties,
      [&] { composer.Button([] {}, buttonProperties, "button"); }, "overlay");
  fixture.Compose(composer);

  const auto *root = fixture.tree.Get(fixture.tree.Root());
  REQUIRE(root != nullptr);
  const auto overlay = root->children.front();
  const auto button = fixture.tree.Get(overlay)->children.front();

  const PlatformEvent pressed = PointerButtonChanged{
      .pointerId = 1,
      .kind = PointerKind::Mouse,
      .button = PointerButton::Primary,
      .state = ButtonState::Pressed,
      .position = Point{20.0F, 20.0F},
  };
  const auto result = fixture.router.Route(pressed);

  REQUIRE(result.callbackInvoked);
  REQUIRE(route == std::vector<std::pair<EventPhase, ElementHandle>>{
                       {EventPhase::Capture, overlay},
                       {EventPhase::Target, button},
                       {EventPhase::Bubble, overlay},
                   });
}

TEST_CASE("button hover capture focus and activation are deterministic") {
  using namespace NGIN::UI;

  InputFixture fixture;
  NGIN::UIntSize activations = 0;
  Composer composer;
  composer.Button([&] { ++activations; }, ButtonProperties(), "button");
  fixture.Compose(composer);

  const auto button = fixture.tree.Get(fixture.tree.Root())->children.front();
  REQUIRE(fixture.router.HitTest(Point{10.0F, 10.0F}) == button);
  REQUIRE_FALSE(fixture.router.HitTest(Point{150.0F, 80.0F}));

  auto moved = fixture.router.Route(PlatformEvent{PointerMoved{
      .pointerId = 7,
      .kind = PointerKind::Mouse,
      .position = Point{10.0F, 10.0F},
  }});
  REQUIRE(moved.visualStateChanged);
  REQUIRE(fixture.router.HoveredElement(7) == button);
  REQUIRE(fixture.tree.Get(button)->interaction.hovered);

  auto pressed = fixture.router.Route(PlatformEvent{PointerButtonChanged{
      .pointerId = 7,
      .kind = PointerKind::Mouse,
      .button = PointerButton::Primary,
      .state = ButtonState::Pressed,
      .position = Point{10.0F, 10.0F},
  }});
  REQUIRE(pressed.visualStateChanged);
  REQUIRE(fixture.router.CapturedElement(7) == button);
  REQUIRE(fixture.router.FocusedElement() == button);
  REQUIRE(fixture.tree.Get(button)->interaction.pressed);
  REQUIRE(fixture.tree.Get(button)->interaction.focused);

  static_cast<void>(fixture.router.Route(PlatformEvent{PointerMoved{
      .pointerId = 7,
      .kind = PointerKind::Mouse,
      .position = Point{180.0F, 80.0F},
  }}));
  REQUIRE_FALSE(fixture.router.HoveredElement(7));
  REQUIRE(fixture.router.CapturedElement(7) == button);
  REQUIRE_FALSE(fixture.tree.Get(button)->interaction.hovered);

  auto releasedOutside =
      fixture.router.Route(PlatformEvent{PointerButtonChanged{
          .pointerId = 7,
          .kind = PointerKind::Mouse,
          .button = PointerButton::Primary,
          .state = ButtonState::Released,
          .position = Point{180.0F, 80.0F},
      }});
  REQUIRE(releasedOutside.visualStateChanged);
  REQUIRE_FALSE(releasedOutside.activated);
  REQUIRE_FALSE(fixture.router.CapturedElement(7));
  REQUIRE_FALSE(fixture.tree.Get(button)->interaction.pressed);
  REQUIRE(activations == 0);

  static_cast<void>(fixture.router.Route(PlatformEvent{PointerButtonChanged{
      .pointerId = 7,
      .kind = PointerKind::Mouse,
      .button = PointerButton::Primary,
      .state = ButtonState::Pressed,
      .position = Point{10.0F, 10.0F},
  }}));
  const auto releasedInside =
      fixture.router.Route(PlatformEvent{PointerButtonChanged{
          .pointerId = 7,
          .kind = PointerKind::Mouse,
          .button = PointerButton::Primary,
          .state = ButtonState::Released,
          .position = Point{10.0F, 10.0F},
      }});
  REQUIRE(releasedInside.activated);
  REQUIRE(releasedInside.handled);
  REQUIRE(activations == 1);
}

TEST_CASE("input router discards focus and capture when a node is removed") {
  using namespace NGIN::UI;

  InputFixture fixture;
  Composer withButton;
  withButton.Button([] {}, ButtonProperties(), "button");
  fixture.Compose(withButton);

  const auto button = fixture.tree.Get(fixture.tree.Root())->children.front();
  static_cast<void>(fixture.router.Route(PlatformEvent{PointerButtonChanged{
      .pointerId = 3,
      .kind = PointerKind::Mouse,
      .button = PointerButton::Primary,
      .state = ButtonState::Pressed,
      .position = Point{10.0F, 10.0F},
  }}));
  REQUIRE(fixture.router.FocusedElement() == button);
  REQUIRE(fixture.router.CapturedElement(3) == button);

  Composer empty;
  fixture.Compose(empty);
  REQUIRE_FALSE(fixture.tree.IsAlive(button));
  REQUIRE_FALSE(fixture.router.FocusedElement());
  REQUIRE_FALSE(fixture.router.CapturedElement(3));
  REQUIRE_FALSE(fixture.router.HoveredElement(3));
}

TEST_CASE("disabled buttons neither retain capture nor activate") {
  using namespace NGIN::UI;

  InputFixture fixture;
  NGIN::UIntSize activations = 0;
  auto properties = ButtonProperties();
  Composer enabled;
  enabled.Button([&] { ++activations; }, properties, "button");
  fixture.Compose(enabled);

  const auto button = fixture.tree.Get(fixture.tree.Root())->children.front();
  static_cast<void>(fixture.router.Route(PlatformEvent{PointerButtonChanged{
      .pointerId = 9,
      .kind = PointerKind::Mouse,
      .button = PointerButton::Primary,
      .state = ButtonState::Pressed,
      .position = Point{10.0F, 10.0F},
  }}));
  REQUIRE(fixture.router.CapturedElement(9) == button);
  REQUIRE(fixture.router.FocusedElement() == button);

  properties.interaction.enabled = false;
  Composer disabled;
  disabled.Button([&] { ++activations; }, properties, "button");
  fixture.Compose(disabled);

  REQUIRE_FALSE(fixture.router.CapturedElement(9));
  REQUIRE_FALSE(fixture.router.FocusedElement());
  REQUIRE_FALSE(fixture.tree.Get(button)->interaction.pressed);

  static_cast<void>(fixture.router.Route(PlatformEvent{PointerButtonChanged{
      .pointerId = 9,
      .kind = PointerKind::Mouse,
      .button = PointerButton::Primary,
      .state = ButtonState::Released,
      .position = Point{10.0F, 10.0F},
  }}));
  REQUIRE(activations == 0);
}
