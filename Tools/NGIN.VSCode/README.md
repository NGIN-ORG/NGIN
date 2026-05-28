# VS Code Extension

`Tools/NGIN.VSCode` contains the in-repo VS Code extension for NGIN projects.
It is an editor front end over the native `ngin` CLI, not a separate project
model or build system.

Use it when you want to select projects and profiles, configure build
metadata, build, run, debug, validate, inspect resolved project state, inspect
graphs, or generate metadata from VS Code while keeping the same behavior as
the terminal commands.

## What It Provides

- a single NGIN activity-bar Workspace view for project, profile, file,
  dependency, and artifact navigation
- read-only graph-native dependency, build, stage, launch, publish, package
  output, analyzer, and diagnostic details in the Workspace tree for the active
  project/profile, backed by `ngin inspect --format json`
- a default visual `.nginproj` editor for common product, profile,
  dependency, source/config, feature, launch, and environment edits while
  preserving XML source
- status bar items for the selected workspace, project, and profile
- commands for configure, build, clean, rebuild, run, debug, validate, analyze,
  graph, variable explanation, local settings initialization, and clang-tidy
  analyzer package authoring
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
manifest rows open `.nginproj` files, Profiles select the active profile,
Dependencies contains authored project/package/runtime/tool uses plus resolved
Packages, Features, Generators, Inputs, Stage, Runtime, Launch, Publish,
Package Outputs, Analyzers, and Diagnostics for the active project/profile,
Generated contains existing staged output, and Files contains declared source
and config inputs.
Right-click context menus expose project/profile
actions, file navigation, path copying, and authored file operations. Generators
are displayed generically; MetaGen appears only as a normal command generator
contributed by its package feature.

`NGIN: Analyze` keeps analyzer diagnostics separate from validation and inspect
diagnostics. The official `NGIN.Tooling.ClangTidy` package can be added with
`NGIN: Enable Clang-Tidy`; the extension can also open or create `.clang-tidy`.
The package is a wrapper over `clang-tidy` from `NGIN_CLANG_TIDY` or `PATH`; the
extension does not install LLVM binaries.

Long-running configure, build, rebuild, stage, publish, and analyze commands
use the CLI JSONL event stream in VS Code. The Output panel and progress
notifications are rendered by the extension from `NGIN.CLI.Event` records rather
than by parsing human terminal text. Analyzer diagnostics prefer structured
`diagnostic` events and retain the older text parser only as a fallback.
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
2. Choose a project/profile from the tree, or open a project manifest from its
   manifest row or project context menu.
3. Expand Dependencies to inspect packages, features, generators, and
   diagnostics.
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
NGIN: Enable Clang-Tidy
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
