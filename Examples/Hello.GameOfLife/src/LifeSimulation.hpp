#pragma once

#include <NGIN/ECS/ECS.hpp>

#include <cstdint>
#include <random>
#include <vector>

namespace Hello::GameOfLife {

enum class LifeRule {
  Conway,
  HighLife,
};

class LifeSimulation final {
public:
  static constexpr NGIN::UIntSize BoardWidth = 1024;
  static constexpr NGIN::UIntSize BoardHeight = 1024;
  static constexpr NGIN::UIntSize EntityCount = BoardWidth * BoardHeight;

  LifeSimulation();

  [[nodiscard]] auto Width() const noexcept -> NGIN::UIntSize;
  [[nodiscard]] auto Height() const noexcept -> NGIN::UIntSize;
  [[nodiscard]] auto Pixels() const noexcept -> const std::vector<NGIN::Byte> &;
  [[nodiscard]] auto IsAlive(NGIN::UIntSize x, NGIN::UIntSize y) const noexcept
      -> bool;
  [[nodiscard]] auto Generation() const noexcept -> NGIN::UInt64;
  [[nodiscard]] auto Population() const noexcept -> NGIN::UIntSize;
  [[nodiscard]] auto Rule() const noexcept -> LifeRule;
  [[nodiscard]] auto SystemCount() const noexcept -> NGIN::UIntSize;
  [[nodiscard]] auto StageCount() const noexcept -> NGIN::UIntSize;
  [[nodiscard]] auto LastFrameMicroseconds() const noexcept -> double;

  void StepGeneration();
  void StepGenerations(NGIN::UIntSize count);
  void SetCell(NGIN::UIntSize x, NGIN::UIntSize y, bool alive);
  void SetRule(LifeRule rule) noexcept;
  void Clear();
  void Randomize();
  void LoadGliderFleet();
  void LoadMethuselahField();

private:
  struct CellPosition final {
    NGIN::UInt32 Index{0};
  };

  struct CellState final {
    NGIN::UInt16 Age{0};
    NGIN::UInt8 Trail{0};
  };

  struct NextCellState final {
    bool Alive{false};
  };

  struct BoardSnapshot final {
    std::vector<NGIN::UInt8> Cells;
  };

  struct RenderSurface final {
    std::vector<NGIN::Byte> Pixels;
  };

  struct PopulationStats final {
    NGIN::UIntSize Living{0};
  };

  struct SimulationRules final {
    LifeRule Rule{LifeRule::Conway};
  };

  [[nodiscard]] auto Index(NGIN::UIntSize x, NGIN::UIntSize y) const noexcept
      -> NGIN::UIntSize;
  void ConfigureSchedule();
  void RefreshPresentation();
  void ResetCells();
  void SetSeedCell(NGIN::UIntSize x, NGIN::UIntSize y);

  NGIN::ECS::Simulation m_simulation;
  NGIN::Containers::Vector<NGIN::ECS::EntityId> m_cells;
  std::mt19937 m_random{0x4E47494EU};
  NGIN::UInt64 m_generation{0};
  double m_lastFrameMicroseconds{0.0};
};

} // namespace Hello::GameOfLife
