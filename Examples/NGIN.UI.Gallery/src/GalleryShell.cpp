#include "GalleryViews.hpp"

#include <array>
#include <utility>

namespace NGIN::UIGallery {
namespace {
using namespace UI;
using namespace Pages;
using Text::String;

// Keep the navigation order independent from the page tags and their
// command-line names.
struct Section {
  const char *name;
  std::span<const Page> pages;
};
constexpr std::array Library{Page::Overview};
constexpr std::array Foundations{Page::Layout, Page::Typography, Page::Images,
                                 Page::TextArea};
constexpr std::array Interaction{Page::Motion, Page::Inputs, Page::Collections,
                                 Page::AsyncData, Page::Overlays};
constexpr std::array ApplicationPages{Page::PropertyEditor, Page::Resources,
                                      Page::Windows, Page::Accessibility,
                                      Page::Diagnostics};
constexpr std::array Sections{Section{"", Library},
                              Section{"Foundations", Foundations},
                              Section{"Interaction", Interaction},
                              Section{"Application", ApplicationPages}};

constexpr auto Ink(UInt32 rgb) -> Color {
  return {static_cast<F32>((rgb >> 16) & 255) / 255,
          static_cast<F32>((rgb >> 8) & 255) / 255,
          static_cast<F32>(rgb & 255) / 255, 1};
}

auto RouteLabel(Page page) -> std::string_view {
  if (page == Page::Overview)
    return "Sample library";
  return PageName(page);
}

auto Brand(const Theme &theme) -> Views::View {
  // The three columns echo the gallery's layout specimens at a scale that
  // remains legible.
  auto bar = [&](F32 height) {
    NodeProperties p{};
    p.layout.preferredSize = {4, height};
    p.layout.verticalAlignment = VerticalAlignment::Center;
    p.visual.base.background = theme.colors.accentForeground;
    return Views::Box(ElementType::Rectangle, {}, p);
  };
  NodeProperties mark{};
  mark.layout.preferredSize = {30, 30};
  mark.layout.padding = Thickness{6, 0, 6, 0};
  mark.layout.gap = 3;
  mark.visual.base.background = theme.colors.accent;
  mark.visual.base.cornerRadius = CornerRadius::Uniform(Dp{6});
  mark.semantics.hidden = true;
  NodeProperties row{};
  row.layout.gap = 10;
  row.layout.verticalAlignment = VerticalAlignment::Center;
  auto type = TextProperties(21, theme.colors.foreground);
  type.layout.verticalAlignment = VerticalAlignment::Center;
  return Views::Row({Views::Row({bar(9), bar(18), bar(13)}, mark),
                     Views::Label("NGIN.UI", type)},
                    row)
      .Key("brand-block");
}

auto QuietButton(const Theme &theme, String label,
                 Utilities::Callable<void()> action, std::string_view key,
                 bool selected = false) -> Views::View {
  NodeProperties p{};
  p.layout.minimumSize.height = 32;
  p.layout.padding = {10, 5, 10, 5};
  p.semantics.label = label;
  p.visual = MakeButtonVisual(theme);
  p.visual.base.background =
      selected ? theme.colors.selection : Color{0, 0, 0, 0};
  p.visual.base.borderThickness = {};
  p.visual.states.hovered.background = theme.colors.sunkenSurface;
  p.visual.states.pressed.background = theme.colors.selection;
  if (selected)
    p.semantics.states |= SemanticStateFlags::Selected;
  auto type = TextProperties(14, selected ? theme.colors.focus
                                          : theme.colors.foreground);
  type.layout.verticalAlignment = VerticalAlignment::Center;
  return Views::Button(std::move(action), Views::Label(label, type), p)
      .Key(key);
}

// A scroll viewport needs a finite allocation even when its children want more
// room. Both policies measure the viewport at the same size they will
// ultimately give it.
class BrowserLayout final : public ILayoutPolicy {
public:
  auto Measure(MeasureChildren &children, SizeConstraints box)
      -> Size override {
    const auto top = children.Measure(0, {.maximum = box.maximum});
    const auto bottom = children.Measure(2, {.maximum = box.maximum});
    const Size list{
        box.maximum.width,
        std::max(0.F, box.maximum.height - top.height - bottom.height)};
    static_cast<void>(children.Measure(1, {.minimum = list, .maximum = list}));
    return box.maximum;
  }
  void Arrange(ArrangeChildren &children, Size size) override {
    const auto top = children.DesiredSize(0).height;
    const auto bottom = children.DesiredSize(2).height;
    children.Place(0, {0, 0, size.width, top});
    children.Place(
        1, {0, top, size.width, std::max(0.F, size.height - top - bottom)});
    children.Place(2, {0, size.height - bottom, size.width, bottom});
  }
};

class WorkspaceLayout final : public ILayoutPolicy {
public:
  explicit WorkspaceLayout(bool compact) : m_compact(compact) {}
  auto Measure(MeasureChildren &children, SizeConstraints box)
      -> Size override {
    for (UIntSize i = 0; i < children.Count(); ++i) {
      const auto rect = Slot(i, box.maximum);
      const Size size{rect.width, rect.height};
      static_cast<void>(
          children.Measure(i, {.minimum = size, .maximum = size}));
    }
    return box.maximum;
  }
  void Arrange(ArrangeChildren &children, Size size) override {
    for (UIntSize i = 0; i < children.Count(); ++i)
      children.Place(i, Slot(i, size));
  }

private:
  auto Slot(UIntSize index, Size size) const -> Rect {
    if (m_compact) {
      const F32 header = std::min(52.F, size.height);
      if (index == 2)
        return {0, 0, size.width, header};
      return {0, header, size.width, size.height - header};
    }
    const F32 rail = std::min(228.F, size.width);
    if (index == 0)
      return {0, 0, rail, size.height};
    return {rail, 0, size.width - rail, size.height};
  }
  bool m_compact;
};

auto Navigation(GalleryViewModel &model) -> Views::View {
  return Views::Component([&model] {
    const auto theme = model.CurrentTheme();
    NodeProperties header{};
    header.layout.padding = {18, 22, 18, 12};
    header.layout.gap = 18;
    auto query = model.NavigationSearchBinding();
    std::vector<Views::View> searchContent{
        TextFieldView(model, theme, query, "Find a sample", "navigation-search",
                      false, true, false, false, 188)};
    if (query.Get().Empty()) {
      auto hint = TextProperties(14, theme.colors.mutedForeground);
      hint.layout.padding = {12, 0, 0, 0};
      hint.layout.verticalAlignment = VerticalAlignment::Center;
      hint.interaction.hitTestVisible = false;
      hint.semantics.hidden = true;
      searchContent.push_back(Views::Label("Find a sample", hint));
    }
    auto search = Views::Overlay(std::move(searchContent)).Key("search-box");
    std::vector<Views::View> groups;
    for (const Section &section : Sections) {
      std::vector<Views::View> links;
      for (auto page : section.pages) {
        if (model.NavigationMatches(page)) {
          links.push_back(QuietButton(
              theme, String{RouteLabel(page)},
              [&model, page] { model.SelectPage(page); },
              std::to_string(static_cast<UIntSize>(page)),
              model.CurrentPage() == page));
        }
      }
      if (links.empty())
        continue;
      if (*section.name) {
        auto cap = TextProperties(12, theme.colors.mutedForeground);
        cap.text.font.weight = 650;
        cap.layout.padding = {10, 0, 0, 4};
        links.insert(links.begin(), Views::Label(String{section.name}, cap));
      }
      groups.push_back(Views::Column(std::move(links)).Key(section.name));
    }
    if (groups.empty()) {
      groups.push_back(TextView("No matching samples", 14,
                                theme.colors.foreground,
                                "navigation-no-results"));
      groups.push_back(QuietButton(
          theme, "Clear search",
          [&model] {
            static_cast<void>(model.NavigationSearchBinding().Set({}));
          },
          "clear-search"));
    }
    NodeProperties list{};
    list.layout.gap = 16;
    NodeProperties scroll{};
    scroll.layout.padding = {12, 4, 12, 10};
    scroll.scroll.vertical = true;
    StyleScrollView(scroll, theme);
    NodeProperties footer{};
    footer.layout.padding = {12, 8, 12, 10};
    footer.visual.base.borderThickness = {0, 1, 0, 0};
    footer.visual.base.borderColor = theme.colors.border;

    NodeProperties rail{};
    rail.visual.base.background = theme.colors.surface;
    rail.visual.base.borderColor = theme.colors.border;
    rail.visual.base.borderThickness = {0, 0, 1, 0};
    rail.semantics.role = SemanticRole::Group;
    rail.semantics.label = "Browse samples";
    std::vector<Views::View> top;
    if (!model.IsCompact())
      top.push_back(Brand(theme));
    top.push_back(std::move(search));
    return Views::Layout(
        std::make_shared<BrowserLayout>(),
        {Views::Column(std::move(top), header),
         Views::ScrollView({Views::Column(std::move(groups), list)}, scroll)
             .Key("sidebar"),
         Views::Column(
             {QuietButton(
                 theme,
                 model.IsLightTheme() ? "Use dark theme" : "Use light theme",
                 [&model] { model.ToggleTheme(); }, "rail-theme")},
             footer)},
        rail);
  });
}

auto CompactHeader(GalleryViewModel &model, const Theme &theme) -> Views::View {
  NodeProperties p{};
  p.layout.padding = {14, 6, 14, 6};
  p.visual.base.background = theme.colors.surface;
  p.visual.base.borderThickness = {0, 0, 0, 1};
  p.visual.base.borderColor = theme.colors.border;
  NodeProperties spacer{};
  spacer.layout.flexGrow = 1;
  return Views::Row(
      {Brand(theme), Views::Box(ElementType::Spacer, {}, spacer),
       QuietButton(
           theme, model.BrowserOpen() ? "Close browser" : "Browse samples",
           [&model] { model.SetBrowserOpen(!model.BrowserOpen()); },
           "browse-samples")},
      p);
}

auto Catalogue(GalleryViewModel &model) -> Views::View {
  auto status = Views::Component([&model] {
    if (model.Status().Empty())
      return Views::View{};
    return TextView(model.Status(), 13,
                    model.CurrentTheme().colors.mutedForeground,
                    "gallery-status");
  });
  return Views::Component([&model, status] {
    const auto theme = model.CurrentTheme();
    const auto page = model.CurrentPage();
    const bool compact = model.IsCompact();
    NodeProperties sheet{};
    sheet.layout.padding =
        compact ? Thickness{18, 15, 18, 40} : Thickness{40, 29, 40, 58};
    sheet.layout.gap = 12;
    sheet.layout.maximumSize.width = 1120;
    sheet.layout.horizontalAlignment = HorizontalAlignment::Center;
    sheet.semantics.role = SemanticRole::Group;
    sheet.semantics.label = String{RouteLabel(page)};
    std::vector<Views::View> body;
    if (page != Page::Overview && !compact) {
      auto back = QuietButton(
          theme, "Sample library",
          [&model] { model.SelectPage(Page::Overview); }, "back-library");
      NodeProperties line{};
      line.layout.horizontalAlignment = HorizontalAlignment::Start;
      body.push_back(Views::Row({std::move(back)}, line));
    }
    body.push_back(model.CurrentPageView());
    body.push_back(status);
    NodeProperties viewport{};
    viewport.semantics.identifier = "catalogue-viewport";
    viewport.scroll.vertical = true;
    StyleScrollView(viewport, theme);
    // Give each route its own scroll position. Opening a sample always shows
    // its controls first.
    return Views::ScrollView(
               {Views::Column(std::move(body), sheet).Key("page")}, viewport)
        .Key("sample-" + std::to_string(static_cast<UIntSize>(page)));
  });
}
} // namespace

auto GalleryViewModel::CurrentTheme() const -> UI::Theme {
  const bool light = IsLightTheme();
  auto theme = light ? UI::MakeLightTheme() : UI::Theme{};
  auto &c = theme.colors;
  c.background = Ink(light ? 0xFAF9F6 : 0x191C20);
  c.surface = Ink(light ? 0xF4F3EF : 0x21252A);
  c.raisedSurface = Ink(light ? 0xFFFFFF : 0x2D333B);
  c.sunkenSurface = Ink(light ? 0xE8E9E5 : 0x30353B);
  c.foreground = Ink(light ? 0x202423 : 0xF5F3EB);
  c.mutedForeground = Ink(light ? 0x565E60 : 0xB2BABD);
  c.border = Ink(light ? 0xCFD2CD : 0x49505A);
  c.accent = Ink(light ? 0x2949CE : 0x344FC9);
  c.accentHovered = Ink(light ? 0x1F3AB8 : 0x405BDD);
  c.accentPressed = Ink(0x203599);
  c.accentForeground = Ink(0xFFFFFF);
  c.focus = Ink(light ? 0x2644BC : 0xABC0FF);
  c.selection = Ink(light ? 0xDDE5FF : 0x323E62);
  theme.radii = {.small = 3, .regular = 5, .large = 8};
  theme.controls.regularHeight = 36;
  theme.controls.spaciousHeight = 40;
  theme.typography.body = 15;
  return theme;
}

void GalleryViewModel::UpdateViewport(UInt32 pixelWidth, F32 scaleFactor) {
  if (!pixelWidth || scaleFactor <= 0)
    return;
  const bool compact = static_cast<F32>(pixelWidth) / scaleFactor < 860;
  if (m_compact.Set(compact) && !compact)
    SetBrowserOpen(false);
}

auto MainView(GalleryViewModel &model) -> Views::View {
  return Views::Component(
      [&model, rail = Navigation(model), page = Catalogue(model)] {
        const auto theme = model.CurrentTheme();
        const bool compact = model.IsCompact();
        const bool browse = compact && model.BrowserOpen();
        NodeProperties root{};
        root.visual.base.background = theme.colors.background;
        NodeProperties nav{}, content{};
        nav.visibility = compact && !browse ? ElementVisibility::Collapsed
                                            : ElementVisibility::Visible;
        content.visibility =
            browse ? ElementVisibility::Collapsed : ElementVisibility::Visible;
        // Keep both slots mounted across a resize or a visit to the compact
        // browser.
        std::vector<Views::View> slots{
            Views::Overlay({rail}, nav).Key("navigation-slot"),
            Views::Overlay({page}, content).Key("sample-slot")};
        if (compact)
          slots.push_back(CompactHeader(model, theme).Key("compact-header"));
        return Views::Layout(std::make_shared<WorkspaceLayout>(compact),
                             std::move(slots), root)
            .Key("gallery-root");
      });
}
} // namespace NGIN::UIGallery
