# NGIN VS Code UI/UX refresh plan

## Status

Implemented as a dogfood-ready release candidate, August 2026. The native UI,
command routing, onboarding, documentation, automated coverage, and packaged
VSIX are complete. Moderated participant research, the manual accessibility
matrix, and one normal dogfood cycle remain release-validation activities that
require user and platform coordination.

The implementation changes the extension's interaction model, not the
authority boundary: the `ngin` CLI and resolved Composition Graph remain the
source of truth.

The earlier [developer-workflow overhaul](vscode-extension-reimagining-plan.md)
documents capabilities that have already been implemented. This plan starts
from the current extension and asks how those capabilities should be presented
with less chrome, less duplication, and a clearer daily workflow.

## Outcome

NGIN should feel like a native part of VS Code. A developer should be able to:

1. open an NGIN workspace and immediately understand whether it is ready;
2. build, run, test, or debug the project relevant to the active file;
3. see progress and failures in the VS Code surface where they expect them;
4. change project context without learning NGIN's internal graph vocabulary;
5. inspect composition details when needed without carrying that complexity in
the everyday interface.

### Implemented decisions

- Routine lifecycle Output stays closed by default; failures offer Problems and
  Output as direct recovery actions.
- Launchable project rows retain Build, Run, and Debug inline; all remaining
  operations are progressively disclosed through Project Actions.
- No replacement Composition Inspector or dashboard was introduced. Native
  JSON, Inspect, Explain, and Diff flows remain the expert surface.
- The single status item shows project and configuration. Target and toolchain
  stay in its tooltip and the Build context group.
- No usage telemetry was added. UX measurement remains an explicit moderated
  study and dogfooding activity.

The primary loop remains:

> Open a source file -> edit -> save -> see diagnostics -> press F5.

## Research basis

This direction applies the following guidance:

- The [UI fundamentals article](https://uxplaybook.org/articles/ui-fundamentals-best-practices-for-ux-designers)
  emphasizes user research, consistency, familiar patterns, simplification,
  progressive disclosure, visual hierarchy, accessibility, feedback, and
  iterative usability testing.
- The official [VS Code UX guidelines](https://code.visualstudio.com/api/ux-guidelines/overview)
  recommend using native workbench surfaces and putting actions in the context
  where they apply.
- The [Views guidance](https://code.visualstudio.com/api/ux-guidelines/views)
  recommends a small number of shallow, data-oriented tree views, no more than
  three actions per item, familiar product icons, and concise welcome content.
- The [Status Bar guidance](https://code.visualstudio.com/api/ux-guidelines/status-bar)
  recommends one short item unless more are genuinely necessary.
- The [Webview guidance](https://code.visualstudio.com/api/ux-guidelines/webviews)
  says webviews should be used only when native APIs cannot express the
  experience, and must be themeable, keyboard accessible, and screen-reader
  friendly.
- The [Quick Pick guidance](https://code.visualstudio.com/api/ux-guidelines/quick-picks)
  supports searchable selections and short multi-step input flows, but not long
  wizard experiences.
- The [Notifications guidance](https://code.visualstudio.com/api/ux-guidelines/notifications)
  recommends protecting attention, keeping progress contextual, and reserving
  modal UI for user-initiated decisions that require an immediate answer.
- Microsoft's [CMake Tools status UI history](https://github.com/microsoft/vscode-cmake-tools/blob/main/docs/cmake-options-configuration.md)
  is a useful adjacent case: it moved configuration controls out of an
  overcrowded status bar and into a project-oriented sidebar surface.

These sources are inputs, not substitutes for observing NGIN users. The first
delivery phase validates the plan's assumptions before expensive UI work.

## Current UX audit

The extension has strong capabilities, but its presentation has accumulated
too many simultaneous interaction models.

### Evidence from the current implementation

- `package.json` contributes 59 commands and 34 project-tree context-menu
  entries.
- `statusBar.ts` creates five persistent left-aligned items: project/context,
  Configure, Build, Run, and Debug. VS Code's guidance is to default to one.
- The Projects view switches between semantic Project and physical Files
  modes. The same location therefore changes meaning, while the native Explorer
  already owns physical file navigation.
- Selecting a project tree row changes the default project. Navigation and a
  state-changing action are coupled.
- The tree can reach four or more semantic levels through Advanced composition,
  then uses leaf rows as Explain commands. This makes graph internals part of
  the normal navigation hierarchy.
- The Project Overview webview repeats readiness, selection, lifecycle actions,
  and status already represented by native surfaces.
- The walkthrough is sensibly limited to four steps, but its media is primarily
  text and does not yet demonstrate the key visual interactions.

### Usability risks to validate

These are hypotheses until tested with users:

- A new user may not know which of the tree, status bar, dashboard, Command
  Palette, Testing view, or F5 is the intended starting point.
- The distinction between active-file owner and manually selected fallback
  project may be technically correct but visually unclear.
- Configuration, target, toolchain, launch, and option selection may feel like
  unrelated commands instead of one build context.
- Successful operations may demand too much attention if Output is revealed for
  every lifecycle command; failed operations may demand too little if the next
  corrective action is buried in Output.
- Advanced composition language may obscure the more common questions: "What
  will build?", "What will run?", and "Why is this file or package here?"

## Users and priority jobs

Design for jobs, not for every CLI command equally.

| User | Primary job | UX priority |
| --- | --- | --- |
| New NGIN user | Reach a successful build and debug session | Readiness, guidance, safe defaults |
| Daily C++ developer | Edit, build, run, debug, and fix errors | Speed, keyboard flow, low noise |
| Multi-project developer | Know and change the effective project/context | Clear ownership and selection |
| Build/tooling expert | Diagnose composition, packages, actions, and staging | Deep detail on demand |

The daily developer loop is primary. Expert inspection remains complete but is
progressively disclosed.

## Design principles

1. **Native first.** Prefer Tree View, Problems, Testing, Run and Debug, Output,
   tasks, Quick Pick, walkthrough, and Settings over custom HTML.
2. **One fact, one home.** Do not show the same state as separate dashboard,
   tree, and status-bar controls.
3. **Context before selection.** The active editor's owning project is the
   effective project. A remembered default is only the fallback.
4. **Actions near objects.** Project actions live on project rows or project
   context menus; file actions live in Explorer/editor menus; tests live in
   Testing; diagnostics live in Problems.
5. **Progressive disclosure.** Common lifecycle actions are immediate. Restore,
   locking, graph inspection, diff, explain, and publishing are one layer
   deeper.
6. **Quiet success, actionable failure.** Progress is visible without stealing
   focus. Failures say what happened and offer the next useful action.
7. **Accessible by construction.** Every workflow works with keyboard and
   screen reader, in light, dark, and high-contrast themes, without relying on
   color alone.
8. **Preserve expert reach.** Reducing persistent UI does not remove Command
   Palette commands or CLI capability.

## Proposed information architecture

### 1. One NGIN activity container, one Projects view

Keep one Activity Bar container and one native **Projects** tree. Remove the
Project/Files mode toggle; the Explorer remains the physical file browser.

The tree shows information, not rows that behave like unlabeled buttons:

```text
NGIN: PROJECTS                         [new] [refresh]

Hello.Native  Application  active file      [build] [run] [debug]
  Manifest
  Product files
    Sources (3)
    Headers (2)
    Resources (1)
  Dependencies (4)
  Launches (1)
  Generated (2)
  Issues (1)

Engine  Library  default                    [build]
  ...
```

Rules:

- A project row selection only selects the row. It does not silently change
  the fallback project.
- `active file`, `default`, and `busy` are explicit text descriptions backed by
  icons; they are never communicated by color alone.
- Project rows expose at most Build, Run, and Debug inline. Non-launchable
  products show only valid actions.
- **Set as Default Project** remains an explicit context-menu and Command
  Palette action.
- Tree expansion is limited to product-oriented data. Composition identities
  and edges leave the daily tree.
- Files open on activation. Semantic nodes reveal their declaring source or
  open a focused detail document; they do not execute lifecycle operations.
- Missing inputs appear under Issues and in Problems with a direct fix or
  source location.
- Physical create/rename/delete/reveal behavior belongs to Explorer. NGIN adds
  only semantic actions there, such as Include in Product and Exclude from
  Product.

### 2. One compact context status item

Replace the five current status-bar items with one workspace-level item:

```text
$(project) Hello.Native · Debug
```

States:

- normal: effective project and configuration;
- active-file ownership: tooltip says why this project is effective;
- fallback: tooltip explicitly says no active file owns a project;
- busy: spinner plus operation, clicking offers Cancel and Show Output;
- issue: warning icon plus short state, clicking opens the corrective action.

Clicking the normal item opens the **NGIN: Project Actions** Quick Pick. The
status bar is context and entry point, not a row of permanent buttons.

### 3. A searchable Project Actions hub

Use one grouped Quick Pick as the compact command center:

```text
Lifecycle       Build | Run | Debug | Test
Build context   Configuration: Debug
                Target: host
                Toolchain: default
                Launch: default
Project         Open Manifest | Set Default | Add Package
Diagnostics     Show Problems | Show Output | Check Setup
Advanced        Configure | Stage | Inspect | Explain | Diff | Publish
```

Requirements:

- Put the current value in each item's description.
- Hide impossible actions rather than letting users discover invalid state
  through errors.
- Keep normal actions one selection away; use multi-step Quick Picks only for
  related input, such as project creation or build-context selection.
- Retain individually searchable `NGIN:` commands for expert and keyboard use.
- Offer **Configure** as an advanced troubleshooting action because Build,
  Run, Debug, Test, and Analyze already configure when needed.

### 4. Native homes for feedback

| Information | Primary home | Secondary detail |
| --- | --- | --- |
| Manifest/compiler/analyzer errors | Problems and editor diagnostics | NGIN Output |
| Tests and results | Testing | Test output |
| Debug state | Run and Debug | Debug Console |
| Background analysis | Status item or Projects view progress | NGIN Output |
| Build/run progress | Project row and single status item | NGIN Output/log file |
| Setup/readiness issue | Projects welcome/issue node | Check Setup Quick Pick |
| Composition facts | Project tree summary | Inspector/JSON/Explain |

Default feedback policy:

- Do not show a success notification for routine build, analysis, or format.
- Do not steal editor focus for background work.
- Keep Output available, but test `never`, `on failure`, and current `on start`
  reveal policies with users before changing the default.
- On failure, show one concise message with at most two useful actions, such as
  **Open Problems** and **Show Output**.
- Use modal confirmation only for destructive user-initiated actions.

### 5. Retire the daily dashboard; reserve rich UI for real complexity

Remove **Project Overview** as a second action/status hub after equivalent
native coverage exists. Do not replace it with another dashboard webview.

Evaluate a separate, on-demand **Composition Inspector** only if research shows
that graph relationships cannot be understood through a native tree plus
virtual documents. Its job would be relational exploration—why an input,
package, action, stage contribution, or launch exists—not Build/Run buttons.

If an inspector is justified, it must:

- open only on explicit request;
- use VS Code theme tokens and Codicons;
- preserve focus, selection, and scroll across updates;
- fully support keyboard navigation, screen readers, 200% zoom, and reduced
  motion;
- provide equivalent commands or documents for essential information;
- load graph details lazily and remain responsive on large workspaces.

## Core journeys

### First open

1. Detect the CLI and projects without opening a modal.
2. If ready, populate Projects and stay quiet.
3. If no project exists, show one primary welcome action, **Create NGIN
   Project**, plus links to **Open Getting Started** and documentation.
4. If the CLI is missing or incompatible, show a single issue state with
   **Check Setup**. The check reports detected path, version, required version,
   and exact remediation.
5. Complete the walkthrough from real events: CLI verified, project found,
   successful build, successful debug or run, and tooling decision.

### Build and recover from failure

1. Resolve the effective project from the active file, then the explicit
   fallback.
2. Start Build from the project row, Project Actions, Command Palette, or task.
3. Show cancellable progress on that project and the single status item.
4. On success, return quietly to ready and retain duration in the tooltip.
5. On failure, populate Problems, mark the project issue, and offer Open
   Problems/Show Output without duplicating the full error in a notification.

### Run or debug

1. F5 and Ctrl+F5 use active-file ownership first.
2. If exactly one launch is valid, proceed without prompting.
3. If several are valid and no remembered choice applies, show a launch Quick
   Pick with executable, arguments summary, and source manifest.
4. Persist the choice per project and configuration.
5. Delegate runtime and debug state to native VS Code surfaces.

### Change build context

1. Open Project Actions from the status item or Command Palette.
2. Show configuration, target, toolchain, and launch as one **Build context**
   group.
3. Change one dimension in a focused Quick Pick with the active value marked.
4. Refresh dependent choices and explain invalidated selections.
5. Persist selections per project; never imply that changing tree selection
   changes build context.

### Work with project files

1. Browse and manage physical files in Explorer.
2. Show generated/missing/semantic state with restrained decorations and
   accessible tooltips.
3. Offer Include/Exclude/Analyze/Format/Reveal Owning Project only when valid.
4. Keep **New C++ Source/Header** as semantic creation flows that create the file
   and update the manifest; start them from a project context menu, Explorer,
   or Command Palette.

## Visual and content system

- Use VS Code theme colors and Codicons; add custom artwork only for the NGIN
  Activity Bar identity and walkthrough illustrations.
- Use sentence case and verb-first commands: **Build Project**, **Select
  Configuration**, **Open Manifest**.
- Use `Project`, `Configuration`, `Target`, `Toolchain`, and `Launch` consistently.
  Do not alternate between selected, active, current, and effective without
  defining the distinction.
- Put the outcome first in messages: `Build failed: 2 errors.` Put paths,
  commands, and backend detail in Output.
- Avoid branded colors for state. Use VS Code warning/error tokens and pair
  icon/color with text.
- Design at narrow sidebar widths first. Labels truncate; essential state stays
  available in the accessible name and tooltip.

## Accessibility acceptance criteria

- Every command and state transition is reachable without a mouse.
- Tree item accessible names include project name, product type, context state,
  and busy/error state when applicable.
- Refreshes preserve focus and expansion where the underlying item still
  exists.
- No essential distinction relies only on color, position, hover, or animation.
- Light, dark, high-contrast light, and high-contrast dark themes are verified.
- UI remains usable at 200% zoom and a 240-pixel sidebar width.
- Walkthrough media has alternative text and works in all supported themes.
- Notifications and progress updates do not produce repetitive screen-reader
  announcements.
- Any retained webview passes automated accessibility checks and a manual
  keyboard/screen-reader pass on Windows and one non-Windows platform.

## Delivery plan

### Phase 0: Baseline and user validation

Deliverables:

- Recruit 5-8 participants across new, daily, multi-project, and expert use.
- Record the current experience for six benchmark tasks: first build, debug the
  active file, switch configuration, recover from a build error, add a source
  file, and explain a dependency.
- Capture completion rate, time on task, wrong-surface visits, prompts opened,
  and a one-question ease score after each task.
- Test low-fidelity versions of the proposed Projects view, single status item,
  and Project Actions hub.
- Decide the Output reveal default and whether a Composition Inspector is
  justified.

Exit gate: at least 80% of participants correctly identify the effective
project and the way to Build/Debug without coaching in the prototype.

### Phase 1: Native shell and information architecture

Primary files: `package.json`, `src/ui/tree.ts`, `src/ui/statusBar.ts`,
`src/extension.ts`.

- Remove Files mode and its view-title toggle.
- Remove selection-as-default side effects from the tree.
- Reduce the tree to product-oriented data and two-to-three useful levels.
- Replace five status-bar items with the single context item.
- Add the grouped Project Actions Quick Pick.
- Preserve existing command IDs where practical so keybindings and tasks do not
  break; deprecate rather than abruptly remove externally usable commands.

Exit gate: the benchmark Build, Run, Debug, and project-selection flows work
without the dashboard or Files mode.

### Phase 2: Contextual actions and file workflow

Primary files: `package.json`, `src/ui/tree.ts`, `src/providers/fileDecorations.ts`,
`src/core/projectOwnership.ts`, `src/extension.ts`.

- Move physical file operations to Explorer and remove duplicated tree menus.
- Audit every context-menu `when` clause so only valid semantic actions appear.
- Make active-file ownership, fallback, ambiguity, and busy state explicit.
- Consolidate configuration/target/toolchain/launch selection under Build
  context.
- Keep project creation and semantic C++ file creation as short, validated
  multi-step flows with a final summary before authored-file changes.

Exit gate: each tested task has one obvious primary path and no participant
mistakes row selection for changing the fallback project.

### Phase 3: Feedback, onboarding, and error recovery

Primary files: `src/core/outputPresentation.ts`, `src/core/controller.ts`,
`src/providers/sourceAnalysis.ts`, `package.json`, `media/walkthrough/`.

- Implement the agreed quiet-success/actionable-failure policy.
- Add setup/readiness diagnostics with direct remediation actions.
- Make cancellation and stale/cached state visible in project context.
- Refresh the four-step walkthrough with theme-aware visual media and real
  completion events.
- Remove repeated or non-actionable notifications and give persistent prompts a
  **Don't Show Again** path where appropriate.

Exit gate: users recover from the benchmark CLI-missing and build-failure cases
without searching raw Output first.

### Phase 4: Advanced inspection and dashboard retirement

Primary files: `src/ui/dashboard.ts`, graph/inspect commands, documentation.

- Remove Project Overview after native replacement coverage is verified.
- Keep Open Resolved Project JSON, Inspect, Explain, and Diff available under
  Advanced and in the Command Palette.
- Prototype Composition Inspector only if Phase 0 evidence supports it.
- Update the extension README and screenshots to describe one coherent model.

Exit gate: experts can answer why a dependency or input exists within the
baseline time, while daily users do not encounter graph vocabulary in their
normal loop.

### Phase 5: Accessibility, performance, and release validation

- Add unit coverage for action-hub grouping, context labels, command visibility,
  and status transitions.
- Add extension-host coverage for keyboard command paths, welcome states,
  project-row behavior, status-bar behavior, cancellation, and focus retention.
- Test multi-root workspaces, no-folder windows, remote workspaces, large
  project trees, missing/incompatible CLI, libraries without Launch intent,
  multiple launch definitions, and ambiguous file ownership.
- Run the accessibility matrix above and verify no regressions in manifest
  completion, diagnostics, Testing, C++ configuration, tasks, or debugging.
- Package and dogfood the VSIX for one normal development cycle before making
  the refreshed UX the only experience.

Exit gate: all critical journeys pass, no critical accessibility issue remains,
and benchmark results meet the success measures below.

## Success measures

Compare against the Phase 0 baseline:

- At least 90% unassisted completion for first build and debug-active-file.
- At least 30% lower median time to identify/change effective project and
  configuration.
- At least 50% fewer wrong-surface visits during the six benchmark tasks.
- At least 80% of participants rate each daily task 5 or better on the 7-point
  Single Ease Question.
- Zero persistent status-bar items beyond the single NGIN context item by
  default.
- Zero duplicated physical file-management commands in the NGIN project tree.
- Zero critical keyboard, screen-reader, high-contrast, or 200%-zoom defects.
- No regression in extension activation time or project-tree refresh latency;
  set concrete budgets from Phase 0 measurements rather than inventing them.

## Migration and rollout

- Keep semantic behavior and CLI command construction unchanged while the UI
  shell moves.
- Preserve stable command IDs, Settings keys, task types, debug type, and stored
  per-project choices unless a specific migration is documented.
- Introduce the refresh behind a temporary `ngin.experimental.nativeUx` setting
  only if dogfooding cannot be done through prerelease VSIX builds. Do not keep
  a permanent parallel UI.
- Announce removed or relocated surfaces once, with **Open Updated UI** and
  **Read What's New** actions plus **Don't Show Again**.
- Remove the old dashboard and Files mode after one validated transition cycle;
  do not maintain two interaction models indefinitely.

## Decisions to confirm after Phase 0

1. Should routine lifecycle Output reveal `never`, `on failure`, or `on start`?
2. Do users need Build/Run/Debug inline on every project, or only on the
   effective project?
3. Is a relational Composition Inspector materially better than native
   tree/virtual-document inspection for expert tasks?
4. Should the single status item show configuration only, or a compact
   `configuration/target` pair when the target is non-default?
5. Is opt-in usage telemetry acceptable for UX measurement, or should all
   evaluation remain in moderated studies and dogfooding?

These questions should be answered with task evidence before implementation
locks in the corresponding behavior.
