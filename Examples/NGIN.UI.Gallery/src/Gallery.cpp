#include "GalleryPropertyEditor.hpp"
#include "GalleryViews.hpp"

#include <NGIN/Async/WhenAll.hpp>
#include <NGIN/Units.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace NGIN::UIGallery {
class GalleryVirtualizedSource final
    : public UI::IVirtualizedDataSource<NGIN::UIntSize> {
public:
  [[nodiscard]] auto Count() const noexcept -> NGIN::UIntSize override {
    return BaseItemCount + m_insertedIds.size();
  }
  [[nodiscard]] auto Revision() const noexcept -> NGIN::UInt64 override {
    return m_revision.Get();
  }
  [[nodiscard]] auto ItemAt(const NGIN::UIntSize index) const
      -> UI::UIResult<NGIN::UIntSize> override {
    if (index >= Count() || !m_loadedRange || index < m_loadedRange->first ||
        index >= m_loadedRange->End()) {
      return std::unexpected(UI::MakeUIError(
          UI::UIErrorCode::ResourceFailed,
          "The Gallery row has not been loaded", "NGIN.UI.Gallery",
          "GalleryVirtualizedSource::ItemAt"));
    }
    return LogicalValue(index);
  }
  auto RequestRange(const UI::IncrementalRange range)
      -> UI::UIResult<void> override {
    if (range.first > Count() || range.count > Count() - range.first) {
      return std::unexpected(UI::MakeUIError(
          UI::UIErrorCode::InvalidArgument,
          "The requested Gallery range is out of bounds", "NGIN.UI.Gallery",
          "GalleryVirtualizedSource::RequestRange"));
    }
    m_loadedRange = range;
    return {};
  }
  void CancelRange(const UI::IncrementalRange) noexcept override {}
  [[nodiscard]] auto KeyAt(const NGIN::UIntSize index) const
      -> UI::UIResult<Text::String> override {
    if (index >= Count()) {
      return std::unexpected(UI::MakeUIError(
          UI::UIErrorCode::InvalidArgument, "The Gallery key is out of bounds",
          "NGIN.UI.Gallery", "GalleryVirtualizedSource::KeyAt"));
    }
    const auto value = LogicalValue(index);
    const auto key =
        std::string{value >= NewItemBase ? "new-" : "item-"} +
        std::to_string(value >= NewItemBase ? value - NewItemBase : value);
    return Text::String{key.c_str()};
  }
  [[nodiscard]] auto LabelAt(const NGIN::UIntSize index) const
      -> UI::UIResult<Text::String> override {
    if (index >= Count()) {
      return std::unexpected(UI::MakeUIError(
          UI::UIErrorCode::InvalidArgument,
          "The Gallery label is out of bounds", "NGIN.UI.Gallery",
          "GalleryVirtualizedSource::LabelAt"));
    }
    const auto value = LogicalValue(index);
    const auto label =
        std::string{value >= NewItemBase ? "New item " : "Item "} +
        std::to_string(value >= NewItemBase ? value - NewItemBase + 1
                                            : value + 1);
    return Text::String{label.c_str()};
  }
  [[nodiscard]] auto IndexOfKey(const Text::String &key) const
      -> std::optional<NGIN::UIntSize> override {
    static_cast<void>(Revision());
    const auto view = key.View();
    const auto prefix = view.starts_with("item-")  ? std::string_view{"item-"}
                        : view.starts_with("new-") ? std::string_view{"new-"}
                                                   : std::string_view{};
    if (prefix.empty()) {
      return std::nullopt;
    }
    NGIN::UIntSize value = 0;
    const auto digits = view.substr(prefix.size());
    const auto parsed =
        std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != digits.data() + digits.size()) {
      return std::nullopt;
    }
    if (prefix == "new-") {
      const auto found = std::ranges::find(m_insertedIds, value);
      return found == m_insertedIds.end()
                 ? std::nullopt
                 : std::optional<NGIN::UIntSize>{static_cast<NGIN::UIntSize>(
                       std::distance(m_insertedIds.begin(), found))};
    }
    const auto index = value + m_insertedIds.size();
    return value < BaseItemCount ? std::optional<NGIN::UIntSize>{index}
                                 : std::nullopt;
  }

  void Prepend(const NGIN::UIntSize count) {
    std::vector<NGIN::UIntSize> inserted;
    inserted.reserve(count);
    for (NGIN::UIntSize index = 0; index < count; ++index) {
      inserted.push_back(m_nextInsertedId++);
    }
    m_insertedIds.insert(m_insertedIds.begin(), inserted.begin(),
                         inserted.end());
    static_cast<void>(m_revision.Update([](NGIN::UInt64 &value) { ++value; }));
  }

private:
  [[nodiscard]] auto LogicalValue(const NGIN::UIntSize index) const noexcept
      -> NGIN::UIntSize {
    return index < m_insertedIds.size() ? NewItemBase + m_insertedIds[index]
                                        : index - m_insertedIds.size();
  }

  static constexpr NGIN::UIntSize BaseItemCount{100'000};
  static constexpr NGIN::UIntSize NewItemBase{1'000'000};
  std::vector<NGIN::UIntSize> m_insertedIds{};
  NGIN::UIntSize m_nextInsertedId{0};
  UI::State<NGIN::UInt64> m_revision{1};
  std::optional<UI::IncrementalRange> m_loadedRange{};
};

class GalleryMotionDemo final {
public:
  explicit GalleryMotionDemo(UI::Application &application)
      : context(std::make_unique<NGIN::Async::TaskContext>(
            application.CreateTaskContext())),
        application(&application) {}

  void SetStatus(const char *message) {
    static_cast<void>(status.Set(NGIN::Text::String{message}));
  }

  std::unique_ptr<NGIN::Async::TaskContext> context{};
  UI::Application *application{nullptr};
  UI::MotionController primary{};
  UI::MotionController secondary{};
  UI::State<NGIN::Text::String> status{
      "Ready to play"};
  bool parallelForward{true};
  bool interruptForward{true};
  std::shared_ptr<NGIN::Async::CancellationSource> cancellation{};
};

class GalleryAsyncViewModel final
    : public std::enable_shared_from_this<GalleryAsyncViewModel> {
public:
  GalleryAsyncViewModel(Text::String key, UI::InvalidationScheduler scheduler)
      : m_key(std::move(key)), m_presentation(scheduler),
        m_retry([this] { BeginLoad(); }, false, scheduler),
        m_cancel([this] { CancelLoad(); }, false, std::move(scheduler)) {
    m_presentation.SetRetryCommand(m_retry.AsBinding());
    m_presentation.SetCancelCommand(m_cancel.AsBinding());
  }

  void Activate(UI::ViewModelTaskScope &scope) noexcept {
    m_scope = &scope;
    BeginLoad();
  }

  void Deactivate() noexcept {
    ++m_revision;
    m_load.Cancel();
    m_scope = nullptr;
  }

  [[nodiscard]] auto Key() const noexcept -> const Text::String & {
    return m_key;
  }
  [[nodiscard]] auto Presentation() const noexcept
      -> const UI::AsyncPresentation<std::vector<Text::String>> & {
    return m_presentation;
  }

private:
  void BeginLoad() {
    if (m_scope == nullptr) {
      return;
    }
    m_load.Cancel();
    const auto revision = ++m_revision;
    m_presentation.SetLoading();
    m_retry.SetEnabled(false);
    m_cancel.SetEnabled(true);
    const auto retained = shared_from_this();
    m_load = m_scope->Start(
        [retained, revision](NGIN::Async::TaskContext &context) {
          return retained->LoadAsync(context, revision);
        },
        [retained, revision](const UI::ViewModelTaskOutcome &outcome) {
          retained->LoadFinished(revision, outcome);
        });
  }

  void CancelLoad() {
    ++m_revision;
    m_load.Cancel();
    m_cancel.SetEnabled(false);
    m_retry.SetEnabled(true);
    m_presentation.SetIdle();
  }

  [[nodiscard]] auto LoadAsync(NGIN::Async::TaskContext &context,
                               const NGIN::UInt64 revision)
      -> UI::ViewModelTaskScope::Task {
    co_await UI::ViewModelTaskScope::Task::Delay(
        context, NGIN::Units::Milliseconds{650.0});
    if (revision != m_revision) {
      co_return;
    }
    if (m_key.View() == "broken") {
      co_await NGIN::Async::DomainFailure(UI::CommandError{
          .kind = UI::CommandErrorKind::Domain,
          .code = Text::String{"gallery-load-failed"},
          .message = Text::String{"The sample could not be loaded"},
      });
    }
    if (m_key.View() == "empty") {
      m_presentation.SetEmpty();
      co_return;
    }
    m_presentation.SetContent({Text::String{"Welcome back"},
                               Text::String{"Review design"},
                               Text::String{"Ship the update"}});
  }

  void LoadFinished(const NGIN::UInt64 revision,
                    const UI::ViewModelTaskOutcome &outcome) {
    if (revision != m_revision) {
      return;
    }
    m_cancel.SetEnabled(false);
    m_retry.SetEnabled(true);
    if ((outcome.kind == UI::ViewModelTaskOutcomeKind::DomainError ||
         outcome.kind == UI::ViewModelTaskOutcomeKind::Fault) &&
        outcome.error) {
      m_presentation.SetError(*outcome.error);
    } else if (outcome.kind == UI::ViewModelTaskOutcomeKind::Canceled &&
               m_presentation.Get().kind ==
                   UI::AsyncPresentationKind::Loading) {
      m_presentation.SetIdle();
    }
  }

  Text::String m_key{};
  UI::AsyncPresentation<std::vector<Text::String>> m_presentation;
  UI::Command m_retry;
  UI::Command m_cancel;
  UI::ViewModelTaskScope *m_scope{nullptr};
  UI::ViewModelTaskHandle m_load{};
  NGIN::UInt64 m_revision{0};
};

class GalleryPageActivationContext final : public UI::PageActivationContext {};

namespace {
using NGIN::F32;
using NGIN::Text::String;
using namespace NGIN::UI;
using namespace Pages;

struct OverviewPage final {};
struct LayoutPage final {};
struct TypographyPage final {};
struct TextAreaPage final {};
struct ImagesPage final {};
struct InputsPage final {};
struct AsyncDataPage final {};
struct CollectionsPage final {};
struct MotionPage final {};
struct OverlaysPage final {};
struct WindowsPage final {};
struct ResourcesPage final {};
struct AccessibilityPage final {};
struct DiagnosticsPage final {};

struct GalleryPageViewModel final {
  GalleryViewModel *gallery{nullptr};
};

using GalleryNavigate =
    UIResult<NavigationChange> (*)(NavigationService &navigation);

template <typename TPage>
auto StartGalleryPage(NavigationService &navigation)
    -> UIResult<NavigationChange> {
  return navigation.Start<TPage>();
}

template <typename TPage>
auto ReplaceGalleryPage(NavigationService &navigation)
    -> UIResult<NavigationChange> {
  return navigation.Replace<TPage>();
}

constexpr std::array<GalleryNavigate, PageCount> GalleryPageStarters{
    &StartGalleryPage<OverviewPage>,       &StartGalleryPage<LayoutPage>,
    &StartGalleryPage<TypographyPage>,     &StartGalleryPage<TextAreaPage>,
    &StartGalleryPage<ImagesPage>,         &StartGalleryPage<InputsPage>,
    &StartGalleryPage<AsyncDataPage>,      &StartGalleryPage<CollectionsPage>,
    &StartGalleryPage<MotionPage>,         &StartGalleryPage<OverlaysPage>,
    &StartGalleryPage<WindowsPage>,        &StartGalleryPage<ResourcesPage>,
    &StartGalleryPage<AccessibilityPage>,  &StartGalleryPage<DiagnosticsPage>,
    &StartGalleryPage<PropertyEditorPage>,
};

constexpr std::array<GalleryNavigate, PageCount> GalleryPageReplacements{
    &ReplaceGalleryPage<OverviewPage>,
    &ReplaceGalleryPage<LayoutPage>,
    &ReplaceGalleryPage<TypographyPage>,
    &ReplaceGalleryPage<TextAreaPage>,
    &ReplaceGalleryPage<ImagesPage>,
    &ReplaceGalleryPage<InputsPage>,
    &ReplaceGalleryPage<AsyncDataPage>,
    &ReplaceGalleryPage<CollectionsPage>,
    &ReplaceGalleryPage<MotionPage>,
    &ReplaceGalleryPage<OverlaysPage>,
    &ReplaceGalleryPage<WindowsPage>,
    &ReplaceGalleryPage<ResourcesPage>,
    &ReplaceGalleryPage<AccessibilityPage>,
    &ReplaceGalleryPage<DiagnosticsPage>,
    &ReplaceGalleryPage<PropertyEditorPage>,
};

[[nodiscard]] auto OutcomeName(const MotionOutcome outcome) noexcept -> const
    char * {
  switch (outcome) {
  case MotionOutcome::Completed:
    return "Completed";
  case MotionOutcome::Canceled:
    return "Canceled";
  case MotionOutcome::Interrupted:
    return "Interrupted by a newer target";
  case MotionOutcome::Unmounted:
    return "Stopped because the element was removed";
  }
  return "Stopped";
}

[[nodiscard]] auto ValidateDisplayName(NGIN::Async::TaskContext &context,
                                       String value)
    -> NGIN::Async::Task<std::vector<ValidationIssue>, ValidationIssue> {
  co_await context.YieldNow();
  co_await context.YieldNow();
  if (value.View() == "admin" || value.View() == "root") {
    co_return std::vector{ValidationIssue{
        .id = String{"display-name-reserved"},
        .field = String{"Display name"},
        .message = String{"That name is reserved"},
        .severity = ValidationSeverity::Error,
    }};
  }
  co_return std::vector<ValidationIssue>{};
}

[[nodiscard]] auto
FirstValidationMessage(const std::unique_ptr<ValidationField<String>> &field,
                       const char *emptyMessage) -> String {
  if (!field) {
    return String{emptyMessage};
  }
  if (field->IsValidating().Get()) {
    return String{"Checking..."};
  }
  const auto &issues = field->Issues().Get();
  return issues.empty() ? String{emptyMessage} : issues.front().message;
}

[[nodiscard]] auto SequenceMotion(NGIN::Async::TaskContext &context,
                                  std::shared_ptr<GalleryMotionDemo> demo)
    -> NGIN::Async::Task<void> {
  const auto spec =
      AnimationSpec{.timing = TweenTiming{
                        .duration = std::chrono::milliseconds{520},
                        .curve = EasingCurve::EaseInOut(),
                    }};
  demo->SetStatus("Moving the blue marker");
  auto outcome = co_await demo->primary.AnimateToAsync(
      context, MotionProperty::Value, 1.0F, spec);
  if (outcome != MotionOutcome::Completed) {
    demo->SetStatus(OutcomeName(outcome));
    co_return;
  }
  demo->SetStatus("Fading the blue track");
  outcome = co_await demo->primary.FadeToAsync(context, 0.25F, spec);
  if (outcome != MotionOutcome::Completed) {
    demo->SetStatus(OutcomeName(outcome));
    co_return;
  }
  demo->SetStatus("Returning to the start");
  outcome = co_await demo->primary.AnimateToAsync(
      context, MotionProperty::Value, 0.0F, spec);
  if (outcome == MotionOutcome::Completed) {
    outcome = co_await demo->primary.FadeToAsync(context, 1.0F, spec);
  }
  demo->SetStatus(OutcomeName(outcome));
}

[[nodiscard]] auto ParallelMotion(NGIN::Async::TaskContext &context,
                                  std::shared_ptr<GalleryMotionDemo> demo)
    -> NGIN::Async::Task<void> {
  const auto target = demo->parallelForward ? 1.0F : 0.0F;
  demo->parallelForward = !demo->parallelForward;
  const auto spec = AnimationSpec{.timing = SpringTiming{
                                      .stiffness = 150.0F,
                                      .damping = 15.0F,
                                  }};
  demo->SetStatus("Both tracks are moving");
  const auto outcomes = co_await NGIN::Async::WhenAll(
      context,
      demo->primary.AnimateToAsync(context, MotionProperty::Value, target,
                                   spec),
      demo->secondary.AnimateToAsync(context, MotionProperty::Value, target,
                                     spec));
  const auto completed = std::get<0>(outcomes) == MotionOutcome::Completed &&
                         std::get<1>(outcomes) == MotionOutcome::Completed;
  demo->SetStatus(completed ? "Both tracks completed" : "Playback interrupted");
}

struct GalleryCancelableRun final {
  GalleryCancelableRun(Application &application,
                       const NGIN::Async::CancellationToken token)
      : context(application.CreateTaskContext(token)) {}

  NGIN::Async::TaskContext context;
};

[[nodiscard]] auto CancelableMotion(std::shared_ptr<GalleryCancelableRun> run,
                                    std::shared_ptr<GalleryMotionDemo> demo)
    -> NGIN::Async::Task<void> {
  demo->SetStatus("Moving slowly");
  const AnimationSpec timing{.timing = TweenTiming{
                                 .duration = std::chrono::milliseconds{1200},
                                 .curve = EasingCurve::Linear(),
                             }};
  auto outcome = co_await demo->primary.AnimateToAsync(
      run->context, MotionProperty::Value, 1.0F, timing);
  if (outcome == MotionOutcome::Completed)
    outcome = co_await demo->primary.AnimateToAsync(
        run->context, MotionProperty::Value, 0.0F, timing);
  demo->SetStatus(OutcomeName(outcome));
}

[[nodiscard]] auto InterruptMotion(NGIN::Async::TaskContext &context,
                                   std::shared_ptr<GalleryMotionDemo> demo,
                                   const F32 target)
    -> NGIN::Async::Task<void> {
  demo->SetStatus("Orange target changed");
  const auto outcome = co_await demo->secondary.AnimateToAsync(
      context, MotionProperty::Value, target,
      AnimationSpec{.timing = TweenTiming{
                        .duration = std::chrono::milliseconds{1500},
                        .curve = EasingCurve::EaseInOut(),
                    }});
  demo->SetStatus(OutcomeName(outcome));
}

constexpr std::array<std::string_view, PageCount> PageNames{
    "Overview", "Layout",     "Typography",    "Text Area",   "Images",
    "Inputs",   "Async Data", "Collections",   "Motion",      "Overlays",
    "Windows",  "Themes",     "Accessibility", "Diagnostics", "Property Editor",
};

constexpr std::array<std::string_view, PageCount> PageIds{
    "overview", "layout",     "typography",    "text-area",   "images",
    "inputs",   "async-data", "collections",   "motion",      "overlays",
    "windows",  "themes",     "accessibility", "diagnostics", "property-editor",
};

constexpr std::array<std::string_view, PageCount> PageSearchTerms{
    "sample library home start custom controls status badge progress chart",
    "responsive layout grid tracks columns wrap flow canvas coordinates",
    "fonts type scale unicode arabic greek cyrillic language proofs",
    "text editor project notes multiline selection copy paste",
    "image fitting artwork picture contain cover stretch crop",
    "form controls playlist preferences volume slider checkbox radio switch text entry validation password",
    "async loading inbox empty offline error retry switch rapidly cancellation",
    "virtual list selection add item context menu tabs collections",
    "motion studio animation timing curves easing choreography loops playback keyframes bezier reduced motion",
    "overlays popup track details dismiss",
    "independent window modal dialog desktop",
    "themes resources palette colors swatches light dark",
    "accessibility narrator screen reader keyboard semantics live announcement",
    "diagnostics frame counters inspector guides navigation text glyph images cache",
    "property editor reflection playback settings mute volume output level",
};

[[nodiscard]] auto ContainsIgnoringCase(const std::string_view text,
                                        const std::string_view query) -> bool {
  if (query.empty()) {
    return true;
  }
  return std::ranges::search(
             text, query,
             [](const char left, const char right) {
               return std::tolower(static_cast<unsigned char>(left)) ==
                      std::tolower(static_cast<unsigned char>(right));
             })
             .begin() != text.end();
}

template <typename TPage, typename Build>
auto RegisterGalleryPage(PageRegistry &registry, GalleryViewModel &model,
                         Page page, Build build) -> UIResult<void> {
  const auto index = static_cast<NGIN::UIntSize>(page);
  return registry.Register<TPage, GalleryPageViewModel>(
      {.id = std::string{PageIds[index]},
       .displayName = std::string{PageNames[index]},
       .routeName = std::string{PageIds[index]}},
      [&model](PageActivationContext &, const NoNavigationParameter &,
               std::string_view) {
        return UIResult<PageLease<GalleryPageViewModel>>{
            PageLease<GalleryPageViewModel>{
                .viewModel = std::make_shared<GalleryPageViewModel>(
                    GalleryPageViewModel{&model})}};
      },
      [build](GalleryPageViewModel &pageModel, const NoNavigationParameter &) {
        auto &gallery = *pageModel.gallery;
        static_cast<void>(gallery.PageRevision());
        if constexpr (std::is_invocable_v<Build, GalleryViewModel &,
                                          const Theme &>)
          return build(gallery, gallery.CurrentTheme());
        else
          return build(gallery.CurrentTheme());
      });
}

auto RegisterGalleryPages(PageRegistry &registry, GalleryViewModel &model)
    -> UIResult<void> {
  if (auto added = RegisterGalleryPage<OverviewPage>(
          registry, model, Page::Overview, OverviewView);
      !added)
    return added;
  if (auto added = RegisterGalleryPage<LayoutPage>(registry, model,
                                                   Page::Layout, LayoutView);
      !added)
    return added;
  if (auto added = RegisterGalleryPage<TypographyPage>(
          registry, model, Page::Typography, TypographyView);
      !added)
    return added;
  if (auto added = RegisterGalleryPage<TextAreaPage>(
          registry, model, Page::TextArea, TextAreaView);
      !added)
    return added;
  if (auto added = RegisterGalleryPage<ImagesPage>(registry, model,
                                                   Page::Images, ImagesView);
      !added)
    return added;
  if (auto added = RegisterGalleryPage<InputsPage>(registry, model,
                                                   Page::Inputs, InputsView);
      !added)
    return added;
  if (auto added = RegisterGalleryPage<AsyncDataPage>(
          registry, model, Page::AsyncData, AsyncDataView);
      !added)
    return added;
  if (auto added = RegisterGalleryPage<CollectionsPage>(
          registry, model, Page::Collections, CollectionsView);
      !added)
    return added;
  if (auto added = RegisterGalleryPage<MotionPage>(registry, model,
                                                   Page::Motion, MotionView);
      !added)
    return added;
  if (auto added = RegisterGalleryPage<OverlaysPage>(
          registry, model, Page::Overlays, OverlaysView);
      !added)
    return added;
  if (auto added = RegisterGalleryPage<WindowsPage>(registry, model,
                                                    Page::Windows, WindowsView);
      !added)
    return added;
  if (auto added = RegisterGalleryPage<ResourcesPage>(
          registry, model, Page::Resources, ResourcesView);
      !added)
    return added;
  if (auto added = RegisterGalleryPage<AccessibilityPage>(
          registry, model, Page::Accessibility, AccessibilityView);
      !added)
    return added;
  if (auto added = RegisterGalleryPage<DiagnosticsPage>(
          registry, model, Page::Diagnostics, DiagnosticsView);
      !added)
    return added;
  const auto index = static_cast<NGIN::UIntSize>(Page::PropertyEditor);
  return RegisterPropertyEditor(registry,
                                {.id = std::string{PageIds[index]},
                                 .displayName = std::string{PageNames[index]},
                                 .routeName = std::string{PageIds[index]}},
                                model);
}

auto AuxiliaryWindowView(GalleryViewModel &model, bool modal) -> Views::View {
  return Views::Component([&model, modal] {
    const auto theme = model.CurrentTheme();
    NodeProperties root{};
    root.layout.padding = Thickness::Uniform(Dp{theme.spacing.spacious});
    root.layout.gap = theme.spacing.spacious;
    root.visual.base.background = theme.colors.background;
    root.semantics.role = modal ? SemanticRole::Dialog : SemanticRole::Group;
    root.semantics.label =
        String{modal ? "Gallery dialog" : "Gallery second window"};
    return Views::Column(
               {
                   TextView(modal ? "Dialog" : "Second window", 26.0F,
                            theme.colors.foreground, "title",
                            SemanticRole::Heading),
                   TextView(modal
                                ? "The main window is paused until this closes."
                                : "This is another NGIN.UI window.",
                            theme.typography.body, theme.colors.mutedForeground,
                            "body"),
                   TextView("Use the window close button when you are done.",
                            theme.typography.caption,
                            theme.colors.mutedForeground, "close-hint"),
               },
               root)
        .Key("auxiliary-root");
  });
}

} // namespace

auto PageAt(const NGIN::UIntSize index) noexcept -> Page {
  return static_cast<Page>(index % PageCount);
}

auto PageName(const Page page) noexcept -> std::string_view {
  return PageNames[static_cast<NGIN::UIntSize>(page)];
}

GalleryViewModel::GalleryViewModel()
    : m_page(Page::Overview), m_navigationSearch(String{}), m_lightTheme(true),
      m_name(String{"NGIN"}), m_password(String{"retained"}),
      m_notes(String{"Write your project notes here.\n\n"
                     "Long lines wrap automatically, and the editor scrolls as "
                     "you add more text.\n"
                     "Add a few lines and try moving with the arrow keys."}),
      m_check(CheckState::Checked), m_mixedCheck(CheckState::Indeterminate),
      m_unchecked(CheckState::Unchecked), m_density(Density::Comfortable),
      m_toggle(true), m_disabledToggle(false), m_slider(0.62F),
      m_formSaveCount(0),
      m_collectionItems(std::vector<std::uint32_t>{
          101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112}),
      m_collectionSelection(std::optional<std::uint32_t>{103}),
      m_collectionDescending(false), m_collectionFiltered(false),
      m_collectionTab(CollectionTab::Selection), m_motionForward(false),
      m_bezierX1(0.2F), m_bezierY1(0.0F), m_bezierX2(0.2F), m_bezierY2(1.0F),
      m_motionRepeatRunning(false), m_virtualizedSelection(String{"item-0"}),
      m_virtualizedSource(std::make_unique<GalleryVirtualizedSource>()),
      m_virtualizedController(std::make_unique<FixedVirtualizedListController>(
          FixedVirtualizationOptions{
              .itemExtent = 38.0F,
              .itemGap = 2.0F,
              .overscanItems = 3,
              .initialViewportExtent = 280.0F,
          })),
      m_motionPopup([this](const InvalidationKind kind) { Invalidate(kind); }),
      m_comboPopup([this](const InvalidationKind kind) { Invalidate(kind); }),
      m_menuPopup([this](const InvalidationKind kind) { Invalidate(kind); }),
      m_contextPopup([this](const InvalidationKind kind) { Invalidate(kind); }),
      m_popupOpen(false), m_inspectorEnabled(false), m_status(String{}),
      m_accessibilityAnnouncement(String{"No announcement yet."}) {
  const auto scheduler = [this](const InvalidationKind kind) {
    Invalidate(kind);
  };
  m_nameValidation = std::make_unique<ValidationField<String>>(
      m_name.AsReadOnly(), ValidationTrigger::Immediate, scheduler);
  m_nameValidation->AddSyncValidator([](const String &value) {
    if (value.Empty()) {
      return std::vector{ValidationIssue{
          .id = String{"display-name-required"},
          .field = String{"Display name"},
          .message = String{"Enter a display name"},
      }};
    }
    return std::vector<ValidationIssue>{};
  });

  m_passwordValidation = std::make_unique<ValidationField<String>>(
      m_password.AsReadOnly(), ValidationTrigger::Deferred, scheduler);
  m_passwordValidation->AddSyncValidator([](const String &value) {
    if (value.Size() < 8) {
      return std::vector{ValidationIssue{
          .id = String{"password-length"},
          .field = String{"Password"},
          .message = String{"Use at least 8 characters"},
      }};
    }
    return std::vector<ValidationIssue>{};
  });

  m_notesValidation = std::make_unique<ValidationField<String>>(
      m_notes.AsReadOnly(), ValidationTrigger::Submit, scheduler);
  m_notesValidation->AddSyncValidator([](const String &value) {
    if (value.Empty()) {
      return std::vector{ValidationIssue{
          .id = String{"notes-required"},
          .field = String{"Notes"},
          .message = String{"Add a short note"},
      }};
    }
    return std::vector<ValidationIssue>{};
  });

  m_validationForm = std::make_unique<ValidationForm>(std::vector{
      m_nameValidation->AsBinding(), m_passwordValidation->AsBinding(),
      m_notesValidation->AsBinding()});
}

GalleryViewModel::~GalleryViewModel() {
  if (m_asyncMotion) {
    if (m_asyncMotion->cancellation) {
      m_asyncMotion->cancellation->Cancel();
    }
    m_asyncMotion->primary.CancelAll();
    m_asyncMotion->secondary.CancelAll();
  }
}

void GalleryViewModel::AttachRuntime(Application &application,
                                     NativeTextSystem &text,
                                     Window &window) noexcept {
  m_application = &application;
  m_text = &text;
  m_window = &window;
  m_asyncMotion = std::make_shared<GalleryMotionDemo>(application);
  m_formSave = std::make_unique<AsyncCommand>(
      application.BackgroundTasks(), application.CreateTaskContext(window),
      [this](NGIN::Async::TaskContext &context) {
        return RunFormSave(context);
      },
      true, CommandConcurrencyPolicy::Reject, 1,
      [this](const InvalidationKind kind) { Invalidate(kind); });
  m_formSave->BindEnabled(m_validationForm->IsValid());
  m_nameValidation->SetAsyncValidator(application.BackgroundTasks(),
                                      application.CreateTaskContext(window),
                                      ValidateDisplayName);
  const auto asyncScheduler = [this](const InvalidationKind kind) {
    Invalidate(kind);
  };
  m_asyncHost = std::make_unique<KeyedViewModelHost<GalleryAsyncViewModel>>(
      application.BackgroundTasks(), application.CreateTaskContext(window),
      [asyncScheduler](const String &key, const ViewModelServiceResolver &) {
        return std::make_shared<GalleryAsyncViewModel>(key, asyncScheduler);
      },
      ViewModelServiceResolver{}, asyncScheduler);
  m_imageCache = std::make_unique<ImageTextureCache>(application.Renderer());
  std::error_code pathError;
  auto imagePath = std::filesystem::path{"images"} / "gallery-sample.png";
  if (!std::filesystem::is_regular_file(imagePath, pathError) || pathError) {
    imagePath = std::filesystem::path{__FILE__}.parent_path().parent_path() /
                "assets" / "images" / "gallery-sample.png";
  }
  m_galleryImage = ImageResource::DecodeFileAsync(
      ImageFileSource{.path = String{imagePath.string()}});
  m_galleryImage->Wait();
  if (m_galleryImage->State() == ImageLoadState::Failed) {
    Report(m_galleryImage->Error());
  }
  m_helpToolTip = std::make_unique<ToolTipController>(
      window, String{"Clear the name in both fields."});
  auto navigation = InitializePageNavigation();
  if (!navigation) {
    Report(std::move(navigation).error());
  }
}

auto GalleryViewModel::InitializePageNavigation() -> UIResult<void> {
  if (m_navigation) {
    return {};
  }
  auto registered = RegisterGalleryPages(m_pages, *this);
  if (!registered) {
    return registered;
  }
  m_pageActivation = std::make_unique<GalleryPageActivationContext>();
  m_navigation =
      std::make_unique<NavigationService>(m_pages, *m_pageActivation,
                                          NavigationOptions{
                                              .region = "Gallery.Content",
                                              .cacheCapacity = 0,
                                          });
  m_navigation->SetFailureObserver([this](const UIError &error) {
    ++m_navigationFailures;
    Report(error);
  });
  const auto pageIndex = static_cast<NGIN::UIntSize>(m_page.Get());
  auto started = GalleryPageStarters[pageIndex](*m_navigation);
  if (!started) {
    return std::unexpected(std::move(started).error());
  }
  ++m_pageActivations;
  return {};
}

auto GalleryViewModel::CurrentPage() const noexcept -> Page {
  return m_page.Get();
}

void GalleryViewModel::SelectPage(const Page page) {
  SetBrowserOpen(false);
  if (m_page.Get() == Page::Motion && page != Page::Motion) {
    CancelMotionRepeat();
    CancelAsyncMotion();
    m_motionPopup.Close();
    if (m_window != nullptr) {
      m_window->SetMotionEnabled(true);
    }
  }
  if (m_page.Get() == Page::AsyncData && page != Page::AsyncData &&
      m_asyncHost) {
    m_asyncHost->Hide();
  }
  SetPopupOpen(false);
  if (m_navigation) {
    const auto pageIndex = static_cast<NGIN::UIntSize>(page);
    auto navigated = GalleryPageReplacements[pageIndex](*m_navigation);
    if (!navigated) {
      return;
    }
    ++m_pageActivations;
    ++m_pageReleases;
  }
  static_cast<void>(m_page.Set(page));
  if (page == Page::AsyncData && m_asyncHost && !m_asyncHost->IsMounted()) {
    auto shown = m_asyncHost->Show(String{"inbox"});
    if (!shown) {
      Report(std::move(shown).error());
    }
  }
}

auto GalleryViewModel::CurrentPageView() const -> Views::View {
  return m_navigation ? m_navigation->ContentView() : Views::View{};
}

auto GalleryViewModel::PageRevision() const -> UInt64 {
  return m_pageRevision.Get();
}

auto GalleryViewModel::PageNavigationDiagnostics() const -> NavigationSnapshot {
  return m_navigation ? m_navigation->Snapshot() : NavigationSnapshot{};
}

auto GalleryViewModel::PageActivationCount() const noexcept -> NGIN::UInt64 {
  return m_pageActivations;
}

auto GalleryViewModel::PageReleaseCount() const noexcept -> NGIN::UInt64 {
  return m_pageReleases;
}

auto GalleryViewModel::NavigationFailureCount() const noexcept -> NGIN::UInt64 {
  return m_navigationFailures;
}

auto GalleryViewModel::ActivePageTaskCount() const noexcept -> NGIN::UIntSize {
  return m_asyncHost ? m_asyncHost->ActiveTaskStatus().activeCount : 0;
}

auto GalleryViewModel::CleanupPageTaskCount() const noexcept -> NGIN::UIntSize {
  return m_asyncHost ? m_asyncHost->CleanupTaskStatus().activeCount : 0;
}

auto GalleryViewModel::NavigationSearchBinding() -> Binding<String> {
  return Bind(m_navigationSearch);
}

auto GalleryViewModel::NavigationMatches(const Page page) const -> bool {
  const auto query = m_navigationSearch.Get().View();
  const auto index = static_cast<NGIN::UIntSize>(page);
  return ContainsIgnoringCase(PageNames[index], query) ||
         ContainsIgnoringCase(PageSearchTerms[index], query);
}

auto GalleryViewModel::IsLightTheme() const noexcept -> bool {
  return m_lightTheme.Get();
}

void GalleryViewModel::ToggleTheme() {
  static_cast<void>(m_lightTheme.Set(!m_lightTheme.Get()));
}

auto GalleryViewModel::Name() const noexcept -> const String & {
  return m_name.Get();
}

auto GalleryViewModel::NameBinding() -> Binding<String> { return Bind(m_name); }

auto GalleryViewModel::PasswordBinding() -> Binding<String> {
  return Bind(m_password);
}

auto GalleryViewModel::NotesBinding() -> Binding<String> {
  return Bind(m_notes);
}

auto GalleryViewModel::NameValidationMessage() const -> String {
  return FirstValidationMessage(m_nameValidation, "Looks good");
}

auto GalleryViewModel::PasswordValidationMessage() const -> String {
  return FirstValidationMessage(m_passwordValidation,
                                "Checked when you choose Check form");
}

auto GalleryViewModel::NotesValidationMessage() const -> String {
  return FirstValidationMessage(m_notesValidation,
                                "Checked on the first form check");
}

auto GalleryViewModel::ValidationSummary() const -> String {
  if (!m_validationForm) {
    return String{"Validation is not ready"};
  }
  if (m_validationForm->IsValidating().Get()) {
    return String{"Checking name availability..."};
  }
  const auto &issues = m_validationForm->Summary().Get();
  if (issues.empty()) {
    return m_validationForm->IsValid().Get()
               ? String{"No problems found. Save is ready."}
               : String{"Choose Check form to validate every field."};
  }
  const auto count = std::to_string(issues.size());
  String result{count.c_str()};
  result.Append(String{issues.size() == 1 ? " problem: " : " problems: "});
  for (NGIN::UIntSize index = 0; index < issues.size(); ++index) {
    if (index != 0) {
      result.Append(String{"; "});
    }
    result.Append(issues[index].field);
  }
  return result;
}

auto GalleryViewModel::IsFormValidating() const noexcept -> bool {
  return m_validationForm && m_validationForm->IsValidating().Get();
}

auto GalleryViewModel::IsFormSaving() const noexcept -> bool {
  return m_formSave && m_formSave->Status().isRunning;
}

auto GalleryViewModel::FormSaveBinding() const -> CommandBinding {
  return m_formSave ? m_formSave->AsBinding() : CommandBinding{};
}

auto GalleryViewModel::FormSaveStatus() const -> String {
  if (!m_formSave) {
    return String{"Check the form to enable Save."};
  }
  const auto &status = m_formSave->Status();
  if (status.isRunning) {
    return String{"Saving... You can cancel while the save is running."};
  }
  switch (status.lastOutcome.kind) {
  case CommandOutcomeKind::None:
    return String{"Check the form to enable Save."};
  case CommandOutcomeKind::Succeeded:
    return LabeledNumber("Saved successfully. Total saves: ",
                         m_formSaveCount.Get());
  case CommandOutcomeKind::DomainError: {
    String message{"Save failed: "};
    message.Append(status.lastOutcome.error
                       ? status.lastOutcome.error->message
                       : String{"The request was rejected"});
    message.Append(String{". Press Save to retry."});
    return message;
  }
  case CommandOutcomeKind::Canceled:
    return String{"Save canceled. Press Save when you are ready."};
  case CommandOutcomeKind::Fault:
    return String{"Save stopped unexpectedly. Press Save to retry."};
  }
  return String{};
}

void GalleryViewModel::ValidateForm() {
  if (m_validationForm) {
    m_validationForm->ValidateAll();
  }
}

void GalleryViewModel::StartFailingFormSave() {
  if (!m_formSave || !m_formSave->Status().canExecute) {
    return;
  }
  m_formSaveShouldFail = true;
  static_cast<void>(m_formSave->Execute());
}

void GalleryViewModel::CancelFormSave() noexcept {
  m_formSaveShouldFail = false;
  if (m_formSave) {
    m_formSave->Cancel();
  }
}

auto GalleryViewModel::RunFormSave(NGIN::Async::TaskContext &context)
    -> NGIN::Async::Task<void, CommandError> {
  const auto shouldFail = std::exchange(m_formSaveShouldFail, false);
  co_await context.Delay(NGIN::Units::Seconds(1.0));
  if (shouldFail) {
    co_await NGIN::Async::DomainFailure(CommandError{
        .kind = CommandErrorKind::Domain,
        .code = String{"gallery-save"},
        .message = String{"The demo server rejected the save"},
    });
  }
  static_cast<void>(m_formSaveCount.Set(m_formSaveCount.Get() + 1));
}

void GalleryViewModel::SelectAsyncDemo(const char *key) {
  if (!m_asyncHost) {
    return;
  }
  auto shown = m_asyncHost->Show(String{key});
  if (!shown) {
    Report(std::move(shown).error());
  }
}

void GalleryViewModel::RunRapidAsyncDemo() {
  SelectAsyncDemo("broken");
  SelectAsyncDemo("empty");
  SelectAsyncDemo("inbox");
}

auto GalleryViewModel::AsyncDemoKey() const -> String {
  return m_asyncHost ? m_asyncHost->CurrentKey() : String{};
}

auto GalleryViewModel::AsyncDemoState() const noexcept
    -> AsyncPresentationKind {
  const auto current = m_asyncHost ? m_asyncHost->Current()
                                   : std::shared_ptr<GalleryAsyncViewModel>{};
  return current ? current->Presentation().Get().kind
                 : AsyncPresentationKind::Idle;
}

auto GalleryViewModel::AsyncDemoMessage() const -> String {
  const auto current = m_asyncHost ? m_asyncHost->Current()
                                   : std::shared_ptr<GalleryAsyncViewModel>{};
  if (!current) {
    return String{"No sample is selected"};
  }
  const auto &snapshot = current->Presentation().Get();
  switch (snapshot.kind) {
  case AsyncPresentationKind::Idle:
    return String{"Loading was canceled"};
  case AsyncPresentationKind::Loading: {
    String message{"Loading "};
    message.Append(current->Key());
    message.Append(String{"..."});
    return message;
  }
  case AsyncPresentationKind::Content:
    return String{"3 items loaded"};
  case AsyncPresentationKind::Empty:
    return String{"There are no items here"};
  case AsyncPresentationKind::Error:
    return snapshot.error ? snapshot.error->message : String{"Loading failed"};
  }
  return String{};
}

auto GalleryViewModel::AsyncDemoItems() const -> std::vector<String> {
  const auto current = m_asyncHost ? m_asyncHost->Current()
                                   : std::shared_ptr<GalleryAsyncViewModel>{};
  if (!current) {
    return {};
  }
  const auto &snapshot = current->Presentation().Get();
  return snapshot.content.value_or(std::vector<String>{});
}

auto GalleryViewModel::AsyncRetryBinding() const -> CommandBinding {
  const auto current = m_asyncHost ? m_asyncHost->Current()
                                   : std::shared_ptr<GalleryAsyncViewModel>{};
  return current ? current->Presentation().RetryCommand() : CommandBinding{};
}

auto GalleryViewModel::AsyncCancelBinding() const -> CommandBinding {
  const auto current = m_asyncHost ? m_asyncHost->Current()
                                   : std::shared_ptr<GalleryAsyncViewModel>{};
  return current ? current->Presentation().CancelCommand() : CommandBinding{};
}

auto GalleryViewModel::GalleryImage() const noexcept
    -> const std::shared_ptr<ImageResource> & {
  return m_galleryImage;
}

auto GalleryViewModel::ImageCache() noexcept -> ImageTextureCache * {
  return m_imageCache.get();
}

auto GalleryViewModel::CheckBinding() -> Binding<CheckState> {
  return Bind(m_check);
}

auto GalleryViewModel::MixedCheckBinding() -> Binding<CheckState> {
  return Bind(m_mixedCheck);
}

auto GalleryViewModel::UncheckedBinding() -> Binding<CheckState> {
  return Bind(m_unchecked);
}

auto GalleryViewModel::DensityBinding() -> Binding<Density> {
  return Bind(m_density);
}

auto GalleryViewModel::ToggleBinding() -> Binding<bool> {
  return Bind(m_toggle);
}

auto GalleryViewModel::DisabledToggleBinding() -> Binding<bool> {
  return Bind(m_disabledToggle);
}

auto GalleryViewModel::SliderBinding() -> Binding<F32> {
  return Bind(m_slider);
}

auto GalleryViewModel::SliderValue() const noexcept -> F32 {
  return m_slider.Get();
}

auto GalleryViewModel::HelpToolTip() noexcept -> ToolTipController * {
  return m_helpToolTip.get();
}

auto GalleryViewModel::CollectionItems() const -> std::vector<std::uint32_t> {
  auto items = m_collectionItems.Get();
  if (m_collectionFiltered.Get()) {
    std::erase_if(items, [](const auto item) { return item % 2 == 0; });
  }
  std::sort(items.begin(), items.end());
  if (m_collectionDescending.Get()) {
    std::reverse(items.begin(), items.end());
  }
  return items;
}

auto GalleryViewModel::CollectionSelection(const std::uint32_t item)
    -> ItemSelection {
  return BindListItem(m_collectionSelection, item);
}

auto GalleryViewModel::SelectedCollectionItem() const noexcept
    -> std::optional<std::uint32_t> {
  return m_collectionSelection.Value();
}

void GalleryViewModel::AddCollectionItem() {
  const auto item = m_nextCollectionItem++;
  static_cast<void>(
      m_collectionItems.Update([item](auto &items) { items.push_back(item); }));
  static_cast<void>(m_collectionSelection.Select(item));
  Notify("Added and selected an item");
}

void GalleryViewModel::RemoveSelectedCollectionItem() {
  const auto selected = m_collectionSelection.Value();
  if (!selected) {
    Notify("Select an item before removing it");
    return;
  }
  static_cast<void>(m_collectionItems.Update(
      [selected](auto &items) { std::erase(items, *selected); }));
  static_cast<void>(m_collectionSelection.Clear());
  Notify("Removed the selected item");
}

void GalleryViewModel::ToggleCollectionSort() {
  static_cast<void>(m_collectionDescending.Set(!m_collectionDescending.Get()));
}

void GalleryViewModel::ToggleCollectionFilter() {
  static_cast<void>(m_collectionFiltered.Set(!m_collectionFiltered.Get()));
}

auto GalleryViewModel::IsCollectionDescending() const noexcept -> bool {
  return m_collectionDescending.Get();
}

auto GalleryViewModel::IsCollectionFiltered() const noexcept -> bool {
  return m_collectionFiltered.Get();
}

auto GalleryViewModel::CollectionTabBinding() -> Binding<CollectionTab> {
  return Bind(m_collectionTab);
}

auto GalleryViewModel::VirtualizedCollectionSource() noexcept
    -> IVirtualizedDataSource<NGIN::UIntSize> & {
  return *m_virtualizedSource;
}

auto GalleryViewModel::VirtualizedCollectionController() noexcept
    -> FixedVirtualizedListController & {
  return *m_virtualizedController;
}

auto GalleryViewModel::SelectedVirtualizedIndex() const noexcept
    -> std::optional<NGIN::UIntSize> {
  return m_virtualizedSource->IndexOfKey(m_virtualizedSelection.Get());
}

auto GalleryViewModel::SelectVirtualizedItem(const NGIN::UIntSize index)
    -> UIResult<void> {
  auto key = m_virtualizedSource->KeyAt(index);
  if (!key) {
    return std::unexpected(std::move(key).error());
  }
  static_cast<void>(m_virtualizedSelection.Set(std::move(key).value()));
  return {};
}

void GalleryViewModel::PrependVirtualizedItems() {
  m_virtualizedSource->Prepend(250);
  Notify("Added 250 rows while keeping the visible item in place");
}

auto GalleryViewModel::MotionForward() const noexcept -> bool {
  return m_motionForward.Get();
}

void GalleryViewModel::ToggleMotionTarget() {
  static_cast<void>(m_motionForward.Set(!m_motionForward.Get()));
}

auto GalleryViewModel::BezierX1Binding() -> Binding<F32> {
  return Bind(m_bezierX1);
}
auto GalleryViewModel::BezierY1Binding() -> Binding<F32> {
  return Bind(m_bezierY1);
}
auto GalleryViewModel::BezierX2Binding() -> Binding<F32> {
  return Bind(m_bezierX2);
}
auto GalleryViewModel::BezierY2Binding() -> Binding<F32> {
  return Bind(m_bezierY2);
}
auto GalleryViewModel::BezierX1() const noexcept -> F32 {
  return m_bezierX1.Get();
}
auto GalleryViewModel::BezierY1() const noexcept -> F32 {
  return m_bezierY1.Get();
}
auto GalleryViewModel::BezierX2() const noexcept -> F32 {
  return m_bezierX2.Get();
}
auto GalleryViewModel::BezierY2() const noexcept -> F32 {
  return m_bezierY2.Get();
}

auto GalleryViewModel::MotionRepeatRunning() const noexcept -> bool {
  return m_motionRepeatRunning.Get();
}

void GalleryViewModel::StartMotionRepeat() {
  if (m_motionRepeatHandle) {
    m_motionRepeatHandle->Cancel();
  }
  m_motionRepeatHandle = std::make_unique<AnimationHandle>();
  static_cast<void>(m_motionRepeatRunning.Set(true));
  Invalidate(InvalidationKind::All);
}

void GalleryViewModel::CancelMotionRepeat() {
  if (m_motionRepeatHandle) {
    m_motionRepeatHandle->Cancel();
  }
  static_cast<void>(m_motionRepeatRunning.Set(false));
  Invalidate(InvalidationKind::All);
}

void GalleryViewModel::FinishMotionRepeat() {
  static_cast<void>(m_motionRepeatRunning.Set(false));
}

auto GalleryViewModel::MotionRepeatHandle() const noexcept
    -> const AnimationHandle * {
  return m_motionRepeatHandle.get();
}

auto GalleryViewModel::MotionPreviewReduced() const noexcept -> bool {
  return m_window != nullptr && !m_window->MotionEnabled();
}

void GalleryViewModel::ToggleMotionPreviewReduced() {
  if (m_window != nullptr) {
    m_window->SetMotionEnabled(!m_window->MotionEnabled());
  }
  Invalidate(InvalidationKind::All);
}

auto GalleryViewModel::MotionPopup() noexcept -> PopupController & {
  return m_motionPopup;
}

auto GalleryViewModel::AsyncPrimaryMotion() noexcept -> MotionController & {
  return m_asyncMotion->primary;
}

auto GalleryViewModel::AsyncSecondaryMotion() noexcept -> MotionController & {
  return m_asyncMotion->secondary;
}

auto GalleryViewModel::AsyncMotionStatus() const noexcept -> const String & {
  return m_asyncMotion->status.Get();
}

void GalleryViewModel::StartAsyncMotionSequence() {
  if (!m_asyncMotion || !m_asyncMotion->context || m_application == nullptr) {
    return;
  }
  if (!m_application->BackgroundTasks().Transfer(
          SequenceMotion(*m_asyncMotion->context, m_asyncMotion))) {
    m_asyncMotion->SetStatus("Motion could not start");
  }
}

void GalleryViewModel::StartParallelMotion() {
  if (!m_asyncMotion || !m_asyncMotion->context || m_application == nullptr) {
    return;
  }
  if (!m_application->BackgroundTasks().Transfer(
          ParallelMotion(*m_asyncMotion->context, m_asyncMotion))) {
    m_asyncMotion->SetStatus("Motion could not start");
  }
}

void GalleryViewModel::StartCancelableMotion() {
  if (!m_asyncMotion || m_application == nullptr) {
    return;
  }
  if (m_asyncMotion->cancellation) {
    m_asyncMotion->cancellation->Cancel();
  }
  m_asyncMotion->cancellation =
      std::make_shared<NGIN::Async::CancellationSource>();
  auto run = std::make_shared<GalleryCancelableRun>(
      *m_application, m_asyncMotion->cancellation->GetToken());
  if (!m_application->BackgroundTasks().Transfer(
          CancelableMotion(run, m_asyncMotion))) {
    m_asyncMotion->SetStatus("Motion could not start");
  }
}

void GalleryViewModel::InterruptAsyncMotion() {
  if (!m_asyncMotion || !m_asyncMotion->context || m_application == nullptr) {
    return;
  }
  const auto target = m_asyncMotion->interruptForward ? 1.0F : 0.0F;
  m_asyncMotion->interruptForward = !m_asyncMotion->interruptForward;
  if (!m_application->BackgroundTasks().Transfer(
          InterruptMotion(*m_asyncMotion->context, m_asyncMotion, target))) {
    m_asyncMotion->SetStatus("Motion could not start");
  }
}

void GalleryViewModel::CancelAsyncMotion() {
  if (!m_asyncMotion) {
    return;
  }
  if (m_asyncMotion->cancellation) {
    m_asyncMotion->cancellation->Cancel();
  }
  m_asyncMotion->primary.CancelAll();
  m_asyncMotion->secondary.CancelAll();
}

auto GalleryViewModel::ComboPopup() noexcept -> PopupController & {
  return m_comboPopup;
}

auto GalleryViewModel::MenuPopup() noexcept -> PopupController & {
  return m_menuPopup;
}

auto GalleryViewModel::ContextPopup() noexcept -> PopupController & {
  return m_contextPopup;
}

void GalleryViewModel::Notify(const char *message) {
  static_cast<void>(m_status.Set(String{message}));
}

auto GalleryViewModel::IsPopupOpen() const noexcept -> bool {
  return m_popupOpen.Get();
}

void GalleryViewModel::SetPopupOpen(const bool open) {
  static_cast<void>(m_popupOpen.Set(open));
}

auto GalleryViewModel::IsInspectorEnabled() const noexcept -> bool {
  return m_inspectorEnabled.Get();
}

void GalleryViewModel::ToggleInspector() {
  const auto enabled = !m_inspectorEnabled.Get();
  static_cast<void>(m_inspectorEnabled.Set(enabled));
  if (m_window != nullptr) {
    m_window->SetInspectorOverlay(InspectorOverlayOptions{
        .enabled = enabled,
        .showLayoutBounds = true,
        .showHitTestBounds = false,
        .showFocus = true,
    });
  }
}

auto GalleryViewModel::OpenAuxiliaryWindow(const bool modal) noexcept
    -> UIResult<void> {
  if (m_application == nullptr || m_text == nullptr || m_window == nullptr) {
    return std::unexpected(MakeUIError(
        UIErrorCode::InvalidState, "Gallery runtime is not attached",
        "NGIN.UI.Gallery", "OpenAuxiliaryWindow"));
  }

  ++m_auxiliaryWindowId;
  String id{modal ? "Gallery.Dialog." : "Gallery.Window."};
  id.Append(Number(m_auxiliaryWindowId));
  String title{modal ? "NGIN.UI Modal Dialog" : "NGIN.UI Auxiliary Window"};
  Window *created = nullptr;
  if (modal) {
    auto result = m_application->CreateDialogWindow(
        *m_window, WindowCreateInfo{
                       .id = id,
                       .title = title,
                       .initialSize = PixelSize{520, 300},
                       .minimumSize = PixelSize{420, 240},
                   });
    if (!result) {
      return std::unexpected(std::move(result).error());
    }
    created = result.value();
  } else {
    auto result = m_application->CreateWindow(WindowCreateInfo{
        .id = id,
        .title = title,
        .initialSize = PixelSize{560, 340},
        .minimumSize = PixelSize{420, 240},
    });
    if (!result) {
      return std::unexpected(std::move(result).error());
    }
    created = result.value();
  }

  created->SetContent(AuxiliaryWindowView(*this, modal),
                      Views::ViewEnvironment{*m_text});
  static_cast<void>(m_status.Set(
      String{modal ? "Opened modal dialog" : "Opened auxiliary window"}));
  return {};
}

auto GalleryViewModel::Status() const noexcept -> const String & {
  return m_status.Get();
}

auto GalleryViewModel::Diagnostics() const noexcept -> WindowDiagnostics {
  return m_window != nullptr ? m_window->Diagnostics() : WindowDiagnostics{};
}

auto GalleryViewModel::TextDiagnostics() const noexcept
    -> GlyphAtlasDiagnostics {
  return m_text != nullptr ? m_text->AtlasDiagnostics()
                           : GlyphAtlasDiagnostics{};
}

auto GalleryViewModel::FontDiagnostics() const noexcept
    -> FontCoverageDiagnostics {
  return m_text != nullptr ? m_text->CoverageDiagnostics()
                           : FontCoverageDiagnostics{};
}

auto GalleryViewModel::ImageDiagnostics() const noexcept
    -> ImageCacheDiagnostics {
  return m_imageCache != nullptr ? m_imageCache->Diagnostics()
                                 : ImageCacheDiagnostics{};
}

auto GalleryViewModel::AccessibilityDiagnostics() const noexcept
    -> NGIN::UI::AccessibilityDiagnostics {
  return m_application != nullptr ? m_application->AccessibilityDiagnostics()
                                  : NGIN::UI::AccessibilityDiagnostics{};
}

auto GalleryViewModel::AccessibilityAnnouncement() const noexcept
    -> const String & {
  return m_accessibilityAnnouncement.Get();
}

void GalleryViewModel::AnnounceAccessibilityDemo() {
  const auto next = ++m_announcementCount;
  String message{"Accessibility message "};
  message.Append(Number(next));
  message.Append(String{" is ready."});
  static_cast<void>(m_accessibilityAnnouncement.Set(std::move(message)));
}

void GalleryViewModel::Report(UIError error) {
  static_cast<void>(m_status.Set(std::move(error.message)));
}

void GalleryViewModel::Invalidate(const InvalidationKind kind) const {
  if (HasInvalidation(kind, InvalidationKind::Compose)) {
    static_cast<void>(
        m_pageRevision.Update([](UInt64 &revision) { ++revision; }));
  }
  if (m_window != nullptr) {
    m_window->Invalidate(kind);
  }
}

auto CreateMainWindow(Application &application, NativeTextSystem &text,
                      GalleryViewModel &model) -> UIResult<Window *> {
  auto window = application.CreateWindow(WindowCreateInfo{
      .id = String{"Gallery.Main"},
      .title = String{"NGIN.UI Sample Gallery"},
      .initialSize = PixelSize{1280, 840},
      .minimumSize = PixelSize{480, 420},
  });
  if (!window) {
    return std::unexpected(std::move(window).error());
  }
  model.AttachRuntime(application, text, *window.value());
  auto &host = **window;
  model.UpdateViewport(host.PixelExtent().width, host.ScaleFactor());
  host.SetEventHandler([&host, &model](const PlatformEvent &event) {
    if (const auto *resize = std::get_if<WindowResized>(&event)) {
      model.UpdateViewport(resize->size.width, host.ScaleFactor());
    } else if (const auto *scale = std::get_if<WindowScaleChanged>(&event)) {
      model.UpdateViewport(host.PixelExtent().width, scale->scaleFactor);
    } else if (const auto *key = std::get_if<KeyChanged>(&event);
               key && key->state == KeyState::Pressed &&
               key->logicalKey == static_cast<NGIN::UInt32>(LogicalKey::Escape)) {
      model.SetBrowserOpen(false);
    }
  });
  window.value()->SetContent(MainView(model), Views::ViewEnvironment{text});
  return window.value();
}
} // namespace NGIN::UIGallery
