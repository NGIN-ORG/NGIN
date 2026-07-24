#pragma once

#include <NGIN/UI/Element.hpp>

#include <string_view>
#include <utility>
#include <vector>

namespace NGIN::UI {
class Composer final {
public:
  class ElementScope final {
  public:
    ElementScope(const ElementScope &) = delete;
    ElementScope(ElementScope &&other) noexcept;
    auto operator=(const ElementScope &) -> ElementScope & = delete;
    auto operator=(ElementScope &&other) noexcept -> ElementScope &;
    ~ElementScope();

  private:
    friend class Composer;

    ElementScope(Composer &composer, UIntSize depth) noexcept;
    void Close() noexcept;

    Composer *m_composer{nullptr};
    UIntSize m_depth{0};
  };

  Composer() = default;

  Composer(const Composer &) = delete;
  Composer(Composer &&) = delete;
  auto operator=(const Composer &) -> Composer & = delete;
  auto operator=(Composer &&) -> Composer & = delete;
  ~Composer() = default;

  [[nodiscard]] auto Begin(ElementType type, std::string_view key = {})
      -> ElementScope;
  [[nodiscard]] auto Begin(ElementType type, const NodeProperties &properties,
                           std::string_view key = {}) -> ElementScope;
  void Leaf(ElementType type, std::string_view key = {});
  void Leaf(ElementType type, const NodeProperties &properties,
            std::string_view key = {});
  void Button(NGIN::Utilities::Callable<void()> onActivate,
              const NodeProperties &properties = {}, std::string_view key = {});

  template <typename ComposeChildren>
  void Scope(std::shared_ptr<const ResourceScope> resources,
             ComposeChildren &&composeChildren, std::string_view key = {}) {
    NodeProperties properties{};
    properties.resources = std::move(resources);
    Element(ElementType::Custom, properties,
            std::forward<ComposeChildren>(composeChildren), key);
  }

  template <typename ComposeChildren>
  void Element(ElementType type, ComposeChildren &&composeChildren,
               std::string_view key = {}) {
    auto scope = Begin(type, key);
    std::forward<ComposeChildren>(composeChildren)();
  }

  template <typename ComposeChildren>
  void Element(ElementType type, const NodeProperties &properties,
               ComposeChildren &&composeChildren, std::string_view key = {}) {
    auto scope = Begin(type, properties, key);
    std::forward<ComposeChildren>(composeChildren)();
  }

  template <typename ComposeChildren>
  void Column(ComposeChildren &&composeChildren, std::string_view key = {}) {
    Element(ElementType::Column, std::forward<ComposeChildren>(composeChildren),
            key);
  }

  template <typename ComposeChildren>
  void Row(ComposeChildren &&composeChildren, std::string_view key = {}) {
    Element(ElementType::Row, std::forward<ComposeChildren>(composeChildren),
            key);
  }

  template <typename ComposeChildren>
  void ScrollView(ComposeChildren &&composeChildren,
                  const NodeProperties &properties = {},
                  std::string_view key = {}) {
    Element(ElementType::ScrollView, properties,
            std::forward<ComposeChildren>(composeChildren), key);
  }

  [[nodiscard]] auto Declarations() const noexcept
      -> const std::vector<ElementDeclaration> &;
  [[nodiscard]] auto IsBalanced() const noexcept -> bool;

private:
  void End(UIntSize depth) noexcept;
  [[nodiscard]] auto CurrentChildren() noexcept
      -> std::vector<ElementDeclaration> &;

  std::vector<ElementDeclaration> m_roots{};
  std::vector<ElementDeclaration *> m_stack{};
};
} // namespace NGIN::UI
