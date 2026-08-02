#pragma once

#include <NGIN/UI/UI.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace NGIN::UIGallery {
class GalleryVirtualizedSource;
class GalleryMotionDemo;
class GalleryAsyncViewModel;

enum class Page : NGIN::UInt8 {
  Overview,
  Layout,
  Typography,
  TextArea,
  Images,
  Inputs,
  AsyncData,
  Collections,
  Motion,
  Overlays,
  Windows,
  Resources,
  Accessibility,
  Diagnostics,
};

enum class Density : NGIN::UInt8 {
  Compact,
  Comfortable,
  Spacious,
};

enum class CollectionTab : NGIN::UInt8 {
  Selection,
  Identity,
  DataSource,
};

inline constexpr NGIN::UIntSize PageCount = 14;

[[nodiscard]] auto PageAt(NGIN::UIntSize index) noexcept -> Page;
[[nodiscard]] auto PageName(Page page) noexcept -> std::string_view;
[[nodiscard]] auto PageExample(Page page) noexcept -> std::string_view;

class GalleryViewModel final {
public:
  GalleryViewModel();
  ~GalleryViewModel();

  void AttachRuntime(UI::Application &application, UI::NativeTextSystem &text,
                     UI::Window &window) noexcept;

  [[nodiscard]] auto CurrentPage() const noexcept -> Page;
  void SelectPage(Page page);
  [[nodiscard]] auto NavigationSearchBinding() -> UI::Binding<Text::String>;
  [[nodiscard]] auto NavigationMatches(Page page) const -> bool;
  void CopyExample(Page page);

  [[nodiscard]] auto CurrentTheme() const -> UI::Theme;
  [[nodiscard]] auto IsLightTheme() const noexcept -> bool;
  void ToggleTheme();

  [[nodiscard]] auto Name() const noexcept -> const Text::String &;
  [[nodiscard]] auto NameBinding() -> UI::Binding<Text::String>;
  [[nodiscard]] auto PasswordBinding() -> UI::Binding<Text::String>;
  [[nodiscard]] auto NotesBinding() -> UI::Binding<Text::String>;
  [[nodiscard]] auto NameValidationMessage() const -> Text::String;
  [[nodiscard]] auto PasswordValidationMessage() const -> Text::String;
  [[nodiscard]] auto NotesValidationMessage() const -> Text::String;
  [[nodiscard]] auto ValidationSummary() const -> Text::String;
  [[nodiscard]] auto IsFormValidating() const noexcept -> bool;
  [[nodiscard]] auto IsFormSaving() const noexcept -> bool;
  [[nodiscard]] auto FormSaveBinding() const -> UI::CommandBinding;
  [[nodiscard]] auto FormSaveStatus() const -> Text::String;
  void ValidateForm();
  void StartFailingFormSave();
  void CancelFormSave() noexcept;
  void SelectAsyncDemo(const char *key);
  void RunRapidAsyncDemo();
  [[nodiscard]] auto AsyncDemoKey() const -> Text::String;
  [[nodiscard]] auto AsyncDemoState() const noexcept
      -> UI::AsyncPresentationKind;
  [[nodiscard]] auto AsyncDemoMessage() const -> Text::String;
  [[nodiscard]] auto AsyncDemoItems() const -> std::vector<Text::String>;
  [[nodiscard]] auto AsyncRetryBinding() const -> UI::CommandBinding;
  [[nodiscard]] auto AsyncCancelBinding() const -> UI::CommandBinding;
  [[nodiscard]] auto GalleryImage() const noexcept
      -> const std::shared_ptr<UI::ImageResource> &;
  [[nodiscard]] auto ImageCache() noexcept -> UI::ImageTextureCache *;
  [[nodiscard]] auto CheckBinding() -> UI::Binding<UI::CheckState>;
  [[nodiscard]] auto MixedCheckBinding() -> UI::Binding<UI::CheckState>;
  [[nodiscard]] auto UncheckedBinding() -> UI::Binding<UI::CheckState>;
  [[nodiscard]] auto DensityBinding() -> UI::Binding<Density>;
  [[nodiscard]] auto ToggleBinding() -> UI::Binding<bool>;
  [[nodiscard]] auto DisabledToggleBinding() -> UI::Binding<bool>;
  [[nodiscard]] auto SliderBinding() -> UI::Binding<F32>;
  [[nodiscard]] auto SliderValue() const noexcept -> F32;
  [[nodiscard]] auto HelpToolTip() noexcept -> UI::ToolTipController *;
  [[nodiscard]] auto ActivationCount() const noexcept -> std::uint32_t;
  void Activate();
  [[nodiscard]] auto CommandDemoBinding() const -> UI::CommandBinding;
  [[nodiscard]] auto CommandDemoStatus() const -> Text::String;
  void StartFailingCommandDemo();
  void CancelCommandDemo() noexcept;

  [[nodiscard]] auto CollectionItems() const -> std::vector<std::uint32_t>;
  [[nodiscard]] auto CollectionSelection(std::uint32_t item)
      -> UI::ItemSelection;
  [[nodiscard]] auto SelectedCollectionItem() const noexcept
      -> std::optional<std::uint32_t>;
  void AddCollectionItem();
  void RemoveSelectedCollectionItem();
  void ToggleCollectionSort();
  void ToggleCollectionFilter();
  [[nodiscard]] auto IsCollectionDescending() const noexcept -> bool;
  [[nodiscard]] auto IsCollectionFiltered() const noexcept -> bool;
  [[nodiscard]] auto CollectionTabBinding() -> UI::Binding<CollectionTab>;
  [[nodiscard]] auto VirtualizedCollectionSource() noexcept
      -> UI::IVirtualizedDataSource<UIntSize> &;
  [[nodiscard]] auto VirtualizedCollectionController() noexcept
      -> UI::FixedVirtualizedListController &;
  [[nodiscard]] auto SelectedVirtualizedIndex() const noexcept
      -> std::optional<UIntSize>;
  [[nodiscard]] auto SelectVirtualizedItem(UIntSize index)
      -> UI::UIResult<void>;
  void PrependVirtualizedItems();
  [[nodiscard]] auto MotionForward() const noexcept -> bool;
  void ToggleMotionTarget();
  [[nodiscard]] auto BezierX1Binding() -> UI::Binding<F32>;
  [[nodiscard]] auto BezierY1Binding() -> UI::Binding<F32>;
  [[nodiscard]] auto BezierX2Binding() -> UI::Binding<F32>;
  [[nodiscard]] auto BezierY2Binding() -> UI::Binding<F32>;
  [[nodiscard]] auto BezierX1() const noexcept -> F32;
  [[nodiscard]] auto BezierY1() const noexcept -> F32;
  [[nodiscard]] auto BezierX2() const noexcept -> F32;
  [[nodiscard]] auto BezierY2() const noexcept -> F32;
  [[nodiscard]] auto MotionRepeatRunning() const noexcept -> bool;
  void StartMotionRepeat();
  void CancelMotionRepeat();
  void FinishMotionRepeat();
  [[nodiscard]] auto MotionRepeatHandle() const noexcept
      -> const UI::AnimationHandle *;
  [[nodiscard]] auto MotionPreviewReduced() const noexcept -> bool;
  void ToggleMotionPreviewReduced();
  [[nodiscard]] auto MotionPopup() noexcept -> UI::PopupController &;
  [[nodiscard]] auto AsyncPrimaryMotion() noexcept -> UI::MotionController &;
  [[nodiscard]] auto AsyncSecondaryMotion() noexcept -> UI::MotionController &;
  [[nodiscard]] auto AsyncMotionStatus() const noexcept -> const Text::String &;
  void StartAsyncMotionSequence();
  void StartParallelMotion();
  void StartCancelableMotion();
  void InterruptAsyncMotion();
  void CancelAsyncMotion();
  [[nodiscard]] auto ComboPopup() noexcept -> UI::PopupController &;
  [[nodiscard]] auto MenuPopup() noexcept -> UI::PopupController &;
  [[nodiscard]] auto ContextPopup() noexcept -> UI::PopupController &;
  void Notify(const char *message);

  [[nodiscard]] auto IsPopupOpen() const noexcept -> bool;
  void SetPopupOpen(bool open);
  [[nodiscard]] auto IsInspectorEnabled() const noexcept -> bool;
  void ToggleInspector();

  [[nodiscard]] auto OpenAuxiliaryWindow(bool modal) noexcept
      -> UI::UIResult<void>;
  [[nodiscard]] auto Status() const noexcept -> const Text::String &;
  [[nodiscard]] auto Diagnostics() const noexcept -> UI::WindowDiagnostics;
  [[nodiscard]] auto TextDiagnostics() const noexcept
      -> UI::GlyphAtlasDiagnostics;
  [[nodiscard]] auto FontDiagnostics() const noexcept
      -> UI::FontCoverageDiagnostics;
  [[nodiscard]] auto ImageDiagnostics() const noexcept
      -> UI::ImageCacheDiagnostics;
  [[nodiscard]] auto AccessibilityDiagnostics() const noexcept
      -> UI::AccessibilityDiagnostics;
  [[nodiscard]] auto AccessibilityAnnouncement() const noexcept
      -> const Text::String &;
  void AnnounceAccessibilityDemo();
  void Report(UI::UIError error);

private:
  [[nodiscard]] auto RunFormSave(NGIN::Async::TaskContext &context)
      -> NGIN::Async::Task<void, UI::CommandError>;
  [[nodiscard]] auto RunCommandDemo(NGIN::Async::TaskContext &context)
      -> NGIN::Async::Task<void, UI::CommandError>;
  void Invalidate(
      UI::InvalidationKind kind = UI::InvalidationKind::All) const noexcept;

  UI::Application *m_application{nullptr};
  UI::NativeTextSystem *m_text{nullptr};
  UI::Window *m_window{nullptr};
  UI::State<Page> m_page;
  UI::State<Text::String> m_navigationSearch;
  UI::State<bool> m_lightTheme;
  UI::State<Text::String> m_name;
  UI::State<Text::String> m_password;
  UI::State<Text::String> m_notes;
  UI::State<UI::CheckState> m_check;
  UI::State<UI::CheckState> m_mixedCheck;
  UI::State<UI::CheckState> m_unchecked;
  UI::State<Density> m_density;
  UI::State<bool> m_toggle;
  UI::State<bool> m_disabledToggle;
  UI::State<F32> m_slider;
  UI::State<std::uint32_t> m_activationCount;
  UI::State<std::uint32_t> m_formSaveCount;
  UI::State<std::uint32_t> m_commandSuccessCount;
  UI::State<std::vector<std::uint32_t>> m_collectionItems;
  UI::SingleSelectionModel<std::uint32_t> m_collectionSelection;
  UI::State<bool> m_collectionDescending;
  UI::State<bool> m_collectionFiltered;
  UI::State<CollectionTab> m_collectionTab;
  UI::State<bool> m_motionForward;
  UI::State<F32> m_bezierX1;
  UI::State<F32> m_bezierY1;
  UI::State<F32> m_bezierX2;
  UI::State<F32> m_bezierY2;
  UI::State<bool> m_motionRepeatRunning;
  UI::State<Text::String> m_virtualizedSelection;
  std::unique_ptr<GalleryVirtualizedSource> m_virtualizedSource;
  std::unique_ptr<UI::FixedVirtualizedListController> m_virtualizedController;
  std::unique_ptr<UI::AnimationHandle> m_motionRepeatHandle;
  std::shared_ptr<GalleryMotionDemo> m_asyncMotion;
  std::unique_ptr<UI::AsyncCommand> m_commandDemo;
  std::unique_ptr<UI::ValidationField<Text::String>> m_nameValidation;
  std::unique_ptr<UI::ValidationField<Text::String>> m_passwordValidation;
  std::unique_ptr<UI::ValidationField<Text::String>> m_notesValidation;
  std::unique_ptr<UI::ValidationForm> m_validationForm;
  std::unique_ptr<UI::AsyncCommand> m_formSave;
  UI::PopupController m_motionPopup;
  UI::PopupController m_comboPopup;
  UI::PopupController m_menuPopup;
  UI::PopupController m_contextPopup;
  UI::State<bool> m_popupOpen;
  UI::State<bool> m_inspectorEnabled;
  UI::State<Text::String> m_status;
  UI::State<Text::String> m_accessibilityAnnouncement;
  std::unique_ptr<UI::ToolTipController> m_helpToolTip;
  std::shared_ptr<UI::ImageResource> m_galleryImage;
  std::unique_ptr<UI::ImageTextureCache> m_imageCache;
  std::unique_ptr<UI::KeyedViewModelHost<GalleryAsyncViewModel>> m_asyncHost;
  std::uint32_t m_nextCollectionItem{113};
  std::uint32_t m_auxiliaryWindowId{0};
  bool m_commandDemoShouldFail{false};
  bool m_formSaveShouldFail{false};
};

void ComposeMainView(UI::Composer &composer, UI::NativeTextSystem &text,
                     GalleryViewModel &model);

[[nodiscard]] auto CreateMainWindow(UI::Application &application,
                                    UI::NativeTextSystem &text,
                                    GalleryViewModel &model)
    -> UI::UIResult<UI::Window *>;
} // namespace NGIN::UIGallery
