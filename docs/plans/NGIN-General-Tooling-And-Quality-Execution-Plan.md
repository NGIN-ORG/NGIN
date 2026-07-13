# NGIN General Tooling And Quality Execution Plan

Status: Implemented as the active V4 tooling contract

Breaking-change policy: approved for this work. NGIN has not shipped the
phase-one analyzer contract, so this plan replaces that contract in place. Do
not add compatibility parsing, silent fallbacks, or migration aliases for the
current `Quality/Analyzer` shape.

## Summary

NGIN needs one general framework for resolving, planning, executing, and
presenting development tools. Static analyzers are the first important use
case, but the framework must also support formatters, linters, scanners,
source transformers, documentation tools, report generators, coverage
processors, benchmark post-processors, and future custom tooling without
teaching the Composition Graph or editor about each executable.

The framework should have this shape:

```text
Packages export tools and versioned drivers.
Projects, profiles, package features, and workspaces select tool actions.
Resolution produces one graph-owned Tool Execution Plan.
A generic scheduler executes the plan through a versioned driver protocol.
Drivers emit structured diagnostics, edits, artifacts, metrics, and progress.
CLI, terminal, CI, and editors render the same normalized results.
```

The current implementation has several good foundations that should remain:

- package-contributed development tooling
- project/profile/workspace overlay semantics
- graph identity, selection, provenance, diff, and explanation
- host tool resolution through packages, bundled tools, environment overrides,
  and `PATH`
- structured CLI JSONL events
- separate validation, inspection, and tooling diagnostics in VS Code

The current implementation should not remain as the execution contract:

- execution is inferred from the string `clang-tidy`
- unsupported enabled analyzers are silently skipped
- analyzer scope is recorded but does not select inputs
- manifest severity replaces a tool's intrinsic diagnostic severity
- a nonzero tool exit can be accepted when parseable findings exist
- source selection is separate from the configured compilation database
- execution is serial, non-cancellable, and uncached
- diagnostics cannot describe ranges, related locations, fixes, or stable
  fingerprints
- the VS Code extension owns one global analyzer result set and contains
  clang-tidy-specific authoring commands

This plan replaces those behaviors with general contracts rather than adding
more special cases to the phase-one runner.

## Goals

- Make tools, actions, invocations, inputs, policies, and results explicit
  graph objects.
- Support arbitrary tool families through a versioned, host-executed driver
  protocol.
- Keep semantic commands such as `ngin analyze` and `ngin format` while making
  them filters over the same execution framework.
- Use resolved build and compilation-unit data rather than rediscovering source
  files inside individual runners.
- Separate intrinsic diagnostic severity, display mapping, and gate policy.
- Treat tool execution failures separately from findings.
- Support structured diagnostics, related locations, edits, artifacts,
  reports, metrics, and progress.
- Make execution parallel, cancellable, deterministic, and optionally cached.
- Make workspace/project/profile/package overlays work by stable tool-run
  identity with normal V4 provenance.
- Give VS Code a general tooling UI with correct per-project result ownership,
  cancellation, active-file execution, and code actions.
- Make CI output deterministic and support baselines and SARIF without making
  SARIF the internal protocol.
- Keep host and target platform identities explicit for cross-compilation.
- Preserve secret redaction and require explicit access to sensitive values.

## Non-Goals

- Do not make every build backend command a tool action. Configure, compile,
  link, stage, launch, and package restore remain owned by their existing
  graph plans and lifecycle commands.
- Do not replace generators in the first implementation. Generators may later
  reuse the driver and scheduler infrastructure, but generated-file ordering
  remains part of the build/generate plan.
- Do not define a shell-script language in manifests.
- Do not parse arbitrary human output using manifest-authored regular
  expressions.
- Do not allow a package to execute merely because it exists in a catalog. A
  tool must be selected into the resolved graph.
- Do not send secrets to a tool unless the action declares the requirement and
  policy authorizes it.
- Do not automatically apply source modifications. Fix and format application
  is always an explicit operation.
- Do not preserve the phase-one analyzer schema or its name-based execution
  inference.

## Design Principles

### Semantic Authoring, General Execution

Users should be able to understand why a tool runs. Authoring therefore names
semantic action kinds such as `Analyze`, `Format`, `Scan`, `Transform`,
`Report`, and `Custom`. Internally, every kind normalizes to the same tool-run
and execution-plan model.

Semantic CLI commands are views over action kinds:

```text
ngin analyze  -> selected runs whose kind is Analyze or Scan
ngin format   -> selected runs whose kind is Format
ngin quality  -> selected runs that participate in quality gates
ngin tool run -> an explicitly named run of any kind
```

No command identifies implementation by executable name.

### Graph Before Execution

No command should reconstruct tooling behavior directly from authored XML.
Resolution first produces the same graph-owned tool plan for graph, inspect,
diff, explain, CLI execution, CI, and editor tooling.

### Drivers Adapt Tools

NGIN should normalize structured driver output, not embed parsers for every
third-party program in the central command handler. A driver may invoke a
third-party executable, use a library, or implement a tool directly. The
driver boundary is a versioned protocol.

### No Shell Expansion

Executables and argument vectors are passed directly to process APIs. Driver
requests contain typed fields and arrays. NGIN never concatenates a command
line for shell evaluation.

### Findings Are Not Execution Failures

A tool process can execute successfully and report findings. Conversely, a
tool can fail before producing any meaningful finding. The normalized result
must preserve that distinction:

```text
execution status: succeeded | failed | cancelled | timed-out | unavailable
gate status: passed | failed | not-evaluated
findings: zero or more diagnostics
```

### Reproducibility Is Observable

The graph and result summary should record the selected package, tool, driver,
resolved executable source, tool version when available, configuration digest,
input digest, and cache status. Local `PATH` tools remain supported but must be
reported as such.

## Terminology

### Tool

A host-executed capability exported by a package or explicitly resolved as a
system tool. A tool has a stable name, provider identity, executable
resolution, version information, host compatibility, and capabilities.

### Driver

The adapter that implements an NGIN tool protocol. It receives a normalized
request and emits normalized events. The driver may be the tool executable
itself or a wrapper around another executable.

### Action

A reusable operation exported by a tooling package. It binds a tool and driver
to an action kind, accepted input contracts, supported result capabilities,
default configuration, and default execution behavior.

### Run

The selected project/workspace/profile instance of an action. A run supplies
identity, input selection, configuration, policy, execution overrides, and
output/report declarations.

### Input Set

A resolved, typed collection supplied to a run. Initial contracts are files,
translation units, build artifacts, staged artifacts, and prior tool results.

### Finding

A normalized diagnostic produced by a run. A finding is distinct from an
execution diagnostic such as a missing executable or malformed driver event.

### Edit Set

One or more validated file edits proposed by a tool. Edits include the source
file digest they were calculated against and are not applied automatically.

### Gate

Policy evaluation over normalized results. A gate decides command/CI success;
it does not rewrite the intrinsic severity of a finding.

## Package Contract

Tooling packages export tools, drivers, and actions. The exact element names
may be adjusted while implementing the schema, but the normalized concepts and
separation are required.

```xml
<Package SchemaVersion="4"
         Name="Vendor.Tooling.CppAudit"
         Version="1.2.0">
  <Tool Name="cpp-audit"
        Kind="Development">
    <Executable Name="cpp-audit"
                HostPlatform="$(HostPlatform)" />
  </Tool>

  <ToolDriver Name="cpp-audit-driver"
              Protocol="NGIN.ToolDriver/1"
              Executable="ngin-cpp-audit-driver">
    <Capabilities>
      <Capability Name="diagnostics" />
      <Capability Name="related-locations" />
      <Capability Name="fixes" />
      <Capability Name="sarif" />
    </Capabilities>
  </ToolDriver>

  <ToolActions>
    <Action Name="analyze"
            Kind="Analyze"
            Tool="cpp-audit"
            Driver="cpp-audit-driver">
      <Accepts Contract="cpp.translation-units/v1" />
      <Defaults>
        <Input Scope="Product" />
      </Defaults>
    </Action>
  </ToolActions>

  <Features>
    <Feature Name="Analyzer">
      <Tooling>
        <Run Name="cpp-static-analysis"
             Action="Vendor.Tooling.CppAudit::analyze" />
      </Tooling>
    </Feature>
  </Features>
</Package>
```

Required rules:

- Tool, driver, action, and run names are stable identities within their
  owning scope.
- An action references one driver protocol major version.
- A driver executable always runs on `HostPlatform`; its inputs may describe a
  different target platform.
- A tool package declares capabilities. NGIN rejects unsupported requested
  behavior during resolution rather than dropping it at execution time.
- Executable discovery, environment overrides, and bundled resolution use the
  existing tool-resolution service, generalized to fully qualified tool
  identity.
- Package selection and trust happen before any driver process starts.
- Package features may contribute default runs, but consuming projects and
  profiles can replace or remove them by run identity.

### System Tool Wrappers

A system-wrapper package does not need to redistribute the underlying tool. It
can export a driver plus a system executable requirement:

```xml
<Tool Name="cpp-audit" Kind="Development">
  <SystemExecutable Name="cpp-audit"
                    OverrideEnvironment="VENDOR_CPP_AUDIT" />
</Tool>
```

The graph must show whether resolution came from a package payload, bundled
tool root, environment override, or `PATH`.

### Driver Distribution Decision

The general framework must not require the NGIN CLI to contain a parser for
each tool. Official tool packages should eventually ship protocol-speaking
drivers as host tools. During bootstrap, an adapter may be built in to the CLI,
but it still registers through the same driver interface and must not be
selected by comparing display or executable names.

## Project And Workspace Authoring

Product, profile, workspace-profile, product-kind workspace policy, and package
feature contributions all normalize to `ToolRunDefinition`.

```xml
<Application>
  <Uses>
    <Package Name="Vendor.Tooling.CppAudit"
             Version="[1.2.0,2.0.0)"
             Scope="Dev">
      <Feature Name="Analyzer" />
    </Package>
  </Uses>

  <Tooling>
    <Run Name="cpp-static-analysis"
         DisplayName="C++ Static Analysis"
         Description="Check C++ sources for correctness and maintainability."
         Action="Vendor.Tooling.CppAudit::analyze">
      <Input Contract="cpp.translation-units/v1"
             Scope="ProductClosure"
             IncludeGenerated="true">
        <Include Path="src/**" />
        <Exclude Path="src/vendor/**" />
      </Input>

      <Config Name="primary"
              Path=".cpp-audit.yml"
              Optional="false" />

      <Policy Gate="true"
              FailOn="Warning"
              Baseline="quality/cpp-audit-baseline.json" />

      <Execution Jobs="Auto"
                 Timeout="10m"
                 Cache="ReadWrite" />

      <Reports>
        <Report Name="ci"
                Format="sarif"
                Path="$(OutputDir)/reports/cpp-audit.sarif" />
      </Reports>
    </Run>
  </Tooling>
</Application>
```

Profile overrides use the same identity:

```xml
<Profile Name="ci">
  <Tooling>
    <Run Name="cpp-static-analysis">
      <Policy Gate="true" FailOn="Warning" />
      <Execution Cache="ReadOnly" />
    </Run>
  </Tooling>
</Profile>
```

Removal is explicit:

```xml
<Tooling>
  <Run Remove="cpp-static-analysis" />
</Tooling>
```

### Run Identity And Merge Rules

- Run identity is `Name` within the effective project.
- An override may omit `Action` when replacing an existing run.
- Scalar properties replace inherited values when explicitly authored.
- Named configs, reports, and policies merge by their own identity.
- Include/exclude collections replace by default; additive behavior requires
  explicit `Merge="Append"`.
- A later override cannot silently change action kind. Changing the action is
  explicit and visible in graph diff.
- Duplicate run identities in the same overlay scope are errors.
- Package, project, profile, and workspace contributions retain provenance.

### Action Kinds

Initial standardized values:

- `Analyze`: source or artifact inspection producing findings
- `Format`: source formatting producing edits or rewritten output
- `Scan`: security, license, dependency, binary, or content inspection
- `Transform`: explicit source/content transformation producing edits/files
- `Report`: consumes graph data or prior results and produces reports
- `Custom`: protocol-based action without built-in semantic command mapping

These values guide command selection and UI presentation. Capabilities, input
contracts, and result contracts determine actual behavior.

## Input Contracts

Inputs are resolved before execution and recorded in the graph. Drivers never
walk the repository unless their action contract explicitly accepts a root
directory input.

### File Set `files/v1`

Each entry includes:

- absolute and workspace-relative path
- role and content kind
- owner kind and owner identity
- authored or generated state
- visibility
- optional digest
- selection provenance

### C++ Translation Units `cpp.translation-units/v1`

This contract is produced from the configured build plan and compilation
database, not from source-extension scanning. Each entry includes:

- source file
- working directory
- compiler executable
- argument vector without shell quoting
- target and host platform identities
- language and standard
- project/product target identity
- generated state
- normalized include, define, and forced-include data where available
- a digest of the effective compile command

The build plan, `compile_commands.json`, C/C++ editor provider, and tooling
framework must share one compilation-unit source of truth.

### Artifact Set `artifacts/v1`

Entries may refer to build outputs, stage outputs, package outputs, symbols,
archives, or reports. Each entry carries graph identity and provenance.

### Prior Results `tool-results/v1`

Report and aggregation actions may depend on normalized results from earlier
runs. This creates explicit execution-plan edges instead of relying on output
directory discovery.

### Input Scope

Initial scope values:

- `Product`: only inputs owned by the selected product
- `ProductClosure`: selected product plus resolved project-reference closure
- `Workspace`: all selected workspace products compatible with the run
- `Explicit`: only authored include entries
- `ActiveFile`: command/editor supplied file, valid only for actions supporting
  incremental execution
- `ChangedFiles`: files selected against an explicit VCS/base revision

Scope is an actual resolver operation. It must never be metadata that the
runner ignores.

## Tool Execution Plan

The Composition Graph gains a `tooling` facet whose resolved plan contains:

```text
tools[]
drivers[]
actions[]
runs[]
inputSets[]
dependencies[]
policies[]
reports[]
diagnostics[]
```

Each selected run records:

- stable identity
- action and kind
- provider package and feature
- selected tool and driver
- driver protocol
- tool/driver capabilities
- host and target selection
- resolved input-set references and counts
- config declarations and resolved digests
- policy and execution settings
- dependencies on other runs or build/configure phases
- declared output/report artifacts
- full contribution and selection provenance

The graph must not contain secret values. It may state that a run requests an
authorized secret and whether that requirement is resolved.

### Plan States

A run has one resolution state:

- `ready`
- `disabled`
- `excluded`
- `unavailable`
- `invalid`

Unavailable or invalid enabled runs are graph diagnostics and cause the
matching semantic command to fail before execution.

### Focused Graph Operations

Add:

```text
ngin graph --tooling-plan
ngin inspect --format json
ngin explain tool:<identity>
ngin explain driver:<identity>
ngin explain action:<identity>
ngin explain run:<identity>
ngin explain input-set:<identity>
ngin diff --from-profile dev --to-profile ci
```

Diff should cover added/removed/replaced runs, driver/tool/version changes,
input-scope changes, gate changes, config changes, and report changes.

## Driver Protocol

The initial external protocol is `NGIN.ToolDriver/1`. Protocol versioning is
independent from manifest, graph, and CLI-event versions.

### Transport

- NGIN writes one request JSON document to a generated request file.
- NGIN passes the request path using `--ngin-request <path>`.
- Driver stdout is reserved for UTF-8 JSONL protocol events.
- Driver stderr is captured as unstructured driver log output and is attached
  to failures or shown in verbose mode.
- NGIN sends normal process termination for cancellation, waits a bounded
  grace period, then force-terminates if necessary.
- Request and response files live under the selected generated output tree and
  are treated as generated artifacts.

A request file is preferable to a large stdin document because translation-unit
plans can be large and are useful for failure inspection. A future protocol
minor version may add streaming input without changing the semantic model.

### Request Envelope

```json
{
  "schemaVersion": "1.0",
  "kind": "NGIN.ToolDriver.Request",
  "runId": "01J...",
  "workspace": { "root": "/repo", "name": "Game" },
  "project": { "path": "/repo/Game/Game.nginproj", "name": "Game" },
  "profile": "ci",
  "action": { "name": "analyze", "kind": "Analyze" },
  "tool": {
    "path": "/usr/bin/example-tool",
    "version": "1.2.0",
    "source": "PATH"
  },
  "host": { "platform": "linux-x64" },
  "target": { "platform": "linux-x64", "abi": "..." },
  "workingDirectory": "/repo/Game",
  "outputDirectory": "/repo/.ngin/build/Game/ci/tooling/cpp-static-analysis",
  "configs": [],
  "inputSets": [],
  "options": {},
  "environment": {},
  "capabilitiesRequested": ["diagnostics", "fixes"]
}
```

Rules:

- Paths are absolute in the driver request and carry a workspace-relative form
  where presentation needs it.
- Arguments are arrays, never shell command strings.
- Only environment variables explicitly resolved for the action are present.
- Secret values are omitted unless declared, authorized, and required.
- Unknown additive fields in protocol `1.x` are ignored.
- Unsupported required capabilities cause a pre-execution resolution error.

### Driver Events

Every stdout line is one of:

- `run.started`
- `progress`
- `diagnostic`
- `edit.proposed`
- `artifact.produced`
- `metric`
- `log`
- `run.completed`

All events contain `schemaVersion`, `kind`, `runId`, `sequence`, `type`, and
`data`. Sequence is monotonically increasing per driver process.

The driver must emit exactly one `run.completed`. EOF without completion,
malformed JSONL, a sequence violation, or a protocol-major mismatch is an
execution failure even if earlier findings were valid. Earlier findings may be
shown but cannot turn the run into success.

### Driver Probe

Before execution, NGIN may invoke a driver with `--ngin-probe <request-path>`.
The probe uses the same JSON/JSONL framing and lets the driver report:

- driver version and protocol versions
- resolved underlying tool version
- dynamically available capabilities
- host compatibility
- an actionable unavailability reason

The driver owns tool-specific version discovery and parsing. The central
framework does not interpret arbitrary `--version` output. Probe results are
cached by driver/tool executable identity and file metadata, and the selected
result is recorded in the graph execution snapshot and command events.

### Completion Status

`run.completed` reports execution, not gate outcome:

```json
{
  "type": "run.completed",
  "data": {
    "status": "succeeded",
    "toolExitCode": 0,
    "diagnostics": 12,
    "edits": 3,
    "artifacts": 1
  }
}
```

NGIN evaluates the gate after normalized results are collected. A driver may
report an expected tool exit code as successful when that exit code represents
findings, but that mapping belongs to the versioned driver implementation, not
the central scheduler.

## Normalized Diagnostics

A diagnostic contains:

- stable run identity
- tool, driver, and action identity
- intrinsic severity: `note`, `info`, `warning`, `error`, or `fatal`
- effective severity after explicit policy mapping
- rule/code and optional rule documentation URL
- message
- primary file range with start and optional end
- zero or more related locations and notes
- tags such as security, performance, deprecated, unnecessary, or style
- stable fingerprint
- suppression/baseline state
- zero or more edit-set references
- original tool severity/code in extension data when normalization changes it

Protocol locations use one-based line and column values and explicit inclusive
start/exclusive end semantics. NGIN CLI events retain the same convention and
document it. Editor consumers convert at their boundary.

### Fingerprints And Deduplication

NGIN calculates or validates a fingerprint from stable fields such as run,
rule, normalized path, semantic location, and message template. Drivers may
provide a stronger native fingerprint.

Results are deduplicated within a run. Repeated header findings from multiple
translation units retain related translation-unit context without creating
identical Problems entries.

### Execution Diagnostics

Missing tools, invalid configuration, protocol violations, timeouts, and
driver crashes are emitted as NGIN execution diagnostics. They are never
converted into ordinary tool findings or suppressed by a findings baseline.

## Edits, Fixes, And Formatting

An edit set contains:

- identity and originating diagnostic/run
- file path
- expected source digest
- non-overlapping text edits with explicit ranges and replacement text
- applicability: automatic, suggested, or unsafe
- optional human-readable description

NGIN validates path ownership, current file digest, range bounds, overlap, and
workspace trust before exposing or applying edits.

Commands:

```text
ngin analyze --fix-preview
ngin analyze --apply-fixes
ngin format --check
ngin format --apply
ngin tool edits --run <run-id> --format json
```

`--apply-fixes` and `--apply` are explicit mutations. CI and editor-on-save
execution default to check/preview behavior.

VS Code code actions apply edits through `WorkspaceEdit` only after digest and
document-version validation. Multi-file edits are presented as such.

## Policy And Quality Gates

Policy is evaluated by NGIN over normalized results.

### Severity Mapping

Intrinsic severity is preserved. Optional policy may map a specific run/rule
severity upward or downward for presentation and gating, but both intrinsic
and effective values remain observable. Execution diagnostics cannot be
downgraded below error.

### Fail Policy

Initial gate settings:

- `Gate="false"`: findings never fail the command
- `FailOn="Info|Warning|Error|Fatal"`: fail when an unsuppressed effective
  finding reaches the threshold
- `MaxFindings`, `MaxWarnings`, and per-rule budgets
- `NewFindingsOnly="true"`: gate only findings absent from the selected
  baseline

Default policy should be explicit in graph conventions. Suggested convention:

- local development analyzer runs report findings but do not gate
- CI profiles gate on warnings or a workspace-defined threshold
- execution failure always fails the command

### Baselines

Baselines are authored policy inputs, not generated build output. A baseline
contains tool action identity, rule, fingerprint, optional expiry, and reason.
It does not contain arbitrary command success state.

Commands:

```text
ngin quality baseline create --run <identity> --output <file>
ngin quality baseline update --run <identity> --output <file>
ngin quality baseline verify --run <identity>
```

Baseline updates are explicit and produce a reviewable diff. Missing or stale
required baselines are errors in CI.

### Suppressions

Native tool suppressions remain supported. NGIN-level suppressions require a
rule/fingerprint, reason, and optional expiry. The result model records whether
the tool, baseline, or NGIN policy suppressed a finding.

## Reports And Artifacts

Reports are declared outputs of runs. Initial standard formats:

- `json`: normalized NGIN result document
- `jsonl`: normalized streaming result events
- `sarif`: CI/code-scanning interoperability
- `text`: stable human report
- tool-native named formats declared by the action

The internal result model is not SARIF-specific. SARIF is rendered from
normalized findings, fixes, rules, and artifacts.

Every report is emitted through `artifact.produced` with run identity, format,
path, digest, and provenance. Report path collisions are graph errors.

## Scheduler

The scheduler executes a directed acyclic Tool Execution Plan.

### Dependencies

Run dependencies can include:

- graph resolution
- restore
- generation
- configure/compilation-unit production
- build artifacts
- staged artifacts
- completion of another tool run

For example, a translation-unit analyzer depends on configure, a binary scanner
depends on build, and an aggregate report depends on selected analyzer runs.

### Concurrency

- `--jobs <n>` controls the global tooling worker budget.
- Runs and per-input shards declare scheduler weight and maximum parallelism.
- Output events remain tagged by run/shard and are rendered deterministically
  where ordering matters.
- Drivers declare whether they support batch, shard, and active-file modes.
- Exclusive resource keys support tools that cannot safely run concurrently.

### Cancellation And Timeout

- All tool commands accept cancellation.
- Editor runs cancel or supersede older runs for the same project/profile/run
  key.
- Each run can declare a timeout.
- Cancellation and timeout are distinct completion states.
- Partial findings may be displayed but never cached as a completed result.

### Failure Strategy

Initial strategies:

- `Continue`: run independent plan nodes after a failure
- `FailFast`: cancel remaining nodes when a gate or execution failure occurs
- `DependencyAware`: default; skip only nodes whose prerequisites failed

Skipped nodes receive an explicit reason in results and summaries.

### Cache

Cache modes are `Off`, `ReadOnly`, `WriteOnly`, and `ReadWrite`. A cache key
includes at least:

- driver protocol and driver digest/version
- tool digest/version and resolution source
- action and normalized options
- configuration file digests
- input digests
- compilation-unit command digests where applicable
- declared non-secret environment values
- target platform/ABI fields consumed by the action

Secret values are never written to cache metadata. Actions receiving secrets
default to non-cacheable unless they declare a safe key strategy.

Cache hits replay normalized result objects through the same CLI event path and
are visibly marked as cached.

## CLI Contract

### General Commands

```text
ngin tool list
ngin tool doctor [<tool-or-run>]
ngin tool plan [<run>]
ngin tool run <run> [selection options]
ngin tool results [<run-id>] --format json
ngin quality [selection options]
ngin quality baseline create|update|verify ...
```

### Semantic Commands

```text
ngin analyze [--run <name>] [--file <path>] [--changed-since <revision>]
             [--jobs <n>] [--no-cache] [--fix-preview|--apply-fixes]

ngin format [--run <name>] [--file <path>] [--changed-since <revision>]
            [--check|--apply] [--jobs <n>]

ngin scan [--run <name>] [--changed-since <revision>] [--jobs <n>]

ngin report [--run <name>] [--jobs <n>]
```

`--file` and `--changed-since` are selection overrides accepted only when the
selected action advertises the corresponding capability. An invalid override
is an error, not an ignored option.

`--no-configure` requires a fresh compatible compilation-unit plan. If none is
available, the command fails with an actionable diagnostic.

### Exit Codes

Define stable categories:

- `0`: execution succeeded and all gates passed
- `1`: one or more quality gates failed
- `2`: authoring, resolution, configuration, or usage error
- `3`: tool/driver execution failure
- `4`: cancelled
- `5`: timeout

If preserving conventional single-code behavior is necessary for shell users,
the CLI may still return a generic nonzero value by default, but structured
completion events must always contain the normalized category. The preferred
implementation is to expose the stable exit codes directly.

### CLI Events

Extend `NGIN.CLI.Event` additively or introduce its next major version with:

- run/action/tool/driver identities on phases and diagnostics
- `tool.run.started`
- `tool.progress`
- richer `diagnostic`
- `edit.proposed`
- `metric`
- `tool.run.completed`
- gate evaluation events
- cache-hit status

The CLI event emitter translates driver events. Editors consume CLI events,
not driver stdout directly.

Human output should summarize:

- selected and skipped runs
- tool/driver versions and resolution source in verbose mode
- input counts
- finding counts by severity and baseline state
- gate results
- cache status
- report artifacts
- timing and execution failures

## VS Code Design

The extension should contain no clang-tidy-specific execution or authoring
logic after this work.

### Tooling Model

The extension consumes the graph `tooling` facet and displays:

- selected runs grouped by action kind
- resolved tool and driver
- input scope and counts
- configuration and policy
- readiness/unavailability diagnostics
- last result, timing, gate, and cache state
- report artifacts and proposed edits

Commands are generated from run capabilities:

- Run
- Run on Active File
- Run on Changed Files
- Preview Fixes
- Apply Fixes
- Open Configuration
- Open Report
- Explain Run/Tool/Driver

### Run Coordinator

Introduce one extension service responsible for:

- process lifecycle and cancellation
- run IDs and generation numbers
- per workspace/project/profile/run state
- incremental CLI-event consumption
- atomic completion
- stale-run rejection
- output and progress routing

The existing single global clear-and-replace behavior must be removed.

### Diagnostic Ownership

Diagnostics are indexed by:

```text
workspace + project + profile + run identity
```

Updating one run removes only that run's prior diagnostics. Switching active
profiles may change visibility without discarding cached results for other
profiles. A cancelled or stale run cannot replace a newer completed result.

### Save Behavior

Replace the ambiguous analyzer-on-manifest-save setting with:

- `ngin.tooling.validateManifestOnSave`
- `ngin.tooling.runOnSave`, a map from stable run identity to `activeFile` or
  `all`
- `ngin.tooling.activeFileDebounceMs`
- per-run enablement configured from the resolved Tooling dashboard

Active-file execution is offered only for runs advertising that capability.
Saving a C++ file should not trigger a full project configure and analysis by
default.

### Code Actions

Diagnostics with edit sets expose general NGIN tool code actions. The
extension validates document version/digest, requests confirmation for unsafe
or multi-file edits, applies a `WorkspaceEdit`, and optionally schedules a
follow-up run.

### Authoring

Remove hardcoded package versions and regex-based
`addClangTidyAnalyzerPackage` behavior. The extension should ask the CLI for
available tooling packages/actions and invoke a general authoring command such
as:

```text
ngin add tool-action Vendor.Tooling.CppAudit::analyze --project ...
```

The CLI remains the owner of manifest mutation, version policy, package
feature selection, and duplicate detection.

### Tests

Add integration coverage for:

- graph-driven command availability
- structured diagnostic ranges and related information
- independent diagnostics for two projects and profiles
- cancellation and stale-run rejection
- active-file execution
- code-action edit application and stale-digest refusal
- malformed protocol handling
- report artifact links

## Security And Trust

Tool execution is code execution and must follow workspace trust.

- VS Code must not launch tools in an untrusted workspace.
- CLI should support trust policy for package-provided drivers and executables.
- The graph reports executable origin and package provenance before execution.
- Drivers receive the minimum environment necessary.
- Secret access is capability- and policy-gated, redacted from events/logs, and
  disabled for caching by default.
- Output and edit paths are normalized and checked against declared output or
  workspace roots.
- Symlink/path traversal is checked before applying edits or accepting
  artifacts.
- Shell interpretation is prohibited.
- Timeouts and output-size limits protect editor and CI processes.
- Driver protocol logs and backend output pass through the existing redaction
  layer before presentation.

Sandboxing can be added later as a platform capability. The contract should
record requested/actual isolation without claiming isolation where none exists.

## Reproducibility And Version Policy

Tool resolution should expose and optionally enforce:

- package version
- driver version and digest
- underlying tool version and digest when available
- resolution source
- supported version range declared by the action/driver
- host compatibility

Workspace policy may require package/bundled tools in CI and reject unpinned
`PATH` resolution:

```xml
<ToolingPolicy>
  <Resolution AllowPath="false"
              RequireVersion="true"
              RequireTrustedPackage="true" />
</ToolingPolicy>
```

Local development can remain permissive while the graph clearly reports the
difference.

## Implementation Workstreams

### Workstream A: Freeze The General Contract

Deliverables:

- this plan accepted as the Quality/Tooling direction
- exact XML schema for package tools/drivers/actions and selected runs
- exact Composition Graph `tooling` facet schema
- exact `NGIN.ToolDriver/1` request/event schemas
- exact normalized diagnostic, edit, artifact, metric, and result schemas
- CLI exit and event contract updates
- explicit removal list for phase-one analyzer fields and behavior

Acceptance gate:

- representative analyzer, formatter, binary scanner, and report aggregator
  can be expressed without adding tool-specific fields to the graph
- schemas reject missing drivers, incompatible input contracts, unsupported
  capabilities, and duplicate run identities

### Workstream B: Replace The Authoring And Model Layer

Primary area:

- `Tools/NGIN.CLI/src/Model.hpp`
- `Tools/NGIN.CLI/src/Authoring.cpp`
- `Tools/NGIN.CLI/src/Overlay.cpp`
- focused authoring, workspace, package, and overlay tests

Deliverables:

- tool, driver, action, run, input, config, policy, execution, and report models
- product/profile/workspace/package-feature parsing
- selector, condition, removal, replacement, and provenance behavior
- strict value/capability validation
- deletion of `AnalyzerDefinition`, `AnalyzerPolicy`, `QualityDefinition`, and
  name-based analyzer merge paths after replacements exist

Acceptance gate:

- runs contributed from all supported scopes resolve deterministically by
  identity
- no current `Quality/Analyzer` manifest is accepted

### Workstream C: Build The Graph-Owned Tooling Plan

Primary area:

- resolution and graph snapshot construction
- graph JSON schema/spec
- graph, inspect, diff, and explain tests

Deliverables:

- resolved registry of tools, drivers, and actions
- selected tool runs and plan states
- typed input-set references
- plan dependency edges
- output/report collision diagnostics
- provenance, focused plan, explain, and diff output

Acceptance gate:

- CLI, tests, and VS Code can determine exactly what would run without
  executing or rediscovering authored state

### Workstream D: Unify Compilation Units

Primary area:

- build-plan resolution
- CMake compile database integration
- VS Code C/C++ provider

Deliverables:

- graph-owned `cpp.translation-units/v1` plan
- freshness/signature metadata
- consistent generated-source behavior
- product and product-closure selection
- reuse by tooling and editor configuration

Acceptance gate:

- compiler database, C/C++ editor provider, and analyzer driver receive the
  same effective translation units and commands

### Workstream E: Implement Driver Host And Scheduler

Primary area:

- new focused CLI source files rather than adding more logic to `Commands.cpp`
- process execution support shared with existing backend helpers where safe

Suggested internal components:

```text
ToolRegistry
ToolExecutionPlanner
ToolDriverHost
ToolProtocolReader
ToolScheduler
ToolResultStore
ToolCache
QualityGateEvaluator
```

Deliverables:

- request serialization and generated request artifacts
- strict JSONL protocol reader
- cancellation, timeout, output limits, and process-tree termination
- dependency-aware scheduling and bounded concurrency
- normalized result aggregation
- optional cache with safe initial defaults

Acceptance gate:

- fake protocol drivers cover success, findings, malformed events, missing
  completion, crash, cancellation, timeout, and parallel execution

### Workstream F: Diagnostics, Edits, Policy, And Reports

Deliverables:

- rich normalized diagnostic model
- fingerprinting and deduplication
- execution diagnostics
- edit validation and explicit apply flow
- severity mapping and gate evaluator
- baseline create/update/verify
- normalized JSON/JSONL and SARIF renderers
- report artifact events

Acceptance gate:

- intrinsic errors cannot be silently downgraded by a gate threshold
- nonzero/crashed execution cannot pass because findings were parsed
- repeated header findings deduplicate without losing related TU context
- stale edits are refused

### Workstream G: Rebuild CLI Commands Over The Framework

Deliverables:

- general `ngin tool` commands
- `ngin quality`
- `ngin analyze`, `ngin format`, `ngin scan`, and `ngin report` as action-kind
  filters
- selection, jobs, cache, check/fix/apply, and report options
- stable structured completion category
- human output rendered from the event/result model
- removal of the clang-tidy branch from `CmdAnalyze`

Acceptance gate:

- unsupported or unavailable enabled runs fail before execution
- commands do not inspect executable names to choose behavior

### Workstream H: Port The Official Example Tooling Package

The first adapter may still target clang-tidy to prove real C++ translation
units, but no general framework type or branch may be named after it.

Deliverables:

- official protocol-speaking driver or registered bootstrap adapter
- updated `NGIN.Tooling.ClangTidy` package using general tool/driver/action/run
  declarations
- tool-version and config discovery
- diagnostics, related notes, fixes, and optional SARIF
- updated `Hello.Analyzer` example using the new general authoring contract

Acceptance gate:

- removing the official package removes all clang-tidy knowledge from project
  authoring and VS Code
- a fake second analyzer package runs through the same protocol without CLI or
  editor changes

### Workstream I: Rebuild VS Code Tooling Integration

Primary area:

- `Tools/NGIN.VSCode/src/extension.ts`
- new focused tooling services/modules
- graph types, project editor, sidebar, commands, and tests

Deliverables:

- graph-driven tooling UI and commands
- run coordinator and cancellation
- per-run diagnostic ownership
- incremental event processing and stale-run protection
- active-file and changed-file modes
- general config/report opening
- general edit/code-action support
- CLI-owned tool-action authoring
- deletion of clang-tidy-specific extension commands and hardcoded versions

Acceptance gate:

- two projects can retain independent results
- cancelled/older runs cannot overwrite newer results
- installing a new compliant tool package requires no TypeScript code change

### Workstream J: Documentation, Examples, And CI

Deliverables:

- authoritative tooling/quality spec
- package author guide for drivers and actions
- project/workspace author guide for runs and policy
- protocol SDK/sample driver if justified
- analyzer, formatter, scanner, and aggregate-report fixtures
- CI profiles with gating and SARIF examples
- removal or rewrite of phase-one analyzer documentation

Acceptance gate:

- a third party can implement a minimal driver using only the published
  protocol documentation and fixture tests

## Test Strategy

### Unit Tests

- strict parsing and rejected legacy shapes
- overlay identity and provenance
- capability and contract negotiation
- input-scope selection
- protocol parsing and sequence validation
- result normalization and deduplication
- severity mapping and gates
- edit validation
- cache-key stability
- scheduler ordering and failure propagation

### CLI Contract Tests

- focused tooling graph JSON and schema validation
- explain and profile diff
- no runs, disabled runs, unavailable runs, and invalid runs
- successful findings with passing and failing gates
- execution failure after one or more findings
- cancellation and timeout completion categories
- cached result replay
- JSONL event richness and ordering
- report artifact production

### Driver Fixture Matrix

Provide small fake drivers for:

- file analyzer with diagnostics
- translation-unit analyzer with repeated header diagnostics
- formatter with edits
- artifact scanner
- aggregator consuming prior results
- malformed/crashing/unresponsive driver

The fake drivers are the primary framework tests. A real clang-tidy smoke test
proves the official adapter but must not be the only integration coverage.

### VS Code Tests

- graph capability mapping
- split JSONL event chunks
- diagnostic ranges, related information, tags, and codes
- per-project/profile/run result retention
- cancellation and stale-run ordering
- active-file debounce
- code action success and stale edit refusal
- general package/action authoring command
- report artifact navigation

## Breaking Removal List

Delete or replace the following rather than preserving compatibility:

- manifest `Quality/Analyzer`
- workspace analyzer policy records
- `AnalyzerDefinition`, `AnalyzerPolicy`, and `QualityDefinition`
- graph `quality.analyzers` and focused phase-one quality payload
- `IsClangTidyAnalyzer`
- source-extension-based `ResolveAnalyzerSources`
- clang-tidy parsing inside the central CLI command handler
- the current `Severity` behavior that rewrites every finding
- silent success for non-clang analyzer declarations
- VS Code `addClangTidyAnalyzerPackage`
- hardcoded `NGIN.Tooling.ClangTidy` versions in TypeScript/snippets
- clang-tidy-specific open/create commands where a general configuration
  capability can provide the same behavior
- fallback text diagnostic parsing after the new CLI event contract and
  supported CLI version floor are established

Generated build trees and launch manifests are regenerated; they are never
migrated or edited in place.

## Recommended Delivery Sequence

1. Freeze XML, graph, driver protocol, result, and CLI-event contracts.
2. Replace authoring/model/overlay structures and reject the legacy analyzer
   shape.
3. Emit graph-owned tool/action/run plans with provenance, explain, and diff.
4. Introduce the shared compilation-unit plan.
5. Implement fake protocol drivers, driver host, scheduler, and result store.
6. Implement diagnostics, edits, policy, reports, and cache.
7. Rebuild `ngin tool`, `ngin analyze`, `ngin format`, and `ngin quality`.
8. Port the official clang-tidy package through the general adapter boundary.
9. Rebuild VS Code integration around graph capabilities and per-run state.
10. Add formatter/scanner/aggregator fixtures to prove generality.
11. Rewrite active docs, schemas, examples, snippets, and progress records.
12. Run the targeted CLI, protocol-driver, example, and VS Code verification
    passes and refreeze the unshipped contract.

Do not start by expanding the current `CmdAnalyze`. The first executable slice
after contract work should be a fake protocol driver through the new plan and
scheduler, followed by the real tool adapter.

## Definition Of Done

This initiative is complete when:

- no framework behavior identifies a tool by display or executable name
- tools and drivers resolve with explicit identity, version, host compatibility,
  capabilities, and provenance
- every selected run has an inspectable graph plan before execution
- unavailable or unsupported enabled runs cannot silently pass
- compilation-unit tools consume the same source of truth as the build/editor
- intrinsic findings, execution status, and gate status are distinct
- diagnostics support ranges, related locations, fingerprints, suppressions,
  and edits
- scheduler execution is bounded, cancellable, timeout-aware, deterministic,
  and dependency-aware
- cache hits are safe and observable
- reports and SARIF are declared artifacts
- VS Code retains independent per-run results and rejects stale completions
- active-file runs and general code actions work through capabilities
- the extension contains no clang-tidy-specific logic
- at least an analyzer, formatter, artifact scanner, and aggregate reporter run
  through the same driver protocol without framework code changes
- current phase-one analyzer schema and compatibility behavior are gone
- active documentation teaches the general framework as the normal path

## Decisions To Confirm During Contract Freeze

The plan recommends defaults for the remaining detailed decisions:

- Use a request file plus JSONL stdout for `NGIN.ToolDriver/1`.
- Keep drivers out-of-process for fault and dependency isolation.
- Let official packages ship drivers; allow temporary CLI-registered adapters
  only for bootstrap.
- Keep semantic authoring kinds over a general normalized run model.
- Default to dependency-aware failure handling and cache off until cache keys
  are proven.
- Use one-based protocol locations with exclusive end positions.
- Store baselines as authored JSON files with explicit reasons and optional
  expiry.
- Require explicit mutation flags for all fixes and transforms.
- Replace and refreeze the unshipped V4 quality slice rather than bumping a
  shipped compatibility version or carrying legacy parsing.

Any change to these defaults should be made before implementing the driver
host, because transport, isolation, location, and compatibility decisions are
expensive to reverse later.
