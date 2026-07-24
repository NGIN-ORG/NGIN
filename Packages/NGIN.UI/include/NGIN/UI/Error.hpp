#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/Utilities/Expected.hpp>

namespace NGIN::UI {
enum class UIErrorCode : UInt16 {
  InvalidArgument,
  InvalidState,
  WrongThread,
  BackendUnavailable,
  WindowCreationFailed,
  SurfaceCreationFailed,
  RenderFailed,
  ResourceFailed,
  TextShapingFailed,
  Unsupported,
  OutOfMemory,
};

struct UIError final {
  UIErrorCode code{UIErrorCode::InvalidState};
  NGIN::Text::String backend{};
  NGIN::Text::String operation{};
  NGIN::Text::String logicalResource{};
  Int32 nativeCode{0};
  NGIN::Text::String message{};
};

template <typename T> using UIResult = NGIN::Utilities::Expected<T, UIError>;

[[nodiscard]] inline auto
MakeUIError(const UIErrorCode code, const char *message,
            const char *backend = "", const char *operation = "",
            const char *logicalResource = "", const Int32 nativeCode = 0)
    -> UIError {
  return UIError{
      .code = code,
      .backend = NGIN::Text::String{backend},
      .operation = NGIN::Text::String{operation},
      .logicalResource = NGIN::Text::String{logicalResource},
      .nativeCode = nativeCode,
      .message = NGIN::Text::String{message},
  };
}
} // namespace NGIN::UI
