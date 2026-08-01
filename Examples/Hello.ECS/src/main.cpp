#include <NGIN/ECS/ECS.hpp>

#include <iomanip>
#include <iostream>
#include <string_view>

namespace
{
    constexpr int FinishLine = 20;

    // Components are plain data. An entity becomes a racer by having these
    // components attached to it.
    struct Position
    {
        int X{0};
    };

    struct Velocity
    {
        int X{0};
    };

    struct Acceleration
    {
        int X{0};
    };

    struct Racer
    {
        std::string_view Name;
        char Symbol{'?'};
    };

    std::string_view DrawRace(NGIN::ECS::World &world, int tick)
    {
        std::cout << "\nTick " << tick << '\n';

        std::string_view winner;
        NGIN::ECS::Query<
            NGIN::ECS::Read<Position>,
            NGIN::ECS::Read<Racer>>
            racers{world};

        racers.ForEach(
            [&](auto row)
            {
                const auto &position = row.template Get<Position>();
                const auto &racer = row.template Get<Racer>();
                const int marker =
                    position.X < FinishLine ? position.X : FinishLine;

                if (winner.empty() && position.X >= FinishLine)
                {
                    winner = racer.Name;
                }

                std::cout << "  " << std::left << std::setw(18) << racer.Name
                          << " |";
                for (int cell = 0; cell <= FinishLine; ++cell)
                {
                    std::cout << (cell == marker ? racer.Symbol : '.');
                }
                std::cout << "|  x = " << position.X << '\n';
            });

        return winner;
    }
}

int main()
{
    using namespace NGIN::ECS;

    Simulation simulation {
        SimulationConfig {.FixedDeltaTime = 0.0}};

    // Each racer is an entity assembled from the same reusable components.
    (void)simulation.Spawn(
        Position{}, Velocity{0}, Acceleration{1}, Racer{"Turbo Tortoise", 'T'});
    (void)simulation.Spawn(
        Position{}, Velocity{3}, Acceleration{0}, Racer{"Hasty Hare", 'H'});
    (void)simulation.Spawn(
        Position{}, Velocity{2}, Acceleration{0}, Racer{"Clockwork Crab", 'C'});

    auto &update = simulation.Schedule(Update);

    const auto accelerate = update.Each(
        "Accelerate",
        [](Velocity &velocity, const Acceleration &acceleration)
        {
            velocity.X += acceleration.X;
        });

    const auto move = update.Each(
        "Move",
        [](Position &position, const Velocity &velocity)
        {
            position.X += velocity.X;
        });

    update.After(move, accelerate);

    std::cout << "=== NGIN.ECS GRAND PRIX ===\n"
              << "Entities race using Position, Velocity, and Acceleration.\n"
              << "The Accelerate and Move systems update every racer.\n";

    (void)DrawRace(simulation.GetWorld(), 0);

    for (int tick = 1; tick <= 10; ++tick)
    {
        (void)simulation.Step(FrameInfo {.DeltaTime = 1.0});
        const auto winner = DrawRace(simulation.GetWorld(), tick);
        if (!winner.empty())
        {
            std::cout << "\nWinner: " << winner << "!\n";
            return 0;
        }
    }

    std::cout << "\nThe race ended without a winner.\n";
    return 1;
}
