# Component Requirements

Most ordinary C++ value types work as components. They need no base class,
macro, reflection metadata, or default constructor.

```cpp
struct Name
{
    explicit Name(std::string value) : Value(std::move(value)) {}
    std::string Value;
};
```

## The Short Version

A component must be:

- an object type;
- destructible without throwing;
- move-constructible or copy-constructible; and
- safely relocatable by a bitwise move, a `noexcept` move, or a `noexcept` copy.

Move-only components should therefore have a non-throwing move constructor:

```cpp
struct MoveOnly
{
    MoveOnly(MoveOnly&&) noexcept = default;
    MoveOnly(const MoveOnly&) = delete;
    ~MoveOnly() noexcept = default;
};
```

NGIN.ECS prefers copying when moving could throw. Unsupported types fail at
compile time when their component metadata is first used.

## Tags

An empty class is stored as a tag:

```cpp
struct Player {};
struct Sleeping {};
```

Tags have presence and change versions but no data column. Filter with
`With<Player>` or `Without<Sleeping>`. Tags may be read for presence, but they
cannot be queried or acquired for mutable data.

## Type Identity

The unqualified type is the component identity. Query declarations use that
plain spelling—`Read<Position>`, not `Read<const Position>`.

Component IDs are process-local keys. They are collision-checked inside a
world, but they are not stable serialization, networking, or save-game IDs.

## Advanced Relocation

Specializing NGIN's bitwise-relocation trait promises that a raw relocation
creates a valid destination and abandons the source without a second
destruction. Only make that promise for a type whose invariants you control;
an incorrect specialization is undefined behavior.
