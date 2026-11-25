#include "GallerySamplePicker.hpp"

namespace NGIN::UIGallery::Pages {
using namespace UI;
using Text::String;

namespace {
enum class DiagnosticsSample { Frame, Navigation, Text, Images };

struct Reading {
  const char *label;
  String value;
};

// A common value column makes counts easy to compare. Labels can wrap when
// the inspector shares a small window with the sample browser.
auto Readings(const Theme &theme, std::vector<Reading> values) -> Views::View {
  std::vector<Views::View> rows;
  for (auto &entry : values) {
    NodeProperties line{};
    line.grid.columns = {GridTrack::Weighted(), GridTrack::Auto()};
    line.layout.preferredSize.width = 460;
    line.layout.padding = Thickness{0, 5, 0, 5};
    auto value = TextProperties(16, theme.colors.foreground);
    value.gridPlacement.column = 1;
    rows.push_back(
        Views::Grid(
            {TextView(entry.label, 15, theme.colors.mutedForeground, "label"),
             Views::Label(std::move(entry.value), value).Key("value")},
            line)
            .Key(entry.label));
  }
  return Views::Column(std::move(rows)).Key("readings");
}

auto Snapshot(GalleryViewModel &model, const Theme &theme,
              DiagnosticsSample sample) -> Views::View {
  const char *heading{};
  const char *key{};
  std::vector<Reading> readings;
  switch (sample) {
  case DiagnosticsSample::Frame: {
    const auto frame = model.Diagnostics();
    heading = "Frame counters";
    key = "diagnostics-card";
    readings = {
        {"Frames rendered", Number(frame.frameCount)},
        {"Compositions", Number(frame.compositionCount)},
        {"Drawing commands", Number(frame.displayCommandCount)},
        {"Draw batches", Number(frame.drawBatchCount)},
        {"Accessible elements", Number(frame.semanticNodeCount)},
        {"Grids laid out", Number(frame.layout.grids.size())},
        {"Wrap panels", Number(frame.layout.wrapPanels.size())},
        {"Virtualized lists", Number(frame.layout.virtualizedLists.size())}};
    break;
  }
  case DiagnosticsSample::Navigation: {
    const auto nav = model.PageNavigationDiagnostics();
    heading = "Page lifetime";
    key = "navigation-diagnostics-card";
    if (!nav.stack.empty())
      readings.push_back(
          {"Current page", String{nav.stack.back().displayName}});
    readings.insert(readings.end(),
                    {{"Pages on stack", Number(nav.stack.size())},
                     {"Cached pages", Number(nav.cache.size())},
                     {"Active tasks", Number(model.ActivePageTaskCount())},
                     {"Cleanup tasks", Number(model.CleanupPageTaskCount())},
                     {"Activations", Number(model.PageActivationCount())},
                     {"Releases", Number(model.PageReleaseCount())},
                     {"Failures", Number(model.NavigationFailureCount())}});
    break;
  }
  case DiagnosticsSample::Text: {
    const auto atlas = model.TextDiagnostics();
    const auto fonts = model.FontDiagnostics();
    heading = "Text atlas";
    key = "text-diagnostics-card";
    readings = {{"Atlas pages", Number(atlas.pageCount)},
                {"Page limit", Number(atlas.maximumPageCount)},
                {"Cached glyphs", Number(atlas.entryCount)},
                {"Glyph cache hits", Number(atlas.hitCount)},
                {"Glyph uploads", Number(atlas.uploadCount)},
                {"Glyph evictions", Number(atlas.evictionCount)},
                {"Fallback characters", Number(fonts.fallbackCodePointCount)},
                {"Missing characters", Number(fonts.missingCodePointCount)}};
    break;
  }
  case DiagnosticsSample::Images: {
    const auto cache = model.ImageDiagnostics();
    heading = "Image cache";
    key = "image-cache-card";
    readings = {{"Resident images", Number(cache.entryCount)},
                {"Entry limit", Number(cache.maximumEntryCount)},
                {"Resident bytes", Number(cache.residentBytes)},
                {"Cache hits", Number(cache.hitCount)},
                {"Uploads", Number(cache.uploadCount)},
                {"Evictions", Number(cache.evictionCount)},
                {"Capacity failures", Number(cache.capacityFailureCount)}};
    break;
  }
  }
  return SampleCard(theme,
                    {TextView(heading, 22, theme.colors.foreground,
                              "metric-title", SemanticRole::Heading),
                     Readings(theme, std::move(readings))},
                    "metric-values", key);
}
} // namespace

auto DiagnosticsView(GalleryViewModel &model, const Theme &theme)
    -> Views::View {
  return Views::Component([&model, theme](Views::ComponentScope &scope) {
    auto choice = scope.Local("sample", DiagnosticsSample::Frame);
    // Reading diagnostics continuously would change the frame counts being
    // observed.
    auto revision = scope.Local("snapshot", UInt64{});
    static_cast<void>(revision.Get());
    std::vector<Views::View> actions{ButtonView(
        theme, "Refresh snapshot",
        [revision] { static_cast<void>(revision.Set(revision.Get() + 1)); },
        "refresh-snapshot", 162)};
    if (choice.Get() == DiagnosticsSample::Frame)
      actions.push_back(ButtonView(
          theme,
          model.IsInspectorEnabled() ? "Hide layout guides"
                                     : "Show layout guides",
          [&model] { model.ToggleInspector(); }, "toggle-inspector", 172, ButtonAppearance::Neutral));
    return PageContentView(
        {PageHeadingView(theme, "Diagnostics",
                         "Inspect a snapshot of the running gallery."),
         SamplePicker(
             theme, choice, "Diagnostic views",
             {{DiagnosticsSample::Frame, "Frame", "diagnostics-frame"},
              {DiagnosticsSample::Navigation, "Navigation",
               "diagnostics-navigation"},
              {DiagnosticsSample::Text, "Text", "diagnostics-text"},
              {DiagnosticsSample::Images, "Images", "diagnostics-images"}}),
         ActionRow(std::move(actions), "snapshot-actions"),
         Snapshot(model, theme, choice.Get())});
  });
}
} // namespace NGIN::UIGallery::Pages
