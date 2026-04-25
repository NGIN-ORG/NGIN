# VS Code Extension

`Tools/NGIN.VSCode` contains the in-repo VS Code extension for NGIN projects.
It is an editor front end over the native `ngin` CLI, not a separate project
model or build system.

Use it when you want to select projects and configurations, build, run, debug,
validate, inspect graphs, or generate metadata from VS Code while keeping the
same behavior as the terminal commands.

## What It Provides

- NGIN activity-bar views for workspace, project, and configuration navigation
- status bar items for the selected workspace, project, and configuration
- commands for build, clean, rebuild, run, debug, validate, graph, and MetaGen
- generated VS Code tasks for known project/configuration pairs
- `.nginlaunch`-based run and debug resolution
- a custom `ngin` debug type that launches native C/C++ debug sessions
- C/C++ configuration-provider support for `ms-vscode.cpptools`
- file registration and snippets for `.ngin`, `.nginproj`, `.nginpkg`, and
  `.nginlaunch`

The CLI remains the source of truth. If a command works in the terminal, the
extension should call the same command with the selected project and
configuration.

## Command Mapping

The extension mirrors the CLI directly:

- selected project maps to `--project`
- selected configuration maps to `--configuration`
- Build maps to `ngin build`
- Clean maps to `ngin clean`
- Rebuild maps to `ngin rebuild`
- Run maps to `ngin run`
- Validate maps to `ngin validate`
- Graph maps to `ngin graph`
- Generate Metadata maps to `ngin metagen`

Run and debug use the staged `.nginlaunch` file produced by `ngin build`. When
debugging, the extension can build first if the launch manifest is missing or
stale.

## Build And Install

Prerequisites:

- Node.js
- npm
- VS Code command-line launcher: `code`

From this directory:

```bash
cd Tools/NGIN.VSCode
npm ci
npm run build
```

Package and install the extension locally:

```bash
VERSION=$(node -p "require('./package.json').version")
npx @vscode/vsce package --out "ngin-vscode-${VERSION}.vsix"
code --install-extension "./ngin-vscode-${VERSION}.vsix" --force
```

Reload VS Code after installing:

```bash
code --reuse-window /home/berggrenmille/NGIN
```

## Daily Use

Open the repository root in VS Code. The extension activates when it finds
`.nginproj` files.

Typical flow:

1. Open the NGIN activity-bar view.
2. Select a project.
3. Select a configuration.
4. Run Validate, Build, Run, Debug, Graph, or Generate Metadata.

The same flow is available from the command palette with commands such as:

```text
NGIN: Select Project
NGIN: Select Configuration
NGIN: Build
NGIN: Run
NGIN: Debug
NGIN: Generate Metadata
```

## Development

Run type checks and unit tests:

```bash
npm run typecheck
npm run test:unit
```

Run integration tests:

```bash
npm run test:integration
```

For active extension development, open `Tools/NGIN.VSCode` in VS Code and launch
the extension host target from the checked-in debugging configuration. The
extension host opens the repository root as the test workspace and builds the
extension before launch.

## Notes

- The extension expects the native CLI to be available from the repository build
  output or configured extension settings.
- Build outputs, launch manifests, compile databases, and MetaGen output are
  generated artifacts.
- The extension should not invent editor-only behavior that disagrees with the
  CLI contract.
