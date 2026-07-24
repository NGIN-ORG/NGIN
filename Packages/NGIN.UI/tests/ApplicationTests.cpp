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
