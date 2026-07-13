# VS Code Extension

`Tools/NGIN.VSCode` contains the in-repo VS Code extension for NGIN projects.
It is an editor front end over the native `ngin` CLI, not a separate project
model or build system.

Use it when you want to select projects and profiles, configure build
metadata, build, run, debug, validate, inspect resolved project state, inspect
graphs, or generate metadata from VS Code while keeping the same behavior as
the terminal commands.

## What It Provides

- a project-first NGIN Workspace view with prominent Build, Run, and Profile
  actions
- a compact active-project tree for the manifest, dependencies, tooling,
  launch selection, artifacts, and current problems
- advanced graph-native input, inactive-tooling, graph, and explanation views
  backed by `ngin inspect --format json` and `ngin explain`
- a default visual `.nginproj` editor for common product, profile,
  dependency, source/config, feature, launch, and environment edits while
  preserving XML source
- status bar items for the selected workspace, project, and profile
- commands for configure, build, clean, rebuild, run, debug, validate, analyze,
  graph and tooling-plan inspection, variable explanation, and local settings
  initialization
- generated VS Code tasks for known project/profile pairs
- `.nginlaunch`-based run and debug resolution
- a custom `ngin` debug type that launches native C/C++ debug sessions
- C/C++ profile-provider support for `ms-vscode.cpptools`
- file registration and snippets for `.ngin`, `.nginproj`, `.nginpkg`,
  `.nginlaunch`, and `.nginsettings`
- a current product-first `.nginproj` schema artifact for editor tooling
- graph inspection backed by the frozen V4 Composition Graph JSON contract in
  `docs/specs/013-composition-graph-json-contract.md`
- workspace/project parsing for product sections, direct project profiles,
  workspace package sources, and authored dependency scopes

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
- Analyze maps to `ngin analyze`
- Graph maps to `ngin graph`
- The active Projects-tree inspector maps to `ngin inspect --format json`
- Explain Variables maps to `ngin variables explain`
- Initialize Local Settings maps to `ngin settings init`

`NGIN: Explain Variables` opens the redacted explanation in a readonly editor
document. `NGIN: Initialize Local Settings` opens the initialized
`.ngin/local/user.nginsettings` file. The editor also completes
`FromLocalSetting` keys from loaded `.nginsettings` files and completes
`FromEnvironment` names from the current process environment without storing
values.

`.nginproj` files open in the NGIN Project Editor by default. The editor writes
targeted XML edits through VS Code document edits so undo, dirty state, comments,
and unsupported sections remain under normal editor control. Use
`NGIN: Open Project XML Source` or the editor action to reopen the same manifest
as XML source.

Configure generates backend build metadata such as `compile_commands.json`
without producing a `.nginlaunch` file. Run and debug use the staged
`.nginlaunch` file produced by `ngin build`. When debugging, the extension can
build first if the launch manifest is missing or stale.

The Workspace tree is product-centered. Project rows select the active product,
and the active row includes the selected profile. Build, Run, Profile, and
Refresh are available from the view title. Less frequent actions such as
Configure, Rebuild, Clean, Validate, Debug, Graph, resolved-input inspection,
and inactive-tooling inspection live in the title overflow menu.

Only the active project expands into resolved profile-specific information.
Dependencies distinguishes workspace project references, direct packages, and
transitive packages. Package features are details of their owning dependency.
Tooling summarizes active generators and package-provided tool runs. Launch shows the effective
launch choice, Artifacts exposes the executable, staged application folder,
launch manifest, and compile database, and Problems appears only when inspect
reports a problem. Source membership and resolved inputs are intentionally not
duplicated from VS Code Explorer in the default tree.

Right-click dependency, tooling, and launch items to run the matching
`ngin explain` operation. Resolved inputs and excluded generators remain
available through `NGIN: Show Resolved Inputs` and
`NGIN: Show Inactive Tooling`.

`NGIN: Analyze` keeps tool-run diagnostics separate from validation and inspect
diagnostics. `NGIN: Show Tooling Plan` displays every effective run using the
same package-neutral graph contract as the CLI. Tool-specific configuration and
installation remain owned by the package and driver rather than the extension.
`NGIN: Add Tool Action` delegates package/action discovery and manifest
authoring to the CLI and contains no package-specific TypeScript logic.
`NGIN: Analyze Active File` and `NGIN: Analyze Changed Files…` use capability-
checked CLI input scopes. Tool quick fixes read stored, digest-bound edit sets
and apply them as native VS Code workspace edits after stale-document and
workspace-trust validation.

Long-running configure, build, rebuild, stage, publish, and analyze commands
use the CLI JSONL event stream in VS Code. The Output panel and progress
notifications are rendered by the extension from `NGIN.CLI.Event` records rather
than by parsing human terminal text. Tool diagnostics require structured
`diagnostic` events; the legacy analyzer text fallback has been removed.
Artifact events are shown in the Output panel, successful event completions can
refresh the workspace tree, malformed JSONL is surfaced as a CLI compatibility
error, and non-event JSON lines are ignored. Configure `ngin.output.verbosity`
to `verbose` to request backend output events, and configure
`ngin.output.color` for commands that still use human output.

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

1. Open the NGIN activity-bar Workspace view.
2. Choose a project from the tree and a profile from the view toolbar.
3. Expand Dependencies, Tooling, Launch, or Artifacts when details are needed.
4. Edit common `.nginproj` fields in the visual editor or reopen XML source for
   unsupported sections.
5. Run Validate, Build, Run, Debug, or Graph from the status bar, command
   palette, or project/profile context menu.

The same flow is available from the command palette with commands such as:

```text
NGIN: Select Project
NGIN: Select Profile
NGIN: Build
NGIN: Run
NGIN: Debug
NGIN: Analyze
NGIN: Analyze Active File
NGIN: Analyze Changed Files…
NGIN: Show Resolved Inputs
NGIN: Show Inactive Tooling
NGIN: Explain Selection
NGIN: Add Tool Action
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
