---
title: World and entities
description: Understand entity identity, plain components, archetype storage, and structural change.
---

# World and entities

`World` owns entity identity and component storage. An entity is a handle; its
current set of component types determines its archetype.

## Components are plain types

```cpp
struct Position { float X; float Y; };
struct Selected {};
```

Components do not inherit from an ECS base class. Keep them focused on state;
put iteration and behavior in systems.

## Spawning

Spawning an entity supplies its initial component set:

```cpp
const auto entity = simulation.Spawn(Position{10, 20}, Selected{});
```

## Structural changes

Adding or removing a component changes the entity's archetype and may move its
storage. Do not retain raw component references across a structural operation
unless the API explicitly guarantees their validity.

## Entity lifetime

An entity handle does not own the world. Validate handles at boundaries where
the entity may have been destroyed or recycled.

## Resources

Use simulation resources for state shared by systems but not naturally attached
to one entity, such as a frame-level settings object or global simulation map.
