# Dependency lock and reproducibility

NGIN uses three deliberately separate derived identities:

| Identity | Covers | Does not cover |
| --- | --- | --- |
| Dependency lock | Exact acquired PackageInstances and artifact-selection facts | Active Exports, capability bindings, Actions, staging choices |
| Composition fingerprint | The complete canonical semantic Composition Graph | Backend/executor-only settings |
| Plan fingerprint | One complete derived executor plan plus its adapter identity | Unrelated plans |

All fingerprints are lowercase SHA-256 values prefixed with `sha256:`. They are
computed from typed canonical JSON envelopes; they never use platform-dependent
hash functions.

## Lock document

The dependency lock is generated output, not XML authoring. Its canonical JSON
root is `NGIN.DependencyLock`. Each package entry records:

- NGIN package name and exact semantic version;
- source binding, host or target context, and PackageInstance fingerprint;
- provider kind, native coordinate, native version, and revision;
- integrity and provider-native artifact identity;
- whether the provider result is hermetic;
- the BinaryCompatibility facts used by the artifact;
- artifact-affecting Options.

Entries are ordered by PackageInstance fingerprint and object members are
lexicographically ordered. Parsing and reserializing a valid lock therefore
produces the same bytes and lock fingerprint.

The lock intentionally excludes active Exports, CapabilityBindings, Actions,
Tools, Plugins, contributions, and ordinary composition-only Options. If one of
those choices acquires another package—such as a selected Action's host Tool—the
new PackageInstance appears in the lock naturally.

## Verification and invalidation

Lock verification resolves the current graph and compares its derived lock with
the stored lock. Failures identify the package and exact changed field, such as
provider version, revision, integrity, artifact identity, compatibility, or
artifact Options. Added and removed packages are reported separately.

Locked CI enables both `requireIntegrity` and `requireHermetic`. A system package
or mutable external installation is reported honestly as non-hermetic and fails
that policy; NGIN does not turn it into a reproducible result merely because it
has been named in a lock.

Composition verification compares the separately stored composition
fingerprint. Consequently, changing an active Export or Action without changing
the dependency closure leaves the dependency lock byte-stable while changing
the composition fingerprint. Changing an artifact Option changes the
PackageInstance and invalidates the lock.
