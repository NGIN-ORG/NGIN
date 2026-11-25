#include <NGIN/UI/Testing/RecordingRenderBackend.hpp>
#include <NGIN/UI/Testing/TestPlatformBackend.hpp>
#include <NGIN/UIGallery/Gallery.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace {
auto Check(const bool condition, const char *message) -> bool {
  if (!condition) {
    std::cerr << "Gallery test failed: " << message << '\n';
  }
  return condition;
}

auto Report(const char *context, const NGIN::UI::UIError &error) -> int {
  std::cerr << context << ": " << error.message.CStr() << '\n';
  return 1;
}

auto HasRole(const NGIN::UI::SemanticTree &tree,
             const NGIN::UI::SemanticRole role) -> bool {
  return std::any_of(
      tree.Nodes().begin(), tree.Nodes().end(),
      [role](const NGIN::UI::SemanticNode &node) { return node.role == role; });
}

auto FindByTypeAndKey(const NGIN::UI::RuntimeTree &tree,
                      const NGIN::UI::ElementType type,
                      const std::string_view key) -> NGIN::UI::ElementHandle {
  std::vector<NGIN::UI::ElementHandle> pending{tree.Root()};
  while (!pending.empty()) {
    const auto handle = pending.back();
    pending.pop_back();
    const auto *node = tree.Get(handle);
    if (node == nullptr) {
      continue;
    }
    if (node->type == type && node->key.View() == key) {
      return handle;
    }
    pending.insert(pending.end(), node->children.begin(), node->children.end());
  }
  return {};
}

auto Center(const NGIN::UI::Rect bounds) noexcept -> NGIN::UI::Point {
  return {bounds.x + bounds.width * 0.5F, bounds.y + bounds.height * 0.5F};
}
} // namespace

auto main() -> int {
  using namespace NGIN::UI;
  using namespace NGIN::UI::Testing;

  auto platform = std::make_unique<TestPlatformBackend>();
  auto renderer = std::make_unique<RecordingRenderBackend>();
  auto *platformObserver = platform.get();
  auto *rendererObserver = renderer.get();
  auto createdApplication = CreateApplication(ApplicationCreateInfo{
      .platform = std::move(platform),
      .renderer = std::move(renderer),
      .applicationName = NGIN::Text::String{"NGIN.UI Gallery Tests"},
      .enableRendererValidation = true,
  });
  if (!createdApplication) {
    return Report("Application creation failed", createdApplication.error());
  }
  auto application = std::move(createdApplication).value();

  auto createdText = NativeTextSystem::Create(application->Renderer());
  if (!createdText) {
    return Report("Native text creation failed", createdText.error());
  }
  auto text = std::move(createdText).value();

  NGIN::UIGallery::GalleryViewModel model;
  auto createdWindow =
      NGIN::UIGallery::CreateMainWindow(*application, *text, model);
  if (!createdWindow) {
    return Report("Gallery window creation failed", createdWindow.error());
  }
  auto *window = createdWindow.value();

  const auto checkGallery = [&]() -> int {
    platformObserver->InjectEvent(
        WindowResized{window->PlatformHandle(), PixelSize{1180, 760}});
    if (!application->PumpOnce()) {
      return 1;
    }
    if (!Check(window->PixelExtent() == PixelSize{1180, 760},
               "resize updates the logical window") ||
        !Check(!rendererObserver->Surfaces().empty() &&
                   rendererObserver->Surfaces().front().size ==
                       PixelSize{1180, 760},
               "resize updates the render surface")) {
      return 1;
    }

    const auto initialSidebar =
        FindByTypeAndKey(window->Tree(), ElementType::ScrollView, "sidebar");
    const auto page =
        FindByTypeAndKey(window->Tree(), ElementType::Column, "page");
    const auto selectedOverview =
        FindByTypeAndKey(window->Tree(), ElementType::Button, "0");
    const auto *initialSidebarNode = window->Tree().Get(initialSidebar);
    const auto *pageNode = window->Tree().Get(page);
    const auto *selectedOverviewNode = window->Tree().Get(selectedOverview);
    if (!Check(initialSidebarNode && pageNode &&
                   initialSidebarNode->arrangedBounds.x +
                           initialSidebarNode->arrangedBounds.width <=
                       pageNode->arrangedBounds.x,
               "on desktop the browser and sample occupy separate columns") ||
        !Check(selectedOverviewNode != nullptr &&
                   HasSemanticState(
                       selectedOverviewNode->properties.semantics.states,
                       SemanticStateFlags::Selected),
               "current navigation page exposes its selected state")) {
      return 1;
    }

    platformObserver->InjectEvent(
        WindowResized{window->PlatformHandle(), PixelSize{1180, 660}});
    if (!application->PumpOnce()) {
      return 1;
    }
    const auto sidebar =
        FindByTypeAndKey(window->Tree(), ElementType::ScrollView, "sidebar");
    const auto *sidebarNode = window->Tree().Get(sidebar);
    if (!Check(sidebar.IsValid(), "navigation is a clipped scroll view") ||
        !Check(sidebarNode != nullptr &&
                   sidebarNode->scroll.contentSize.height >
                       sidebarNode->scroll.viewportSize.height,
               "short windows keep navigation inside a scrollable sidebar")) {
      return 1;
    }
    platformObserver->InjectEvent(PointerWheelChanged{
        .window = window->PlatformHandle(),
        .pointerId = 1,
        .delta = Point{0.0F, -1.0F},
        .position = Center(sidebarNode->arrangedBounds),
    });
    if (!application->PumpOnce() ||
        !Check(window->Tree().Get(sidebar)->scroll.offset.y > 0.0F,
               "navigation scrolls when its buttons exceed the panel")) {
      return 1;
    }

    platformObserver->InjectEvent(
        WindowResized{window->PlatformHandle(), PixelSize{1180, 760}});
    if (!application->PumpOnce()) {
      return 1;
    }

    const auto activate = [&](std::string_view key) {
      const auto handle =
          FindByTypeAndKey(window->Tree(), ElementType::Button, key);
      if (!handle ||
          (window->FocusedElement() != handle && !window->Focus(handle))) {
        std::cerr << "Gallery test could not focus button " << key << '\n';
        return false;
      }
      platformObserver->InjectEvent(
          KeyChanged{.window = window->PlatformHandle(),
                     .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Space),
                     .state = KeyState::Pressed});
      platformObserver->InjectEvent(
          KeyChanged{.window = window->PlatformHandle(),
                     .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Space),
                     .state = KeyState::Released});
      auto frame = application->PumpOnce();
      if (!frame) {
        std::cerr << "Gallery test could not activate " << key << ": "
                  << frame.error().message.CStr() << '\n';
      }
      return frame.has_value();
    };

    using NGIN::UIGallery::Page;
    for (const auto &[key, destination] :
         {std::pair{"sample-motion", Page::Motion},
          std::pair{"sample-layout", Page::Layout},
          std::pair{"sample-inputs", Page::Inputs},
          std::pair{"sample-collections", Page::Collections}}) {
      model.SelectPage(Page::Overview);
      if (!application->PumpOnce() ||
          !Check(activate(key) && model.CurrentPage() == destination,
                 "a keyboard user can open each sample directly from its "
                 "illustration")) {
        return 1;
      }
    }

    for (NGIN::UIntSize index = 0; index < NGIN::UIGallery::PageCount;
         ++index) {
      const auto page = NGIN::UIGallery::PageAt(index);
      model.SelectPage(page);
      auto pumped = application->PumpOnce();
      if (!pumped) {
        return Report("Gallery page frame failed", pumped.error());
      }
      if (!Check(model.CurrentPage() == page,
                 "page selection is deterministic") ||
          !Check(!NGIN::UIGallery::PageName(page).empty(),
                 "every page has a name") ||
          !Check(!FindByTypeAndKey(window->Tree(), ElementType::Button,
                                   "copy-example"),
                 "samples open without a copy-code footer") ||
          !Check(!window->Semantics().Nodes().empty(),
                 "every page emits semantics")) {
        return 1;
      }
      if (page == NGIN::UIGallery::Page::Overview) {
        const auto progressRing = FindByTypeAndKey(
            window->Tree(), ElementType::CustomElement, "progress-ring");
        const auto chart = FindByTypeAndKey(
            window->Tree(), ElementType::CustomElement, "bar-chart");
        const auto *progressNode = window->Tree().Get(progressRing);
        const auto *chartNode = window->Tree().Get(chart);
        const auto *progressSemantics =
            progressNode != nullptr
                ? window->Semantics().FindByOwner(progressNode->id)
                : nullptr;
        const auto *chartSemantics =
            chartNode != nullptr
                ? window->Semantics().FindByOwner(chartNode->id)
                : nullptr;
        if (!Check(progressSemantics != nullptr &&
                       progressSemantics->role == SemanticRole::ProgressBar &&
                       HasSemanticAction(progressSemantics->actions,
                                         SemanticActionFlags::Increment),
                   "custom progress exposes an interactive value") ||
            !Check(chartSemantics != nullptr &&
                       chartSemantics->role == SemanticRole::Slider &&
                       HasSemanticAction(chartSemantics->actions,
                                         SemanticActionFlags::Increment),
                   "custom chart exposes keyboard value navigation")) {
          return 1;
        }

        const auto progressBefore = progressSemantics->value;
        if (!Check(window->Focus(progressRing),
                   "custom progress accepts keyboard focus")) {
          return 1;
        }
        platformObserver->InjectEvent(KeyChanged{
            .window = window->PlatformHandle(),
            .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Space),
            .state = KeyState::Pressed,
        });
        if (!application->PumpOnce()) {
          return 1;
        }
        progressNode = window->Tree().Get(progressRing);
        progressSemantics =
            progressNode != nullptr
                ? window->Semantics().FindByOwner(progressNode->id)
                : nullptr;
        if (!Check(progressSemantics != nullptr &&
                       progressSemantics->value != progressBefore,
                   "custom progress changes when activated")) {
          return 1;
        }

        if (!Check(window->Focus(chart),
                   "custom chart accepts keyboard focus")) {
          return 1;
        }
        platformObserver->InjectEvent(KeyChanged{
            .window = window->PlatformHandle(),
            .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Right),
            .state = KeyState::Pressed,
        });
        if (!application->PumpOnce()) {
          return 1;
        }
        chartNode = window->Tree().Get(chart);
        chartSemantics = chartNode != nullptr
                             ? window->Semantics().FindByOwner(chartNode->id)
                             : nullptr;
        if (!Check(chartSemantics != nullptr &&
                       chartSemantics->value !=
                           NGIN::Text::String{"No bar selected"},
                   "custom chart selection changes with the arrow keys")) {
          return 1;
        }
      }
      if (page == NGIN::UIGallery::Page::Inputs &&
          (!Check(HasRole(window->Semantics(), SemanticRole::CheckBox),
                  "inputs page exposes checkbox semantics") ||
           !Check(HasRole(window->Semantics(), SemanticRole::RadioButton),
                  "inputs page exposes radio semantics") ||
           !Check(HasRole(window->Semantics(), SemanticRole::Switch),
                  "inputs page exposes switch semantics") ||
           !Check(activate("inputs-level"),
                  "the volume sample opens from the keyboard") ||
           !Check(HasRole(window->Semantics(), SemanticRole::Slider),
                  "inputs page exposes slider semantics") ||
           !Check(HasRole(window->Semantics(), SemanticRole::ProgressBar),
                  "inputs page exposes progress semantics") ||
           !Check(activate("inputs-form"),
                  "the form sample opens from the keyboard") ||
           !Check(FindByTypeAndKey(window->Tree(), ElementType::Button,
                                   "validation-check")
                      .IsValid(),
                  "MVVM workflow exposes form validation") ||
           !Check(FindByTypeAndKey(window->Tree(), ElementType::Button,
                                   "validation-save")
                      .IsValid(),
                  "MVVM workflow exposes command-bound Save") ||
           !Check(FindByTypeAndKey(window->Tree(), ElementType::Button,
                                   "validation-fail")
                      .IsValid(),
                  "MVVM workflow exposes the error path") ||
           !Check(FindByTypeAndKey(window->Tree(), ElementType::Button,
                                   "validation-cancel")
                      .IsValid(),
                  "MVVM workflow exposes cancellation"))) {
        return 1;
      }
      if (page == NGIN::UIGallery::Page::Inputs) {
        const auto original = model.PasswordBinding().Get();
        if (!model.PasswordBinding().Set(NGIN::Text::String{"short"}) ||
            !activate("validation-check"))
          return 1;
        const auto *message = window->Tree().Get(FindByTypeAndKey(
            window->Tree(), ElementType::Text, "validation-password-message"));
        if (!Check(
                message && message->properties.text.value.View() ==
                               "Use at least 8 characters",
                "checking the form shows feedback beside the password field") ||
            !Check(!model.FormSaveBinding().CanExecute(),
                   "a field error disables saving") ||
            !model.PasswordBinding().Set(original))
          return 1;
      }
      if (page == NGIN::UIGallery::Page::Collections) {
        const auto virtualList = FindByTypeAndKey(
            window->Tree(), ElementType::ListView, "virtual-list-100000");
        const auto virtualDiagnostics = std::find_if(
            window->LastLayoutStats().virtualizedLists.begin(),
            window->LastLayoutStats().virtualizedLists.end(),
            [&](const auto &diagnostics) {
              const auto *node = window->Tree().Get(virtualList);
              return node != nullptr && diagnostics.element == node->id;
            });
        if (!Check(HasRole(window->Semantics(), SemanticRole::List),
                   "collections page exposes list semantics") ||
            !Check(HasRole(window->Semantics(), SemanticRole::ListItem),
                   "collections page exposes list-item semantics") ||
            !Check(virtualList.IsValid(),
                   "collections page contains the 100,000-item list") ||
            !Check(virtualDiagnostics !=
                       window->LastLayoutStats().virtualizedLists.end(),
                   "large-list realization is observable") ||
            !Check(virtualDiagnostics->logicalItemCount == 100'000,
                   "large list keeps 100,000 logical rows") ||
            !Check(virtualDiagnostics->realizedNodeCount <= 16,
                   "large list realizes only viewport rows") ||
            !Check(window->Focus(virtualList),
                   "large list accepts keyboard focus")) {
          return 1;
        }

        platformObserver->InjectEvent(KeyChanged{
            .window = window->PlatformHandle(),
            .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::End),
            .state = KeyState::Pressed,
        });
        if (!application->PumpOnce() ||
            !Check(model.SelectedVirtualizedIndex() == 99'999,
                   "End selects and reveals the last logical row") ||
            !Check(window->FocusedElement() == virtualList,
                   "large-list focus stays on its stable owner")) {
          return 1;
        }

        const auto lastRange =
            model.VirtualizedCollectionController().RealizedRange();
        if (!Check(lastRange.End() == 100'000,
                   "last logical row is realized after keyboard navigation")) {
          return 1;
        }
        if (!application->PumpOnce() ||
            !Check(FindByTypeAndKey(window->Tree(), ElementType::ListItem,
                                    "item-99999")
                       .IsValid(),
                   "the last row replaces the previous realized range")) {
          return 1;
        }

        model.PrependVirtualizedItems();
        if (!application->PumpOnce() ||
            !Check(model.VirtualizedCollectionSource().Count() == 100'250,
                   "Gallery source adds a new incremental range") ||
            !Check(model.SelectedVirtualizedIndex() == 100'249,
                   "stable selection follows inserted rows") ||
            !Check(
                model.VirtualizedCollectionController().RealizedRange().End() ==
                    100'250,
                "scroll anchoring keeps the selected end row visible") ||
            !Check(model.VirtualizedCollectionController()
                           .Diagnostics()
                           .rangeRequestCount >= 2,
                   "Gallery reports incremental range loads")) {
          return 1;
        }
        const auto itemCount = model.CollectionItems().size();
        if (!Check(activate("collections-selection") && activate("add-item") &&
                       model.CollectionItems().size() == itemCount + 1,
                   "the selection sample adds an item to the displayed list") ||
            !Check(!FindByTypeAndKey(window->Tree(), ElementType::ListView,
                                     "virtual-list-100000")
                        .IsValid(),
                   "the virtual list unmounts when switching samples") ||
            !Check(activate("collections-menus"), "open menus and tabs") ||
            !Check(HasRole(window->Semantics(), SemanticRole::ComboBox),
                   "collections page exposes combo-box semantics") ||
            !Check(HasRole(window->Semantics(), SemanticRole::TabList),
                   "collections page exposes tab-list semantics") ||
            !Check(HasRole(window->Semantics(), SemanticRole::Tab),
                   "collections page exposes tab semantics") ||
            !Check(HasRole(window->Semantics(), SemanticRole::TabPanel),
                   "collections page exposes active tab-panel semantics")) {
          return 1;
        }
      }
      if (page == NGIN::UIGallery::Page::Motion) {
        const auto *stage = window->Tree().Get(FindByTypeAndKey(
            window->Tree(), ElementType::CustomElement, "motion-stage"));
        const auto *play = window->Tree().Get(FindByTypeAndKey(
            window->Tree(), ElementType::Button, "play-curve"));
        if (!Check(stage && play && stage->properties.motion.value,
                   "timing curve and playback action mount together") ||
            !Check(
                stage->arrangedBounds.y >= 0 &&
                    stage->arrangedBounds.y + stage->arrangedBounds.height <=
                        play->arrangedBounds.y + 1 &&
                    play->arrangedBounds.y + play->arrangedBounds.height <=
                        window->PixelExtent().height &&
                    std::abs(play->arrangedBounds.y - stage->arrangedBounds.y -
                             stage->arrangedBounds.height) <= 24,
                "the preview and play button fit together without scrolling") ||
            !Check(!FindByTypeAndKey(window->Tree(), ElementType::CustomElement,
                                     "motion-progress"),
                   "other motion studies stay unmounted until selected")) {
          return 1;
        }
      }
      if (page == NGIN::UIGallery::Page::TextArea &&
          !Check(HasRole(window->Semantics(), SemanticRole::TextBox),
                 "text-area page exposes editable text semantics")) {
        return 1;
      }
      if (page == Page::Layout) {
        const auto node = [&](ElementType type, std::string_view key) {
          return window->Tree().Get(
              FindByTypeAndKey(window->Tree(), type, key));
        };
        if (!activate("tracks-two"))
          return 1;
        const auto *grid = node(ElementType::Grid, "track-artboard");
        if (!Check(grid && grid->grid.resolvedColumns.size() == 2 &&
                       grid->grid.resolvedRows.size() == 3,
                   "changing the track count reflows all six tiles") ||
            !activate("layout-wrap") || !activate("flow-narrow"))
          return 1;
        const auto *flow = node(ElementType::WrapPanel, "flow-artboard");
        if (!Check(flow && flow->wrapPanel.lines.size() > 1,
                   "labels wrap inside a narrow preview frame") ||
            !activate("layout-canvas") || !activate("offset-right"))
          return 1;
        const auto *canvas = node(ElementType::Canvas, "canvas-artboard");
        const auto *marker = node(ElementType::Border, "coordinate-marker");
        if (!Check(canvas && marker &&
                       std::abs(marker->arrangedBounds.x -
                                canvas->arrangedBounds.x - 224) < 1,
                   "canvas markers are placed in logical pixels"))
          return 1;
      }
      if (page == NGIN::UIGallery::Page::Typography) {
        const auto fontDiagnostics = model.FontDiagnostics();
        const auto usedFallback = std::any_of(
            fontDiagnostics.faces.begin(), fontDiagnostics.faces.end(),
            [](const auto &face) {
              return face.fallback && face.resolvedCodePointCount > 0;
            });
        if (!Check(fontDiagnostics.missingCodePointCount == 0,
                   "typography samples have complete packaged coverage") ||
            !Check(fontDiagnostics.fallbackCodePointCount > 0,
                   "typography samples exercise fallback fonts") ||
            !Check(usedFallback,
                   "typography diagnostics identify a used fallback face") ||
            !Check(activate("type-display"),
                   "the type scale is keyboard accessible")) {
          return 1;
        }
        const auto *proof = window->Tree().Get(
            FindByTypeAndKey(window->Tree(), ElementType::Text, "type-proof"));
        if (!Check(proof && proof->properties.text.fontSize == 44,
                   "selecting display size updates the proof")) {
          return 1;
        }
      }
      if (page == NGIN::UIGallery::Page::Images &&
          (!Check(HasRole(window->Semantics(), SemanticRole::Image),
                  "images page exposes image semantics") ||
           !Check(model.GalleryImage() &&
                      model.GalleryImage()->State() == ImageLoadState::Ready,
                  "images page decodes its staged PNG asset") ||
           !Check(model.GalleryImage()->Size() == PixelSize{1536, 1024},
                  "images page retains decoded PNG dimensions") ||
           !Check(model.ImageDiagnostics().entryCount > 0,
                  "image cache activity is observable"))) {
        return 1;
      }
      if (page == Page::Images) {
        if (!activate("image-cover"))
          return 1;
        const auto *image = window->Tree().Get(FindByTypeAndKey(
            window->Tree(), ElementType::Image, "image-proof"));
        if (!Check(image && image->properties.image.fit == ImageFit::Cover,
                   "the fit selection updates the displayed artwork"))
          return 1;
      }
      if (page == NGIN::UIGallery::Page::PropertyEditor) {
        const auto pressKey = [&](const char *identifier, LogicalKey key) {
          const auto handle =
              window->Tree().FindBySemanticIdentifier(identifier);
          if (!handle.IsValid() || !window->Focus(handle)) {
            return false;
          }
          for (auto state : {KeyState::Pressed, KeyState::Released}) {
            platformObserver->InjectEvent(KeyChanged{
                .window = window->PlatformHandle(),
                .logicalKey = static_cast<NGIN::UInt32>(key),
                .state = state,
            });
          }
          return application->PumpOnce().has_value();
        };
        const auto outputIs = [&](std::string_view expected) {
          const auto output =
              window->Tree().FindBySemanticIdentifier("property-Output level");
          const auto *node = window->Tree().Get(output);
          return node && node->type == ElementType::Text &&
                 node->properties.text.value.View() == expected &&
                 !node->properties.interaction.focusable;
        };
        // Output level is a getter-only property computed by the underlying
        // settings object.
        if (!Check(outputIs("50"),
                   "reflected output level starts at the object's default") ||
            !Check(pressKey("property-Volume", LogicalKey::Right) &&
                       outputIs("51"),
                   "editing the reflected volume changes the object's output "
                   "level") ||
            !Check(pressKey("property-Mute", LogicalKey::Space) &&
                       outputIs("0"),
                   "the reflected mute checkbox changes the object's output "
                   "level")) {
          return 1;
        }
      }
      if (page == NGIN::UIGallery::Page::Diagnostics) {
        const auto navigation = model.PageNavigationDiagnostics();
        if (!Check(activate("diagnostics-navigation"),
                   "open navigation diagnostics") ||
            !Check(navigation.region == "Gallery.Content",
                   "Gallery uses the public named navigation region") ||
            !Check(navigation.stack.size() == 1 &&
                       navigation.stack.back().pageId == "diagnostics",
                   "Gallery diagnostics reports the current registered page") ||
            !Check(model.PageReleaseCount() + 1 == model.PageActivationCount(),
                   "Gallery releases every replaced page") ||
            !Check(FindByTypeAndKey(window->Tree(), ElementType::Border,
                                    "navigation-diagnostics-card")
                       .IsValid(),
                   "Gallery shows page and ViewModel navigation diagnostics")) {
          return 1;
        }
      }
    }

    if (!Check(model.PageActivationCount() >= NGIN::UIGallery::PageCount,
               "Gallery reports page activations")) {
      return 1;
    }

    auto navigationSearch = model.NavigationSearchBinding();
    const auto accessibilityPageKey = std::to_string(
        static_cast<NGIN::UIntSize>(NGIN::UIGallery::Page::Accessibility));
    const auto searchField = FindByTypeAndKey(
        window->Tree(), ElementType::TextField, "navigation-search");
    if (!Check(searchField.IsValid() && window->Focus(searchField),
               "the sidebar search field accepts keyboard focus")) {
      return 1;
    }
    platformObserver->InjectEvent(
        TextInput{.window = window->PlatformHandle(),
                  .text = NGIN::Text::String{"narrator"}});
    if (!application->PumpOnce() ||
        !Check(window->FocusedElement() == searchField,
               "typing a filter keeps keyboard focus in the sidebar search "
               "field") ||
        !Check(FindByTypeAndKey(window->Tree(), ElementType::Button,
                                accessibilityPageKey)
                   .IsValid(),
               "navigation search finds pages by feature") ||
        !Check(!FindByTypeAndKey(window->Tree(), ElementType::Button, "0")
                    .IsValid(),
               "navigation search hides unrelated pages")) {
      return 1;
    }
    if (!navigationSearch.Set(NGIN::Text::String{"not-a-gallery-feature"}) ||
        !application->PumpOnce() ||
        !Check(FindByTypeAndKey(window->Tree(), ElementType::Text,
                                "navigation-no-results")
                   .IsValid(),
               "navigation search explains an empty result")) {
      return 1;
    }
    if (!Check(activate("clear-search") && navigationSearch.Get().Empty() &&
                   FindByTypeAndKey(window->Tree(), ElementType::Button, "0"),
               "clearing an empty search restores the library")) {
      return 1;
    }

    model.SelectPage(NGIN::UIGallery::Page::Motion);
    if (!Check(rendererObserver->RenderPackets().size() >=
                   NGIN::UIGallery::PageCount,
               "every gallery page rendered")) {
      return 1;
    }

    model.SelectPage(NGIN::UIGallery::Page::Layout);
    platformObserver->InjectEvent(
        WindowResized{window->PlatformHandle(), PixelSize{680, 520}});
    if (!application->PumpOnce()) {
      return 1;
    }
    const auto viewport =
        window->Tree().FindBySemanticIdentifier("catalogue-viewport");
    const auto *viewportNode = window->Tree().Get(viewport);
    if (!Check(model.IsCompact() && viewportNode &&
                   viewportNode->arrangedBounds.width >= 640,
               "compact layout gives the sample the width of the window"))
      return 1;

    if (!Check(activate("browse-samples") && model.BrowserOpen(),
               "keyboard opens the compact browser"))
      return 1;
    platformObserver->InjectEvent(
        KeyChanged{.window = window->PlatformHandle(),
                   .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Escape),
                   .state = KeyState::Pressed});
    if (!Check(application->PumpOnce() && !model.BrowserOpen() &&
                   model.CurrentPage() == Page::Layout,
               "Escape returns to the current sample") ||
        !Check(activate("browse-samples") &&
                   activate(std::to_string(
                       static_cast<NGIN::UIntSize>(Page::Collections))) &&
                   !model.BrowserOpen() &&
                   model.CurrentPage() == Page::Collections,
               "choosing a sample closes the compact browser"))
      return 1;
    platformObserver->InjectEvent(
        WindowResized{window->PlatformHandle(), PixelSize{1180, 760}});
    if (!application->PumpOnce()) {
      return 1;
    }

    const auto wasLight = model.IsLightTheme();
    model.ToggleTheme();
    if (!application->PumpOnce()) {
      return 1;
    }
    if (!Check(model.IsLightTheme() != wasLight,
               "theme switching is stateful")) {
      return 1;
    }

    model.SelectPage(NGIN::UIGallery::Page::Inputs);
    if (!application->PumpOnce()) {
      return 1;
    }
    const auto checkHandle = window->Tree().FindBySemanticIdentifier(
        NGIN::Text::String{"settings-check"});
    const auto *checkNode = window->Tree().Get(checkHandle);
    if (!Check(checkNode != nullptr,
               "checkbox has a stable semantic identity")) {
      return 1;
    }
    const auto checkCenter = Center(checkNode->arrangedBounds);
    const auto beforePointer = model.CheckBinding().Get();
    platformObserver->InjectEvent(PointerMoved{
        .window = window->PlatformHandle(),
        .pointerId = 1,
        .position = checkCenter,
    });
    platformObserver->InjectEvent(PointerButtonChanged{
        .window = window->PlatformHandle(),
        .pointerId = 1,
        .button = PointerButton::Primary,
        .state = ButtonState::Pressed,
        .position = checkCenter,
    });
    platformObserver->InjectEvent(PointerButtonChanged{
        .window = window->PlatformHandle(),
        .pointerId = 1,
        .button = PointerButton::Primary,
        .state = ButtonState::Released,
        .position = checkCenter,
    });
    if (!application->PumpOnce()) {
      return 1;
    }
    if (!Check(model.CheckBinding().Get() != beforePointer,
               "pointer activation changes a gallery control")) {
      return 1;
    }

    const auto beforeKeyboard = model.CheckBinding().Get();
    const auto keyboardCheckHandle = window->Tree().FindBySemanticIdentifier(
        NGIN::Text::String{"settings-check"});
    if (!Check(window->FocusedElement() == keyboardCheckHandle ||
                   window->Focus(keyboardCheckHandle),
               "keyboard target accepts focus")) {
      return 1;
    }
    platformObserver->InjectEvent(KeyChanged{
        .window = window->PlatformHandle(),
        .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Space),
        .state = KeyState::Pressed,
    });
    platformObserver->InjectEvent(KeyChanged{
        .window = window->PlatformHandle(),
        .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Space),
        .state = KeyState::Released,
    });
    if (!application->PumpOnce()) {
      return 1;
    }
    if (!Check(model.CheckBinding().Get() != beforeKeyboard,
               "keyboard activation changes a gallery control")) {
      return 1;
    }

    model.SelectPage(NGIN::UIGallery::Page::Motion);
    if (!application->PumpOnce())
      return 1;
    // Both ends of this interaction should be visible on the first frame of
    // a short window. The graph scales to available width during playback.
    for (const auto size : {PixelSize{680, 520}, PixelSize{480, 420}}) {
      platformObserver->InjectEvent(
          WindowResized{window->PlatformHandle(), size});
      if (!application->PumpOnce())
        return 1;
      const auto *compactStage = window->Tree().Get(FindByTypeAndKey(
          window->Tree(), ElementType::CustomElement, "motion-stage"));
      const auto *compactPlay = window->Tree().Get(
          FindByTypeAndKey(window->Tree(), ElementType::Button, "play-curve"));
      if (!Check(compactStage && compactPlay &&
                     compactStage->arrangedBounds.y >= 52 &&
                     compactStage->arrangedBounds.x >= 0 &&
                     compactStage->arrangedBounds.x +
                             compactStage->arrangedBounds.width <=
                         size.width &&
                     compactPlay->arrangedBounds.y +
                             compactPlay->arrangedBounds.height <=
                         size.height,
                 "the compact window shows both the timing plot and its "
                 "playback button"))
        return 1;
    }
    if (!Check(activate("play-curve") && window->HasActiveAnimations(),
               "playing a curve starts the visible preview"))
      return 1;
    platformObserver->InjectEvent(
        WindowResized{window->PlatformHandle(), PixelSize{1180, 760}});
    platformObserver->AdvanceTime(std::chrono::milliseconds{120});
    if (!application->PumpOnce() || !activate("play-curve") ||
        !Check(window->HasActiveAnimations(),
               "playback can reverse while running"))
      return 1;

    if (!activate("curve-keyframes") || !activate("curve-custom") ||
        !Check(HasRole(window->Semantics(), SemanticRole::Slider),
               "choosing the custom curve exposes editable handles") ||
        !activate("study-loop") || !activate("start-repeat") ||
        !Check(model.MotionRepeatRunning() && model.MotionRepeatHandle(),
               "the loop has a live cancellation handle"))
      return 1;
    if (!activate("stop-repeat") ||
        !Check(!model.MotionRepeatRunning(),
               "the loop can be stopped locally") ||
        !activate("start-repeat") || !activate("study-choreography") ||
        !Check(!model.MotionRepeatRunning(),
               "switching studies retires the old loop"))
      return 1;

    const auto initialMotionStatus = model.AsyncMotionStatus();
    if (!activate("slow") || !application->PumpOnce())
      return 1;
    const auto *motionStatus = window->Tree().Get(
        FindByTypeAndKey(window->Tree(), ElementType::Text, "async-status"));
    if (!Check(motionStatus &&
                   model.AsyncMotionStatus() != initialMotionStatus &&
                   motionStatus->properties.text.value ==
                       model.AsyncMotionStatus(),
               "async playback publishes its status beside the tracks"))
      return 1;
    if (!activate("cancel") || !activate("study-feedback"))
      return 1;
    const auto *progress = window->Tree().Get(FindByTypeAndKey(
        window->Tree(), ElementType::CustomElement, "motion-progress"));
    if (!Check(progress && progress->properties.motion.value.has_value(),
               "the feedback study contains an animated loading control") ||
        !activate("reduced-motion") ||
        !Check(model.MotionPreviewReduced() && !window->HasActiveAnimations(),
               "reduced motion settles the active study") ||
        !activate("reduced-motion") ||
        !Check(!model.MotionPreviewReduced(), "full motion can be restored"))
      return 1;

    model.MotionPopup().Open();
    if (!application->PumpOnce())
      return 1;
    const auto motionPopup =
        FindByTypeAndKey(window->Tree(), ElementType::Popup, "popup");
    const auto *motionPopupNode = window->Tree().Get(motionPopup);
    const auto popupCard =
        FindByTypeAndKey(window->Tree(), ElementType::Column, "popup-card");
    if (!Check(motionPopupNode != nullptr,
               "motion example opens a retained popup") ||
        !Check(!motionPopupNode->properties.visual.base.background.has_value(),
               "popup viewport does not cover the Gallery") ||
        !Check(motionPopupNode->properties.motion.opacity.has_value() &&
                   motionPopupNode->properties.motion.translation.has_value() &&
                   motionPopupNode->properties.motion.translation->Initial() ==
                       std::optional<Point>{Point{0.0F, 36.0F}} &&
                   std::holds_alternative<TweenTiming>(
                       motionPopupNode->properties.motion.translation->Spec()
                           .timing) &&
                   std::get<TweenTiming>(
                       motionPopupNode->properties.motion.translation->Spec()
                           .timing)
                           .duration >= std::chrono::milliseconds{500},
               "popup demo uses an obvious rise and fade") ||
        !Check(popupCard.IsValid(), "popup presents a visible card") ||
        !Check(motionPopupNode->popup.contentBounds.width <= 320.0F &&
                   motionPopupNode->popup.contentBounds.height <= 170.0F,
               "popup content stays a small anchored panel")) {
      return 1;
    }
    model.MotionPopup().Close();
    if (!application->PumpOnce()) {
      return 1;
    }
    platformObserver->AdvanceTime(std::chrono::milliseconds{520});
    if (!application->PumpOnce() || !application->PumpOnce()) {
      return 1;
    }
    model.SelectPage(NGIN::UIGallery::Page::Inputs);
    if (!application->PumpOnce()) {
      return 1;
    }

    if (!Check(activate("inputs-text"), "open the text editing sample"))
      return 1;

    const auto editor =
        FindByTypeAndKey(window->Tree(), ElementType::TextField, "editable");
    if (!Check(editor.IsValid() && window->Focus(editor),
               "editable field accepts focus")) {
      return 1;
    }
    if (!platformObserver->SetClipboardText(NGIN::Text::String{" clipboard"})
             .has_value()) {
      return 1;
    }
    platformObserver->InjectEvent(KeyChanged{
        .window = window->PlatformHandle(),
        .logicalKey = static_cast<NGIN::UInt32>('V'),
        .state = KeyState::Pressed,
        .modifiers = static_cast<NGIN::UInt32>(KeyModifierFlags::Control),
    });
    if (!application->PumpOnce()) {
      return 1;
    }
    if (!Check(model.Name().View().find("clipboard") != std::string_view::npos,
               "clipboard paste commits through the focused binding")) {
      return 1;
    }
    const auto beforeComposition = model.Name();
    platformObserver->InjectEvent(TextComposition{
        .window = window->PlatformHandle(),
        .text = NGIN::Text::String{"\xC3\xA5"},
        .selectionStart = 0,
        .selectionLength = 2,
    });
    if (!application->PumpOnce()) {
      return 1;
    }
    if (!Check(model.Name() == beforeComposition,
               "IME pre-edit remains transient")) {
      return 1;
    }
    platformObserver->InjectEvent(TextInput{
        .window = window->PlatformHandle(),
        .text = NGIN::Text::String{"\xC3\xA5"},
    });
    if (!application->PumpOnce()) {
      return 1;
    }
    if (!Check(model.Name() != beforeComposition,
               "IME commit updates the focused binding")) {
      return 1;
    }

    auto checkChanged = model.CheckBinding().Set(CheckState::Indeterminate);
    auto toggleChanged = model.ToggleBinding().Set(false);
    auto sliderChanged = model.SliderBinding().Set(0.8F);
    auto radioChanged =
        model.DensityBinding().Set(NGIN::UIGallery::Density::Spacious);
    if (!Check(checkChanged.has_value(), "checkbox binding is writable") ||
        !Check(toggleChanged.has_value(), "toggle binding is writable") ||
        !Check(sliderChanged.has_value(), "slider binding is writable") ||
        !Check(radioChanged.has_value(), "typed radio binding is writable") ||
        !Check(model.SliderValue() == 0.8F,
               "range state is retained by the gallery model")) {
      return 1;
    }

    model.SelectPage(NGIN::UIGallery::Page::Overlays);
    model.SetPopupOpen(true);
    if (!application->PumpOnce()) {
      return 1;
    }
    if (!Check(model.IsPopupOpen(), "popup state is controllable") ||
        !Check(window->Diagnostics().semanticNodeCount > 0,
               "popup frame updates diagnostics")) {
      return 1;
    }
    platformObserver->InjectEvent(KeyChanged{
        .window = window->PlatformHandle(),
        .logicalKey = static_cast<NGIN::UInt32>(LogicalKey::Escape),
        .state = KeyState::Pressed,
    });
    if (!application->PumpOnce() ||
        !Check(!model.IsPopupOpen(), "Escape dismisses the popup")) {
      return 1;
    }

    model.ToggleInspector();
    if (!Check(model.IsInspectorEnabled(), "inspector state is retained") ||
        !Check(window->InspectorOverlay().enabled,
               "inspector overlay follows the model")) {
      return 1;
    }

    auto openedWindow = model.OpenAuxiliaryWindow(false);
    if (!openedWindow) {
      return Report("Auxiliary window creation failed", openedWindow.error());
    }
    auto openedDialog = model.OpenAuxiliaryWindow(true);
    if (!openedDialog) {
      return Report("Dialog creation failed", openedDialog.error());
    }
    auto pumpedWindows = application->PumpOnce();
    if (!pumpedWindows) {
      return Report("Multiple-window frame failed", pumpedWindows.error());
    }
    if (!Check(application->ActiveWindowCount() == 3,
               "gallery creates independent and modal windows") ||
        !Check(window->ActiveModalDialog() != nullptr,
               "modal ownership is established")) {
      return 1;
    }

    const auto dialogRecord = std::find_if(
        platformObserver->Windows().begin(), platformObserver->Windows().end(),
        [](const TestWindowRecord &record) {
          return !record.destroyed && record.info.kind == WindowKind::Dialog;
        });
    if (!Check(dialogRecord != platformObserver->Windows().end(),
               "test platform records the modal dialog")) {
      return 1;
    }
    platformObserver->InjectEvent(WindowCloseRequested{dialogRecord->handle});
    if (!application->PumpOnce()) {
      return 1;
    }
    if (!Check(application->ActiveWindowCount() == 2,
               "dialog close updates multiple-window ownership") ||
        !Check(window->ActiveModalDialog() == nullptr,
               "dialog close restores the owner modal state")) {
      return 1;
    }

    return 0;
  };
  const auto status = checkGallery();
  auto shutdown = application->ShutdownTasks();
  if (!shutdown)
    return Report("Gallery shutdown failed", shutdown.error());
  if (status != 0)
    return status;
  std::cout << "NGIN.UI gallery headless checks passed\n";
  return 0;
}
