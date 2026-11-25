#include "GallerySamplePicker.hpp"

#include <array>
#include <utility>

namespace NGIN::UIGallery::Pages {
using namespace NGIN::UI;
using NGIN::Text::String;

namespace {
enum class LayoutStudy { Tracks, Flow, Canvas };

// The artboard uses the same public layout primitives as an application. Each
// study changes their actual inputs so layout diagnostics match the preview.
auto TrackStudy(const Theme &theme) -> Views::View {
  return Views::Component([theme](Views::ComponentScope &scope) {
    auto columns = scope.Local("columns", 3);
    NodeProperties grid{};
    grid.grid.columns.assign(columns.Get(), GridTrack::Weighted());
    grid.grid.rows.assign(6 / columns.Get(), GridTrack::Fixed(66));
    grid.grid.columnGap = grid.grid.rowGap = 6;
    constexpr std::array labels{"North", "East",  "South",
                                "West",  "Coast", "Inland"};
    std::vector<Views::View> tiles;
    for (UIntSize i = 0; i < labels.size(); ++i) {
      NodeProperties cell{};
      cell.gridPlacement = {.row = i / static_cast<UIntSize>(columns.Get()),
                            .column = i % static_cast<UIntSize>(columns.Get())};
      cell.layout.padding = {12, 10, 12, 10};
      cell.visual.base.background =
          i == 0 ? theme.colors.accent : theme.colors.sunkenSurface;
      const auto ink =
          i == 0 ? theme.colors.accentForeground : theme.colors.foreground;
      auto label = TextProperties(16, ink);
      label.layout.verticalAlignment = VerticalAlignment::End;
      tiles.push_back(
          Views::Border({Views::Label(labels[i], label)}, cell).Key(labels[i]));
    }
    return SampleCard(
        theme,
        {TextView("Grid tracks", 22, theme.colors.foreground, "track-title",
                  SemanticRole::Heading),
         SamplePicker(theme, columns, "Number of columns",
                      {{2, "Two columns", "tracks-two"},
                       {3, "Three columns", "tracks-three"}}),
         Views::Grid(std::move(tiles), grid).Key("track-artboard")},
        "tracks", "tracks-study");
  });
}

auto WrapStudy(const Theme &theme) -> Views::View {
  return Views::Component([theme](Views::ComponentScope &scope) {
    auto narrow = scope.Local("narrow", false);
    NodeProperties flow{};
    flow.layout.preferredSize.width = flow.layout.maximumSize.width =
        narrow.Get() ? 215 : 600;
    flow.layout.horizontalAlignment = HorizontalAlignment::Start;
    flow.layout.padding = Thickness::Uniform(Dp{10});
    flow.visual.base.background = theme.colors.sunkenSurface;
    flow.wrapPanel.itemGap = flow.wrapPanel.lineGap = 7;
    std::vector<Views::View> words;
    for (const auto *word :
         {"Bass", "Lead guitar", "Drums", "Strings", "Synthesizer"}) {
      NodeProperties item{};
      item.layout.padding = {13, 10, 13, 10};
      item.visual.base.background = theme.colors.raisedSurface;
      words.push_back(
          Views::Border({TextView(word, 16, theme.colors.foreground, "word")},
                        item)
              .Key(word));
    }
    return SampleCard(
        theme,
        {TextView("Wrapping labels", 22, theme.colors.foreground, "flow-title",
                  SemanticRole::Heading),
         SamplePicker(theme, narrow, "Available width",
                      {{false, "Wide frame", "flow-wide"},
                       {true, "Narrow frame", "flow-narrow"}}),
         Views::WrapPanel(std::move(words), flow).Key("flow-artboard")},
        "flow", "flow-study");
  });
}

auto CanvasStudy(const Theme &theme) -> Views::View {
  return Views::Component([theme](Views::ComponentScope &scope) {
    auto offset = scope.Local("offset", 32);
    NodeProperties canvas{};
    canvas.layout.preferredSize = {310, 102};
    canvas.layout.maximumSize.width = 310;
    canvas.visual.base.background = theme.colors.sunkenSurface;
    const auto marker = [&](int x, bool active) {
      NodeProperties dot{};
      dot.canvasPlacement.offset = {static_cast<F32>(x), 32};
      dot.canvasPlacement.contributesToDesiredSize = false;
      dot.layout.preferredSize = {38, 38};
      dot.visual.base.background =
          active ? theme.colors.accent : theme.colors.surface;
      dot.visual.base.borderColor =
          active ? theme.colors.accent : theme.colors.border;
      dot.visual.base.borderThickness = Thickness::Uniform(Dp{1});
      dot.visual.base.cornerRadius = CornerRadius::Uniform(Dp{19});
      return Views::Border({}, dot).Key(active ? "coordinate-marker"
                                               : std::to_string(x));
    };
    return SampleCard(
        theme,
        {TextView("Canvas coordinates", 22, theme.colors.foreground,
                  "canvas-title", SemanticRole::Heading),
         SamplePicker(theme, offset, "Marker horizontal offset",
                      {{32, "32 px", "offset-left"},
                       {128, "128 px", "offset-middle"},
                       {224, "224 px", "offset-right"}}),
         Views::Canvas({marker(32, false), marker(128, false),
                        marker(224, false), marker(offset.Get(), true)},
                       canvas)
             .Key("canvas-artboard")},
        "canvas", "canvas-study");
  });
}
} // namespace

auto LayoutView(const Theme &theme) -> Views::View {
  return Views::Component([theme](Views::ComponentScope &scope) {
    auto study = scope.Local("layout", LayoutStudy::Tracks);
    Views::View artboard;
    switch (study.Get()) {
    case LayoutStudy::Tracks:
      artboard = TrackStudy(theme);
      break;
    case LayoutStudy::Flow:
      artboard = WrapStudy(theme);
      break;
    case LayoutStudy::Canvas:
      artboard = CanvasStudy(theme);
      break;
    }
    return PageContentView(
        {PageHeadingView(
             theme, "Responsive layout",
             "Change the space, tracks or coordinates of a layout."),
         SamplePicker(theme, study, "Layout primitive",
                      {{LayoutStudy::Tracks, "Grid", "layout-grid"},
                       {LayoutStudy::Flow, "Wrap", "layout-wrap"},
                       {LayoutStudy::Canvas, "Canvas", "layout-canvas"}}),
         std::move(artboard)});
  });
}

auto TypographyView(const Theme &theme) -> Views::View {
  return Views::Component([theme](Views::ComponentScope &scope) {
    auto size = scope.Local("size", 30);
    auto line =
        TextProperties(static_cast<F32>(size.Get()), theme.colors.foreground);
    line.text.lineHeight = static_cast<F32>(size.Get()) * 1.35F;
    line.layout.horizontalAlignment = HorizontalAlignment::Stretch;
    NodeProperties paper{};
    paper.layout.padding = {20, 16, 20, 16};
    paper.visual.base.background = theme.colors.sunkenSurface;
    return PageContentView(
        {PageHeadingView(
             theme, "Typography",
             "Set a line of type and compare the shapes across scripts."),
         SampleCard(theme,
                    {SamplePicker(theme, size, "Preview type size",
                                  {{16, "Body 16", "type-body"},
                                   {30, "Title 30", "type-title"},
                                   {44, "Display 44", "type-display"}}),
                     Views::Border(
                         {Views::Label(
                              "Pack my box with five dozen liquor jugs.", line)
                              .Key("type-proof")},
                         paper)},
                    "scale", "type-scale"),
         SampleCard(theme,
                    {TextView("Language proofs", 22, theme.colors.foreground,
                              "script-title", SemanticRole::Heading),
                     TextView("Latin   ·   naïve café", 18,
                              theme.colors.foreground, "latin"),
                     TextView("Greek   ·   Ελληνικά", 18,
                              theme.colors.foreground, "greek"),
                     TextView("Cyrillic   ·   Кириллица", 18,
                              theme.colors.foreground, "cyrillic"),
                     TextView("Arabic   ·   العربية", 18,
                              theme.colors.foreground, "arabic"),
                     TextView("Mixed direction   ·   Hello, مرحبا", 18,
                              theme.colors.foreground, "bidi"),
                     TextView("Combining marks   ·   e\xCC\x81  A\xCC\x88", 18,
                              theme.colors.foreground, "graphemes"),
                     TextView("Symbols   ·   ✓  ★", 18, theme.colors.foreground,
                              "symbols")},
                    "scripts", "language-proofs")});
  });
}

auto TextAreaView(GalleryViewModel &model, const Theme &theme) -> Views::View {
  NodeProperties area{};
  area.layout.preferredSize.height = 230;
  area.layout.horizontalAlignment = HorizontalAlignment::Stretch;
  area.layout.padding = {14, 12, 14, 12};
  area.visual = MakeTextFieldVisual(theme);
  area.text.fontSize = 16;
  area.text.lineHeight = 23;
  area.text.wrapping = TextWrapping::Wrap;
  area.text.color = theme.colors.foreground;
  area.textField.selectionColor = theme.colors.selection;
  area.textField.caretColor = area.textField.compositionColor =
      theme.colors.focus;
  area.textField.onError = [&model](const UIError &error) {
    model.Report(error);
  };
  StyleScrollView(area, theme);
  return PageContentView(
      {PageHeadingView(theme, "Text editor",
                       "Write a few lines. Use Shift and the arrow keys to "
                       "extend a selection."),
       SampleCard(theme,
                  {TextView("Project notes", 21, theme.colors.foreground,
                            "text-area-title", SemanticRole::Heading),
                   Views::TextArea("Project notes", model.NotesBinding(), area)
                       .Key("project-notes")},
                  "text-area-column", "text-area-card")});
}

auto ImagesView(GalleryViewModel &model, const Theme &theme) -> Views::View {
  return Views::Component([&model, theme](Views::ComponentScope &scope) {
    auto mode = scope.Local("fit", ImageFit::Contain);
    NodeProperties frame{};
    frame.layout.preferredSize.height = frame.layout.maximumSize.height = 208;
    frame.visual.base.background = theme.colors.sunkenSurface;
    NodeProperties image{};
    image.image.fit = mode.Get();
    image.image.alignment = {.5F, .5F};
    std::vector<Views::View> artwork;
    if (model.ImageCache() && model.GalleryImage()) {
      artwork.push_back(
          Views::Image(model.GalleryImage(), *model.ImageCache(),
                       "Blue and violet scene with a glowing coral cube", image)
              .Key("image-proof"));
    }
    const auto *caption =
        "The entire image stays in view. Open space preserves its proportions.";
    if (mode.Get() == ImageFit::Cover)
      caption =
          "The image fills the frame. Edges outside the frame are cropped.";
    if (mode.Get() == ImageFit::Fill)
      caption = "The image stretches to the width and height of the frame.";
    return PageContentView(
        {PageHeadingView(
             theme, "Image fitting",
             "Compare three ways to fit the same artwork inside a frame."),
         SampleCard(
             theme,
             {SamplePicker(theme, mode, "Image fit",
                           {{ImageFit::Contain, "Contain", "image-contain"},
                            {ImageFit::Cover, "Cover", "image-cover"},
                            {ImageFit::Fill, "Stretch", "image-fill"}}),
              Views::Border(std::move(artwork), frame).Key("image-frame"),
              TextView(caption, 15, theme.colors.mutedForeground,
                       "fit-caption")},
             "image-column", "image-card")});
  });
}
} // namespace NGIN::UIGallery::Pages
