# Use semantic merge laws and compact build selection

## Status

Accepted

## Context

The previous profile/overlay model treated unrelated concepts as values that
overlay one another. Version constraints, required files, package options,
build items, policy, and capability requirements do not share one valid merge
operation. Arbitrary profile and variant dimensions also make the selection
space difficult to understand and reproduce.

## Decision

Every semantic category declares its own identity, merge law, authority, and
conflict rule. In particular:

- version constraints intersect;
- required dependencies and contributions accumulate;
- workspace policy gates rather than being overwritten;
- scalar defaults refine through declared authority levels;
- build items use Include, Exclude, Remove, and Update;
- option assignments use declared typed authority;
- capability requirements accumulate and resolve through version-compatible
  capability bindings.

Fundamental build selection consists of Configuration, structured Target,
Toolchain, and typed Options. Target owns OS, architecture, and platform
environment/version facts. Toolchain owns compiler, language ABI, standard and
runtime libraries, and linker facts. Binary compatibility is derived from
Target, Toolchain, Configuration, linkage, and artifact-affecting Options.

Presets expand to concrete command and selection inputs before semantic
resolution. A preset name is not part of graph identity. Runtime environments
belong to launch, stage, or publish intent; a real compile-time distinction is
a typed Option.

## Consequences

XML order is not an override mechanism. Equal-authority conflicts are errors.
There is no universal Profile, Variant, Condition expression, or overlay
object. Resolver code uses category-specific operations and reports the merge
law involved in a conflict.
