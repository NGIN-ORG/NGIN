# NGIN.Core Architecture

`NGIN.Core` is a hosted runtime layered on top of the NGIN project/package
model. It is optional: applications can use the NGIN CLI without linking the
runtime.

## Host Startup

`ApplicationBuilder` is the user-facing entry point. It gathers project
metadata, config inputs, services, static module factories, plugin search paths,
and command-line arguments, then builds a `KernelHostConfig`.

`IKernel` owns startup and shutdown:

- apply host config and layered configuration
- discover static and dynamic module descriptors
- resolve compatibility, dependencies, and startup order
- build services, events, tasks, config, and logging
- construct modules and run lifecycle callbacks

## Modules

Static modules are registered with factories in C++. Dynamic modules are
described by XML files under plugin search paths and loaded from shared
libraries. The descriptor remains the source of metadata for dependency and
compatibility checks; the library registrar only supplies factories.

Dynamic plugin loading is intentionally simple:

- descriptor attributes: `Name`, `Library`, optional `Registrar`
- default registrar: `NGIN_RegisterPlugin`
- registrar API: `CoreResult<void>(IPluginModuleRegistry&)`
- loaded libraries stay alive until the kernel is destroyed

There is no hot reload, sandboxing, signature verification, or stable
cross-compiler ABI guarantee in this contract.

## Services, Events, Tasks, Config

- Services use typed keys with optional names and support singleton, scoped, and
  transient lifetimes.
- Events are typed-first, with raw records available for dynamic or tooling
  scenarios.
- Deferred events are queue-owned: `Main`, `IO`, `Worker`, `Background`, and
  optional `Render`.
- Tasks run on lane-specific schedulers and expose barriers per lane or across
  all lanes.
- Configuration is layered from defaults, host inputs, environment, local
  override, command line, and runtime mutation.

## Observability

The kernel reports startup warnings/failures through `StartupReport` and logs
through `NGIN.Log` categories for kernel, module loading, services, events,
tasks, config, and plugins.
