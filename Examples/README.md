# Examples

The public examples are intentionally small. Each one demonstrates one real
end-to-end behavior that can be validated, built, run, and inspected.

## Learning Path

1. [`Hello.Native`](Hello.Native/README.md)
   Plain C++ executable managed by the NGIN CLI. No `NGIN.Core`.

2. [`Hello.Hosted`](Hello.Hosted/README.md)
   Smallest `NGIN.Core` hosted application with a real static module and
   manifest-selected runtime composition.

3. [`Hello.Reflection`](Hello.Reflection/README.md)
   Reflection code generation through the `NGIN.Reflection.MetaGen` package.

## Naming

Examples use CMake-like profiles:

```text
Debug
Release
Debug.Asan
Debug.Reflection
```

The examples avoid `Runtime` as a default profile name because runtime already
has a separate meaning in `NGIN.Core`.

## What Moved Out

Older showcase, game, and basic examples were removed from the public learning
path because they modeled too many future or overlapping ideas at once.
Project-reference collision manifests now live under CLI test fixtures instead
of `Examples/`.
