#pragma once

#include <NGIN/UI/UI.hpp>

#include <cstdint>
#include <string_view>

namespace NGIN::UIGallery {
enum class Page : NGIN::UInt8 {
  Overview,
  Layout,
  Typography,
  Inputs,
  Collections,
  Overlays,
  Windows,
  Resources,
  Diagnostics,
};

inline constexpr NGIN::UIntSize PageCount = 9;

[[nodiscard]] auto PageAt(NGIN::UIntSize index) noexcept -> Page;
[[nodiscard]] auto PageName(Page page) noexcept -> std::string_view;

class Model final {
public:
  Model();

  void AttachRuntime(UI::Application &application, UI::NativeTextSystem &text,
                     UI::Window &window) noexcept;

  [[nodiscard]] auto CurrentPage() const noexcept -> Page;
  void SelectPage(Page page);

  [[nodiscard]] auto CurrentTheme() const -> UI::Theme;
  [[nodiscard]] auto IsLightTheme() const noexcept -> bool;
  void ToggleTheme();

  [[nodiscard]] auto Name() const noexcept -> const Text::String &;
  [[nodiscard]] auto NameBinding() -> UI::Binding<Text::String>;
  [[nodiscard]] auto PasswordBinding() -> UI::Binding<Text::String>;
  [[nodiscard]] auto ActivationCount() const noexcept -> std::uint32_t;
  void Activate();

  [[nodiscard]] auto IsPopupOpen() const noexcept -> bool;
  void SetPopupOpen(bool open);
  [[nodiscard]] auto IsInspectorEnabled() const noexcept -> bool;
  void ToggleInspector();

  [[nodiscard]] auto OpenAuxiliaryWindow(bool modal) noexcept
      -> UI::UIResult<void>;
  [[nodiscard]] auto Status() const noexcept -> const Text::String &;
  [[nodiscard]] auto Diagnostics() const noexcept -> UI::WindowDiagnostics;
  void Report(UI::UIError error);

private:
  void Invalidate(
      UI::InvalidationKind kind = UI::InvalidationKind::All) const noexcept;

  UI::Application *m_application{nullptr};
  UI::NativeTextSystem *m_text{nullptr};
  UI::Window *m_window{nullptr};
  UI::State<Page> m_page;
  UI::State<bool> m_lightTheme;
  UI::State<Text::String> m_name;
  UI::State<Text::String> m_password;
  UI::State<std::uint32_t> m_activationCount;
  UI::State<bool> m_popupOpen;
  UI::State<bool> m_inspectorEnabled;
  UI::State<Text::String> m_status;
  std::uint32_t m_auxiliaryWindowId{0};
};

void ComposeMainView(UI::Composer &composer, UI::NativeTextSystem &text,
                     Model &model);

[[nodiscard]] auto CreateMainWindow(UI::Application &application,
                                    UI::NativeTextSystem &text, Model &model)
    -> UI::UIResult<UI::Window *>;
} // namespace NGIN::UIGallery
