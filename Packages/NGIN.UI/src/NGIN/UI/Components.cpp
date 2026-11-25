#include "ComponentPrimitives.hpp"
#include "ControlPrimitives.hpp"
#include "SplitPane.hpp"

#include <atomic>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace NGIN::UI::Views {
namespace Detail {
struct ViewDescription final {
  ContainerBuilder emit{};
  std::vector<View> children{};
  ComponentBuilder build{};
  std::type_index type{typeid(void)};
  std::optional<ElementType> primitive{};
  InvalidationKind readInvalidation{InvalidationKind::Compose};
};

struct ViewAccess final {
  static auto Create(ViewDescription description) -> View {
    View result;
    result.m_description =
        std::make_shared<ViewDescription>(std::move(description));
    return result;
  }
  static auto Description(const View &view) -> const auto & {
    return view.m_description;
  }
  static auto Key(const View &view) -> const std::string & {
    return view.m_key;
  }
};

auto ComponentWithType(ComponentBuilder build, std::type_index type) -> View {
  return ViewAccess::Create({.build = std::move(build), .type = type});
}
} // namespace Detail
namespace {
using Detail::ViewAccess;
using Declaration = std::shared_ptr<const ElementDeclaration>;
std::atomic<UInt64> NextComponentId{1};

struct MountContext final {
  ViewEnvironment environment;
  InvalidationScheduler invalidate;
};

class MountedView final : public std::enable_shared_from_this<MountedView> {
public:
  MountedView(View view, std::weak_ptr<MountedView> parent,
              std::shared_ptr<MountContext> mount)
      : m_view(std::move(view)), m_parent(std::move(parent)),
        m_mount(std::move(mount)) {}

  void Emit(Composer &output, std::string_view inheritedKey = {}) {
    const auto &description = ViewAccess::Description(m_view);
    if (!description) {
      return;
    }
    if (m_dirty) {
      const auto key = inheritedKey.empty()
                           ? std::string_view{ViewAccess::Key(m_view)}
                           : inheritedKey;
      Composer composer;
      m_dirty = false;
      const bool rebuild = std::exchange(m_rebuild, false);
      try {
        if (description->build) {
          if (rebuild) {
            Track([&] { SetChildren({description->build(m_scope)}); });
          }
          m_children.front()->Emit(composer, key);
        } else {
          if (rebuild) {
            SetChildren(description->children);
          }
          Track([&] {
            description->emit(
                composer, m_mount->environment, key,
                [&](UIntSize index) { m_children.at(index)->Emit(composer); });
          });
        }
        std::vector<Declaration> next;
        for (auto &root : std::move(composer).TakeDeclarations()) {
          root.componentInstance = m_identity;
          next.push_back(root.cached ? std::move(root.cached)
                                     : std::make_shared<ElementDeclaration>(
                                           std::move(root)));
        }
        m_cache = std::move(next);

      } catch (...) {
        m_dirty = m_rebuild = true;
        throw;
      }
    }
    for (const auto &node : m_cache) {
      output.AppendCached(node);
    }
  }

private:
  void SetChildren(const std::vector<View> &views) {
    std::unordered_map<std::string, UIntSize> keyed;
    for (UIntSize index = 0; index < m_children.size(); ++index) {
      const auto &key = ViewAccess::Key(m_children[index]->m_view);
      if (!key.empty()) {
        keyed.emplace(key, index);
      }
    }
    std::vector<std::shared_ptr<MountedView>> next;
    next.reserve(views.size());
    for (const auto &view : views) {
      const auto &key = ViewAccess::Key(view);
      UIntSize index = next.size();
      if (!key.empty()) {
        const auto found = keyed.find(key);
        index = found == keyed.end() ? m_children.size() : found->second;
        if (found != keyed.end()) {
          keyed.erase(found);
        }
      }
      std::shared_ptr<MountedView> child;
      if (index < m_children.size() && m_children[index] &&
          ViewAccess::Key(m_children[index]->m_view) == key) {
        const auto &old = ViewAccess::Description(m_children[index]->m_view);
        const auto &incoming = ViewAccess::Description(view);
        if (old == incoming ||
            (old && incoming && old->type == incoming->type &&
             old->primitive == incoming->primitive)) {
          child = m_children[index];
          if (old != incoming) {
            child->ReplaceDescription(view);
          }
        }
      }
      next.push_back(child ? std::move(child)
                           : std::make_shared<MountedView>(
                                 view, weak_from_this(), m_mount));
    }
    m_children = std::move(next);
  }

  void ReplaceDescription(const View &view) {
    m_view = view;
    m_dirty = m_rebuild = true;
  }

  template <typename Build> void Track(Build build) {
    NGIN::UI::Detail::ReadCollection reads{m_subscriptions,
                                           [weak = weak_from_this()] {
                                             if (auto node = weak.lock())
                                               node->Changed();
                                           }};
    build();
    reads.Commit();
  }

  void Changed() {
    const auto kind = ViewAccess::Description(m_view)->readInvalidation;
    if (HasInvalidation(kind, InvalidationKind::Compose)) {
      m_rebuild = true;
      m_dirty = true;
      for (auto node = m_parent.lock(); node; node = node->m_parent.lock()) {
        node->m_dirty = true;
      }
    }
    if (m_mount->invalidate) {
      m_mount->invalidate(kind);
    }
  }

  UInt64 m_identity{NextComponentId.fetch_add(1, std::memory_order_relaxed)};
  View m_view;
  std::weak_ptr<MountedView> m_parent;
  std::shared_ptr<MountContext> m_mount;
  ComponentScope m_scope{};
  bool m_dirty{true};
  bool m_rebuild{true};
  std::vector<Declaration> m_cache{};
  std::vector<std::shared_ptr<MountedView>> m_children{};
  std::vector<NGIN::UI::Detail::ObservedRead> m_subscriptions{};
};

auto TextProperties(const ViewEnvironment &env) -> NodeProperties {
  NodeProperties properties;
  properties.text.servicesOwner = env.TextOwner();
  properties.text.geometry = &env.geometry;
  properties.text.layout = &env.layout;
  properties.text.glyphAtlas = &env.atlas;
  properties.text.fontSize = env.theme.typography.body;
  properties.text.color = env.theme.colors.foreground;
  return properties;
}

auto RequireText(const std::shared_ptr<NativeTextSystem> &text)
    -> NativeTextSystem & {
  if (!text) {
    throw std::invalid_argument("ViewEnvironment requires a text service");
  }
  return *text;
}
} // namespace

ViewEnvironment::ViewEnvironment(ITextLayout &layoutService,
                                 IGlyphAtlas &atlasService,
                                 ITextGeometry &geometryService,
                                 IGraphemeSegmenter &segmenterService)
    : layout(layoutService), atlas(atlasService), geometry(geometryService),
      segmenter(segmenterService) {}
ViewEnvironment::ViewEnvironment(NativeTextSystem &text)
    : ViewEnvironment(text, text, text, text) {}
ViewEnvironment::ViewEnvironment(std::shared_ptr<NativeTextSystem> text)
    : ViewEnvironment(RequireText(text)) {
  m_text = std::move(text);
}

ComponentScope::~ComponentScope() {
  // Cancellation can invoke observers synchronously. Expire local writes first.
  m_slots.clear();
  if (m_lifetime)
    m_lifetime->Cancel(NGIN::Async::StopReason::OwnerClosed);
}

auto ComponentScope::Lifetime() -> NGIN::Async::CancellationToken {
  if (!m_lifetime)
    m_lifetime.emplace();
  return m_lifetime->GetToken();
}

auto View::Key(std::string_view key) const -> View {
  auto copy = *this;
  copy.m_key = key;
  return copy;
}

auto Detail::ContainerWithType(ContainerBuilder build,
                               std::vector<View> children, std::type_index type)
    -> View {
  return ViewAccess::Create({.emit = std::move(build),
                             .children = std::move(children),
                             .type = type});
}

auto Layout(std::shared_ptr<ILayoutPolicy> policy, std::vector<View> children,
            NodeProperties properties) -> View {
  properties.layoutPolicy = std::move(policy);
  return Box(ElementType::Custom, std::move(children), std::move(properties));
}

auto Column(std::vector<View> children, NodeProperties properties) -> View {
  return Box(ElementType::Column, std::move(children), std::move(properties));
}

auto Row(std::vector<View> children, NodeProperties properties) -> View {
  return Box(ElementType::Row, std::move(children), std::move(properties));
}

auto Label(NGIN::Text::String value) -> View {
  return Element([value = std::move(value)](Composer &composer,
                                            const ViewEnvironment &env,
                                            std::string_view key) {
    composer.Text(value, env.layout, env.atlas, TextProperties(env), key);
  });
}

auto Grid(std::vector<View> children, NodeProperties properties) -> View {
  return Box(ElementType::Grid, std::move(children), std::move(properties));
}

auto Canvas(std::vector<View> children, NodeProperties properties) -> View {
  return Box(ElementType::Canvas, std::move(children), std::move(properties));
}

auto Border(std::vector<View> children, NodeProperties properties) -> View {
  return Box(ElementType::Border, std::move(children), std::move(properties));
}

auto ScrollView(std::vector<View> children, NodeProperties properties) -> View {
  return Box(ElementType::ScrollView, std::move(children),
             std::move(properties));
}

auto WrapPanel(std::vector<View> children, NodeProperties properties) -> View {
  return Box(ElementType::WrapPanel, std::move(children),
             std::move(properties));
}

auto Overlay(std::vector<View> children, NodeProperties properties) -> View {
  return Box(ElementType::Overlay, std::move(children), std::move(properties));
}

auto Box(ElementType type, std::vector<View> children,
         NodeProperties properties) -> View {
  const auto count = children.size();
  return ViewAccess::Create({
      .emit =
          [type, count, properties = std::move(properties)](
              Composer &composer, const ViewEnvironment &, std::string_view key,
              const ChildEmitter &emit) {
            auto scope = composer.Begin(type, properties, key);
            for (UIntSize index = 0; index < count; ++index)
              emit(index);
          },
      .children = std::move(children),
      .primitive = type,
  });
}

namespace {
auto WithTextServices(NodeProperties properties, const ViewEnvironment &env)
    -> NodeProperties {
  properties.text.servicesOwner = env.TextOwner();
  properties.text.geometry = &env.geometry;
  properties.text.layout = &env.layout;
  properties.text.glyphAtlas = &env.atlas;
  return properties;
}

template <bool Multiline>
auto EditableText(NGIN::Text::String label, Binding<NGIN::Text::String> value,
                  NodeProperties properties) -> View {
  properties.semantics.label = std::move(label);
  return Element([value = std::move(value), properties = std::move(properties)](
                     Composer &composer, const ViewEnvironment &env,
                     std::string_view key) {
    if (value.IsReadable())
      static_cast<void>(value.Get());
    auto field = WithTextServices(properties, env);
    if constexpr (Multiline)
      composer.TextArea(value, env.segmenter, field, key);
    else
      composer.TextField(value, env.segmenter, field, key);
  });
}
} // namespace

auto TextField(NGIN::Text::String label, Binding<NGIN::Text::String> value,
               NodeProperties properties) -> View {
  return EditableText<false>(std::move(label), std::move(value),
                             std::move(properties));
}

auto TextArea(NGIN::Text::String label, Binding<NGIN::Text::String> value,
              NodeProperties properties) -> View {
  return EditableText<true>(std::move(label), std::move(value),
                            std::move(properties));
}

auto Label(NGIN::Text::String value, NodeProperties properties) -> View {
  return Element([value = std::move(value), properties = std::move(properties)](
                     Composer &composer, const ViewEnvironment &env,
                     std::string_view key) {
    composer.Text(value, env.layout, env.atlas,
                  WithTextServices(properties, env), key);
  });
}

auto Image(std::shared_ptr<ImageResource> resource, IImageResolver &resolver,
           NGIN::Text::String description, NodeProperties properties) -> View {
  return Element([resource = std::move(resource), &resolver,
                  description = std::move(description),
                  properties = std::move(properties)](Composer &composer,
                                                      const ViewEnvironment &,
                                                      std::string_view key) {
    composer.Image(resource, resolver, description, properties, key);
  });
}

auto Separator(SeparatorOrientation orientation, NodeProperties properties)
    -> View {
  return Element(
      [orientation, properties = std::move(properties)](
          Composer &composer, const ViewEnvironment &, std::string_view key) {
        composer.Separator(orientation, properties, key);
      });
}

namespace {
template <typename Action>
auto ButtonWithContent(Action action, View content, NodeProperties properties)
    -> View {
  return Container(
      [action = std::move(action), properties = std::move(properties)](
          Composer &composer, const ViewEnvironment &, std::string_view key,
          const ChildEmitter &emit) {
        auto scope = composer.BeginButton(action, properties, key);
        emit(0);
      },
      {std::move(content)});
}

} // namespace

auto Button(Utilities::Callable<void()> activate, View content,
            NodeProperties properties) -> View {
  return ButtonWithContent(std::move(activate), std::move(content),
                           std::move(properties));
}

auto Button(CommandBinding command, View content, NodeProperties properties)
    -> View {
  return ButtonWithContent(std::move(command), std::move(content),
                           std::move(properties));
}

auto Button(NGIN::Text::String label, Utilities::Callable<void()> activate)
    -> View {
  return Button(std::move(label), std::move(activate),
                ButtonAppearance::Accent);
}

auto Button(NGIN::Text::String label, Utilities::Callable<void()> activate,
            ButtonAppearance appearance) -> View {
  auto action =
      std::make_shared<Utilities::Callable<void()>>(std::move(activate));
  return Element([label = std::move(label), action = std::move(action),
                  appearance](Composer &composer, const ViewEnvironment &env,
                              std::string_view key) {
    NodeProperties button;
    button.semantics.label = label;
    button.visual = MakeButtonVisual(env.theme, appearance);
    button.layout.minimumSize.height = env.theme.controls.regularHeight;
    button.layout.padding = Thickness::Uniform(Dp{env.theme.spacing.regular});
    button.interaction.enabled = static_cast<bool>(*action);
    auto scope = composer.BeginButton(
        [action] {
          if (*action)
            (*action)();
        },
        button, key);
    auto text = TextProperties(env);
    text.interaction.hitTestVisible = false;
    text.semantics.hidden = true;
    text.text.color = button.interaction.enabled
                          ? *button.visual.base.foreground
                          : env.theme.colors.disabledForeground;
    text.text.alignment = TextAlignment::Center;
    composer.Text(label, env.layout, env.atlas, text);
  });
}

auto TextField(NGIN::Text::String label, Binding<NGIN::Text::String> value)
    -> View {
  return Element([label = std::move(label), value = std::move(value)](
                     Composer &composer, const ViewEnvironment &env,
                     std::string_view key) {
    // Synchronize retained editing state on external writes.
    if (value.IsReadable())
      static_cast<void>(value.Get());
    auto field = TextProperties(env);
    field.semantics.label = label;
    field.visual = MakeTextFieldVisual(env.theme);
    field.layout.padding = Thickness::Uniform(Dp{env.theme.spacing.regular});
    field.layout.minimumSize.height = env.theme.controls.regularHeight;
    composer.TextField(value, env.segmenter, field, key);
  });
}

auto Slider(NGIN::Text::String label, Binding<F32> value, SliderRange range)
    -> View {
  return Element([label = std::move(label), value = std::move(value),
                  range](Composer &composer, const ViewEnvironment &env,
                         std::string_view key) {
    if (value.IsReadable()) {
      static_cast<void>(value.Get());
    }
    NodeProperties properties;
    properties.semantics.label = label;
    UI::Slider(composer, value, range, {.theme = env.theme}, properties, key);
  });
}

auto Custom(std::shared_ptr<ICustomElement> element, NodeProperties properties)
    -> View {
  return Element(
      [element = std::move(element), properties = std::move(properties)](
          Composer &composer, const ViewEnvironment &, std::string_view key) {
        composer.Custom(element, properties, key);
      });
}

auto SplitPane(Binding<F32> fraction, View first, View second,
               NodeProperties properties) -> View {
  auto emit = [fraction = std::move(fraction),
               properties = std::move(properties)](
                  Composer &composer, const ViewEnvironment &,
                  std::string_view key, const ChildEmitter &child) {
    if (fraction.IsReadable()) {
      static_cast<void>(fraction.Get());
    }
    UI::SplitPane(
        composer, fraction, [&] { child(0); }, [&] { child(1); }, properties,
        key);
  };
  // The layout policy and the accessible divider both read the live binding.
  return ViewAccess::Create({
      .emit = std::move(emit),
      .children = {std::move(first), std::move(second)},
      .type = typeid(decltype(emit)),
      .readInvalidation = InvalidationKind::Measure,
  });
}

auto Mount(View view, ViewEnvironment environment,
           InvalidationScheduler invalidate)
    -> Utilities::Callable<void(Composer &)> {
  auto context = std::make_shared<MountContext>(
      MountContext{std::move(environment), std::move(invalidate)});
  auto root = std::make_shared<MountedView>(
      std::move(view), std::weak_ptr<MountedView>{}, std::move(context));
  return [root = std::move(root)](Composer &composer) { root->Emit(composer); };
}

} // namespace NGIN::UI::Views
