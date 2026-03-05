# Spec 005: Implementation Roadmap

Status: Active v1 Roadmap  
Owner: NGIN umbrella workspace (`NGIN`)  
Last updated: 2026-03-04

Depends on:

- `001-module-dependency-graph.md`
- `002-runtime-kernel-design.md`
- `003-plugin-abi-header-spec.md`
- `004-editor-architecture.md`

## Scope

- milestone sequencing for platform contract hardening
- CI/release gates tied to measurable acceptance tests
- risk tracking and completion criteria for runtime/module/plugin architecture

## Milestones

1. M1: Spec 001 metadata enforcement baseline
- Deliverables:
  - schema + semantic validation in `tools/ngin-sync.py`
  - hard-fail CI gate (`validate-spec001`)
  - deterministic `resolve-target` report path
- Exit criteria:
  - `validate-spec001` passes on Linux and Windows
  - invalid schema/graph/target cases fail with path-aware diagnostics

2. M2: Runtime kernel production baseline (`NGIN.Runtime`)
- Deliverables:
  - module resolver + lifecycle + startup/shutdown orchestration
  - services/events/tasks/config subsystems
  - required `NGIN.Log` integration
- Exit criteria:
  - runtime tests pass in CI
  - startup failure paths return structured `KernelError`

3. M3: Contract hardening pass (breaking-change allowed)
- Deliverables:
  - DI V2 lifetimes/scopes
  - descriptor-family-based enforcement
  - version/range compatibility checks
  - lane-aware tasks and queue-owned deferred events
  - API thread policy enforcement
- Exit criteria:
  - expanded runtime test matrix passes
  - no unresolved Spec 001/002 contract-drift findings in active critique list

4. M4: Plugin ABI handoff preparation (Spec 003 seam)
- Deliverables:
  - filesystem descriptor discovery
  - stable dynamic seam (`IPluginCatalog`, `IPluginBinaryLoader`)
  - explicit unsupported result when binary loader is absent
- Exit criteria:
  - static runtime path remains green
  - dynamic seam compatibility tests pass

## CI Mapping

- `.github/workflows/workspace-ci.yml`:
  - Python syntax gate
  - JSON parse gate for manifests/schemas/catalogs
  - `python tools/ngin-sync.py doctor`
  - `python tools/ngin-sync.py validate-spec001`
  - `python tools/ngin-sync.py resolve-target --target NGIN.RuntimeSample`
  - runtime configure/build/test gates

## Risk Log

1. Schema/tooling drift vs runtime vocabulary.
- Mitigation: canonical load-phase enum and semantic validators.

2. Runtime API drift under breaking changes.
- Mitigation: migration doc + expanded tests + explicit deprecations.

3. Reproducibility drift across release channels.
- Mitigation: required-component pin policy for non-`dev` channels.

## Completion Criteria (v1)

- Spec 001 and Spec 002 validations are hard-fail gates in CI.
- Runtime tests cover resolver compatibility, lifecycle unwind, DI scopes/lifetimes, queue/lane semantics, and thread policies.
- Platform manifest for non-`dev` channels has pinned refs for all `required: true` components.
- Remaining dynamic binary ABI behavior is explicitly tracked under Spec 003.
