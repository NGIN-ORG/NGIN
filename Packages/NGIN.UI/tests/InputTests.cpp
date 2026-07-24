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

TEST_CASE("keyboard events route through capture target and bubble") {
  using namespace NGIN::UI;

  InputFixture fixture;
  std::vector<std::pair<EventPhase, ElementHandle>> route;
  NodeProperties parentProperties{};
  parentProperties.interaction.onKey = [&](RoutedKeyEvent &event) {
    route.emplace_back(event.phase, event.currentTarget);
  };
  auto buttonProperties = ButtonProperties();
  buttonProperties.interaction.onKey = [&](RoutedKeyEvent &event) {
    route.emplace_back(event.phase, event.currentTarget);
  };

  Composer composer;
  composer.Element(
      ElementType::Overlay, parentProperties,
      [&] { composer.Button([] {}, buttonProperties, "button"); }, "overlay");
  fixture.Compose(composer);

  const auto overlay = fixture.tree.Get(fixture.tree.Root())->children.front();
  const auto button = fixture.tree.Get(overlay)->children.front();
  REQUIRE(fixture.router.SetFocus(button));

  const auto result = fixture.router.Route(PlatformEvent{KeyChanged{
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Left),
      .state = KeyState::Pressed,
  }});
  REQUIRE(result.callbackInvoked);
  REQUIRE(route == std::vector<std::pair<EventPhase, ElementHandle>>{
                       {EventPhase::Capture, overlay},
                       {EventPhase::Target, button},
                       {EventPhase::Bubble, overlay},
                   });
}

TEST_CASE("tab traversal honors explicit order and skips unavailable nodes") {
  using namespace NGIN::UI;

  InputFixture fixture;
  auto firstProperties = ButtonProperties();
  firstProperties.interaction.tabIndex = 0;
  auto priorityProperties = ButtonProperties();
  priorityProperties.interaction.tabIndex = 2;
  auto skippedProperties = ButtonProperties();
  skippedProperties.interaction.tabIndex = -1;
  auto disabledProperties = ButtonProperties();
  disabledProperties.interaction.enabled = false;

  Composer composer;
  composer.Column([&] {
    composer.Button([] {}, firstProperties, "normal");
    composer.Button([] {}, priorityProperties, "priority");
    composer.Button([] {}, skippedProperties, "skipped");
    composer.Button([] {}, disabledProperties, "disabled");
  });
  fixture.Compose(composer);

  const auto *column =
      fixture.tree.Get(fixture.tree.Get(fixture.tree.Root())->children.front());
  const auto normal = column->children[0];
  const auto priority = column->children[1];

  REQUIRE(fixture.router.MoveFocus());
  REQUIRE(fixture.router.FocusedElement() == priority);

  const auto tabResult = fixture.router.Route(PlatformEvent{KeyChanged{
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Tab),
      .state = KeyState::Pressed,
  }});
  REQUIRE(tabResult.handled);
  REQUIRE(tabResult.visualStateChanged);
  REQUIRE(fixture.router.FocusedElement() == normal);

  fixture.tree.Get(normal)->interaction.pressed = true;
  fixture.tree.Get(normal)->interaction.keyboardPressed = true;
  const auto reverseTab = fixture.router.Route(PlatformEvent{KeyChanged{
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Tab),
      .state = KeyState::Pressed,
      .modifiers = static_cast<NGIN::UInt32>(KeyModifierFlags::Shift),
  }});
  REQUIRE(reverseTab.handled);
  REQUIRE(fixture.router.FocusedElement() == priority);
  REQUIRE_FALSE(fixture.tree.Get(normal)->interaction.pressed);
  REQUIRE_FALSE(fixture.tree.Get(normal)->interaction.keyboardPressed);
}

TEST_CASE("keyboard activation drives retained button state") {
  using namespace NGIN::UI;

  InputFixture fixture;
  NGIN::UIntSize activations = 0;
  Composer composer;
  composer.Button([&] { ++activations; }, ButtonProperties(), "button");
  fixture.Compose(composer);

  const auto button = fixture.tree.Get(fixture.tree.Root())->children.front();
  REQUIRE(fixture.router.SetFocus(button));

  const auto pressed = fixture.router.Route(PlatformEvent{KeyChanged{
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Space),
      .state = KeyState::Pressed,
  }});
  REQUIRE(pressed.handled);
  REQUIRE(pressed.visualStateChanged);
  REQUIRE(fixture.tree.Get(button)->interaction.pressed);
  REQUIRE(activations == 0);

  const auto released = fixture.router.Route(PlatformEvent{KeyChanged{
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Space),
      .state = KeyState::Released,
  }});
  REQUIRE(released.handled);
  REQUIRE(released.activated);
  REQUIRE_FALSE(fixture.tree.Get(button)->interaction.pressed);
  REQUIRE(activations == 1);
}

TEST_CASE("text input and composition remain separate from key events") {
  using namespace NGIN::UI;

  InputFixture fixture;
  NGIN::UIntSize keyEvents = 0;
  std::vector<RoutedTextEvent> textEvents;
  auto properties = ButtonProperties();
  properties.interaction.focusable = true;
  properties.interaction.onKey = [&](RoutedKeyEvent &) { ++keyEvents; };
  properties.interaction.onText = [&](RoutedTextEvent &event) {
    textEvents.push_back(event);
  };

  Composer composer;
  composer.Leaf(ElementType::TextField, properties, "editor");
  fixture.Compose(composer);
  const auto editor = fixture.tree.Get(fixture.tree.Root())->children.front();
  REQUIRE(fixture.router.SetFocus(editor));

  static_cast<void>(fixture.router.Route(PlatformEvent{KeyChanged{
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Space),
      .state = KeyState::Pressed,
  }}));
  static_cast<void>(fixture.router.Route(PlatformEvent{TextInput{
      .text = NGIN::Text::String{"å"},
  }}));
  static_cast<void>(fixture.router.Route(PlatformEvent{TextComposition{
      .text = NGIN::Text::String{"ö"},
      .selectionStart = 1,
      .selectionLength = 2,
  }}));

  REQUIRE(keyEvents == 1);
  REQUIRE(textEvents.size() == 2);
  REQUIRE(textEvents[0].eventKind == RoutedTextEventKind::Input);
  REQUIRE(textEvents[0].text == NGIN::Text::String{"å"});
  REQUIRE(textEvents[1].eventKind == RoutedTextEventKind::Composition);
  REQUIRE(textEvents[1].text == NGIN::Text::String{"ö"});
  REQUIRE(textEvents[1].selectionStart == 1);
  REQUIRE(textEvents[1].selectionLength == 2);
}

TEST_CASE("wheel input scrolls the nearest available ancestor") {
  using namespace NGIN::UI;

  InputFixture fixture;
  NodeProperties scrollProperties{};
  scrollProperties.layout.preferredSize = Size{100.0F, 50.0F};
  scrollProperties.layout.horizontalAlignment = HorizontalAlignment::Start;
  scrollProperties.layout.verticalAlignment = VerticalAlignment::Start;
  scrollProperties.scroll.wheelStep = 30.0F;
  auto contentProperties = ButtonProperties();
  contentProperties.layout.preferredSize = Size{100.0F, 240.0F};

  Composer composer;
  composer.ScrollView(
      [&] { composer.Leaf(ElementType::Rectangle, contentProperties); },
      scrollProperties, "scroll");
  fixture.Compose(composer);

  auto *scroll =
      fixture.tree.Get(fixture.tree.Get(fixture.tree.Root())->children.front());
  const auto content = scroll->children.front();
  const auto result = fixture.router.Route(PlatformEvent{PointerWheelChanged{
      .pointerId = 1,
      .delta = Point{0.0F, -1.0F},
      .position = Point{10.0F, 10.0F},
  }});
  REQUIRE(result.handled);
  REQUIRE(result.layoutStateChanged);
  REQUIRE(scroll->scroll.offset == Point{0.0F, 30.0F});

  static_cast<void>(fixture.layout.Perform(
      SizeConstraints{
          .minimum = Size{200.0F, 100.0F},
          .maximum = Size{200.0F, 100.0F},
      },
      Rect{0.0F, 0.0F, 200.0F, 100.0F}));
  REQUIRE(fixture.tree.Get(content)->arrangedBounds ==
          Rect{0.0F, -30.0F, 100.0F, 240.0F});
}

TEST_CASE("modal popup traps focus dismisses and restores its owner focus") {
  using namespace NGIN::UI;

  InputFixture fixture;
  NGIN::UIntSize dismissals = 0;

  Composer base;
  base.Button([] {}, ButtonProperties(), "owner");
  fixture.Compose(base);
  const auto owner = fixture.tree.Get(fixture.tree.Root())->children.front();
  REQUIRE(fixture.router.SetFocus(owner));

  NodeProperties popupProperties{};
  popupProperties.popup.onDismiss = [&] { ++dismissals; };
  Composer opened;
  opened.Button([] {}, ButtonProperties(), "owner");
  opened.Popup(
      [&] {
        opened.Column([&] {
          opened.Button([] {}, ButtonProperties(), "first");
          opened.Button([] {}, ButtonProperties(), "second");
        });
      },
      popupProperties, "popup");
  fixture.Compose(opened);

  const auto *root = fixture.tree.Get(fixture.tree.Root());
  const auto popup = root->children[1];
  const auto *column = fixture.tree.Get(fixture.tree.Get(popup)->children[0]);
  const auto first = column->children[0];
  const auto second = column->children[1];
  REQUIRE(fixture.router.FocusedElement() == first);
  REQUIRE_FALSE(fixture.router.SetFocus(owner));

  const auto tab = fixture.router.Route(PlatformEvent{KeyChanged{
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Tab),
      .state = KeyState::Pressed,
  }});
  REQUIRE(tab.handled);
  REQUIRE(fixture.router.FocusedElement() == second);

  const auto outside = fixture.router.Route(PlatformEvent{
      PointerButtonChanged{
          .pointerId = 1,
          .kind = PointerKind::Mouse,
          .button = PointerButton::Primary,
          .state = ButtonState::Pressed,
          .position = Point{190.0F, 90.0F},
      },
  });
  REQUIRE(outside.handled);
  REQUIRE(outside.callbackInvoked);
  REQUIRE(dismissals == 1);

  const auto escape = fixture.router.Route(PlatformEvent{KeyChanged{
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Escape),
      .state = KeyState::Pressed,
  }});
  REQUIRE(escape.handled);
  REQUIRE(escape.callbackInvoked);
  REQUIRE(dismissals == 2);

  Composer closed;
  closed.Button([] {}, ButtonProperties(), "owner");
  fixture.Compose(closed);
  REQUIRE(fixture.router.FocusedElement() == owner);
}
