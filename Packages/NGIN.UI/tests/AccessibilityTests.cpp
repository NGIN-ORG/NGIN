#include <NGIN/UI/Accessibility.hpp>
#include <NGIN/UI/Application.hpp>
#include <NGIN/UI/Composer.hpp>
#include <NGIN/UI/Testing/RecordingRenderBackend.hpp>
#include <NGIN/UI/Testing/TestPlatformBackend.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace NGIN;
using namespace NGIN::UI;

namespace {
auto Node(const UInt64 id, const SemanticRole role, const UInt64 parent = 0)
    -> SemanticNode {
  return SemanticNode{
      .id = SemanticNodeId{id},
      .parent = SemanticNodeId{parent},
      .role = role,
      .label = NGIN::Text::String{"node"},
      .bounds = Rect{1.0F, 2.0F, 30.0F, 20.0F},
  };
}

class RecordingAccessibilityBackend final : public IAccessibilityBackend {
public:
  [[nodiscard]] auto Name() const noexcept -> const char * override {
    return "Recording accessibility";
  }
  [[nodiscard]] auto Capabilities() const noexcept
      -> AccessibilityCapabilityFlags override {
    return AccessibilityCapabilityFlags::NativeBridge |
           AccessibilityCapabilityFlags::Actions;
  }
  auto Initialize(IAccessibilityActionSink &value) noexcept
      -> UIResult<void> override {
    sink = &value;
    diagnostics.configured = true;
    diagnostics.available = true;
    diagnostics.providerName = NGIN::Text::String{Name()};
    diagnostics.capabilities = Capabilities();
    return {};
  }
  auto AttachWindow(const AccessibilityWindowInfo &info) noexcept
      -> UIResult<void> override {
    attached = info;
    diagnostics.attachedWindowCount = 1;
    return {};
  }
  auto DetachWindow(PlatformWindowHandle window) noexcept
      -> UIResult<void> override {
    detached = window;
    diagnostics.attachedWindowCount = 0;
    return {};
  }
  auto Publish(AccessibilitySnapshot value) noexcept
      -> UIResult<void> override {
    snapshot = std::move(value);
    ++diagnostics.publishedSnapshotCount;
    return {};
  }
  [[nodiscard]] auto Diagnostics() const noexcept
      -> AccessibilityDiagnostics override {
    return diagnostics;
  }

  IAccessibilityActionSink *sink{};
  AccessibilityWindowInfo attached{};
  PlatformWindowHandle detached{};
  AccessibilitySnapshot snapshot{};
  AccessibilityDiagnostics diagnostics{};
};
} // namespace

TEST_CASE("accessibility snapshot lookup is stable") {
  AccessibilitySnapshot snapshot{
      .revision = 4,
      .root = SemanticNodeId{1},
      .nodes = {Node(1, SemanticRole::Window),
                Node(2, SemanticRole::Button, 1)},
  };

  REQUIRE(snapshot.Find(SemanticNodeId{2}) != nullptr);
  CHECK(snapshot.Find(SemanticNodeId{2})->role == SemanticRole::Button);
  CHECK(snapshot.Find(SemanticNodeId{99}) == nullptr);
}

TEST_CASE("accessibility snapshot diff describes provider events") {
  auto oldRoot = Node(1, SemanticRole::Window);
  oldRoot.children = {SemanticNodeId{2}, SemanticNodeId{3}};
  auto oldButton = Node(2, SemanticRole::Button, 1);
  oldButton.actions = SemanticActionFlags::Activate;
  auto removed = Node(3, SemanticRole::ListItem, 1);
  removed.states = SemanticStateFlags::Selected;

  auto newRoot = oldRoot;
  newRoot.children = {SemanticNodeId{2}, SemanticNodeId{4}};
  auto newButton = oldButton;
  newButton.label = NGIN::Text::String{"renamed"};
  newButton.states = SemanticStateFlags::Focused | SemanticStateFlags::Selected;
  newButton.live = SemanticLiveSetting::Polite;
  auto added = Node(4, SemanticRole::Text, 1);

  const AccessibilitySnapshot previous{
      .revision = 8,
      .root = SemanticNodeId{1},
      .focused = SemanticNodeId{3},
      .nodes = {oldRoot, oldButton, removed},
  };
  const AccessibilitySnapshot current{
      .revision = 9,
      .root = SemanticNodeId{1},
      .focused = SemanticNodeId{2},
      .nodes = {newRoot, newButton, added},
  };

  const auto diff = DiffAccessibilitySnapshots(previous, current);
  CHECK(diff.previousRevision == 8);
  CHECK(diff.revision == 9);
  REQUIRE(diff.added.size() == 1);
  CHECK(diff.added.front() == SemanticNodeId{4});
  REQUIRE(diff.removed.size() == 1);
  CHECK(diff.removed.front() == SemanticNodeId{3});
  CHECK(diff.structureChanged);
  CHECK(diff.previousFocus == SemanticNodeId{3});
  CHECK(diff.focus == SemanticNodeId{2});
  REQUIRE(diff.changed.size() == 1);
  CHECK(HasAccessibilityProperty(diff.changed.front().properties,
                                 AccessibilityPropertyFlags::Name));
  REQUIRE(diff.selectionChanged.size() == 1);
  CHECK(diff.selectionChanged.front() == SemanticNodeId{2});
  REQUIRE(diff.liveRegionChanged.size() == 1);
  CHECK(diff.liveRegionChanged.front() == SemanticNodeId{2});
  CHECK_FALSE(diff.Empty());
}

TEST_CASE("equal accessibility snapshots have an empty diff") {
  const AccessibilitySnapshot snapshot{
      .revision = 3,
      .root = SemanticNodeId{1},
      .nodes = {Node(1, SemanticRole::Window)},
  };
  const auto diff = DiffAccessibilitySnapshots(snapshot, snapshot);
  CHECK(diff.Empty());
}

TEST_CASE("application publishes semantics and executes provider actions") {
  using namespace NGIN::UI::Testing;

  auto backend = std::make_unique<RecordingAccessibilityBackend>();
  auto *observer = backend.get();
  auto created = CreateApplication(ApplicationCreateInfo{
      .platform = std::make_unique<TestPlatformBackend>(),
      .renderer = std::make_unique<RecordingRenderBackend>(),
      .accessibility = std::move(backend),
  });
  REQUIRE(created.HasValue());
  auto application = std::move(created).Value();
  auto windowResult = application->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"accessibility-test"},
      .title = NGIN::Text::String{"Accessible window"},
  });
  REQUIRE(windowResult.HasValue());
  auto *window = windowResult.Value();
  bool activated = false;
  window->SetContent([&](Composer &composer) {
    NodeProperties button{};
    button.interaction.enabled = true;
    button.interaction.focusable = true;
    button.interaction.onActivate = [&] { activated = true; };
    button.semantics.role = SemanticRole::Button;
    button.semantics.label = NGIN::Text::String{"Run action"};
    button.semantics.actions =
        SemanticActionFlags::Activate | SemanticActionFlags::Focus;
    composer.Column(
        [&] {
          composer.Leaf(ElementType::Button, button, "run");
          NodeProperties password{};
          password.textField.password = true;
          password.semantics.role = SemanticRole::TextBox;
          password.semantics.label = NGIN::Text::String{"Password"};
          password.semantics.value = NGIN::Text::String{"must not escape"};
          composer.Leaf(ElementType::TextField, password, "password");
        },
        "content");
  });

  REQUIRE(application->PumpOnce().HasValue());
  CHECK(observer->attached.nativeWindow.kind == NativeWindowKind::Win32);
  REQUIRE(observer->snapshot.nodes.size() >= 2);
  const auto item = std::find_if(observer->snapshot.nodes.begin(),
                                 observer->snapshot.nodes.end(),
                                 [](const SemanticNode &node) {
                                   return node.role == SemanticRole::Button;
                                 });
  REQUIRE(item != observer->snapshot.nodes.end());
  const auto buttonId = item->id;
  const auto password = std::find_if(
      observer->snapshot.nodes.begin(), observer->snapshot.nodes.end(),
      [](const SemanticNode &node) { return node.password; });
  REQUIRE(password != observer->snapshot.nodes.end());
  CHECK(password->value.Empty());
  REQUIRE(observer->sink != nullptr);
  REQUIRE(observer->sink
              ->PostAccessibilityAction(AccessibilityActionRequest{
                  .window = window->PlatformHandle(),
                  .semantic =
                      SemanticActionRequest{
                          .node = buttonId,
                          .action = SemanticActionKind::Activate,
                      },
              })
              .HasValue());
  CHECK_FALSE(activated);
  REQUIRE(application->PumpOnce().HasValue());
  CHECK(activated);

  const auto platformWindow = window->PlatformHandle();
  REQUIRE(application->CloseWindow(*window).HasValue());
  CHECK(observer->detached == platformWindow);
  CHECK(application->AccessibilityDiagnostics().attachedWindowCount == 0);
  REQUIRE(observer->sink
              ->PostAccessibilityAction(AccessibilityActionRequest{
                  .window = platformWindow,
                  .semantic =
                      SemanticActionRequest{
                          .node = buttonId,
                          .action = SemanticActionKind::Activate,
                      },
              })
              .HasValue());
  REQUIRE(application->PumpOnce().HasValue());
  CHECK(application->AccessibilityDiagnostics().lastError.has_value());
}
