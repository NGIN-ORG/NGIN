# NGIN Structured Conditions Plan

## Summary

NGIN manifests need a powerful condition model for selecting authored inputs
based on the active build and runtime configuration. The model should be more
toolable and less stringly typed than `.csproj`-style inline expressions.

This plan introduces a root-level `<Conditions>` section with named, structured
conditions. Manifest items can reference those conditions through `When="..."`.
Simple typed selectors can still exist later, but reusable named conditions
should be the first-class model for non-trivial selection.

Source ownership changes, including `<Sources><Public>` and
`<Sources><Private>`, are intentionally deferred. This plan defines conditions
as a general manifest capability first, then source selection can consume it
after the condition contract is settled.

## Goals

- Add reusable named conditions to `.nginproj`.
- Support AND, OR, and NOT logic without introducing a free-form expression
  language as the primary model.
- Keep conditions schema-validatable and inspectable by tooling.
- Make diagnostics explain why a conditional item was included or excluded.
- Match against existing configuration concepts such as operating system,
  architecture, build configuration, environment, and configuration name.
- Leave room for a later expression escape hatch without depending on it.

## Non-Goals

- Do not implement source visibility changes in this plan.
- Do not make `.csproj`-style string expressions the default condition model.
- Do not support arbitrary scripting.
- Do not add package restore or dependency resolution conditions yet.
- Do not export project conditions to referenced projects or packages.
- Do not allow packages to contribute conditions to consuming projects yet.
- Do not remove existing configuration-specific manifest sections.

## User-Facing Model

A project can define named conditions:

```xml
<Conditions>
  <Condition Name="Linux">
    <Match OperatingSystem="linux" />
  </Condition>

  <Condition Name="DebugBuild">
    <Match BuildConfiguration="Debug" />
  </Condition>

  <Condition Name="LinuxDebugBuild">
    <All>
      <ConditionRef Name="Linux" />
      <ConditionRef Name="DebugBuild" />
    </All>
  </Condition>
</Conditions>
```

Items can reference a named condition:

```xml
<Build Backend="CMake"
       Mode="Generated"
       Language="CXX"
       LanguageStandard="23">
  <CompileDefinitions>
    <Definition Value="NGIN_LINUX_DEBUG"
                Visibility="Private"
                When="LinuxDebugBuild" />
  </CompileDefinitions>
</Build>
```

Simple single-match conditions can use attributes directly on `<Condition>` as
shorthand:

```xml
<Conditions>
  <Condition Name="Windows" OperatingSystem="windows" />
  <Condition Name="Shipping" BuildConfiguration="Shipping" />
</Conditions>
```

This shorthand is equivalent to:

```xml
<Condition Name="Windows">
  <Match OperatingSystem="windows" />
</Condition>
```

Selector attributes on `Condition` are shorthand for one `Match` node. Multiple
selector attributes on `Condition` are combined with AND, exactly like multiple
attributes on `Match`.

On `Condition`, `Name` is the only non-selector attribute in v1. Any other
attribute must be a known selector attribute. Unknown attributes are validation
errors.

## Structured Logic

Conditions support five core node types:

- `Match`
- `All`
- `Any`
- `Not`
- `ConditionRef`

`Match` compares typed fields against the active selection context:

```xml
<Match OperatingSystem="linux"
       Architecture="x64"
       BuildConfiguration="Debug" />
```

Multiple attributes on one `Match` are combined with AND.

`All` requires every child condition node to match:

```xml
<All>
  <Match OperatingSystem="linux" />
  <Match BuildConfiguration="Debug" />
</All>
```

`Any` requires at least one child condition node to match:

```xml
<Any>
  <Match OperatingSystem="windows" />
  <Match OperatingSystem="linux" />
  <Match OperatingSystem="macos" />
</Any>
```

`Not` negates exactly one child condition node:

```xml
<Not>
  <Match BuildConfiguration="Shipping" />
</Not>
```

Named conditions can be reused through `ConditionRef`:

```xml
<Condition Name="DesktopDebugBuild">
  <All>
    <ConditionRef Name="Desktop" />
    <ConditionRef Name="DebugBuild" />
  </All>
</Condition>
```

Forward references should be allowed. A condition may reference another
condition that is declared later in the same `<Conditions>` section. Tooling
should collect all condition names before validating `ConditionRef` references
and cycles.

`ConditionRef` resolves only against the current project manifest's
`<Conditions>` section in the first implementation. Workspace conditions,
package-contributed conditions, imported condition files, and shared aliases are
deferred.

`ConditionRef` may also be used as the single body of a `Condition`, allowing
one condition to alias another condition:

```xml
<Condition Name="Posix">
  <ConditionRef Name="Linux" />
</Condition>
```

## Condition Body Rules

A `Condition` must define exactly one condition body:

- selector attributes on the `Condition` element, or
- exactly one child node.

Supported child nodes are:

- `Match`
- `All`
- `Any`
- `Not`
- `ConditionRef`

These forms are valid:

```xml
<Condition Name="Linux" OperatingSystem="linux" />
```

```xml
<Condition Name="Linux">
  <Match OperatingSystem="linux" />
</Condition>
```

```xml
<Condition Name="LinuxDebugBuild">
  <All>
    <ConditionRef Name="Linux" />
    <ConditionRef Name="DebugBuild" />
  </All>
</Condition>
```

This mixed form should be invalid:

```xml
<Condition Name="Mixed" OperatingSystem="linux">
  <Match BuildConfiguration="Debug" />
</Condition>
```

`All` and `Any` must contain at least one child condition node. `Not` must
contain exactly one child condition node. `Match` must contain at least one
selector attribute.

An empty `<Conditions />` section is valid and has no effect.

A project manifest may contain at most one root-level `<Conditions>` section.
When present, the canonical root order places `<Conditions>` after
`<Sources>` or legacy `<SourceRoots>` and before `<Output>`.

`All`, `Any`, and `Not` may contain only supported condition nodes. Unknown
child elements are validation errors. Non-whitespace text content inside
condition nodes is invalid.

Contradictory condition logic is valid. Validation checks structure,
references, and known selector values; it does not try to prove that a condition
can ever match.

```xml
<Condition Name="Impossible">
  <All>
    <Match OperatingSystem="linux" />
    <Match OperatingSystem="windows" />
  </All>
</Condition>
```

This condition is valid and simply evaluates to false.

Condition names should be manifest identifiers:

```text
[A-Za-z_][A-Za-z0-9_.-]*
```

Condition references are exact. Validation should also reject condition names
that differ only by case, such as `Linux` and `linux`, to avoid confusing
editor, filesystem, and CLI behavior.

## Selection Context

The first implementation should match against fields already present in the
selected project configuration and that do not create circular build semantics:

- `Configuration`
- `BuildConfiguration`
- `OperatingSystem`
- `Architecture`
- `Environment`

`Configuration` should match the selected configuration name.

`Configuration` and `BuildConfiguration` are distinct:

```xml
<Configuration Name="Runtime"
               BuildConfiguration="Debug"
               OperatingSystem="linux"
               Architecture="x64"
               Environment="local" />
```

In that example, `Configuration="Runtime"` matches the NGIN configuration name,
while `BuildConfiguration="Debug"` matches the backend/native build
configuration.

Selector attribute names are schema-defined and case-sensitive. Known selector
value domains should normalize values according to their domain rules. Unknown
or user-authored domains should remain exact unless their owning contract says
otherwise. `Environment` should match only the environment name in the first
implementation.

All first-version selector fields must be present on the resolved configuration
before condition evaluation. If a required field is absent, configuration
resolution or validation should fail before conditions are evaluated.

`EnableReflection` is intentionally left out of the first selection context. It
is a useful project capability flag, but it can also affect build generation.
Conditions should start with stable selection fields before matching against
capability flags that may influence generated build behavior.

Example:

```xml
<Condition Name="RuntimeConfiguration"
           Configuration="Runtime" />

<Condition Name="DebugBuild"
           BuildConfiguration="Debug" />

<Condition Name="RuntimeLinux"
           Configuration="Runtime"
           OperatingSystem="linux" />
```

## Evaluation Semantics

Condition evaluation should be pure and side-effect-free.

Conditions are evaluated after the selected project configuration has already
been resolved. They do not select the configuration, mutate the configuration,
change package resolution, or change runtime metadata. They only determine
whether a supported conditional authored item is included in the resolved
project model.

Validation should collect all condition declarations first, then validate
references, then validate cycles, then evaluate conditions.

Each named condition should be evaluated at most once per selected
configuration for inclusion decisions, with results reused by `ConditionRef`
evaluation. Trace-producing evaluation should still be able to collect child
results for all children of `All` and `Any` so explanations are complete.

## Item Usage

Conditional item support should be added incrementally.

The first implementation should support `When` on leaf build setting items:

```xml
<CompileDefinitions>
  <Definition Value="NGIN_DEBUG_TOOLS"
              Visibility="Private"
              When="DebugBuild" />
</CompileDefinitions>
```

If a supported item does not specify `When`, it is unconditional and is included
for every selected configuration:

```xml
<CompileDefinitions>
  <Definition Value="NGIN_ALWAYS_ON" />
  <Definition Value="NGIN_DEBUG_ONLY" When="DebugBuild" />
</CompileDefinitions>
```

`When` should reference exactly one named condition. If an item needs multiple
requirements, authors should define a named condition using `All`:

```xml
<Conditions>
  <Condition Name="LinuxDebugBuild">
    <All>
      <ConditionRef Name="Linux" />
      <ConditionRef Name="DebugBuild" />
    </All>
  </Condition>
</Conditions>
```

`When` is not a list, expression, or inline selector:

```xml
<!-- Invalid -->
<Definition Value="A" When="Linux;Debug" />

<!-- Invalid -->
<Definition Value="A" When="Linux Debug" />

<!-- Invalid -->
<Definition Value="A" When="Linux && Debug" />
```

The first implementation should not allow direct selector attributes on
conditional items. This keeps item syntax narrow and makes named conditions the
single place where conditional logic lives:

```xml
<!-- Preferred -->
<Definition Value="NGIN_LINUX" When="Linux" />

<!-- Deferred -->
<Definition Value="NGIN_LINUX" OperatingSystem="linux" />
```

Group-level `When` should be deferred:

```xml
<!-- Deferred -->
<CompileDefinitions When="DebugBuild">
  <Definition Value="A" />
  <Definition Value="B" />
</CompileDefinitions>
```

Condition filtering must preserve the authored order of included items.
Excluded items are removed without sorting, grouping, or reordering the
remaining items.

```xml
<CompileOptions>
  <Option Value="-Wall" />
  <Option Value="-Wextra" When="DebugBuild" />
  <Option Value="-Werror" />
</CompileOptions>
```

If `DebugBuild` does not match, the resolved option order is:

```text
-Wall
-Werror
```

The first build-setting surfaces should be:

- `CompileDefinitions`
- `CompileOptions`
- `LinkOptions`
- `IncludeDirectories`

Other manifest areas can adopt `When` later after their merge and validation
semantics are clear.

Potential later consumers:

- source roots and explicit files
- project and package references
- config sources
- content files
- runtime module enables
- environment features

## Deferred Selectors

The first selector set is intentionally limited to stable project
configuration fields. C++ build authoring will likely need toolchain-aware
selectors later, but only after the CLI has a stable resolved compiler and
toolchain identity before condition evaluation.

Potential later selector fields:

- `Compiler`
- `CompilerVersion`
- `Toolchain`
- `Generator`
- `PlatformSdk`
- `RuntimeLibrary`
- environment feature flags

NGIN should continue to prefer `OperatingSystem` and `Architecture` over a
generic `Platform` selector unless a separate first-class platform concept is
introduced.

## Diagnostics

Condition diagnostics should be explicit and actionable.

Validation should report:

- duplicate condition names
- condition names that differ only by case
- invalid condition names
- unknown `When` condition names
- unknown `ConditionRef` names
- cycles in `ConditionRef` references
- direct self-reference through `ConditionRef`
- conditions with no body
- conditions with both selector attributes and child nodes
- conditions with more than one child node
- unknown attributes on `Condition`
- unknown selector attributes
- unknown condition child elements
- non-whitespace text content inside condition nodes
- empty `Match` nodes
- invalid selector values when the value domain is known
- `Not` with zero or multiple child nodes
- `All` or `Any` with no child nodes
- `When` values that are lists, expressions, inline selectors, or invalid
  condition names

Condition evaluation should return trace information from the first
implementation, even if detailed traces are initially exposed only through
verbose validation or graph tooling.

```text
ConditionEvaluationResult
  condition: LinuxDebugBuild
  matched: false
  reason:
    All failed:
      ConditionRef named "Linux" matched:
        OperatingSystem expected "linux", actual "linux"
      ConditionRef named "DebugBuild" failed:
        BuildConfiguration expected "Debug", actual "Release"
```

That trace can power user-facing explanations:

```text
Definition NGIN_LINUX_DEBUG excluded:
  When="LinuxDebugBuild" did not match
  OperatingSystem was "linux"
  BuildConfiguration was "Release"
```

Graph or verbose diagnostic output should be able to show declared conditions,
whether each condition matched, and conditional build items with their
included/excluded state:

```text
Conditions:
  Linux: matched
  DebugBuild: not matched
  LinuxDebugBuild: not matched

Build settings:
  CompileDefinitions:
    NGIN_PLATFORM_LINUX included, When="Linux"
    NGIN_LINUX_DEBUG excluded, When="LinuxDebugBuild"
```

## Expression Escape Hatch

A string expression syntax should not be part of the first implementation.

If later needed, expressions should be contained inside named conditions rather
than scattered across individual manifest items:

```xml
<Condition Name="CustomLegacyCase"
           Expression="OperatingSystem == 'linux' and BuildConfiguration != 'Shipping'" />
```

Expression conditions should be treated as an escape hatch. They require a
stable grammar, typed values, clear diagnostics, and tooling support before they
belong in the contract.

## Manifest Contract Changes

`docs/specs/002-project-and-target-manifest.md` should be updated to add
`Conditions` as a supported root section.

The project root surface would become:

- `Sources`
- `SourceRoots`
- `Conditions`
- `Output`
- `Build`
- `References`
- `ConfigSources`
- `Runtime`
- `Environments`
- `Configurations`

The spec should define:

- condition names are unique within one project manifest
- condition names must be valid manifest identifiers
- condition names that differ only by case are rejected
- `When` references one project-local condition name
- `When` is supported only on leaf build setting items in the first
  implementation
- items without `When` are unconditional
- condition filtering preserves authored item order
- a project manifest may contain at most one root-level `Conditions` section
- `Condition` selector attributes are shorthand for one `Match` node
- `Name` is the only non-selector attribute allowed on `Condition`
- `When` and `ConditionRef` resolve only against project-local conditions in the
  first implementation
- condition matching happens against the selected project configuration
- condition evaluation is pure and side-effect-free
- a condition must have exactly one body
- `ConditionRef` is a condition node and forward references are allowed
- packages do not export or contribute conditions in the first implementation
- unknown condition references are validation errors
- evaluation should produce trace information
- structured condition nodes are preferred over expression strings

## Implementation Plan

1. Add condition authoring and evaluation model types.
2. Parse root-level `<Conditions>` from `.nginproj`.
3. Validate condition names, body shapes, selector attributes, references, and
   cycles.
4. Add condition evaluation against `ConfigurationDefinition` with trace output.
5. Add focused tests for validation and evaluation before build integration.
6. Add `When` support to leaf build setting authoring models.
7. Filter generated CMake build settings according to evaluated conditions.
8. Add generated CMake tests for conditional build settings.
9. Add verbose diagnostic or graph output powered by condition evaluation
   traces.
10. Update project manifest spec documentation and README examples.

## Suggested Test Cases

- A simple shorthand condition matches `OperatingSystem="linux"`.
- A shorthand condition does not match a different operating system.
- `All` matches only when all child nodes match.
- `Any` matches when at least one child node matches.
- `Not` negates a child match.
- `ConditionRef` can reference another condition.
- `ConditionRef` can reference a condition declared later in the manifest.
- `ConditionRef` can be used as the single body of a condition alias.
- A condition that directly references itself fails validation.
- Cyclic `ConditionRef` references fail validation.
- Unknown `When` references fail validation.
- Duplicate condition names fail validation.
- Condition names that differ only by case fail validation.
- Invalid condition names fail validation.
- Mixed selector attributes and child nodes fail validation.
- Unknown child elements inside condition nodes fail validation.
- Non-whitespace text content inside condition nodes fails validation.
- Empty `Match` nodes fail validation.
- An empty `<Conditions />` section is valid and has no effect.
- Multiple root-level `<Conditions>` sections fail validation.
- Items without `When` are included unconditionally.
- `When` values containing lists or expressions fail validation.
- Contradictory condition logic is valid and evaluates to false.
- `When` on `CompileDefinitions` includes matching definitions.
- `When` on `CompileDefinitions` omits non-matching definitions.
- A non-matching conditional compile definition is absent from generated CMake
  input, not emitted behind a CMake generator expression.
- Filtering conditional build settings preserves authored item order.
- Group-level `When` fails validation in the first implementation.
- `When` on include directories, compile options, and link options follows the
  same evaluation path.
- Evaluation records enough trace information to explain why a condition matched
  or did not match.

## Open Questions

- Should a later version add workspace-level shared condition aliases?
- Should a later version add environment feature flag selectors?
- Should a later version allow direct typed selector attributes on build setting
  items, or should conditional item usage continue to go through `When`?

## Decisions For Later Versions

### Workspace-Level Condition Aliases

Workspace-level shared condition aliases are deferred. The first implementation
keeps all conditions project-local. A later version may add workspace-level
conditions for large multi-project repositories, but lookup should be explicit
rather than implicit so projects do not silently depend on workspace-global
state.

### Environment Feature Selectors

Environment feature flag selectors are deferred. In v1, `Environment` matches
only the selected environment name. Feature matching should only be added after
environment features are first-class, typed, and resolved before condition
evaluation.

### Direct Selectors On Items

Direct typed selector attributes on build setting items should remain deferred
and are not preferred. Conditional item usage should continue to go through
`When` and named conditions unless a strong future usability need justifies
adding a second syntax.
