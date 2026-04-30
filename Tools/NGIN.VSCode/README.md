# VS Code Extension

`Tools/NGIN.VSCode` contains the in-repo VS Code extension for NGIN projects.
It is an editor front end over the native `ngin` CLI, not a separate project
model or build system.

Use it when you want to select projects and profiles, configure build
metadata, build, run, debug, validate, inspect graphs, or generate metadata from
VS Code while keeping the same behavior as the terminal commands.

## What It Provides

- NGIN activity-bar views for workspace, project, and profile navigation
- status bar items for the selected workspace, project, and profile
- commands for configure, build, clean, rebuild, run, debug, validate, graph,
  variable explanation, and local settings initialization
- generated VS Code tasks for known project/profile pairs
- `.nginlaunch`-based run and debug resolution
- a custom `ngin` debug type that launches native C/C++ debug sessions
- C/C++ profile-provider support for `ms-vscode.cpptools`
- file registration and snippets for `.ngin`, `.nginproj`, `.nginmodel`,
  `.nginpkg`, `.nginlaunch`, and `.nginsettings`
- V3 workspace/project parsing for `.nginmodel` includes, defaults, and profile
  templates so project trees, tasks, debug profiles, and status items use
  effective profile data

The CLI remains the source of truth. If a command works in the terminal, the
extension should call the same command with the selected project and
profile.

## Command Mapping

The extension mirrors the CLI directly:

- selected project maps to `--project`
- selected profile maps to `--profile`
- Configure maps to `ngin configure`
- Build maps to `ngin build`
- Clean maps to `ngin clean`
- Rebuild maps to `ngin rebuild`
- Run maps to `ngin run`
- Validate maps to `ngin validate`
- Graph maps to `ngin graph`
- Explain Variables maps to `ngin variables explain`
- Initialize Local Settings maps to `ngin settings init`

`NGIN: Explain Variables` opens the redacted explanation in a readonly editor
document. `NGIN: Initialize Local Settings` opens the initialized
`.ngin/local/user.nginsettings` file. The editor also completes
`FromLocalSetting` keys from loaded `.nginsettings` files and completes
`FromEnvironment` names from the current process environment without storing
values.

Configure generates backend build metadata such as `compile_commands.json`
without producing a `.nginlaunch` file. Run and debug use the staged
`.nginlaunch` file produced by `ngin build`. When debugging, the extension can
build first if the launch manifest is missing or stale.

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
3. Select a profile.
4. Run Validate, Build, Run, Debug, or Graph.

The same flow is available from the command palette with commands such as:

```text
NGIN: Select Project
NGIN: Select Profile
NGIN: Build
NGIN: Run
NGIN: Debug
NGIN: Explain Variables
NGIN: Initialize Local Settings
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
the extension host target from the checked-in debugging profile. The
extension host opens the repository root as the test workspace and builds the
extension before launch.

## Notes

- The extension expects the native CLI to be available from the repository build
  output or configured extension settings.
- Build outputs, launch manifests, compile databases, and generator outputs are
  generated artifacts.
- The extension should not invent editor-only behavior that disagrees with the
  CLI contract.
