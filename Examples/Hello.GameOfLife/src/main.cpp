#include "LifeBoard.hpp"
#include "LifeSimulation.hpp"

#include <NGIN/UI/Backend/SDL3/SDL3.hpp>
#include <NGIN/UI/UI.hpp>

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

class GameModel final {
public:
  explicit GameModel(Window &window, IRenderBackend &renderer)
      : m_window(window), m_images(renderer),
        m_surface(CreateSurface(m_simulation)),
        m_speed(0.58F, [this](const InvalidationKind kind) {
          m_window.Invalidate(kind);
        }) {}

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

  [[nodiscard]] auto Running() const noexcept -> bool { return m_running; }

  [[nodiscard]] auto Speed() noexcept -> State<NGIN::F32> & { return m_speed; }

  [[nodiscard]] auto ActualRate() const noexcept -> double {
    return m_actualRate;
  }

  [[nodiscard]] auto TargetRate() const noexcept -> double {
    return std::pow(120.0, static_cast<double>(m_speed.Get()));
  }

  [[nodiscard]] auto Status() const noexcept -> const NGIN::Text::String & {
    return m_status;
  }

  [[nodiscard]] auto UIDiagnostics() const noexcept
      -> const WindowDiagnostics & {
    return m_window.Diagnostics();
  }

  void Start() {
    if (!m_running) {
      m_running = true;
      m_generationAccumulator = 0.0;
      m_lastPulse = std::chrono::steady_clock::now();
      m_rateWindowStarted = m_lastPulse;
      m_rateWindowGeneration = m_simulation.Generation();
      m_window.Invalidate(InvalidationKind::Compose |
                          InvalidationKind::Semantics);
    }
    ScheduleNext();
  }

  void ToggleRunning() {
    if (m_running) {
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
      throw std::runtime_error{created.Error().message.CStr()};
    }
    return std::move(created).Value();
  }

  void RefreshSurface() {
    auto updated = m_surface->UpdatePixels(ImagePixels{
        .size = PixelSize{static_cast<NGIN::UInt32>(m_simulation.Width()),
                          static_cast<NGIN::UInt32>(m_simulation.Height())},
        .rgba = m_simulation.Pixels(),
    });
    if (!updated) {
      throw std::runtime_error{updated.Error().message.CStr()};
    }
  }

  void Pause() {
    if (!m_running) {
      return;
    }
    m_running = false;
    m_actualRate = 0.0;
    if (m_scheduledAction != 0) {
      static_cast<void>(m_window.CancelScheduled(m_scheduledAction));
      m_scheduledAction = 0;
    }
    m_window.Invalidate(InvalidationKind::Compose |
                        InvalidationKind::Semantics);
  }

  void ScheduleNext() {
    if (!m_running || m_scheduledAction != 0) {
      return;
    }
    auto scheduled = m_window.Schedule(std::chrono::milliseconds{8}, [this] {
      m_scheduledAction = 0;
      if (!m_running) {
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
      Fail(scheduled.Error().message.CStr());
      return;
    }
    m_scheduledAction = scheduled.Value();
  }

  void Fail(const char *message) {
    m_running = false;
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
    auto kind = InvalidationKind::Paint;
    if (now - m_lastDetailsInvalidation >= std::chrono::milliseconds{100}) {
      kind = kind | InvalidationKind::Compose | InvalidationKind::Semantics;
      m_lastDetailsInvalidation = now;
    }
    m_window.Invalidate(kind);
  }

  void InvalidateSimulation() const noexcept {
    m_window.Invalidate(InvalidationKind::Compose | InvalidationKind::Paint |
                        InvalidationKind::Semantics);
  }

  Window &m_window;
  LifeSimulation m_simulation{};
  ImageTextureCache m_images;
  std::shared_ptr<ImageResource> m_surface;
  State<NGIN::F32> m_speed;
  NGIN::Text::String m_status{};
  Window::ScheduledActionId m_scheduledAction{0};
  std::chrono::steady_clock::time_point m_lastPulse{};
  std::chrono::steady_clock::time_point m_lastDetailsInvalidation{};
  std::chrono::steady_clock::time_point m_rateWindowStarted{};
  NGIN::UInt64 m_rateWindowGeneration{0};
  double m_generationAccumulator{0.0};
  double m_actualRate{0.0};
  NGIN::UIntSize m_lastBatchSize{0};
  bool m_running{false};
};

[[nodiscard]] auto TextProperties(NativeTextSystem &text,
                                  const NGIN::F32 fontSize, const Color color)
    -> NodeProperties {
  NodeProperties properties{};
  properties.layout.horizontalAlignment = HorizontalAlignment::Start;
  properties.layout.verticalAlignment = VerticalAlignment::Start;
  properties.interaction.hitTestVisible = false;
  properties.text.fontSize = fontSize;
  properties.text.color = color;
  properties.text.geometry = &text;
  properties.text.wrapping = TextWrapping::Wrap;
  return properties;
}

void ComposeText(Composer &composer, NativeTextSystem &text,
                 NGIN::Text::String value, const NGIN::F32 fontSize,
                 const Color color, const std::string_view key,
                 const SemanticRole role = SemanticRole::Text) {
  auto properties = TextProperties(text, fontSize, color);
  properties.semantics.role = role;
  composer.Text(std::move(value), text, text, properties, key);
}

void ComposeButton(Composer &composer, NativeTextSystem &text,
                   const Theme &theme, const char *label,
                   NGIN::Utilities::Callable<void()> onActivate,
                   const std::string_view key, const NGIN::F32 width = 118.0F,
                   const bool selected = false) {
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

  composer.Element(
      ElementType::Button, button,
      [&] {
        auto labelProperties = TextProperties(
            text, theme.typography.body,
            selected ? theme.colors.foreground : theme.colors.accentForeground);
        labelProperties.layout.horizontalAlignment =
            HorizontalAlignment::Center;
        labelProperties.layout.verticalAlignment = VerticalAlignment::Center;
        labelProperties.text.wrapping = TextWrapping::NoWrap;
        composer.Text(NGIN::Text::String{label}, text, text, labelProperties,
                      "label");
      },
      key);
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

void ComposeMainView(Composer &composer, NativeTextSystem &text,
                     GameModel &model) {
  const Theme theme{};
  const auto &simulation = model.Simulation();

  NodeProperties root{};
  root.layout.padding = Thickness::Uniform(Dp{22.0F});
  root.grid.rows = {GridTrack::Auto(), GridTrack::Auto(),
                    GridTrack::Weighted(1.0F, 300.0F)};
  root.grid.columns = {GridTrack::Weighted(1.0F, 420.0F),
                       GridTrack::Fixed(276.0F)};
  root.grid.rowGap = 14.0F;
  root.grid.columnGap = 18.0F;
  root.visual.base.background = theme.colors.background;
  root.semantics.role = SemanticRole::Group;
  root.semantics.label = NGIN::Text::String{"Hello Game of Life"};

  composer.Grid(
      [&] {
        NodeProperties heading{};
        heading.gridPlacement =
            GridPlacement{.row = 0, .column = 0, .columnSpan = 2};
        heading.layout.gap = 4.0F;
        composer.Element(
            ElementType::Column, heading,
            [&] {
              ComposeText(composer, text,
                          NGIN::Text::String{"Hello, Game of Life"}, 30.0F,
                          theme.colors.foreground, "title",
                          SemanticRole::Heading);
              ComposeText(
                  composer, text,
                  NGIN::Text::String{
                      "NGIN.ECS evolves 1,048,576 entities while NGIN.UI "
                      "streams the universe through one dynamic texture."},
                  theme.typography.body, theme.colors.mutedForeground,
                  "subtitle");
            },
            "heading");

        NodeProperties toolbar{};
        toolbar.gridPlacement =
            GridPlacement{.row = 1, .column = 0, .columnSpan = 2};
        toolbar.wrapPanel.itemGap = 8.0F;
        toolbar.wrapPanel.lineGap = 8.0F;
        composer.WrapPanel(
            [&] {
              ComposeButton(
                  composer, text, theme, model.Running() ? "Pause" : "Run",
                  [&model] { model.ToggleRunning(); }, "run", 104.0F,
                  model.Running());
              ComposeButton(
                  composer, text, theme, "Step", [&model] { model.Step(); },
                  "step", 92.0F);
              ComposeButton(
                  composer, text, theme, "Clear", [&model] { model.Clear(); },
                  "clear", 92.0F);
              ComposeButton(
                  composer, text, theme, "Randomize",
                  [&model] { model.Randomize(); }, "random", 116.0F);
              ComposeButton(
                  composer, text, theme, "Glider fleet",
                  [&model] { model.LoadGliderFleet(); }, "fleet", 122.0F);
              ComposeButton(
                  composer, text, theme, "Acorn field",
                  [&model] { model.LoadMethuselahField(); }, "acorns", 122.0F);
              ComposeButton(
                  composer, text, theme,
                  simulation.Rule() == LifeRule::Conway ? "Rule: Conway"
                                                        : "Rule: HighLife",
                  [&model] { model.ToggleRule(); }, "rule", 134.0F, true);
            },
            toolbar, "toolbar");

        NodeProperties board{};
        board.gridPlacement = GridPlacement{.row = 2, .column = 0};
        board.layout.minimumSize = Size{360.0F, 260.0F};
        board.layout.flexGrow = 1.0F;
        board.layout.horizontalAlignment = HorizontalAlignment::Stretch;
        board.layout.verticalAlignment = VerticalAlignment::Stretch;
        board.interaction.focusable = true;
        board.semantics.role = SemanticRole::Image;
        composer.Custom(
            std::make_shared<LifeBoardElement>(
                simulation, model.Surface(), model.Images(),
                [&model](const NGIN::UIntSize x, const NGIN::UIntSize y,
                         const bool alive) { model.SetCell(x, y, alive); }),
            board, "life-board");

        NodeProperties sidebar{};
        sidebar.gridPlacement = GridPlacement{.row = 2, .column = 1};
        sidebar.layout.padding = Thickness::Uniform(Dp{16.0F});
        sidebar.layout.gap = 11.0F;
        sidebar.layout.verticalAlignment = VerticalAlignment::Stretch;
        sidebar.visual = MakePanelVisual(theme);
        sidebar.interaction.focusable = true;
        sidebar.scroll.vertical = true;
        sidebar.scroll.horizontal = false;
        sidebar.semantics.role = SemanticRole::Group;
        sidebar.semantics.label = NGIN::Text::String{"Simulation details"};
        composer.ScrollView(
            [&] {
              NodeProperties details{};
              details.layout.gap = 10.0F;
              composer.Element(
                  ElementType::Column, details,
                  [&] {
                    ComposeText(composer, text,
                                NGIN::Text::String{"Simulation"},
                                theme.typography.title, theme.colors.foreground,
                                "simulation-heading", SemanticRole::Heading);
                    ComposeText(
                        composer, text,
                        NumberText("Entities: ", LifeSimulation::EntityCount),
                        theme.typography.body, theme.colors.foreground,
                        "entities");
                    ComposeText(
                        composer, text,
                        NumberText("Generation: ", simulation.Generation()),
                        theme.typography.body, theme.colors.foreground,
                        "generation");
                    ComposeText(
                        composer, text,
                        NumberText("Living cells: ", simulation.Population()),
                        theme.typography.body, theme.colors.foreground,
                        "population");
                    ComposeText(
                        composer, text,
                        FrameTimeText(simulation.LastFrameMicroseconds()),
                        theme.typography.body, theme.colors.mutedForeground,
                        "frame-time");

                    NodeProperties separator{};
                    separator.visual = MakeSeparatorVisual(theme);
                    composer.Separator(SeparatorOrientation::Horizontal,
                                       separator, "stats-separator");

                    ComposeText(composer, text,
                                NGIN::Text::String{"ECS schedule"},
                                theme.typography.title, theme.colors.foreground,
                                "schedule-heading", SemanticRole::Heading);
                    ComposeText(
                        composer, text,
                        NumberText("Systems: ", simulation.SystemCount()),
                        theme.typography.body, theme.colors.foreground,
                        "systems");
                    ComposeText(composer, text,
                                NumberText("Dependency stages: ",
                                           simulation.StageCount()),
                                theme.typography.body, theme.colors.foreground,
                                "stages");
                    ComposeText(
                        composer, text,
                        NGIN::Text::String{"Deterministic parallel executor\n"
                                           "Parallel chunks: evolve, commit, "
                                           "rasterize"},
                        theme.typography.caption, theme.colors.mutedForeground,
                        "executor");

                    const auto &ui = model.UIDiagnostics();
                    ComposeText(composer, text,
                                NGIN::Text::String{"NGIN.UI frame"},
                                theme.typography.title, theme.colors.foreground,
                                "ui-heading", SemanticRole::Heading);
                    ComposeText(composer, text,
                                NumberText("Display commands: ",
                                           ui.displayCommandCount),
                                theme.typography.body, theme.colors.foreground,
                                "commands");
                    ComposeText(composer, text,
                                NumberText("Draw batches: ", ui.drawBatchCount),
                                theme.typography.body, theme.colors.foreground,
                                "batches");
                    ComposeText(
                        composer, text,
                        MillisecondsText("UI CPU frame: ",
                                         ui.frameTimings.totalMilliseconds),
                        theme.typography.caption, theme.colors.mutedForeground,
                        "ui-time");

                    ComposeText(composer, text,
                                RateText("Target: ", model.TargetRate()),
                                theme.typography.body, theme.colors.foreground,
                                "speed-label");
                    ComposeText(composer, text,
                                RateText("Measured: ", model.ActualRate()),
                                theme.typography.caption,
                                theme.colors.mutedForeground, "actual-speed");
                    NodeProperties slider{};
                    slider.layout.preferredSize = Size{240.0F, 40.0F};
                    Slider(composer, Bind(model.Speed()),
                           SliderRange{
                               .minimum = 0.0F,
                               .maximum = 1.0F,
                               .step = 0.01F,
                           },
                           ControlPresentation{.theme = theme}, slider,
                           "speed");

                    ComposeText(
                        composer, text,
                        NGIN::Text::String{
                            "Wheel to zoom. Middle/right drag to pan. Primary "
                            "drag paints cells. Click the minimap to jump. "
                            "Home shows the whole universe; arrow keys and "
                            "Space edit the focused cell."},
                        theme.typography.caption, theme.colors.mutedForeground,
                        "instructions");

                    if (!model.Status().Empty()) {
                      ComposeText(composer, text, model.Status(),
                                  theme.typography.caption, theme.colors.error,
                                  "status");
                    }
                  },
                  "details");
            },
            sidebar, "sidebar");
      },
      root, "root");
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
                       createdApplication.Error());
  }
  auto application = std::move(createdApplication).Value();

  auto createdText = NativeTextSystem::Create(application->Renderer());
  if (!createdText) {
    return ReportError("Native text creation failed", createdText.Error());
  }
  auto text = std::move(createdText).Value();
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
    return ReportError("Window creation failed", createdWindow.Error());
  }
  auto *window = createdWindow.Value();

  GameModel model{*window, application->Renderer()};
  window->SetContent([&text, &model](Composer &composer) {
    ComposeMainView(composer, *text, model);
  });

  if (smoke) {
    model.RunSmokeBatch();
    auto pumped = application->PumpOnce();
    if (!pumped) {
      return ReportError("Smoke frame failed", pumped.Error());
    }
    const auto &diagnostics = window->Diagnostics();
    std::cout << "entities=" << LifeSimulation::EntityCount
              << " display_commands=" << diagnostics.displayCommandCount
              << " draw_batches=" << diagnostics.drawBatchCount
              << " ui_cpu_ms=" << diagnostics.frameTimings.totalMilliseconds
              << '\n';
    if (diagnostics.displayCommandCount > 128U) {
      std::cerr << "Smoke frame emitted an unexpectedly large display list\n";
      return 1;
    }
    return 0;
  }
  model.Start();

  auto run = application->Run();
  return run ? 0 : ReportError("Application run failed", run.Error());
}
