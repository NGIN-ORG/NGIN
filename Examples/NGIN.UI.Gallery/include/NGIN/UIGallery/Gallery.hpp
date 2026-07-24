#pragma once

#include <NGIN/UI/UI.hpp>

#include <cstdint>

namespace NGIN::UIGallery {
class Model final {
public:
  Model();

  void AttachWindow(UI::Window &window) noexcept;
  void Activate() noexcept;

  UI::State<Text::String> name;
  std::uint32_t activationCount{0};

private:
  UI::Window *m_window{nullptr};
};

void ComposeMainView(UI::Composer &composer, UI::NativeTextSystem &text,
                     Model &model);

[[nodiscard]] auto CreateMainWindow(UI::Application &application,
                                    UI::NativeTextSystem &text, Model &model)
    -> UI::UIResult<UI::Window *>;
} // namespace NGIN::UIGallery
