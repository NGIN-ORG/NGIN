# NGIN.Base XML Breaking Performance Refactor Plan

Status: Implemented

Date: 2026-07-24

## Purpose

This document records the deliberately breaking XML refactor in `NGIN.Base`,
including the alternatives evaluated, the chosen design, the repository
migration, and the measured result.

The work follows the
[serialization review and refactor plan](NGIN-Base-Serialization-Review-And-Refactor-Plan.md).
Backward source and ABI compatibility were intentionally not preserved.

## Relationship To NGIN V4

XML is an authoring frontend for NGIN V4. It is not the Composition Graph and
does not contain product or schema policy.

The refactor therefore preserves these boundaries:

1. Semantic XML retains source identity and byte spans for diagnostics and
   provenance.
2. Lossless formatting remains the responsibility of `SyntaxDocument`.
3. Schema conversion remains above `NGIN.Base`.
4. DOM-free consumers can continue to use the direct event parser.

## Executive Decision

The implemented design uses:

- opaque state-plus-index value views
- no pointer-returning view API and no cached `ElementView` table
- compact 32-bit source offsets, IDs, spans, and ranges
- one-pass DOM construction with sibling-linked child IDs
- contiguous attribute storage
- allocation-free child, filtered-child, and attribute iteration
- one shared allocator-backed memory budget for retained DOM and arena storage
- source-preserving owning and borrowed parse modes
- an explicit in-situ mode that mutates caller-provided storage
- the existing direct event parser for consumers that do not need a DOM
- the existing lossless syntax document for formatting and editor workflows

The original proposal favored exactly sized immutable child tables. Prototypes
showed that exact two-pass construction reduced allocations and committed
memory further, but its additional grammar scan caused a material throughput
regression. The selected sibling-link representation keeps one-pass parsing,
eliminates per-element child allocations, and avoids a final compaction pass.

## Goals And Outcomes

### API

- [x] Make XML views cheap values no larger than 16 bytes.
- [x] Remove pointer identity from the public contract.
- [x] Remove public fields that mirror internal records.
- [x] Make filtered traversal allocation-free.
- [x] Replace repository-local child-vector helpers with native ranges.
- [x] State source ownership, scratch lifetime, and in-situ mutation rules.

### Performance

- [x] Remove the cached `ElementView` table and finalization pass.
- [x] Remove per-element temporary attribute and child vectors from parsing.
- [x] Replace expanded record fields with compact document-relative data.
- [x] Enforce memory limits at allocation boundaries.
- [x] Preserve zero-copy source slices when decoding is unnecessary.
- [x] Decode directly into retained arena or in-situ storage.
- [x] Combine UTF-8 and XML-character validation into one pass.
- [x] Add fast delimiter scans for long text and attribute runs.

### Correctness

- [x] Preserve the strict XML profile.
- [x] Preserve duplicate-attribute primary and related spans.
- [x] Preserve UTF-8 and XML 1.0 character validation.
- [x] Preserve input, depth, node, member, decoded-byte, and memory limits.
- [x] Preserve deterministic `LimitExceeded` versus `OutOfMemory` results at
  allocation boundaries.
- [x] Preserve semantic and lossless syntax behavior.

## Non-Goals

- XML Schema, DTD validation, namespace resolution, or external entities.
- Incremental input without a demonstrated NGIN consumer.
- A compatibility layer for the removed pointer APIs.
- Making the semantic DOM lossless.
- Matching pugixml by removing validation, diagnostics, limits, or provenance.
- Adding a third-party runtime dependency.

## Baseline

Representative Release measurements before this breaking refactor were:

| Workload | NGIN owning DOM | TinyXML2 DOM | pugixml DOM |
| --- | ---: | ---: | ---: |
| Project manifest | approximately 0.0015 ms | approximately 0.0009 ms | approximately 0.0002 ms |
| 100 KiB elements | approximately 1.5 ms | approximately 0.6 ms | approximately 0.1 ms |
| 1 MiB elements | approximately 18 ms | approximately 7 ms | approximately 3 ms |

The 1 MiB NGIN document reported:

- 1,048,600 source bytes
- 32,113 nodes
- 14,021,936 used bytes
- 18,125,696 committed bytes

These are same-machine development baselines, not portable absolute
guarantees. Competitor parsers also do not implement an equivalent strictness,
diagnostic, limit, or provenance contract.

## Public API Break

### Value views

`ElementView`, `AttributeView`, and `NodeView` are immutable value handles.
They retain:

- a pointer to stable document state
- a compact record index

They do not own data and do not expose mutable record fields. Copying a view is
the normal operation.

Moving a document preserves views because its state remains stable. Destroying
the document invalidates its views.

### Removed pointer APIs

The migration removed:

```cpp
Document::RootPtr();
BorrowedDocument::RootPtr();
NodeView::ElementPtr();
ElementView::FirstChildPtr();
```

The canonical operations now return values or `std::optional`:

```cpp
ElementView Document::Root() const noexcept;
std::optional<ElementView> NodeView::TryElement() const noexcept;
std::optional<ElementView>
ElementView::FirstChild(std::string_view name) const noexcept;
```

No compatibility adapters remain.

### Range contract

`Attributes()`, `Children()`, and `Children(name)` return cheap range values.
Iteration constructs views on demand and performs no allocation:

```cpp
for (auto package : root.Children("Package"))
{
    Consume(package);
}
```

The XML builder now enforces a tree rather than a graph:

- a child handle may appear only once
- an attached node cannot be reparented
- an attached node cannot be selected as the document root

## Parse Modes

### Owning

```cpp
Expected<Document, ParseDiagnostic>
Parse(OwnedTextBuffer input, ...);
```

The document owns and preserves the exact source bytes. Unescaped semantic text
references those bytes directly.

### Borrowed

```cpp
Expected<BorrowedDocument, ParseDiagnostic>
ParseBorrowed(BorrowedTextView input, ParseScratch& scratch, ...);
```

The completed document borrows only the input buffer. `ParseScratch` is
temporary reusable workspace and is not retained by the document.

### In-situ

```cpp
Expected<Document, ParseDiagnostic>
ParseInSitu(MutableTextBuffer input, ...);
```

This mode is explicit because it mutates the supplied source:

- entity references are compacted in place
- XML line endings are normalized in place
- semantic views reference the mutated buffer
- source spans continue to use original byte offsets
- `SourceText()` is not a lossless copy of the authored input after success

Callers that require exact source bytes use owning, borrowed, or syntax parsing.
In-situ mode shares the same grammar and strictness rules rather than forming a
permissive parser fork.

## Internal Representation

### Compact records

The retained representation uses:

- one document-level `SourceId`
- `CompactSpan { UInt32 begin, UInt32 end }`
- `TextRef` containing a source offset or a high-bit-tagged decoded-text ID
- `TableRange` for contiguous attributes
- `SiblingRange` plus `NodeRecord::nextSibling` for children

Element nodes reuse the otherwise unused node-name payload for their compact
element-record index. The actual element name is stored once in
`ElementRecord`.

Compile-time budgets enforce:

| Type | Maximum |
| --- | ---: |
| `ElementView`, `AttributeView`, `NodeView` | 16 bytes |
| `AttributeRange`, `ChildRange` | 16 bytes |
| `NodeRecord` | 32 bytes |
| `ElementRecord` | 32 bytes |
| `AttributeRecord` | 40 bytes |

Source spans and semantic views are reconstructed lazily. The input must fit
the compact 31-bit source-address space; larger inputs fail with
`LimitExceeded`.

### Child construction

The parser appends each child node once and links it to its previous sibling.
An element stores only the first child and child count. The parser temporarily
uses the not-yet-final element end offset as its last-child construction ID;
the closing tag replaces that temporary value with the final source offset.

This representation provides:

- one grammar pass
- no per-element child vector
- no global child-ID table
- no construction tape
- no compaction pass
- allocation-free ordered iteration

Indexed access to the Nth child is linear. Existing NGIN consumers traverse or
filter children sequentially, so this is a favorable trade.

### Shared allocation budget

`AllocationBudget` and `BudgetAllocator<T>` route retained parser storage
through `ParseResources`:

- node, element, and attribute tables
- decoded-text arena blocks
- arena block metadata
- XML builder retained storage

Owned source bytes are included in document accounting and deducted from the
available storage budget. Borrowed source bytes are not retained allocations.

The document exposes:

```cpp
MemoryUsed();
MemoryCommitted();
PeakMemoryCommitted();
AllocationCount();
```

Limits are checked at allocation boundaries. The old repeated full
`WithinMemoryLimit()` scan after node insertion is gone.

## Construction Prototypes And Decision

Three internal strategies were measured.

### One-pass tape plus compaction

This removed per-element vectors but retained temporary construction records
and rewrote records into final tables.

Representative result:

- 1 MiB committed memory: approximately 6.75 MiB
- 100 KiB parse: approximately 0.85 ms

The extra writes and peak overlap made this inferior to direct sibling links.

### Exact two-pass prepass

A structural prepass counted exact table sizes, followed by direct fill.

Representative result:

- 1 MiB committed memory: approximately 5.16 MiB
- retained allocations: 3
- 100 KiB parse: approximately 0.86 ms versus approximately 0.76 ms for
  sibling links
- 1 MiB parse: approximately 7.62 ms versus approximately 6.76 ms for sibling
  links

The memory result was excellent, but the second grammar scan lost enough
throughput to fail the throughput-first decision gate.

### Selected one-pass sibling links

Sibling links provided the best balance across:

- manifest latency
- 100 KiB and 1 MiB throughput
- retained and peak memory
- allocation count
- implementation complexity
- diagnostic parity

Both losing prototypes were removed. There is one semantic DOM construction
path.

## Scanner And Decoding

The implemented scanner work is intentionally narrower than a complete parser
rewrite:

- UTF-8 decoding and XML 1.0 character validation share one validation pass
- text delimiter scans use `string_view::find`
- attribute scans use fast searches for the quote terminator and forbidden
  `<`
- parsed names produce compact source `TextRef` values directly
- text decoding returns `TextRef` directly
- in-situ decoding writes into the source buffer at original offsets

This removes repeated scans and temporary pointer classification in common
paths while retaining exact diagnostic offsets.

A full grammar-fused SIMD scanner was not implemented. It remains a possible
future optimization only if profiling demonstrates a worthwhile gain without
weakening truncated-input safety, multibyte validation, or diagnostic spans.

## Consumer Migration

The value API was migrated through:

- `Tools/NGIN.CLI/src/Support.hpp`
- `Tools/NGIN.CLI/src/Authoring.cpp`
- `Tools/NGIN.CLI/src/Build.cpp`
- `Tools/NGIN.CLI/src/Commands.cpp`
- `Packages/NGIN.Core/src/NGIN/Core/Application.cpp`
- `Packages/NGIN.Core/src/NGIN/Core/Loader.cpp`
- `Packages/NGIN.Reflection.MetaGen/src/MetaGenContext.cpp`

Consumers now use:

- value views instead of `const XmlElement*`
- `std::optional<XmlElement>` for absence
- `Root()`, `TryElement()`, and `FirstChild()`
- direct filtered ranges instead of allocated vectors of child pointers

## Final Benchmark Results

Release results below use 200 samples on the same machine and toolchain.

### Semantic parsing

| Workload | NGIN owning | NGIN borrowed | NGIN in-situ | TinyXML2 | pugixml |
| --- | ---: | ---: | ---: | ---: | ---: |
| Package manifest | 0.000549 ms | 0.000521 ms | 0.000549 ms | — | — |
| Project manifest | 0.000979 ms | 0.000887 ms | 0.000917 ms | 0.000723 ms | 0.000227 ms |
| 1 KiB elements | 0.00575 ms | 0.00623 ms | 0.00588 ms | — | — |
| 100 KiB elements | 0.810 ms | 0.574 ms | 0.657 ms | 0.436 ms | 0.0765 ms |
| 1 MiB elements | 6.30 ms | 6.12 ms | 7.20 ms | 5.99 ms | 2.13 ms |
| 20 KiB entities | 0.0465 ms | 0.0389 ms | 0.0296 ms | 0.0156 ms | 0.0139 ms |
| 64-level nesting | 0.00490 ms | — | — | — | — |

### Other XML modes

| Workload | Result |
| --- | ---: |
| Direct events, project manifest | 0.000612 ms |
| Direct events, 100 KiB elements | 0.456 ms |
| Lossless syntax, project manifest | 0.00133 ms |
| Lossless syntax, 100 KiB elements | 0.542 ms |
| Writer, 100 KiB output | 0.414 ms |

### Memory

| Workload | Source | Nodes | Used | Committed | Peak committed | Allocations |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Package | 79 B | 2 | 271 B | 271 B | 303 B | 6 |
| Project | 129 B | 5 | 513 B | 577 B | 705 B | 12 |
| 1 KiB elements | 1,057 B | 36 | 5,569 B | 6,753 B | 8,769 B | 32 |
| 100 KiB elements | 102,425 B | 3,236 | 516,537 B | 591,577 B | 764,217 B | 65 |
| 1 MiB elements | 1,048,600 B | 32,113 | 5,158,968 B | 6,619,800 B | 8,586,104 B | 83 |
| Entities | 20,506 B | 2 | 24,738 B | 24,763 B | 24,795 B | 7 |
| Nested | 453 B | 65 | 4,581 B | 6,469 B | 8,485 B | 24 |

Compared with the baseline, the owning parser improved by approximately:

- 35% on the project manifest
- 46% on 100 KiB
- 65% on 1 MiB
- 63% in both retained used and committed memory for 1 MiB

### Competitor interpretation

NGIN is now close to TinyXML2 on the large flat workload:

- owning NGIN is about 1.86 times slower at 100 KiB
- borrowed NGIN is about 1.32 times slower at 100 KiB
- owning NGIN is about 1.05 times slower at 1 MiB
- borrowed NGIN is about 1.02 times slower at 1 MiB

pugixml remains substantially faster:

- roughly 8 to 11 times faster at 100 KiB, depending on NGIN ownership mode
- roughly 3 times faster at 1 MiB

This comparison is directional rather than contract-equivalent. NGIN retains
strict UTF-8/XML-character validation, duplicate diagnostics, source identity,
precise spans, configurable limits, and deterministic budget errors.

In-situ mode is not universally fastest. It wins strongly on entity-heavy
input because decoding stays in the source buffer, but writes and compaction
cost more on the 1 MiB empty-element workload. The API remains useful because
it exposes that trade explicitly rather than mutating default inputs.

## Verification

Completed verification:

- focused XML parser tests
- XML/JSON corpus tests
- Release XML benchmark and competitor comparison
- Clang ASan and UBSan XML parser test run
- `ngin_cli` build
- `ngin_reflection_metagen` build
- focused CLI manifest-authoring, package-product, and XML-format tests
- `Hello.Native`, `Hello.Hosted`, and `Hello.Reflection` validation
- `NGIN.Core` build and all 40 Core CTest cases

The full CLI test executable was attempted but exceeded the available
approximately 64-second command window. The focused XML-dependent CLI tests
passed; this timeout is recorded as an incomplete broad run, not a test
failure.

On Windows, the sanitizer executable requires the Clang ASan runtime directory
on `PATH`:

```powershell
$env:PATH = 'C:\Program Files\LLVM\lib\clang\22\lib\windows;' + $env:PATH
```

## Remaining Optional Work

The implemented refactor meets its performance and memory gates. Further work
should be measurement-driven:

1. Profile real V4 manifests to separate strict validation, grammar, DOM
   construction, and decoding costs.
2. Prototype grammar-fused ASCII scanning only if validation remains a leading
   cost.
3. Consider optional per-element name indexes only for demonstrated
   high-fan-out lookup workloads.
4. Revisit exact preallocation only if memory becomes more important than
   throughput for a concrete deployment profile.

Do not reintroduce cached views, per-element vectors, dual construction paths,
or default source mutation.

## Completion Definition

This refactor is complete because:

- the old pointer APIs and cached view table are removed
- all known repository consumers use the value API
- child and attribute traversal allocate no memory
- the retained representation meets its record-size budgets
- memory limits are enforced through the shared allocation budget
- explicit in-situ parsing is implemented and documented
- losing construction prototypes are removed
- correctness, integration, sanitizer, benchmark, and memory checks pass
- performance and memory improvements exceed the planned gates

