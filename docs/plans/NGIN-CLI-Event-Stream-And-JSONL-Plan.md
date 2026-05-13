# NGIN CLI Event Stream And JSONL Output Plan

Status: Implemented

Implementation note: the `NGIN.CLI.Event` `1.0` JSONL stream is implemented
for configure, build, stage, rebuild, publish, analyze, restore, run, test,
benchmark, package pack, and package lock. Human lifecycle output for these
commands is rendered from events, backend stream mode emits incremental output
events, and VS Code consumes JSONL for long-running command progress and
analyzer diagnostics.

## Purpose

NGIN now has a modern human output baseline: compact command summaries,
backend phase timing, optional backend streaming, verbose resolved details, and
VS Code progress notifications.

The next step is to stop treating command output as handwritten text emitted
directly from each command. Build, configure, stage, run, analyze, publish, and
restore should emit structured lifecycle events. Human text, compact terminal
output, VS Code progress, diagnostics, and machine-readable logs should all be
renderers over the same event stream.

The goal is:

```text
Commands produce events.
Renderers decide presentation.
Editors and CI consume stable structured output.
Human output can evolve without breaking tools.
```

## Current Baseline

The current CLI supports:

- `--quiet`
- `--verbose`
- `--trace`
- `--plain`
- `--color auto|always|never`
- `--ui auto|pretty|compact|plain|json`
- `--backend-output stream|compact|silent`

Build-backed commands already collect backend step timing through
`BackendStepResult` and can hide CMake/Ninja output on success.

The limitation is that output is still assembled imperatively in command
handlers and backend helpers. VS Code still receives text and parses diagnostic
lines. There is no stable event contract for progress, phases, backend logs,
artifacts, or diagnostics.

## Goals

- Introduce a stable CLI event model for command lifecycle, phases, backend
  steps, diagnostics, artifacts, and summaries.
- Add JSON Lines output for machine consumers.
- Keep current compact human output behavior.
- Make VS Code consume events for progress and diagnostics where possible.
- Keep analyzer diagnostic text stable until VS Code fully consumes structured
  diagnostics.
- Allow terminal output to become richer without changing command semantics.
- Make failures easier to diagnose by attaching backend output to failed
  phases.

## Non-Goals

- Do not redesign the V4 Composition Graph JSON schema in this slice.
- Do not replace `ngin inspect --format json`; inspect remains the graph API.
- Do not require every command to become event-native in the first commit.
- Do not implement remote telemetry.
- Do not remove existing text output flags until event output is proven.
- Do not make VS Code depend on animated terminal control sequences.

## Terminology

### Event

A structured record emitted by command execution.

Examples:

- command started
- phase started
- backend output captured
- diagnostic produced
- artifact produced
- command completed

### Phase

A named part of a command lifecycle.

Recommended initial phases:

- `resolve`
- `restore`
- `generate`
- `configure`
- `build`
- `stage`
- `analyze`
- `publish`
- `run`
- `clean`

### Renderer

A consumer that turns events into output or UI.

Initial renderers:

- compact human renderer
- verbose human renderer
- plain human renderer
- JSONL renderer
- VS Code event consumer

### JSONL

JSON Lines. Each event is one JSON object followed by `\n`.

This is easier to stream than one large JSON document and allows tools to react
while the command is still running.

## Event Envelope

Every event should share a common envelope:

```json
{
  "schemaVersion": "1.0",
  "kind": "NGIN.CLI.Event",
  "sequence": 1,
  "timestamp": "2026-05-12T00:00:00.000Z",
  "type": "phase.started",
  "command": "build",
  "project": "Hello.Hosted",
  "profile": "Debug",
  "data": {}
}
```

Fields:

- `schemaVersion`: event contract version, independent of manifest schema
- `kind`: always `NGIN.CLI.Event`
- `sequence`: monotonically increasing per CLI process
- `timestamp`: UTC ISO-8601 timestamp
- `type`: event type
- `command`: command name when known
- `project`: product/project name when known
- `profile`: selected profile when known
- `data`: type-specific payload

## Initial Event Types

### `command.started`

Emitted once command execution starts after CLI argument parsing.

```json
{
  "type": "command.started",
  "data": {
    "argv": ["build", "--project", "Examples/Hello.Hosted/Hello.Hosted.nginproj"],
    "workingDirectory": "/repo"
  }
}
```

### `command.selection`

Emitted after project/profile resolution.

```json
{
  "type": "command.selection",
  "data": {
    "projectPath": "Examples/Hello.Hosted/Hello.Hosted.nginproj",
    "productKind": "Application",
    "hostPlatform": "host",
    "targetPlatform": "linux-x64",
    "buildType": "Debug",
    "toolchain": "clang-lld"
  }
}
```

### `phase.started`

Emitted before a phase begins.

```json
{
  "type": "phase.started",
  "data": {
    "phase": "configure",
    "label": "CMake configure"
  }
}
```

### `phase.completed`

Emitted when a phase succeeds.

```json
{
  "type": "phase.completed",
  "data": {
    "phase": "configure",
    "label": "CMake configure",
    "durationMs": 1803
  }
}
```

### `phase.failed`

Emitted when a phase fails.

```json
{
  "type": "phase.failed",
  "data": {
    "phase": "build",
    "label": "CMake build",
    "durationMs": 4200,
    "exitCode": 1
  }
}
```

### `backend.output`

Emitted for backend output. In compact mode this may be emitted only on failure.
In stream or JSONL mode it can be emitted incrementally.

```json
{
  "type": "backend.output",
  "data": {
    "phase": "build",
    "stream": "stdout",
    "text": "[1/3] Building CXX object ...\n"
  }
}
```

### `diagnostic`

Normalized diagnostic event.

```json
{
  "type": "diagnostic",
  "data": {
    "severity": "warning",
    "source": "clang-tidy",
    "code": "readability-magic-numbers",
    "message": "42 is a magic number; consider replacing it with a named constant",
    "file": "/repo/Examples/Hello.Analyzer/src/main.cpp",
    "line": 8,
    "column": 16
  }
}
```

### `artifact.produced`

Emitted for important command outputs.

```json
{
  "type": "artifact.produced",
  "data": {
    "kind": "executable",
    "name": "Hello.Hosted",
    "path": "/tmp/ngin-render-demo/bin/Hello.Hosted"
  }
}
```

Other artifact kinds:

- `launch-manifest`
- `compile-database`
- `stage-directory`
- `package-manifest`
- `package-archive`
- `publish-directory`
- `publish-archive`
- `lock-file`

### `summary`

Emitted near command completion with a compact summary payload.

```json
{
  "type": "summary",
  "data": {
    "output": "/tmp/ngin-render-demo",
    "launch": "/tmp/ngin-render-demo/Hello.Hosted.Debug.nginlaunch",
    "executable": "Hello.Hosted",
    "packages": 4,
    "sources": 1,
    "headers": 0,
    "stagedFiles": 1
  }
}
```

### `command.completed`

Emitted once at process end.

```json
{
  "type": "command.completed",
  "data": {
    "status": "success",
    "exitCode": 0,
    "durationMs": 13310
  }
}
```

## CLI Contract

Add event output flags without breaking current human output.

Recommended flags:

```bash
ngin build --events jsonl
ngin build --events none
```

Potential aliases:

```bash
ngin build --format jsonl
ngin build --ui jsonl
```

Recommended behavior:

- default: current compact human output
- `--events jsonl`: emit JSONL events to stdout
- `--events jsonl --backend-output stream`: include backend output events
- `--events jsonl --backend-output compact`: include backend output events only
  on failure
- `--events jsonl --quiet`: still emit events, because events are the selected
  output format
- `--plain`: affects human renderer only, not JSONL
- `--color`: affects human renderer only, not JSONL

Important rule:

```text
When JSONL is selected, stdout must contain only JSONL events.
Human messages and backend passthrough logs must not be mixed into stdout.
```

If non-event text is still needed in JSONL mode, send it to stderr only when it
does not break consumers, or represent it as `backend.output` or `diagnostic`
events.

## Renderer Architecture

Introduce a small event sink abstraction in the CLI.

Sketch:

```cpp
enum class CliEventType
{
    CommandStarted,
    CommandSelection,
    PhaseStarted,
    PhaseCompleted,
    PhaseFailed,
    BackendOutput,
    Diagnostic,
    ArtifactProduced,
    Summary,
    CommandCompleted,
};

struct CliEvent
{
    std::uint64_t sequence{};
    std::chrono::system_clock::time_point timestamp{};
    CliEventType type{};
    std::string command{};
    std::string project{};
    std::string profile{};
    JsonObject data{};
};

class ICliEventSink
{
public:
    virtual ~ICliEventSink() = default;
    virtual void Emit(const CliEvent &event) = 0;
};
```

If the repo does not yet have a reusable JSON object type appropriate for this,
phase one can use a simpler payload builder:

```cpp
struct EventField
{
    std::string name;
    std::string value;
};

struct CliEvent
{
    std::string type;
    std::vector<EventField> fields;
};
```

But the implementation should avoid stringly-typed JSON assembly scattered
through command handlers.

### Human Renderer

Consumes events and produces current compact text.

Responsibilities:

- title and selection fields
- phase timing summary
- backend output on failure
- success/failure summary
- verbose sections
- terminal progress line for interactive sessions

### JSONL Renderer

Consumes events and writes one JSON object per event.

Responsibilities:

- escaping strings correctly
- preserving event order
- flushing after each event for live consumers
- never writing non-JSON text to stdout

### Composite Sink

Most command executions should use one sink, but tests may benefit from a
composite sink:

```cpp
class CompositeEventSink : public ICliEventSink
{
    std::vector<ICliEventSink *> sinks;
};
```

This allows one command execution to both render human text and record events
for assertions during migration.

## Integration Points

### Command Layer

Current command functions should stop printing lifecycle text directly over
time.

Initial target commands:

1. `configure`
2. `build`
3. `stage`
4. `rebuild`
5. `publish`
6. `analyze`

Later:

7. `restore`
8. `package pack`
9. `test`
10. `benchmark`
11. `run`

### Build Layer

The build layer currently records backend steps. Replace or augment
`BackendStepResult` with emitted events:

- emit `phase.started` before configure/build backend process
- emit `backend.output` as data is captured or streamed
- emit `phase.completed` or `phase.failed`

Phase one can continue capturing output as a string and emit output at phase end.
Phase two should stream backend output events incrementally.

### Analyzer Layer

`ngin analyze` currently prints normalized diagnostic lines.

Migration path:

1. Keep existing text lines for human/plain output.
2. Emit `diagnostic` events for each clang-tidy finding.
3. Teach VS Code to prefer JSONL diagnostics when available.
4. Keep text parser as fallback until the event path is stable.

### VS Code Extension

The extension should eventually invoke:

```bash
ngin build ... --events jsonl --backend-output compact
```

Then it can:

- update progress from `phase.started` / `phase.completed`
- show elapsed active phase time
- update Problems from `diagnostic`
- update output links from `artifact.produced`
- display backend output only on failure
- refresh project tree after `command.completed`

Do not put spinner control sequences in the Output channel. Use VS Code
progress notifications and status bar items.

## Implementation Plan

### Step 1: Add Event Model

Add new files:

```text
Tools/NGIN.CLI/src/Events.hpp
Tools/NGIN.CLI/src/Events.cpp
```

Include:

- event type constants
- event envelope
- event sink interface
- null sink
- JSONL sink
- human sink or human renderer adapter
- helper functions for common event payloads

Acceptance:

- CLI compiles with event model available.
- Unit tests can instantiate a recording event sink.

### Step 2: Add CLI Event Flags

Extend `ParsedArgs`:

```cpp
enum class EventOutputMode
{
    None,
    JsonLines,
};

EventOutputMode eventOutputMode{EventOutputMode::None};
```

Parse:

```text
--events jsonl
--events none
```

Acceptance:

- `ngin build --events jsonl` selects JSONL mode.
- invalid mode reports a clear argument error.

### Step 3: Create Command Execution Context

Introduce a context passed through command and build layers:

```cpp
struct CommandExecutionContext
{
    ParsedArgs args;
    ICliEventSink *events{};
    std::string command;
    std::string project;
    std::string profile;
};
```

Keep this small. Do not turn it into a global service locator.

Acceptance:

- `configure` and `build` can access an event sink without global state.

### Step 4: Emit Command Events

For `build`:

- `command.started`
- `command.selection`
- `summary`
- `artifact.produced`
- `command.completed`

Acceptance:

```bash
ngin build ... --events jsonl
```

emits valid JSONL with command start and completion events.

### Step 5: Emit Backend Phase Events

Update generated CMake configure/build process execution:

- `phase.started configure`
- `backend.output` for captured configure output
- `phase.completed configure`
- `phase.started build`
- `backend.output` for captured build output
- `phase.completed build`

On failure:

- `phase.failed`
- backend output event must be present in compact mode
- command completion status must be `failed`

Acceptance:

- successful compact JSONL contains phase timings but no backend output unless
  explicitly requested.
- failed compact JSONL contains backend output.

### Step 6: Preserve Human Output

Keep current human behavior by implementing the compact renderer over events or
by temporarily emitting both events and existing text from the same command
paths.

Acceptance:

- existing focused CLI tests pass.
- direct `ngin build` still shows compact output by default.
- `--verbose` still shows resolved details.
- `--backend-output stream` still streams backend logs.

### Step 7: Add VS Code JSONL Consumer

Add a streaming parser in the extension:

```text
Tools/NGIN.VSCode/src/core/events.ts
```

Responsibilities:

- parse one JSON object per line
- ignore incomplete trailing line until more data arrives
- validate `kind === "NGIN.CLI.Event"`
- expose typed event helpers

Update `runCli`:

- for build/configure/stage/rebuild/publish/analyze, request `--events jsonl`
  once CLI support exists
- update progress notifications from events
- append stable human summaries to Output panel using extension-side rendering
  or selected event text
- apply diagnostics from `diagnostic` events

Acceptance:

- VS Code build progress changes from generic elapsed seconds to phase-specific
  messages like `Configuring`, `Building`, `Staging`.
- Analyzer diagnostics do not depend on text regex when JSONL is available.

### Step 8: Add Tests

CLI tests:

- JSONL output is valid one-object-per-line JSON.
- build emits `command.started`, `command.selection`, phase events,
  `artifact.produced`, `summary`, and `command.completed`.
- compact success does not include backend output events by default.
- backend stream mode includes backend output events.
- failed backend includes backend output in compact mode.
- human output remains stable enough for existing tests.

VS Code tests:

- JSONL parser handles split chunks.
- JSONL parser rejects non-event JSON cleanly.
- build event stream updates progress state.
- diagnostic event maps to VS Code diagnostic with file, line, column, severity,
  source, and code.
- fallback text diagnostic parser still works.

### Step 9: Document The Contract

Add documentation to:

- `Tools/NGIN.VSCode/README.md`
- `docs/specs/006-cli-contract.md` or a V4 replacement CLI spec
- `docs/plans/NGIN-V4-Implementation-Progress.md`

Document:

- JSONL is the stable event stream format.
- human output is not the editor integration contract.
- event schema versioning policy.

## Example JSONL Build

Command:

```bash
ngin build \
  --project Examples/Hello.Hosted/Hello.Hosted.nginproj \
  --profile Debug \
  --output /tmp/ngin-render-demo \
  --events jsonl
```

Example output:

```jsonl
{"schemaVersion":"1.0","kind":"NGIN.CLI.Event","sequence":1,"timestamp":"2026-05-12T00:00:00.000Z","type":"command.started","command":"build","data":{"workingDirectory":"/repo"}}
{"schemaVersion":"1.0","kind":"NGIN.CLI.Event","sequence":2,"timestamp":"2026-05-12T00:00:00.012Z","type":"command.selection","command":"build","project":"Hello.Hosted","profile":"Debug","data":{"projectPath":"Examples/Hello.Hosted/Hello.Hosted.nginproj","productKind":"Application","targetPlatform":"linux-x64","buildType":"Debug"}}
{"schemaVersion":"1.0","kind":"NGIN.CLI.Event","sequence":3,"timestamp":"2026-05-12T00:00:00.020Z","type":"phase.started","command":"build","project":"Hello.Hosted","profile":"Debug","data":{"phase":"configure","label":"CMake configure"}}
{"schemaVersion":"1.0","kind":"NGIN.CLI.Event","sequence":4,"timestamp":"2026-05-12T00:00:01.823Z","type":"phase.completed","command":"build","project":"Hello.Hosted","profile":"Debug","data":{"phase":"configure","label":"CMake configure","durationMs":1803}}
{"schemaVersion":"1.0","kind":"NGIN.CLI.Event","sequence":5,"timestamp":"2026-05-12T00:00:01.824Z","type":"phase.started","command":"build","project":"Hello.Hosted","profile":"Debug","data":{"phase":"build","label":"CMake build"}}
{"schemaVersion":"1.0","kind":"NGIN.CLI.Event","sequence":6,"timestamp":"2026-05-12T00:00:13.240Z","type":"phase.completed","command":"build","project":"Hello.Hosted","profile":"Debug","data":{"phase":"build","label":"CMake build","durationMs":11416}}
{"schemaVersion":"1.0","kind":"NGIN.CLI.Event","sequence":7,"timestamp":"2026-05-12T00:00:13.260Z","type":"artifact.produced","command":"build","project":"Hello.Hosted","profile":"Debug","data":{"kind":"launch-manifest","path":"/tmp/ngin-render-demo/Hello.Hosted.Debug.nginlaunch"}}
{"schemaVersion":"1.0","kind":"NGIN.CLI.Event","sequence":8,"timestamp":"2026-05-12T00:00:13.261Z","type":"artifact.produced","command":"build","project":"Hello.Hosted","profile":"Debug","data":{"kind":"executable","name":"Hello.Hosted","path":"/tmp/ngin-render-demo/bin/Hello.Hosted"}}
{"schemaVersion":"1.0","kind":"NGIN.CLI.Event","sequence":9,"timestamp":"2026-05-12T00:00:13.262Z","type":"command.completed","command":"build","project":"Hello.Hosted","profile":"Debug","data":{"status":"success","exitCode":0,"durationMs":13262}}
```

## Failure Behavior

For backend failures, JSONL compact mode must include enough information to
diagnose the failure:

```jsonl
{"type":"phase.failed","data":{"phase":"build","label":"CMake build","durationMs":4200,"exitCode":1}}
{"type":"backend.output","data":{"phase":"build","stream":"combined","text":"compiler error..."}}
{"type":"command.completed","data":{"status":"failed","exitCode":1,"durationMs":4300}}
```

Human compact output should render the same failure as:

```text
NGIN build
  product  Hello.Hosted
  profile  Debug

Backend
  - CMake configure complete  1.8s
  - CMake build failed        4.2s

Backend output
  [CMake build]
    compiler error...

Build failed
```

## Versioning Policy

The event stream has its own schema version:

```text
NGIN.CLI.Event schemaVersion 1.0
```

Compatibility rules:

- Adding optional fields is allowed in `1.x`.
- Removing fields requires `2.0`.
- Renaming event types requires `2.0`.
- Consumers must ignore unknown event types.
- Consumers must ignore unknown fields.
- Producers should not reorder semantically dependent events.

## Open Decisions

- Whether `--ui jsonl` should remain an alias for `--events jsonl`.
- Whether backend output events should split stdout/stderr or use a combined
  stream everywhere.
- Whether event JSON should include absolute paths, workspace-relative paths, or
  both.
- Whether JSONL should include full resolved graph fragments or only references
  to `ngin inspect`.
- Whether all event output should go to stdout and all human errors to stderr in
  JSONL mode.
- Whether command events should include process ID and NGIN CLI version.

## Acceptance Gates

### Gate 1: Event Model Exists

- CLI has event model and JSONL sink.
- `ngin build --events jsonl` emits valid event JSONL.
- Existing human output still works.

### Gate 2: Build Events Are Useful

- build/configure/stage/publish/analyze emit phase and artifact events.
- backend failures include backend output in compact JSONL mode.
- focused CLI tests cover event order and required fields.

### Gate 3: VS Code Uses Events

- VS Code uses JSONL for build/analyze progress.
- analyzer diagnostics can come from `diagnostic` events.
- text parsing remains fallback only.

### Gate 4: Human Output Is Renderer-Owned

- command handlers no longer directly assemble most lifecycle output.
- compact, verbose, plain, and JSONL modes are renderer choices over the same
  events.
