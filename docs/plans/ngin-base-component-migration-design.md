# NGIN.Base compiled-component migration review

Status: approved and implemented on 2026-08-05.

## Decision requested

Approve replacing the metadata-only ownership targets and overlapping
transitional split libraries with six real compiled components:

```text
Foundation
└─ Execution
   └─ IO
      ├─ Serialization
      │  └─ Crypto
      └─ Net ──> Crypto (only when the approved TLS surface is present)
```

More exactly:

- Foundation has no Base component dependency.
- Execution depends on Foundation.
- IO depends on Foundation and Execution.
- Serialization depends on Foundation and IO.
- Crypto depends on Foundation, IO, and Serialization.
- Net depends on Foundation, Execution, and IO; approved TLS adds Crypto.

The current public-header audit reports zero forbidden dependency directions.

## Target model

Each component receives compiled linkage targets:

- `NGIN::Base::<Component>::Static`
- `NGIN::Base::<Component>::Shared`
- `NGIN::Base::<Component>`, selecting the enabled preferred form

Compatibility aggregates remain:

- `NGIN::Base::Static`, an interface aggregate of all static components
- `NGIN::Base::Shared`, an interface aggregate of all shared components
- `NGIN::Base`, selecting the preferred aggregate

There is no second aggregate compilation. Every source has one component owner
and is compiled once per enabled linkage form. If static and shared are both
requested, separate object sets are required on Windows so export/import
visibility remains correct; that is intentional linkage-form compilation, not
aggregate duplication.

The old overlapping `NGIN.Net.Static`, `NGIN.Serialization.Static`, and
`NGIN.Crypto.Static` implementations are removed. No alternate legacy target
spellings or fallback aliases are added.

## Build and package enforcement

- generate component source/header lists from the existing authoritative
  ownership variables
- fail configure on unowned, multiply owned, or forbidden cross-component
  public includes
- link platform libraries at the owning component rather than the aggregate
- export every enabled component and aggregate through `NGINBaseTargets.cmake`
- generate a build-tree package export alongside the installed package export
- update `NGINBaseConfig.cmake` to construct canonical preferred aliases
- expose all six canonical component targets in
  `Packages/NGIN.Base/NGIN.Base.nginpkg`
- add build-tree and installed external consumers that link every component
  individually and the aggregate

## Consumer migration order and intended links

1. NGIN.Base tests, benchmarks, examples
   - each focused target links its owning component
   - cross-component integration tests list every component they use
2. CLI model/authoring, then resolution/build/tooling/commands
   - Foundation, IO, Serialization, and Crypto only where directly included
3. Reflection and MetaGen
   - Reflection: Foundation
   - MetaGen: Foundation and Serialization, plus Reflection as its own package
4. Core and Log
   - Log: Foundation and IO for file sinks
   - Core: Foundation, Execution, IO, Serialization plus its Log/Reflection
     package dependencies
5. ECS
   - Foundation and Execution where its worker scheduling requires it
6. UI and UI integrations
   - Foundation and Execution; add IO or Net only in the integration that uses it
7. Examples and remaining package wrappers
   - narrowest target proven by direct includes

The migration changes CMake linkage only; source includes are narrowed when an
umbrella is broader than the code actually uses. Each ownership group is built
and tested before the next group.

## Static/shared and symbol rules

- public non-template compiled APIs use component-specific visibility macros;
  `NGIN_BASE_API` remains available for source compatibility
- each shared component compiles with the export definition and consumes its
  dependencies with import definitions
- static components never expose shared import/export definitions
- Windows shared builds use component visibility macros plus automatic symbol
  export as a compatibility bridge for older declarations
- platform and provider link dependencies are `PRIVATE` unless a public header
  exposes their types (the provider-neutral TLS API does not)
- aggregate interface targets add no symbols

## Verification and rollback boundary

- configure-time ownership and dependency-boundary checks
- all 257 independent public-header compilations
- focused build-tree consumer for each component and the aggregate
- install into an empty prefix, then repeat the consumer matrix
- static-only, shared-only, and combined static/shared builds
- symbol inspection for duplicate definitions and unintended exports
- downstream builds in the migration order above

The rollback boundary is the target-model change itself: no consumer group is
migrated until component archives/shared libraries and both consumer modes pass.
Because no compatibility target spellings are introduced, a failed migration
cannot become a permanent silent fallback.
