#include <NGIN/UI/Accessibility/Windows/Windows.hpp>

namespace NGIN::UI::Accessibility::Windows {
auto CreateAccessibilityBackend() noexcept
    -> std::unique_ptr<IAccessibilityBackend> {
  return {};
}
} // namespace NGIN::UI::Accessibility::Windows
