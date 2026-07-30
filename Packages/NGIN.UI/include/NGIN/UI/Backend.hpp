#pragma once

#include <NGIN/Primitives.hpp>

#include <compare>

namespace NGIN::UI {
/// @brief Version negotiated between NGIN.UI and a platform or render backend.
struct BackendContractVersion final {
  UInt16 major{1};
  UInt16 minor{0};

  [[nodiscard]] constexpr auto
  Supports(const BackendContractVersion required) const noexcept -> bool {
    return major == required.major && minor >= required.minor;
  }

  [[nodiscard]] constexpr auto
  operator<=>(const BackendContractVersion &) const noexcept = default;
};

inline constexpr BackendContractVersion CurrentBackendContractVersion{1, 1};

/// @brief Optional facilities advertised by a platform backend.
enum class PlatformCapabilityFlags : UInt32 {
  None = 0,
  Clipboard = 1U << 0U,
  IME = 1U << 1U,
  MultipleWindows = 1U << 2U,
  FileDrop = 1U << 3U,
  PenInput = 1U << 4U,
  TouchInput = 1U << 5U,
  NativeDialogs = 1U << 6U,
  NativeWindow = 1U << 7U,
};

/// @brief Optional facilities advertised by a render backend.
enum class RenderCapabilityFlags : UInt32 {
  None = 0,
  TextureUpdates = 1U << 0U,
  ScissorRects = 1U << 1U,
  Index32 = 1U << 2U,
  OffscreenTargets = 1U << 3U,
  DeviceLossRecovery = 1U << 4U,
};

[[nodiscard]] constexpr auto
operator|(const PlatformCapabilityFlags left,
          const PlatformCapabilityFlags right) noexcept
    -> PlatformCapabilityFlags {
  return static_cast<PlatformCapabilityFlags>(static_cast<UInt32>(left) |
                                              static_cast<UInt32>(right));
}

[[nodiscard]] constexpr auto
HasPlatformCapability(const PlatformCapabilityFlags value,
                      const PlatformCapabilityFlags capability) noexcept
    -> bool {
  return (static_cast<UInt32>(value) & static_cast<UInt32>(capability)) ==
         static_cast<UInt32>(capability);
}

[[nodiscard]] constexpr auto
operator|(const RenderCapabilityFlags left,
          const RenderCapabilityFlags right) noexcept -> RenderCapabilityFlags {
  return static_cast<RenderCapabilityFlags>(static_cast<UInt32>(left) |
                                            static_cast<UInt32>(right));
}

[[nodiscard]] constexpr auto
HasRenderCapability(const RenderCapabilityFlags value,
                    const RenderCapabilityFlags capability) noexcept -> bool {
  return (static_cast<UInt32>(value) & static_cast<UInt32>(capability)) ==
         static_cast<UInt32>(capability);
}

inline constexpr auto RequiredRenderCapabilities =
    RenderCapabilityFlags::ScissorRects | RenderCapabilityFlags::Index32;
inline constexpr auto RequiredPlatformCapabilities =
    PlatformCapabilityFlags::MultipleWindows;
} // namespace NGIN::UI
