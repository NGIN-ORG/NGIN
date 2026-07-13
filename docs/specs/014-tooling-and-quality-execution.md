# General Tooling And Quality Execution Contract

Status: V4 active contract. The repository has not shipped the removed
`Quality/Analyzer` contract; no compatibility behavior is provided.

## Ownership And Authoring

Packages declare tools, protocol drivers, and actions. Workspaces, products,
profiles, and selected package features contribute named runs. The resolved
Composition Graph is the source of truth for editor UI and execution.

```xml
<ToolDrivers>
  <Driver Name="driver" Protocol="NGIN.ToolDriver/1" Executable="vendor-driver">
    <Capabilities><Capability Name="diagnostics" /></Capabilities>
  </Driver>
</ToolDrivers>
<ToolActions>
  <Action Name="analyze" Kind="Analyze" Tool="vendor-tool" Driver="driver">
    <Accepts Contract="files/v1" />
    <Capabilities><Capability Name="diagnostics" /></Capabilities>
    <Environment>
      <Variable Name="ANALYZER_MODE" Required="false" />
      <Variable Name="ANALYZER_TOKEN" Secret="true" CacheKey="false" />
    </Environment>
  </Action>
</ToolActions>
```

Action kinds are `Analyze`, `Format`, `Scan`, `Transform`, `Report`, and
`Custom`. An enabled action must bind to a selected package tool and driver,
the driver must use `NGIN.ToolDriver/1`, action capabilities must be a subset
of driver capabilities, and the selected input contract must be accepted.
Invalid or unavailable enabled runs fail resolution.

```xml
<Tooling>
  <Run Name="cpp-analysis" Action="Vendor.Tooling::analyze">
    <Input Contract="cpp.translation-units/v1" Scope="ProductClosure"
           IncludeGenerated="true">
      <Include Path="src/**" />
      <Exclude Path="src/vendor/**" />
    </Input>
    <Config Name="primary" Path=".vendor.yml" Optional="false" />
    <Policy Gate="true" FailOn="Warning"
            Baseline="quality/vendor-baseline.json"
            NewFindingsOnly="true" MaxWarnings="0">
      <Severity Rule="style-rule" To="info" />
      <Suppress Rule="legacy-rule" Reason="accepted debt" Expires="2027-01-01" />
      <Budget Rule="security-rule" Max="0" />
    </Policy>
    <Execution Jobs="Auto" Timeout="10m" Cache="ReadWrite"
               FailureStrategy="DependencyAware" Weight="1"
               MaxParallelism="2" ExclusiveResource="vendor-license" />
    <DependsOn Run="generated-api-check" />
    <Reports>
      <Report Name="ci" Format="sarif" Path="$(OutputDir)/vendor.sarif" />
    </Reports>
  </Run>
</Tooling>
```

Run identity controls overlay replacement and removal. A profile override may
omit `Action` when the run exists in a lower-precedence scope. Removal uses
`<Run Remove="cpp-analysis" />`. Legacy `Quality` and `Analyzer` elements are
errors.

Input scopes are `Product`, `ProductClosure`, `Workspace`, `Explicit`,
`ActiveFile`, and `ChangedFiles`. C++ translation-unit requests come from the
configured compile database and include compiler, arguments, working directory,
target platform, owner, and a stable command digest.
Configured compilation-unit plans carry an authoring/selection compatibility
signature. The graph exposes the complete selected translation units, and
`--no-configure` rejects a missing or stale signature.

Action environment requirements are explicit. A requirement names an existing
resolved product environment variable and pins its secret classification.
Required missing values are invalid plans. Tool subprocesses inherit only a
small platform runtime allowlist; declared values are sent in the protected
request artifact. Non-secret values participate in cache keys. Runs receiving
secrets are non-cacheable unless the action explicitly declares `CacheKey`, in
which case only a digest participates in the key.

Input include/exclude lists replace inherited lists unless `Merge="Append"` is
explicit. Configs and reports merge by stable `Name`. Explicit run dependencies
must exist, be enabled, and form an acyclic plan.

System-wrapper packages use `SystemExecutable` with optional
`OverrideEnvironment` and `VersionRange`. Drivers may opt into protocol probing
with `Probe="true"`. `ToolingPolicy/Resolution` controls whether `PATH` is
allowed and whether versions and package/bundled resolution are required.

Intrinsic diagnostic severity is never rewritten by policy. Gate evaluation
is separate and uses `Gate`, `FailOn`, budgets, and optional fingerprint
baselines. Execution failure, timeout, protocol failure, and a failed quality
gate all fail the command.

## Driver Protocol And Results

NGIN writes a request artifact and invokes an external driver as
`driver --ngin-request <request.json>`. The driver writes ordered JSONL
`NGIN.ToolDriver.Event` records. Sequences start at one and exactly one
`run.completed` record is required. Malformed JSON, a mismatched run ID, a
sequence gap, missing completion, nonzero exit, timeout, or output-limit breach
is an execution failure.

Drivers may emit diagnostics, proposed edit sets, artifacts, metrics, logs, and
progress. Diagnostics support ranges, related locations, tags, fingerprints,
and edit-set IDs. Edits carry an expected file digest; NGIN refuses stale or
overlapping edits. Applicability is `automatic`, `suggested`, or `unsafe`;
unsafe edits require explicit approval.
Progress and log records are translated to CLI events incrementally while the
driver is still running. POSIX process groups and Windows job objects provide
bounded output, timeout, cancellation, and process-tree termination.

Normative schemas:

- `docs/schemas/ngin-tool-driver-v1.schema.json`
- `docs/schemas/ngin-tool-result-v1.schema.json`
- `docs/schemas/ngin-tool-baseline-v1.schema.json`

Every external execution writes `tooling/<run>/request.json`; every execution
writes normalized `tooling/<run>/result.json`. Declared `json`, `jsonl`, `text`,
and `sarif` reports are rendered from normalized results. Cache entries are
keyed by action/tool identity, platforms, capabilities, configs, inputs, job
budget, and compilation-command digests. Only successful completed results are
written.

## Graph And CLI

The graph exposes `plans.tooling.tools`, `drivers`, `actions`, `runs`,
`inputSets`, `dependencies`, `policies`, `reports`, and `diagnostics`; focused views are
`ngin graph --tooling-plan` and `ngin tool plan`. Runs report binding state,
package/action/tool/driver identity, selected files, inputs, configs, gate and
execution policy, reports, and provenance.
Disabled and overlay-removed runs remain inspectable as `disabled` and
`excluded`. Focused explain identities are `tool:`, `driver:`, `action:`,
`run:`, and `input-set:`. Profile diff includes action/tool/driver versions,
input selection, configs, policy, execution, and reports.

```text
ngin tool list [--available] [--format json]
ngin tool doctor
ngin tool plan
ngin tool run <RunName> [--check|--apply]
ngin tool results [RunId] [--run <RunName>] --format json
ngin tool edits [RunId] --run <RunName> --format json
ngin add tool-action <Package::Action> [--run <RunName>]
ngin analyze | format | scan | report | quality
ngin quality baseline create|update|verify --run <RunName>
```

`--file <path>` and `--changed-since <revision>` are mutually exclusive scope
overrides and run only actions declaring the corresponding incremental
capability. `--jobs` sets the global scheduler worker budget; each run's
`Execution Jobs` controls its driver budget. `--no-cache` disables cache use,
and `--no-configure` requires a fresh compatible compilation-unit plan. `--apply`
applies digest-validated edits; `--allow-unsafe` is required for unsafe sets.

Tool command process codes are `0` success, `1` gate failure, `2` invalid or
unavailable plan, `3` execution failure, `4` cancellation, and `5` timeout.
Structured `NGIN.CLI.Event` completion records are authoritative for editor
integrations.

## VS Code

The extension consumes graph and CLI contracts only. It retains diagnostics by
project/profile/run, ignores stale completions, cancels superseded process
trees, preserves ranges/related information/tags, exposes edit quick fixes,
and delegates action discovery and manifest mutation to the CLI. Quick fixes
load stored edit sets with `ngin tool edits`, validate trust, document version,
file digest, path ownership, range overlap, and applicability, then apply a
native multi-file `WorkspaceEdit`; they never rerun a mutating tool command.
Tool processes and edit application are disabled in untrusted workspaces.
The C/C++ provider consumes graph translation units first, so editor
configuration, analyzer requests, and the build compile database share the
same commands and command digests.

Save settings are `ngin.tooling.validateManifestOnSave`,
`ngin.tooling.runOnManifestSave`, `ngin.tooling.runActiveFileOnSave`, and
`ngin.tooling.activeFileDebounceMs`. Active-file saves run only explicitly
compatible actions.
