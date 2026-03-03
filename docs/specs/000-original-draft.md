# NGIN — Original Draft (Archived)

NGIN — Next Generation Infrastructure for eNGINes
Below is a **formal technical foundation document** for your platform, written as if it were the internal spec you hand to engineers and future contributors.

This positions **NGIN** clearly as an _Application Engine Platform_ — not a game engine — and incorporates:

- NGIN.Base (core systems library)
- NGIN.Reflection (runtime type system)
- Build/Module/Plugin architecture
- Editor + packaging model
- Engine-for-engines vision

---

# NGIN — Application Engine Platform

## Technical Architecture Specification (v1)

---

## 1. Overview

**NGIN** is a modular, cross-platform C++ application engine platform designed to serve as a foundation for building any type of application, domain engine, or toolchain.

NGIN is not game-specific. It provides:

- Runtime kernel
- Platform abstraction
- Module & plugin system
- Build & packaging pipeline
- Editor/tool host framework
- Asset and data infrastructure

Domain-specific engines (game, simulation, CAD, visualization, etc.) are built on top of NGIN.

---

## 2. Architectural Vision

### 2.1 Layered Model

```text
Application
   ↑
Domain Engine (Game/Sim/CAD/etc)
   ↑
NGIN Platform
   ↑
OS / Hardware
```

NGIN acts as the **application runtime substrate**.

---

### 2.2 Core Principles

#### Domain-Agnostic

No assumptions about application type, execution model, or rendering style.

#### Modular

All functionality implemented as modules/plugins.

#### Binary Distribution First

Applications and tools can be built without engine source code.

#### Deterministic Builds

Reproducible outputs across platforms.

#### Extensible Platform

New platforms and domain engines can be added without core changes.

---

## 3. Major Components

### 3.1 NGIN.Base — Core Systems Library

NGIN.Base is the foundational C++ systems library used by all NGIN components.

It replaces reliance on the STL where needed and provides controlled abstractions.

## Responsibilities

### Memory Systems

- Allocators
- Pools
- Arena allocation
- Memory tracking
- Debug instrumentation

### Containers

- Vectors
- Hash maps
- Sparse sets
- Ring buffers
- Custom intrusive containers

### Concurrency

- Threads
- Thread pools
- Task scheduling primitives
- Synchronization primitives
- Lock abstractions

### Async Framework

- Futures/promises
- Continuations
- Coroutines integration (if enabled)
- Cancellation tokens
- Timers

### Networking

- Sockets abstraction
- Protocol utilities
- Async I/O integration

### Utilities

- String system
- File utilities
- Time utilities
- Logging primitives

## Design Constraints

- Non-header-only
- ABI-stable within NGIN version
- Cross-platform implementations

---

### 3.2 NGIN.Reflection — Runtime Type System

Provides runtime type information and metadata.

## Capabilities

- Type registration
- Property inspection
- Method invocation
- Constructor metadata
- Attribute/annotation system
- Serialization hooks

## Intended Uses

### Inversion of Control (IoC)

Modules can request dependencies dynamically.

### Dependency Injection

Constructor injection via metadata.

### Plugin Discovery

Automatic module registration.

### Serialization & Editor Integration

Dynamic property editing and persistence.

## Optionality

Reflection is optional for runtime applications but required for:

- Editor tooling
- Visual scripting
- Asset inspection
- Dependency injection systems

---

### 3.3 NGIN Runtime Kernel

Provides core platform services.

## Responsibilities

### Module Loader

- Load/unload modules at runtime
- Dependency resolution
- Version checks

### Service Locator / IoC Host

Built on NGIN.Reflection when enabled.

### Event System

- Publish/subscribe
- Signals
- Message buses

### Task Scheduler

Unified async execution model.

### Configuration System

Hierarchical config with overrides.

---

### 3.4 Platform Abstraction Layer

Provides OS-independent interfaces for:

- Windowing
- Input
- Filesystem
- Networking
- Graphics device access
- Timing
- Process control

Platforms implemented as modules:

```text
Platform.Win64
Platform.Linux
Platform.Android (future)
```

---

### 3.5 Rendering & GPU Abstraction

NGIN provides a rendering platform, not a game renderer.

## Provides

- GPU device abstraction
- Resource management
- Shader system
- Render graph infrastructure

Domain engines implement rendering pipelines on top.

---

### 3.6 Asset & Data System

Generic asset infrastructure.

## Features

- Asset database
- Import pipeline
- Versioned asset formats
- Dependency tracking
- Streaming support

Not limited to game assets.

---

### 3.7 UI & Editor Framework

NGIN includes a tool host framework enabling creation of editors and applications.

## Features

- Dockable workspace
- Panels/windows
- Layout persistence
- Command system
- Tool/plugin integration

The NGIN Editor is itself an application built on this framework.

---

## 4. Build & Module System

### 4.1 BuildTool

Custom build orchestrator using Ninja backend.

## Responsibilities

- Discover descriptors
- Build dependency graph
- Run code generation
- Emit Ninja files
- Execute builds

---

### 4.2 Modules

Smallest unit of compilation and deployment.

## Structure

```text
Module/
  Module.module.json
  Public/
  Private/
```

## Types

- Runtime
- Editor
- Developer
- Program
- ThirdParty

---

### 4.3 Targets

Define final build products.

Examples:

- NGIN.Editor
- MyApplication
- SimulationServer

---

### 4.4 Plugins

Optional module bundles.

Can contain:

- Modules
- Assets
- Config
- Resources

---

## 5. Binary Plugin System

NGIN supports binary-only modules without source distribution.

### Plugin ABI

C ABI boundary:

```cpp
extern "C" PluginApi* GetPluginAPI(HostApiVersion);
```

Provides stability across compilers.

---

## 6. Packaging & Distribution

## Build Pipeline

1. Build
2. Cook assets
3. Stage runtime
4. Package distribution

---

## 7. NGIN Editor

A universal tool host built on the NGIN platform.

Not game-specific.

Supports:

- Creating applications
- Managing assets
- Packaging builds
- Installing plugins
- Running domain engines

---

## 8. Domain Engines

NGIN enables creation of domain-specific engines:

- Game Engine
- Simulation Engine
- CAD Engine
- Visualization Engine
- Film Production Engine

Each implemented as a set of modules/plugins.

---

## 9. Application Model

Applications built on NGIN consist of:

- Domain engine modules
- Application modules
- Assets
- Configurations

Packaged with runtime platform.

---

## 10. Versioning & Compatibility

Each module/plugin specifies:

- NGIN version
- Compatible range
- ABI version
- Platform support

Loader enforces compatibility.

---

## 11. Future Expansion

### Planned Areas

- Visual scripting
- Distributed builds
- Cloud asset pipelines
- Marketplace ecosystem
- Plugin sandboxing

---

# Conclusion

NGIN is a **general-purpose application engine platform** enabling construction of domain engines and applications through modular architecture, robust tooling, and binary distribution capabilities.

