#include <NGIN/UI/Composer.hpp>

#include <utility>

namespace NGIN::UI {
Composer::ElementScope::ElementScope(Composer &composer,
                                     const UIntSize depth) noexcept
    : m_composer(&composer), m_depth(depth) {}

Composer::ElementScope::ElementScope(ElementScope &&other) noexcept
    : m_composer(std::exchange(other.m_composer, nullptr)),
      m_depth(other.m_depth) {}

auto Composer::ElementScope::operator=(ElementScope &&other) noexcept
    -> ElementScope & {
  if (this != &other) {
    Close();
    m_composer = std::exchange(other.m_composer, nullptr);
    m_depth = other.m_depth;
  }
  return *this;
}

Composer::ElementScope::~ElementScope() { Close(); }

void Composer::ElementScope::Close() noexcept {
  if (m_composer != nullptr) {
    m_composer->End(m_depth);
    m_composer = nullptr;
  }
}

auto Composer::Begin(const ElementType type, const std::string_view key)
    -> ElementScope {
  return Begin(type, NodeProperties{}, key);
}

auto Composer::Begin(const ElementType type, const NodeProperties &properties,
                     const std::string_view key) -> ElementScope {
  auto &children = CurrentChildren();
  children.emplace_back(type, key, properties);
  auto *element = &children.back();
  m_stack.push_back(element);
  return ElementScope{*this, m_stack.size()};
}

void Composer::Leaf(const ElementType type, const std::string_view key) {
  Leaf(type, NodeProperties{}, key);
}

void Composer::Leaf(const ElementType type, const NodeProperties &properties,
                    const std::string_view key) {
  CurrentChildren().emplace_back(type, key, properties);
}

void Composer::Button(NGIN::Utilities::Callable<void()> onActivate,
                      const NodeProperties &properties,
                      const std::string_view key) {
  auto buttonProperties = properties;
  buttonProperties.interaction.focusable = true;
  buttonProperties.interaction.onActivate = std::move(onActivate);
  if (buttonProperties.semantics.role == SemanticRole::None) {
    buttonProperties.semantics.role = SemanticRole::Button;
  }
  buttonProperties.semantics.actions = buttonProperties.semantics.actions |
                                       SemanticActionFlags::Activate |
                                       SemanticActionFlags::Focus;
  Leaf(ElementType::Button, buttonProperties, key);
}

void Composer::TextField(Binding<NGIN::Text::String> value,
                         IGraphemeSegmenter &graphemeSegmenter,
                         const NodeProperties &properties,
                         const std::string_view key) {
  auto textFieldProperties = properties;
  textFieldProperties.interaction.focusable = true;
  textFieldProperties.textField.value = std::move(value);
  textFieldProperties.textField.graphemeSegmenter = &graphemeSegmenter;
  if (textFieldProperties.semantics.role == SemanticRole::None) {
    textFieldProperties.semantics.role = SemanticRole::TextBox;
  }
  textFieldProperties.semantics.actions =
      textFieldProperties.semantics.actions | SemanticActionFlags::Focus |
      SemanticActionFlags::SetValue;
  if (textFieldProperties.textField.password) {
    textFieldProperties.semantics.value = {};
  }
  Leaf(ElementType::TextField, textFieldProperties, key);
}

void Composer::TextArea(Binding<NGIN::Text::String> value,
                        IGraphemeSegmenter &graphemeSegmenter,
                        const NodeProperties &properties,
                        const std::string_view key) {
  auto textAreaProperties = properties;
  textAreaProperties.interaction.focusable = true;
  textAreaProperties.textField.value = std::move(value);
  textAreaProperties.textField.graphemeSegmenter = &graphemeSegmenter;
  textAreaProperties.scroll.horizontal =
      textAreaProperties.text.wrapping == TextWrapping::NoWrap;
  textAreaProperties.scroll.vertical = true;
  if (textAreaProperties.semantics.role == SemanticRole::None) {
    textAreaProperties.semantics.role = SemanticRole::TextBox;
  }
  textAreaProperties.semantics.actions = textAreaProperties.semantics.actions |
                                         SemanticActionFlags::Focus |
                                         SemanticActionFlags::SetValue;
  Leaf(ElementType::TextArea, textAreaProperties, key);
}

void Composer::Text(NGIN::Text::String value, ITextLayout &layout,
                    IGlyphAtlas &glyphAtlas, const NodeProperties &properties,
                    const std::string_view key) {
  auto textProperties = properties;
  textProperties.text.value = std::move(value);
  textProperties.text.layout = &layout;
  textProperties.text.glyphAtlas = &glyphAtlas;
  if (textProperties.semantics.role == SemanticRole::None) {
    textProperties.semantics.role = SemanticRole::Text;
  }
  if (textProperties.semantics.value.Empty()) {
    textProperties.semantics.value = textProperties.text.value;
  }
  Leaf(ElementType::Text, textProperties, key);
}

void Composer::Image(std::shared_ptr<ImageResource> resource,
                     IImageResolver &resolver, NGIN::Text::String description,
                     const NodeProperties &properties,
                     const std::string_view key) {
  auto imageProperties = properties;
  imageProperties.image.resource = std::move(resource);
  imageProperties.image.resolver = &resolver;
  if (imageProperties.semantics.role == SemanticRole::None) {
    imageProperties.semantics.role = SemanticRole::Image;
  }
  if (imageProperties.semantics.description.Empty()) {
    imageProperties.semantics.description = std::move(description);
  }
  Leaf(ElementType::Image, imageProperties, key);
}

void Composer::Separator(const SeparatorOrientation orientation,
                         const NodeProperties &properties,
                         const std::string_view key) {
  auto separatorProperties = properties;
  separatorProperties.separator.orientation = orientation;
  if (orientation == SeparatorOrientation::Horizontal) {
    if (separatorProperties.layout.preferredSize.height <= 0.0F) {
      separatorProperties.layout.preferredSize.height = 1.0F;
    }
  } else if (separatorProperties.layout.preferredSize.width <= 0.0F) {
    separatorProperties.layout.preferredSize.width = 1.0F;
  }
  Leaf(ElementType::Separator, separatorProperties, key);
}

void Composer::Custom(std::shared_ptr<ICustomElement> element,
                      const NodeProperties &properties,
                      const std::string_view key) {
  auto customProperties = properties;
  customProperties.custom.element = std::move(element);
  Leaf(ElementType::CustomElement, customProperties, key);
}

auto Composer::Declarations() const noexcept
    -> const std::vector<ElementDeclaration> & {
  return m_roots;
}

auto Composer::IsBalanced() const noexcept -> bool { return m_stack.empty(); }

void Composer::End(const UIntSize depth) noexcept {
  if (depth == m_stack.size() && !m_stack.empty()) {
    m_stack.pop_back();
  }
}

auto Composer::CurrentChildren() noexcept -> std::vector<ElementDeclaration> & {
  return m_stack.empty() ? m_roots : m_stack.back()->children;
}
} // namespace NGIN::UI
