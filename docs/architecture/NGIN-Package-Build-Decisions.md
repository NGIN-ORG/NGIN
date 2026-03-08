# NGIN Package Build Decisions

Status: Active design note  
Last updated: 2026-03-08

## Purpose

This note captures the current design decisions around package build metadata, artifact modeling, and target executable selection.

It is intentionally narrower than the main specs. It exists to record:

- the issue being solved
- the current suggested solution
- the concrete actions required next

This note should guide the next package-build spike and the next updates to Spec 002 and Spec 003.

## Design Rule

The public package model should describe what users consume, not how the umbrella workspace happens to be organized internally.

If a distinction matters only to workspace management, release pinning, or local repo layout, it should stay out of the public package contract unless the spike proves it is necessary.

## Scope

This note is about:

- package source binding
- package artifacts
- build-vs-runtime resolution boundaries
- target-level executable selection
- the concrete validation spike for the current repo

This note is not about:

- remote registries
- install/publish flows
- final run/entrypoint metadata
- raw include/link metadata unless the spike proves it is necessary

## 1. Packages Must Describe Build Concerns

Issue:
Packages already describe runtime contributions such as modules, plugins, and content, but `ngin project build` still only stages content and config. There is not yet a real package-driven build contract.

Suggested solution:
Packages must describe both:

- runtime contributions
- build-facing artifacts

Packages remain the main reusable unit. Build metadata should be added to the package model instead of inventing a second build-only dependency system.

Required actions:

- extend Spec 003 to include a first-class `Artifacts` section
- update the CLI model to parse package artifacts
- update the build flow to resolve package artifacts instead of only staging content

## 2. Separate Package Backing From Package Outputs

Issue:
The model becomes confusing when “where a package comes from” and “what a package provides” are described as one thing.

Suggested solution:
Keep these as separate concepts:

- `SourceBinding`
  Answers: where does this package come from?

- `Artifacts`
  Answers: what does this package provide?

This separation should remain explicit in both spec language and implementation.

Required actions:

- keep `SourceBinding` as a package-level concept
- introduce `Artifacts` as a separate package-level concept
- avoid mixing these two concerns into one overloaded `Build` section

## 3. Keep SourceBinding Kinds Minimal

Issue:
The source-kind vocabulary can easily drift into umbrella-workspace terminology instead of package-facing meaning.

Suggested solution:
Use a minimal public set of source kinds:

- `Source`
- `CMakePackage`
- `Prebuilt`

Rationale:

- whether source lives in the umbrella repo or another checked-out repo is a workspace concern, not a package-facing concept
- external users care whether NGIN builds from source, asks CMake to find a package, or consumes prebuilt binaries
- package-relative local content should not require a separate public source kind unless the spike proves that it cannot be modeled cleanly as `Source`

Required actions:

- collapse current workspace-shaped source kinds into the public set above
- move repo-provenance concerns out of the public package contract
- update package manifests, CLI parsing, and docs to match the final names

## 4. Add Artifacts As A First-Class Package Section

Issue:
The current package model has runtime-facing sections and a thin `Build` section, but no clear contract for what executable or library outputs a package exposes.

Suggested solution:
Add a top-level `Artifacts` section to the package model.

Start with only:

- `Libraries`
- `Executables`

Do not add more artifact categories until the spike proves a real need.

Required actions:

- update Spec 003 to define `Artifacts`
- update package XML examples to use `Artifacts`
- update the CLI data model to parse `Libraries` and `Executables`

## 5. Keep Build Optional And Backend-Thin

Issue:
If `Build` grows too much, it will become a second overloaded package contract and reintroduce the same conceptual mixing the design is trying to remove.

Suggested solution:
`Artifacts` should be the package contract.

`Build` should remain:

- optional
- backend-thin
- limited to backend-mapping details that cannot be inferred

If a field answers “what does this package expose?”, it belongs in `Artifacts`.
If a field answers “how does the backend find or map that exposure?”, it belongs in `Build`.

Required actions:

- audit current `Build` usage in Spec 003
- move package-exposed library definitions under `Artifacts`
- keep `Build` only for backend mapping or backend-specific hints

## 6. Keep Workspace Integration Metadata Out Of The Public Package Contract

Issue:
The repo still has a real need to track source ownership, local checkout layout, and release pinning. Those concerns are real, but they are not the same thing as the public package contract.

Suggested solution:
Split the model mentally into two layers:

- public package contract
  - identity
  - dependencies
  - source kind
  - artifacts
  - runtime contributions
  - content

- workspace integration metadata
  - repo URL
  - checkout path
  - local-vs-synced repo provenance
  - release pins

The first layer belongs in `.nginpkg`.
The second layer belongs in workspace manifests, release manifests, or internal tooling metadata.

Required actions:

- keep repo-provenance and workspace-layout detail out of Spec 003 unless the spike proves otherwise
- treat release/workspace manifests as the place for checkout and pinning concerns

## 7. Split Artifact Shape From Artifact Origin

Issue:
The current package model overloads `Kind`. In practice it is being used for two different axes:

- binary shape, such as `Static`
- supply mode, such as `Imported`

Suggested solution:
Split these into separate fields.

For libraries:

- `Linkage = Static | Shared | Interface`
- `Origin = Built | Imported | Prebuilt`

For executables:

- `Origin = Built | Imported | Prebuilt`

Do not overload one field to mean both.

Required actions:

- update Spec 003 examples and field definitions
- update package wrapper manifests like `NGIN.Core` and `SDL2`
- update CLI parsing and display logic

## 8. Artifact Origin Should Not Always Be Mandatory

Issue:
Artifact-level `Origin` is useful, but forcing it to be spelled out everywhere may add noise where it can be inferred safely from the package backing.

Suggested solution:
Support artifact-level `Origin`, but infer it by default when possible from `SourceBinding`.

Examples:

- `Source` package library usually implies `Built`
- `CMakePackage` library usually implies `Imported`
- `Prebuilt` package artifact usually implies `Prebuilt`

Only require explicit `Origin` when:

- the default would be ambiguous
- a package mixes multiple supply modes

Required actions:

- define inference rules in the spec or implementation note
- make CLI parsing support optional `Origin`
- emit warnings or errors only when inference is ambiguous

## 9. External Libraries Must Fit The Same Package Model

Issue:
If external libraries like SDL2 need a different path than first-party packages, the model is too weak.

Suggested solution:
Treat external libraries as normal packages.

Example:

- `SDL2`
  - `SourceBinding = CMakePackage`
  - library artifact with `Origin = Imported`

External packages should not require a parallel side model.

Required actions:

- keep `SDL2` as a canonical external package example
- make the spike prove that the SDL2 wrapper works cleanly under the new artifact model

## 10. Prebuilt Binaries Are Valid Packages

Issue:
The package model must be able to represent prebuilt libraries and tool executables, not only source-backed code.

Suggested solution:
Allow packages backed by `Prebuilt`, and allow artifacts with `Origin = Prebuilt`.

This should cover:

- prebuilt libraries
- prebuilt executables
- prebuilt tool binaries

Required actions:

- keep `Prebuilt` in `SourceBinding`
- support `Origin = Prebuilt` on artifacts
- defer raw binary-path/include/link details until the spike proves they are required

## 11. Do Not Model Raw Include And Link Metadata Yet

Issue:
Explicit include directories, compile definitions, and link flags would make the package model much heavier very quickly.

Suggested solution:
Do not add raw include/link metadata by default yet.

For now:

- source-backed packages should rely on CMake target semantics
- imported CMake packages should rely on imported target semantics
- only prebuilt scenarios may force explicit metadata later

Required actions:

- keep this metadata out of Spec 003 for now
- only introduce it if one of the spike packages proves it is necessary

## 12. Keep Libraries And Executables Separate

Issue:
Flattening all build outputs into one generic binary bucket would make the target/run model less clear.

Suggested solution:
Keep explicit artifact groups:

- `Libraries`
- `Executables`

This should remain true even if both are stored as artifacts internally.

Required actions:

- define these as separate sections in Spec 003
- make CLI parsing and reporting preserve that distinction

## 13. Runtime Resolution Must Not Depend On Artifact Resolution

Issue:
If runtime module/plugin resolution depends on artifact build planning, the design becomes tangled and difficult to reason about.

Suggested solution:
Keep runtime resolution and artifact resolution as separate phases.

Package/runtime resolution should decide:

- package closure
- module closure
- plugin closure
- content closure

Artifact resolution should later decide:

- which libraries and executables are needed
- how they map to backend targets/imports

Required actions:

- preserve separate phases in the CLI build design
- do not let executable or library selection drive runtime module resolution

## 14. Add An Explicit Artifact-Resolution Phase

Issue:
“Resolve packages” and “resolve build outputs” are not the same operation.

Suggested solution:
Use this build flow:

1. resolve project and target
2. resolve packages and runtime graph
3. resolve selected executable and artifact closure
4. map artifacts to backend targets/imports
5. build required outputs
6. stage outputs, content, and config
7. emit `.ngintarget`

This should replace the current mental model where build and staging are treated as one step.

Required actions:

- document this flow in the package-build spike
- update CLI implementation planning to follow this order

## 15. Target-Level Executable Selection Is Needed Only When Ambiguous

Issue:
Targets currently have no explicit place to select a runnable executable, but making executable selection mandatory would overcomplicate simple projects.

Suggested solution:
Use inference-first behavior:

- zero executable candidates: error
- one executable candidate: infer it
- multiple executable candidates: require explicit target-level selection

This keeps simple cases simple while still supporting multi-executable packages or targets.

Required actions:

- update Spec 002 with a target-level executable selection slot
- make it optional
- only require it for ambiguous cases

## 16. Do Not Add Extra Artifact Metadata Until The Spike Proves A Gap

Issue:
It is tempting to add fields like `Role`, `Entrypoint`, include directories, or raw link metadata preemptively.

Suggested solution:
Do not add:

- `Role`
- `Entrypoint`
- raw include/link metadata

unless the spike proves a real need.

This is a deliberate simplicity rule.

Required actions:

- reject schema growth that is not required by the spike packages
- revisit these only if the current five-package spike cannot be modeled cleanly without them

## 17. The Spike Must Prove More Than Source-Built Libraries

Issue:
If the spike only works for source-built libraries and one app executable, the model is still too narrow.

Suggested solution:
Prove the model with these five packages:

- `NGIN.Core`
- `NGIN.ECS`
- `SDL2`
- `MyGame.Runtime`
- `NGIN.Diagnostics`

Rationale:

- `NGIN.Core` proves first-class workspace source plus runtime modules
- `NGIN.ECS` proves first-party repo-backed source plus modules
- `SDL2` proves imported external library packages
- `MyGame.Runtime` proves app executable selection
- `NGIN.Diagnostics` proves content/plugin-first or package-first behavior without assuming a normal code-backed library flow

Required actions:

- make these five the official validation spike set
- reject extra metadata that is not needed for these five

## Immediate Next Actions

1. Update Spec 003 to add `Artifacts` and reduce `Build` to a backend-thin optional section.
2. Reduce `SourceBinding` to the public set:
   - `Source`
   - `CMakePackage`
   - `Prebuilt`
3. Update Spec 002 to add optional target-level executable selection for ambiguous cases.
4. Update the CLI model in `Tools/NGIN.CLI/src/main.cpp` to parse and report `Artifacts`.
5. Implement the package-build spike around:
   - `NGIN.Core`
   - `NGIN.ECS`
   - `SDL2`
   - `MyGame.Runtime`
   - `NGIN.Diagnostics`
6. Reject any metadata additions not required by those five.
