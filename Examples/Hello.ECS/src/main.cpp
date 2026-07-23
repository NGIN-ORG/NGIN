#include <NGIN/ECS/ECS.hpp>

struct Position
{
    float X {0.0f};
};

struct Velocity
{
    float X {0.0f};
};

int main()
{
    NGIN::ECS::World world;
    const auto entity = world.Spawn(Position {}, Velocity {2.0f});

    NGIN::ECS::Scheduler scheduler;
    (void)scheduler.AddSystem(
        "Move",
        [](NGIN::ECS::Query<
            NGIN::ECS::Write<Position>,
            NGIN::ECS::Read<Velocity>>& query) {
            query.ForEach([](auto row) {
                row.template Get<Position>().X +=
                    row.template Get<Velocity>().X;
            });
        });

    scheduler.Run(world);
    return world.Get<Position>(entity).X == 2.0f ? 0 : 1;
}
