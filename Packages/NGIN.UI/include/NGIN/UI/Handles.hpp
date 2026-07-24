#pragma once

#include <NGIN/Primitives.hpp>

#include <compare>
#include <limits>

namespace NGIN::UI {
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

struct PlatformWindowHandleTag;
struct RenderSurfaceHandleTag;
struct TextureHandleTag;
struct ElementHandleTag;

using PlatformWindowHandle = Handle<PlatformWindowHandleTag>;
using RenderSurfaceHandle = Handle<RenderSurfaceHandleTag>;
using TextureHandle = Handle<TextureHandleTag>;
using ElementHandle = Handle<ElementHandleTag>;

struct ElementId final {
  UInt64 value{0};

  [[nodiscard]] constexpr auto IsValid() const noexcept -> bool {
    return value != 0;
  }

  [[nodiscard]] constexpr auto
  operator<=>(const ElementId &) const noexcept = default;
};
} // namespace NGIN::UI
