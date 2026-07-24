#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/UI/Geometry.hpp>
#include <NGIN/UI/Style.hpp>

#include <limits>
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

enum class HorizontalAlignment : UInt8 {
  Start,
  Center,
  End,
  Stretch,
};

enum class VerticalAlignment : UInt8 {
  Start,
  Center,
  End,
  Stretch,
};

struct LayoutProperties final {
  Size preferredSize{};
  Size minimumSize{};
  Size maximumSize{
      std::numeric_limits<F32>::infinity(),
      std::numeric_limits<F32>::infinity(),
  };
  Thickness padding{};
  F32 gap{0.0F};
  HorizontalAlignment horizontalAlignment{HorizontalAlignment::Stretch};
  VerticalAlignment verticalAlignment{VerticalAlignment::Stretch};
};

struct NodeProperties final {
  LayoutProperties layout{};
  Color background{};
  bool paintsBackground{false};
};

struct ElementDeclaration final {
  ElementType type{ElementType::Custom};
  NGIN::Text::String key{};
  NodeProperties properties{};
  std::vector<ElementDeclaration> children{};

  ElementDeclaration() = default;

  explicit ElementDeclaration(const ElementType elementType,
                              const std::string_view elementKey = {},
                              const NodeProperties &nodeProperties = {})
      : type(elementType), key(elementKey), properties(nodeProperties) {}

  [[nodiscard]] auto IsKeyed() const noexcept -> bool { return !key.Empty(); }
};
} // namespace NGIN::UI
