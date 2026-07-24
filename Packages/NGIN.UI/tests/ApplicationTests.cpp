#include <catch2/catch_test_macros.hpp>

#include <NGIN/UI/Application.hpp>
#include <NGIN/UI/Testing/RecordingRenderBackend.hpp>
#include <NGIN/UI/Testing/TestPlatformBackend.hpp>

#include <memory>

TEST_CASE("application requires both backend roles") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  auto missingPlatform = CreateApplication(ApplicationCreateInfo{
      .renderer = std::make_unique<RecordingRenderBackend>(),
  });
  REQUIRE_FALSE(missingPlatform.HasValue());
  REQUIRE(missingPlatform.Error().code == UIErrorCode::InvalidArgument);

  auto missingRenderer = CreateApplication(ApplicationCreateInfo{
      .platform = std::make_unique<TestPlatformBackend>(),
  });
  REQUIRE_FALSE(missingRenderer.HasValue());
  REQUIRE(missingRenderer.Error().code == UIErrorCode::InvalidArgument);
}

TEST_CASE("headless application completes the logical window frame lifecycle") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  auto platform = std::make_unique<TestPlatformBackend>();
  auto renderer = std::make_unique<RecordingRenderBackend>();
  auto *platformObserver = platform.get();
  auto *rendererObserver = renderer.get();

  auto createdApplication = CreateApplication(ApplicationCreateInfo{
      .platform = std::move(platform),
      .renderer = std::move(renderer),
      .applicationName = NGIN::Text::String{"NGIN.UI.Tests"},
      .enableRendererValidation = true,
  });
  REQUIRE(createdApplication.HasValue());
  auto application = std::move(createdApplication).Value();

  auto createdWindow = application->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"Main"},
      .title = NGIN::Text::String{"Headless window"},
      .initialSize = PixelSize{800, 600},
      .minimumSize = PixelSize{320, 240},
  });
  REQUIRE(createdWindow.HasValue());
  auto *window = createdWindow.Value();

  REQUIRE(application->ActiveWindowCount() == 1);
  REQUIRE_FALSE(application->ShouldExit());
  REQUIRE(platformObserver->Windows().front().visible);
  REQUIRE(rendererObserver->Surfaces().size() == 1);
  REQUIRE(window->IsDirty());

  REQUIRE(application->PumpOnce().HasValue());
  REQUIRE_FALSE(window->IsDirty());
  REQUIRE(rendererObserver->RenderPackets().size() == 1);
  REQUIRE(rendererObserver->RenderPackets().front().vertices.empty());
  REQUIRE(rendererObserver->RenderPackets().front().targetSize ==
          PixelSize{800, 600});
  REQUIRE(rendererObserver->Surfaces().front().presentCount == 1);

  NGIN::UIntSize pointerEvents = 0;
  window->SetEventHandler([&](const PlatformEvent &event) {
    if (std::holds_alternative<PointerMoved>(event)) {
      ++pointerEvents;
    }
  });

  platformObserver->InjectEvent(WindowResized{
      window->PlatformHandle(),
      PixelSize{1024, 768},
  });
  platformObserver->InjectEvent(PointerMoved{
      .window = window->PlatformHandle(),
      .pointerId = 1,
      .kind = PointerKind::Mouse,
      .position = Point{50.0F, 60.0F},
  });

  REQUIRE(application->PumpOnce().HasValue());
  REQUIRE(window->PixelExtent() == PixelSize{1024, 768});
  REQUIRE(pointerEvents == 1);
  REQUIRE(rendererObserver->Surfaces().front().size == PixelSize{1024, 768});
  REQUIRE(rendererObserver->RenderPackets().size() == 2);
  REQUIRE(rendererObserver->RenderPackets().back().targetSize ==
          PixelSize{1024, 768});

  platformObserver->InjectEvent(WindowCloseRequested{window->PlatformHandle()});
  REQUIRE(application->PumpOnce().HasValue());
  REQUIRE(window->IsCloseRequested());
  REQUIRE(window->IsClosed());
  REQUIRE(application->ActiveWindowCount() == 0);
  REQUIRE(application->ShouldExit());
  REQUIRE(platformObserver->Windows().front().destroyed);
  REQUIRE(rendererObserver->Surfaces().front().destroyed);
}

TEST_CASE(
    "application rejects invalid and duplicate logical window identities") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  auto createdApplication = CreateApplication(ApplicationCreateInfo{
      .platform = std::make_unique<TestPlatformBackend>(),
      .renderer = std::make_unique<RecordingRenderBackend>(),
  });
  REQUIRE(createdApplication.HasValue());
  auto application = std::move(createdApplication).Value();

  auto emptyId = application->CreateWindow(WindowCreateInfo{
      .title = NGIN::Text::String{"No id"},
  });
  REQUIRE_FALSE(emptyId.HasValue());

  const WindowCreateInfo valid{
      .id = NGIN::Text::String{"Main"},
      .title = NGIN::Text::String{"Main"},
  };
  REQUIRE(application->CreateWindow(valid).HasValue());
  auto duplicate = application->CreateWindow(valid);
  REQUIRE_FALSE(duplicate.HasValue());
  REQUIRE(duplicate.Error().code == UIErrorCode::InvalidArgument);
}

TEST_CASE("window content composes on demand and retains runtime identity") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  auto createdApplication = CreateApplication(ApplicationCreateInfo{
      .platform = std::make_unique<TestPlatformBackend>(),
      .renderer = std::make_unique<RecordingRenderBackend>(),
  });
  REQUIRE(createdApplication.HasValue());
  auto application = std::move(createdApplication).Value();

  auto createdWindow = application->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"Main"},
      .title = NGIN::Text::String{"Composition"},
  });
  REQUIRE(createdWindow.HasValue());
  auto *window = createdWindow.Value();

  NGIN::UIntSize compositionCount = 0;
  window->SetContent([&](Composer &composer) {
    ++compositionCount;
    composer.Column(
        [&] {
          composer.Leaf(ElementType::Text, "title");
          composer.Leaf(ElementType::Button, "action");
        },
        "root-column");
  });

  REQUIRE(application->PumpOnce().HasValue());
  REQUIRE(compositionCount == 1);
  REQUIRE(window->Tree().LiveCount() == 4);
  REQUIRE(window->LastReconcileStats().created == 3);

  const auto *root = window->Tree().Get(window->Tree().Root());
  REQUIRE(root != nullptr);
  const auto columnHandle = root->children.front();

  window->Invalidate(InvalidationKind::Paint);
  REQUIRE(application->PumpOnce().HasValue());
  REQUIRE(compositionCount == 1);
  REQUIRE(window->Tree().Get(window->Tree().Root())->children.front() ==
          columnHandle);

  window->Invalidate(InvalidationKind::Compose | InvalidationKind::Paint);
  REQUIRE(application->PumpOnce().HasValue());
  REQUIRE(compositionCount == 2);
  REQUIRE(window->LastReconcileStats().preserved == 3);
  REQUIRE(window->Tree().Get(window->Tree().Root())->children.front() ==
          columnHandle);
}

TEST_CASE("window frames lay out painted nodes into renderer packets") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  auto renderer = std::make_unique<RecordingRenderBackend>();
  auto *rendererObserver = renderer.get();
  auto createdApplication = CreateApplication(ApplicationCreateInfo{
      .platform = std::make_unique<TestPlatformBackend>(),
      .renderer = std::move(renderer),
  });
  REQUIRE(createdApplication.HasValue());
  auto application = std::move(createdApplication).Value();

  auto createdWindow = application->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"Paint"},
      .title = NGIN::Text::String{"Paint"},
      .initialSize = PixelSize{200, 100},
  });
  REQUIRE(createdWindow.HasValue());
  auto *window = createdWindow.Value();

  window->SetContent([](Composer &composer) {
    NodeProperties properties{};
    properties.layout.preferredSize = Size{50.0F, 20.0F};
    properties.layout.horizontalAlignment = HorizontalAlignment::Center;
    properties.layout.verticalAlignment = VerticalAlignment::Center;
    properties.background = Color{0.2F, 0.4F, 0.8F, 1.0F};
    properties.paintsBackground = true;
    composer.Leaf(ElementType::Rectangle, properties, "rectangle");
  });

  REQUIRE(application->PumpOnce().HasValue());
  REQUIRE(window->DisplayCommandCount() == 1);
  REQUIRE(window->LastLayoutStats().measured == 2);
  REQUIRE(window->LastLayoutStats().arranged == 2);
  REQUIRE(rendererObserver->RenderPackets().size() == 1);
  REQUIRE(rendererObserver->RenderPackets().front().vertices.size() == 4);
  REQUIRE(rendererObserver->RenderPackets().front().indices.size() == 6);

  const auto *rectangle = window->Tree().Get(
      window->Tree().Get(window->Tree().Root())->children.front());
  REQUIRE(rectangle != nullptr);
  REQUIRE(rectangle->arrangedBounds == Rect{75.0F, 40.0F, 50.0F, 20.0F});
}

TEST_CASE("window routes injected pointer input to a semantic button") {
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
      .id = NGIN::Text::String{"Input"},
      .title = NGIN::Text::String{"Input"},
      .initialSize = PixelSize{200, 100},
  });
  REQUIRE(createdWindow.HasValue());
  auto *window = createdWindow.Value();

  NGIN::UIntSize activations = 0;
  window->SetContent([&](Composer &composer) {
    NodeProperties properties{};
    properties.layout.preferredSize = Size{100.0F, 40.0F};
    properties.layout.horizontalAlignment = HorizontalAlignment::Start;
    properties.layout.verticalAlignment = VerticalAlignment::Start;
    composer.Button([&] { ++activations; }, properties, "activate");
  });
  REQUIRE(application->PumpOnce().HasValue());

  const auto button = window->HitTest(Point{20.0F, 20.0F});
  REQUIRE(button);

  platformObserver->InjectEvent(PointerButtonChanged{
      .window = window->PlatformHandle(),
      .pointerId = 1,
      .kind = PointerKind::Mouse,
      .button = PointerButton::Primary,
      .state = ButtonState::Pressed,
      .position = Point{20.0F, 20.0F},
  });
  platformObserver->InjectEvent(PointerButtonChanged{
      .window = window->PlatformHandle(),
      .pointerId = 1,
      .kind = PointerKind::Mouse,
      .button = PointerButton::Primary,
      .state = ButtonState::Released,
      .position = Point{20.0F, 20.0F},
  });

  REQUIRE(application->PumpOnce().HasValue());
  REQUIRE(activations == 1);
  REQUIRE(window->FocusedElement() == button);
  REQUIRE_FALSE(window->CapturedElement(1));
  REQUIRE(window->Tree().Get(button)->interaction.focused);

  platformObserver->InjectEvent(KeyChanged{
      .window = window->PlatformHandle(),
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Enter),
      .state = KeyState::Pressed,
  });
  platformObserver->InjectEvent(KeyChanged{
      .window = window->PlatformHandle(),
      .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Enter),
      .state = KeyState::Released,
  });
  REQUIRE(application->PumpOnce().HasValue());
  REQUIRE(activations == 2);
}

TEST_CASE("window wheel input arranges and clips retained scroll content") {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  auto platform = std::make_unique<TestPlatformBackend>();
  auto renderer = std::make_unique<RecordingRenderBackend>();
  auto *platformObserver = platform.get();
  auto *rendererObserver = renderer.get();
  auto createdApplication = CreateApplication(ApplicationCreateInfo{
      .platform = std::move(platform),
      .renderer = std::move(renderer),
  });
  REQUIRE(createdApplication.HasValue());
  auto application = std::move(createdApplication).Value();

  auto createdWindow = application->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"Scroll"},
      .title = NGIN::Text::String{"Scroll"},
      .initialSize = PixelSize{100, 50},
  });
  REQUIRE(createdWindow.HasValue());
  auto *window = createdWindow.Value();

  window->SetContent([](Composer &composer) {
    NodeProperties scrollProperties{};
    scrollProperties.scroll.wheelStep = 40.0F;
    NodeProperties contentProperties{};
    contentProperties.layout.preferredSize = Size{100.0F, 200.0F};
    contentProperties.paintsBackground = true;
    contentProperties.background = Color{0.4F, 0.5F, 0.7F, 1.0F};
    composer.ScrollView(
        [&] {
          composer.Leaf(ElementType::Rectangle, contentProperties, "content");
        },
        scrollProperties, "scroll");
  });
  REQUIRE(application->PumpOnce().HasValue());

  const auto *root = window->Tree().Get(window->Tree().Root());
  const auto scrollHandle = root->children.front();
  const auto contentHandle = window->Tree().Get(scrollHandle)->children.front();
  REQUIRE(window->DisplayCommandCount() == 3);

  platformObserver->InjectEvent(PointerWheelChanged{
      .window = window->PlatformHandle(),
      .pointerId = 1,
      .delta = Point{0.0F, -1.0F},
      .position = Point{10.0F, 10.0F},
  });
  REQUIRE(application->PumpOnce().HasValue());

  REQUIRE(window->Tree().Get(scrollHandle)->scroll.offset ==
          Point{0.0F, 40.0F});
  REQUIRE(window->Tree().Get(contentHandle)->arrangedBounds ==
          Rect{0.0F, -40.0F, 100.0F, 200.0F});
  REQUIRE(rendererObserver->RenderPackets().back().batches.front().scissor ==
          PixelRect{0, 0, 100, 50});
}

TEST_CASE("dialog windows preserve ownership modality and owner lifecycle") {
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

  auto createdOwner = application->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"Owner"},
      .title = NGIN::Text::String{"Owner"},
      .initialSize = PixelSize{200, 100},
  });
  REQUIRE(createdOwner.HasValue());
  auto *owner = createdOwner.Value();
  owner->SetContent([](Composer &composer) {
    NodeProperties properties{};
    properties.layout.preferredSize = Size{100.0F, 40.0F};
    properties.layout.horizontalAlignment = HorizontalAlignment::Start;
    properties.layout.verticalAlignment = VerticalAlignment::Start;
    composer.Button([] {}, properties, "owner-action");
  });
  REQUIRE(application->PumpOnce().HasValue());
  const auto ownerButton = owner->HitTest(Point{10.0F, 10.0F});
  REQUIRE(owner->Focus(ownerButton));

  auto createdDialog = application->CreateDialogWindow(
      *owner, WindowCreateInfo{
                  .id = NGIN::Text::String{"Dialog"},
                  .title = NGIN::Text::String{"Dialog"},
                  .initialSize = PixelSize{120, 80},
              });
  REQUIRE(createdDialog.HasValue());
  auto *dialog = createdDialog.Value();
  REQUIRE(dialog->Kind() == WindowKind::Dialog);
  REQUIRE(dialog->IsModal());
  REQUIRE(dialog->Owner() == owner);
  REQUIRE(owner->ActiveModalDialog() == dialog);
  REQUIRE(platformObserver->Windows()[1].info.kind == WindowKind::Dialog);
  REQUIRE(platformObserver->Windows()[1].info.owner == owner->PlatformHandle());
  REQUIRE(platformObserver->Windows()[1].info.modal);

  auto duplicateModal = application->CreateDialogWindow(
      *owner, WindowCreateInfo{
                  .id = NGIN::Text::String{"SecondDialog"},
                  .title = NGIN::Text::String{"Second dialog"},
              });
  REQUIRE_FALSE(duplicateModal.HasValue());
  REQUIRE(duplicateModal.Error().code == UIErrorCode::InvalidState);

  NGIN::UIntSize blockedEvents = 0;
  owner->SetEventHandler([&](const PlatformEvent &) { ++blockedEvents; });
  platformObserver->InjectEvent(PointerMoved{
      .window = owner->PlatformHandle(),
      .pointerId = 1,
      .position = Point{10.0F, 10.0F},
  });
  platformObserver->InjectEvent(WindowCloseRequested{owner->PlatformHandle()});
  REQUIRE(application->PumpOnce().HasValue());
  REQUIRE(blockedEvents == 0);
  REQUIRE_FALSE(owner->IsCloseRequested());
  REQUIRE_FALSE(owner->IsClosed());

  platformObserver->InjectEvent(WindowCloseRequested{dialog->PlatformHandle()});
  REQUIRE(application->PumpOnce().HasValue());
  REQUIRE(dialog->IsClosed());
  REQUIRE(owner->ActiveModalDialog() == nullptr);
  REQUIRE(owner->FocusedElement() == ownerButton);

  auto createdOwned =
      application->CreateDialogWindow(*owner,
                                      WindowCreateInfo{
                                          .id = NGIN::Text::String{"Owned"},
                                          .title = NGIN::Text::String{"Owned"},
                                      },
                                      false);
  REQUIRE(createdOwned.HasValue());
  auto *owned = createdOwned.Value();
  REQUIRE_FALSE(owned->IsModal());
  REQUIRE(application->CloseWindow(*owner).HasValue());
  REQUIRE(owner->IsClosed());
  REQUIRE(owned->IsClosed());
}
