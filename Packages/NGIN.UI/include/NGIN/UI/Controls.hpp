#pragma once

#include <NGIN/UI/Application.hpp>
#include <NGIN/UI/Theme.hpp>

#include <chrono>
#include <concepts>
#include <memory>
#include <string_view>
#include <utility>

namespace NGIN::UI {
/// @brief Three-state value used by check-box controls.
enum class CheckState : UInt8 {
  Unchecked,
  Checked,
  Indeterminate,
};

/// @brief Shared theme, validation, and error presentation for controls.
struct ControlPresentation final {
  Theme theme{};
  bool invalid{false};
  NGIN::Utilities::Callable<void(const UIError &)> onError{};
};

/// @brief Type-erased selection behavior consumed by a radio button.
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

/// @brief Minimum, maximum, and step constraints for a slider.
struct SliderRange final {
  F32 minimum{0.0F};
  F32 maximum{1.0F};
  F32 step{0.1F};
};

/// @brief Determinate or indeterminate value presented by a progress bar.
struct ProgressValue final {
  F32 value{0.0F};
  F32 minimum{0.0F};
  F32 maximum{1.0F};
  bool indeterminate{false};
};

/// @brief Schedules, exposes, and dismisses a tooltip for an attached element.
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

  /// @brief Last pointer position used for popup placement.
  [[nodiscard]] auto Anchor() const noexcept -> Rect;

private:
  struct State;
  std::shared_ptr<State> m_state;
};
} // namespace NGIN::UI
