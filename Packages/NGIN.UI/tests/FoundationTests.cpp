#include <catch2/catch_test_macros.hpp>

#include <NGIN/UI/Application.hpp>
#include <NGIN/UI/Resources.hpp>
#include <NGIN/UI/Semantics.hpp>
#include <NGIN/UI/Testing/RecordingRenderBackend.hpp>
#include <NGIN/UI/Testing/TestPlatformBackend.hpp>

#include <memory>

TEST_CASE("resource scopes resolve typed values through parent scopes") {
  using namespace NGIN::UI;

  auto applicationResources = std::make_shared<ResourceScope>();
  applicationResources->Provide(ThemeResource, Theme{.revision = 42});
  applicationResources->Provide(LocaleResource, std::string{"en-US"});
  applicationResources->Provide(ReducedMotionResource, true);

  auto windowResources = std::make_shared<ResourceScope>(applicationResources);
  windowResources->Provide(LocaleResource, std::string{"sv-SE"});

  REQUIRE(windowResources->Resolve(ThemeResource) != nullptr);
  REQUIRE(windowResources->Resolve(ThemeResource)->revision == 42);
  REQUIRE(*windowResources->Resolve(LocaleResource) == "sv-SE");
  REQUIRE(*windowResources->Resolve(ReducedMotionResource));
  REQUIRE(windowResources->Resolve(HighContrastResource) == nullptr);

  Composer composer;
  composer.Scope(
      windowResources,
      [&] { composer.Leaf(ElementType::Rectangle, "content"); }, "theme-scope");
  RuntimeTree tree;
  Reconciler reconciler{tree};
  static_cast<void>(reconciler.Reconcile(composer.Declarations()));

  const auto scope = tree.Get(tree.Root())->children.front();
  REQUIRE(tree.Get(scope)->properties.resources == windowResources);
  const auto content = tree.Get(scope)->children.front();
  REQUIRE(tree.ResourcesFor(content) == windowResources);
  REQUIRE(tree.ResourcesFor(content)->Resolve(ThemeResource)->revision == 42);
}

TEST_CASE("semantic tree hoists controls through decorative layout nodes") {
  using namespace NGIN::UI;

  Composer composer;
  NodeProperties buttonProperties{};
  buttonProperties.layout.preferredSize = Size{100.0F, 40.0F};
  buttonProperties.semantics.label = NGIN::Text::String{"Save"};
  composer.Column(
      [&] {
        composer.Button([] {}, buttonProperties, "save");
        NodeProperties hiddenText{};
        hiddenText.semantics.hidden = true;
        composer.Leaf(ElementType::Text, hiddenText, "private");
      },
      "layout");

  RuntimeTree runtimeTree;
  Reconciler reconciler{runtimeTree};
  static_cast<void>(reconciler.Reconcile(composer.Declarations()));
  LayoutEngine layout{runtimeTree};
  static_cast<void>(layout.Perform(
      SizeConstraints{
          .minimum = Size{200.0F, 100.0F},
          .maximum = Size{200.0F, 100.0F},
      },
      Rect{0.0F, 0.0F, 200.0F, 100.0F}));

  const auto semantics =
      BuildSemanticTree(runtimeTree, NGIN::Text::String{"Settings"});
  REQUIRE(semantics.Nodes().size() == 2);
  REQUIRE(semantics.Find(semantics.Root())->role == SemanticRole::Window);
  REQUIRE(semantics.Find(semantics.Root())->label ==
          NGIN::Text::String{"Settings"});

  const auto *column =
      runtimeTree.Get(runtimeTree.Get(runtimeTree.Root())->children.front());
  const auto *button = runtimeTree.Get(column->children.front());
  const auto *semanticButton = semantics.FindByOwner(button->id);
  REQUIRE(semanticButton != nullptr);
  REQUIRE(semanticButton->role == SemanticRole::Button);
  REQUIRE(semanticButton->label == NGIN::Text::String{"Save"});
  REQUIRE(semanticButton->bounds == button->arrangedBounds);
  REQUIRE(HasSemanticAction(semanticButton->actions,
                            SemanticActionFlags::Activate));
  REQUIRE(semantics.Find(semantics.Root())->children ==
          std::vector<SemanticNodeId>{semanticButton->id});
}

TEST_CASE("window exposes live semantic state and frame diagnostics") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  auto platform = std::make_unique<TestPlatformBackend>();
  auto *platformObserver = platform.get();
  auto createdApplication = CreateApplication(ApplicationCreateInfo{
      .platform = std::move(platform),
      .renderer = std::make_unique<RecordingRenderBackend>(),
  });
  REQUIRE(createdApplication.HasValue());
  auto application = std::move(createdApplication).Value();

  auto createdWindow = application->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"Diagnostics"},
      .title = NGIN::Text::String{"Diagnostics"},
      .initialSize = PixelSize{200, 100},
  });
  REQUIRE(createdWindow.HasValue());
  auto *window = createdWindow.Value();

  window->SetContent([](Composer &composer) {
    NodeProperties properties{};
    properties.layout.preferredSize = Size{100.0F, 40.0F};
    properties.layout.horizontalAlignment = HorizontalAlignment::Start;
    properties.layout.verticalAlignment = VerticalAlignment::Start;
    properties.semantics.label = NGIN::Text::String{"Run"};
    properties.background = Color{0.2F, 0.4F, 0.8F, 1.0F};
    properties.paintsBackground = true;
    composer.Button([] {}, properties, "run");
  });
  REQUIRE(application->PumpOnce().HasValue());

  const auto button = window->HitTest(Point{10.0F, 10.0F});
  const auto *runtimeButton = window->Tree().Get(button);
  REQUIRE(runtimeButton != nullptr);
  const auto *semanticButton =
      window->Semantics().FindByOwner(runtimeButton->id);
  REQUIRE(semanticButton != nullptr);
  REQUIRE_FALSE(
      HasSemanticState(semanticButton->states, SemanticStateFlags::Focused));

  const auto &initial = window->Diagnostics();
  REQUIRE(initial.frameCount == 1);
  REQUIRE(initial.compositionCount == 1);
  REQUIRE(initial.semanticNodeCount == 2);
  REQUIRE(initial.displayCommandCount == 1);
  REQUIRE(initial.drawBatchCount == 1);
  REQUIRE(initial.vertexCount == 9);
  REQUIRE(initial.indexCount == 36);
  REQUIRE(initial.frameTimings.totalMilliseconds >= 0.0);

  platformObserver->InjectEvent(PointerButtonChanged{
      .window = window->PlatformHandle(),
      .pointerId = 11,
      .kind = PointerKind::Mouse,
      .button = PointerButton::Primary,
      .state = ButtonState::Pressed,
      .position = Point{10.0F, 10.0F},
  });
  REQUIRE(application->PumpOnce().HasValue());

  semanticButton = window->Semantics().FindByOwner(runtimeButton->id);
  REQUIRE(
      HasSemanticState(semanticButton->states, SemanticStateFlags::Focused));
  REQUIRE(
      HasSemanticState(semanticButton->states, SemanticStateFlags::Pressed));
  REQUIRE(window->Diagnostics().frameCount == 2);
  REQUIRE(window->Diagnostics().focusedElement == button);
  REQUIRE(window->Diagnostics().pointerCaptureOwner == button);
}

TEST_CASE("inspector snapshots runtime state and appends debugging overlays") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  auto createdApplication = CreateApplication(ApplicationCreateInfo{
      .platform = std::make_unique<TestPlatformBackend>(),
      .renderer = std::make_unique<RecordingRenderBackend>(),
  });
  REQUIRE(createdApplication.HasValue());
  auto application = std::move(createdApplication).Value();

  auto createdWindow = application->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"Inspector.Target"},
      .title = NGIN::Text::String{"Inspector target"},
      .initialSize = PixelSize{200, 100},
  });
  REQUIRE(createdWindow.HasValue());
  auto *window = createdWindow.Value();
  window->SetContent([](Composer &composer) {
    NodeProperties properties{};
    properties.layout.preferredSize = Size{100.0F, 40.0F};
    properties.layout.horizontalAlignment = HorizontalAlignment::Start;
    properties.layout.verticalAlignment = VerticalAlignment::Start;
    properties.background = Color{0.2F, 0.4F, 0.8F, 1.0F};
    properties.paintsBackground = true;
    properties.semantics.label = NGIN::Text::String{"Inspect me"};
    composer.Button([] {}, properties, "inspect-me");
  });
  REQUIRE(application->PumpOnce().HasValue());

  const auto button = window->HitTest(Point{10.0F, 10.0F});
  REQUIRE(window->Focus(button));
  window->SetInspectorOverlay(InspectorOverlayOptions{
      .enabled = true,
      .showLayoutBounds = true,
      .showHitTestBounds = true,
      .showFocus = true,
      .selected = button,
  });
  REQUIRE(application->PumpOnce().HasValue());

  const auto snapshot = window->Inspect();
  REQUIRE(snapshot.windowId == NGIN::Text::String{"Inspector.Target"});
  REQUIRE(snapshot.pixelExtent == PixelSize{200, 100});
  REQUIRE(snapshot.nodes.size() == 2);
  REQUIRE(snapshot.semanticNodes.size() == 2);
  REQUIRE(snapshot.diagnostics.focusedElement == button);
  REQUIRE(snapshot.nodes[1].handle == button);
  REQUIRE(snapshot.nodes[1].parent == window->Tree().Root());
  REQUIRE(snapshot.nodes[1].key == NGIN::Text::String{"inspect-me"});
  REQUIRE(snapshot.nodes[1].depth == 1);
  REQUIRE(snapshot.nodes[1].interaction.focused);
  REQUIRE(snapshot.nodes[1].arrangedBounds == Rect{0.0F, 0.0F, 100.0F, 40.0F});

  const auto overlay =
      BuildInspectorOverlay(window->Tree(), window->InspectorOverlay());
  REQUIRE(overlay.size() == 6);
  REQUIRE(window->DisplayCommandCount() == 7);
  REQUIRE(std::holds_alternative<StrokeRect>(overlay.back()));
  REQUIRE(std::get<StrokeRect>(overlay.back()).color ==
          window->InspectorOverlay().selectedColor);
}
