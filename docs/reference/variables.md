# Variables

Manifest strings can refer to resolved values with `$(Name)` syntax.

Common project and build variables include:

| Variable | Meaning |
| --- | --- |
| `$(ProjectName)` | Selected project name |
| `$(ProjectVersion)` | Selected project version |
| `$(ProjectDir)` | Directory containing the project manifest |
| `$(ProfileName)` | Selected profile |
| `$(OutputName)` | Product output name |
| `$(OutputDir)` | Selected staged output directory |
| `$(StageDir)` | Staging directory |
| `$(GeneratedDir)` | Generated-file directory |
| `$(GeneratorContext)` | Generator context file path |

Tool drivers additionally support variables defined by their adapter contract,
including `$(InputFile)`, `$(InputContentFile)`, `$(Config)`,
`$(WorkspaceRoot)`, `$(ProjectPath)`, `$(WorkingDirectory)`, and
`$(OutputDirectory)`.

Availability depends on the element being resolved. Validation fails when a
required variable is unavailable in that context.

Use the CLI to inspect project environment values and their source:

```bash
ngin variables explain --project App.nginproj --profile Debug
```

Secret values are shown as `<secret>` or `<redacted>` and are not copied into
graph JSON, diagnostics, logs, diffs, or generated launch metadata.

`ngin settings init` creates the local settings file under
`.ngin/local/user.nginsettings` and ensures it is ignored by source control.
