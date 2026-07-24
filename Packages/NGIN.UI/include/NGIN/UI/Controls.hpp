#pragma once

#include <NGIN/UI/Application.hpp>
#include <NGIN/UI/Theme.hpp>

#include <chrono>
#include <concepts>
#include <memory>
#include <string_view>
#include <utility>

namespace NGIN::UI {
enum class CheckState : UInt8 {
  Unchecked,
  Checked,
  Indeterminate,
};

struct ControlPresentation final {
  Theme theme{};
  bool invalid{false};
  NGIN::Utilities::Callable<void(const UIError &)> onError{};
};

struct RadioSelection final {
  NGIN::Utilities::Callable<bool()> isSelected{};
  NGIN::Utilities::Callable<UIResult<void>()> select{};
};

template <typename T>
  requires std::equality_comparable<T>
[[nodiscard]] auto BindRadio(Binding<T> selection, T option) -> RadioSelection {
  return RadioSelection{
      .isSelected = [selection, option] { return selection.Get() == option; },
      .select =
          [selection, option = std::move(option)]() mutable {
            return selection.Set(option);
          },
  };
}

struct SliderRange final {
  F32 minimum{0.0F};
  F32 maximum{1.0F};
  F32 step{0.1F};
};

struct ProgressValue final {
  F32 value{0.0F};
  F32 minimum{0.0F};
  F32 maximum{1.0F};
  bool indeterminate{false};
};

void CheckBox(Composer &composer, Binding<CheckState> value,
              const ControlPresentation &presentation = {},
              const NodeProperties &properties = {}, std::string_view key = {});
void RadioButton(Composer &composer, RadioSelection selection,
                 const ControlPresentation &presentation = {},
                 const NodeProperties &properties = {},
                 std::string_view key = {});
void ToggleSwitch(Composer &composer, Binding<bool> value,
                  const ControlPresentation &presentation = {},
                  const NodeProperties &properties = {},
                  std::string_view key = {});
void Slider(Composer &composer, Binding<F32> value, SliderRange range = {},
            const ControlPresentation &presentation = {},
            const NodeProperties &properties = {}, std::string_view key = {});
void ProgressBar(Composer &composer, ProgressValue value,
                 const ControlPresentation &presentation = {},
                 const NodeProperties &properties = {},
                 std::string_view key = {});

void Label(Composer &composer, NGIN::Text::String value, ITextLayout &layout,
           IGlyphAtlas &glyphAtlas, std::string_view identifier,
           std::string_view targetIdentifier,
           const NodeProperties &properties = {}, std::string_view key = {});

class ToolTipController final {
public:
  explicit ToolTipController(
      Window &window, NGIN::Text::String content,
      std::chrono::milliseconds delay = std::chrono::milliseconds{500});
  ToolTipController(const ToolTipController &) = delete;
  ToolTipController(ToolTipController &&) = delete;
  auto operator=(const ToolTipController &) -> ToolTipController & = delete;
  auto operator=(ToolTipController &&) -> ToolTipController & = delete;
  ~ToolTipController() = default;

  void Attach(NodeProperties &target);
  [[nodiscard]] auto IsOpen() const noexcept -> bool;
  void Dismiss() noexcept;

  template <typename ComposeContent>
  void Compose(Composer &composer, ComposeContent &&composeContent,
               std::string_view key = "tooltip") const {
    if (!IsOpen()) {
      return;
    }
    NodeProperties popup{};
    popup.popup.anchor = Anchor();
    popup.popup.placement = PopupPlacement::BelowStart;
    popup.popup.gap = 8.0F;
    popup.popup.modal = false;
    popup.popup.dismissOnOutsidePointer = false;
    popup.popup.dismissOnEscape = false;
    popup.interaction.hitTestVisible = false;
    popup.semantics.hidden = true;
    composer.Popup(std::forward<ComposeContent>(composeContent), popup, key);
  }

private:
  struct State;

  [[nodiscard]] auto Anchor() const noexcept -> Rect;

  std::shared_ptr<State> m_state;
};
} // namespace NGIN::UI
