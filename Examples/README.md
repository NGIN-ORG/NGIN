# Examples

`Examples/` contains the canonical authored examples for the active NGIN model. They exist to show what the V2 project-first shape looks like in practice, not just in specs: how a small app is authored, how a richer app adds configuration-level overlays, and how separate executables are modeled as separate projects.

If you are learning the repo, this directory is the shortest path from “I understand the words in the spec” to “I can see how those words map to real project files.”

## Recommended Order

The easiest reading order is:

1. `App.Basic/`
2. `App.Showcase/`
3. `Game.Engine/`, `Game.Client/`, and `Game.Server/`
4. `ProjectRef.Config/`
5. `Workspace/NGIN.Workspace.ngin`

That order starts with the smallest runnable app, then introduces richer configuration behavior, then shows how separate executables are split into separate projects, and finally shows project-reference and workspace-level composition behavior.

## What Each Example Shows

- `App.Basic/` is the smallest real application project in the repo. It owns its `main.cpp`, project manifest, config, and executable output.
- `App.Showcase/` is the richer application example. It demonstrates multiple configurations in one project, configuration-level package and config overlays, and project-owned runtime metadata.
- `Game.Engine/` is a small local library project used by the game samples.
- `Game.Client/` is a separate executable project that references `Game.Engine`.
- `Game.Server/` is a separate headless executable project that also references `Game.Engine`.
- `ProjectRef.Config/` is a focused set of manifests for project-reference resolution, including configuration inheritance and collision scenarios.
- `Workspace/NGIN.Workspace.ngin` is a minimal sample workspace file that points at the examples and demonstrates the optional `.ngin` workspace layer.

## Minimal Versus Advanced

If you want the smallest normal application path, use `App.Basic`.

If you want to see how the same project can take on several runtime shapes through named configurations, use `App.Showcase`.

If you want to understand the V2 rule that separate executables should usually be separate projects, use the game samples. They are intentionally split into:

- one engine/library project
- one game client project
- one headless server project

That is the clearest way to see the difference between “another buildable thing” and “another configuration of the same thing.”

## Suggested Learning Path

Start by validating and running `App.Basic`. Then read `App.Showcase` to see what changes when configurations start adding overlays. After that, inspect `Game.Client` and `Game.Server` to understand why those are modeled as separate projects instead of as extra configurations.

Once that model feels natural, `ProjectRef.Config` and the sample workspace file become much easier to read.
