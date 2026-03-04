# NGIN.Runtime Architecture

NGIN.Runtime implements the Spec 002 runtime-kernel contract.

## Subsystems

- Module loader/resolver
- Service registry
- Event bus
- Task runtime
- Configuration store
- Kernel orchestrator

## Loading model

- Static-first module loading is production-ready in v1.
- Dynamic plugin loading remains behind `IPluginCatalog` and `IPluginBinaryLoader` seams pending Spec 003.

## Observability

- Uses `NGIN.Log` with categories:
  - `Kernel`
  - `ModuleLoader`
  - `Services`
  - `Events`
  - `Tasks`
  - `Config`
  - `Plugin`
