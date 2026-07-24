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
