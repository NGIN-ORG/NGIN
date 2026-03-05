# Spec 001: Module Dependency Graph (Core Module Map)

Status: Implemented v1 (umbrella-enforced)  
Owner: NGIN umbrella workspace (`NGIN`)  
Last updated: 2026-03-03

## Summary

This spec defines dependency and layering rules for NGIN across:

- component repositories (today)
- runtime modules/plugins/targets (future platform)
- packaging/build composition

It is the first detailed spec because later runtime kernel, plugin ABI, and editor designs depend on stable layering.

## Goals

- Prevent cyclic or upward dependencies
- Distinguish repo/module/plugin/target terminology
- Preserve standalone library usability (`NGIN.Base`, `NGIN.Log`, `NGIN.Reflection`, `NGIN.ECS`)
- Support a future binary plugin boundary and editor/runtime separation

## Non-Goals

- Full plugin ABI function definitions (Spec 003)
- Runtime kernel service semantics (Spec 002)
- Editor UI layout and tool host implementation (Spec 004)

## Terminology (Normative)

- **Repository**: a Git repo (example: `NGIN.Reflection`)
- **Component**: a published repo/library unit tracked in the platform manifest
- **Module**: smallest compilation/deployment unit inside the platform/module system (`*.module.json`)
- **Plugin**: optional bundle of modules/assets/config/resources
- **Target**: final build product (app/editor/server/tool)

## Graphs (Three Distinct Graphs)

## 1. Component Repository Graph (Current / Near-Term)

This graph governs dependencies between standalone `NGIN.*` repositories.

### Current observed component graph (workspace snapshot)

- `NGIN.Base` -> (none)
- `NGIN.Log` -> `NGIN.Base`
- `NGIN.Reflection` -> `NGIN.Base`
- `NGIN.ECS` -> `NGIN.Base`
- `NGIN.Runtime` -> `NGIN.Base`, `NGIN.Log`
- `NGIN.Benchmark` -> `NGIN.Base` (expected/tooling role; manifest marks optional)

### Rules (Component Graph)

1. `NGIN.Base` is foundational and must not depend on higher-level NGIN repos.
2. `NGIN.Log` and `NGIN.Reflection` may depend on `NGIN.Base`; higher-level repos may optionally depend on them.
3. Domain libraries (for example `NGIN.ECS`) may depend on `NGIN.Base`, and may optionally integrate with `NGIN.Reflection` via adapter modules, not mandatory core dependency by default.
4. Experimental repos must be marked `required: false` in the platform manifest until release-grade.

## 2. Runtime Module Graph (Platform Modules, Future)

This graph governs in-platform loadable/static modules and their layering.

### Proposed Layer Stack (Top to Bottom)

1. Application modules
2. Domain engine modules (`Game`, `Sim`, `CAD`, `Viz`, etc.)
3. Editor modules (tool host, panels, inspectors) [editor targets only]
4. Platform service modules (runtime kernel services, asset/data, render abstractions)
5. Platform abstraction modules (`Platform.Win64`, `Platform.Linux`, ...)
6. Foundation modules (`NGIN.Base`, `NGIN.Log`, optional `NGIN.Reflection` support layer, low-level utils)

### Allowed Dependency Direction

Dependencies may only point downward within the stack, plus same-layer dependencies when explicitly allowed by that layer's rules.

### Forbidden Dependency Examples

- Foundation -> Editor
- Platform abstraction -> Application
- Runtime service module -> Domain application module
- Generic asset/data module -> game-specific module

## 3. Build/Packaging Graph

This graph maps modules/plugins into build targets and packaged outputs.

### Rules

1. A target may include only modules valid for its target type (`Runtime`, `Editor`, `Program`, `Developer`).
2. Plugin bundles may introduce optional modules but cannot violate runtime module graph rules.
3. Packaging stages (`Build`, `Cook`, `Stage`, `Package`) must consume dependency metadata rather than hardcoded project-specific rules where possible.

## Naming Rules (Normative)

### Repository Names

- Pattern: `NGIN.<Component>`
- Examples: `NGIN.Base`, `NGIN.Reflection`

### Runtime Module Names

- Pattern: `<Family>.<Name>`
- Examples: `Platform.Win64`, `Runtime.Kernel`, `Asset.Database`, `Editor.Inspector`

### Plugin Names

- Pattern: `<Vendor>.<PluginName>` or `<Org>.<PluginName>`
- Examples: `NGIN.VisualScripting`, `Acme.SimExporter`

### Target Names

- Examples: `NGIN.Editor`, `MyApplication`, `SimulationServer`

## Reflection Optionality Policy

### Runtime Applications

Reflection is optional by default for runtime-only applications and servers.

### Required/Preferred Cases

Reflection is required or strongly preferred for:

- editor tooling
- visual scripting
- property inspection
- dependency injection / IoC metadata
- cross-module metadata export/import workflows

### Integration Pattern

Modules that can operate with or without reflection should isolate reflection usage behind adapter/service interfaces to avoid making `NGIN.Reflection` a hard dependency for every runtime target.

## Editor / Runtime Separation Rules

1. Editor-only modules must not be required by runtime targets.
2. Runtime modules may expose metadata/services that editors consume, but not the reverse.
3. Shared tool/runtime code must live in neutral modules (e.g. `Asset.Core`, `Config.Core`) rather than `Editor.*`.

## Binary Plugin Boundary Placement (Pre-ABI Rules)

1. Binary plugin host interface crosses a **C ABI boundary**.
2. The ABI boundary sits at the runtime kernel/plugin loader layer, not deep within arbitrary internal modules.
3. Plugin compatibility checks must include:
   - platform version/range
   - plugin ABI version
   - module compatibility metadata
4. Reflection metadata import/export is optional and must not be the only means of plugin compatibility.

## Current Repo Mapping to Platform Vision

### `NGIN.Base`

- Role: Foundation
- Future position: foundational system library consumed by almost all platform/domain layers

### `NGIN.Log`

- Role: Foundation logging subsystem
- Future position: structured, sink-based logging library consumed by runtime/platform/domain components

### `NGIN.Reflection`

- Role: Foundation (optional runtime, required for many tools)
- Future position: runtime type system + metadata substrate for editor/IoC/plugin discovery workflows

### `NGIN.ECS`

- Role: Domain / reusable domain engine subsystem
- Future position: reusable data-oriented subsystem used by domain engines or apps; not required for the whole platform

### `NGIN.Runtime`

- Current role: runtime kernel component and platform service host
- Future position: stable home for `Runtime.*`/`Platform.*` kernel service modules

## Allowed Dependency Matrix (Initial)

Legend: `Y` allowed, `O` optional/conditional, `N` forbidden

| From \\ To        | Base | Reflection | RuntimeSvc | Platform.* | Editor.* | Domain.* | App.* |
|-------------------|------|------------|------------|------------|----------|----------|-------|
| Base              | N    | N          | N          | N          | N        | N        | N     |
| Reflection        | Y    | N          | N          | N          | N        | N        | N     |
| RuntimeSvc        | Y    | O          | Y          | Y          | N        | N        | N     |
| Platform.*        | Y    | O          | N          | Y          | N        | N        | N     |
| Editor.*          | Y    | O          | Y          | Y          | Y        | O        | N     |
| Domain.*          | Y    | O          | Y          | Y          | N        | Y        | N     |
| App.*             | Y    | O          | Y          | Y          | N        | Y        | Y     |

Notes:

- `Base -> Base` is shown `N` in this matrix because the table is at family granularity, not per-module intra-family linking.
- `Editor.* -> Domain.*` is optional and should occur via extension/plugin contracts, not hardcoded assumptions where possible.

## Descriptor Requirements (Implemented v1)

Module metadata is now enforced via `manifests/module-catalog.json`.
Each module entry defines at minimum:

- `name`
- `family` (`Base`, `Reflection`, `RuntimeSvc`, `Platform`, `Editor`, `Domain`, `App`)
- `type` (`Runtime`, `Editor`, `Developer`, `Program`, `ThirdParty`)
- `component` (`NGIN.*`)
- `dependencies` (hard + optional)
- `platforms`
- `version`
- `compatiblePlatformRange`
- `loadPhase`
- `flags.editorOnly`
- `flags.requiresReflection`

Rule mapping note:

- `Developer` and `ThirdParty` module types use `App.*` dependency permissions for matrix validation.

## Validation & Enforcement (Implemented)

The NGIN workspace tooling/CI now enforces:

1. Component manifest dependency consistency
2. JSON Schema validation for module/plugin/target catalogs (draft 2020-12)
3. Canonical runtime load phases (`Bootstrap`, `Platform`, `CoreServices`, `Data`, `Domain`, `Application`, `Editor`)
4. Semver/version-range semantic checks across manifest catalogs
5. Runtime module graph acyclicity
6. Forbidden layer dependencies
7. Target composition validity (editor/runtime/program)
8. Plugin bundle manifest compatibility before packaging
9. Reproducibility policy: on non-`dev` channels, every `required: true` component must have a non-null `ref`

Enforcement surface:

- `python tools/ngin-sync.py validate-spec001`
- `python tools/ngin-sync.py resolve-target --target <TargetName>`
- `.github/workflows/workspace-ci.yml` runs both commands as hard gates
- Root CMake targets:
  - `ngin.spec001.validate`
  - `ngin.spec001.resolve.runtime`

## Open Questions (Tracked, Not Blocking This Spec)

1. Whether render/GPU abstractions live in a dedicated component repo or inside the umbrella repo initially.
2. Degree of cross-component semantic version strictness vs commit pinning during early platform releases.
