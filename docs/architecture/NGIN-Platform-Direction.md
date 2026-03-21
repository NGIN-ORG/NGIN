# NGIN Platform Direction

NGIN V2 is intentionally simple by default and explicit when needed.

## Direction

- project-first authoring
- optional workspace
- package as reusable unit, not mandatory for every local app
- narrow configuration model
- generated `.nginlaunch` as the runtime handoff
- code-first application composition with optional advanced manifest metadata

## Consequences

- separate executables should usually be separate projects
- package `SourceBinding` is removed from the active contract
- CLI commands operate directly on projects and configurations
- tooling and docs should avoid mixed `variant` and `target` terminology
