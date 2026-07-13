# Tool Driver Authoring

Use this guide to publish a tool package that works with the NGIN CLI, graph,
CI output, and editors without adding framework code. The normative contract is
[`014-tooling-and-quality-execution.md`](../specs/014-tooling-and-quality-execution.md)
and the transport schema is
[`ngin-tool-driver-v1.schema.json`](../schemas/ngin-tool-driver-v1.schema.json).

## Package Shape

A package exports separate tool, protocol-driver, and semantic-action
identities.

```xml
<Tool Name="Vendor.Audit.Product">
  <Exports>
    <Tool Name="audit" Kind="Development">
      <SystemExecutable Name="vendor-audit"
                        OverrideEnvironment="VENDOR_AUDIT"
                        VersionRange="[2.0.0,3.0.0)" />
    </Tool>
  </Exports>
</Tool>
<ToolDrivers>
  <Driver Name="audit-driver" Protocol="NGIN.ToolDriver/1"
          Executable="bin/audit-driver" Version="1.0.0" Probe="true">
    <Capabilities>
      <Capability Name="diagnostics" />
      <Capability Name="related-locations" />
      <Capability Name="fixes" />
    </Capabilities>
  </Driver>
</ToolDrivers>
<ToolActions>
  <Action Name="analyze" Kind="Analyze" Tool="audit" Driver="audit-driver"
          ToolVersionRange="[2.0.0,3.0.0)"
          DriverVersionRange="[1.0.0,2.0.0)">
    <Accepts Contract="cpp.translation-units/v1" />
    <Capabilities><Capability Name="diagnostics" /></Capabilities>
    <Environment>
      <Variable Name="AUDIT_MODE" Required="false" />
      <Variable Name="AUDIT_TOKEN" Secret="true" CacheKey="false" />
    </Environment>
  </Action>
</ToolActions>
```

Use a package-relative executable for a shipped driver. Use
`SystemExecutable` only for the underlying tool. Both processes run for the
resolved host platform; request inputs retain target platform and ABI.

## Request Handling

NGIN invokes a normal run as:

```text
audit-driver --ngin-request /absolute/generated/request.json
```

The request is one UTF-8 JSON document. Read arguments and paths as typed
fields and arrays; never pass them through a shell. Inputs are already selected,
so the driver must not rediscover files by walking the workspace.

- `files/v1` uses `inputSets[].files`.
- `cpp.translation-units/v1` uses `inputSets[].translationUnits`, including the
  exact compiler argument vector and command digest.
- `artifacts/v1` file entries have role `Artifact`.
- `tool.results/v1` uses `inputSets[].priorResultPaths`.

Only environment entries present in the request are authorized. A driver must
not read unrelated secrets from project files or print secret-bearing values.
The action declaration must match the project variable's secret classification.
Secret-bearing runs default to non-cacheable; `CacheKey="true"` explicitly
permits a value digest—not the value itself—to affect the cache key.

## Event Stream

Reserve stdout for one JSON event per line and send human/debug logs to stderr.
Every event carries the request `runId`; sequence starts at one and increments
by one. Emit exactly one final `run.completed` and nothing after it.

```json
{"schemaVersion":"1.0","kind":"NGIN.ToolDriver.Event","runId":"run-123","sequence":1,"type":"run.started","data":{}}
{"schemaVersion":"1.0","kind":"NGIN.ToolDriver.Event","runId":"run-123","sequence":2,"type":"diagnostic","data":{"severity":"warning","code":"audit-rule","message":"Example finding","fingerprint":"stable-native-id","primaryLocation":{"file":{"absolute":"/repo/src/main.cpp","workspaceRelative":"src/main.cpp"},"range":{"start":{"line":4,"column":2},"end":{"line":4,"column":8}}}}}
{"schemaVersion":"1.0","kind":"NGIN.ToolDriver.Event","runId":"run-123","sequence":3,"type":"run.completed","data":{"status":"succeeded","toolExitCode":0}}
```

Locations are one-based with an exclusive end. Findings do not make execution
fail. Translate tool-specific "findings found" exit codes inside the driver;
report crashes, invalid configuration, and invocation failures as failed
execution.

Proposed edits use applicability `automatic`, `suggested`, or `unsafe`. Include
the digest of every source file as observed by the driver. NGIN and editors
reject stale, out-of-workspace, malformed, or overlapping edits.

## Probe

When `Probe="true"`, NGIN invokes:

```text
audit-driver --ngin-probe /absolute/generated/probe-request.json
```

Return one `probe.completed` event. The driver—not the central CLI—owns any
tool-specific version command and parsing.

```json
{"schemaVersion":"1.0","kind":"NGIN.ToolDriver.Event","runId":"probe-123","sequence":1,"type":"probe.completed","data":{"available":true,"hostCompatible":true,"driverVersion":"1.0.0","toolVersion":"2.4.1","protocols":["NGIN.ToolDriver/1"],"capabilities":["diagnostics","fixes"]}}
```

Use `available:false` and an actionable `reason` for a missing tool, license,
runtime, or host prerequisite. Probe output is cached by driver/tool identity
and host, so it must describe only those inputs.

## Validation Checklist

- `ngin tool doctor` resolves the package and probe.
- `ngin graph --tooling-plan --format json` exposes tool, driver, action, run,
  input-set, dependency, policy, and report identities.
- A successful no-finding run still emits `run.completed`.
- Findings plus a supported findings exit remain execution success.
- Malformed JSONL, sequence gaps, EOF, crashes, cancellation, and timeout cannot
  be reported as success.
- Normalized JSON and declared SARIF reports contain stable fingerprints.
- Edit previews are non-mutating; explicit CLI apply and editor quick fixes
  reject stale source digests.
