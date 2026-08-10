# Placeholders

Manifest placeholders use `${name}` syntax. They are typed and phase-limited;
unknown names, unavailable values, recursive values, and values that violate
their declared type are errors.

The backend-neutral registry contains:

| Placeholder | Type | Available phases |
| --- | --- | --- |
| `${project.name}` | identifier | output, stage, launch, publish |
| `${project.version}` | semantic version | output, publish |
| `${configuration}` | identifier | output, stage, launch, publish |
| `${target.os}` | identifier | output, stage, launch, publish |
| `${target.architecture}` | identifier | output, stage, launch, publish |
| `${output.name}` | filename | stage, launch, publish |
| `${workspace.root}` | path | local execution only |

Values used in canonical Composition Graph identity must be machine-portable.
Local filesystem values such as `${workspace.root}` are deliberately excluded
from canonical identity and can be expanded only while deriving a local
execution plan.

The CMake Action adapter additionally expands these execution-only arguments
inside package Action `<Argument>` values:

| Placeholder | Meaning |
| --- | --- |
| `${ProjectDir}` | directory containing the selected project |
| `${BuildDir}` | generated CMake build directory |
| `${ActionOutputDir}` | isolated Action output root |
| `${ActionContext}` | generated context document for Generate Actions |

These adapter placeholders are resolved into an ActionPlan; they are not
backend-neutral graph fields and do not leak CMake paths into package semantics.
Secrets are references in Launch intent, not placeholders. The CLI refuses to
launch when secret references exist and no external secret provider is
configured; it never copies secret values into graph JSON or generated plans.
