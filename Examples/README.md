# Examples

`Examples/` is the best place to learn the active NGIN model. Each example is a
small authored project that you can validate, graph, build, stage, and run with
the same CLI commands used by real projects.

Build the CLI first from the repository root:

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli
```

## Recommended Order

Read and run the examples in this order:

1. [`App.NativeMinimal`](App.NativeMinimal/README.md)
   Smallest plain native executable. No `NGIN.Core`, no hosted runtime.
2. [`App.HostedCore`](App.HostedCore/README.md)
   Adds the optional `NGIN.Core` hosted runtime with code-first startup.
3. [`App.Basic`](App.Basic/README.md)
   Adds project-owned runtime metadata while staying compact.
4. [`App.ReflectionMetaGen`](App.ReflectionMetaGen/README.md)
   Shows the first `ngin metagen` workflow for annotated reflection metadata.
5. [`App.Showcase`](App.Showcase/README.md)
   Shows profile overlays, optional packages, and richer runtime metadata.
6. `Game.Engine`, `Game.Client`, and `Game.Server`
   Show the rule that separate executables should usually be separate projects.
7. `ProjectRef.Config`
   Focused manifests for project-reference profile resolution.
8. `Workspace/NGIN.Workspace.ngin`
   A minimal sample workspace file.

That path starts with the tooling-only case, then introduces the hosted runtime,
then moves into richer profile and composition behavior.

## What Each Example Shows

- `App.NativeMinimal/` is a normal C++ executable described by an `.nginproj`.
- `App.HostedCore/` links `NGIN.Core`, loads staged config, and starts from code.
- `App.Basic/` demonstrates a compact hosted application with manifest-owned
  runtime metadata.
- `App.ReflectionMetaGen/` demonstrates annotation-driven reflection metadata
  generation with `ngin metagen`.
- `App.Showcase/` demonstrates several profiles in one project, including
  config overlays, package overlays, module enable/disable behavior, and
  reflection-gated composition.
- `Game.Engine/` is a small local library project used by the game samples.
- `Game.Client/` is a separate executable project that references `Game.Engine`.
- `Game.Server/` is a separate headless executable project that also references
  `Game.Engine`.
- `ProjectRef.Config/` captures profile inheritance and collision cases
  for project references.
- `Workspace/NGIN.Workspace.ngin` shows the optional workspace layer.

## One Command Pattern

Every launchable example follows the same command shape:

```bash
./build/dev/Tools/NGIN.CLI/ngin validate \
  --project <path-to-project.nginproj> \
  --profile <Profile>

./build/dev/Tools/NGIN.CLI/ngin graph \
  --project <path-to-project.nginproj> \
  --profile <Profile>

./build/dev/Tools/NGIN.CLI/ngin build \
  --project <path-to-project.nginproj> \
  --profile <Profile> \
  --output build/manual/<ExampleName>

./build/dev/Tools/NGIN.CLI/ngin run \
  --project <path-to-project.nginproj> \
  --profile <Profile> \
  --output build/manual/<ExampleName>
```

`validate` answers whether the selected project/profile is coherent.
`graph` shows the resolved composition. `build` creates a staged runnable
directory. `run` launches from the generated `.nginlaunch`.

## Minimal Versus Advanced

Use `App.NativeMinimal` when you want to understand NGIN as a build/stage/run
tool for a plain native executable.

Use `App.HostedCore` when you want the smallest example of the optional hosted
runtime.

Use `App.Showcase` when you want to understand how much variation can live in
profiles before a second buildable unit should become another project.

Use the game samples when you want to see the opposite case: a shared library,
a client executable, and a server executable modeled as separate projects.
