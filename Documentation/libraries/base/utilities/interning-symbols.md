---
title: String interning and symbols
description: Deduplicate names, understand returned view lifetime, and distinguish local IDs from durable identity.
---

# String interning and symbols

`StringInterner<Allocator, ThreadPolicy>` stores one owned copy of each distinct
string. `InsertOrGet` returns a numeric `IdType`; `Intern` returns a stable view
into interner-owned storage; `TryGetId` and `View` perform reverse lookup.

```cpp
NGIN::Utilities::StringInterner<> names;
auto id = names.InsertOrGet("position");
std::string_view name = names.View(id);
```

The returned view borrows storage. It remains valid only under the interner's
documented lifetime/mutation rules and never outlives the interner. `Statistics`,
`Size`, and `TotalStoredBytes` help diagnose memory use.

The default thread policy is a no-op mutex and is not thread-safe. Supply a
compatible mutex policy when multiple threads mutate/read one interner, and
make that policy part of the owner's design rather than adding ad hoc external
locks.

`SymbolTable` layers `Meta::SymbolId` over an interner. `Intern` returns a
symbol, `TryGet` resolves an existing name, and `View` recovers text. A symbol's
numeric value is local to that table. Persist the name or define a versioned
external symbol mapping when identity must survive rebuilding the table.

Intern only data with a bounded vocabulary or explicit resource policy.
Interning attacker-controlled unbounded strings creates an intentional
never-deduplicated memory growth path.
