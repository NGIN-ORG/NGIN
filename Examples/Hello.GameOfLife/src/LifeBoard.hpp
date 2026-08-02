#pragma once

#include "LifeSimulation.hpp"

#include <NGIN/UI/CustomElement.hpp>
#include <NGIN/UI/Image.hpp>

#include <functional>
#include <memory>
#include <optional>

namespace Hello::GameOfLife {

class LifeBoardElement final : public NGIN::UI::ICustomElement {
public:
  using SetCell = std::function<void(NGIN::UIntSize, NGIN::UIntSize, bool)>;

  LifeBoardElement(const LifeSimulation &simulation,
                   std::shared_ptr<NGIN::UI::ImageResource> surface,
                   NGIN::UI::IImageResolver &resolver, SetCell setCell);

  [[nodiscard]] auto Measure(NGIN::UI::CustomElementContext &context,
                             NGIN::UI::SizeConstraints constraints)
      -> NGIN::UI::UIResult<NGIN::UI::Size> override;
  auto Paint(NGIN::UI::CustomElementContext &context,
             NGIN::UI::PaintContext &paint)
      -> NGIN::UI::UIResult<void> override;
  [[nodiscard]] auto Semantics(NGIN::UI::CustomElementContext &context)
      -> NGIN::UI::UIResult<NGIN::UI::SemanticProperties> override;
  auto PointerEvent(NGIN::UI::CustomElementContext &context,
                    NGIN::UI::RoutedPointerEvent &event)
      -> NGIN::UI::UIResult<NGIN::UI::InvalidationKind> override;
  auto KeyEvent(NGIN::UI::CustomElementContext &context,
                NGIN::UI::RoutedKeyEvent &event)
      -> NGIN::UI::UIResult<NGIN::UI::InvalidationKind> override;
  auto SemanticAction(NGIN::UI::CustomElementContext &context,
                      const NGIN::UI::SemanticActionRequest &request)
      -> NGIN::UI::UIResult<NGIN::UI::InvalidationKind> override;

private:
  enum class DragMode {
    None,
    Paint,
    Pan,
  };

  struct Cursor final {
    NGIN::UIntSize X{LifeSimulation::BoardWidth / 2};
    NGIN::UIntSize Y{LifeSimulation::BoardHeight / 2};
  };

  struct DragState final {
    DragMode Mode{DragMode::None};
    bool PaintAlive{false};
    NGIN::UIntSize LastCell{LifeSimulation::EntityCount};
    NGIN::UI::Point LastPointer{};
  };

  struct Viewport final {
    NGIN::F32 Zoom{1.0F};
    NGIN::F32 CenterX{0.5F};
    NGIN::F32 CenterY{0.5F};
  };

  [[nodiscard]] auto BoardBounds(NGIN::UI::Size extent) const noexcept
      -> NGIN::UI::Rect;
  [[nodiscard]] auto MiniMapBounds(NGIN::UI::Size extent) const noexcept
      -> NGIN::UI::Rect;
  [[nodiscard]] auto SourceRect(const Viewport &viewport) const noexcept
      -> NGIN::UI::Rect;
  [[nodiscard]] auto CellAt(NGIN::UI::Point local, NGIN::UI::Size extent,
                            const Viewport &viewport) const noexcept
      -> std::optional<Cursor>;
  void ClampViewport(Viewport &viewport) const noexcept;
  void PaintCell(const Cursor &cursor, bool alive) const;

  const LifeSimulation *m_simulation{nullptr};
  std::shared_ptr<NGIN::UI::ImageResource> m_surface{};
  NGIN::UI::IImageResolver *m_resolver{nullptr};
  SetCell m_setCell{};
};

} // namespace Hello::GameOfLife
