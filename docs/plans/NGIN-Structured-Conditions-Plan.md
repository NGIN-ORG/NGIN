# NGIN Structured Conditions Plan

Status: Implemented

## Summary

NGIN manifests need one coherent selection model for authored inputs. Direct
typed selectors and named conditions should not be separate systems. They should
normalize into the same structured condition model and evaluate against the same
resolved selection context.

This plan introduces project-local named conditions through a root-level
`<Conditions>` section and refactors manifest selection so all supported item
selection uses one engine:

- direct selector attributes for simple local AND-only selection
- `When="..."` for reusable named conditions
- direct selector attributes plus `When` on the same item as implicit AND
- structured `All`, `Any`, `Not`, `Match`, and `ConditionRef` nodes for
  reusable non-trivial logic

The user-facing goal is a simple ladder of complexity. Authors should write the
short form for common cases and use named conditions only when the condition is
shared, has OR/NOT logic, or benefits from a meaningful name.

## Goals

- Unify direct typed selectors and `When` under one manifest selection engine.
- Add reusable named conditions to `.nginproj`.
- Allow direct selectors and `When` on the same item as implicit AND.
- Support AND, OR, and NOT logic without making free-form string expressions
  the primary model.
- Keep condition authoring schema-validatable and inspectable by tooling.
- Make diagnostics explain why a selected item was included or excluded.
- Preserve concise direct selectors for simple local conditions.
- Reuse the same selection semantics across sources, build settings, and later
  manifest surfaces where selection is appropriate.
- Leave room for a later expression escape hatch without depending on it.

## Non-Goals

- Do not replace direct typed selectors with mandatory named conditions.
- Do not introduce arbitrary scripting.
- Do not make free-form string expressions the default condition model.
- Do not let conditions select or mutate the active project profile.
- Do not add package restore or dependency resolution conditions in the first
  implementation.
- Do not export project conditions to referenced projects or packages in the
  first implementation.
- Do not allow packages to contribute conditions to consuming projects yet.
- Do not remove existing configuration-specific manifest sections.

## Authoring Model

### Simple Local Selection

Simple AND-only selection should use direct selector attributes:

```xml
<Definition Value="NGIN_DEBUG_TOOLS"
            Visibility="Private"
            BuildType="Debug" />
```

Multiple direct selector attributes are combined with AND:

```xml
<File Path="src/platform/LinuxDebug.cpp"
      OperatingSystem="linux"
      BuildType="Debug" />
```

### Reusable Named Selection

Reusable conditions are declared once and referenced by name:

```xml
<Conditions>
  <Condition Name="Desktop">
    <Any>
      <Match OperatingSystem="windows" />
      <Match OperatingSystem="linux" />
      <Match OperatingSystem="macos" />
    </Any>
  </Condition>
</Conditions>

<Definition Value="NGIN_DESKTOP"
            Visibility="Private"
            When="Desktop" />
```

### Combined Local And Reusable Selection

`When` and direct selectors may be used together. The effective selection is an
implicit AND:

```xml
<Definition Value="NGIN_DESKTOP_DEBUG"
            Visibility="Private"
            When="Desktop"
            BuildType="Debug" />
```

That item is equivalent to:

```xml
<All>
  <ConditionRef Name="Desktop" />
  <Match BuildType="Debug" />
</All>
```

This avoids tiny one-off named conditions when only part of the condition needs
to be reusable.

### Group Selection

Groups may carry selection when the group has clear containment semantics. A
group-level condition is inherited by every child as an implicit AND.

```xml
<CompileDefinitions When="Desktop">
  <Definition Value="NGIN_DESKTOP" />
  <Definition Value="NGIN_DESKTOP_DEBUG"
              BuildType="Debug" />
</CompileDefinitions>
```

The second definition is selected only when both `Desktop` and
`BuildType="Debug"` match.

`<Files>` already behaves like a group for line-separated file lists:

```xml
<Files OperatingSystem="linux">
  src/platform/linux_window.cpp
  src/platform/linux_entry.cpp
</Files>
```

The same inheritance rule should apply if `When` is added to `<Files>`:

```xml
<Files When="Desktop" OperatingSystem="linux">
  src/platform/linux_window.cpp
  src/platform/linux_entry.cpp
</Files>
```

## Conditions Section

A project may define reusable named conditions:

```xml
<Conditions>
  <Condition Name="Linux">
    <Match OperatingSystem="linux" />
  </Condition>

  <Condition Name="DebugBuild">
    <Match BuildType="Debug" />
  </Condition>

  <Condition Name="LinuxDebugBuild">
    <All>
      <ConditionRef Name="Linux" />
      <ConditionRef Name="DebugBuild" />
    </All>
  </Condition>
</Conditions>
```

Simple single-match conditions can use selector attributes directly on
`<Condition>` as shorthand:

```xml
<Conditions>
  <Condition Name="Windows" OperatingSystem="windows" />
  <Condition Name="Shipping" BuildType="Shipping" />
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

On `Condition`, `Name` is the only non-selector attribute in the base model.
Any other attribute must be a known selector attribute. Unknown attributes are
validation errors.

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
       BuildType="Debug" />
```

Multiple attributes on one `Match` are combined with AND.

`All` requires every child condition node to match:

```xml
<All>
  <Match OperatingSystem="linux" />
  <Match BuildType="Debug" />
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
  <Match BuildType="Shipping" />
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

Forward references are allowed. A condition may reference another condition
that is declared later in the same `<Conditions>` section. Tooling should
collect all condition names before validating `ConditionRef` references and
cycles.

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

## Selection Normalization

Every selectable item should normalize to an effective condition tree.

An item with no direct selectors and no `When` is unconditional:

```xml
<Definition Value="NGIN_ALWAYS_ON" />
```

Normalized form:

```text
Always
```

An item with direct selectors becomes one `Match`:

```xml
<Definition Value="NGIN_LINUX_DEBUG"
            OperatingSystem="linux"
            BuildType="Debug" />
```

Normalized form:

```xml
<Match OperatingSystem="linux"
       BuildType="Debug" />
```

An item with only `When` becomes one `ConditionRef`:

```xml
<Definition Value="NGIN_DESKTOP"
            When="Desktop" />
```

Normalized form:

```xml
<ConditionRef Name="Desktop" />
```

An item with both direct selectors and `When` becomes an `All`:

```xml
<Definition Value="NGIN_DESKTOP_DEBUG"
            When="Desktop"
            BuildType="Debug" />
```

Normalized form:

```xml
<All>
  <ConditionRef Name="Desktop" />
  <Match BuildType="Debug" />
</All>
```

If a parent group has a selection condition, the child effective condition is
the AND of all ancestor selection conditions and the child selection condition.

## Condition Body Rules

A `Condition` must define exactly one body:

- selector attributes on the `Condition` element, or
- exactly one child condition node.

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

This mixed form is invalid:

```xml
<Condition Name="Mixed" OperatingSystem="linux">
  <Match BuildType="Debug" />
</Condition>
```

`All` and `Any` must contain at least one child condition node. `Not` must
contain exactly one child condition node. `Match` must contain at least one
selector attribute.

An empty `<Conditions />` section is valid and has no effect.

A project manifest may contain at most one root-level `<Conditions>` section.
When present, the canonical root order places `<Conditions>` after `<Sources>`
or legacy `<SourceRoots>` and before `<Output>`.

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

Condition references are exact. Validation should reject condition names that
differ only by case, such as `Linux` and `linux`, to avoid confusing editor,
filesystem, and CLI behavior.

## Selection Context

The first implementation should match against fields already present in the
selected project profile and that do not create circular build semantics:

- `Profile`
- `BuildType`
- `OperatingSystem`
- `Architecture`
- `Environment`

`Profile` matches the selected project profile name.

`Profile` and `BuildType` are distinct:

```xml
<Profile Name="Runtime"
               BuildType="Debug"
               OperatingSystem="linux"
               Architecture="x64"
               Environment="local" />
```

In that example, `Profile="Runtime"` matches the NGIN profile name,
while `BuildType="Debug"` matches the backend/native build
configuration.

Selector attribute names are schema-defined and case-sensitive. Known selector
value domains should normalize values according to their domain rules. Unknown
or user-authored domains should remain exact unless their owning contract says
otherwise. `Environment` should match only the environment name in the first
implementation.

Only selector fields used by the effective condition need to be present on the
resolved configuration. If an item or condition uses a selector whose value is
absent from the selected configuration, validation or evaluation should fail
with an actionable diagnostic for that selector. Conditions should not make
currently optional configuration attributes globally required.

`EnableReflection` is intentionally left out of the first selection context. It
is a useful project capability flag, but it can also affect build generation.
Conditions should start with stable selection fields before matching against
capability flags that may influence generated build behavior.

## Evaluation Semantics

Condition evaluation is pure and side-effect-free.

Selection is evaluated after the selected project profile has already
been resolved. Selection does not choose the configuration, mutate the
configuration, change package resolution, or change runtime metadata. It only
determines whether a supported authored item is included in the resolved project
model.

Validation should collect all condition declarations first, then validate
references, validate cycles, normalize selectable items, and evaluate effective
conditions for the selected configuration.

Each named condition should be evaluated at most once per selected
configuration for inclusion decisions, with results reused by `ConditionRef`
evaluation. Trace-producing evaluation should still be able to collect child
results for all children of `All` and `Any` so explanations are complete.

Selection filtering must preserve the authored order of included items.
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

## Supported Surfaces

The first complete selection engine should cover the surfaces that already have
direct typed selector support plus `When` on those same surfaces.

Initial selectable source entries:

- `Sources/Public/Root`
- `Sources/Public/File`
- `Sources/Public/Files`
- `Sources/Private/Root`
- `Sources/Private/File`
- `Sources/Private/Files`

Initial selectable build setting entries:

- `IncludeDirectories/Directory`
- `CompileDefinitions/Definition`
- `CompileOptions/Option`
- `LinkOptions/Option`

Initial selectable build setting groups:

- `IncludeDirectories`
- `CompileDefinitions`
- `CompileOptions`
- `LinkOptions`

Other manifest areas can adopt the same selection model after their merge and
validation semantics are clear.

Potential later consumers:

- project and package references
- config inputs
- content files
- runtime module enables
- environment features

## Source Selection Semantics

Source selection must keep the existing nested-root exclusion behavior.

If a selected source root contains a nested source root or file entry whose
effective condition does not match, generated source scanning must exclude the
non-selected nested path from the broader root scan.

```xml
<Sources>
  <Private>
    <Root Path="src" />
    <Root Path="src/platform/windows" OperatingSystem="windows" />
    <Root Path="src/platform/linux" OperatingSystem="linux" />
  </Private>
</Sources>
```

For a linux configuration, `src/platform/windows` is excluded from the broad
`src` scan and `src/platform/linux` is included.

The same rule applies when `When` participates in the effective condition:

```xml
<Sources>
  <Private>
    <Root Path="src" />
    <Root Path="src/tools/debug"
          When="DeveloperTools"
          BuildType="Debug" />
  </Private>
</Sources>
```

If the effective condition for `src/tools/debug` does not match, that nested
path is excluded from the broad `src` scan.

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
- selector fields used by an effective condition but absent from the selected
  configuration
- `Not` with zero or multiple child nodes
- `All` or `Any` with no child nodes
- `When` values that are lists, expressions, inline selectors, or invalid
  condition names
- `When` on a manifest surface that has not adopted selection

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
        BuildType expected "Debug", actual "Release"
```

Graph or verbose diagnostic output should be able to show declared conditions,
effective item conditions, and included/excluded state:

```text
Conditions:
  Desktop: matched
  DebugBuild: not matched
  DesktopDebugBuild: not matched

Build settings:
  CompileDefinitions:
    NGIN_DESKTOP included:
      When="Desktop" matched
    NGIN_DESKTOP_DEBUG excluded:
      When="Desktop" matched
      BuildType expected "Debug", actual "Release"
```

## Expression Escape Hatch

A string expression syntax should not be part of the first implementation.

If later needed, expressions should be contained inside named conditions rather
than scattered across individual manifest items:

```xml
<Condition Name="CustomLegacyCase"
           Expression="OperatingSystem == 'linux' and BuildType != 'Shipping'" />
```

Expression conditions should be treated as an escape hatch. They require a
stable grammar, typed values, clear diagnostics, and tooling support before they
belong in the contract.

## Manifest Contract Changes

`docs/specs/002-project-and-target-manifest.md` should be updated to add
`Conditions` as a supported root section and to define selection as a shared
manifest concept.

The project root surface would become:

- `Sources`
- `SourceRoots`
- `Conditions`
- `Output`
- `Build`
- `References`
- `ConfigInputs`
- `LocalSettings`
- `Runtime`
- `Environments`
- `Profiles`

The spec should define:

- condition names are unique within one project manifest
- condition names must be valid manifest identifiers
- condition names that differ only by case are rejected
- `When` references one project-local condition name
- direct typed selectors remain supported on selectable source entries and
  build setting entries
- direct typed selectors and `When` may be combined on the same selectable item
  as implicit AND
- selectable groups may provide inherited selection when the group surface
  explicitly supports it
- items without `When`, direct selector attributes, or inherited group
  selection are unconditional
- effective condition filtering preserves authored item order
- source selection preserves nested-root exclusion semantics
- a project manifest may contain at most one root-level `Conditions` section
- `Condition` selector attributes are shorthand for one `Match` node
- `Name` is the only non-selector attribute allowed on `Condition`
- `When` and `ConditionRef` resolve only against project-local conditions in the
  first implementation
- condition matching happens against the selected project profile
- condition evaluation is pure and side-effect-free
- a condition must have exactly one body
- `ConditionRef` is a condition node and forward references are allowed
- packages do not export or contribute conditions in the first implementation
- unknown condition references are validation errors
- evaluation should produce trace information
- structured condition nodes are preferred over expression strings

## Implementation Plan

1. Introduce shared manifest selection model types.
2. Represent direct selector attributes, `When`, and inherited group selection
   as one normalized condition tree.
3. Add condition authoring and evaluation model types.
4. Parse root-level `<Conditions>` from `.nginproj`.
5. Validate condition names, body shapes, selector attributes, references, and
   cycles.
6. Add condition evaluation against `ProfileDefinition` with trace output.
7. Refactor existing direct selector matching to use the shared selection
   engine.
8. Apply the shared selection engine to source entries while preserving
   nested-root exclusion behavior.
9. Apply the shared selection engine to build setting entries and supported
   build setting groups.
10. Add focused tests for validation, normalization, and evaluation before
    broad build integration.
11. Add generated CMake tests for conditional source and build setting
    selection.
12. Add verbose diagnostic or graph output powered by selection traces.
13. Update project manifest spec documentation and README/guide examples.

## Suggested Test Cases

- A simple direct selector matches `OperatingSystem="linux"`.
- A direct selector does not match a different operating system.
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
- Mixed selector attributes and child nodes on `Condition` fail validation.
- Unknown child elements inside condition nodes fail validation.
- Non-whitespace text content inside condition nodes fails validation.
- Empty `Match` nodes fail validation.
- An empty `<Conditions />` section is valid and has no effect.
- Multiple root-level `<Conditions>` sections fail validation.
- Items without `When`, direct selectors, or inherited group selectors are
  included unconditionally.
- A build setting item with both `When` and direct selectors is normalized as
  implicit AND.
- A source entry with both `When` and direct selectors is normalized as
  implicit AND.
- A group with `When` passes an inherited condition to child entries.
- A child entry with direct selectors and inherited group `When` is normalized
  as implicit AND.
- `When` values containing lists or expressions fail validation.
- Contradictory condition logic is valid and evaluates to false.
- `When` on `CompileDefinitions` includes matching definitions.
- `When` on `CompileDefinitions` omits non-matching definitions.
- Direct selectors on build settings continue to emit the same generated CMake
  as before.
- A non-matching conditional compile definition is absent from generated CMake
  input, not emitted behind a CMake generator expression.
- Filtering conditional build settings preserves authored item order.
- `When` on include directories, compile options, and link options follows the
  same evaluation path.
- `When` on source roots includes matching roots.
- `When` on source roots excludes non-matching nested roots from broader
  selected root scans.
- Evaluation records enough trace information to explain why an item matched or
  did not match.

## Migration And Compatibility

Existing manifests that use direct typed selectors remain valid.

The implementation should preserve the behavior of existing direct selector
attributes by routing them through the new shared selection engine. Generated
CMake output for existing manifests should remain behaviorally unchanged.

New manifests can adopt named conditions incrementally. Authors do not need to
rewrite simple direct selector attributes into named conditions.

## Open Questions

- Should group-level selection be available on all build setting groups in the
  first implementation, or should it start with leaf entries and `<Files>` only?
- Should later workspace-level shared condition aliases be supported?
- Should later environment feature flag selectors be supported?
- Should `When` ever accept more than one condition name, or should authors
  continue using named `All` conditions for that case?

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

### Expression Conditions

Expression conditions are deferred. If added later, expression syntax should
live only inside named conditions and should normalize to the same evaluation
trace model as structured conditions.
