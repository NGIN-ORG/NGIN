# Spec 008: Roadmap and Non-Goals

Status: Active
Last updated: 2026-03-08

## Direction

NGIN is moving toward a simpler, package-first platform:

- XML manifests
- native C++ CLI
- project and target driven composition
- packages as the primary reusable unit
- staged targets as the handoff between tooling and runtime

## Near-Term Steps

1. Move runtime module and plugin declaration fully into `.nginpkg`.
2. Make `Packages/` the authoritative umbrella integration layer over `Dependencies/` and locally owned package source trees such as `Packages/NGIN.Core/`.
3. Introduce shared C++ manifest and composition libraries used by both the CLI and `NGIN.Core`.
4. Tighten collision and completeness validation around modules, plugins, config, and staged content.
5. Complete the `.ngintarget` contract so future `ngin run` support has a stable input.
6. Define `.nginpack`, installed package stores, and the initial package install/inspect flow.
7. Connect staged output more directly to `NGIN.Core` startup.

## Non-Goals Right Now

- remote registry protocol
- package publishing workflow or remote feed UX
- asset pipeline productization
- final editor framework architecture
- final published plugin ABI and distribution model
- reintroducing a lockfile-centric public workflow

## Quality Bar

The platform should stay:

- small in vocabulary
- explicit in composition rules
- deterministic in staging
- easy to understand and modify
