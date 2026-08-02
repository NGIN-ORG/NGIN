#include "LifeBoard.hpp"

#include <NGIN/Text/String.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace Hello::GameOfLife {
namespace {
using namespace NGIN::UI;

constexpr Color BoardBackground{0.018F, 0.025F, 0.05F, 1.0F};
constexpr Color MiniMapBorder{0.28F, 0.72F, 0.95F, 0.95F};
constexpr Color ViewportBorder{1.0F, 0.78F, 0.28F, 1.0F};
constexpr Color CursorColor{1.0F, 0.9F, 0.42F, 1.0F};
constexpr NGIN::F32 MiniMapSize = 136.0F;
constexpr NGIN::F32 MiniMapMargin = 14.0F;
} // namespace

LifeBoardElement::LifeBoardElement(const LifeSimulation &simulation,
                                   std::shared_ptr<ImageResource> surface,
                                   IImageResolver &resolver, SetCell setCell)
    : m_simulation(&simulation), m_surface(std::move(surface)),
      m_resolver(&resolver), m_setCell(std::move(setCell)) {}

auto LifeBoardElement::Measure(CustomElementContext &,
                               const SizeConstraints constraints)
    -> UIResult<Size> {
  return constraints.Constrain(Size{760.0F, 580.0F});
}

auto LifeBoardElement::Paint(CustomElementContext &context, PaintContext &paint)
    -> UIResult<void> {
  auto viewport = context.State<Viewport>("viewport");
  if (!viewport) {
    return viewport.Error();
  }
  ClampViewport(*viewport.Value());
  const auto board = BoardBounds(paint.Extent());
  const auto source = SourceRect(*viewport.Value());
  paint.FillRounded(paint.Bounds(), CornerRadius::Uniform(Dp{12.0F}),
                    BoardBackground);

  auto resolved = m_resolver->Resolve(m_surface);
  if (!resolved) {
    return resolved.Error();
  }
  if (resolved.Value().state == ImageLoadState::Ready &&
      resolved.Value().texture) {
    paint.ImageRegion(resolved.Value().texture, board, source);

    const auto miniMap = MiniMapBounds(paint.Extent());
    paint.FillRounded(Rect{miniMap.x - 4.0F, miniMap.y - 4.0F,
                           miniMap.width + 8.0F, miniMap.height + 8.0F},
                      CornerRadius::Uniform(Dp{6.0F}), BoardBackground);
    paint.Image(resolved.Value().texture, miniMap);
    paint.StrokeRounded(miniMap, CornerRadius::Uniform(Dp{3.0F}), 1.5F,
                        MiniMapBorder);
    paint.Stroke(Rect{miniMap.x + source.x * miniMap.width,
                      miniMap.y + source.y * miniMap.height,
                      source.width * miniMap.width,
                      source.height * miniMap.height},
                 2.0F, ViewportBorder);
  }

  if (const auto *cursor = context.FindState<Cursor>("cursor");
      cursor != nullptr && viewport.Value()->Zoom >= 8.0F) {
    const auto x = static_cast<NGIN::F32>(cursor->X) / m_simulation->Width();
    const auto y = static_cast<NGIN::F32>(cursor->Y) / m_simulation->Height();
    if (x >= source.x && y >= source.y && x < source.x + source.width &&
        y < source.y + source.height) {
      const auto cellWidth = board.width * viewport.Value()->Zoom /
                             static_cast<NGIN::F32>(m_simulation->Width());
      const auto cellHeight = board.height * viewport.Value()->Zoom /
                              static_cast<NGIN::F32>(m_simulation->Height());
      paint.Stroke(Rect{board.x + (x - source.x) / source.width * board.width,
                        board.y + (y - source.y) / source.height * board.height,
                        std::max(1.0F, cellWidth), std::max(1.0F, cellHeight)},
                   1.5F, CursorColor);
    }
  }
  return {};
}

auto LifeBoardElement::Semantics(CustomElementContext &context)
    -> UIResult<SemanticProperties> {
  auto value = std::to_string(m_simulation->Population());
  value += " living cells at generation ";
  value += std::to_string(m_simulation->Generation());
  if (const auto *viewport = context.FindState<Viewport>("viewport")) {
    value += ", zoom ";
    value += std::to_string(static_cast<int>(std::round(viewport->Zoom)));
    value += " times";
  }
  return SemanticProperties{
      .role = SemanticRole::Image,
      .label = NGIN::Text::String{"Million-cell Life universe"},
      .value = NGIN::Text::String{value.c_str()},
      .description =
          NGIN::Text::String{
              "Wheel to zoom, middle or right drag to pan, primary drag to "
              "paint, and click the minimap to navigate"},
      .actions = SemanticActionFlags::Activate | SemanticActionFlags::Focus,
  };
}

auto LifeBoardElement::PointerEvent(CustomElementContext &context,
                                    RoutedPointerEvent &event)
    -> UIResult<InvalidationKind> {
  if (event.phase != EventPhase::Target) {
    return InvalidationKind::None;
  }
  auto drag = context.State<DragState>("drag");
  auto cursor = context.State<Cursor>("cursor");
  auto viewport = context.State<Viewport>("viewport");
  if (!drag) {
    return drag.Error();
  }
  if (!cursor) {
    return cursor.Error();
  }
  if (!viewport) {
    return viewport.Error();
  }

  const auto local = context.ToLocal(event.position);
  const auto board = BoardBounds(context.ArrangedSize());
  if (event.eventKind == RoutedPointerEventKind::Wheel &&
      board.Contains(local)) {
    const auto oldSource = SourceRect(*viewport.Value());
    const auto anchorX =
        std::clamp((local.x - board.x) / board.width, 0.0F, 1.0F);
    const auto anchorY =
        std::clamp((local.y - board.y) / board.height, 0.0F, 1.0F);
    const auto worldX = oldSource.x + anchorX * oldSource.width;
    const auto worldY = oldSource.y + anchorY * oldSource.height;
    const auto wheel = std::clamp(event.wheelDelta.y, -4.0F, 4.0F);
    viewport.Value()->Zoom = std::clamp(
        viewport.Value()->Zoom * std::pow(1.32F, wheel), 1.0F, 128.0F);
    const auto span = 1.0F / viewport.Value()->Zoom;
    viewport.Value()->CenterX = worldX - (anchorX - 0.5F) * span;
    viewport.Value()->CenterY = worldY - (anchorY - 0.5F) * span;
    ClampViewport(*viewport.Value());
    event.Handle();
    return InvalidationKind::Paint | InvalidationKind::Semantics;
  }

  if (event.eventKind == RoutedPointerEventKind::ButtonPressed &&
      event.button == PointerButton::Primary) {
    const auto miniMap = MiniMapBounds(context.ArrangedSize());
    if (miniMap.Contains(local)) {
      viewport.Value()->CenterX =
          std::clamp((local.x - miniMap.x) / miniMap.width, 0.0F, 1.0F);
      viewport.Value()->CenterY =
          std::clamp((local.y - miniMap.y) / miniMap.height, 0.0F, 1.0F);
      viewport.Value()->Zoom = std::max(4.0F, viewport.Value()->Zoom);
      ClampViewport(*viewport.Value());
      event.Handle();
      return InvalidationKind::Paint | InvalidationKind::Semantics;
    }
  }

  if (event.eventKind == RoutedPointerEventKind::ButtonPressed &&
      (event.button == PointerButton::Middle ||
       event.button == PointerButton::Secondary)) {
    drag.Value()->Mode = DragMode::Pan;
    drag.Value()->LastPointer = local;
    event.CapturePointer();
    event.Handle();
    return InvalidationKind::Paint;
  }

  if (event.eventKind == RoutedPointerEventKind::ButtonPressed &&
      event.button == PointerButton::Primary) {
    const auto cell = CellAt(local, context.ArrangedSize(), *viewport.Value());
    if (!cell) {
      return InvalidationKind::None;
    }
    *cursor.Value() = *cell;
    drag.Value()->Mode = DragMode::Paint;
    drag.Value()->PaintAlive = !m_simulation->IsAlive(cell->X, cell->Y);
    drag.Value()->LastCell = cell->Y * m_simulation->Width() + cell->X;
    PaintCell(*cell, drag.Value()->PaintAlive);
    event.CapturePointer();
    event.Handle();
    return InvalidationKind::Paint | InvalidationKind::Semantics;
  }

  if (event.eventKind == RoutedPointerEventKind::Moved &&
      drag.Value()->Mode == DragMode::Pan) {
    const auto source = SourceRect(*viewport.Value());
    viewport.Value()->CenterX -=
        (local.x - drag.Value()->LastPointer.x) / board.width * source.width;
    viewport.Value()->CenterY -=
        (local.y - drag.Value()->LastPointer.y) / board.height * source.height;
    ClampViewport(*viewport.Value());
    drag.Value()->LastPointer = local;
    event.Handle();
    return InvalidationKind::Paint;
  }

  if (event.eventKind == RoutedPointerEventKind::Moved &&
      drag.Value()->Mode == DragMode::Paint) {
    const auto cell = CellAt(local, context.ArrangedSize(), *viewport.Value());
    if (cell) {
      const auto index = cell->Y * m_simulation->Width() + cell->X;
      if (index != drag.Value()->LastCell) {
        *cursor.Value() = *cell;
        drag.Value()->LastCell = index;
        PaintCell(*cell, drag.Value()->PaintAlive);
      }
    }
    event.Handle();
    return InvalidationKind::Paint | InvalidationKind::Semantics;
  }

  if (event.eventKind == RoutedPointerEventKind::ButtonReleased &&
      drag.Value()->Mode != DragMode::None) {
    drag.Value()->Mode = DragMode::None;
    drag.Value()->LastCell = LifeSimulation::EntityCount;
    event.ReleasePointerCapture();
    event.Handle();
    return InvalidationKind::Paint;
  }
  return InvalidationKind::None;
}

auto LifeBoardElement::KeyEvent(CustomElementContext &context,
                                RoutedKeyEvent &event)
    -> UIResult<InvalidationKind> {
  if (event.phase != EventPhase::Target || event.state == KeyState::Released) {
    return InvalidationKind::None;
  }
  auto cursor = context.State<Cursor>("cursor");
  auto viewport = context.State<Viewport>("viewport");
  if (!cursor) {
    return cursor.Error();
  }
  if (!viewport) {
    return viewport.Error();
  }
  switch (event.logicalKey) {
  case LogicalKey::Left:
    cursor.Value()->X =
        (cursor.Value()->X + m_simulation->Width() - 1) % m_simulation->Width();
    break;
  case LogicalKey::Right:
    cursor.Value()->X = (cursor.Value()->X + 1) % m_simulation->Width();
    break;
  case LogicalKey::Up:
    cursor.Value()->Y = (cursor.Value()->Y + m_simulation->Height() - 1) %
                        m_simulation->Height();
    break;
  case LogicalKey::Down:
    cursor.Value()->Y = (cursor.Value()->Y + 1) % m_simulation->Height();
    break;
  case LogicalKey::Space:
    PaintCell(*cursor.Value(),
              !m_simulation->IsAlive(cursor.Value()->X, cursor.Value()->Y));
    event.Handle();
    return InvalidationKind::Paint | InvalidationKind::Semantics;
  case LogicalKey::Home:
    *viewport.Value() = Viewport{};
    event.Handle();
    return InvalidationKind::Paint | InvalidationKind::Semantics;
  default:
    return InvalidationKind::None;
  }
  event.Handle();
  return InvalidationKind::Paint;
}

auto LifeBoardElement::SemanticAction(CustomElementContext &context,
                                      const SemanticActionRequest &request)
    -> UIResult<InvalidationKind> {
  if (request.action != SemanticActionKind::Activate) {
    return InvalidationKind::None;
  }
  auto cursor = context.State<Cursor>("cursor");
  if (!cursor) {
    return cursor.Error();
  }
  PaintCell(*cursor.Value(),
            !m_simulation->IsAlive(cursor.Value()->X, cursor.Value()->Y));
  return InvalidationKind::Paint | InvalidationKind::Semantics;
}

auto LifeBoardElement::BoardBounds(const Size extent) const noexcept -> Rect {
  const auto size = std::max(0.0F, std::min(extent.width, extent.height));
  return Rect{(extent.width - size) * 0.5F, (extent.height - size) * 0.5F, size,
              size};
}

auto LifeBoardElement::MiniMapBounds(const Size extent) const noexcept -> Rect {
  const auto size =
      std::min(MiniMapSize,
               std::max(48.0F, std::min(extent.width, extent.height) * 0.28F));
  return Rect{extent.width - size - MiniMapMargin, MiniMapMargin, size, size};
}

auto LifeBoardElement::SourceRect(const Viewport &viewport) const noexcept
    -> Rect {
  const auto span = 1.0F / std::clamp(viewport.Zoom, 1.0F, 128.0F);
  return Rect{viewport.CenterX - span * 0.5F, viewport.CenterY - span * 0.5F,
              span, span};
}

auto LifeBoardElement::CellAt(const Point local, const Size extent,
                              const Viewport &viewport) const noexcept
    -> std::optional<Cursor> {
  const auto board = BoardBounds(extent);
  if (!board.Contains(local) || board.width <= 0.0F || board.height <= 0.0F) {
    return std::nullopt;
  }
  const auto source = SourceRect(viewport);
  const auto worldX =
      source.x + (local.x - board.x) / board.width * source.width;
  const auto worldY =
      source.y + (local.y - board.y) / board.height * source.height;
  const auto x = static_cast<NGIN::UIntSize>(
      std::floor(worldX * static_cast<NGIN::F32>(m_simulation->Width())));
  const auto y = static_cast<NGIN::UIntSize>(
      std::floor(worldY * static_cast<NGIN::F32>(m_simulation->Height())));
  if (x >= m_simulation->Width() || y >= m_simulation->Height()) {
    return std::nullopt;
  }
  return Cursor{.X = x, .Y = y};
}

void LifeBoardElement::ClampViewport(Viewport &viewport) const noexcept {
  viewport.Zoom = std::clamp(viewport.Zoom, 1.0F, 128.0F);
  const auto halfSpan = 0.5F / viewport.Zoom;
  viewport.CenterX = std::clamp(viewport.CenterX, halfSpan, 1.0F - halfSpan);
  viewport.CenterY = std::clamp(viewport.CenterY, halfSpan, 1.0F - halfSpan);
}

void LifeBoardElement::PaintCell(const Cursor &cursor, const bool alive) const {
  if (m_setCell) {
    m_setCell(cursor.X, cursor.Y, alive);
  }
}

} // namespace Hello::GameOfLife
