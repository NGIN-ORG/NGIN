---
title: Troubleshoot run and plugins
description: Diagnose launch plans, working directories, runtime files, shared libraries, Core modules, native plugins, and shutdown failures.
---

# Troubleshoot run and plugins

## Build and stage pass, but run fails

Run the exact staged executable from the staged working directory to separate
the application from the launcher. Before that, inspect the RunPlan through the
graph: executable, working directory, arguments, environment, run name, and
staged dependencies must match the selected profile.

An executable product has an implicit default Run. If several authored Runs
exist, select the intended name explicitly.

## Shared library cannot be loaded

Check that the library exists in the stage, matches architecture and build
configuration, and has all of its own runtime dependencies. Loader search rules
are platform-specific; prefer stage metadata that puts dependencies in the
layout expected by the platform adapter.

Successful file discovery does not prove ABI compatibility.

## NGIN.Core host does not start

Inspect `IApplicationHost::GetStartupReport()` and the full `KernelError` chain.
Keep `code`, `subsystem`, `module`, `dependencyPath`, message, and nested cause.

Common startup codes:

- `MissingRequiredDependency` or `DependencyCycle`;
- host/platform/version incompatibility;
- `CapabilityConflict` between exclusive providers;
- stage/layer ordering violation;
- module factory or lifecycle failure;
- service registration/config/schema failure;
- thread policy violation.

Fix descriptor/registration intent. Priority affects deterministic startup
ordering; it does not silently select one exclusive provider over another.

## Dynamic plugin is not discovered

Verify all four layers:

1. the plugin is enabled by resolved product/profile configuration;
2. its descriptor is staged under a configured search path;
3. the descriptor's library path resolves from its module root;
4. the library exports the configured registrar (default
   `NGIN_RegisterPlugin`).

NGIN.Core does not provide hot reload, sandboxing, signature verification, or
stable cross-compiler ABI. Build trusted plugins for a compatible compiler,
runtime, architecture, and NGIN.Core version.

## Reflection module refuses to unload

Reflected values and instances retain their owning module. Release instances,
descriptor-dependent state, service instances, callbacks, and adapters before
requesting unload. The refusal is a lifetime guard, not a leak to bypass.

## Network or async work stalls during runtime

NGIN.Base runtimes are explicit. A cooperative scheduler, network driver,
filesystem driver, or resolver driver makes progress only while its owner runs
or polls it. Keep the driver, scheduler, task context, buffers, and handles
alive until terminal completion.

## Shutdown hangs

Stop producers first, request cooperative cancellation, drain/barrier accepted
work, flush events and logs, then destroy modules and runtimes in dependency
order. A task that ignores cancellation can delay structured `WhenAny`, module
shutdown, or a task-runtime barrier indefinitely.

