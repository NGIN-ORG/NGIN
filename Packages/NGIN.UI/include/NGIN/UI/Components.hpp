#pragma once

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/UI/Command.hpp>
#include <NGIN/UI/Controls.hpp>
#include <NGIN/UI/NativeText.hpp>

#include <any>
#include <concepts>
#include <memory>
#include <optional>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace NGIN::UI::Views {
/// @brief Text and theme services shared by all views in one mount.
struct ViewEnvironment final {
  explicit ViewEnvironment(NativeTextSystem &text);
  explicit ViewEnvironment(std::shared_ptr<NativeTextSystem> text);
  ViewEnvironment(ITextLayout &layout, IGlyphAtlas &atlas,
                  ITextGeometry &geometry, IGraphemeSegmenter &segmenter);

  ITextLayout &layout;
  IGlyphAtlas &atlas;
  ITextGeometry &geometry;
  IGraphemeSegmenter &segmenter;
  Theme theme{};
  [[nodiscard]] auto TextOwner() const noexcept
      -> const std::shared_ptr<NativeTextSystem> & {
    return m_text;
  }

private:
  std::shared_ptr<NativeTextSystem> m_text{};
};

/// @brief Local state retained for one component identity on the UI thread.
class ComponentScope final {
public:
  ComponentScope() = default;
  ~ComponentScope();
  /// @brief Token canceled with OwnerClosed when this component unmounts.
  [[nodiscard]] auto Lifetime() -> NGIN::Async::CancellationToken;
  ComponentScope(const ComponentScope &) = delete;
  auto operator=(const ComponentScope &) -> ComponentScope & = delete;
  /// @brief Reuses the named state on every build until this component
  /// unmounts.
  template <typename T>
  auto Local(std::string_view key, T initial) -> Binding<T> {
    const std::string name{key};
    const auto found = m_slots.find(name);
    if (found != m_slots.end()) {
      return Bind(
          *std::any_cast<const std::shared_ptr<State<T>> &>(found->second));
    }
    auto state = std::make_shared<State<T>>(std::move(initial));
    m_slots.emplace(name, state);
    return Bind(*state);
  }

private:
  std::unordered_map<std::string, std::any> m_slots{};
  std::optional<NGIN::Async::CancellationSource> m_lifetime{};
};

namespace Detail {
/// @brief Immutable authoring description shared between view values.
struct ViewDescription;
/// @brief Internal access to view construction and mounting.
struct ViewAccess;
} // namespace Detail

/// @brief An immutable, reusable component description with optional sibling
/// identity.
class View final {
public:
  View() = default;
  [[nodiscard]] auto Key(std::string_view key) const -> View;

private:
  friend struct Detail::ViewAccess;
  std::shared_ptr<const Detail::ViewDescription> m_description{};
  std::string m_key{};
};

/// @brief Component factory re-evaluated when an observable it read changes.
using ComponentBuilder = Utilities::Callable<View(ComponentScope &)>;

namespace Detail {
[[nodiscard]] auto ComponentWithType(ComponentBuilder build,
                                     std::type_index type) -> View;
} // namespace Detail

/// @brief Provides an independently reactive boundary, with optional retained
/// local state.
template <typename Factory>
  requires(std::invocable<Factory &, ComponentScope &> ||
           std::invocable<Factory &>)
[[nodiscard]] auto Component(Factory build) -> View {
  if constexpr (std::invocable<Factory &, ComponentScope &>) {
    return Detail::ComponentWithType(std::move(build), typeid(Factory));
  } else {
    return Detail::ComponentWithType(
        [build = std::move(build)](ComponentScope &) mutable {
          return build();
        },
        typeid(Factory));
  }
}

[[nodiscard]] auto Column(std::vector<View> children,
                          NodeProperties properties = {}) -> View;
[[nodiscard]] auto Row(std::vector<View> children,
                       NodeProperties properties = {}) -> View;
[[nodiscard]] auto Grid(std::vector<View> children,
                        NodeProperties properties = {}) -> View;
[[nodiscard]] auto Canvas(std::vector<View> children,
                          NodeProperties properties = {}) -> View;
[[nodiscard]] auto Border(std::vector<View> children,
                          NodeProperties properties = {}) -> View;
[[nodiscard]] auto ScrollView(std::vector<View> children,
                              NodeProperties properties = {}) -> View;
[[nodiscard]] auto WrapPanel(std::vector<View> children,
                             NodeProperties properties = {}) -> View;
[[nodiscard]] auto Overlay(std::vector<View> children,
                           NodeProperties properties = {}) -> View;
/// @brief Builds a container or leaf from a retained primitive and its child
/// views.
[[nodiscard]] auto Box(ElementType type, std::vector<View> children,
                       NodeProperties properties = {}) -> View;
[[nodiscard]] auto Label(NGIN::Text::String value) -> View;
[[nodiscard]] auto Label(NGIN::Text::String value, NodeProperties properties)
    -> View;
[[nodiscard]] auto Image(std::shared_ptr<ImageResource> resource,
                         IImageResolver &resolver,
                         NGIN::Text::String description,
                         NodeProperties properties = {}) -> View;
[[nodiscard]] auto Separator(SeparatorOrientation orientation,
                             NodeProperties properties = {}) -> View;
[[nodiscard]] auto TextArea(NGIN::Text::String label,
                            Binding<NGIN::Text::String> value,
                            NodeProperties properties = {}) -> View;
[[nodiscard]] auto TextField(NGIN::Text::String label,
                             Binding<NGIN::Text::String> value,
                             NodeProperties properties) -> View;
[[nodiscard]] auto Button(Utilities::Callable<void()> activate, View content,
                          NodeProperties properties = {}) -> View;
[[nodiscard]] auto Button(CommandBinding command, View content,
                          NodeProperties properties = {}) -> View;
[[nodiscard]] inline auto Label(std::string_view value) -> View {
  return Label(NGIN::Text::String{value});
}
[[nodiscard]] inline auto Label(const char *value) -> View {
  return Label(NGIN::Text::String{value});
}
[[nodiscard]] auto Button(NGIN::Text::String label,
                          Utilities::Callable<void()> activate) -> View;
[[nodiscard]] auto Button(NGIN::Text::String label,
                          Utilities::Callable<void()> activate,
                          ButtonAppearance appearance) -> View;
[[nodiscard]] auto TextField(NGIN::Text::String label,
                             Binding<NGIN::Text::String> value) -> View;
[[nodiscard]] auto Slider(NGIN::Text::String label, Binding<F32> value,
                          SliderRange range = {}) -> View;
/// @brief Places child components using the caller's measurement and
/// arrangement policy.
[[nodiscard]] auto Layout(std::shared_ptr<ILayoutPolicy> policy,
                          std::vector<View> children,
                          NodeProperties properties = {}) -> View;
[[nodiscard]] auto Custom(std::shared_ptr<ICustomElement> element,
                          NodeProperties properties = {}) -> View;
[[nodiscard]] auto SplitPane(Binding<F32> fraction, View first, View second,
                             NodeProperties properties = {}) -> View;

} // namespace NGIN::UI::Views
