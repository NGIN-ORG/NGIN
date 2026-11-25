// The sample index uses the same view, layout and paint APIs as its
// destinations.
#include "GalleryViews.hpp"
#include <NGIN/UIGallery/CustomControls.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace NGIN::UIGallery::Pages {
namespace {
using namespace NGIN::UI;
using NGIN::Text::String;

constexpr auto Ink(unsigned rgb) -> Color {
  return {static_cast<F32>((rgb >> 16) & 255) / 255,
          static_cast<F32>((rgb >> 8) & 255) / 255,
          static_cast<F32>(rgb & 255) / 255, 1};
}

// Work from the space offered by the host. The same index can be embedded in
// a narrow tool panel without having to know the native window's dimensions.
class SpecimenGrid final : public ILayoutPolicy {
public:
  SpecimenGrid(F32 minimumTrack, F32 gap)
      : m_minimumTrack(minimumTrack), m_gap(gap) {}

  auto Measure(MeasureChildren &children, SizeConstraints available)
      -> Size override {
    const auto width =
        std::isfinite(available.maximum.width)
            ? available.maximum.width
            : children.Count() * (m_minimumTrack + m_gap) - m_gap;
    const auto columns = Columns(width, children.Count());
    const auto track = (width - (columns - 1) * m_gap) / columns;
    F32 height = 0;
    for (UIntSize first = 0; first < children.Count(); first += columns) {
      F32 rowHeight = 0;
      for (auto i = first; i < std::min(first + columns, children.Count());
           ++i) {
        const auto child =
            children.Measure(i, {.minimum = {track, 0},
                                 .maximum = {track, available.maximum.height}});
        rowHeight = std::max(rowHeight, child.height);
      }
      height += rowHeight + (first ? m_gap : 0);
    }
    return available.Constrain({width, height});
  }
  void Arrange(ArrangeChildren &children, Size available) override {
    const auto columns = Columns(available.width, children.Count());
    const auto track = (available.width - (columns - 1) * m_gap) / columns;
    F32 y = 0;
    for (UIntSize first = 0; first < children.Count(); first += columns) {
      const auto end = std::min(first + columns, children.Count());
      F32 height = 0;
      for (auto i = first; i < end; ++i)
        height = std::max(height, children.DesiredSize(i).height);
      for (auto i = first; i < end; ++i)
        children.Place(i, {static_cast<F32>(i - first) * (track + m_gap), y,
                           track, height});
      y += height + m_gap;
    }
  }

private:
  auto Columns(F32 width, UIntSize count) const -> UIntSize {
    auto fit =
        static_cast<UIntSize>((width + m_gap) / (m_minimumTrack + m_gap));
    return std::max<UIntSize>(1, std::min(count, fit));
  }
  F32 m_minimumTrack;
  F32 m_gap;
};

void Disc(PaintContext &paint, F32 x, F32 y, F32 radius, Color ink) {
  paint.FillRounded({x - radius, y - radius, 2 * radius, 2 * radius},
                    CornerRadius::Uniform(Dp{radius}), ink);
}

// A sampled bounce: longer gaps indicate higher speed. The guide and tick
// marks use logical pixels so the illustration remains crisp at any scale.
void MotionPrint(PaintContext &paint) {
  const auto [width, height] = paint.Extent();
  const auto left = width * 0.08F;
  const auto run = width * 0.83F;
  const auto baseline = height * 0.80F;
  const auto line = Ink(0x788bf0);
  paint.Fill({left, baseline, run, 1}, line);
  for (int i = 0; i <= 32; ++i) {
    const F32 h = i % 4 == 0 ? 7 : 3;
    paint.Fill({left + run * i / 32, baseline + 1, 1, h}, line);
  }
  const auto radius = std::min(width * 0.065F, 19.0F);
  for (int i = 0; i <= 16; ++i) {
    const F32 t = static_cast<F32>(i) / 16;
    const auto x = left + radius + t * (run - 2 * radius);
    const auto y = baseline - radius - 4 * t * (1 - t) * (height * 0.52F);
    if (i == 11) {
      Disc(paint, x, y, radius, Ink(0xff8257));
      Disc(paint, x, y, 2.5F, Ink(0x482f44));
    } else if (i == 0 || i == 16 || i == 4 || i == 8) {
      paint.StrokeRounded({x - radius, y - radius, 2 * radius, 2 * radius},
                          CornerRadius::Uniform(Dp{radius}), 1.3F,
                          Ink(0xa6b5ff));
    } else {
      Disc(paint, x, y, 2.1F, Ink(0xc9d4ff));
    }
  }
}

void TrackPrint(PaintContext &paint) {
  const auto [w, h] = paint.Extent();
  const F32 x = w * 0.13F, y = h * 0.24F, width = w * 0.74F, height = h * 0.52F;
  const F32 split = width * 0.34F, gap = 4;
  paint.Fill({x, y, split, height}, Ink(0xd65b3c));
  paint.Fill({x + split + gap, y, width - split - gap, height * .38F},
             Ink(0x955136));
  paint.Fill({x + split + gap, y + height * .38F + gap, width - split - gap,
              height * .62F - gap},
             Ink(0xfffbeb));
  paint.Fill({x - 6, y - 7, width + 12, 1}, Ink(0xb68870));
  for (auto px : {x, x + split, x + width})
    paint.Fill({px, y - 10, 1, 7}, Ink(0x85543e));
}

void ControlPrint(PaintContext &paint) {
  const auto [w, h] = paint.Extent();
  const auto ink = Ink(0x264197);
  const F32 x = w * 0.14F, span = w * 0.68F,
            radius = std::min(h * 0.073F, 7.0F);
  constexpr std::array<F32, 3> Values{0.27F, 0.78F, 0.48F};
  for (std::size_t i = 0; i < Values.size(); ++i) {
    const auto y = h * (0.28F + 0.22F * i);
    paint.Fill({x, y, span, 2}, Ink(0xa7b5dc));
    paint.Fill({x, y, span * Values[i], 2}, ink);
    Disc(paint, x + span * Values[i], y + 1, radius, ink);
    Disc(paint, x + span * Values[i], y + 1, radius - 2, Ink(0xfcfcf8));
  }
}

void ListPrint(PaintContext &paint) {
  const auto [w, h] = paint.Extent();
  const auto x = w * 0.14F, span = w * 0.72F, step = h * 0.14F;
  constexpr std::array<F32, 5> Lengths{0.69F, 0.45F, 0.82F, 0.53F, 0.67F};
  for (std::size_t i = 0; i < Lengths.size(); ++i) {
    const auto y = h * .18F + i * step;
    const auto selected = i == 2;
    if (selected)
      paint.Fill({x - 7, y - 3, span + 14, step}, Ink(0x336850));
    auto ink = selected ? Ink(0xf3ffe8) : Ink(0x658b75);
    paint.Fill({x, y, 6, 6}, ink);
    paint.Fill({x + 12, y + 1, (span - 24) * Lengths[i], 3}, ink);
  }
}

// The bars echo the waveform previews used by the clip browser's tile template.
void WaveformPrint(PaintContext &paint) {
  const auto [width, height] = paint.Extent();
  constexpr std::array<F32, 30> peaks{
      .08F, .16F, .34F, .56F, .27F, .47F, .76F, .91F, .40F, .57F,
      .35F, .21F, .12F, .26F, .43F, .30F, .50F, .69F, .81F, .59F,
      .39F, .48F, .63F, .35F, .26F, .43F, .28F, .18F, .12F, .06F};
  const auto step = width * .80F / peaks.size();
  for (UIntSize i = 0; i < peaks.size(); ++i) {
    const auto bar = height * .64F * peaks[i];
    paint.FillRounded({width * .10F + step * i, (height - bar) / 2,
                       step * .64F, bar}, CornerRadius::Uniform(Dp{1.5F}),
                      Ink(0x70435D));
  }
}

struct Specimen {
  Page page;
  const char *title;
  const char *description;
  const char *key;
  Color paper;
  void (*draw)(PaintContext &);
};
constexpr Specimen Motion{Page::Motion,
                          "Motion studio",
                          "Play a transition and compare timing curves.",
                          "sample-motion",
                          Ink(0x2846be),
                          MotionPrint};
constexpr std::array Samples{
    Specimen{Page::Templates, "Control templates", "One library, two views",
             "sample-templates", Ink(0xF1DDE3), WaveformPrint},
    Specimen{Page::Layout, "Responsive layout", "Sizing, grids and wrapping",
             "sample-layout", Ink(0xf2d7c5), TrackPrint},
    Specimen{Page::Inputs, "Form controls", "Inputs and validation",
             "sample-inputs", Ink(0xdce4fc), ControlPrint},
    Specimen{Page::Collections, "Collections", "Selection and virtual lists",
             "sample-collections", Ink(0xddeada), ListPrint},
};

class SpecimenArt final : public ICustomElement {
public:
  explicit SpecimenArt(Specimen sample) : m_sample(sample) {}
  auto Measure(CustomElementContext &, SizeConstraints constraints)
      -> UIResult<Size> override {
    return constraints.Constrain(
        {260, m_sample.page == Page::Motion ? 180.0F : 102.0F});
  }
  auto Paint(CustomElementContext &, PaintContext &paint)
      -> UIResult<void> override {
    if (m_sample.page != Page::Motion)
      paint.Fill(paint.Bounds(), m_sample.paper);
    m_sample.draw(paint);
    return {};
  }

private:
  Specimen m_sample;
};

auto Art(const Specimen &sample) -> Views::View {
  NodeProperties props{};
  props.interaction.hitTestVisible = false;
  props.semantics.hidden = true;
  return Views::Custom(std::make_shared<SpecimenArt>(sample), props);
}

auto Label(const char *text, F32 size, Color color, UInt16 weight = 400)
    -> Views::View {
  auto props = TextProperties(size, color);
  props.text.font.weight = weight;
  props.semantics.hidden = true;
  return Views::Label(String{text}, props);
}

auto SampleLink(GalleryViewModel &model, const Theme &theme,
                const Specimen &sample) -> Views::View {
  const bool featured = sample.page == Page::Motion;
  NodeProperties button{};
  button.semantics.label = String{sample.title};
  button.semantics.description = String{sample.description};
  button.visual.focus = MakeButtonVisual(theme).focus;
  button.visual.base.background =
      featured ? sample.paper : theme.colors.background;
  button.visual.states.hovered.background =
      featured ? Ink(0x2038a5) : theme.colors.raisedSurface;
  button.visual.states.pressed.background =
      featured ? Ink(0x182b87) : theme.colors.sunkenSurface;
  NodeProperties caption{};
  caption.layout.gap = featured ? 8 : 3;
  caption.layout.padding =
      featured ? Thickness{24, 20, 4, 20} : Thickness{0, 9, 0, 7};
  caption.layout.verticalAlignment = VerticalAlignment::Center;

  Views::View content;
  if (featured) {
    auto action = TextProperties(14, Ink(0x192d79));
    action.text.font.weight = 650;
    action.layout.padding = {13, 6, 13, 6};
    action.visual.base.background = Ink(0xf2f4ff);
    action.visual.base.cornerRadius = CornerRadius::Uniform(Dp{3});
    action.semantics.hidden = true;
    auto summary = Views::Column(
        {
            Label(sample.title, 32, Ink(0xffffff), 650),
            Label(sample.description, 15, Ink(0xe4eaff)),
            Views::Label(String{"Open studio"}, action),
        },
        caption);
    content = Views::Layout(std::make_shared<SpecimenGrid>(180, 0),
                            {std::move(summary), Art(sample)});
  } else {
    auto summary = Views::Column(
        {Label(sample.title, 19, theme.colors.foreground, 650),
         Label(sample.description, 14, theme.colors.mutedForeground)},
        caption);
    content = Views::Column({Art(sample), std::move(summary)});
  }
  // The full specimen is one reachable action. Its artwork and caption do not
  // add duplicate stops to the keyboard or accessibility trees.
  return Views::Button([&model, page = sample.page] { model.SelectPage(page); },
                       std::move(content), button)
      .Key(sample.key);
}

auto Section(const char *title, const Theme &theme, Views::View content)
    -> Views::View {
  NodeProperties group{};
  group.layout.gap = 12;
  auto heading = TextProperties(21, theme.colors.foreground);
  heading.text.font.weight = 650;
  heading.semantics.role = SemanticRole::Heading;
  return Views::Column(
      {Views::Label(String{title}, heading), std::move(content)}, group);
}
} // namespace

auto OverviewView(GalleryViewModel &model, const Theme &theme)
    -> UI::Views::View {
  std::vector<Views::View> links;
  for (const auto &sample : Samples)
    links.push_back(SampleLink(model, theme, sample));
  return PageContentView({
      PageHeadingView(
          theme, "Sample library",
          "Working examples for desktop apps, tools and game interfaces."),
      SampleLink(model, theme, Motion),
      Section("Explore samples", theme,
              Views::Layout(std::make_shared<SpecimenGrid>(184, 16),
                            std::move(links))),
      Section("Custom controls", theme, CustomControlExamples(theme)),
  });
}
} // namespace NGIN::UIGallery::Pages
