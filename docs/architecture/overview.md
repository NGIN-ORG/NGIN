# Architecture overview

NGIN has two independent layers: a project system and optional application
libraries.

```text
authored manifests
       |
       v
Composition Graph
       |
       +--> generated CMake --> compiler
       +--> staging and launch
       +--> tools and editors
       `--> tests and publishing

optional libraries: Base, Log, Core, Reflection, ECS, UI
```

## Project system

`.nginproj`, `.nginpkg`, and optional `.ngin` workspace manifests describe
intent. The CLI validates and resolves them into one Composition Graph. Backend
generation and every downstream tool consume that graph.

CMake is the current generated backend. It remains a native build system, but
it is not the normal application authoring surface.

## Packages and source ownership

`Packages/` contains NGIN package wrappers and locally owned runtime packages.
`Dependencies/NGIN/` contains first-party source trees. A wrapper describes how
the project system exposes or builds a source tree; it does not transfer source
ownership into the wrapper.

## Runtime boundary

The CLI and manifests are build-time tools. A plain executable needs no NGIN
runtime. `NGIN.Core` is an optional host for applications that want modules,
services, configuration, and managed lifecycle behavior.

## Generated output

Build backends, stage directories, launch manifests, compile databases, and
generator output live under build output. They can be inspected but are never
the source of truth.

See the [architecture decisions](decisions/README.md) for the small set of
choices that need durable rationale.
