# Tool driver protocol

Tool packages separate three identities:

- the underlying executable;
- the driver that speaks `NGIN.ToolDriver/1`;
- the semantic action, such as analyze or format.

NGIN resolves inputs and writes a UTF-8 request document. It invokes the driver
without a shell:

```text
driver --ngin-request /absolute/path/request.json
```

The driver writes one JSON event per line to stdout and human diagnostics to
stderr. Events use `NGIN.ToolDriver.Event`, carry the request `runId`, start at
sequence one, and end with exactly one `run.completed` event.

```json
{"schemaVersion":"1.0","kind":"NGIN.ToolDriver.Event","runId":"run-1","sequence":1,"type":"run.started","data":{}}
{"schemaVersion":"1.0","kind":"NGIN.ToolDriver.Event","runId":"run-1","sequence":2,"type":"run.completed","data":{"status":"succeeded","toolExitCode":0}}
```

Malformed JSON, sequence gaps, mismatched run IDs, missing completion, timeout,
output-limit breach, or a nonzero driver exit is an execution failure.

Drivers may report diagnostics, proposed edits, artifacts, metrics, logs, and
progress. Proposed edits include the observed file digest. NGIN rejects stale,
out-of-workspace, malformed, or overlapping edits and owns all source mutation.

Only environment entries present in the request are authorized. Secret values
must not be logged. Secret-bearing runs are non-cacheable unless the action
explicitly permits a value digest to participate in the key.

Probe-capable drivers are invoked with `--ngin-probe` and return one
`probe.completed` event describing availability, compatibility, versions,
protocols, and capabilities.

Normative schemas:

- [`ngin-tool-driver-v1.schema.json`](../schemas/ngin-tool-driver-v1.schema.json)
- [`ngin-tool-result-v1.schema.json`](../schemas/ngin-tool-result-v1.schema.json)
- [`ngin-tool-baseline-v1.schema.json`](../schemas/ngin-tool-baseline-v1.schema.json)

Tool command process codes are `0` for success, `1` for a failed gate or
required changes in check mode, `2` for an invalid or unavailable plan, `3` for
execution failure, `4` for cancellation, and `5` for timeout.

See [Tool driver authoring](../guides/tool-driver-authoring.md) for a package
example and validation checklist.
