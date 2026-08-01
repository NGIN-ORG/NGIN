#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/UI/CustomElement.hpp>
#include <NGIN/UI/Geometry.hpp>
#include <NGIN/UI/Image.hpp>
#include <NGIN/UI/LayoutPrimitives.hpp>
#include <NGIN/UI/Resources.hpp>
#include <NGIN/UI/RoutedEvent.hpp>
#include <NGIN/UI/Semantics.hpp>
#include <NGIN/UI/State.hpp>
#include <NGIN/UI/Style.hpp>
#include <NGIN/UI/TextEditing.hpp>
#include <NGIN/Utilities/Callable.hpp>

#include <limits>
#include <string_view>
#include <vector>

namespace NGIN::UI {
/// @brief Built-in node kind used by declarations and runtime nodes.
enum class ElementType : UInt16 {
  Root,
  Column,
  Row,
  Grid,
  WrapPanel,
  Canvas,
  Overlay,
  Padding,
  Border,
  Spacer,
  Separator,
  Rectangle,
  Text,
  Button,
  TextField,
  TextArea,
  Image,
  ScrollView,
  ListView,
  ListItem,
  Tab,
  MenuItem,
  Popup,
  Custom,
  CustomElement,
};

/// @brief Determines whether an element participates in layout and painting.
enum class ElementVisibility : UInt8 {
  Visible,
  Hidden,
  Collapsed,
};

/// @brief Horizontal placement within the space offered by a parent.
enum class HorizontalAlignment : UInt8 {
  Start,
  Center,
  End,
  Stretch,
};

/// @brief Vertical placement within the space offered by a parent.
enum class VerticalAlignment : UInt8 {
  Start,
  Center,
  End,
  Stretch,
};

/// @brief Authored sizing, spacing, alignment, and flex layout properties.
struct LayoutProperties final {
  Size preferredSize{};
  Size minimumSize{};
  Size maximumSize{
      std::numeric_limits<F32>::infinity(),
      std::numeric_limits<F32>::infinity(),
  };
  Thickness padding{};
  F32 gap{0.0F};
  F32 flexGrow{0.0F};
  F32 flexShrink{0.0F};
  HorizontalAlignment horizontalAlignment{HorizontalAlignment::Stretch};
  VerticalAlignment verticalAlignment{VerticalAlignment::Stretch};
};

/// @brief Authored focus, hit-testing, command, and routed-input behavior.
struct InteractionProperties final {
  bool hitTestVisible{true};
  bool enabled{true};
  bool focusable{false};
  Int32 tabIndex{0};
  NGIN::Utilities::Callable<void(RoutedPointerEvent &)> onPointer{};
  NGIN::Utilities::Callable<void(RoutedKeyEvent &)> onKey{};
  NGIN::Utilities::Callable<void(RoutedTextEvent &)> onText{};
  NGIN::Utilities::Callable<void()> onActivate{};
};

/// @brief Scrolling axes, offsets, extents, and scrollbar policy.
struct ScrollProperties final {
  bool horizontal{false};
  bool vertical{true};
  F32 wheelStep{40.0F};
  bool showScrollbars{true};
  F32 scrollbarThickness{8.0F};
  F32 minimumThumbLength{24.0F};
  Color scrollbarTrack{0.08F, 0.09F, 0.11F, 0.72F};
  Color scrollbarThumb{0.42F, 0.45F, 0.52F, 0.9F};
  Color scrollbarThumbHovered{0.58F, 0.62F, 0.7F, 1.0F};
};

/// @brief Preferred placement of a popup relative to its anchor.
enum class PopupPlacement : UInt8 {
  Center,
  BelowStart,
  BelowEnd,
  AboveStart,
  AboveEnd,
};

/// @brief Direction in which a separator extends.
enum class SeparatorOrientation : UInt8 {
  Horizontal,
  Vertical,
};

/// @brief Authored separator orientation.
struct SeparatorProperties final {
  SeparatorOrientation orientation{SeparatorOrientation::Horizontal};
};

/// @brief Anchor, placement, modality, and dismissal behavior for a popup.
struct PopupProperties final {
  Rect anchor{};
  NGIN::Text::String anchorIdentifier{};
  PopupPlacement placement{PopupPlacement::Center};
  F32 gap{4.0F};
  bool modal{true};
  bool dismissOnOutsidePointer{true};
  bool dismissOnEscape{true};
  NGIN::Utilities::Callable<void()> onDismiss{};
};

/// @brief Editing behavior and optional text binding for a text input.
struct TextFieldProperties final {
  Binding<NGIN::Text::String> value{};
  IGraphemeSegmenter *graphemeSegmenter{nullptr};
  NGIN::Utilities::Callable<void(const UIError &)> onError{};
  Color selectionColor{0.2F, 0.45F, 0.85F, 0.45F};
  Color caretColor{1.0F, 1.0F, 1.0F, 1.0F};
  Color compositionColor{0.4F, 0.7F, 1.0F, 1.0F};
  F32 caretWidth{1.0F};
  bool readOnly{false};
  bool password{false};
};

/// @brief Text layout, wrapping, alignment, and typography properties.
struct TextElementProperties final {
  NGIN::Text::String value{};
  FontRequest font{};
  F32 fontSize{14.0F};
  TextDirection direction{TextDirection::Automatic};
  NGIN::Text::String language{};
  NGIN::Text::String script{};
  F32 lineHeight{0.0F};
  TextAlignment alignment{TextAlignment::Start};
  TextWrapping wrapping{TextWrapping::Wrap};
  Color color{1.0F, 1.0F, 1.0F, 1.0F};
  ITextLayout *layout{nullptr};
  ITextGeometry *geometry{nullptr};
  IGlyphAtlas *glyphAtlas{nullptr};
  NGIN::Utilities::Callable<void(const UIError &)> onError{};
  bool clip{true};
};

/// @brief Resource, fit, alignment, tint, and accessibility data for an image.
struct ImageProperties final {
  std::shared_ptr<ImageResource> resource{};
  IImageResolver *resolver{nullptr};
  ImageFit fit{ImageFit::Contain};
  ImageAlignment alignment{};
  Color tint{1.0F, 1.0F, 1.0F, 1.0F};
  bool clip{true};
  NGIN::Utilities::Callable<void(const UIError &)> onError{};
};

/// @brief Custom-element implementation and its authored interaction data.
struct CustomElementProperties final {
  std::shared_ptr<ICustomElement> element{};
  NGIN::Utilities::Callable<void(const UIError &)> onError{};
};

/// @brief Complete authored property set shared by declarative elements.
struct NodeProperties final {
  ElementVisibility visibility{ElementVisibility::Visible};
  LayoutProperties layout{};
  GridProperties grid{};
  GridPlacement gridPlacement{};
  WrapPanelProperties wrapPanel{};
  CanvasProperties canvas{};
  CanvasPlacement canvasPlacement{};
  InteractionProperties interaction{};
  ScrollProperties scroll{};
  PopupProperties popup{};
  SeparatorProperties separator{};
  TextFieldProperties textField{};
  TextElementProperties text{};
  ImageProperties image{};
  CustomElementProperties custom{};
  SemanticProperties semantics{};
  std::shared_ptr<const ResourceScope> resources{};
  VisualProperties visual{};
  Color background{};
  bool paintsBackground{false};
};

/// @brief One keyed node in the ephemeral declarative composition tree.
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
