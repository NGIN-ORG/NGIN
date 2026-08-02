#include "LifeSimulation.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <utility>

namespace Hello::GameOfLife {
namespace {
constexpr NGIN::UInt8 TrailLifetime = 18;

void WritePixel(std::vector<NGIN::Byte> &pixels, const NGIN::UIntSize index,
                const NGIN::UInt16 age, const NGIN::UInt8 trail) {
  const auto offset = index * 4U;
  if (age != 0) {
    const auto warmth =
        static_cast<NGIN::UInt8>(std::min<NGIN::UInt16>(age, 48));
    pixels[offset] = static_cast<NGIN::Byte>(34U + warmth * 3U);
    pixels[offset + 1U] = static_cast<NGIN::Byte>(220U - warmth);
    pixels[offset + 2U] = static_cast<NGIN::Byte>(190U - warmth * 2U);
  } else if (trail != 0) {
    pixels[offset] = static_cast<NGIN::Byte>(16U + trail * 4U);
    pixels[offset + 1U] = static_cast<NGIN::Byte>(12U + trail);
    pixels[offset + 2U] = static_cast<NGIN::Byte>(34U + trail * 5U);
  } else {
    pixels[offset] = NGIN::Byte{5};
    pixels[offset + 1U] = NGIN::Byte{8};
    pixels[offset + 2U] = NGIN::Byte{18};
  }
  pixels[offset + 3U] = NGIN::Byte{255};
}

constexpr std::array<std::pair<int, int>, 5> Glider{{
    {1, 0},
    {2, 1},
    {0, 2},
    {1, 2},
    {2, 2},
}};

constexpr std::array<std::pair<int, int>, 7> Acorn{{
    {1, 0},
    {3, 1},
    {0, 2},
    {1, 2},
    {4, 2},
    {5, 2},
    {6, 2},
}};
} // namespace

LifeSimulation::LifeSimulation()
    : m_simulation(NGIN::ECS::SimulationConfig{
          .Execution = NGIN::ECS::ExecutionMode::DeterministicParallel,
          .Diagnostics =
              NGIN::ECS::DiagnosticsConfig{
                  .Mode = NGIN::ECS::DiagnosticsMode::Rolling,
                  .HistoryLength = 120,
              },
          .FixedDeltaTime = 1.0,
          .MaxFixedStepsPerFrame = 12,
      }) {
  m_simulation.ReserveEntities(EntityCount);
  m_simulation.ReserveArchetype<CellPosition, CellState, NextCellState>(
      EntityCount);
  m_cells = m_simulation.SpawnBatch(EntityCount, CellPosition{}, CellState{},
                                    NextCellState{});
  for (NGIN::UIntSize index = 0; index < EntityCount; ++index) {
    m_simulation.GetMutable<CellPosition>(m_cells[index]).Index =
        static_cast<NGIN::UInt32>(index);
  }

  m_simulation.InsertResource(BoardSnapshot{
      .Cells = std::vector<NGIN::UInt8>(EntityCount),
  });
  m_simulation.InsertResource(RenderSurface{
      .Pixels = std::vector<NGIN::Byte>(EntityCount * 4U),
  });
  m_simulation.InsertResource(PopulationStats{});
  m_simulation.InsertResource(SimulationRules{});

  ConfigureSchedule();
  LoadGliderFleet();
}

auto LifeSimulation::Width() const noexcept -> NGIN::UIntSize {
  return BoardWidth;
}

auto LifeSimulation::Height() const noexcept -> NGIN::UIntSize {
  return BoardHeight;
}

auto LifeSimulation::Pixels() const noexcept
    -> const std::vector<NGIN::Byte> & {
  return m_simulation.GetResource<RenderSurface>().Pixels;
}

auto LifeSimulation::IsAlive(const NGIN::UIntSize x,
                             const NGIN::UIntSize y) const noexcept -> bool {
  return x < BoardWidth && y < BoardHeight &&
         m_simulation.Get<CellState>(m_cells[Index(x, y)]).Age != 0;
}

auto LifeSimulation::Generation() const noexcept -> NGIN::UInt64 {
  return m_generation;
}

auto LifeSimulation::Population() const noexcept -> NGIN::UIntSize {
  return m_simulation.GetResource<PopulationStats>().Living;
}

auto LifeSimulation::Rule() const noexcept -> LifeRule {
  return m_simulation.GetResource<SimulationRules>().Rule;
}

auto LifeSimulation::SystemCount() const noexcept -> NGIN::UIntSize {
  return m_simulation.Schedule(NGIN::ECS::FixedUpdate).SystemCount() +
         m_simulation.Schedule(NGIN::ECS::PostUpdate).SystemCount();
}

auto LifeSimulation::StageCount() const noexcept -> NGIN::UIntSize {
  return m_simulation.Schedule(NGIN::ECS::FixedUpdate).StageCount() +
         m_simulation.Schedule(NGIN::ECS::PostUpdate).StageCount();
}

auto LifeSimulation::LastFrameMicroseconds() const noexcept -> double {
  return m_lastFrameMicroseconds;
}

void LifeSimulation::StepGeneration() { StepGenerations(1); }

void LifeSimulation::StepGenerations(const NGIN::UIntSize count) {
  if (count == 0) {
    return;
  }
  const auto result = m_simulation.Step(
      NGIN::ECS::FrameInfo{.DeltaTime = static_cast<double>(count)});
  m_generation += result.FixedSteps;
  if (const auto *profile = m_simulation.LastFrameProfile();
      profile != nullptr) {
    m_lastFrameMicroseconds =
        static_cast<double>(profile->DurationNanoseconds) / 1'000.0;
  }
}

void LifeSimulation::SetCell(const NGIN::UIntSize x, const NGIN::UIntSize y,
                             const bool alive) {
  if (x >= BoardWidth || y >= BoardHeight) {
    return;
  }
  const auto index = Index(x, y);
  auto &state = m_simulation.GetMutable<CellState>(m_cells[index]);
  const auto wasAlive = state.Age != 0;
  if (wasAlive == alive) {
    return;
  }
  state.Age = alive ? NGIN::UInt16{1} : NGIN::UInt16{0};
  state.Trail = alive ? NGIN::UInt8{0} : TrailLifetime;
  m_simulation.GetResource<BoardSnapshot>().Cells[index] = alive ? 1U : 0U;
  auto &population = m_simulation.GetResource<PopulationStats>().Living;
  population = alive ? population + 1U : population - 1U;
  WritePixel(m_simulation.GetResource<RenderSurface>().Pixels, index, state.Age,
             state.Trail);
}

void LifeSimulation::SetRule(const LifeRule rule) noexcept {
  m_simulation.GetResource<SimulationRules>().Rule = rule;
}

void LifeSimulation::Clear() {
  ResetCells();
  m_generation = 0;
  m_lastFrameMicroseconds = 0.0;
  RefreshPresentation();
}

void LifeSimulation::Randomize() {
  std::bernoulli_distribution alive{0.11};
  for (NGIN::UIntSize index = 0; index < EntityCount; ++index) {
    auto &state = m_simulation.GetMutable<CellState>(m_cells[index]);
    state.Age = alive(m_random) ? NGIN::UInt16{1} : NGIN::UInt16{0};
    state.Trail = 0;
  }
  m_generation = 0;
  m_lastFrameMicroseconds = 0.0;
  RefreshPresentation();
}

void LifeSimulation::LoadGliderFleet() {
  ResetCells();
  for (NGIN::UIntSize y = 8; y + 3 < BoardHeight; y += 32) {
    for (NGIN::UIntSize x = 8; x + 3 < BoardWidth; x += 32) {
      for (const auto [offsetX, offsetY] : Glider) {
        SetSeedCell(x + static_cast<NGIN::UIntSize>(offsetX),
                    y + static_cast<NGIN::UIntSize>(offsetY));
      }
    }
  }
  m_generation = 0;
  m_lastFrameMicroseconds = 0.0;
  RefreshPresentation();
}

void LifeSimulation::LoadMethuselahField() {
  ResetCells();
  for (NGIN::UIntSize y = 24; y + 3 < BoardHeight; y += 64) {
    for (NGIN::UIntSize x = 24; x + 7 < BoardWidth; x += 64) {
      for (const auto [offsetX, offsetY] : Acorn) {
        SetSeedCell(x + static_cast<NGIN::UIntSize>(offsetX),
                    y + static_cast<NGIN::UIntSize>(offsetY));
      }
    }
  }
  m_generation = 0;
  m_lastFrameMicroseconds = 0.0;
  RefreshPresentation();
}

auto LifeSimulation::Index(const NGIN::UIntSize x,
                           const NGIN::UIntSize y) const noexcept
    -> NGIN::UIntSize {
  return y * BoardWidth + x;
}

void LifeSimulation::ConfigureSchedule() {
  using namespace NGIN::ECS;

  auto &fixed = m_simulation.Schedule(FixedUpdate);
  const auto evolve = fixed.Each(
      "Evolve one million cells",
      [](const CellPosition &position, const CellState &cell,
         NextCellState &next, Resource<const BoardSnapshot> board,
         Resource<const SimulationRules> rules) {
        const auto index = static_cast<NGIN::UIntSize>(position.Index);
        const auto x = index % BoardWidth;
        const auto y = index / BoardWidth;
        const auto left = x == 0 ? BoardWidth - 1 : x - 1;
        const auto right = x + 1 == BoardWidth ? 0 : x + 1;
        const auto above = (y == 0 ? BoardHeight - 1 : y - 1) * BoardWidth;
        const auto row = y * BoardWidth;
        const auto below = (y + 1 == BoardHeight ? 0 : y + 1) * BoardWidth;
        const auto neighbors = static_cast<NGIN::UInt8>(
            board->Cells[above + left] + board->Cells[above + x] +
            board->Cells[above + right] + board->Cells[row + left] +
            board->Cells[row + right] + board->Cells[below + left] +
            board->Cells[below + x] + board->Cells[below + right]);
        next.Alive = cell.Age != 0 ? neighbors == 2 || neighbors == 3
                                   : neighbors == 3 ||
                                         (rules->Rule == LifeRule::HighLife &&
                                          neighbors == 6);
      });

  const auto commit = fixed.Each(
      "Commit generation",
      [](const CellPosition &position, CellState &cell,
         const NextCellState &next, Resource<BoardSnapshot> board,
         Resource<PopulationStats> stats) {
        const auto wasAlive = cell.Age != 0;
        if (next.Alive) {
          cell.Age = static_cast<NGIN::UInt16>(
              std::min<NGIN::UInt32>(static_cast<NGIN::UInt32>(cell.Age) + 1U,
                                     std::numeric_limits<NGIN::UInt16>::max()));
          cell.Trail = 0;
        } else {
          cell.Age = 0;
          cell.Trail = wasAlive ? TrailLifetime
                                : static_cast<NGIN::UInt8>(
                                      cell.Trail == 0 ? 0 : cell.Trail - 1U);
        }
        if (wasAlive != next.Alive) {
          std::atomic_ref<NGIN::UIntSize> population{stats->Living};
          if (next.Alive) {
            population.fetch_add(1, std::memory_order_relaxed);
          } else {
            population.fetch_sub(1, std::memory_order_relaxed);
          }
        }
        board->Cells[position.Index] = next.Alive ? 1U : 0U;
      });

  fixed.Before(evolve, commit);
  fixed.SetIterationPolicy(evolve, IterationPolicy::ParallelChunks, 4096);
  fixed.SetIterationPolicy(commit, IterationPolicy::ParallelChunks, 4096);
  fixed.Build();

  auto &present = m_simulation.Schedule(PostUpdate);
  const auto rasterize = present.Each(
      "Rasterize dynamic surface",
      [](const CellPosition &position, const CellState &cell,
         Resource<RenderSurface> surface) {
        WritePixel(surface->Pixels, position.Index, cell.Age, cell.Trail);
      });
  present.SetIterationPolicy(rasterize, IterationPolicy::ParallelChunks, 4096);
  present.Build();
}

void LifeSimulation::RefreshPresentation() {
  auto &board = m_simulation.GetResource<BoardSnapshot>().Cells;
  auto &pixels = m_simulation.GetResource<RenderSurface>().Pixels;
  auto &population = m_simulation.GetResource<PopulationStats>().Living;
  population = 0;
  for (NGIN::UIntSize index = 0; index < EntityCount; ++index) {
    const auto &state = m_simulation.Get<CellState>(m_cells[index]);
    const auto alive = state.Age != 0;
    board[index] = alive ? 1U : 0U;
    population += alive ? 1U : 0U;
    WritePixel(pixels, index, state.Age, state.Trail);
  }
}

void LifeSimulation::ResetCells() {
  for (NGIN::UIntSize index = 0; index < EntityCount; ++index) {
    m_simulation.GetMutable<CellState>(m_cells[index]) = CellState{};
  }
}

void LifeSimulation::SetSeedCell(const NGIN::UIntSize x,
                                 const NGIN::UIntSize y) {
  m_simulation.GetMutable<CellState>(m_cells[Index(x, y)]).Age = 1;
}

} // namespace Hello::GameOfLife
