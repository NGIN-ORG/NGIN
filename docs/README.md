# NGIN documentation

Start with the path that matches what you are trying to do.

## Evaluate or learn NGIN

1. [Install and build the CLI](getting-started/installation.md).
2. [Create your first project](getting-started/first-project.md).
3. Run [Hello.Native](../Examples/Hello.Native) and then choose another
   [example](../Examples/README.md).

## Use the project system

- [Projects](guides/projects.md)
- [Profiles](guides/profiles.md)
- [Packages](guides/packages.md)
- [Workspaces](guides/workspaces.md)
- [Generators](guides/generators.md)
- [Staging and launch](guides/staging-and-launch.md)
- [Publishing](guides/publishing.md)
- [Tooling and quality checks](guides/tooling.md)

## Look up an exact contract

- [Project manifest](reference/project-manifest.md)
- [Package manifest](reference/package-manifest.md)
- [Workspace manifest](reference/workspace-manifest.md)
- [CLI](reference/cli.md)
- [Variables](reference/variables.md)
- [Composition Graph JSON](reference/composition-graph.md)
- [Tool driver protocol](reference/tool-driver.md)

The CLI is the executable source of truth for commands and manifest schema:

```bash
ngin
ngin schema --format json
```

## Use an NGIN library

See the [libraries index](libraries/README.md) for `NGIN.Base`, `NGIN.Core`,
`NGIN.Log`, `NGIN.Reflection`, `NGIN.ECS`, and `NGIN.UI`.

## Work on NGIN itself

- [Architecture overview](architecture/overview.md)
- [Architecture decisions](architecture/decisions/README.md)
- [Build the repository](contributing/building-ngin.md)
- [Run tests](contributing/testing.md)
- [Documentation style](contributing/documentation.md)

Roadmaps and implementation work belong in GitHub issues and projects. Git
history preserves superseded designs. Release and migration material is kept
only when it helps users move between published library versions.
