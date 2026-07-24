#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Text/String.hpp>

#include <string_view>
#include <vector>

namespace NGIN::UI {
enum class ElementType : UInt16 {
  Root,
  Column,
  Row,
  Overlay,
  Padding,
  Spacer,
  Rectangle,
  Text,
  Button,
  TextField,
  Custom,
};

struct ElementDeclaration final {
  ElementType type{ElementType::Custom};
  NGIN::Text::String key{};
  std::vector<ElementDeclaration> children{};

  ElementDeclaration() = default;

  explicit ElementDeclaration(const ElementType elementType,
                              const std::string_view elementKey = {})
      : type(elementType), key(elementKey) {}

  [[nodiscard]] auto IsKeyed() const noexcept -> bool { return !key.Empty(); }
};
} // namespace NGIN::UI
