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

Every lifecycle callback receives the resolved descriptor through
`ModuleContext`. Dynamic discovery records the normalized descriptor path,
descriptor directory as the module root, plugin name, and resolved library
path. Static modules may declare a module root through `ModuleOptions`.

The module root is provenance, not a content subsystem. Higher layers decide
whether it contains assets, scripts, templates, localization, migrations,
schemas, or other files and mount those files through their own services.

Dynamic plugin loading is intentionally simple:

- descriptor attributes: `Name`, `Library`, optional `Registrar`
- default registrar: `NGIN_RegisterPlugin`
- registrar API: `CoreResult<void>(IPluginModuleRegistry&)`
- loaded libraries stay alive until the kernel is destroyed

There is no hot reload, sandboxing, signature verification, or stable
cross-compiler ABI guarantee in this contract.

## Capabilities And Provider Selection

Capabilities describe roles provided by active modules independently from
service contracts. A capability may be exclusive, meaning at most one active
module can provide that role. The resolver checks exclusive capabilities after
module selection and compatibility filtering but before dependency ordering or
lifecycle callbacks.

Provider selection remains explicit in project/profile/builder configuration.
Priority controls deterministic startup ordering; it never silently selects a
provider. Conflicts return `KernelErrorCode::CapabilityConflict` with the
capability and sorted provider names.

## Services, Events, Tasks, Config

- Services use typed keys with optional names and support singleton, scoped, and
  transient lifetimes.
- Events are typed-first, with raw records available for dynamic or tooling
  scenarios.
- Deferred events are queue-owned: `Main`, `IO`, `Worker`, `Background`, and
  optional `Render`.
- Tasks run on lane-specific schedulers.
- Configuration is layered from defaults, host inputs, environment, local
  override, command line, and runtime mutation.

## Tasks and shutdown

`ModuleContext::Tasks()` submits work owned by the module. Its barriers and
`WaitIdle` observe that module's callbacks. The runtime returned by
`IKernel::GetTasks()` and supplied to bootstrap observes work across the host.
The same lanes and task IDs are shared across both interfaces.

A task stays active while queued, running, or waiting for its scheduled time.
The lane barrier waits until its lane is idle, and the global barrier waits
until all its lanes are idle. Both wait through callback capture destruction.
`WaitIdle` returns false on timeout, and a negative timeout waits indefinitely.
A callback cannot block on a barrier for its own lane or on a wait across all
lanes of its runtime. Submission reports invalid arguments and scheduler
rejections to the caller. Callbacks must handle their own errors. An exception
that escapes a callback terminates the process.

When shutdown begins, the kernel closes admission for the host runtime and all
module task views. It calls `OnStop` for started modules in reverse startup
order, then waits for all accepted work to finish. Once idle, the kernel clears
module subscriptions, calls `OnShutdown`, and ends module service scopes in
reverse startup order. Startup failure uses the same drain for work accepted
before the failure. Use `OnStop` to signal callbacks to finish and
`OnShutdown` to release module resources those callbacks use.

The drain waits up to two seconds per shutdown attempt. If it times out,
`Shutdown()` returns `TaskDrainTimeout` and the kernel stays in `Stopping`
with its module scopes active. Another call to `Shutdown()` can finish the
drain and cleanup without repeating `OnStop`. Releasing the kernel also
waits for accepted callbacks before destroying their resources. The module
task view is borrowed from the kernel.

## Observability

The kernel reports startup warnings/failures through `StartupReport` and logs
through `NGIN.Log` categories for kernel, module loading, services, events,
tasks, config, and plugins.
