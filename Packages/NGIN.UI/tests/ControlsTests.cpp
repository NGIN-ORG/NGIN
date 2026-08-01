#include <catch2/catch_test_macros.hpp>

#include <NGIN/UI/Controls.hpp>
#include <NGIN/UI/DisplayList.hpp>
#include <NGIN/UI/Input.hpp>
#include <NGIN/UI/Layout.hpp>
#include <NGIN/UI/Semantics.hpp>
#include <NGIN/UI/Testing/RecordingRenderBackend.hpp>
#include <NGIN/UI/Testing/TestPlatformBackend.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <variant>

namespace {
using namespace NGIN::UI;

struct ControlTree final {
  RuntimeTree tree{};
  Reconciler reconciler{tree};
  LayoutEngine layout{tree};
  InputRouter input{tree};

  void Compose(const Composer &composer, const Size size = {320.0F, 120.0F}) {
    static_cast<void>(reconciler.Reconcile(composer.Declarations()));
    static_cast<void>(
        layout.Perform(SizeConstraints{.minimum = size, .maximum = size},
                       Rect{0.0F, 0.0F, size.width, size.height}, 1.0F));
    input.Synchronize();
  }

  [[nodiscard]] auto First() const -> ElementHandle {
    return tree.Get(tree.Root())->children.front();
  }

  auto Pointer(const RoutedPointerEventKind kind, const Point position)
      -> InputDispatchResult {
    if (kind == RoutedPointerEventKind::Moved) {
      return input.Route(PointerMoved{
          .pointerId = 7,
          .kind = PointerKind::Mouse,
          .position = position,
      });
    }
    return input.Route(PointerButtonChanged{
        .pointerId = 7,
        .kind = PointerKind::Mouse,
        .button = PointerButton::Primary,
        .state = kind == RoutedPointerEventKind::ButtonPressed
                     ? ButtonState::Pressed
                     : ButtonState::Released,
        .position = position,
    });
  }
};

class StubText final : public ITextLayout, public IGlyphAtlas {
public:
  auto LayoutParagraph(const ParagraphRequest &) noexcept
      -> UIResult<ParagraphLayout> override {
    return ParagraphLayout{.size = Size{80.0F, 18.0F}};
  }

  auto ResolveGlyph(const GlyphAtlasRequest &) noexcept
      -> UIResult<GlyphAtlasEntry> override {
    return GlyphAtlasEntry{};
  }
};
} // namespace

TEST_CASE("checkbox supports checked mixed disabled and keyboard states") {
  using namespace NGIN::UI;

  State<CheckState> value{CheckState::Indeterminate};
  NodeProperties properties{};
  properties.layout.horizontalAlignment = HorizontalAlignment::Start;
  properties.layout.verticalAlignment = VerticalAlignment::Start;
  properties.semantics.label = NGIN::Text::String{"Automatic updates"};

  Composer composer;
  CheckBox(composer, Bind(value), {}, properties, "checkbox");
  ControlTree runtime;
  runtime.Compose(composer);

  auto semantics = BuildSemanticTree(runtime.tree);
  const auto *control =
      semantics.FindByOwner(runtime.tree.Get(runtime.First())->id);
  REQUIRE(control != nullptr);
  REQUIRE(control->role == SemanticRole::CheckBox);
  REQUIRE(control->value == NGIN::Text::String{"mixed"});
  REQUIRE(HasSemanticState(control->states, SemanticStateFlags::Indeterminate));
  if (runtime.input.FocusedElement() != runtime.First()) {
    REQUIRE(runtime.input.SetFocus(runtime.First()));
  }

  const auto key = runtime.input.Route(KeyChanged{
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Space),
      .state = KeyState::Pressed,
  });
  REQUIRE(key.handled);
  REQUIRE(value.Get() == CheckState::Checked);

  NodeProperties disabled = properties;
  disabled.interaction.enabled = false;
  Composer disabledComposer;
  CheckBox(disabledComposer, Bind(value), {}, disabled, "checkbox");
  runtime.Compose(disabledComposer);
  static_cast<void>(runtime.Pointer(RoutedPointerEventKind::ButtonPressed,
                                    Point{10.0F, 10.0F}));
  static_cast<void>(runtime.Pointer(RoutedPointerEventKind::ButtonReleased,
                                    Point{10.0F, 10.0F}));
  REQUIRE(value.Get() == CheckState::Checked);
  semantics = BuildSemanticTree(runtime.tree);
  control = semantics.FindByOwner(runtime.tree.Get(runtime.First())->id);
  REQUIRE(HasSemanticState(control->states, SemanticStateFlags::Disabled));
}

TEST_CASE("typed radio selection and toggle switch update bindings") {
  using namespace NGIN::UI;

  enum class Density : NGIN::UInt8 { Compact, Comfortable };
  State<Density> density{Density::Compact};
  NodeProperties properties{};
  properties.layout.horizontalAlignment = HorizontalAlignment::Start;
  properties.layout.verticalAlignment = VerticalAlignment::Start;

  Composer radioComposer;
  RadioButton(radioComposer, BindRadio(Bind(density), Density::Comfortable), {},
              properties, "comfortable");
  ControlTree radioRuntime;
  radioRuntime.Compose(radioComposer);
  static_cast<void>(radioRuntime.Pointer(RoutedPointerEventKind::ButtonPressed,
                                         Point{10.0F, 10.0F}));
  REQUIRE(radioRuntime.input.CapturedElement(7) == radioRuntime.First());
  const auto pressedDisplay = BuildDisplayList(radioRuntime.tree);
  REQUIRE(std::any_of(
      pressedDisplay.begin(), pressedDisplay.end(), [](const auto &command) {
        return std::visit(
            [](const auto &value) {
              if constexpr (requires { value.color; }) {
                return value.color == Theme{}.colors.accentPressed;
              }
              return false;
            },
            command);
      }));
  static_cast<void>(radioRuntime.Pointer(RoutedPointerEventKind::ButtonReleased,
                                         Point{10.0F, 10.0F}));
  REQUIRE(density.Get() == Density::Comfortable);
  const auto radioSemantics = BuildSemanticTree(radioRuntime.tree);
  const auto *radio = radioSemantics.FindByOwner(
      radioRuntime.tree.Get(radioRuntime.First())->id);
  REQUIRE(radio->role == SemanticRole::RadioButton);
  REQUIRE(HasSemanticState(radio->states, SemanticStateFlags::Selected));

  State<bool> enabled{false};
  Composer toggleComposer;
  ToggleSwitch(toggleComposer, Bind(enabled), {}, properties, "toggle");
  ControlTree toggleRuntime;
  toggleRuntime.Compose(toggleComposer);
  REQUIRE(toggleRuntime.input.SetFocus(toggleRuntime.First()));
  const auto activated = toggleRuntime.input.Route(KeyChanged{
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Enter),
      .state = KeyState::Pressed,
  });
  REQUIRE(activated.handled);
  REQUIRE(enabled.Get());
}

TEST_CASE("slider captures pointer and supports keyboard range actions") {
  using namespace NGIN::UI;

  State<NGIN::F32> value{0.25F};
  NodeProperties properties{};
  properties.layout.horizontalAlignment = HorizontalAlignment::Start;
  properties.layout.verticalAlignment = VerticalAlignment::Start;
  properties.semantics.label = NGIN::Text::String{"Volume"};
  Composer composer;
  Slider(composer, Bind(value),
         SliderRange{.minimum = 0.0F, .maximum = 1.0F, .step = 0.1F}, {},
         properties, "volume");

  ControlTree runtime;
  runtime.Compose(composer);
  static_cast<void>(runtime.Pointer(RoutedPointerEventKind::ButtonPressed,
                                    Point{55.0F, 14.0F}));
  REQUIRE(runtime.input.CapturedElement(7) == runtime.First());
  static_cast<void>(
      runtime.Pointer(RoutedPointerEventKind::Moved, Point{200.0F, 14.0F}));
  REQUIRE(value.Get() >= 0.8F);
  static_cast<void>(runtime.Pointer(RoutedPointerEventKind::ButtonReleased,
                                    Point{200.0F, 14.0F}));
  REQUIRE_FALSE(runtime.input.CapturedElement(7));

  if (runtime.input.FocusedElement() != runtime.First()) {
    REQUIRE(runtime.input.SetFocus(runtime.First()));
  }
  static_cast<void>(runtime.input.Route(KeyChanged{
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Home),
      .state = KeyState::Pressed,
  }));
  REQUIRE(value.Get() == 0.0F);
  static_cast<void>(runtime.input.Route(KeyChanged{
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Right),
      .state = KeyState::Pressed,
  }));
  REQUIRE(value.Get() == 0.1F);

  const auto semantics = BuildSemanticTree(runtime.tree);
  const auto *slider =
      semantics.FindByOwner(runtime.tree.Get(runtime.First())->id);
  REQUIRE(slider->role == SemanticRole::Slider);
  REQUIRE(HasSemanticAction(slider->actions, SemanticActionFlags::SetValue));
  REQUIRE(HasSemanticAction(slider->actions, SemanticActionFlags::Increment));
  REQUIRE(HasSemanticAction(slider->actions, SemanticActionFlags::Decrement));
  REQUIRE(slider->range.has_value());
  REQUIRE(slider->range->minimum == 0.0);
  REQUIRE(slider->range->maximum == 1.0);
  REQUIRE(slider->range->current > 0.09);
  REQUIRE(slider->range->current < 0.11);
  REQUIRE(slider->range->step > 0.09);
  REQUIRE(slider->range->step < 0.11);
}

TEST_CASE("progress bars expose determinate and indeterminate semantics") {
  using namespace NGIN::UI;

  NodeProperties properties{};
  properties.layout.horizontalAlignment = HorizontalAlignment::Start;
  properties.layout.verticalAlignment = VerticalAlignment::Start;
  Composer composer;
  ProgressBar(composer,
              ProgressValue{.value = 0.65F,
                            .minimum = 0.0F,
                            .maximum = 1.0F,
                            .indeterminate = true},
              {}, properties, "progress");
  ControlTree runtime;
  runtime.Compose(composer);

  const auto semantics = BuildSemanticTree(runtime.tree);
  const auto *progress =
      semantics.FindByOwner(runtime.tree.Get(runtime.First())->id);
  REQUIRE(progress != nullptr);
  REQUIRE(progress->role == SemanticRole::ProgressBar);
  REQUIRE(progress->value == NGIN::Text::String{"indeterminate"});
  REQUIRE(
      HasSemanticState(progress->states, SemanticStateFlags::Indeterminate));
}

TEST_CASE("labels publish explicit semantic control associations") {
  using namespace NGIN::UI;

  StubText text;
  Composer composer;
  Label(composer, NGIN::Text::String{"Volume"}, text, text, "volume-label",
        "volume-control", {}, "label");
  State<NGIN::F32> value{0.5F};
  NodeProperties sliderProperties{};
  sliderProperties.semantics.identifier = NGIN::Text::String{"volume-control"};
  sliderProperties.semantics.labelledBy = NGIN::Text::String{"volume-label"};
  Slider(composer, Bind(value), {}, {}, sliderProperties, "slider");

  ControlTree runtime;
  runtime.Compose(composer);
  const auto semantics = BuildSemanticTree(runtime.tree);
  REQUIRE(semantics.Nodes().size() == 3);
  const auto &label = semantics.Nodes()[1];
  const auto &slider = semantics.Nodes()[2];
  REQUIRE(label.identifier == NGIN::Text::String{"volume-label"});
  REQUIRE(label.labelFor == NGIN::Text::String{"volume-control"});
  REQUIRE(slider.identifier == NGIN::Text::String{"volume-control"});
  REQUIRE(slider.labelledBy == NGIN::Text::String{"volume-label"});
}

TEST_CASE(
    "foundational controls share theme validation focus and disabled colors") {
  using namespace NGIN::UI;

  State<CheckState> check{CheckState::Checked};
  State<NGIN::UInt8> radio{1};
  State<bool> toggle{true};
  State<NGIN::F32> slider{0.5F};
  Theme theme{};
  theme.colors.error = Color{0.91F, 0.02F, 0.17F, 1.0F};
  theme.colors.focus = Color{0.03F, 0.88F, 0.72F, 1.0F};
  theme.colors.disabledSurface = Color{0.21F, 0.22F, 0.23F, 1.0F};
  theme.colors.disabledForeground = Color{0.31F, 0.32F, 0.33F, 1.0F};
  const ControlPresentation invalid{.theme = theme, .invalid = true};
  const ControlPresentation normal{.theme = theme};

  const auto composeControls = [&](Composer &composer,
                                   const NodeProperties &properties,
                                   const ControlPresentation &presentation) {
    composer.Column([&] {
      CheckBox(composer, Bind(check), presentation, properties, "check");
      RadioButton(composer, BindRadio(Bind(radio), NGIN::UInt8{1}),
                  presentation, properties, "radio");
      ToggleSwitch(composer, Bind(toggle), presentation, properties, "toggle");
      Slider(composer, Bind(slider), {}, presentation, properties, "slider");
      ProgressBar(composer, ProgressValue{.value = 0.5F}, presentation,
                  properties, "progress");
    });
  };

  NodeProperties properties{};
  properties.layout.horizontalAlignment = HorizontalAlignment::Start;
  properties.layout.verticalAlignment = VerticalAlignment::Start;
  Composer invalidComposer;
  composeControls(invalidComposer, properties, invalid);
  ControlTree invalidRuntime;
  invalidRuntime.Compose(invalidComposer, Size{320.0F, 180.0F});
  const auto column = invalidRuntime.First();
  const auto firstControl = invalidRuntime.tree.Get(column)->children.front();
  REQUIRE(invalidRuntime.input.SetFocus(firstControl));
  const auto invalidDisplay = BuildDisplayList(invalidRuntime.tree);
  const auto usesColor = [](const DisplayCommand &command, const Color color) {
    return std::visit(
        [color](const auto &value) {
          if constexpr (requires { value.color; }) {
            return value.color == color;
          }
          return false;
        },
        command);
  };
  REQUIRE(std::count_if(invalidDisplay.begin(), invalidDisplay.end(),
                        [&](const DisplayCommand &command) {
                          return usesColor(command, theme.colors.error);
                        }) >= 5);
  REQUIRE(std::any_of(invalidDisplay.begin(), invalidDisplay.end(),
                      [&](const DisplayCommand &command) {
                        return usesColor(command, theme.colors.focus);
                      }));

  properties.interaction.enabled = false;
  Composer disabledComposer;
  composeControls(disabledComposer, properties, normal);
  ControlTree disabledRuntime;
  disabledRuntime.Compose(disabledComposer, Size{320.0F, 180.0F});
  const auto disabledDisplay = BuildDisplayList(disabledRuntime.tree);
  REQUIRE(std::count_if(
              disabledDisplay.begin(), disabledDisplay.end(),
              [&](const DisplayCommand &command) {
                return usesColor(command, theme.colors.disabledSurface) ||
                       usesColor(command, theme.colors.disabledForeground);
              }) >= 5);
}

TEST_CASE("tooltips open after a delay without taking keyboard focus") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  auto platform = std::make_unique<TestPlatformBackend>();
  auto *platformObserver = platform.get();
  auto created = CreateApplication(ApplicationCreateInfo{
      .platform = std::move(platform),
      .renderer = std::make_unique<RecordingRenderBackend>(),
  });
  REQUIRE(created.HasValue());
  auto application = std::move(created).Value();
  auto createdWindow = application->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"Tooltip"},
      .title = NGIN::Text::String{"Tooltip"},
      .initialSize = PixelSize{240, 120},
  });
  REQUIRE(createdWindow.HasValue());
  auto *window = createdWindow.Value();
  ToolTipController tooltip{*window, NGIN::Text::String{"Delayed help"},
                            std::chrono::milliseconds{2}};

  window->SetContent([&](Composer &composer) {
    NodeProperties target{};
    target.layout.preferredSize = Size{100.0F, 40.0F};
    target.layout.horizontalAlignment = HorizontalAlignment::Start;
    target.layout.verticalAlignment = VerticalAlignment::Start;
    target.interaction.focusable = true;
    target.semantics.role = SemanticRole::Button;
    target.semantics.label = NGIN::Text::String{"Target"};
    tooltip.Attach(target);
    composer.Leaf(ElementType::Rectangle, target, "target");
    tooltip.Compose(
        composer,
        [&] {
          NodeProperties bubble{};
          bubble.layout.preferredSize = Size{120.0F, 32.0F};
          bubble.paintsBackground = true;
          bubble.background = Color{0.1F, 0.1F, 0.12F, 1.0F};
          composer.Leaf(ElementType::Rectangle, bubble, "bubble");
        },
        "tooltip-popup");
  });
  REQUIRE(application->PumpOnce().HasValue());
  const auto target = window->HitTest(Point{20.0F, 20.0F});
  REQUIRE(target);
  REQUIRE(window->Focus(target));

  platformObserver->InjectEvent(PointerMoved{
      .window = window->PlatformHandle(),
      .pointerId = 1,
      .kind = PointerKind::Mouse,
      .position = Point{20.0F, 20.0F},
  });
  REQUIRE(application->PumpOnce().HasValue());
  REQUIRE_FALSE(tooltip.IsOpen());
  platformObserver->AdvanceTime(std::chrono::milliseconds{4});
  REQUIRE(application->PumpOnce().HasValue());
  REQUIRE(tooltip.IsOpen());
  REQUIRE(window->FocusedElement() == target);
}

TEST_CASE("scroll views paint draggable scrollbars and accept keyboard input") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  auto platform = std::make_unique<TestPlatformBackend>();
  auto *platformObserver = platform.get();
  auto created = CreateApplication(ApplicationCreateInfo{
      .platform = std::move(platform),
      .renderer = std::make_unique<RecordingRenderBackend>(),
  });
  REQUIRE(created.HasValue());
  auto application = std::move(created).Value();
  auto createdWindow = application->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"Scrollbars"},
      .title = NGIN::Text::String{"Scrollbars"},
      .initialSize = PixelSize{120, 80},
  });
  REQUIRE(createdWindow.HasValue());
  auto *window = createdWindow.Value();
  window->SetContent([](Composer &composer) {
    NodeProperties scroll{};
    scroll.layout.preferredSize = Size{100.0F, 60.0F};
    scroll.layout.maximumSize = Size{100.0F, 60.0F};
    scroll.layout.horizontalAlignment = HorizontalAlignment::Start;
    scroll.layout.verticalAlignment = VerticalAlignment::Start;
    scroll.interaction.focusable = true;
    scroll.scroll.vertical = true;
    scroll.semantics.role = SemanticRole::Group;
    scroll.semantics.label = NGIN::Text::String{"Scrollable content"};
    composer.ScrollView(
        [&] {
          NodeProperties content{};
          content.layout.preferredSize = Size{100.0F, 240.0F};
          content.layout.horizontalAlignment = HorizontalAlignment::Start;
          content.layout.verticalAlignment = VerticalAlignment::Start;
          content.paintsBackground = true;
          content.background = Color{0.2F, 0.3F, 0.5F, 1.0F};
          composer.Leaf(ElementType::Rectangle, content, "content");
        },
        scroll, "scroll");
  });
  REQUIRE(application->PumpOnce().HasValue());
  const auto *root = window->Tree().Get(window->Tree().Root());
  REQUIRE(root != nullptr);
  const auto scrollHandle = root->children.front();
  const auto *scrollNode = window->Tree().Get(scrollHandle);
  REQUIRE(scrollNode != nullptr);
  REQUIRE(scrollNode->scroll.contentSize.height >
          scrollNode->scroll.viewportSize.height);
  const auto display = BuildDisplayList(window->Tree());
  REQUIRE(
      std::count_if(display.begin(), display.end(), [](const auto &command) {
        return std::holds_alternative<FillRoundedRect>(command);
      }) >= 2);

  platformObserver->InjectEvent(PointerButtonChanged{
      .window = window->PlatformHandle(),
      .pointerId = 9,
      .kind = PointerKind::Mouse,
      .button = PointerButton::Primary,
      .state = ButtonState::Pressed,
      .position = Point{96.0F, 20.0F},
  });
  platformObserver->InjectEvent(PointerMoved{
      .window = window->PlatformHandle(),
      .pointerId = 9,
      .kind = PointerKind::Mouse,
      .position = Point{96.0F, 48.0F},
  });
  REQUIRE(application->PumpOnce().HasValue());
  REQUIRE(window->CapturedElement(9) == scrollHandle);
  REQUIRE(window->Tree().Get(scrollHandle)->scroll.offset.y > 0.0F);
  platformObserver->InjectEvent(PointerButtonChanged{
      .window = window->PlatformHandle(),
      .pointerId = 9,
      .kind = PointerKind::Mouse,
      .button = PointerButton::Primary,
      .state = ButtonState::Released,
      .position = Point{96.0F, 48.0F},
  });
  REQUIRE(application->PumpOnce().HasValue());
  REQUIRE_FALSE(window->CapturedElement(9));

  if (window->FocusedElement() != scrollHandle) {
    REQUIRE(window->Focus(scrollHandle));
  }
  platformObserver->InjectEvent(KeyChanged{
      .window = window->PlatformHandle(),
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Home),
      .state = KeyState::Pressed,
  });
  platformObserver->InjectEvent(KeyChanged{
      .window = window->PlatformHandle(),
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Down),
      .state = KeyState::Pressed,
  });
  REQUIRE(application->PumpOnce().HasValue());
  REQUIRE(window->Tree().Get(scrollHandle)->scroll.offset.y ==
          window->Tree().Get(scrollHandle)->properties.scroll.wheelStep);
}
