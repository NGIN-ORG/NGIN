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

struct PropertyEditor final {
  Type type;
  std::shared_ptr<EditorDocument> document;
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

auto BuildProperties(const Type &type,
                     const std::shared_ptr<EditorDocument> &document,
                     GalleryViewModel &gallery) -> UIResult<Views::View> {
  // A write may change several reflected getters (e.g. Muted affects
  // OutputLevel).
  static_cast<void>(document->revision.Get());
  const auto theme = gallery.CurrentTheme();
  const ControlPresentation presentation{
      .theme = theme,
      .onError = [&gallery](const UIError &error) { gallery.Report(error); }};
  std::vector<Views::View> rows;
  rows.reserve(type.PropertyCount() + 1);
  rows.push_back(Pages::TextView("Playback settings", 22,
                                 theme.colors.foreground, "playback-title",
                                 SemanticRole::Heading));
  NodeProperties row{};
  row.wrapPanel.itemGap = 16;
  row.wrapPanel.lineGap = 6;
  for (std::size_t index = 0; index < type.PropertyCount(); ++index) {
    auto property = type.PropertyAt(index);
    if (!property) {
      return std::unexpected(EditorError(property.error()));
    }
    auto value = property->Read(document->instance);
    if (!value) {
      return std::unexpected(EditorError(value.error()));
    }
    const std::string name{property->Name()};
    auto label = Pages::TextProperties(16, theme.colors.foreground);
    label.layout.preferredSize.width = 110;
    label.layout.verticalAlignment = VerticalAlignment::Center;
    label.semantics.labelFor = String{"property-" + name};
    rows.push_back(
        Views::WrapPanel(
            {Views::Label(String{name}, label).Key("name"),
             PropertyValue(*property, *value, document, presentation)},
            row)
            .Key(name));
  }
  return Pages::PageContentView(
      {Pages::PageHeadingView(theme, "Property editor",
                              "Edit a reflected playback object and watch its "
                              "output level change."),
       Pages::SampleCard(theme, std::move(rows), "playback-settings",
                         "property-editor-card")});
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
            .viewModel = std::make_shared<PropertyEditor>(
                PropertyEditor{*type, std::move(document)})};
      },
      [&gallery](PropertyEditor &page, const UI::NoNavigationParameter &) {
        auto properties = BuildProperties(page.type, page.document, gallery);
        if (!properties) {
          gallery.Report(properties.error());
          return Views::View{};
        }
        return std::move(*properties);
      });
}
} // namespace NGIN::UIGallery
