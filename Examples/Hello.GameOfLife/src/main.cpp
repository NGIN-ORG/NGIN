#include "LifeBoard.hpp"
#include "LifeSimulation.hpp"

#include <NGIN/UI/Backend/SDL3/SDL3.hpp>
#include <NGIN/UI/UI.hpp>
#include <NGIN/UI/ViewControls.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace Hello::GameOfLife {
namespace {
using namespace NGIN::UI;

struct FrameStats final {
  NGIN::UInt64 generation{};
  NGIN::UIntSize population{};
  double batchMicroseconds{};
  double actualRate{};
  NGIN::UIntSize displayCommands{};
  NGIN::UIntSize drawBatches{};
  double uiMilliseconds{};
  NGIN::Text::String status{};
  auto operator==(const FrameStats &) const -> bool = default;
};

class GameModel final {
public:
  explicit GameModel(Window &window, IRenderBackend &renderer)
      : m_window(window), m_images(renderer),
        m_surface(CreateSurface(m_simulation)) {
    PublishStats();
  }

  ~GameModel() {
    if (m_scheduledAction != 0) {
      static_cast<void>(m_window.CancelScheduled(m_scheduledAction));
    }
  }

  [[nodiscard]] auto Simulation() const noexcept -> const LifeSimulation & {
    return m_simulation;
  }

  [[nodiscard]] auto Surface() const noexcept
      -> const std::shared_ptr<ImageResource> & {
    return m_surface;
  }

  [[nodiscard]] auto Images() noexcept -> ImageTextureCache & {
    return m_images;
  }

  [[nodiscard]] auto Running() const noexcept -> bool {
    return m_running.Get();
  }

  [[nodiscard]] auto Speed() noexcept -> State<NGIN::F32> & { return m_speed; }

  [[nodiscard]] auto Stats() const noexcept -> const FrameStats & {
    return m_stats.Get();
  }
  [[nodiscard]] auto Rule() const noexcept -> LifeRule { return m_rule.Get(); }

  [[nodiscard]] auto TargetRate() const noexcept -> double {
    return std::pow(120.0, static_cast<double>(m_speed.Get()));
  }

  void Start() {
    if (!Running()) {
      static_cast<void>(m_running.Set(true));
      m_generationAccumulator = 0.0;
      m_lastPulse = std::chrono::steady_clock::now();
      m_rateWindowStarted = m_lastPulse;
      m_rateWindowGeneration = m_simulation.Generation();
    }
    ScheduleNext();
  }

  void ToggleRunning() {
    if (Running()) {
      Pause();
    } else {
      Start();
    }
  }

  void Step() {
    try {
      m_simulation.StepGeneration();
      RefreshSurface();
      m_status = {};
      InvalidateSimulation();
    } catch (const std::exception &error) {
      Fail(error.what());
    }
  }

  void RunSmokeBatch() {
    try {
      m_simulation.StepGenerations(2);
      RefreshSurface();
      m_status = {};
      InvalidateSimulation();
    } catch (const std::exception &error) {
      Fail(error.what());
    }
  }

  void Clear() {
    Pause();
    m_simulation.Clear();
    RefreshSurface();
    m_status = {};
    InvalidateSimulation();
  }

  void Randomize() {
    Pause();
    m_simulation.Randomize();
    RefreshSurface();
    m_status = {};
    InvalidateSimulation();
  }

  void LoadGliderFleet() {
    Pause();
    m_simulation.LoadGliderFleet();
    RefreshSurface();
    m_status = {};
    InvalidateSimulation();
  }

  void LoadMethuselahField() {
    Pause();
    m_simulation.LoadMethuselahField();
    RefreshSurface();
    m_status = {};
    InvalidateSimulation();
  }

  void ToggleRule() {
    m_simulation.SetRule(m_simulation.Rule() == LifeRule::Conway
                             ? LifeRule::HighLife
                             : LifeRule::Conway);
    static_cast<void>(m_rule.Set(m_simulation.Rule()));
    InvalidateSimulation();
  }

  void SetCell(const NGIN::UIntSize x, const NGIN::UIntSize y,
               const bool alive) {
    m_simulation.SetCell(x, y, alive);
    RefreshSurface();
    InvalidateSimulation();
  }

private:
  [[nodiscard]] static auto CreateSurface(const LifeSimulation &simulation)
      -> std::shared_ptr<ImageResource> {
    auto created = ImageResource::FromPixels(
        ImagePixels{
            .size = PixelSize{static_cast<NGIN::UInt32>(simulation.Width()),
                              static_cast<NGIN::UInt32>(simulation.Height())},
            .rgba = simulation.Pixels(),
        },
        TextureFilter::Nearest);
    if (!created) {
      throw std::runtime_error{created.error().message.CStr()};
    }
    return std::move(created).value();
  }

  void RefreshSurface() {
    auto updated = m_surface->UpdatePixels(ImagePixels{
        .size = PixelSize{static_cast<NGIN::UInt32>(m_simulation.Width()),
                          static_cast<NGIN::UInt32>(m_simulation.Height())},
        .rgba = m_simulation.Pixels(),
    });
    if (!updated) {
      throw std::runtime_error{updated.error().message.CStr()};
    }
  }

  void Pause() {
    if (!Running()) {
      return;
    }
    static_cast<void>(m_running.Set(false));
    m_actualRate = 0.0;
    if (m_scheduledAction != 0) {
      static_cast<void>(m_window.CancelScheduled(m_scheduledAction));
      m_scheduledAction = 0;
    }
    PublishStats();
  }

  void ScheduleNext() {
    if (!Running() || m_scheduledAction != 0) {
      return;
    }
    auto scheduled = m_window.Schedule(std::chrono::milliseconds{8}, [this] {
      m_scheduledAction = 0;
      if (!Running()) {
        return;
      }
      const auto now = std::chrono::steady_clock::now();
      const auto elapsed = std::min(
          std::chrono::duration<double>(now - m_lastPulse).count(), 0.25);
      m_lastPulse = now;
      m_generationAccumulator =
          std::min(m_generationAccumulator + elapsed * TargetRate(), 12.0);
      const auto requestedGenerations =
          static_cast<NGIN::UIntSize>(std::floor(m_generationAccumulator));
      auto generations = requestedGenerations;
      if (m_lastBatchSize > 0 && m_simulation.LastFrameMicroseconds() > 0.0) {
        const auto microsecondsPerGeneration =
            m_simulation.LastFrameMicroseconds() /
            static_cast<double>(m_lastBatchSize);
        const auto responsiveBatch = static_cast<NGIN::UIntSize>(
            std::clamp(12'000.0 / microsecondsPerGeneration, 1.0, 12.0));
        generations = std::min(generations, responsiveBatch);
      }
      if (generations > 0) {
        try {
          m_simulation.StepGenerations(generations);
          m_lastBatchSize = generations;
          RefreshSurface();
          m_generationAccumulator -= static_cast<double>(generations);
          m_status = {};
          UpdateRate(now);
          InvalidateFrame(now);
        } catch (const std::exception &error) {
          Fail(error.what());
        }
      }
      ScheduleNext();
    });
    if (!scheduled) {
      Fail(scheduled.error().message.CStr());
      return;
    }
    m_scheduledAction = scheduled.value();
  }

  void Fail(const char *message) {
    static_cast<void>(m_running.Set(false));
    m_scheduledAction = 0;
    m_status = NGIN::Text::String{message};
    InvalidateSimulation();
  }

  void UpdateRate(const std::chrono::steady_clock::time_point now) {
    const auto seconds =
        std::chrono::duration<double>(now - m_rateWindowStarted).count();
    if (seconds < 0.5) {
      return;
    }
    m_actualRate = static_cast<double>(m_simulation.Generation() -
                                       m_rateWindowGeneration) /
                   seconds;
    m_rateWindowStarted = now;
    m_rateWindowGeneration = m_simulation.Generation();
  }

  void InvalidateFrame(const std::chrono::steady_clock::time_point now) {
    if (now - m_lastDetailsInvalidation >= std::chrono::milliseconds{100}) {
      PublishStats();
      m_lastDetailsInvalidation = now;
    }
    m_window.Invalidate(InvalidationKind::Paint);
  }

  void InvalidateSimulation() {
    PublishStats();
    m_window.Invalidate(InvalidationKind::Paint | InvalidationKind::Semantics);
  }

  void PublishStats() {
    const auto &ui = m_window.Diagnostics();
    static_cast<void>(m_stats.Set(FrameStats{
        .generation = m_simulation.Generation(),
        .population = m_simulation.Population(),
        .batchMicroseconds = m_simulation.LastFrameMicroseconds(),
        .actualRate = m_actualRate,
        .displayCommands = ui.displayCommandCount,
        .drawBatches = ui.drawBatchCount,
        .uiMilliseconds = ui.frameTimings.totalMilliseconds,
        .status = m_status,
    }));
  }

  Window &m_window;
  LifeSimulation m_simulation{};
  ImageTextureCache m_images;
  std::shared_ptr<ImageResource> m_surface;
  State<NGIN::F32> m_speed{0.58F};
  State<LifeRule> m_rule{m_simulation.Rule()};
  State<FrameStats> m_stats{};
  NGIN::Text::String m_status{};
  Window::ScheduledActionId m_scheduledAction{0};
  std::chrono::steady_clock::time_point m_lastPulse{};
  std::chrono::steady_clock::time_point m_lastDetailsInvalidation{};
  std::chrono::steady_clock::time_point m_rateWindowStarted{};
  NGIN::UInt64 m_rateWindowGeneration{0};
  double m_generationAccumulator{0.0};
  double m_actualRate{0.0};
  NGIN::UIntSize m_lastBatchSize{0};
  State<bool> m_running{false};
};

[[nodiscard]] auto TextProperties(const NGIN::F32 fontSize, const Color color)
    -> NodeProperties {
  NodeProperties properties{};
  properties.layout.horizontalAlignment = HorizontalAlignment::Start;
  properties.layout.verticalAlignment = VerticalAlignment::Start;
  properties.interaction.hitTestVisible = false;
  properties.text.fontSize = fontSize;
  properties.text.color = color;
  properties.text.wrapping = TextWrapping::Wrap;
  return properties;
}

auto Caption(NGIN::Text::String value, const NGIN::F32 fontSize, Color color,
             std::string_view key, SemanticRole role = SemanticRole::Text)
    -> Views::View {
  auto properties = TextProperties(fontSize, color);
  properties.semantics.role = role;
  return Views::Label(std::move(value), properties).Key(key);
}

auto Action(const Theme &theme, const char *label,
            NGIN::Utilities::Callable<void()> onActivate, std::string_view key,
            NGIN::F32 width, bool selected = false) -> Views::View {
  NodeProperties button{};
  button.layout.preferredSize = Size{width, theme.controls.regularHeight};
  button.layout.padding = Thickness{12.0F, 8.0F, 12.0F, 8.0F};
  button.layout.horizontalAlignment = HorizontalAlignment::Start;
  button.layout.verticalAlignment = VerticalAlignment::Start;
  button.interaction.focusable = true;
  button.interaction.onActivate = std::move(onActivate);
  button.semantics.role = SemanticRole::Button;
  button.semantics.label = NGIN::Text::String{label};
  button.semantics.actions =
      SemanticActionFlags::Activate | SemanticActionFlags::Focus;
  button.visual = MakeButtonVisual(theme);
  if (selected) {
    button.visual.state |= VisualStateFlags::Selected;
    button.visual.states.selected.background = theme.colors.raisedSurface;
    button.visual.states.selected.foreground = theme.colors.foreground;
    button.visual.states.selected.borderColor = theme.colors.focus;
  }

  auto caption = TextProperties(theme.typography.body,
                                selected ? theme.colors.foreground
                                         : theme.colors.accentForeground);
  caption.layout.horizontalAlignment = HorizontalAlignment::Center;
  caption.layout.verticalAlignment = VerticalAlignment::Center;
  caption.text.wrapping = TextWrapping::NoWrap;
  return Views::Box(
             ElementType::Button,
             {Views::Label(NGIN::Text::String{label}, caption).Key("label")},
             button)
      .Key(key);
}

[[nodiscard]] auto NumberText(const char *label, const NGIN::UInt64 value)
    -> NGIN::Text::String {
  auto result = std::string{label};
  result += std::to_string(value);
  return NGIN::Text::String{result.c_str()};
}

[[nodiscard]] auto FrameTimeText(const double microseconds)
    -> NGIN::Text::String {
  auto result = std::string{"Last ECS batch: "};
  result += std::to_string(
      static_cast<NGIN::UInt64>(std::max(0.0, std::round(microseconds))));
  result += " us";
  return NGIN::Text::String{result.c_str()};
}

[[nodiscard]] auto RateText(const char *label, const double rate)
    -> NGIN::Text::String {
  auto result = std::string{label};
  result += std::to_string(
      static_cast<NGIN::UInt64>(std::max(0.0, std::round(rate))));
  result += " generations/s";
  return NGIN::Text::String{result.c_str()};
}

[[nodiscard]] auto MillisecondsText(const char *label, const double value)
    -> NGIN::Text::String {
  auto result = std::string{label};
  result += std::to_string(std::max(0.0, value));
  result += " ms";
  return NGIN::Text::String{result.c_str()};
}

auto Toolbar(GameModel &model, const Theme &theme) -> Views::View {
  NodeProperties toolbar{};
  toolbar.gridPlacement = GridPlacement{.row = 1, .column = 0, .columnSpan = 2};
  toolbar.wrapPanel.itemGap = 8;
  toolbar.wrapPanel.lineGap = 8;
  return Views::Box(
             ElementType::WrapPanel,
             {
                 Views::Component([&model, theme] {
                   return Action(
                       theme, model.Running() ? "Pause" : "Run",
                       [&model] { model.ToggleRunning(); }, "run", 104,
                       model.Running());
                 }),
                 Action(
                     theme, "Step", [&model] { model.Step(); }, "step", 92),
                 Action(
                     theme, "Clear", [&model] { model.Clear(); }, "clear", 92),
                 Action(
                     theme, "Randomize", [&model] { model.Randomize(); },
                     "random", 116),
                 Action(
                     theme, "Glider fleet",
                     [&model] { model.LoadGliderFleet(); }, "fleet", 122),
                 Action(
                     theme, "Acorn field",
                     [&model] { model.LoadMethuselahField(); }, "acorns", 122),
                 Views::Component([&model, theme] {
                   return Action(
                       theme,
                       model.Rule() == LifeRule::Conway ? "Rule: Conway"
                                                        : "Rule: HighLife",
                       [&model] { model.ToggleRule(); }, "rule", 134, true);
                 }),
             },
             toolbar)
      .Key("toolbar");
}

auto Details(GameModel &model, const Theme &theme) -> Views::View {
  NodeProperties sidebar{};
  sidebar.gridPlacement = GridPlacement{.row = 2, .column = 1};
  sidebar.layout.padding = Thickness::Uniform(Dp{16});
  sidebar.layout.gap = 11;
  sidebar.layout.verticalAlignment = VerticalAlignment::Stretch;
  sidebar.visual = MakePanelVisual(theme);
  sidebar.interaction.focusable = true;
  sidebar.scroll.vertical = true;
  sidebar.scroll.horizontal = false;
  sidebar.semantics.role = SemanticRole::Group;
  sidebar.semantics.label = NGIN::Text::String{"Simulation details"};
  auto details = Views::Component([&model, theme] {
    const auto &stats = model.Stats();
    const auto &simulation = model.Simulation();
    auto title = [&theme](const char *value, const char *key) {
      return Caption(value, theme.typography.title, theme.colors.foreground,
                     key, SemanticRole::Heading);
    };
    auto body = [&theme](NGIN::Text::String value, const char *key) {
      return Caption(std::move(value), theme.typography.body,
                     theme.colors.foreground, key);
    };
    auto note = [&theme](NGIN::Text::String value, const char *key) {
      return Caption(std::move(value), theme.typography.caption,
                     theme.colors.mutedForeground, key);
    };
    NodeProperties separator{};
    separator.visual = MakeSeparatorVisual(theme);
    NodeProperties slider{};
    slider.layout.preferredSize = Size{240, 40};
    std::vector<Views::View> children{
        title("Simulation", "simulation-heading"),
        body(NumberText("Entities: ", LifeSimulation::EntityCount), "entities"),
        body(NumberText("Generation: ", stats.generation), "generation"),
        body(NumberText("Living cells: ", stats.population), "population"),
        Caption(FrameTimeText(stats.batchMicroseconds), theme.typography.body,
                theme.colors.mutedForeground, "frame-time"),
        Views::Separator(SeparatorOrientation::Horizontal, separator)
            .Key("stats-separator"),
        title("ECS schedule", "schedule-heading"),
        body(NumberText("Systems: ", simulation.SystemCount()), "systems"),
        body(NumberText("Dependency stages: ", simulation.StageCount()),
             "stages"),
        note("Deterministic parallel executor\nParallel chunks: evolve, "
             "commit, rasterize",
             "executor"),
        title("NGIN.UI frame", "ui-heading"),
        body(NumberText("Display commands: ", stats.displayCommands),
             "commands"),
        body(NumberText("Draw batches: ", stats.drawBatches), "batches"),
        note(MillisecondsText("UI CPU frame: ", stats.uiMilliseconds),
             "ui-time"),
        body(RateText("Target: ", model.TargetRate()), "speed-label"),
        note(RateText("Measured: ", stats.actualRate), "actual-speed"),
        Views::Slider(Bind(model.Speed()),
                      SliderRange{.minimum = 0, .maximum = 1, .step = 0.01F},
                      ControlPresentation{.theme = theme}, slider)
            .Key("speed"),
        note("Wheel to zoom. Middle/right drag to pan. Primary drag paints "
             "cells. Click the minimap to jump. Home shows the whole universe; "
             "arrow keys and Space edit the focused cell.",
             "instructions")};
    if (!stats.status.Empty()) {
      children.push_back(Caption(stats.status, theme.typography.caption,
                                 theme.colors.error, "status"));
    }
    NodeProperties column{};
    column.layout.gap = 10;
    return Views::Column(std::move(children), column).Key("details");
  });
  return Views::Box(ElementType::ScrollView, {std::move(details)}, sidebar)
      .Key("sidebar");
}

auto MainView(GameModel &model) -> Views::View {
  const Theme theme{};
  NodeProperties root{};
  root.layout.padding = Thickness::Uniform(Dp{22});
  root.grid.rows = {GridTrack::Auto(), GridTrack::Auto(),
                    GridTrack::Weighted(1, 300)};
  root.grid.columns = {GridTrack::Weighted(1, 420), GridTrack::Fixed(276)};
  root.grid.rowGap = 14;
  root.grid.columnGap = 18;
  root.visual.base.background = theme.colors.background;
  root.semantics.role = SemanticRole::Group;
  root.semantics.label = NGIN::Text::String{"Hello Game of Life"};

  NodeProperties heading{};
  heading.gridPlacement = GridPlacement{.row = 0, .column = 0, .columnSpan = 2};
  heading.layout.gap = 4;
  auto title = Views::Column(
                   {Caption("Hello, Game of Life", 30, theme.colors.foreground,
                            "title", SemanticRole::Heading),
                    Caption("NGIN.ECS evolves 1,048,576 entities while NGIN.UI "
                            "streams the universe through one dynamic texture.",
                            theme.typography.body, theme.colors.mutedForeground,
                            "subtitle")},
                   heading)
                   .Key("heading");

  NodeProperties board{};
  board.gridPlacement = GridPlacement{.row = 2, .column = 0};
  board.layout.minimumSize = Size{360, 260};
  board.layout.flexGrow = 1;
  board.layout.horizontalAlignment = HorizontalAlignment::Stretch;
  board.layout.verticalAlignment = VerticalAlignment::Stretch;
  board.interaction.focusable = true;
  board.semantics.role = SemanticRole::Image;
  board.semantics.identifier = NGIN::Text::String{"life-board"};
  auto lifeBoard = std::make_shared<LifeBoardElement>(
      model.Simulation(), model.Surface(), model.Images(),
      [&model](NGIN::UIntSize x, NGIN::UIntSize y, bool alive) {
        model.SetCell(x, y, alive);
      });
  return Views::Box(
             ElementType::Grid,
             {std::move(title), Toolbar(model, theme),
              Views::Custom(std::move(lifeBoard), board).Key("life-board"),
              Details(model, theme)},
             root)
      .Key("root");
}

auto ReportError(const char *context, const UIError &error) -> int {
  std::cerr << context << ": " << error.message.CStr() << '\n';
  return 1;
}
} // namespace
} // namespace Hello::GameOfLife

auto main(const int argc, char **argv) -> int {
  using namespace Hello::GameOfLife;
  using namespace NGIN::UI;

  bool smoke = false;
  if (argc == 2 && std::string_view{argv[1]} == "--smoke") {
    smoke = true;
  } else if (argc != 1) {
    std::cerr << "Usage: Hello.GameOfLife [--smoke]\n";
    return 2;
  }

  auto createdApplication = CreateApplication(ApplicationCreateInfo{
      .platform = SDL3::CreatePlatformBackend(),
      .renderer = SDL3::CreateRendererBackend(),
      .applicationName = NGIN::Text::String{"Hello.GameOfLife"},
      .enableRendererValidation = true,
  });
  if (!createdApplication) {
    return ReportError("Application creation failed",
                       createdApplication.error());
  }
  auto application = std::move(createdApplication).value();

  auto createdText = NativeTextSystem::Create(application->Renderer());
  if (!createdText) {
    return ReportError("Native text creation failed", createdText.error());
  }
  auto text = std::move(createdText).value();
  text->SetResourcesInvalidatedCallback(
      [applicationObserver = application.get()] {
        applicationObserver->InvalidateAll(InvalidationKind::All);
      });

  auto createdWindow = application->CreateWindow(WindowCreateInfo{
      .id = NGIN::Text::String{"Hello.GameOfLife.Main"},
      .title = NGIN::Text::String{"NGIN - Conway's Game of Life"},
      .initialSize = PixelSize{1180, 760},
      .minimumSize = PixelSize{760, 560},
  });
  if (!createdWindow) {
    return ReportError("Window creation failed", createdWindow.error());
  }
  auto *window = createdWindow.value();

  GameModel model{*window, application->Renderer()};
  window->SetContent(MainView(model), Views::ViewEnvironment{*text});

  if (smoke) {
    auto initial = application->PumpOnce();
    if (!initial) {
      return ReportError("Initial smoke frame failed", initial.error());
    }
    const auto board = window->Tree().FindBySemanticIdentifier(
        NGIN::Text::String{"life-board"});
    if (!board.IsValid() || !window->Focus(board)) {
      std::cerr << "The mounted game board did not accept keyboard focus\n";
      return 1;
    }
    const auto expectedGeneration = model.Simulation().Generation() + 2;
    model.RunSmokeBatch();
    auto pumped = application->PumpOnce();
    if (!pumped) {
      return ReportError("Smoke frame failed", pumped.error());
    }
    const auto expectedLabel = NumberText("Generation: ", expectedGeneration);
    const auto &nodes = window->Semantics().Nodes();
    if (window->FocusedElement() != board ||
        std::none_of(nodes.begin(), nodes.end(), [&](const auto &node) {
          return node.value == expectedLabel;
        })) {
      std::cerr << "Advancing the simulation must update statistics and "
                   "preserve board focus\n";
      return 1;
    }
    const auto &diagnostics = window->Diagnostics();
    std::cout << "entities=" << LifeSimulation::EntityCount
              << " display_commands=" << diagnostics.displayCommandCount
              << " draw_batches=" << diagnostics.drawBatchCount
              << " ui_cpu_ms=" << diagnostics.frameTimings.totalMilliseconds
              << '\n';
    if (diagnostics.displayCommandCount == 0 ||
        diagnostics.displayCommandCount > 128U) {
      std::cerr
          << "Smoke frame emitted an unexpected number of display commands\n";
      return 1;
    }
    return 0;
  }
  model.Start();

  auto run = application->Run();
  return run ? 0 : ReportError("Application run failed", run.error());
}
