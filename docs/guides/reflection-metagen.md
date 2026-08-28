# Reflection metadata generation

`NGIN.Reflection.MetaGen` is a C++23 header scanner for opt-in runtime
reflection. It has no LLVM, libclang, compiler-SDK, or compiler-AST dependency.
It asks the target's selected C++ compiler to preprocess the declared reflection
headers, scans only NGIN's supported annotated declaration surface, writes a
versioned `ReflectionModel`, and emits ordinary `TypeBuilder<T>` code for the
target compiler to validate.

## Select headers explicitly

Enable the package Action and declare its header inputs:

```xml
<Uses>
  <Package Name="NGIN.Reflection" Version="0.1" />
</Uses>
<Generate Using="NGIN.Reflection.MetaGen/ReflectionCodegen" Version="0.1">
  <Header Include="src/**/*.hpp" />
</Generate>
```

MetaGen does not recursively claim every header reachable through an include.
The resolved Action input list is the reflection ownership boundary and is
recorded in generated diagnostic metadata.

## Annotation syntax

Include `<NGIN/Reflection/Annotations.hpp>`. Annotation values use a small,
declarative grammar: strings, numbers, Booleans, identifiers, lists, and
namespaced key-value metadata. They are data, not arbitrary C++ expressions.

```cpp
NGIN_REFLECT(Name = "Demo::Player",
             Attributes = (Serialize::Required = true))
struct Player
{
    std::string displayName;

    NGIN_IGNORE
    int transientCounter{};

    NGIN_METHOD(Name = "add_experience")
    void AddExperience(int amount) const & noexcept;
};
```

Built-in keys are case-insensitive. Unknown namespaced keys are preserved in
the model and emitted as runtime attributes where the runtime metadata kind
supports attributes.

| Declaration | Default in an `NGIN_REFLECT` type | Override |
| --- | --- | --- |
| Public base | Included | Non-public bases are excluded |
| Public field | Included | `NGIN_IGNORE`; `NGIN_FIELD(...)` customizes |
| Private/protected field | Excluded | `NGIN_FIELD(...)` plus `NGIN_GENERATED_BODY()` |
| Enum value | Included | `NGIN_IGNORE`; `NGIN_ENUM_VALUE(...)` customizes |
| Method | Excluded | `NGIN_METHOD(...)` includes it |
| Getter/setter property | Excluded | Matching `NGIN_PROPERTY(Name = "...")` annotations |
| Default constructor | Inferred once | An explicit `NGIN_CTOR()` replaces inference |
| Other constructor | Excluded | `NGIN_CTOR(...)` or `NGIN_INJECT` includes it |
| Nested type | Excluded | Its own `NGIN_REFLECT(...)` includes it |

`NGIN_GENERATED_BODY()` expands to one stable friend declaration. It is needed
only when generated code addresses a non-public declaration; it does not
include a filename-specific generated header.

## Supported declaration surface

The scanner supports non-template classes, structs, and enums; namespaces and
nested-name qualification; direct public bases; ordinary fields; annotated
methods with static, const, volatile, lvalue-ref, and `noexcept` qualifiers;
property accessors; annotated constructors and dependency metadata; and
compiler-preprocessed conditional declarations. Exact member-pointer casts are
always generated, so ordinary overloads do not rely on ambiguous `&Type::Name`
deduction.

Open templates, anonymous or local types, rvalue-qualified runtime methods,
declarations produced by stateful macros, and compiler extensions without a
portable member expression are outside the supported surface. An annotation on
unsupported syntax is an error at the original header location with a remedy.
Unusual declarations can use the existing `NginReflect` or `Describe<T>` manual
descriptor API. Annotate the owning non-template type with
`NGIN_REFLECT(Manual = true)` to skip generated member description while still
including that authored descriptor in automatic module aggregation.

## Generated output

The Action emits:

- one descriptor/registration include per owned header;
- one deterministic module aggregate source;
- a versioned `ReflectionModel` JSON diagnostic artifact; and
- an ownership manifest used to remove stale per-header output.

Headers are sorted by normalized path and declarations retain source order.
Files are rewritten only when their content changes. The target compiler's
compilation-database command supplies its real include directories,
definitions, language mode, architecture options, and platform configuration
to preprocessing.

Generated output stays below the selected build output and must not be edited.
The runtime registry format is independent of the scanner frontend so a future
C++ standard-reflection backend can produce the same metadata.

The package tool also supports inspection against an authored generator
context:

```bash
ngin-metagen --context <context.xml> --explain Demo::Player
ngin-metagen --context <context.xml> --dump-model
```

`--explain` reports inclusion reasons, normalized names, exact generated member
expressions, source locations, and cache keys. `--dump-model` writes the same
versioned JSON model used by generated diagnostic artifacts to standard output.
