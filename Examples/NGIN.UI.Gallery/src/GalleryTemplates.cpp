// A field-recording browser with interchangeable item, layout and detail
// templates. Selection and saved clips belong to the browser's data model.
#include "GallerySamplePicker.hpp"

#include <algorithm>
#include <array>

namespace NGIN::UIGallery::Pages {
namespace {
using namespace UI;
using Text::String;

enum class ClipId { Breakwater, NightTram, Glasshouse };
enum class Presentation { Cards, Rows };

struct Recording {
  ClipId id;
  const char *key;
  const char *name;
  const char *subject;
  const char *duration;
  const char *format;
  Color paper;
  Color ink;
  // Illustrative peak envelopes for the three sample recordings.
  std::array<int, 32> peaks;
};

constexpr std::array Recordings{
    Recording{ClipId::Breakwater,
              "breakwater",
              "Breakwater",
              "Sea wall",
              "1:42",
              "Stereo · 48 kHz · WAV",
              {.83F, .91F, .85F, 1},
              {.13F, .34F, .29F, 1},
              {2,  4,  6,  7,  12, 19, 25, 20, 17, 15, 12, 9, 7, 4, 5, 8,
               11, 18, 22, 26, 21, 19, 15, 13, 9,  6,  7,  9, 8, 6, 4, 2}},
    Recording{ClipId::NightTram,
              "night-tram",
              "Night tram",
              "Street crossing",
              "0:36",
              "Stereo · 96 kHz · WAV",
              {.98F, .84F, .71F, 1},
              {.52F, .22F, .13F, 1},
              {2,  3,  4,  3,  5,  9,  20, 11, 17, 26, 10, 15, 23, 13, 20, 29,
               11, 16, 23, 12, 16, 22, 9,  12, 17, 8,  9,  11, 6,  5,  4,  2}},
    Recording{ClipId::Glasshouse,
              "glasshouse",
              "Glasshouse",
              "Rain on glass",
              "2:08",
              "Mono · 48 kHz · FLAC",
              {.85F, .85F, .97F, 1},
              {.28F, .24F, .53F, 1},
              {3,  5, 14, 4, 6,  23, 4,  3, 16, 7,  4, 11, 4,  22, 7, 3,
               15, 5, 20, 6, 10, 3,  18, 7, 4,  19, 6, 3,  12, 5,  8, 3}},
};

struct ArchiveState {
  ClipId selected = ClipId::Breakwater;
  std::array<bool, Recordings.size()> saved{};
  bool savedOnly = false;

  auto IsSaved(ClipId id) const -> bool {
    return saved[static_cast<UIntSize>(id)];
  }

  // If filtering removes the selected clip, pick the first remaining one.
  // With no matches the view displays its empty state.
  void Reconcile() {
    if (savedOnly && !IsSaved(selected)) {
      for (const auto &clip : Recordings) {
        if (IsSaved(clip.id)) {
          selected = clip.id;
          return;
        }
      }
    }
  }
  bool operator==(const ArchiveState &) const = default;
};

class Waveform final : public ICustomElement {
public:
  Waveform(const Recording &clip, bool compact)
      : m_clip(clip), m_compact(compact) {}
  auto Measure(CustomElementContext &, SizeConstraints space)
      -> UIResult<Size> override {
    return space.Constrain(m_compact ? Size{110, 48} : Size{170, 76});
  }
  auto Paint(CustomElementContext &, PaintContext &paint)
      -> UIResult<void> override {
    const auto [width, height] = paint.Extent();
    paint.FillRounded(paint.Bounds(), CornerRadius::Uniform(Dp{4}),
                      m_clip.paper);
    const auto step = (width - 20) / m_clip.peaks.size();
    if (step <= 0)
      return {};
    Color guide = m_clip.ink;
    guide.alpha = .28F;
    paint.Fill({8, height / 2, width - 16, 1}, guide);
    for (UIntSize i = 0; i < m_clip.peaks.size(); ++i) {
      const F32 peak = height * .41F * m_clip.peaks[i] / 30;
      paint.Fill({10 + i * step, height / 2 - peak, step * .64F, 2 * peak},
                 m_clip.ink);
    }
    return {};
  }

private:
  const Recording &m_clip;
  bool m_compact;
};

auto Title(const char *value, F32 size, const Theme &theme) -> Views::View {
  auto text = TextProperties(size, theme.colors.foreground);
  text.text.font.weight = 600;
  return Views::Label(value, text);
}

auto Card(const Recording &clip, const Theme &theme, bool compact)
    -> Views::View {
  NodeProperties art{};
  art.semantics.hidden = true;
  art.interaction.hitTestVisible = false;
  String subtitle;
  if (!compact) {
    subtitle.Append(clip.subject);
    subtitle.Append(" · ");
  }
  subtitle.Append(clip.duration);
  NodeProperties column{};
  column.layout.gap = 6;
  return Views::Column(
      {Views::Custom(std::make_shared<Waveform>(clip, compact), art),
       Title(clip.name, compact ? 16 : 18, theme),
       TextView(subtitle, 13, theme.colors.mutedForeground, "subtitle")},
      column);
}

auto FileRow(const Recording &clip, const Theme &theme) -> Views::View {
  NodeProperties marker{};
  marker.layout.preferredSize = {5, 30};
  marker.layout.verticalAlignment = VerticalAlignment::Center;
  marker.visual.base.background = clip.ink;
  NodeProperties title{};
  title.layout.flexGrow = 1;
  NodeProperties row{};
  row.layout.gap = 12;
  row.layout.verticalAlignment = VerticalAlignment::Center;
  auto time = TextProperties(15, theme.colors.foreground);
  time.layout.verticalAlignment = VerticalAlignment::Center;
  return Views::Row(
      {Views::Border({}, marker),
       Views::Column({Title(clip.name, 16, theme),
                      TextView(clip.subject, 13, theme.colors.mutedForeground,
                               "subject")},
                     title),
       Views::Label(clip.duration, time)},
      row);
}

auto Details(const Recording &clip, Binding<ArchiveState> archive,
             const Theme &theme) -> Views::View {
  const bool saved = archive.Get().IsSaved(clip.id);
  String metadata{clip.format};
  metadata.Append(" · ");
  metadata.Append(clip.duration);
  NodeProperties block{};
  block.layout.gap = 4;
  auto summary = Views::Column(
      {Title(clip.name, 23, theme),
       TextView(metadata, 14, theme.colors.mutedForeground, "clip-format")},
      block);
  auto save = ButtonView(
      theme, saved ? "Remove from saved" : "Save clip",
      [archive, id = clip.id] {
        auto next = archive.Get();
        next.saved[static_cast<UIntSize>(id)] = !next.IsSaved(id);
        next.Reconcile();
        static_cast<void>(archive.Set(next));
      },
      "archive-save", saved ? 168 : 100, ButtonAppearance::Neutral);
  NodeProperties inspector{};
  inspector.layout.padding = {14, 12, 14, 12};
  inspector.wrapPanel.itemGap = 24;
  inspector.wrapPanel.lineGap = 8;
  inspector.visual.base.background = theme.colors.surface;
  inspector.visual.base.borderColor = theme.colors.border;
  inspector.visual.base.borderThickness = {0, 1, 0, 1};
  inspector.semantics.role = SemanticRole::Group;
  inspector.semantics.label = "Selected recording";
  return Views::WrapPanel({std::move(summary), std::move(save)}, inspector)
      .Key("clip-inspector");
}

// The slots render content; the control supplies stable keys, keyboard
// selection and accessible list-item roles for both presentations.
template <typename ItemTemplate, typename LayoutTemplate,
          typename DetailTemplate>
auto ClipBrowser(const std::vector<const Recording *> &clips,
                 Binding<ClipId> selection, NodeProperties item,
                 ItemTemplate renderItem, LayoutTemplate arrange,
                 DetailTemplate renderDetail, const Theme &theme)
    -> Views::View {
  if (clips.empty())
    return TextView(
        "No saved recordings. Choose All clips to browse the archive.", 15,
        theme.colors.mutedForeground, "empty-archive");

  std::vector<Views::View> items;
  const Recording *selected = clips.front();
  for (const Recording *clip : clips) {
    if (selection.Get() == clip->id)
      selected = clip;
    item.semantics.label = clip->name;
    item.semantics.description = clip->subject;
    items.push_back(Views::SelectableListItem(BindListItem(selection, clip->id),
                                              renderItem(*clip), item)
                        .Key(clip->key));
  }
  NodeProperties list{};
  list.semantics.role = SemanticRole::List;
  list.semantics.label = "Field recordings";
  list.scroll.vertical = false;
  list.scroll.showScrollbars = false;
  NodeProperties body{};
  body.layout.gap = 12;
  return Views::Column(
      {Views::Box(ElementType::ListView, {arrange(std::move(items))}, list)
           .Key("clip-browser"),
       renderDetail(*selected)},
      body);
}

} // namespace

auto TemplatesView(GalleryViewModel &model, const Theme &theme) -> Views::View {
  return Views::Component([&model, theme](Views::ComponentScope &scope) {
    const bool compact = model.IsCompact();
    const auto archive = scope.Local("archive", ArchiveState{});
    const auto mode = scope.Local("presentation", Presentation::Cards);
    const auto &state = archive.Get();
    Binding<ClipId> selection{
        [archive]() -> const ClipId & { return archive.Get().selected; },
        [archive](ClipId id) {
          auto next = archive.Get();
          next.selected = id;
          return archive.Set(next);
        }};

    std::vector<const Recording *> clips;
    for (const auto &clip : Recordings)
      if (!state.savedOnly || state.IsSaved(clip.id))
        clips.push_back(&clip);

    const auto saved = std::count(state.saved.begin(), state.saved.end(), true);
    const std::string filterLabel =
        state.savedOnly ? "All clips"
                        : "Saved clips (" + std::to_string(saved) + ")";
    auto filters = ButtonView(
        theme, filterLabel.c_str(),
        [archive] {
          auto next = archive.Get();
          next.savedOnly = !next.savedOnly;
          next.Reconcile();
          static_cast<void>(archive.Set(next));
        },
        "archive-filter", 145, ButtonAppearance::Neutral);
    auto picker =
        SamplePicker(theme, mode, "Item template",
                     {{Presentation::Cards, "Waveform cards", "archive-cards"},
                      {Presentation::Rows, "File rows", "archive-rows"}});
    NodeProperties item{};
    item.layout.padding = {10, 9, 10, 9};
    item.visual = MakePanelVisual(theme);
    item.visual.base.background = theme.colors.raisedSurface;
    item.visual.states.selected.background = theme.colors.selection;
    item.visual.states.selected.borderColor = theme.colors.focus;
    item.visual.states.hovered.borderColor = theme.colors.focus;
    item.visual.focus = MakeButtonVisual(theme).focus;

    auto detail = [archive, theme](const Recording &clip) {
      return Details(clip, archive, theme);
    };
    Views::View browser;
    if (mode.Get() == Presentation::Cards) {
      item.layout.preferredSize.width = compact ? 136 : 210;
      browser = ClipBrowser(
          clips, selection, item,
          [&](const auto &clip) { return Card(clip, theme, compact); },
          [](auto items) {
            NodeProperties wrap{};
            wrap.wrapPanel.itemGap = wrap.wrapPanel.lineGap = 10;
            return Views::WrapPanel(std::move(items), wrap)
                .Key("archive-card-grid");
          },
          detail, theme);
    } else {
      browser = ClipBrowser(
          clips, selection, item,
          [&](const auto &clip) { return FileRow(clip, theme); },
          [](auto items) {
            NodeProperties fileList{};
            fileList.layout.gap = 4;
            return Views::Column(std::move(items), fileList)
                .Key("archive-file-list");
          },
          detail, theme);
    }
    auto heading =
        compact ? TextView("Control templates", 30, theme.colors.foreground,
                           "page-title", SemanticRole::Heading)
                : PageHeadingView(
                      theme, "Control templates",
                      "Browse field recordings as waveform cards or file rows. "
                      "Select a clip to inspect or save it.");
    NodeProperties page{};
    page.layout.gap = compact ? 12 : 20;
    return Views::Column({std::move(heading),
                          ActionRow({std::move(picker), std::move(filters)},
                                    "archive-toolbar"),
                          std::move(browser)},
                         page)
        .Key("gallery-page-content");
  });
}
} // namespace NGIN::UIGallery::Pages
