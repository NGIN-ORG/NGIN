#include "GalleryPropertyEditor.hpp"
#include "GallerySamplePicker.hpp"

#include <NGIN/Reflection/Reflection.hpp>
#include <NGIN/UI/ViewControls.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace NGIN::UIGallery {
namespace {
using namespace UI;
using namespace Reflection;
using Text::String;

constexpr auto SettingsTypeName = "Gallery::PlaybackSettings";

struct PlaybackSettings final {
  bool muted{false};
  int volume{50};

  bool Muted() const { return muted; }
  void SetMuted(bool value) { muted = value; }
  int Volume() const { return volume; }
  void SetVolume(int value) { volume = value; }
  int OutputLevel() const { return muted ? 0 : volume; }

  friend void NginReflect(Tag<PlaybackSettings>,
                          TypeBuilder<PlaybackSettings> &builder) {
    builder.SetName(SettingsTypeName);
    builder.Property<&PlaybackSettings::Muted, &PlaybackSettings::SetMuted>(
        "Mute");
    builder.Property<&PlaybackSettings::Volume, &PlaybackSettings::SetVolume>(
        "Volume");
    builder.Property<&PlaybackSettings::OutputLevel>("Output level");
  }
};

auto EditorError(const Error &error) {
  return MakeUIError(UIErrorCode::ResourceFailed, error.message.c_str(),
                     "NGIN.UI.Gallery", "Property Editor");
}

// Editable properties and computed getters share one reflected document.
struct EditorDocument final {
  explicit EditorDocument(InstanceRef settings)
      : instance(std::move(settings)) {}
  InstanceRef instance;
  State<UInt64> revision{0};
};

enum class EditorLayout { Inspector, Mixer };

struct PropertyEditor final {
  Type type;
  std::shared_ptr<EditorDocument> document;
  State<EditorLayout> layout{EditorLayout::Inspector};
};

template <typename T, typename Convert>
auto BindProperty(T initial, Property property,
                  std::shared_ptr<EditorDocument> document, Convert convert)
    -> Binding<T> {
  auto displayed = std::make_shared<T>(initial);
  return Binding<T>{[displayed]() -> const T & { return *displayed; },
                    [displayed, property, document = std::move(document),
                     convert](T value) mutable -> UIResult<void> {
                      auto written =
                          property.Write(document->instance, convert(value));
                      if (!written) {
                        return std::unexpected(EditorError(written.error()));
                      }
                      *displayed = value;
                      static_cast<void>(document->revision.Update(
                          [](UInt64 &revision) { ++revision; }));
                      return {};
                    }};
}

auto PropertyLabel(String value, const Theme &theme, String id = {})
    -> Views::View {
  NodeProperties label{};
  label.layout.verticalAlignment = VerticalAlignment::Center;
  label.text.fontSize = theme.typography.body;
  label.text.color = theme.colors.foreground;
  label.semantics.identifier = std::move(id);
  return Views::Label(std::move(value), label);
}

auto PropertyValue(const Property &property, const Value &value,
                   const std::shared_ptr<EditorDocument> &document,
                   const ControlPresentation &presentation) -> Views::View {
  const std::string name{property.Name()};
  NodeProperties control{};
  control.layout.verticalAlignment = VerticalAlignment::Center;
  control.semantics.identifier = String{"property-" + name};
  control.semantics.label = String{name};
  const auto *number = value.TryAs<std::int64_t>();
  const auto *flag = value.TryAs<bool>();
  if (property.IsWritable() && flag) {
    auto binding = BindProperty(
        *flag ? CheckState::Checked : CheckState::Unchecked, property, document,
        [](CheckState state) { return Value{state == CheckState::Checked}; });
    return Views::CheckBox(std::move(binding), presentation, control)
        .Key("checkbox");
  }
  if (property.IsWritable() && number) {
    control.layout.preferredSize.width = 158;
    auto binding = BindProperty(
        static_cast<F32>(*number), property, document,
        [](F32 level) { return Value{static_cast<std::int64_t>(level)}; });
    return Views::Slider(std::move(binding),
                         {.minimum = 0, .maximum = 100, .step = 1},
                         presentation, control)
        .Key("slider");
  }
  std::string display{property.ValueTypeName()};
  if (number) {
    display = std::to_string(*number);
  } else if (flag) {
    display = *flag ? "On" : "Off";
  }
  auto readout = PropertyLabel(String{display}, presentation.theme,
                               control.semantics.identifier)
                     .Key("read-only");
  if (name == "Output level" && number) {
    NodeProperties meter{};
    meter.layout.preferredSize.width = 118;
    meter.layout.verticalAlignment = VerticalAlignment::Center;
    meter.semantics.label = String{"Output meter"};
    return Pages::ActionRow(
        {Views::ProgressBar(
             {.value = static_cast<F32>(*number), .maximum = 100}, presentation,
             meter),
         std::move(readout)},
        "output");
  }
  return readout;
}

// The inspector and the mixer receive the same reflected field and document.
// A template owns the choice of editor and its layout; writes still go through
// the property's setter, including in the alternate switch below.
using FieldTemplate = Views::View (*)(const Property &, const Value &,
                                      const std::shared_ptr<EditorDocument> &,
                                      const ControlPresentation &);

auto FieldLabel(const Property &property, const Theme &theme, F32 width = 0)
    -> Views::View {
  const std::string name{property.Name()};
  auto label =
      Pages::TextProperties(theme.typography.body, theme.colors.foreground);
  if (width > 0)
    label.layout.preferredSize.width = width;
  label.layout.verticalAlignment = VerticalAlignment::Center;
  label.semantics.labelFor = String{"property-" + name};
  return Views::Label(String{name}, label).Key("name");
}

auto InspectorRow(const Property &property, const Value &value,
                  const std::shared_ptr<EditorDocument> &document,
                  const ControlPresentation &presentation) -> Views::View {
  return Pages::ActionRow(
      {FieldLabel(property, presentation.theme, 110),
       PropertyValue(property, value, document, presentation)},
      property.Name());
}

auto MixerModule(const Property &property, const Value &value,
                 const std::shared_ptr<EditorDocument> &document,
                 const ControlPresentation &presentation) -> Views::View {
  const auto &theme = presentation.theme;
  const std::string name{property.Name()};
  std::vector<Views::View> content{FieldLabel(property, theme)};
  if (const auto *flag = value.TryAs<bool>(); flag && property.IsWritable()) {
    auto binding = BindProperty(*flag, property, document,
                                [](bool muted) { return Value{muted}; });
    NodeProperties toggle{};
    toggle.semantics.identifier = String{"property-" + name};
    toggle.semantics.label = String{name};
    content.push_back(Pages::TextView(*flag ? "Muted" : "Live", 21,
                                      theme.colors.foreground, "state"));
    content.push_back(
        Views::ToggleSwitch(std::move(binding), presentation, toggle));
  } else if (const auto *number = value.TryAs<std::int64_t>()) {
    auto label = Pages::TextProperties(23, theme.colors.foreground);
    if (!property.IsWritable())
      label.semantics.identifier = String{"property-" + name};
    content.push_back(Views::Label(String{std::to_string(*number) + "%"}, label)
                          .Key("reading"));
    if (property.IsWritable()) {
      content.push_back(PropertyValue(property, value, document, presentation));
    } else {
      NodeProperties meter{};
      meter.layout.preferredSize = {156, 12};
      meter.semantics.label = String{name};
      content.push_back(Views::ProgressBar(
          {.value = static_cast<F32>(*number), .maximum = 100}, presentation,
          meter));
    }
  } else {
    content.push_back(PropertyValue(property, value, document, presentation));
  }
  NodeProperties body{};
  body.layout.preferredSize.width = 194;
  body.layout.minimumSize.height = 112;
  body.layout.padding = Thickness::Uniform(Dp{12});
  body.layout.gap = 6;
  body.visual = MakePanelVisual(theme);
  body.visual.base.background = theme.colors.raisedSurface;
  return Views::Column(std::move(content), body).Key(name);
}

auto BuildProperties(PropertyEditor &editor, GalleryViewModel &gallery)
    -> UIResult<Views::View> {
  // A write may change several getters (e.g. muting changes the output).
  static_cast<void>(editor.document->revision.Get());
  const auto theme = gallery.CurrentTheme();
  const ControlPresentation presentation{
      .theme = theme,
      .onError = [&gallery](const UIError &error) { gallery.Report(error); }};
  const bool mixer = editor.layout.Get() == EditorLayout::Mixer;
  const FieldTemplate renderField = mixer ? MixerModule : InspectorRow;
  std::vector<Views::View> fields;
  for (std::size_t index = 0; index < editor.type.PropertyCount(); ++index) {
    auto property = editor.type.PropertyAt(index);
    if (!property)
      return std::unexpected(EditorError(property.error()));
    auto value = property->Read(editor.document->instance);
    if (!value)
      return std::unexpected(EditorError(value.error()));
    fields.push_back(
        renderField(*property, *value, editor.document, presentation));
  }
  NodeProperties column{};
  column.layout.gap = theme.spacing.regular;
  auto body =
      mixer ? Pages::ActionRow(std::move(fields), "mixer-modules")
            : Views::Column(std::move(fields), column).Key("inspector-fields");
  return Pages::PageContentView(
      {Pages::PageHeadingView(
           theme, "Property editor",
           "Edit one playback object through two different field templates."),
       Pages::SamplePicker(
           theme, Bind(editor.layout), "Editor template",
           {{EditorLayout::Inspector, "Inspector", "editor-inspector"},
            {EditorLayout::Mixer, "Mixer", "editor-mixer"}}),
       Pages::SampleCard(
           theme,
           {Pages::TextView(mixer ? "Mix strip" : "Playback settings", 22,
                            theme.colors.foreground, "playback-title",
                            SemanticRole::Heading),
            std::move(body)},
           "playback-settings", "property-editor-card")});
}

} // namespace

auto RegisterPropertyEditor(UI::PageRegistry &registry,
                            UI::PageRegistrationOptions options,
                            GalleryViewModel &gallery) -> UI::UIResult<void> {
  return registry.Register<PropertyEditorPage, PropertyEditor>(
      std::move(options),
      [](UI::PageActivationContext &, const UI::NoNavigationParameter &,
         std::string_view) -> UI::UIResult<UI::PageLease<PropertyEditor>> {
        if (!EnsureModuleInitialized("Gallery.Playback",
                                     [](ModuleRegistration &module) {
                                       module.RegisterType<PlaybackSettings>();
                                     })) {
          return std::unexpected(
              UI::MakeUIError(UI::UIErrorCode::ResourceFailed,
                              "Could not register the playback settings",
                              "NGIN.UI.Gallery", "Property Editor"));
        }
        auto type = GetType(SettingsTypeName);
        if (!type) {
          return std::unexpected(EditorError(type.error()));
        }
        auto settings = type->Construct();
        if (!settings) {
          return std::unexpected(EditorError(settings.error()));
        }
        auto document = std::make_shared<EditorDocument>(std::move(*settings));
        return UI::PageLease<PropertyEditor>{
            .viewModel = std::make_shared<PropertyEditor>(*type, std::move(document))};
      },
      [&gallery](PropertyEditor &page, const UI::NoNavigationParameter &) {
        auto properties = BuildProperties(page, gallery);
        if (!properties) {
          gallery.Report(properties.error());
          return Views::View{};
        }
        return std::move(*properties);
      });
}
} // namespace NGIN::UIGallery
