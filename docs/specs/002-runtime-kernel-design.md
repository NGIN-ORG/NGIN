# Spec 002: Runtime Kernel Detailed Design

Status: Draft v1 (foundation-aligned)  
Owner: NGIN umbrella workspace (`NGIN`)  
Last updated: 2026-02-26

Depends on:

- `001-module-dependency-graph.md`

Related:

- `003-plugin-abi-header-spec.md` (binary ABI details)
- `004-editor-architecture.md` (editor host built on kernel)

## Summary

The NGIN Runtime Kernel is the process-level host that turns a set of modules/plugins into a running application, tool, or domain engine.

It provides:

- module discovery, dependency resolution, and lifecycle orchestration
- service registry / IoC integration point
- event/message infrastructure
- task scheduling contract
- configuration layering with runtime overrides

This spec defines **kernel behavior and contracts**, not the exact binary plugin ABI function table (that is Spec 003).

## Goals

- Provide a domain-agnostic runtime substrate for applications, tools, and domain engines
- Support both static-linked and dynamically loaded modules
- Keep reflection optional for runtime-only targets, while enabling richer tooling when present
- Enforce compatibility/version checks before module activation
- Make startup/shutdown deterministic and observable
- Support binary distribution and source-free plugin loading (via future ABI spec)

## Non-Goals

- Editor UI/tool host behavior (Spec 004)
- Full BuildTool pipeline behavior (future build specs)
- Render graph/device APIs (separate rendering specs)
- Full hot-reload implementation (only defines extension points/constraints here)

## Terminology (Normative)

- **Kernel**: the runtime host and orchestration layer for modules/services/tasks/config/events
- **Host**: the executable process instantiating the kernel (app/editor/server/program)
- **Module**: a unit of functionality with descriptor metadata and lifecycle hooks
- **Plugin**: an optional bundle that contributes modules and resources
- **Service**: a kernel-registered capability queried by modules (typed and/or reflection-backed)
- **Activation**: the transition where a resolved module becomes initialized and starts running

## Architectural Position

Per `Spec 001`, runtime kernel functionality lives in the **Platform Service** layer and may depend on:

- `NGIN.Base` (required)
- `NGIN.Reflection` (optional integration path)
- platform abstraction modules (for dynamic library/process/filesystem support)

It must not depend on:

- editor modules
- domain/application modules

## Kernel Subsystems (Normative)

The runtime kernel is composed of the following subsystem families.

### 1. Module Loader

Responsibilities:

- discover module descriptors from static registration, plugins, and target manifests
- resolve dependency graph and load order
- enforce version/platform compatibility
- create module instances and drive lifecycle transitions
- coordinate unload/shutdown order

### 2. Service Host / IoC Bridge

Responsibilities:

- register/query process services
- manage service lifetime and ownership policies
- expose optional reflection-backed resolution/constructor injection integration
- provide a stable non-reflection fallback service API

### 3. Event & Messaging System

Responsibilities:

- synchronous publish/subscribe (in-process)
- queued dispatch across threads/phases
- typed events and named channels
- scoped subscriptions and deterministic dispatch ordering rules

### 4. Task Scheduler Contract

Responsibilities:

- define execution lanes/queues used by kernel and modules
- expose async execution API shape to modules
- integrate with `NGIN.Base` execution/async primitives
- coordinate phase barriers (important for deterministic systems/tooling)

### 5. Configuration System

Responsibilities:

- hierarchical config sources and override precedence
- type-safe reads with string/raw access fallback
- layered config by target/environment/plugin/module
- change notifications and snapshot semantics

## Reference Module Family Layout (Initial)

The kernel may be implemented as one repo or several repos/modules, but the logical split is:

- `Runtime.Kernel` (bootstrapping + orchestration)
- `Runtime.ModuleLoader`
- `Runtime.Services`
- `Runtime.Events`
- `Runtime.Tasks`
- `Runtime.Config`
- `Runtime.ReflectionBridge` (optional adapter module)

Notes:

- `NGIN.Core` is a candidate seed for these capabilities but is not assumed to remain the final public name.
- Kernel implementations may collapse some families initially, but the API boundaries should follow this split.

## Runtime Modes / Host Types

The kernel supports multiple host modes without domain assumptions.

### Supported host categories (initial)

- `RuntimeApp` (shipping runtime application)
- `EditorHost` (tool host / editor process)
- `Server` (headless service or simulation server)
- `Program` (developer/program utility)

Mode affects defaults, not core architecture:

- reflection required by default in `EditorHost`
- reflection optional in `RuntimeApp` / `Server`
- dynamic plugin loading may be disabled in some shipping profiles

## Public Kernel Interfaces (Conceptual, Normative)

This section defines the public interface shapes that later C++ APIs must implement. Names may change, but semantics should remain.

### 1. `KernelHostConfig`

Boot-time host configuration (immutable after kernel start except noted override channels).

Required fields:

- `hostName`
- `hostType` (`RuntimeApp`, `EditorHost`, `Server`, `Program`)
- `platformName`
- `targetName`
- `workingDirectory`
- `configSources[]`
- `pluginSearchPaths[]`
- `enableDynamicPlugins`
- `enableReflection`

Optional fields:

- `commandLineArgs[]`
- `environmentName` (e.g. `Dev`, `QA`, `Prod`)
- `logSink configuration`
- `scheduler policy`

### 2. `Kernel`

Process host object. Single active kernel instance per process is the default supported mode in v1.

Core operations:

- `Create(KernelHostConfig)`
- `Start()`
- `Run()` or `Tick(frameArgs)` depending on host mode
- `RequestStop(reason)`
- `Shutdown()`

State transitions are strict and observable (see lifecycle state machine).

### 3. `ModuleDescriptor`

Kernel-consumed metadata for module resolution.

Required fields (kernel-facing view):

- `name`
- `type`
- `version`
- `compatiblePlatformRange`
- `platforms[]`
- `dependencies[]` (hard/optional)
- `loadPhase`
- `entryKind` (`Static`, `Dynamic`)

Optional fields:

- `pluginName`
- `providesServices[]`
- `requiresServices[]`
- `reflectionRequired`
- `capabilities[]`

Note:

- This overlaps with `Spec 001` descriptor requirements. The kernel consumes a normalized runtime view regardless of source format (`*.module.json`, static registration tables, plugin manifests).

### 4. `ServiceRegistry`

Dual-path service access:

- **Typed path (required):** compile-time service keys/interfaces
- **Reflection path (optional):** metadata-assisted discovery/injection when `NGIN.Reflection` is enabled

Minimum operations:

- register singleton/service instance
- register factory
- query optional/required service
- enumerate services by contract/key
- scoped lifetime registration (kernel/module/plugin scope)

### 5. `EventBus`

Minimum operations:

- subscribe/unsubscribe
- publish immediate
- enqueue deferred
- flush channel/phase queue

Must support scoped subscriptions tied to module/plugin unload.

### 6. `TaskRuntime`

Minimum operations:

- submit task to lane/queue
- schedule continuation
- wait/join or future handle
- barrier/synchronization point
- timer/delayed task hook (can delegate to platform services)

### 7. `ConfigStore`

Minimum operations:

- get value (typed / raw string/object)
- set override (mutable override layers only)
- snapshot
- enumerate subtree
- subscribe to change notifications

## Kernel Lifecycle State Machine (Normative)

Kernel state transitions:

1. `Created`
2. `ConfigLoaded`
3. `ModulesResolved`
4. `ServicesBuilt`
5. `ModulesLoaded`
6. `Running`
7. `Stopping`
8. `Stopped`
9. `Shutdown`

### Transition rules

- `Create()` enters `Created`
- Config sources are resolved and merged before module graph resolution
- Module dependency resolution must complete successfully before any module activation
- Service registrations required for module construction must exist before dependent module activation
- `Running` is entered only after all required modules in active load phases are initialized
- Shutdown/unload order is reverse topological dependency order

### Failure behavior during transitions

- A failure before `Running` aborts startup and transitions to `Stopped` then `Shutdown`
- A failure in `Running` triggers host policy:
  - `FailFast`
  - `StopKernel`
  - `IsolateModule` (future/optional)

## Module Lifecycle (Normative)

Each module follows a staged lifecycle to keep startup deterministic and plugin unloading tractable.

### Module states

1. `Discovered`
2. `Resolved`
3. `Loaded` (binary/image loaded if dynamic; static entry available if static)
4. `Constructed`
5. `Initialized`
6. `Running`
7. `Stopping`
8. `Uninitialized`
9. `Unloaded`

### Lifecycle callbacks (conceptual)

The exact ABI/signatures are deferred, but the kernel must support callbacks equivalent to:

- `OnRegister(ModuleContext&)`
  - register services, event subscriptions, config schemas/defaults
  - no long-running work
- `OnInit(ModuleContext&)`
  - allocate resources, resolve required services
  - may fail with structured error
- `OnStart(ModuleContext&)`
  - begin active processing / schedule tasks / open endpoints
- `OnStop(ModuleContext&)`
  - quiesce work, unsubscribe, stop accepting new requests
- `OnShutdown(ModuleContext&)`
  - release resources and unregister transient state

Rules:

1. Dependency modules must reach `Initialized` before dependent `OnInit`.
2. `OnStart` ordering follows load phase and dependency order.
3. `OnStop`/`OnShutdown` ordering is reverse dependency order.
4. Modules must not assume reflection is enabled unless their descriptor requires it.

## Load Phases (Initial Set)

The kernel defines coarse load phases to separate foundational startup from domain/editor features.

Initial phases:

- `Bootstrap`
- `Platform`
- `CoreServices`
- `Data`
- `Domain`
- `Application`
- `Editor` (editor hosts only)

Rules:

- dependencies may be same-phase or earlier-phase only
- `Editor` phase is invalid for runtime-only targets
- phase definitions are stable kernel semantics; modules choose membership via descriptors

## Module Resolution & Dependency Graph

## Inputs

The kernel resolves modules from:

- static linked module registrars
- plugin manifests/bundles (if enabled)
- host target manifest / command line requested modules

## Resolution algorithm (v1)

1. Collect candidate descriptors.
2. Filter by platform/host type.
3. Merge duplicates by identity/version policy.
4. Validate descriptor schema/required fields.
5. Build directed dependency graph.
6. Validate:
   - no cycles
   - required dependencies present
   - phase ordering valid
   - layer constraints (from Spec 001)
7. Compute topological order with stable tie-breakers.
8. Partition by phase.

### Stable tie-breakers (determinism)

When multiple valid orders exist, sort by:

1. `loadPhase` ordinal
2. explicit priority (optional, default `0`)
3. module name (lexicographic)

## Static vs Dynamic Modules

The kernel supports two module entry kinds:

### Static modules

- Compiled into the host executable or linked libraries
- Registered via static/module registrar tables at startup
- Preferred for minimal-runtime deployments and core platform services

### Dynamic modules

- Loaded from plugin binaries via platform abstraction and plugin loader
- Must pass compatibility checks before activation
- Binary ABI boundary specified in Spec 003

Kernel behavior is identical after descriptor normalization and successful load.

## Compatibility & Version Enforcement (Normative)

The kernel must reject incompatible modules before activation.

### Checks required in v1

- platform support (`platforms[]`)
- compatible platform version range (`compatiblePlatformRange`)
- module dependency versions/ranges
- host mode compatibility (`Editor` vs runtime-only)
- plugin ABI version (dynamic modules only; exact mechanism in Spec 003)
- reflection requirement vs host reflection setting

### Policy defaults

- Required dependency incompatibility: **startup failure**
- Optional dependency incompatibility: **module continues without optional feature**, with warning
- Duplicate module name with incompatible version: **startup failure** unless host explicitly selects one

## Service Host / IoC Design

## Design principle

The kernel must work without reflection. Reflection enhances service discovery and injection but is not the baseline requirement.

### Service registration model (v1)

Services are registered with:

- contract key (typed key or stable identifier)
- lifetime (`Kernel`, `Plugin`, `Module`)
- ownership policy (kernel-owned instance / external instance / factory)
- optional metadata (capabilities, tags)

### Resolution model (v1)

- direct typed lookup (required path)
- optional lookup returning null/expected
- named/qualified lookup (optional)
- reflection-assisted constructor injection (only when enabled)

### Reflection integration (`Runtime.ReflectionBridge`)

When `NGIN.Reflection` is enabled:

- modules may expose reflected service types and metadata
- the kernel may resolve constructor dependencies via metadata
- editor/tooling can inspect registered services and capabilities

When disabled:

- typed service registry remains fully functional
- modules requiring reflection are rejected during compatibility checks

## Event & Messaging Design

The kernel event system provides two complementary channels:

### 1. Immediate events (synchronous)

Use cases:

- lifecycle notifications
- local state transitions
- in-thread orchestration where deterministic ordering is required

Rules:

- dispatch occurs on caller thread
- subscriber invocation order is deterministic (registration order within priority buckets)
- reentrancy is allowed only where documented per channel; default is allowed but discouraged for high-level channels

### 2. Deferred events/messages (queued)

Use cases:

- cross-thread handoff
- decoupled module communication
- frame/tick phase delivery

Rules:

- enqueued messages are delivered at defined flush points
- delivery order is deterministic per queue (FIFO)
- queue ownership/lane is explicit

### Subscription lifetime

Subscriptions are bound to one of:

- kernel scope
- plugin scope
- module scope
- explicit token scope

Kernel must auto-unsubscribe module/plugin scoped subscriptions during unload.

### Event contracts

The kernel itself defines a small set of reserved events (examples):

- `KernelStarting`
- `KernelRunning`
- `KernelStopping`
- `ModuleLoaded`
- `ModuleStarted`
- `ModuleFailed`
- `ConfigChanged`

## Task Scheduler Contract

The kernel does not need to own all scheduling implementation details, but it must define a stable contract consumed by modules.

### Backing implementation

The default implementation should build on `NGIN.Base` execution/async facilities where available:

- thread primitives
- schedulers / executors
- async tasks/futures/cancellation
- timing utilities

### Execution lanes (initial concept)

- `Main` (host thread / deterministic coordination)
- `IO`
- `Worker`
- `Background`
- `Render` (optional, only when host/render platform defines it)

### Contract requirements

1. Modules can submit work to named lanes.
2. Modules can await completion or chain continuations.
3. Cancellation is propagated where supported.
4. The kernel can define barrier points for deterministic phases.
5. Long-running module startup work must not block the main lane indefinitely without progress reporting.

### Tick/frame integration

For hosts that run a loop (`RuntimeApp`, `EditorHost`):

- kernel exposes a per-tick entry (`Tick`)
- deferred event queues may flush at start/end of tick phases
- modules may register tick handlers with explicit phase order

For headless/program modes:

- `Run()` may block on event loop/scheduler until stop requested

## Configuration System Design

The kernel configuration system provides hierarchical, layered settings with deterministic precedence.

### Source types (initial)

- built-in defaults
- host target config
- plugin config
- environment config (`Dev`, `QA`, `Prod`)
- machine/user local overrides
- command line overrides
- runtime mutable overrides (optional by host policy)

### Precedence (highest wins)

1. Runtime mutable overrides
2. Command line overrides
3. User/machine local overrides
4. Environment config
5. Plugin/module config overlays
6. Host target config
7. Built-in defaults

### Requirements

- immutable snapshot available to modules during startup phases
- change notifications for mutable layers after startup
- source provenance tracking (which layer provided the winning value)
- typed conversion with error reporting

### Configuration namespaces

Recommended namespace layout:

- `Kernel.*`
- `Platform.*`
- `Module.<ModuleName>.*`
- `Plugin.<PluginName>.*`
- `App.*`

## Error Model & Observability

The kernel must be diagnosable in startup failure scenarios and deterministic in emitted errors.

### Error handling

- public kernel operations return structured errors / status codes (prefer `expected`-style APIs)
- startup errors must include:
  - failing subsystem
  - module/plugin (if applicable)
  - error code
  - human-readable message
  - dependency path / context when relevant

### Logging

Kernel logs should include categories at minimum:

- `Kernel`
- `ModuleLoader`
- `Services`
- `Events`
- `Tasks`
- `Config`
- `Plugin`

Planned implementation path: use `NGIN.Log` as the default foundation logging component once stabilized.

### Diagnostics surfaces (v1)

- startup report summary (resolved modules, skipped optional modules, warnings)
- runtime module state query
- registered service query (typed/basic metadata)

## Host Integration Model

The executable host should remain thin.

### Host responsibilities

- construct `KernelHostConfig`
- provide platform-specific bootstrap services (filesystem/dylib/process abstractions)
- call `Create`/`Start`/`Run` or `Tick`
- handle process shutdown signals and map them to `RequestStop`

### Kernel responsibilities

- everything after configuration normalization and startup orchestration

## Security / Safety Considerations (Initial)

- dynamic plugin loading is opt-in per host config
- plugin search paths must be explicit (no uncontrolled recursive scanning by default)
- module/plugin descriptor parsing must be fail-closed on invalid required fields
- unload is only permitted when module/plugin declares unload-safe behavior (full hot reload remains future work)

## v1 Constraints / Deferred Work

- Single-kernel-per-process assumption
- No guaranteed hot-reload support (only lifecycle shape prepared)
- No plugin sandboxing (tracked future area)
- No distributed kernel/service federation
- Reflection-backed DI is optional and may initially support only constructor injection for simple cases

## Acceptance Scenarios (Testable)

## Startup & Resolution

1. Host starts with static modules only; kernel reaches `Running`.
2. Missing required dependency causes startup failure before any module `OnStart`.
3. Optional dependency missing logs warning and module starts with degraded feature set.
4. Platform-incompatible module is skipped/rejected before load.

## Lifecycle & Shutdown

1. Modules initialize and start in dependency order.
2. Stop/shutdown callbacks execute in reverse dependency order.
3. Module-scoped event subscriptions are removed automatically during unload.
4. `RequestStop` during run loop transitions kernel to `Stopping` and then `Shutdown`.

## Services / Reflection

1. Runtime host with `enableReflection=false` can start modules using typed services only.
2. Module marked `reflectionRequired=true` fails compatibility on a non-reflection host.
3. Editor host with reflection enabled can enumerate reflected services/types (implementation details deferred).

## Events / Tasks / Config

1. Deferred events enqueued during tick N are delivered at the documented flush point for tick N or N+1 (per queue policy).
2. Task submission to `Worker` lane executes without blocking `Main` lane progress.
3. Config override precedence yields deterministic winning values with provenance.
4. Config change notifications include namespace/key and effective new value.

## Open Questions (Tracked, Non-Blocking for v1 Draft)

1. Whether kernel public APIs live in `NGIN.Core` temporarily or a new `NGIN.Runtime` component.
2. Exact representation of version ranges (`SemVer`, normalized tuple, or custom comparator rules).
3. Whether tick-phase registration belongs to `Runtime.Tasks` or a separate `Runtime.Loop` subsystem.
4. How much unload support to require from v1 dynamic plugins vs load-once semantics.

## Example Startup Sequence (Illustrative)

1. Host builds `KernelHostConfig` (`EditorHost`, reflection enabled, plugin loading enabled).
2. Kernel loads and merges config layers.
3. Kernel discovers static modules and scans plugin manifests.
4. Kernel resolves module graph and validates compatibility/version constraints.
5. Kernel builds foundational services (`Config`, `Events`, `Tasks`, logging).
6. Kernel loads/constructs modules in phase order.
7. Modules register services/subscriptions/config defaults in `OnRegister`.
8. Modules initialize (`OnInit`) and start (`OnStart`) in dependency order.
9. Kernel emits `KernelRunning` and enters main loop.
