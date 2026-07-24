#pragma once

#include <NGIN/Primitives.hpp>

#include <compare>
#include <limits>

namespace NGIN::UI {
/// @brief Opaque, comparable identifier for a backend- or runtime-owned object.
template <typename Tag> struct Handle final {
  static constexpr UInt32 INVALID_INDEX = std::numeric_limits<UInt32>::max();

  UInt32 index{INVALID_INDEX};
  UInt32 generation{0};

  [[nodiscard]] constexpr auto IsValid() const noexcept -> bool {
    return index != INVALID_INDEX && generation != 0;
  }

  constexpr explicit operator bool() const noexcept { return IsValid(); }

  [[nodiscard]] constexpr auto
  operator<=>(const Handle &) const noexcept = default;
};

/// @brief Type tag for opaque native window handles.
struct PlatformWindowHandleTag;
/// @brief Type tag for opaque render-surface handles.
struct RenderSurfaceHandleTag;
/// @brief Type tag for opaque renderer texture handles.
struct TextureHandleTag;
/// @brief Type tag for opaque font-face handles.
struct FontFaceHandleTag;
/// @brief Type tag for opaque runtime element handles.
struct ElementHandleTag;

/// @brief Opaque identifier of a native platform window.
using PlatformWindowHandle = Handle<PlatformWindowHandleTag>;
/// @brief Opaque identifier of a renderer surface.
using RenderSurfaceHandle = Handle<RenderSurfaceHandleTag>;
/// @brief Opaque identifier of a renderer texture.
using TextureHandle = Handle<TextureHandleTag>;
/// @brief Opaque identifier of a resolved font face.
using FontFaceHandle = Handle<FontFaceHandleTag>;
/// @brief Opaque identifier of a retained runtime element.
using ElementHandle = Handle<ElementHandleTag>;

/// @brief Stable runtime identity combining an element slot and generation.
struct ElementId final {
  UInt64 value{0};

  [[nodiscard]] constexpr auto IsValid() const noexcept -> bool {
    return value != 0;
  }

  [[nodiscard]] constexpr auto
  operator<=>(const ElementId &) const noexcept = default;
};
} // namespace NGIN::UI
