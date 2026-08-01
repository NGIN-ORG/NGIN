#include <NGIN/UIGallery/CustomControls.hpp>
#include <NGIN/UIGallery/Gallery.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace NGIN::UIGallery {
class GalleryVirtualizedSource final
    : public UI::IVirtualizedDataSource<NGIN::UIntSize> {
public:
  [[nodiscard]] auto Count() const noexcept -> NGIN::UIntSize override {
    return BaseItemCount + m_insertedIds.size();
  }
  [[nodiscard]] auto Revision() const noexcept -> NGIN::UInt64 override {
    return m_revision;
  }
  [[nodiscard]] auto ItemAt(const NGIN::UIntSize index) const
      -> UI::UIResult<NGIN::UIntSize> override {
    if (index >= Count() || !m_loadedRange || index < m_loadedRange->first ||
        index >= m_loadedRange->End()) {
      return UI::MakeUIError(UI::UIErrorCode::ResourceFailed,
                             "The Gallery row has not been loaded",
                             "NGIN.UI.Gallery",
                             "GalleryVirtualizedSource::ItemAt");
    }
    return LogicalValue(index);
  }
  auto RequestRange(const UI::IncrementalRange range)
      -> UI::UIResult<void> override {
    if (range.first > Count() || range.count > Count() - range.first) {
      return UI::MakeUIError(UI::UIErrorCode::InvalidArgument,
                             "The requested Gallery range is out of bounds",
                             "NGIN.UI.Gallery",
                             "GalleryVirtualizedSource::RequestRange");
    }
    m_loadedRange = range;
    return {};
  }
  void CancelRange(const UI::IncrementalRange) noexcept override {}
  [[nodiscard]] auto KeyAt(const NGIN::UIntSize index) const
      -> UI::UIResult<Text::String> override {
    if (index >= Count()) {
      return UI::MakeUIError(
          UI::UIErrorCode::InvalidArgument, "The Gallery key is out of bounds",
          "NGIN.UI.Gallery", "GalleryVirtualizedSource::KeyAt");
    }
    const auto value = LogicalValue(index);
    const auto key =
        std::string{value >= NewItemBase ? "new-" : "item-"} +
        std::to_string(value >= NewItemBase ? value - NewItemBase : value);
    return Text::String{key.c_str()};
  }
  [[nodiscard]] auto LabelAt(const NGIN::UIntSize index) const
      -> UI::UIResult<Text::String> override {
    if (index >= Count()) {
      return UI::MakeUIError(UI::UIErrorCode::InvalidArgument,
                             "The Gallery label is out of bounds",
                             "NGIN.UI.Gallery",
                             "GalleryVirtualizedSource::LabelAt");
    }
    const auto value = LogicalValue(index);
    const auto label =
        std::string{value >= NewItemBase ? "New item " : "Item "} +
        std::to_string(value >= NewItemBase ? value - NewItemBase + 1
                                            : value + 1);
    return Text::String{label.c_str()};
  }
  [[nodiscard]] auto IndexOfKey(const Text::String &key) const
      -> std::optional<NGIN::UIntSize> override {
    const auto view = key.View();
    const auto prefix = view.starts_with("item-")  ? std::string_view{"item-"}
                        : view.starts_with("new-") ? std::string_view{"new-"}
                                                   : std::string_view{};
    if (prefix.empty()) {
      return std::nullopt;
    }
    NGIN::UIntSize value = 0;
    const auto digits = view.substr(prefix.size());
    const auto parsed =
        std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != digits.data() + digits.size()) {
      return std::nullopt;
    }
    if (prefix == "new-") {
      const auto found = std::ranges::find(m_insertedIds, value);
      return found == m_insertedIds.end()
                 ? std::nullopt
                 : std::optional<NGIN::UIntSize>{static_cast<NGIN::UIntSize>(
                       std::distance(m_insertedIds.begin(), found))};
    }
    const auto index = value + m_insertedIds.size();
    return value < BaseItemCount ? std::optional<NGIN::UIntSize>{index}
                                 : std::nullopt;
  }

  void Prepend(const NGIN::UIntSize count) {
    std::vector<NGIN::UIntSize> inserted;
    inserted.reserve(count);
    for (NGIN::UIntSize index = 0; index < count; ++index) {
      inserted.push_back(m_nextInsertedId++);
    }
    m_insertedIds.insert(m_insertedIds.begin(), inserted.begin(),
                         inserted.end());
    ++m_revision;
  }

private:
  [[nodiscard]] auto LogicalValue(const NGIN::UIntSize index) const noexcept
      -> NGIN::UIntSize {
    return index < m_insertedIds.size() ? NewItemBase + m_insertedIds[index]
                                        : index - m_insertedIds.size();
  }

  static constexpr NGIN::UIntSize BaseItemCount{100'000};
  static constexpr NGIN::UIntSize NewItemBase{1'000'000};
  std::vector<NGIN::UIntSize> m_insertedIds{};
  NGIN::UIntSize m_nextInsertedId{0};
  NGIN::UInt64 m_revision{1};
  std::optional<UI::IncrementalRange> m_loadedRange{};
};

namespace {
using NGIN::F32;
using NGIN::Text::String;
using namespace NGIN::UI;

constexpr std::array<std::string_view, PageCount> PageNames{
    "Overview", "Layout", "Typography",    "Text Area",
    "Images",   "Inputs", "Collections",   "Overlays",
    "Windows",  "Themes", "Accessibility", "Diagnostics",
};

[[nodiscard]] auto Number(const std::uint64_t value) -> String {
  const auto formatted = std::to_string(value);
  return String{formatted.c_str()};
}

[[nodiscard]] auto LabeledNumber(const char *label, const std::uint64_t value)
    -> String {
  String result{label};
  result.Append(Number(value));
  return result;
}

[[nodiscard]] auto FontFaceLine(const FontFaceDiagnostics &face) -> String {
  String result{face.fallback ? "Fallback font: " : "Main font: "};
  result.Append(face.family);
  result.Append(String{" · used for "});
  result.Append(Number(face.resolvedCodePointCount));
  result.Append(String{" characters"});
  return result;
}

[[nodiscard]] auto GlyphSizeLine(const GlyphAtlasSizeDiagnostics &diagnostics)
    -> String {
  String result{"At "};
  result.Append(Number(diagnostics.pixelSize));
  result.Append(String{" px: "});
  result.Append(Number(diagnostics.entryCount));
  result.Append(String{" glyphs"});
  return result;
}

[[nodiscard]] auto TrackLine(const char *label, const NGIN::UIntSize index,
                             const std::vector<F32> &tracks) -> String {
  String result{label};
  result.Append(Number(index + 1));
  result.Append(String{" tracks: "});
  for (NGIN::UIntSize track = 0; track < tracks.size(); ++track) {
    if (track != 0) {
      result.Append(String{" / "});
    }
    result.Append(Number(static_cast<std::uint64_t>(tracks[track])));
  }
  return result;
}

[[nodiscard]] auto TextProperties(NativeTextSystem &text, const F32 fontSize,
                                  const Color color) -> NodeProperties {
  NodeProperties properties{};
  properties.layout.horizontalAlignment = HorizontalAlignment::Start;
  properties.layout.verticalAlignment = VerticalAlignment::Start;
  properties.interaction.hitTestVisible = false;
  properties.text.fontSize = fontSize;
  properties.text.color = color;
  properties.text.geometry = &text;
  properties.text.wrapping = TextWrapping::NoWrap;
  return properties;
}

void ComposeText(Composer &composer, NativeTextSystem &text, String value,
                 const F32 fontSize, const Color color,
                 const std::string_view key,
                 const SemanticRole role = SemanticRole::Text) {
  auto properties = TextProperties(text, fontSize, color);
  properties.semantics.role = role;
  composer.Text(std::move(value), text, text, properties, key);
}

void ComposeButton(Composer &composer, NativeTextSystem &text,
                   const Theme &theme, const char *label,
                   NGIN::Utilities::Callable<void()> onActivate,
                   const std::string_view key, const F32 width = 210.0F,
                   const bool enabled = true, const bool selected = false,
                   ToolTipController *toolTip = nullptr) {
  NodeProperties button{};
  button.layout.preferredSize = Size{width, theme.controls.regularHeight};
  button.layout.padding = Thickness{14.0F, 8.0F, 14.0F, 8.0F};
  button.layout.horizontalAlignment = HorizontalAlignment::Start;
  button.layout.verticalAlignment = VerticalAlignment::Start;
  button.interaction.enabled = enabled;
  button.interaction.focusable = true;
  button.interaction.onActivate = std::move(onActivate);
  button.semantics.role = SemanticRole::Button;
  button.semantics.label = String{label};
  button.semantics.actions =
      SemanticActionFlags::Activate | SemanticActionFlags::Focus;
  button.visual = MakeButtonVisual(theme);
  if (selected) {
    button.visual.state |= VisualStateFlags::Selected;
    button.visual.states.selected.background = theme.colors.raisedSurface;
    button.visual.states.selected.foreground = theme.colors.foreground;
    button.visual.states.selected.borderColor = theme.colors.focus;
  }
  if (toolTip != nullptr) {
    toolTip->Attach(button);
  }

  const auto labelColor = enabled ? (selected ? theme.colors.foreground
                                              : theme.colors.accentForeground)
                                  : theme.colors.disabledForeground;
  composer.Element(
      ElementType::Button, button,
      [&] {
        auto labelProperties =
            TextProperties(text, theme.typography.body, labelColor);
        labelProperties.layout.horizontalAlignment =
            HorizontalAlignment::Center;
        labelProperties.layout.verticalAlignment = VerticalAlignment::Center;
        composer.Text(String{label}, text, text, labelProperties, "label");
      },
      key);
}

template <typename ComposeContent>
void ComposeCard(Composer &composer, const Theme &theme,
                 ComposeContent &&composeContent, const std::string_view key,
                 const F32 width = 720.0F) {
  NodeProperties card{};
  card.layout.preferredSize.width = width;
  card.layout.maximumSize.width = width;
  card.layout.padding = Thickness::Uniform(Dp{theme.spacing.spacious});
  card.layout.horizontalAlignment = HorizontalAlignment::Start;
  card.layout.verticalAlignment = VerticalAlignment::Start;
  card.visual = MakePanelVisual(theme);
  card.semantics.role = SemanticRole::Group;
  composer.Border(std::forward<ComposeContent>(composeContent), card, key);
}

void ComposePageHeading(Composer &composer, NativeTextSystem &text,
                        const Theme &theme, const char *title,
                        const char *description) {
  ComposeText(composer, text, String{title}, 30.0F, theme.colors.foreground,
              "page-title", SemanticRole::Heading);
  ComposeText(composer, text, String{description}, theme.typography.body,
              theme.colors.mutedForeground, "page-description");
  NodeProperties separator{};
  separator.layout.preferredSize.width = 720.0F;
  separator.layout.horizontalAlignment = HorizontalAlignment::Start;
  separator.visual = MakeSeparatorVisual(theme);
  composer.Separator(SeparatorOrientation::Horizontal, separator,
                     "page-separator");
}

void ComposeTextField(Composer &composer, NativeTextSystem &text, Model &model,
                      const Theme &theme, Binding<String> value,
                      const char *label, const std::string_view key,
                      const bool readOnly = false, const bool enabled = true,
                      const bool invalid = false, const bool password = false) {
  NodeProperties field{};
  field.layout.preferredSize = Size{430.0F, theme.controls.spaciousHeight};
  field.layout.maximumSize.width = 520.0F;
  field.layout.padding = Thickness{12.0F, 10.0F, 12.0F, 10.0F};
  field.layout.horizontalAlignment = HorizontalAlignment::Start;
  field.layout.verticalAlignment = VerticalAlignment::Start;
  field.interaction.enabled = enabled;
  field.visual = MakeTextFieldVisual(theme);
  if (invalid) {
    field.visual.state |= VisualStateFlags::Invalid;
  }
  field.text.fontSize = 17.0F;
  field.text.color =
      enabled ? theme.colors.foreground : theme.colors.disabledForeground;
  field.text.layout = &text;
  field.text.geometry = &text;
  field.text.glyphAtlas = &text;
  field.text.wrapping = TextWrapping::NoWrap;
  field.textField.selectionColor = theme.colors.selection;
  field.textField.caretColor = theme.colors.focus;
  field.textField.compositionColor = theme.colors.focus;
  field.textField.readOnly = readOnly;
  field.textField.password = password;
  field.textField.onError = [&model](const UIError &error) {
    model.Report(error);
  };
  field.semantics.label = String{label};
  composer.TextField(std::move(value), text, field, key);
}

void ComposeSwatch(Composer &composer, const Theme &theme, const Color color,
                   const char *label, const std::string_view key) {
  NodeProperties swatch{};
  swatch.layout.preferredSize = Size{130.0F, 74.0F};
  swatch.layout.padding = Thickness::Uniform(Dp{theme.spacing.regular});
  swatch.layout.horizontalAlignment = HorizontalAlignment::Start;
  swatch.layout.verticalAlignment = VerticalAlignment::Start;
  swatch.visual = MakePanelVisual(theme);
  swatch.visual.base.background = color;
  swatch.semantics.role = SemanticRole::Group;
  swatch.semantics.label = String{label};
  composer.Border([] {}, swatch, key);
}

void StyleScrollView(NodeProperties &properties, const Theme &theme) {
  properties.interaction.focusable = true;
  properties.scroll.scrollbarTrack = theme.colors.sunkenSurface;
  properties.scroll.scrollbarThumb = theme.colors.border;
  properties.scroll.scrollbarThumbHovered = theme.colors.focus;
}

void ComposeOverviewPage(Composer &composer, NativeTextSystem &text,
                         Model &model, const Theme &theme) {
  ComposePageHeading(
      composer, text, theme, "Explore NGIN.UI",
      "Try controls, layouts, text, images, themes, popups, and windows.");

  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.regular;
        composer.Column(
            [&] {
              ComposeText(composer, text, String{"Included features"}, 19.0F,
                          theme.colors.foreground, "features-title",
                          SemanticRole::Heading);
              ComposeText(
                  composer, text,
                  String{"Desktop windows \xC2\xB7 Controls and layouts "
                         "\xC2\xB7 Unicode text"},
                  theme.typography.body, theme.colors.mutedForeground,
                  "features-value");
              ComposeText(
                  composer, text,
                  String{"Keyboard, mouse, clipboard, themes, and custom "
                         "controls."},
                  theme.typography.body, theme.colors.mutedForeground,
                  "hosting-value");
            },
            "features-column");
      },
      "features-card");

  NodeProperties metricsRow{};
  metricsRow.layout.gap = theme.spacing.regular;
  metricsRow.layout.horizontalAlignment = HorizontalAlignment::Start;
  metricsRow.layout.verticalAlignment = VerticalAlignment::Start;
  composer.Element(
      ElementType::Row, metricsRow,
      [&] {
        ComposeSwatch(composer, theme, theme.colors.accent, "Accent",
                      "accent-swatch");
        ComposeSwatch(composer, theme, theme.colors.selection, "Selection",
                      "selection-swatch");
        ComposeSwatch(composer, theme, theme.colors.focus, "Focus",
                      "focus-swatch");
      },
      "token-swatches");

  ComposeButton(
      composer, text, theme, "Click me", [&model] { model.Activate(); },
      "overview-activate", 250.0F);
  ComposeText(
      composer, text, LabeledNumber("Clicks: ", model.ActivationCount()),
      theme.typography.body, theme.colors.mutedForeground, "activation-count");

  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.regular;
        composer.Column(
            [&] {
              ComposeText(composer, text, String{"Custom controls"}, 19.0F,
                          theme.colors.foreground, "custom-title",
                          SemanticRole::Heading);
              ComposeText(
                  composer, text,
                  String{"A badge, progress ring, and interactive chart."},
                  theme.typography.body, theme.colors.mutedForeground,
                  "custom-description");
              ComposeCustomControlExamples(composer, text, theme);
            },
            "custom-column");
      },
      "custom-controls-card");
}

void ComposeLayoutPage(Composer &composer, NativeTextSystem &text,
                       const Theme &theme) {
  ComposePageHeading(
      composer, text, theme, "Layout",
      "Build forms, toolbars, dashboards, and free-positioned diagrams.");

  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.regular;
        composer.Column(
            [&] {
              ComposeText(composer, text, String{"Settings form"}, 18.0F,
                          theme.colors.foreground, "row-title",
                          SemanticRole::Heading);
              NodeProperties form{};
              form.layout.preferredSize.width = 680.0F;
              form.grid.columns = {GridTrack::Auto(120.0F, 180.0F),
                                   GridTrack::Weighted(1.0F)};
              form.grid.rows = {GridTrack::Auto(), GridTrack::Auto(),
                                GridTrack::Auto()};
              form.grid.columnGap = theme.spacing.spacious;
              form.grid.rowGap = theme.spacing.regular;
              composer.Grid(
                  [&] {
                    const auto composeRow = [&](const NGIN::UIntSize row,
                                                const char *label,
                                                const char *value,
                                                const std::string_view key) {
                      NodeProperties labelCell{};
                      labelCell.gridPlacement =
                          GridPlacement{.row = row, .column = 0};
                      composer.Element(
                          ElementType::Column, labelCell,
                          [&] {
                            ComposeText(composer, text, String{label},
                                        theme.typography.body,
                                        theme.colors.mutedForeground, "text");
                          },
                          std::string{key} + "-label");

                      NodeProperties valueCell{};
                      valueCell.gridPlacement =
                          GridPlacement{.row = row, .column = 1};
                      valueCell.layout.padding =
                          Thickness{12.0F, 9.0F, 12.0F, 9.0F};
                      valueCell.layout.horizontalAlignment =
                          HorizontalAlignment::Stretch;
                      valueCell.visual = MakePanelVisual(theme);
                      valueCell.visual.base.background =
                          theme.colors.sunkenSurface;
                      composer.Border(
                          [&] {
                            ComposeText(composer, text, String{value},
                                        theme.typography.body,
                                        theme.colors.foreground, "text");
                          },
                          valueCell, std::string{key} + "-value");
                    };
                    composeRow(0, "Project", "Desktop app", "project");
                    composeRow(1, "Theme", "System default", "theme");
                    composeRow(2, "Updates", "Install automatically",
                               "updates");
                  },
                  form, "settings-grid");
            },
            "layout-column");
      },
      "grid-card");

  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.regular;
        composer.Column(
            [&] {
              ComposeText(composer, text, String{"Responsive toolbar"}, 18.0F,
                          theme.colors.foreground, "toolbar-title",
                          SemanticRole::Heading);
              ComposeText(composer, text,
                          String{"Resize the window and the actions wrap."},
                          theme.typography.body, theme.colors.mutedForeground,
                          "toolbar-help");
              NodeProperties toolbar{};
              toolbar.layout.preferredSize.width = 680.0F;
              toolbar.wrapPanel.itemGap = theme.spacing.regular;
              toolbar.wrapPanel.lineGap = theme.spacing.regular;
              toolbar.wrapPanel.lineAlignment = WrapLineAlignment::Start;
              composer.WrapPanel(
                  [&] {
                    ComposeButton(
                        composer, text, theme, "New", [] {}, "new", 112.0F);
                    ComposeButton(
                        composer, text, theme, "Open", [] {}, "open", 112.0F);
                    ComposeButton(
                        composer, text, theme, "Save", [] {}, "save", 112.0F);
                    ComposeButton(
                        composer, text, theme, "Export", [] {}, "export",
                        112.0F);
                    ComposeButton(
                        composer, text, theme, "Share", [] {}, "share", 112.0F);
                  },
                  toolbar, "responsive-toolbar");
            },
            "toolbar-column");
      },
      "wrap-card");

  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.regular;
        composer.Column(
            [&] {
              ComposeText(composer, text, String{"Dashboard tiles"}, 18.0F,
                          theme.colors.foreground, "dashboard-title",
                          SemanticRole::Heading);
              NodeProperties dashboard{};
              dashboard.layout.preferredSize.width = 680.0F;
              dashboard.grid.columns = {GridTrack::Weighted(),
                                        GridTrack::Weighted(),
                                        GridTrack::Weighted()};
              dashboard.grid.rows = {GridTrack::Fixed(82.0F),
                                     GridTrack::Fixed(82.0F)};
              dashboard.grid.columnGap = theme.spacing.regular;
              dashboard.grid.rowGap = theme.spacing.regular;
              composer.Grid(
                  [&] {
                    const auto composeTile =
                        [&](GridPlacement placement, const char *label,
                            const Color color, const std::string_view key) {
                          NodeProperties tile{};
                          tile.gridPlacement = placement;
                          tile.layout.padding =
                              Thickness::Uniform(Dp{theme.spacing.regular});
                          tile.layout.horizontalAlignment =
                              HorizontalAlignment::Stretch;
                          tile.layout.verticalAlignment =
                              VerticalAlignment::Stretch;
                          tile.visual = MakePanelVisual(theme);
                          tile.visual.base.background = color;
                          composer.Border(
                              [&] {
                                ComposeText(composer, text, String{label},
                                            theme.typography.body,
                                            theme.colors.foreground, "label");
                              },
                              tile, key);
                        };
                    composeTile(
                        GridPlacement{.row = 0, .column = 0, .columnSpan = 2},
                        "Activity", theme.colors.raisedSurface, "activity");
                    composeTile(GridPlacement{.row = 0, .column = 2}, "Status",
                                theme.colors.selection, "status");
                    composeTile(GridPlacement{.row = 1, .column = 0}, "Files",
                                theme.colors.sunkenSurface, "files");
                    composeTile(
                        GridPlacement{.row = 1, .column = 1, .columnSpan = 2},
                        "Recent work", theme.colors.raisedSurface, "recent");
                  },
                  dashboard, "dashboard-grid");
            },
            "dashboard-column");
      },
      "dashboard-card");

  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.regular;
        composer.Column(
            [&] {
              ComposeText(composer, text, String{"Canvas diagram"}, 18.0F,
                          theme.colors.foreground, "canvas-title",
                          SemanticRole::Heading);
              NodeProperties canvas{};
              canvas.layout.preferredSize = Size{680.0F, 160.0F};
              canvas.visual = MakePanelVisual(theme);
              canvas.visual.base.background = theme.colors.sunkenSurface;
              composer.Canvas(
                  [&] {
                    const auto composeNode = [&](Point offset,
                                                 const char *label,
                                                 const std::string_view key) {
                      NodeProperties node{};
                      node.canvasPlacement.offset = offset;
                      node.canvasPlacement.contributesToDesiredSize = false;
                      node.layout.preferredSize = Size{150.0F, 48.0F};
                      node.layout.padding =
                          Thickness::Uniform(Dp{theme.spacing.regular});
                      node.visual = MakePanelVisual(theme);
                      node.visual.base.background = theme.colors.raisedSurface;
                      composer.Border(
                          [&] {
                            ComposeText(composer, text, String{label},
                                        theme.typography.body,
                                        theme.colors.foreground, "label");
                          },
                          node, key);
                    };
                    composeNode(Point{24.0F, 24.0F}, "Load data", "load");
                    composeNode(Point{260.0F, 82.0F}, "Process", "process");
                    composeNode(Point{496.0F, 24.0F}, "Show result", "result");
                  },
                  canvas, "diagram-canvas");
            },
            "canvas-column");
      },
      "canvas-card");

  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties scroll{};
        scroll.layout.preferredSize = Size{680.0F, 190.0F};
        scroll.layout.maximumSize = Size{680.0F, 190.0F};
        scroll.layout.horizontalAlignment = HorizontalAlignment::Start;
        scroll.layout.verticalAlignment = VerticalAlignment::Start;
        scroll.scroll.vertical = true;
        StyleScrollView(scroll, theme);
        composer.ScrollView(
            [&] {
              NodeProperties list{};
              list.layout.gap = theme.spacing.regular;
              list.layout.padding = Thickness::Uniform(Dp{4.0F});
              composer.Column(
                  [&] {
                    for (std::uint32_t index = 1; index <= 12; ++index) {
                      ComposeText(composer, text,
                                  LabeledNumber("Scrollable item ", index),
                                  theme.typography.body,
                                  theme.colors.foreground,
                                  std::to_string(index));
                    }
                  },
                  "scroll-content");
            },
            scroll, "nested-scroll");
      },
      "scroll-card");
}

void ComposeTypographyPage(Composer &composer, NativeTextSystem &text,
                           const Theme &theme) {
  ComposePageHeading(
      composer, text, theme, "Typography",
      "Text sizes, languages, symbols, wrapping, and alignment.");
  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.regular;
        composer.Column(
            [&] {
              ComposeText(composer, text, String{"Display 34"}, 34.0F,
                          theme.colors.foreground, "display");
              ComposeText(composer, text, String{"Title 20"}, 20.0F,
                          theme.colors.foreground, "title");
              ComposeText(composer, text, String{"Latin: naïve café"}, 14.0F,
                          theme.colors.foreground, "unicode");
              ComposeText(composer, text, String{"Greek: Ελληνικά"}, 16.0F,
                          theme.colors.foreground, "greek");
              ComposeText(composer, text, String{"Cyrillic: Кириллица"}, 16.0F,
                          theme.colors.foreground, "cyrillic");
              ComposeText(composer, text,
                          String{"Arabic: \xD8\xA7\xD9\x84\xD8\xB9\xD8\xB1"
                                 "\xD8\xA8\xD9\x8A\xD8\xA9"},
                          18.0F, theme.colors.foreground, "arabic");
              ComposeText(composer, text,
                          String{"Mixed direction: English · \xD9\x85\xD8\xB1"
                                 "\xD8\xAD\xD8\xA8\xD8\xA7"},
                          16.0F, theme.colors.foreground, "bidi");
              ComposeText(composer, text,
                          String{"Combined letters: e\xCC\x81  A\xCC\x88"},
                          16.0F, theme.colors.foreground, "graphemes");
              ComposeText(composer, text,
                          String{"Symbols: \xE2\x9C\x93  \xE2\x98\x85"}, 18.0F,
                          theme.colors.foreground, "symbols");
              ComposeText(composer, text,
                          String{"Color emoji: not included in version 0.2"},
                          14.0F, theme.colors.mutedForeground, "emoji-policy");
              NodeProperties paragraph =
                  TextProperties(text, 16.0F, theme.colors.foreground);
              paragraph.layout.preferredSize.width = 650.0F;
              paragraph.layout.maximumSize.width = 650.0F;
              paragraph.text.wrapping = TextWrapping::Wrap;
              paragraph.text.lineHeight = 24.0F;
              paragraph.text.alignment = TextAlignment::Start;
              composer.Text(
                  String{
                      "Long text wraps automatically. It also keeps manual "
                      "line breaks and works with text from many languages."},
                  text, text, paragraph, "wrapped-paragraph");

              auto centered = paragraph;
              centered.text.alignment = TextAlignment::Center;
              centered.text.color = theme.colors.mutedForeground;
              composer.Text(String{"Centered text\nworks on every line."}, text,
                            text, centered, "centered-paragraph");
            },
            "typography-column");
      },
      "typography-card");
}

void ComposeTextAreaPage(Composer &composer, NativeTextSystem &text,
                         Model &model, const Theme &theme) {
  ComposePageHeading(
      composer, text, theme, "Text Area",
      "Write, select, copy, paste, and move through multiple lines.");
  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.regular;
        composer.Column(
            [&] {
              ComposeText(composer, text, String{"Editable project notes"},
                          20.0F, theme.colors.foreground, "text-area-title",
                          SemanticRole::Heading);
              ComposeText(
                  composer, text,
                  String{"Try Enter, Shift+Arrow, Home, End, Up, and Down."},
                  14.0F, theme.colors.mutedForeground, "text-area-help");

              NodeProperties area{};
              area.layout.preferredSize = Size{650.0F, 230.0F};
              area.layout.maximumSize = Size{650.0F, 230.0F};
              area.layout.padding = Thickness{14.0F, 12.0F, 14.0F, 12.0F};
              area.layout.horizontalAlignment = HorizontalAlignment::Start;
              area.layout.verticalAlignment = VerticalAlignment::Start;
              area.visual = MakeTextFieldVisual(theme);
              area.text.layout = &text;
              area.text.geometry = &text;
              area.text.glyphAtlas = &text;
              area.text.fontSize = 16.0F;
              area.text.lineHeight = 23.0F;
              area.text.wrapping = TextWrapping::Wrap;
              area.text.color = theme.colors.foreground;
              area.textField.selectionColor = theme.colors.selection;
              area.textField.caretColor = theme.colors.focus;
              area.textField.compositionColor = theme.colors.focus;
              area.textField.onError = [&model](const UIError &error) {
                model.Report(error);
              };
              area.semantics.label = String{"Project notes"};
              StyleScrollView(area, theme);
              composer.TextArea(model.NotesBinding(), text, area,
                                "project-notes");
            },
            "text-area-column");
      },
      "text-area-card");
}

void ComposeImagesPage(Composer &composer, NativeTextSystem &text, Model &model,
                       const Theme &theme) {
  ComposePageHeading(composer, text, theme, "Images",
                     "Load PNG and JPEG files, then choose how they fit.");
  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.spacious;
        composer.Column(
            [&] {
              ComposeText(composer, text, String{"PNG file"}, 20.0F,
                          theme.colors.foreground, "image-title",
                          SemanticRole::Heading);
              ComposeText(
                  composer, text,
                  String{"This app image is shown in three different ways."},
                  14.0F, theme.colors.mutedForeground, "image-help");

              NodeProperties row{};
              row.layout.gap = theme.spacing.regular;
              row.layout.horizontalAlignment = HorizontalAlignment::Start;
              row.layout.verticalAlignment = VerticalAlignment::Start;
              composer.Element(
                  ElementType::Row, row,
                  [&] {
                    const auto composeSample =
                        [&](const char *label, const ImageFit fit,
                            const ImageAlignment alignment, const Color tint,
                            const std::string_view key) {
                          NodeProperties tile{};
                          tile.layout.preferredSize = Size{205.0F, 170.0F};
                          tile.layout.maximumSize = Size{205.0F, 170.0F};
                          tile.layout.padding =
                              Thickness::Uniform(Dp{theme.spacing.compact});
                          tile.layout.gap = theme.spacing.compact;
                          tile.layout.horizontalAlignment =
                              HorizontalAlignment::Start;
                          tile.layout.verticalAlignment =
                              VerticalAlignment::Start;
                          tile.visual = MakePanelVisual(theme);
                          composer.Element(
                              ElementType::Column, tile,
                              [&] {
                                ComposeText(composer, text, String{label},
                                            14.0F, theme.colors.foreground,
                                            "label");
                                NodeProperties image{};
                                image.layout.preferredSize =
                                    Size{185.0F, 125.0F};
                                image.layout.maximumSize = Size{185.0F, 125.0F};
                                image.layout.horizontalAlignment =
                                    HorizontalAlignment::Start;
                                image.layout.verticalAlignment =
                                    VerticalAlignment::Start;
                                image.image.fit = fit;
                                image.image.alignment = alignment;
                                image.image.tint = tint;
                                if (model.ImageCache() != nullptr &&
                                    model.GalleryImage()) {
                                  composer.Image(
                                      model.GalleryImage(), *model.ImageCache(),
                                      String{"Blue and violet abstract scene "
                                             "with a glowing coral cube"},
                                      image, "image");
                                }
                              },
                              key);
                        };
                    composeSample("Fit inside", ImageFit::Contain,
                                  ImageAlignment{0.5F, 0.5F},
                                  Color{1.0F, 1.0F, 1.0F, 1.0F}, "contain");
                    composeSample("Fill · align right", ImageFit::Cover,
                                  ImageAlignment{1.0F, 0.5F},
                                  Color{1.0F, 1.0F, 1.0F, 1.0F}, "cover");
                    composeSample("Fill · warm tint", ImageFit::Cover,
                                  ImageAlignment{0.0F, 0.5F},
                                  Color{1.0F, 0.78F, 0.68F, 0.9F}, "tinted");
                  },
                  "image-samples");
            },
            "image-column");
      },
      "image-card");
}

template <typename ComposeControl>
void ComposeControlRow(Composer &composer, NativeTextSystem &text,
                       const Theme &theme, const char *label,
                       const std::string_view identifier,
                       ComposeControl &&composeControl,
                       const std::string_view key) {
  NodeProperties row{};
  row.layout.gap = theme.spacing.spacious;
  row.layout.horizontalAlignment = HorizontalAlignment::Start;
  row.layout.verticalAlignment = VerticalAlignment::Center;
  composer.Element(
      ElementType::Row, row,
      [&] {
        auto labelProperties = TextProperties(text, theme.typography.body,
                                              theme.colors.foreground);
        labelProperties.layout.preferredSize.width = 220.0F;
        labelProperties.layout.maximumSize.width = 220.0F;
        labelProperties.layout.verticalAlignment = VerticalAlignment::Center;
        const auto labelIdentifier = std::string{identifier} + "-label";
        Label(composer, String{label}, text, text, labelIdentifier, identifier,
              labelProperties, "label");

        NodeProperties control{};
        control.layout.horizontalAlignment = HorizontalAlignment::Start;
        control.layout.verticalAlignment = VerticalAlignment::Center;
        control.semantics.identifier = String{identifier};
        control.semantics.labelledBy = String{labelIdentifier.c_str()};
        control.semantics.label = String{label};
        std::forward<ComposeControl>(composeControl)(control);
      },
      key);
}

void ComposeInputsPage(Composer &composer, NativeTextSystem &text, Model &model,
                       const Theme &theme) {
  ComposePageHeading(
      composer, text, theme, "Inputs",
      "Buttons, checkboxes, radio buttons, switches, sliders, progress bars, "
      "tooltips, and text fields.");

  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.regular;
        composer.Column(
            [&] {
              ComposeText(composer, text, String{"Button states"}, 18.0F,
                          theme.colors.foreground, "button-states-title",
                          SemanticRole::Heading);
              NodeProperties row{};
              row.layout.gap = theme.spacing.regular;
              composer.Row(
                  [&] {
                    ComposeButton(
                        composer, text, theme, "Normal", [] {}, "normal",
                        150.0F);
                    ComposeButton(
                        composer, text, theme, "Selected", [] {}, "selected",
                        150.0F, true, true);
                    ComposeButton(
                        composer, text, theme, "Disabled", [] {}, "disabled",
                        150.0F, false);
                  },
                  "button-states");
            },
            "buttons-column");
      },
      "buttons-card");

  const ControlPresentation normalControl{.theme = theme};
  const ControlPresentation invalidControl{
      .theme = theme,
      .invalid = true,
      .onError = [&model](const UIError &error) { model.Report(error); },
  };
  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.regular;
        composer.Column(
            [&] {
              ComposeText(composer, text, String{"Choices and switches"}, 18.0F,
                          theme.colors.foreground, "selection-title",
                          SemanticRole::Heading);
              ComposeControlRow(
                  composer, text, theme, "Checkbox — checked", "settings-check",
                  [&](const NodeProperties &control) {
                    CheckBox(composer, model.CheckBinding(), normalControl,
                             control, "control");
                  },
                  "checked-row");
              ComposeControlRow(
                  composer, text, theme, "Checkbox — mixed", "settings-mixed",
                  [&](const NodeProperties &control) {
                    CheckBox(composer, model.MixedCheckBinding(), normalControl,
                             control, "control");
                  },
                  "mixed-row");
              ComposeControlRow(
                  composer, text, theme, "Checkbox — disabled",
                  "settings-disabled-check",
                  [&](NodeProperties control) {
                    control.interaction.enabled = false;
                    CheckBox(composer, model.UncheckedBinding(), normalControl,
                             control, "control");
                  },
                  "disabled-check-row");
              ComposeControlRow(
                  composer, text, theme, "Checkbox — error",
                  "settings-invalid-check",
                  [&](const NodeProperties &control) {
                    CheckBox(composer, model.UncheckedBinding(), invalidControl,
                             control, "control");
                  },
                  "invalid-check-row");
              ComposeControlRow(
                  composer, text, theme, "Radio button — compact",
                  "density-compact",
                  [&](const NodeProperties &control) {
                    RadioButton(
                        composer,
                        BindRadio(model.DensityBinding(), Density::Compact),
                        normalControl, control, "control");
                  },
                  "radio-compact-row");
              ComposeControlRow(
                  composer, text, theme, "Radio button — comfortable",
                  "density-comfortable",
                  [&](const NodeProperties &control) {
                    RadioButton(
                        composer,
                        BindRadio(model.DensityBinding(), Density::Comfortable),
                        normalControl, control, "control");
                  },
                  "radio-comfortable-row");
              ComposeControlRow(
                  composer, text, theme, "Radio button — disabled",
                  "density-disabled",
                  [&](NodeProperties control) {
                    control.interaction.enabled = false;
                    RadioButton(
                        composer,
                        BindRadio(model.DensityBinding(), Density::Spacious),
                        normalControl, control, "control");
                  },
                  "radio-disabled-row");
              ComposeControlRow(
                  composer, text, theme, "Radio button — error",
                  "density-invalid",
                  [&](const NodeProperties &control) {
                    RadioButton(
                        composer,
                        BindRadio(model.DensityBinding(), Density::Spacious),
                        invalidControl, control, "control");
                  },
                  "radio-invalid-row");
              ComposeControlRow(
                  composer, text, theme, "Switch — on", "updates-toggle",
                  [&](const NodeProperties &control) {
                    ToggleSwitch(composer, model.ToggleBinding(), normalControl,
                                 control, "control");
                  },
                  "toggle-row");
              ComposeControlRow(
                  composer, text, theme, "Switch — disabled",
                  "updates-disabled-toggle",
                  [&](NodeProperties control) {
                    control.interaction.enabled = false;
                    ToggleSwitch(composer, model.DisabledToggleBinding(),
                                 normalControl, control, "control");
                  },
                  "toggle-disabled-row");
              ComposeControlRow(
                  composer, text, theme, "Switch — error",
                  "updates-invalid-toggle",
                  [&](const NodeProperties &control) {
                    ToggleSwitch(composer, model.DisabledToggleBinding(),
                                 invalidControl, control, "control");
                  },
                  "toggle-invalid-row");
            },
            "selection-controls");
      },
      "selection-controls-card");

  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.regular;
        composer.Column(
            [&] {
              ComposeText(composer, text, String{"Sliders and progress"}, 18.0F,
                          theme.colors.foreground, "range-title",
                          SemanticRole::Heading);
              ComposeControlRow(
                  composer, text, theme, "Slider", "volume-slider",
                  [&](NodeProperties control) {
                    control.layout.preferredSize.width = 320.0F;
                    Slider(composer, model.SliderBinding(),
                           SliderRange{
                               .minimum = 0.0F, .maximum = 1.0F, .step = 0.05F},
                           normalControl, control, "control");
                  },
                  "slider-row");
              ComposeControlRow(
                  composer, text, theme, "Slider — error", "invalid-slider",
                  [&](NodeProperties control) {
                    control.layout.preferredSize.width = 320.0F;
                    Slider(composer, model.SliderBinding(), {}, invalidControl,
                           control, "control");
                  },
                  "invalid-slider-row");
              ComposeControlRow(
                  composer, text, theme, "Slider — disabled", "disabled-slider",
                  [&](NodeProperties control) {
                    control.layout.preferredSize.width = 320.0F;
                    control.interaction.enabled = false;
                    Slider(composer, model.SliderBinding(), {}, normalControl,
                           control, "control");
                  },
                  "disabled-slider-row");
              ComposeControlRow(
                  composer, text, theme, "Progress bar", "progress-determinate",
                  [&](NodeProperties control) {
                    control.layout.preferredSize.width = 320.0F;
                    ProgressBar(composer,
                                ProgressValue{.value = model.SliderValue(),
                                              .minimum = 0.0F,
                                              .maximum = 1.0F},
                                normalControl, control, "control");
                  },
                  "progress-row");
              ComposeControlRow(
                  composer, text, theme, "Progress bar — busy",
                  "progress-indeterminate",
                  [&](NodeProperties control) {
                    control.layout.preferredSize.width = 320.0F;
                    ProgressBar(composer,
                                ProgressValue{.value = 0.0F,
                                              .minimum = 0.0F,
                                              .maximum = 1.0F,
                                              .indeterminate = true},
                                normalControl, control, "control");
                  },
                  "progress-indeterminate-row");
              ComposeControlRow(
                  composer, text, theme, "Progress bar — error",
                  "progress-invalid",
                  [&](NodeProperties control) {
                    control.layout.preferredSize.width = 320.0F;
                    ProgressBar(composer,
                                ProgressValue{.value = 0.35F,
                                              .minimum = 0.0F,
                                              .maximum = 1.0F},
                                invalidControl, control, "control");
                  },
                  "progress-invalid-row");
              ComposeControlRow(
                  composer, text, theme, "Progress bar — disabled",
                  "progress-disabled",
                  [&](NodeProperties control) {
                    control.layout.preferredSize.width = 320.0F;
                    control.interaction.enabled = false;
                    ProgressBar(composer,
                                ProgressValue{.value = 0.48F,
                                              .minimum = 0.0F,
                                              .maximum = 1.0F},
                                normalControl, control, "control");
                  },
                  "progress-disabled-row");
              ComposeText(composer, text,
                          String{"Try Tab, Space, Enter, and the arrow keys."},
                          theme.typography.caption,
                          theme.colors.mutedForeground, "keyboard-help");
            },
            "range-controls");
      },
      "range-controls-card");

  ComposeCard(
      composer, theme,
      [&] {
        ComposeText(composer, text, String{"Tooltip"}, 18.0F,
                    theme.colors.foreground, "tooltip-title",
                    SemanticRole::Heading);
        ComposeButton(
            composer, text, theme, "Hover for help", [] {}, "tooltip-target",
            250.0F, true, false, model.HelpToolTip());
        if (auto *toolTip = model.HelpToolTip(); toolTip != nullptr) {
          toolTip->Compose(
              composer,
              [&] {
                ComposeCard(
                    composer, theme,
                    [&] {
                      ComposeText(composer, text,
                                  String{"Helpful text after a short delay"},
                                  theme.typography.caption,
                                  theme.colors.foreground, "tooltip-text");
                    },
                    "tooltip-card", 280.0F);
              },
              "delayed-tooltip");
        }
      },
      "tooltip-demo-card");

  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.regular;
        composer.Column(
            [&] {
              ComposeText(composer, text, String{"Editable"}, 14.0F,
                          theme.colors.mutedForeground, "editable-label");
              ComposeTextField(composer, text, model, theme,
                               model.NameBinding(), "Name", "editable");
              ComposeText(composer, text, String{"Read-only"}, 14.0F,
                          theme.colors.mutedForeground, "readonly-label");
              ComposeTextField(composer, text, model, theme,
                               model.NameBinding(), "Read-only name",
                               "readonly", true);
              ComposeText(composer, text, String{"Error"}, 14.0F,
                          theme.colors.mutedForeground, "invalid-label");
              ComposeTextField(composer, text, model, theme,
                               model.NameBinding(), "Invalid example",
                               "invalid", false, true, true);
              ComposeText(composer, text, String{"Password"}, 14.0F,
                          theme.colors.mutedForeground, "password-label");
              ComposeTextField(composer, text, model, theme,
                               model.PasswordBinding(), "Password", "password",
                               false, true, false, true);
              ComposeText(composer, text, String{"Disabled"}, 14.0F,
                          theme.colors.mutedForeground, "disabled-label");
              ComposeTextField(composer, text, model, theme,
                               model.NameBinding(), "Disabled field",
                               "disabled-field", false, false);
            },
            "fields-column");
      },
      "fields-card");
}

void ComposeCollectionsPage(Composer &composer, NativeTextSystem &text,
                            Model &model, const Theme &theme) {
  ComposePageHeading(composer, text, theme, "Collections",
                     "Small lists, huge lists, tabs, menus, and combo boxes.");

  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties content{};
        content.layout.gap = theme.spacing.regular;
        composer.Element(
            ElementType::Column, content,
            [&] {
              ComposeText(composer, text, String{"100,000-item list"}, 20.0F,
                          theme.colors.foreground, "virtual-list-title",
                          SemanticRole::Heading);
              ComposeText(
                  composer, text,
                  String{"Scroll or press End. Only the rows near the screen "
                         "are created."},
                  theme.typography.body, theme.colors.mutedForeground,
                  "virtual-list-help");

              NodeProperties actions{};
              actions.layout.gap = theme.spacing.regular;
              actions.layout.horizontalAlignment = HorizontalAlignment::Start;
              composer.Element(
                  ElementType::Row, actions,
                  [&] {
                    ComposeButton(
                        composer, text, theme, "Add 250 rows above",
                        [&model] { model.PrependVirtualizedItems(); },
                        "prepend-virtual-items", 220.0F);
                    const auto selected = model.SelectedVirtualizedIndex();
                    ComposeText(composer, text,
                                selected ? LabeledNumber("Selected row: ",
                                                         *selected + 1)
                                         : String{"No row selected"},
                                theme.typography.body,
                                theme.colors.mutedForeground,
                                "virtual-selected-row");
                  },
                  "virtual-list-actions");

              auto &source = model.VirtualizedCollectionSource();
              auto &controller = model.VirtualizedCollectionController();
              VirtualizedListPresentation presentation{};
              presentation.list.layout.preferredSize = Size{680.0F, 280.0F};
              presentation.list.layout.maximumSize = Size{680.0F, 280.0F};
              presentation.list.layout.horizontalAlignment =
                  HorizontalAlignment::Start;
              presentation.list.layout.verticalAlignment =
                  VerticalAlignment::Start;
              presentation.list.layout.padding = Thickness::Uniform(Dp{6.0F});
              presentation.list.visual = MakePanelVisual(theme);
              presentation.list.visual.base.background =
                  theme.colors.sunkenSurface;
              presentation.list.semantics.label =
                  String{"100,000-item virtual list"};
              StyleScrollView(presentation.list, theme);
              presentation.item.layout.padding =
                  Thickness{12.0F, 8.0F, 12.0F, 8.0F};
              presentation.item.visual = MakePanelVisual(theme);
              presentation.item.visual.base.background =
                  theme.colors.raisedSurface;
              presentation.item.visual.states.hovered.borderColor =
                  theme.colors.focus;
              presentation.item.visual.states.pressed.background =
                  theme.colors.accentPressed;
              presentation.item.visual.states.selected.background =
                  theme.colors.accent;
              presentation.item.visual.states.selected.borderColor =
                  theme.colors.focus;
              presentation.selectedIndex = [&model] {
                return model.SelectedVirtualizedIndex();
              };
              presentation.isSelected = [&model](const auto index) {
                return model.SelectedVirtualizedIndex() == index;
              };
              presentation.activate = [&model](const auto index) {
                return model.SelectVirtualizedItem(index);
              };
              presentation.onError = [&model](const UIError &error) {
                model.Report(error);
              };
              VirtualizedListView<NGIN::UIntSize>(
                  composer, controller, source,
                  [&](Composer &, const NGIN::UIntSize &,
                      const NGIN::UIntSize index) {
                    auto label = source.LabelAt(index);
                    ComposeText(composer, text,
                                label ? std::move(label).Value()
                                      : String{"Loading row"},
                                theme.typography.body,
                                model.SelectedVirtualizedIndex() == index
                                    ? theme.colors.accentForeground
                                    : theme.colors.foreground,
                                "virtual-row-label");
                  },
                  presentation, "virtual-list-100000");

              const auto diagnostics = controller.Diagnostics();
              String rangeLine{"Rows on screen: "};
              if (diagnostics.realized.count == 0) {
                rangeLine.Append(String{"none"});
              } else {
                rangeLine.Append(Number(diagnostics.realized.first + 1));
                rangeLine.Append(String{" to "});
                rangeLine.Append(Number(diagnostics.realized.End()));
              }
              rangeLine.Append(String{" · created: "});
              rangeLine.Append(Number(diagnostics.realizedNodeCount));
              rangeLine.Append(String{" · range loads: "});
              rangeLine.Append(Number(diagnostics.rangeRequestCount));
              ComposeText(composer, text, std::move(rangeLine),
                          theme.typography.caption,
                          theme.colors.mutedForeground,
                          "virtual-list-diagnostics");
            },
            "virtual-list-content");
      },
      "virtual-list-card");

  NodeProperties actions{};
  actions.layout.gap = theme.spacing.regular;
  actions.layout.horizontalAlignment = HorizontalAlignment::Start;
  composer.Element(
      ElementType::Row, actions,
      [&] {
        ComposeButton(
            composer, text, theme, "Add item",
            [&model] { model.AddCollectionItem(); }, "add-item", 150.0F);
        ComposeButton(
            composer, text, theme, "Remove selected",
            [&model] { model.RemoveSelectedCollectionItem(); }, "remove-item",
            170.0F);
        ComposeButton(
            composer, text, theme,
            model.IsCollectionDescending() ? "Sort ascending"
                                           : "Sort descending",
            [&model] { model.ToggleCollectionSort(); }, "sort-items", 170.0F);
        ComposeButton(
            composer, text, theme,
            model.IsCollectionFiltered() ? "Show all" : "Filter even items",
            [&model] { model.ToggleCollectionFilter(); }, "filter-items",
            160.0F);
      },
      "collection-actions");

  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties collectionColumn{};
        collectionColumn.layout.gap = theme.spacing.regular;
        composer.Element(
            ElementType::Column, collectionColumn,
            [&] {
              ComposeText(composer, text, String{"Selectable list"}, 20.0F,
                          theme.colors.foreground, "list-title",
                          SemanticRole::Heading);
              ComposeText(
                  composer, text,
                  String{"Use the arrow keys, Home, End, or type a letter."},
                  theme.typography.body, theme.colors.mutedForeground,
                  "list-help");
              NodeProperties list{};
              list.layout.preferredSize = Size{680.0F, 250.0F};
              list.layout.maximumSize = Size{680.0F, 250.0F};
              list.layout.horizontalAlignment = HorizontalAlignment::Start;
              list.layout.verticalAlignment = VerticalAlignment::Start;
              list.layout.padding = Thickness::Uniform(Dp{6.0F});
              list.visual = MakePanelVisual(theme);
              list.visual.base.background = theme.colors.sunkenSurface;
              list.semantics.label = String{"Gallery items"};
              StyleScrollView(list, theme);
              const auto items = model.CollectionItems();
              const auto selected = model.SelectedCollectionItem();
              composer.ListView(
                  [&] {
                    NodeProperties itemColumn{};
                    itemColumn.layout.gap = theme.spacing.compact;
                    composer.Element(
                        ElementType::Column, itemColumn,
                        [&] {
                          for (const auto item : items) {
                            auto itemProperties = NodeProperties{};
                            itemProperties.layout.preferredSize =
                                Size{650.0F, 42.0F};
                            itemProperties.layout.padding =
                                Thickness{12.0F, 8.0F, 12.0F, 8.0F};
                            itemProperties.layout.horizontalAlignment =
                                HorizontalAlignment::Start;
                            itemProperties.layout.verticalAlignment =
                                VerticalAlignment::Start;
                            itemProperties.visual = MakePanelVisual(theme);
                            itemProperties.visual.base.background =
                                theme.colors.raisedSurface;
                            itemProperties.visual.states.hovered.borderColor =
                                theme.colors.focus;
                            itemProperties.visual.states.pressed.background =
                                theme.colors.accentPressed;
                            itemProperties.visual.states.selected.background =
                                theme.colors.accent;
                            itemProperties.visual.states.selected.borderColor =
                                theme.colors.focus;
                            itemProperties.semantics.label =
                                LabeledNumber("Item ", item);
                            const auto key = std::to_string(item);
                            SelectableListItem(
                                composer, model.CollectionSelection(item),
                                [&] {
                                  ComposeText(
                                      composer, text,
                                      LabeledNumber(selected &&
                                                            *selected == item
                                                        ? "> Item "
                                                        : "  Item ",
                                                    item),
                                      15.0F,
                                      selected && *selected == item
                                          ? theme.colors.accentForeground
                                          : theme.colors.foreground,
                                      "item-label");
                                },
                                itemProperties,
                                CollectionPresentation{
                                    .onError =
                                        [&model](const UIError &error) {
                                          model.Report(error);
                                        },
                                },
                                key);
                          }
                        },
                        "keyed-items");
                  },
                  list, "collection-list");
            },
            "collection-content");
      },
      "collection-card");

  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties navigationColumn{};
        navigationColumn.layout.gap = theme.spacing.regular;
        composer.Element(
            ElementType::Column, navigationColumn,
            [&] {
              ComposeText(composer, text, String{"Combo box, tabs, and menus"},
                          20.0F, theme.colors.foreground, "navigation-title",
                          SemanticRole::Heading);
              ComposeText(composer, text,
                          String{"Open each control and try its options."},
                          theme.typography.body, theme.colors.mutedForeground,
                          "navigation-help");

              NodeProperties navigationRow{};
              navigationRow.layout.gap = theme.spacing.spacious;
              navigationRow.layout.horizontalAlignment =
                  HorizontalAlignment::Start;
              composer.Element(
                  ElementType::Row, navigationRow,
                  [&] {
                    NodeProperties comboButton{};
                    comboButton.layout.preferredSize =
                        Size{230.0F, theme.controls.regularHeight};
                    comboButton.layout.padding =
                        Thickness{14.0F, 8.0F, 14.0F, 8.0F};
                    comboButton.layout.horizontalAlignment =
                        HorizontalAlignment::Start;
                    comboButton.layout.verticalAlignment =
                        VerticalAlignment::Start;
                    comboButton.visual = MakeButtonVisual(theme);
                    comboButton.semantics.label = String{"Density"};

                    NodeProperties comboPopup{};
                    ComboBox(
                        composer, model.ComboPopup(), "gallery-density-combo",
                        [&] {
                          const auto label =
                              model.DensityBinding().Get() == Density::Compact
                                  ? "Density: Compact"
                              : model.DensityBinding().Get() ==
                                      Density::Spacious
                                  ? "Density: Spacious"
                                  : "Density: Comfortable";
                          ComposeText(composer, text, String{label}, 14.0F,
                                      theme.colors.accentForeground,
                                      "combo-summary");
                        },
                        [&] {
                          NodeProperties popupCard{};
                          popupCard.layout.preferredSize.width = 230.0F;
                          popupCard.layout.padding =
                              Thickness::Uniform(Dp{theme.spacing.compact});
                          popupCard.visual = MakePanelVisual(theme);
                          composer.Border(
                              [&] {
                                NodeProperties optionColumn{};
                                optionColumn.layout.gap = theme.spacing.compact;
                                composer.Element(
                                    ElementType::Column, optionColumn,
                                    [&] {
                                      constexpr std::array options{
                                          Density::Compact,
                                          Density::Comfortable,
                                          Density::Spacious};
                                      constexpr std::array labels{
                                          "Compact", "Comfortable", "Spacious"};
                                      for (NGIN::UIntSize index = 0;
                                           index < options.size(); ++index) {
                                        auto option = NodeProperties{};
                                        option.layout.preferredSize =
                                            Size{210.0F, 36.0F};
                                        option.layout.padding =
                                            Thickness{10.0F, 7.0F, 10.0F, 7.0F};
                                        option.visual = MakePanelVisual(theme);
                                        option.visual.states.selected
                                            .background = theme.colors.accent;
                                        option.semantics.label =
                                            String{labels[index]};
                                        auto selection =
                                            BindListItem(model.DensityBinding(),
                                                         options[index]);
                                        auto select = selection.select;
                                        selection.select =
                                            [select = std::move(select),
                                             &model]() mutable
                                            -> UIResult<void> {
                                          auto result = select();
                                          if (result) {
                                            model.ComboPopup().Close();
                                          }
                                          return result;
                                        };
                                        SelectableListItem(
                                            composer, std::move(selection),
                                            [&] {
                                              ComposeText(
                                                  composer, text,
                                                  String{labels[index]}, 14.0F,
                                                  theme.colors.foreground,
                                                  "option-label");
                                            },
                                            option, {}, labels[index]);
                                      }
                                    },
                                    "density-options");
                              },
                              popupCard, "density-popup-card");
                        },
                        comboButton, comboPopup, "density-combo");

                    NodeProperties menuButton{};
                    menuButton.layout.preferredSize =
                        Size{190.0F, theme.controls.regularHeight};
                    menuButton.layout.padding =
                        Thickness{14.0F, 8.0F, 14.0F, 8.0F};
                    menuButton.layout.horizontalAlignment =
                        HorizontalAlignment::Start;
                    menuButton.layout.verticalAlignment =
                        VerticalAlignment::Start;
                    menuButton.visual = MakeButtonVisual(theme);
                    menuButton.semantics.label = String{"Collection actions"};
                    MenuButton(
                        composer, model.MenuPopup(), "gallery-menu-button",
                        [&] {
                          ComposeText(composer, text, String{"Actions menu"},
                                      14.0F, theme.colors.accentForeground,
                                      "menu-summary");
                        },
                        [&] {
                          NodeProperties menuCard{};
                          menuCard.layout.preferredSize.width = 220.0F;
                          menuCard.layout.padding =
                              Thickness::Uniform(Dp{theme.spacing.compact});
                          menuCard.layout.gap = theme.spacing.compact;
                          menuCard.visual = MakePanelVisual(theme);
                          composer.Element(
                              ElementType::Column, menuCard,
                              [&] {
                                const auto composeMenuItem =
                                    [&](const char *label, const char *message,
                                        const std::string_view key) {
                                      NodeProperties item{};
                                      item.layout.preferredSize =
                                          Size{200.0F, 36.0F};
                                      item.layout.padding =
                                          Thickness{10.0F, 7.0F, 10.0F, 7.0F};
                                      item.visual = MakePanelVisual(theme);
                                      item.visual.states.hovered.background =
                                          theme.colors.raisedSurface;
                                      item.semantics.label = String{label};
                                      MenuItem(
                                          composer,
                                          [&] {
                                            ComposeText(composer, text,
                                                        String{label}, 14.0F,
                                                        theme.colors.foreground,
                                                        "menu-item-label");
                                          },
                                          [&model, message] {
                                            model.Notify(message);
                                            model.MenuPopup().Close();
                                          },
                                          item, key);
                                    };
                                composeMenuItem("Duplicate selection",
                                                "Menu: duplicate requested",
                                                "duplicate");
                                composeMenuItem("Export selection",
                                                "Menu: export requested",
                                                "export");
                              },
                              "actions-menu");
                        },
                        menuButton, {}, "actions-menu-button");
                  },
                  "navigation-controls");

              const std::array<TabDefinition<CollectionTab>, 3> tabs{
                  TabDefinition<CollectionTab>{
                      .value = CollectionTab::Selection,
                      .key = String{"selection"},
                      .label = String{"Selection"},
                  },
                  TabDefinition<CollectionTab>{
                      .value = CollectionTab::Identity,
                      .key = String{"identity"},
                      .label = String{"Changing items"},
                  },
                  TabDefinition<CollectionTab>{
                      .value = CollectionTab::DataSource,
                      .key = String{"data-source"},
                      .label = String{"Loading more"},
                  },
              };
              TabsPresentation tabsPresentation{};
              tabsPresentation.root.layout.gap = theme.spacing.regular;
              tabsPresentation.root.layout.horizontalAlignment =
                  HorizontalAlignment::Start;
              tabsPresentation.tabList.layout.gap = theme.spacing.compact;
              tabsPresentation.tab.layout.preferredSize =
                  Size{170.0F, theme.controls.regularHeight};
              tabsPresentation.tab.layout.padding =
                  Thickness{12.0F, 8.0F, 12.0F, 8.0F};
              tabsPresentation.tab.visual = MakePanelVisual(theme);
              tabsPresentation.tab.visual.states.hovered.background =
                  theme.colors.raisedSurface;
              tabsPresentation.tab.visual.states.selected.background =
                  theme.colors.accent;
              tabsPresentation.tab.visual.states.selected.foreground =
                  theme.colors.accentForeground;
              tabsPresentation.tab.visual.focus = MakeButtonVisual(theme).focus;
              tabsPresentation.panel.layout.preferredSize = Size{650.0F, 92.0F};
              tabsPresentation.panel.layout.padding =
                  Thickness::Uniform(Dp{theme.spacing.spacious});
              tabsPresentation.panel.visual = MakePanelVisual(theme);
              tabsPresentation.panel.visual.base.background =
                  theme.colors.sunkenSurface;
              tabsPresentation.onError = [&model](const UIError &error) {
                model.Report(error);
              };
              Tabs<CollectionTab>(
                  composer, model.CollectionTabBinding(), tabs,
                  [&](Composer &, const auto &definition,
                      const bool selectedTab) {
                    ComposeText(composer, text, definition.label, 14.0F,
                                selectedTab ? theme.colors.accentForeground
                                            : theme.colors.foreground,
                                "tab-label");
                  },
                  [&](Composer &, const auto &definition, const bool) {
                    const auto description =
                        definition.value == CollectionTab::Selection
                            ? "Choose one item or several items."
                        : definition.value == CollectionTab::Identity
                            ? "Items keep their state when you add, remove, "
                              "sort, or filter."
                            : "Large lists can load more items when needed.";
                    ComposeText(composer, text, String{description}, 14.0F,
                                theme.colors.foreground, "tab-description");
                  },
                  tabsPresentation, "collection-tabs");

              NodeProperties contextTarget{};
              contextTarget.layout.preferredSize = Size{650.0F, 58.0F};
              contextTarget.layout.padding =
                  Thickness::Uniform(Dp{theme.spacing.spacious});
              contextTarget.layout.horizontalAlignment =
                  HorizontalAlignment::Start;
              contextTarget.visual = MakePanelVisual(theme);
              contextTarget.visual.base.background = theme.colors.raisedSurface;
              contextTarget.semantics.role = SemanticRole::Group;
              contextTarget.semantics.label = String{"Context-menu target"};
              AttachContextMenu(contextTarget, model.ContextPopup());
              composer.Border(
                  [&] {
                    ComposeText(composer, text,
                                String{"Right-click here for a context menu"},
                                14.0F, theme.colors.foreground,
                                "context-target-label");
                  },
                  contextTarget, "context-target");
              ContextMenu(composer, model.ContextPopup(), [&] {
                NodeProperties menuCard{};
                menuCard.layout.preferredSize.width = 220.0F;
                menuCard.layout.padding =
                    Thickness::Uniform(Dp{theme.spacing.compact});
                menuCard.visual = MakePanelVisual(theme);
                composer.Element(
                    ElementType::Column, menuCard,
                    [&] {
                      NodeProperties item{};
                      item.layout.preferredSize = Size{200.0F, 36.0F};
                      item.layout.padding = Thickness{10.0F, 7.0F, 10.0F, 7.0F};
                      item.visual = MakePanelVisual(theme);
                      item.semantics.label = String{"Inspect item"};
                      MenuItem(
                          composer,
                          [&] {
                            ComposeText(composer, text, String{"Inspect item"},
                                        14.0F, theme.colors.foreground,
                                        "context-item-label");
                          },
                          [&model] {
                            model.Notify("Context menu: inspect requested");
                            model.ContextPopup().Close();
                          },
                          item, "inspect");
                    },
                    "context-menu-items");
              });
            },
            "navigation-content");
      },
      "navigation-card");
}

void ComposeOverlaysPage(Composer &composer, NativeTextSystem &text,
                         Model &model, const Theme &theme) {
  ComposePageHeading(
      composer, text, theme, "Overlays",
      "Open a popup, then close it with Escape or by clicking outside.");
  ComposeButton(
      composer, text, theme,
      model.IsPopupOpen() ? "Popup is open" : "Open popup",
      [&model] { model.SetPopupOpen(true); }, "open-popup", 230.0F);

  if (!model.IsPopupOpen()) {
    return;
  }

  NodeProperties popup{};
  popup.popup.anchor = Rect{420.0F, 240.0F, 1.0F, 1.0F};
  popup.popup.placement = PopupPlacement::Center;
  popup.popup.modal = true;
  popup.popup.dismissOnEscape = true;
  popup.popup.dismissOnOutsidePointer = true;
  popup.popup.onDismiss = [&model] { model.SetPopupOpen(false); };
  popup.semantics.label = String{"Gallery popup"};
  composer.Popup(
      [&] {
        ComposeCard(
            composer, theme,
            [&] {
              NodeProperties column{};
              column.layout.gap = theme.spacing.regular;
              composer.Column(
                  [&] {
                    ComposeText(composer, text, String{"Popup"}, 22.0F,
                                theme.colors.foreground, "popup-title",
                                SemanticRole::Heading);
                    ComposeText(
                        composer, text,
                        String{"Press Escape, click outside, or use Close."},
                        theme.typography.body, theme.colors.mutedForeground,
                        "popup-help");
                    ComposeButton(
                        composer, text, theme, "Close",
                        [&model] { model.SetPopupOpen(false); }, "close-popup",
                        140.0F);
                  },
                  "popup-column");
            },
            "popup-card", 360.0F);
      },
      popup, "demo-popup");
}

void ComposeWindowsPage(Composer &composer, NativeTextSystem &text,
                        Model &model, const Theme &theme) {
  ComposePageHeading(composer, text, theme, "Windows",
                     "Open another window or a dialog.");
  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.regular;
        composer.Column(
            [&] {
              ComposeButton(
                  composer, text, theme, "Open another window",
                  [&model] {
                    auto opened = model.OpenAuxiliaryWindow(false);
                    if (!opened) {
                      model.Report(opened.Error());
                    }
                  },
                  "open-window", 240.0F);
              ComposeButton(
                  composer, text, theme, "Open dialog",
                  [&model] {
                    auto opened = model.OpenAuxiliaryWindow(true);
                    if (!opened) {
                      model.Report(opened.Error());
                    }
                  },
                  "open-dialog", 240.0F);
              ComposeText(composer, text,
                          String{"A dialog pauses the main window until it "
                                 "closes."},
                          theme.typography.body, theme.colors.mutedForeground,
                          "window-help");
            },
            "window-actions");
      },
      "windows-card");
}

void ComposeResourcesPage(Composer &composer, NativeTextSystem &text,
                          Model &model, const Theme &theme) {
  ComposePageHeading(composer, text, theme, "Themes",
                     "Switch between light and dark colors.");
  ComposeButton(
      composer, text, theme,
      model.IsLightTheme() ? "Switch to dark theme" : "Switch to light theme",
      [&model] { model.ToggleTheme(); }, "toggle-theme", 240.0F);

  auto resources = std::make_shared<ResourceScope>();
  resources->Provide(ThemeResource, theme);
  resources->Provide(LocaleResource, std::string{"en-US"});
  resources->Provide(ReducedMotionResource, false);
  composer.Scope(
      resources,
      [&] {
        ComposeCard(
            composer, theme,
            [&] {
              NodeProperties column{};
              column.layout.gap = theme.spacing.regular;
              composer.Column(
                  [&] {
                    ComposeText(composer, text, String{"Current settings"},
                                19.0F, theme.colors.foreground,
                                "resources-title", SemanticRole::Heading);
                    ComposeText(composer, text,
                                String{model.IsLightTheme() ? "Theme: Light"
                                                            : "Theme: Dark"},
                                theme.typography.body,
                                theme.colors.mutedForeground, "theme-resource");
                    ComposeText(
                        composer, text, String{"Language: English (US)"},
                        theme.typography.body, theme.colors.mutedForeground,
                        "locale-resource");
                    ComposeText(composer, text, String{"Reduced motion: Off"},
                                theme.typography.body,
                                theme.colors.mutedForeground,
                                "motion-resource");
                  },
                  "resource-values");
            },
            "resources-card");
      },
      "resource-scope");
}

void ComposeAccessibilityPage(Composer &composer, NativeTextSystem &text,
                              Model &model, const Theme &theme) {
  ComposePageHeading(
      composer, text, theme, "Accessibility",
      "Try common controls with Narrator, the keyboard, or the mouse.");

  const auto diagnostics = model.AccessibilityDiagnostics();
  String provider{diagnostics.available ? "Ready: " : "Not available: "};
  provider.Append(diagnostics.providerName);
  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.compact;
        composer.Column(
            [&] {
              ComposeText(composer, text, String{"Screen reader connection"},
                          18.0F, theme.colors.foreground, "provider-title",
                          SemanticRole::Heading);
              ComposeText(composer, text, std::move(provider),
                          theme.typography.body,
                          diagnostics.available ? theme.colors.foreground
                                                : theme.colors.mutedForeground,
                          "provider-status");
              ComposeText(
                  composer, text,
                  String{
                      diagnostics.available
                          ? "Narrator can read and operate this window."
                          : "A native provider is not enabled on this system."},
                  theme.typography.caption, theme.colors.mutedForeground,
                  "provider-help");
            },
            "provider-values");
      },
      "provider-card");

  const ControlPresentation controls{.theme = theme};
  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.regular;
        composer.Column(
            [&] {
              ComposeText(composer, text, String{"Controls to try"}, 18.0F,
                          theme.colors.foreground, "controls-title",
                          SemanticRole::Heading);
              ComposeButton(
                  composer, text, theme, "Announce a message",
                  [&model] { model.AnnounceAccessibilityDemo(); },
                  "announce-button", 250.0F);
              ComposeControlRow(
                  composer, text, theme, "Enable notifications",
                  "accessibility-check",
                  [&](const NodeProperties &control) {
                    CheckBox(composer, model.CheckBinding(), controls, control,
                             "control");
                  },
                  "accessibility-check-row");
              ComposeControlRow(
                  composer, text, theme, "Automatic updates",
                  "accessibility-switch",
                  [&](const NodeProperties &control) {
                    ToggleSwitch(composer, model.ToggleBinding(), controls,
                                 control, "control");
                  },
                  "accessibility-switch-row");
              ComposeControlRow(
                  composer, text, theme, "Volume", "accessibility-slider",
                  [&](NodeProperties control) {
                    control.layout.preferredSize.width = 300.0F;
                    Slider(composer, model.SliderBinding(),
                           SliderRange{
                               .minimum = 0.0F, .maximum = 1.0F, .step = 0.05F},
                           controls, control, "control");
                  },
                  "accessibility-slider-row");
              ComposeTextField(composer, text, model, theme,
                               model.NameBinding(), "Display name",
                               "accessibility-name");

              auto live = TextProperties(text, theme.typography.body,
                                         theme.colors.foreground);
              live.semantics.role = SemanticRole::Text;
              live.semantics.label = model.AccessibilityAnnouncement();
              live.semantics.live = SemanticLiveSetting::Polite;
              composer.Text(model.AccessibilityAnnouncement(), text, text, live,
                            "accessibility-live-region");
            },
            "accessibility-controls");
      },
      "accessibility-controls-card");
}

void ComposeDiagnosticsPage(Composer &composer, NativeTextSystem &text,
                            Model &model, const Theme &theme) {
  ComposePageHeading(
      composer, text, theme, "Diagnostics",
      "See drawing work, text storage, and visual layout guides.");
  const auto diagnostics = model.Diagnostics();
  const auto textDiagnostics = model.TextDiagnostics();
  const auto fontDiagnostics = model.FontDiagnostics();
  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.compact;
        composer.Column(
            [&] {
              ComposeText(composer, text,
                          LabeledNumber("Frames: ", diagnostics.frameCount),
                          theme.typography.body, theme.colors.foreground,
                          "frames");
              ComposeText(composer, text,
                          LabeledNumber("Screen updates: ",
                                        diagnostics.compositionCount),
                          theme.typography.body, theme.colors.foreground,
                          "compositions");
              ComposeText(composer, text,
                          LabeledNumber("Accessible items: ",
                                        diagnostics.semanticNodeCount),
                          theme.typography.body, theme.colors.foreground,
                          "semantic-count");
              ComposeText(composer, text,
                          LabeledNumber("Drawing steps: ",
                                        diagnostics.displayCommandCount),
                          theme.typography.body, theme.colors.foreground,
                          "display-count");
              ComposeText(
                  composer, text,
                  LabeledNumber("Draw groups: ", diagnostics.drawBatchCount),
                  theme.typography.body, theme.colors.foreground,
                  "batch-count");
              ComposeText(composer, text,
                          LabeledNumber("Grids laid out: ",
                                        diagnostics.layout.grids.size()),
                          theme.typography.body, theme.colors.foreground,
                          "grid-count");
              ComposeText(composer, text,
                          LabeledNumber("Wrapped toolbars: ",
                                        diagnostics.layout.wrapPanels.size()),
                          theme.typography.body, theme.colors.foreground,
                          "wrap-count");
              ComposeText(
                  composer, text,
                  LabeledNumber("Virtualized lists: ",
                                diagnostics.layout.virtualizedLists.size()),
                  theme.typography.body, theme.colors.foreground,
                  "virtual-list-count");
              for (NGIN::UIntSize index = 0;
                   index < diagnostics.layout.grids.size(); ++index) {
                ComposeText(composer, text,
                            TrackLine("Grid ", index,
                                      diagnostics.layout.grids[index].columns),
                            theme.typography.body, theme.colors.mutedForeground,
                            std::string{"grid-tracks-"} +
                                std::to_string(index));
              }
              for (NGIN::UIntSize index = 0;
                   index < diagnostics.layout.wrapPanels.size(); ++index) {
                ComposeText(
                    composer, text,
                    LabeledNumber(
                        "Wrapped lines: ",
                        diagnostics.layout.wrapPanels[index].lines.size()),
                    theme.typography.body, theme.colors.mutedForeground,
                    std::string{"wrap-lines-"} + std::to_string(index));
              }
              for (NGIN::UIntSize index = 0;
                   index < diagnostics.layout.virtualizedLists.size();
                   ++index) {
                const auto &virtualList =
                    diagnostics.layout.virtualizedLists[index];
                String line{"Virtual list rows: "};
                line.Append(Number(virtualList.logicalItemCount));
                line.Append(String{" total · "});
                line.Append(Number(virtualList.realizedNodeCount));
                line.Append(String{" created"});
                ComposeText(composer, text, std::move(line),
                            theme.typography.body, theme.colors.mutedForeground,
                            std::string{"virtual-list-rows-"} +
                                std::to_string(index));
              }
            },
            "diagnostic-values");
      },
      "diagnostics-card");
  ComposeCard(
      composer, theme,
      [&] {
        NodeProperties column{};
        column.layout.gap = theme.spacing.compact;
        composer.Column(
            [&] {
              ComposeText(composer, text,
                          LabeledNumber("Text pages in use: ",
                                        textDiagnostics.pageCount),
                          theme.typography.body, theme.colors.foreground,
                          "text-page-count");
              ComposeText(composer, text,
                          LabeledNumber("Text page limit: ",
                                        textDiagnostics.maximumPageCount),
                          theme.typography.body, theme.colors.foreground,
                          "text-page-limit");
              const auto occupancy =
                  textDiagnostics.capacityPixelArea == 0
                      ? 0
                      : (textDiagnostics.usedPixelArea * 100U) /
                            textDiagnostics.capacityPixelArea;
              ComposeText(composer, text,
                          LabeledNumber("Text storage used (%): ", occupancy),
                          theme.typography.body, theme.colors.foreground,
                          "text-occupancy");
              ComposeText(
                  composer, text,
                  LabeledNumber("Stored glyphs: ", textDiagnostics.entryCount),
                  theme.typography.body, theme.colors.foreground,
                  "text-entry-count");
              ComposeText(composer, text,
                          LabeledNumber("Text sizes stored: ",
                                        textDiagnostics.pixelSizeCount),
                          theme.typography.body, theme.colors.foreground,
                          "text-size-count");
              ComposeText(composer, text,
                          LabeledNumber("Page rebuilds: ",
                                        textDiagnostics.pageRecycleCount),
                          theme.typography.body, theme.colors.foreground,
                          "text-page-rebuilds");
              ComposeText(composer, text,
                          LabeledNumber("Storage waits: ",
                                        textDiagnostics.allocationFailureCount),
                          theme.typography.body, theme.colors.foreground,
                          "text-storage-waits");
              ComposeText(
                  composer, text,
                  LabeledNumber("Glyphs reused: ", textDiagnostics.hitCount),
                  theme.typography.body, theme.colors.foreground,
                  "text-cache-hits");
              ComposeText(composer, text,
                          LabeledNumber("Glyphs uploaded: ",
                                        textDiagnostics.uploadCount),
                          theme.typography.body, theme.colors.foreground,
                          "text-uploads");
              ComposeText(composer, text,
                          LabeledNumber("Glyphs removed: ",
                                        textDiagnostics.evictionCount),
                          theme.typography.body, theme.colors.foreground,
                          "text-evictions");
              ComposeText(composer, text,
                          LabeledNumber("Device restores: ",
                                        textDiagnostics.restorationCount),
                          theme.typography.body, theme.colors.foreground,
                          "text-restores");
              ComposeText(composer, text,
                          LabeledNumber("Fallback characters: ",
                                        fontDiagnostics.fallbackCodePointCount),
                          theme.typography.body, theme.colors.foreground,
                          "fallback-glyphs");
              ComposeText(composer, text,
                          LabeledNumber("Missing characters: ",
                                        fontDiagnostics.missingCodePointCount),
                          theme.typography.body, theme.colors.foreground,
                          "missing-glyphs");
              for (const auto &face : fontDiagnostics.faces) {
                const auto key =
                    std::string{"font-face-"} + std::to_string(face.face.index);
                ComposeText(composer, text, FontFaceLine(face),
                            theme.typography.body, theme.colors.mutedForeground,
                            key);
              }
              for (const auto &size : textDiagnostics.pixelSizes) {
                const auto key = std::string{"text-pixel-size-"} +
                                 std::to_string(size.pixelSize);
                ComposeText(composer, text, GlyphSizeLine(size),
                            theme.typography.body, theme.colors.mutedForeground,
                            key);
              }
            },
            "text-diagnostic-values");
      },
      "text-diagnostics-card");
  ComposeButton(
      composer, text, theme,
      model.IsInspectorEnabled() ? "Hide layout guides" : "Show layout guides",
      [&model] { model.ToggleInspector(); }, "toggle-inspector", 260.0F);
}

void ComposePage(Composer &composer, NativeTextSystem &text, Model &model,
                 const Theme &theme) {
  switch (model.CurrentPage()) {
  case Page::Overview:
    ComposeOverviewPage(composer, text, model, theme);
    break;
  case Page::Layout:
    ComposeLayoutPage(composer, text, theme);
    break;
  case Page::Typography:
    ComposeTypographyPage(composer, text, theme);
    break;
  case Page::TextArea:
    ComposeTextAreaPage(composer, text, model, theme);
    break;
  case Page::Images:
    ComposeImagesPage(composer, text, model, theme);
    break;
  case Page::Inputs:
    ComposeInputsPage(composer, text, model, theme);
    break;
  case Page::Collections:
    ComposeCollectionsPage(composer, text, model, theme);
    break;
  case Page::Overlays:
    ComposeOverlaysPage(composer, text, model, theme);
    break;
  case Page::Windows:
    ComposeWindowsPage(composer, text, model, theme);
    break;
  case Page::Resources:
    ComposeResourcesPage(composer, text, model, theme);
    break;
  case Page::Accessibility:
    ComposeAccessibilityPage(composer, text, model, theme);
    break;
  case Page::Diagnostics:
    ComposeDiagnosticsPage(composer, text, model, theme);
    break;
  }
}

void ComposeAuxiliaryWindow(Composer &composer, NativeTextSystem &text,
                            Model &model, const bool modal) {
  const auto theme = model.CurrentTheme();
  NodeProperties root{};
  root.layout.padding = Thickness::Uniform(Dp{theme.spacing.spacious});
  root.layout.gap = theme.spacing.spacious;
  root.visual.base.background = theme.colors.background;
  root.semantics.role = modal ? SemanticRole::Dialog : SemanticRole::Group;
  root.semantics.label =
      String{modal ? "Gallery dialog" : "Gallery second window"};
  composer.Element(
      ElementType::Column, root,
      [&] {
        ComposeText(composer, text, String{modal ? "Dialog" : "Second window"},
                    26.0F, theme.colors.foreground, "title",
                    SemanticRole::Heading);
        ComposeText(
            composer, text,
            String{modal ? "The main window is paused until this closes."
                         : "This is another NGIN.UI window."},
            theme.typography.body, theme.colors.mutedForeground, "body");
        ComposeText(composer, text,
                    "Use the window close button when you are done.",
                    theme.typography.caption, theme.colors.mutedForeground,
                    "close-hint");
      },
      "auxiliary-root");
}
} // namespace

auto PageAt(const NGIN::UIntSize index) noexcept -> Page {
  return static_cast<Page>(index % PageCount);
}

auto PageName(const Page page) noexcept -> std::string_view {
  return PageNames[static_cast<NGIN::UIntSize>(page)];
}

Model::Model()
    : m_page(
          Page::Overview,
          [this](const InvalidationKind kind) { Invalidate(kind); },
          InvalidationKind::All),
      m_lightTheme(
          false, [this](const InvalidationKind kind) { Invalidate(kind); },
          InvalidationKind::All),
      m_name(String{"NGIN"},
             [this](const InvalidationKind kind) { Invalidate(kind); }),
      m_password(String{"retained"},
                 [this](const InvalidationKind kind) { Invalidate(kind); }),
      m_notes(String{"Write your project notes here.\n\n"
                     "Long lines wrap automatically, and the editor scrolls as "
                     "you add more text.\n"
                     "Add a few lines and try moving with the arrow keys."},
              [this](const InvalidationKind kind) { Invalidate(kind); }),
      m_check(CheckState::Checked,
              [this](const InvalidationKind kind) { Invalidate(kind); }),
      m_mixedCheck(CheckState::Indeterminate,
                   [this](const InvalidationKind kind) { Invalidate(kind); }),
      m_unchecked(CheckState::Unchecked,
                  [this](const InvalidationKind kind) { Invalidate(kind); }),
      m_density(Density::Comfortable,
                [this](const InvalidationKind kind) { Invalidate(kind); }),
      m_toggle(true, [this](const InvalidationKind kind) { Invalidate(kind); }),
      m_disabledToggle(
          false, [this](const InvalidationKind kind) { Invalidate(kind); }),
      m_slider(0.62F,
               [this](const InvalidationKind kind) { Invalidate(kind); }),
      m_activationCount(
          0, [this](const InvalidationKind kind) { Invalidate(kind); }),
      m_collectionItems(
          std::vector<std::uint32_t>{101, 102, 103, 104, 105, 106, 107, 108,
                                     109, 110, 111, 112},
          [this](const InvalidationKind kind) { Invalidate(kind); }),
      m_collectionSelection(
          std::optional<std::uint32_t>{103},
          [this](const InvalidationKind kind) { Invalidate(kind); }),
      m_collectionDescending(
          false, [this](const InvalidationKind kind) { Invalidate(kind); }),
      m_collectionFiltered(
          false, [this](const InvalidationKind kind) { Invalidate(kind); }),
      m_collectionTab(
          CollectionTab::Selection,
          [this](const InvalidationKind kind) { Invalidate(kind); }),
      m_virtualizedSelection(
          String{"item-0"},
          [this](const InvalidationKind kind) { Invalidate(kind); }),
      m_virtualizedSource(std::make_unique<GalleryVirtualizedSource>()),
      m_virtualizedController(std::make_unique<FixedVirtualizedListController>(
          FixedVirtualizationOptions{
              .itemExtent = 38.0F,
              .itemGap = 2.0F,
              .overscanItems = 3,
              .initialViewportExtent = 280.0F,
          },
          [this](const InvalidationKind kind) { Invalidate(kind); })),
      m_comboPopup([this](const InvalidationKind kind) { Invalidate(kind); }),
      m_menuPopup([this](const InvalidationKind kind) { Invalidate(kind); }),
      m_contextPopup([this](const InvalidationKind kind) { Invalidate(kind); }),
      m_popupOpen(
          false, [this](const InvalidationKind kind) { Invalidate(kind); },
          InvalidationKind::All),
      m_inspectorEnabled(
          false, [this](const InvalidationKind kind) { Invalidate(kind); },
          InvalidationKind::All),
      m_status(String{},
               [this](const InvalidationKind kind) { Invalidate(kind); }),
      m_accessibilityAnnouncement(
          String{"No announcement yet."},
          [this](const InvalidationKind kind) { Invalidate(kind); }) {}

Model::~Model() = default;

void Model::AttachRuntime(Application &application, NativeTextSystem &text,
                          Window &window) noexcept {
  m_application = &application;
  m_text = &text;
  m_window = &window;
  m_imageCache = std::make_unique<ImageTextureCache>(application.Renderer());
  std::error_code pathError;
  auto imagePath = std::filesystem::path{"images"} / "gallery-sample.png";
  if (!std::filesystem::is_regular_file(imagePath, pathError) || pathError) {
    imagePath = std::filesystem::path{__FILE__}.parent_path().parent_path() /
                "assets" / "images" / "gallery-sample.png";
  }
  m_galleryImage = ImageResource::DecodeFileAsync(
      ImageFileSource{.path = String{imagePath.string()}});
  m_galleryImage->Wait();
  if (m_galleryImage->State() == ImageLoadState::Failed) {
    Report(m_galleryImage->Error());
  }
  m_helpToolTip = std::make_unique<ToolTipController>(
      window, String{"Appears after 500 ms without moving keyboard focus."});
}

auto Model::CurrentPage() const noexcept -> Page { return m_page.Get(); }

void Model::SelectPage(const Page page) {
  SetPopupOpen(false);
  static_cast<void>(m_page.Set(page));
}

auto Model::CurrentTheme() const -> Theme {
  return IsLightTheme() ? MakeLightTheme() : Theme{};
}

auto Model::IsLightTheme() const noexcept -> bool { return m_lightTheme.Get(); }

void Model::ToggleTheme() {
  static_cast<void>(m_lightTheme.Set(!m_lightTheme.Get()));
}

auto Model::Name() const noexcept -> const String & { return m_name.Get(); }

auto Model::NameBinding() -> Binding<String> { return Bind(m_name); }

auto Model::PasswordBinding() -> Binding<String> { return Bind(m_password); }

auto Model::NotesBinding() -> Binding<String> { return Bind(m_notes); }

auto Model::GalleryImage() const noexcept
    -> const std::shared_ptr<ImageResource> & {
  return m_galleryImage;
}

auto Model::ImageCache() noexcept -> ImageTextureCache * {
  return m_imageCache.get();
}

auto Model::CheckBinding() -> Binding<CheckState> { return Bind(m_check); }

auto Model::MixedCheckBinding() -> Binding<CheckState> {
  return Bind(m_mixedCheck);
}

auto Model::UncheckedBinding() -> Binding<CheckState> {
  return Bind(m_unchecked);
}

auto Model::DensityBinding() -> Binding<Density> { return Bind(m_density); }

auto Model::ToggleBinding() -> Binding<bool> { return Bind(m_toggle); }

auto Model::DisabledToggleBinding() -> Binding<bool> {
  return Bind(m_disabledToggle);
}

auto Model::SliderBinding() -> Binding<F32> { return Bind(m_slider); }

auto Model::SliderValue() const noexcept -> F32 { return m_slider.Get(); }

auto Model::HelpToolTip() noexcept -> ToolTipController * {
  return m_helpToolTip.get();
}

auto Model::ActivationCount() const noexcept -> std::uint32_t {
  return m_activationCount.Get();
}

void Model::Activate() {
  static_cast<void>(m_activationCount.Set(m_activationCount.Get() + 1));
}

auto Model::CollectionItems() const -> std::vector<std::uint32_t> {
  auto items = m_collectionItems.Get();
  if (m_collectionFiltered.Get()) {
    std::erase_if(items, [](const auto item) { return item % 2 == 0; });
  }
  std::sort(items.begin(), items.end());
  if (m_collectionDescending.Get()) {
    std::reverse(items.begin(), items.end());
  }
  return items;
}

auto Model::CollectionSelection(const std::uint32_t item) -> ItemSelection {
  return BindListItem(m_collectionSelection, item);
}

auto Model::SelectedCollectionItem() const noexcept
    -> std::optional<std::uint32_t> {
  return m_collectionSelection.Value();
}

void Model::AddCollectionItem() {
  const auto item = m_nextCollectionItem++;
  static_cast<void>(
      m_collectionItems.Update([item](auto &items) { items.push_back(item); }));
  static_cast<void>(m_collectionSelection.Select(item));
  Notify("Added and selected an item");
}

void Model::RemoveSelectedCollectionItem() {
  const auto selected = m_collectionSelection.Value();
  if (!selected) {
    Notify("Select an item before removing it");
    return;
  }
  static_cast<void>(m_collectionItems.Update(
      [selected](auto &items) { std::erase(items, *selected); }));
  static_cast<void>(m_collectionSelection.Clear());
  Notify("Removed the selected item");
}

void Model::ToggleCollectionSort() {
  static_cast<void>(m_collectionDescending.Set(!m_collectionDescending.Get()));
}

void Model::ToggleCollectionFilter() {
  static_cast<void>(m_collectionFiltered.Set(!m_collectionFiltered.Get()));
}

auto Model::IsCollectionDescending() const noexcept -> bool {
  return m_collectionDescending.Get();
}

auto Model::IsCollectionFiltered() const noexcept -> bool {
  return m_collectionFiltered.Get();
}

auto Model::CollectionTabBinding() -> Binding<CollectionTab> {
  return Bind(m_collectionTab);
}

auto Model::VirtualizedCollectionSource() noexcept
    -> IVirtualizedDataSource<NGIN::UIntSize> & {
  return *m_virtualizedSource;
}

auto Model::VirtualizedCollectionController() noexcept
    -> FixedVirtualizedListController & {
  return *m_virtualizedController;
}

auto Model::SelectedVirtualizedIndex() const noexcept
    -> std::optional<NGIN::UIntSize> {
  return m_virtualizedSource->IndexOfKey(m_virtualizedSelection.Get());
}

auto Model::SelectVirtualizedItem(const NGIN::UIntSize index)
    -> UIResult<void> {
  auto key = m_virtualizedSource->KeyAt(index);
  if (!key) {
    return std::move(key).Error();
  }
  static_cast<void>(m_virtualizedSelection.Set(std::move(key).Value()));
  return {};
}

void Model::PrependVirtualizedItems() {
  m_virtualizedSource->Prepend(250);
  Invalidate(InvalidationKind::All);
  Notify("Added 250 rows while keeping the visible item in place");
}

auto Model::ComboPopup() noexcept -> PopupController & { return m_comboPopup; }

auto Model::MenuPopup() noexcept -> PopupController & { return m_menuPopup; }

auto Model::ContextPopup() noexcept -> PopupController & {
  return m_contextPopup;
}

void Model::Notify(const char *message) {
  static_cast<void>(m_status.Set(String{message}));
}

auto Model::IsPopupOpen() const noexcept -> bool { return m_popupOpen.Get(); }

void Model::SetPopupOpen(const bool open) {
  static_cast<void>(m_popupOpen.Set(open));
}

auto Model::IsInspectorEnabled() const noexcept -> bool {
  return m_inspectorEnabled.Get();
}

void Model::ToggleInspector() {
  const auto enabled = !m_inspectorEnabled.Get();
  static_cast<void>(m_inspectorEnabled.Set(enabled));
  if (m_window != nullptr) {
    m_window->SetInspectorOverlay(InspectorOverlayOptions{
        .enabled = enabled,
        .showLayoutBounds = true,
        .showHitTestBounds = false,
        .showFocus = true,
    });
  }
}

auto Model::OpenAuxiliaryWindow(const bool modal) noexcept -> UIResult<void> {
  if (m_application == nullptr || m_text == nullptr || m_window == nullptr) {
    return MakeUIError(UIErrorCode::InvalidState,
                       "Gallery runtime is not attached", "NGIN.UI.Gallery",
                       "OpenAuxiliaryWindow");
  }

  ++m_auxiliaryWindowId;
  String id{modal ? "Gallery.Dialog." : "Gallery.Window."};
  id.Append(Number(m_auxiliaryWindowId));
  String title{modal ? "NGIN.UI Modal Dialog" : "NGIN.UI Auxiliary Window"};
  Window *created = nullptr;
  if (modal) {
    auto result = m_application->CreateDialogWindow(
        *m_window, WindowCreateInfo{
                       .id = id,
                       .title = title,
                       .initialSize = PixelSize{520, 300},
                       .minimumSize = PixelSize{420, 240},
                   });
    if (!result) {
      return std::move(result).Error();
    }
    created = result.Value();
  } else {
    auto result = m_application->CreateWindow(WindowCreateInfo{
        .id = id,
        .title = title,
        .initialSize = PixelSize{560, 340},
        .minimumSize = PixelSize{420, 240},
    });
    if (!result) {
      return std::move(result).Error();
    }
    created = result.Value();
  }

  created->SetContent([this, modal](Composer &composer) {
    ComposeAuxiliaryWindow(composer, *m_text, *this, modal);
  });
  static_cast<void>(m_status.Set(
      String{modal ? "Opened modal dialog" : "Opened auxiliary window"}));
  return {};
}

auto Model::Status() const noexcept -> const String & { return m_status.Get(); }

auto Model::Diagnostics() const noexcept -> WindowDiagnostics {
  return m_window != nullptr ? m_window->Diagnostics() : WindowDiagnostics{};
}

auto Model::TextDiagnostics() const noexcept -> GlyphAtlasDiagnostics {
  return m_text != nullptr ? m_text->AtlasDiagnostics()
                           : GlyphAtlasDiagnostics{};
}

auto Model::FontDiagnostics() const noexcept -> FontCoverageDiagnostics {
  return m_text != nullptr ? m_text->CoverageDiagnostics()
                           : FontCoverageDiagnostics{};
}

auto Model::AccessibilityDiagnostics() const noexcept
    -> NGIN::UI::AccessibilityDiagnostics {
  return m_application != nullptr ? m_application->AccessibilityDiagnostics()
                                  : NGIN::UI::AccessibilityDiagnostics{};
}

auto Model::AccessibilityAnnouncement() const noexcept -> const String & {
  return m_accessibilityAnnouncement.Get();
}

void Model::AnnounceAccessibilityDemo() {
  const auto next = m_activationCount.Get() + 1;
  static_cast<void>(m_activationCount.Set(next));
  String message{"Accessibility message "};
  message.Append(Number(next));
  message.Append(String{" is ready."});
  static_cast<void>(m_accessibilityAnnouncement.Set(std::move(message)));
}

void Model::Report(UIError error) {
  static_cast<void>(m_status.Set(std::move(error.message)));
}

void Model::Invalidate(const InvalidationKind kind) const noexcept {
  if (m_window != nullptr) {
    m_window->Invalidate(kind);
  }
}

void ComposeMainView(Composer &composer, NativeTextSystem &text, Model &model) {
  const auto theme = model.CurrentTheme();
  NodeProperties root{};
  root.layout.padding = Thickness::Uniform(Dp{24.0F});
  root.grid.columns = {GridTrack::Fixed(210.0F),
                       GridTrack::Weighted(1.0F, 280.0F)};
  root.grid.rows = {GridTrack::Weighted()};
  root.grid.columnGap = 22.0F;
  root.visual.base.background = theme.colors.background;
  root.semantics.role = SemanticRole::Group;
  root.semantics.label = String{"NGIN.UI control gallery"};

  composer.Element(
      ElementType::Grid, root,
      [&] {
        NodeProperties sidebar{};
        sidebar.layout.preferredSize.width = 210.0F;
        sidebar.layout.minimumSize.width = 210.0F;
        sidebar.layout.maximumSize.width = 210.0F;
        sidebar.layout.flexShrink = 0.0F;
        sidebar.gridPlacement = GridPlacement{.row = 0, .column = 0};
        sidebar.layout.padding = Thickness::Uniform(Dp{14.0F});
        sidebar.layout.gap = theme.spacing.regular;
        sidebar.layout.verticalAlignment = VerticalAlignment::Stretch;
        sidebar.visual = MakePanelVisual(theme);
        sidebar.semantics.role = SemanticRole::Group;
        sidebar.semantics.label = String{"Gallery navigation"};
        composer.Element(
            ElementType::Column, sidebar,
            [&] {
              ComposeText(composer, text, String{"NGIN.UI"}, 25.0F,
                          theme.colors.focus, "brand", SemanticRole::Heading);
              ComposeText(composer, text, String{"Control gallery"}, 13.0F,
                          theme.colors.mutedForeground, "brand-subtitle");
              NodeProperties separator{};
              separator.visual = MakeSeparatorVisual(theme);
              composer.Separator(SeparatorOrientation::Horizontal, separator,
                                 "nav-separator");
              for (NGIN::UIntSize index = 0; index < PageCount; ++index) {
                const auto page = PageAt(index);
                const auto name = PageName(page);
                ComposeButton(
                    composer, text, theme, name.data(),
                    [&model, page] { model.SelectPage(page); },
                    std::to_string(index), 180.0F, true,
                    model.CurrentPage() == page);
              }
              ComposeButton(
                  composer, text, theme,
                  model.IsLightTheme() ? "Use dark theme" : "Use light theme",
                  [&model] { model.ToggleTheme(); }, "theme", 180.0F);
            },
            "sidebar");

        NodeProperties viewport{};
        viewport.layout.preferredSize = Size{780.0F, 680.0F};
        viewport.layout.minimumSize = Size{280.0F, 320.0F};
        viewport.layout.flexGrow = 1.0F;
        viewport.layout.flexShrink = 1.0F;
        viewport.layout.horizontalAlignment = HorizontalAlignment::Stretch;
        viewport.layout.verticalAlignment = VerticalAlignment::Stretch;
        viewport.gridPlacement = GridPlacement{.row = 0, .column = 1};
        viewport.scroll.vertical = true;
        viewport.scroll.horizontal = false;
        StyleScrollView(viewport, theme);
        composer.ScrollView(
            [&] {
              NodeProperties page{};
              page.layout.padding = Thickness{4.0F, 4.0F, 18.0F, 28.0F};
              page.layout.gap = theme.spacing.spacious;
              page.layout.horizontalAlignment = HorizontalAlignment::Stretch;
              page.layout.verticalAlignment = VerticalAlignment::Start;
              page.semantics.role = SemanticRole::Group;
              page.semantics.label =
                  String{PageName(model.CurrentPage()).data()};
              composer.Element(
                  ElementType::Column, page,
                  [&] {
                    ComposePage(composer, text, model, theme);
                    if (!model.Status().Empty()) {
                      ComposeText(composer, text, model.Status(), 13.0F,
                                  theme.colors.mutedForeground,
                                  "gallery-status");
                    }
                  },
                  "page");
            },
            viewport, "catalogue-viewport");
      },
      "gallery-root");
}

auto CreateMainWindow(Application &application, NativeTextSystem &text,
                      Model &model) -> UIResult<Window *> {
  auto window = application.CreateWindow(WindowCreateInfo{
      .id = String{"Gallery.Main"},
      .title = String{"NGIN.UI Control Gallery"},
      .initialSize = PixelSize{1180, 780},
      .minimumSize = PixelSize{640, 480},
  });
  if (!window) {
    return std::move(window).Error();
  }
  model.AttachRuntime(application, text, *window.Value());
  window.Value()->SetContent([&text, &model](Composer &composer) {
    ComposeMainView(composer, text, model);
  });
  return window.Value();
}
} // namespace NGIN::UIGallery
