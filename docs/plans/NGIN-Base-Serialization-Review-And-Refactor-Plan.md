# NGIN.Base Serialization Review And Breaking Refactor Plan

Status: Implemented on 2026-07-23

Implementation record:

- compact immutable JSON/XML semantic views and explicit owned, borrowed, and
  JSON in-situ entry points are shipped in `NGIN.Base`
- XML also has a validated lossless syntax document
- strict profiles, source spans, limits, diagnostics, segmented decoded-string
  storage, allocator injection, reusable scratch, builders, DOM writers,
  stateful sink writers, and explicitly contiguous event APIs are present
- legacy archive and ambiguous reader APIs have been removed
- the CLI, `NGIN.Core`, MetaGen, JWT, and PASETO callers use the new APIs
- checked-in corpora, focused tests, optional fuzz targets, and workload-shaped
  benchmarks cover the contract

Verification at implementation:

- 33 focused serialization/include tests passed
- 40 `NGIN.Core` tests passed
- CLI tool-driver tests passed (34 assertions across 5 cases)
- `ngin_cli`, `ngin_reflection_metagen`, `JsonBenchmarks`, and
  `XmlBenchmarks` built successfully
- `Hello.Native`, `Hello.Hosted`, and `Hello.Reflection` validated successfully

Scope:

- `NGIN.Base` JSON parsing, DOM, event parsing, and archive APIs
- `NGIN.Base` XML parsing, DOM, event parsing, and archive APIs
- shared serialization input, diagnostics, allocation, and writing facilities
- integration with the native `ngin` CLI, `NGIN.Core`, and
  `NGIN.Reflection.MetaGen`

Compatibility policy:

- this proposal is intentionally breaking
- existing API shapes should not be retained solely for source compatibility
- migration adapters should be added only when they reduce implementation risk
  during a short-lived transition; they are not part of the target design

## Summary

The current JSON and XML implementation is a strong prototype with several
useful low-level techniques:

- parsing operates on contiguous memory rather than streams
- errors use `Expected` and a structured payload
- delimiter scanning uses SIMD-aware helpers in important paths
- unescaped strings and text can be represented as views
- nesting depth is explicitly limited
- decoded strings use arena storage
- both DOM and event-driven parsing surfaces exist

Those choices fit the systems-oriented direction of `NGIN.Base`.

The public ownership, storage, and error contracts do not yet meet the needs of
a foundation library, however. The most important problems are:

1. DOM string lifetime is borrowed implicitly and can become invalid after
   ordinary temporary use or object movement.
2. DOM storage uses a fixed slab whose capacity is guessed from source length.
3. several `noexcept` mutation and interning APIs can terminate on allocation
   failure.
4. `InputCursor` can perform undefined out-of-range pointer arithmetic.
5. some advertised parser options, most notably JSON trailing commas, do not
   implement their stated behavior.
6. JSON stores every number as `F64`, losing integer identity and precision.
7. the XML parser accepts a loosely defined subset rather than enforcing a
   clear well-formed XML contract.
8. the event APIs are called streaming APIs but require the entire input and
   allocate storage proportional to it.
9. the DOM query surface is so small that each NGIN subsystem reimplements the
   same traversal and conversion helpers.
10. the archive layer has no real consumers, no generic serialization
    protocol, and no text writer.
11. test and benchmark coverage is much narrower than the public surface and
    performance goals.

The recommended direction is not a patch-by-patch stabilization of the current
public API. It is a replacement built around:

- explicit owned, borrowed, and in-situ source types
- compact indexed documents with stable handles and contiguous ranges
- allocator injection plus explicit parse limits, without caller-supplied
  arena-size guesses
- safe JSON number variants
- a deliberate, strict XML profile
- source spans and lazily computed line/column diagnostics
- immutable query views and separate builders
- direct JSON and XML writers
- genuinely incremental parsers only where a demonstrated consumer needs them
- removal of the current virtual archive family

## Relationship To NGIN V4

NGIN V4 treats XML as the first official authoring frontend and the Composition
Graph as the product contract. Authored XML is therefore not just configuration
input. It participates in:

- project, package, workspace, and definition authoring
- provenance
- diagnostics
- formatting
- editor integration
- graph construction
- runtime-side manifest loading

JSON is similarly important as the public representation of the Composition
Graph and as the protocol format for tool-driver and editor event streams.

This produces an important ownership boundary:

- `NGIN.Base` should own reliable JSON/XML syntax processing, document views,
  diagnostics, and writers
- the NGIN manifest and graph layers should own V4 element names, schema rules,
  defaults, product semantics, and graph construction

The base parser should not know what an `Application`, `Package`, `Stage`, or
`Runtime` element means. It should make it easy and safe for the higher layer to
implement those meanings with precise source locations and without repeatedly
allocating traversal results.

## Current Architecture

The implementation currently consists of:

- shared input and error types:
  - `Serialization/Core/InputCursor.hpp`
  - `Serialization/Core/ParseError.hpp`
- JSON:
  - `JsonTypes.hpp`
  - `JsonParser.hpp` and `JsonParser.cpp`
  - `JsonArchive.hpp` and `JsonArchive.cpp`
- XML:
  - `XmlTypes.hpp`
  - `XmlParser.hpp` and `XmlParser.cpp`
  - `XmlArchive.hpp` and `XmlArchive.cpp`
- parser tests:
  - `tests/Serialization/JsonParserTests.cpp`
  - `tests/Serialization/XmlParserTests.cpp`
- microbenchmarks:
  - `benchmarks/JsonBenchmarks.cpp`
  - `benchmarks/XmlBenchmarks.cpp`

The dominant DOM strategy is:

1. create one `LinearAllocator` slab
2. optionally perform a pre-pass to count container sizes
3. placement-construct object/array/element containers in the slab
4. store child/member collections in allocator-aware vectors
5. keep most source strings as `std::string_view`
6. allocate decoded strings from the same slab

This is simple and can be fast on small inputs, but its public contract exposes
too much of the storage strategy while leaving ownership and capacity
requirements implicit.

## Design Principles For The Replacement

The replacement should follow these principles.

### Correctness Before Fast Paths

SIMD scanning and arena allocation are valuable only after:

- bounds safety is proven
- source ownership is explicit
- parser options behave as documented
- invalid encodings and malformed syntax are handled consistently
- allocation failure follows one contract

### Fast Common Paths Without Unsafe Defaults

The common NGIN cases should remain cheap:

- small manifest documents
- repeated JSONL event records
- mostly ASCII names
- mostly unescaped strings
- shallow object and element trees
- repeated access to a small number of attributes or object members

The default API must nevertheless remain correct for:

- temporary inputs
- moved documents
- large flat arrays
- attribute-heavy XML
- escape-heavy text
- malformed data at the end of a short buffer
- explicit resource limits and allocation failure

### Type-Encoded Lifetime And Mutation

An option bit should not silently change the lifetime model of the returned
document. Owned, borrowed, and mutable in-situ parsing should be distinct entry
points or distinct source types.

Similarly, immutable parsed documents and mutable output construction should
not share public fields and partially safe mutation methods.

### Honest Scope

The XML component should choose one of two identities:

1. a tested general XML 1.0 parser with clearly documented unsupported
   facilities, or
2. a strict, explicitly named XML profile designed for NGIN-style manifests

The current middle ground is difficult to reason about: it accepts some invalid
XML, silently ignores some constructs, and rejects some valid XML without
defining a complete profile.

## Detailed Finding 1: Implicit Borrowed String Lifetime

### Current behavior

The parser returns `JsonDocument` and `XmlDocument`, names that normally imply
ownership. Most names, keys, text values, CDATA values, and unescaped strings
instead remain `std::string_view` references into the caller's buffer.

The reader overload is different: it buffers the input and calls
`AdoptInput(...)` after parsing. That means documents produced by different
overloads have different ownership behavior even though they have the same
type.

The current documentation states that the caller should keep the input alive:

- [serialization README](../../Dependencies/NGIN/NGIN.Base/include/NGIN/Serialization/README.md)
- [JSON string-view storage](../../Dependencies/NGIN/NGIN.Base/include/NGIN/Serialization/JSON/JsonTypes.hpp)
- [XML string-view storage](../../Dependencies/NGIN/NGIN.Base/include/NGIN/Serialization/XML/XmlTypes.hpp)

NGIN consumers compensate with wrapper structures containing both the source
string and the parsed document:

- [CLI `LoadedXml`](../../Tools/NGIN.CLI/src/Support.hpp)
- [`NGIN.Core` application loading](../../Packages/NGIN.Core/src/NGIN/Core/Application.cpp)
- [`NGIN.Reflection.MetaGen` context loading](../../Packages/NGIN.Reflection.MetaGen/src/MetaGenContext.cpp)

### Why this is unsafe

The following shape is legal at the type level and produces borrowed views into
a temporary:

```cpp
auto parsed = JsonParser::Parse(std::string{"{\"name\":\"NGIN\"}"});
```

The wrapper approach is also not a complete type-level guarantee. Moving a
wrapper can relocate a small-string-optimized source while the document keeps
views to the old address. Whether return-value optimization happens should not
determine document validity.

The API also gives no quick way to answer:

- does this document own its source?
- may this document be moved?
- may the caller release the input?
- did escaped strings allocate while unescaped strings remain borrowed?
- does mutation introduce additional borrowed values?

### Recommended solution

Use explicit source ownership types and make the normal document self-contained.

Suggested conceptual API:

```cpp
struct OwnedTextBuffer;
struct MutableTextBuffer;
struct BorrowedTextView;

Expected<JsonDocument, ParseDiagnostic>
ParseJson(OwnedTextBuffer input, const JsonParseOptions& options = {});

Expected<BorrowedJsonDocument, ParseDiagnostic>
ParseJson(BorrowedTextView input,
          ScratchAllocator& scratch,
          const JsonParseOptions& options = {});

Expected<JsonDocument, ParseDiagnostic>
ParseJsonInSitu(MutableTextBuffer input,
                const JsonParseOptions& options = {});
```

The exact source wrappers can be shared across JSON and XML. Important semantic
requirements are:

- `JsonDocument` and `XmlDocument` own everything needed by their views
- borrowed document types are visibly borrowed
- borrowed documents cannot outlive their source by accident where C++ type
  mechanics can reasonably prevent it
- in-situ parsing takes ownership of the mutable buffer rather than mutating an
  arbitrary caller span and returning a document that still depends on it
- moving an owning document does not invalidate node, member, attribute, or
  string handles

### Rejected solution: always copy every string

Always copying strings would solve lifetime correctness but discard an
important performance property. Owning the original source buffer allows most
strings to remain zero-copy while making the document self-contained.

### Acceptance criteria

- parsing a temporary owned string is safe
- moving a document does not invalidate any document-derived view
- an owning document has no hidden reference to caller memory
- borrowed parsing is unmistakable in names and types
- tests cover short SSO-sized input, large input, move construction, move
  assignment, and return through `Expected`

## Detailed Finding 2: Fixed Arena Capacity Guess

### Current behavior

Unless the caller supplies `arenaBytes`, each parser allocates approximately:

```text
input bytes * 2 + 4096
```

This assumes a stable relationship between source text size and DOM storage
size. No such relationship exists across document shapes.

A number-heavy JSON array can use only a few source bytes per value while each
DOM value requires a tagged representation aligned for a `string_view`.
Similarly, XML with many short empty elements can require an element object and
child entry for a small amount of source text.

For inputs below the precompute threshold, container vectors grow
geometrically. Because the allocator is monotonic and individual deallocation
is a no-op, old vector buffers continue consuming the slab.

NGIN consumers already expose the failure of the default heuristic by selecting
larger multipliers:

- [CLI XML loading](../../Tools/NGIN.CLI/src/Support.hpp)
- [`NGIN.Core` loader](../../Packages/NGIN.Core/src/NGIN/Core/Loader.cpp)
- [`NGIN.Core` manifest loading](../../Packages/NGIN.Core/src/NGIN/Core/Application.cpp)
- [MetaGen XML loading](../../Packages/NGIN.Reflection.MetaGen/src/MetaGenContext.cpp)

### Performance consequences

If the estimate is too small:

- valid documents fail with `OutOfMemory`
- failure depends on document shape rather than a documented resource limit
- callers retry only by knowing implementation details

If the estimate is too large:

- small and medium documents reserve unnecessary memory
- repeated JSONL parsing produces avoidable heap traffic
- peak memory becomes input-size-based even for event parsing that retains no
  DOM

### Recommended solution

Remove `arenaBytes` from normal parse options.

Use two complementary storage strategies:

1. compact indexed DOM storage for predictable node/member/attribute costs
2. a segmented monotonic allocator for decoded strings and uncommon auxiliary
   storage

A segmented arena grows by adding blocks. It preserves bump-allocation speed and
pointer stability without requiring a perfect initial capacity. The parser
should accept a resource budget rather than a capacity guess:

```cpp
struct ParseLimits
{
    UIntSize maxInputBytes;
    UIntSize maxDepth;
    UIntSize maxNodes;
    UIntSize maxMembers;
    UIntSize maxDecodedStringBytes;
    UIntSize maxTotalMemoryBytes;
};
```

The allocator itself should be injectable:

```cpp
Expected<JsonDocument, ParseDiagnostic>
ParseJson(OwnedTextBuffer input,
          AllocatorRef allocator,
          const JsonParseOptions& options,
          const ParseLimits& limits);
```

Defaults should be practical and documented. A caller should adjust semantic
limits, not reverse-engineer `sizeof(JsonValue)`.

### Precomputation strategy

A pre-pass remains worthwhile only if it changes the storage result materially.
With compact arrays, the first pass can count exact nodes and structural ranges,
then one allocation per primary table can be made before the fill pass.

The current pre-pass repeats much of the grammar and still relies on a slab
multiplier. The replacement should either:

- perform a cheap structural scan that produces counts/tape offsets, or
- use chunked tables that grow predictably in one pass

Both should be benchmarked against actual NGIN workloads.

### Acceptance criteria

- callers do not supply DOM byte estimates
- a valid document fails only because of allocator failure or an explicit parse
  limit
- large flat JSON arrays and large collections of short XML elements parse
  within documented memory bounds
- memory use is recorded in benchmarks as peak committed and peak used bytes
- event parsing does not allocate memory proportional to input unless decoding
  requires it

## Detailed Finding 3: Inconsistent Allocation And `noexcept` Contract

### Current behavior

Several APIs are declared `noexcept` while calling containers or hash maps whose
growth can throw:

- `JsonObject::Set`
- `JsonObject::BuildIndex`
- `JsonDocument::InternString`
- `XmlElement::BuildAttributeIndex`
- `XmlDocument::InternString`
- most `JsonArchive` and `XmlArchive` operations

The DOM parse path catches `std::bad_alloc` around part of parsing, but:

- precompute vectors may allocate before the catch block
- `noexcept` interning functions terminate before an outer catch can observe
  the exception
- reader buffering can throw independently
- public mutation has a different failure contract from parsing

### Why this matters

NGIN.Base explicitly prefers typed failures and disciplined exception
specifications. A method returning `bool` strongly suggests that allocation
failure will result in `false`. If it can terminate instead, the apparent
contract is more dangerous than an ordinary throwing method.

### Recommended solution

Use one of these contracts consistently per API family:

- parser and builder operations return `Expected<T, Error>`
- low-level fallible allocation helpers return nullable/result values and all
  dependent containers propagate them without throwing
- ordinary C++ containers may throw, and the public function is not `noexcept`

For serialization, the recommended public contract is typed failure:

```cpp
enum class SerializationErrorCode
{
    ParseFailure,
    InvalidOperation,
    TypeMismatch,
    DuplicateMember,
    LimitExceeded,
    OutOfMemory,
    SinkFailure,
};
```

Builders should return results rather than `bool`:

```cpp
Expected<JsonObjectMemberHandle, BuildError>
JsonObjectBuilder::Add(std::string_view key, JsonValue value);
```

`noexcept` should be retained only for:

- pure queries
- stable handle comparisons
- operations that provably do not allocate
- destruction and moves whose member types provide the same guarantee

### Acceptance criteria

- no `noexcept` function calls a potentially throwing container operation
- allocation failure never terminates unless the configured global allocation
  policy explicitly chooses termination
- parser, builder, writer, and sink failures have distinct error codes
- failure-path tests use a deterministic failing allocator

## Detailed Finding 4: `InputCursor` Bounds Safety

### Current behavior

`InputCursor::Peek(offset)` forms `m_current + offset` and only then compares the
result to the end pointer. If `offset` exceeds the valid one-past range, the
pointer arithmetic itself is undefined.

Parser literal checks intentionally peek ahead several bytes:

```cpp
cursor.Peek(1);
cursor.Peek(2);
cursor.Peek(3);
cursor.Peek(4);
```

Short malformed inputs therefore exercise this path naturally.

The pointer-based representation also complicates empty input, location
tracking, and the meaning of `Advance(count)` across CRLF normalization.

### Recommended solution

Represent the cursor as:

```cpp
std::span<const Byte> input;
UIntSize offset;
```

Then implement lookahead through integer bounds checks:

```cpp
[[nodiscard]] char Peek(UIntSize lookahead = 0) const noexcept
{
    if (lookahead >= input.size() - offset)
        return '\0';
    return static_cast<char>(input[offset + lookahead]);
}
```

The actual implementation must guard `offset > input.size()` before the
subtraction, although the cursor invariant should also prohibit that state.

Line and column tracking should not be embedded in every `Advance` operation.
Keep byte offset as the parser's source of truth and compute line/column lazily
from:

- a newline-offset table built on demand, or
- a scan from the nearest cached line boundary

This keeps fast parsing branch-light while improving diagnostic accuracy.

### Acceptance criteria

- empty input and every lookahead distance are bounds-safe
- fuzzing short malformed literals does not trigger ASan or UBSan
- `Advance(n)` always means exactly `n` bytes
- CRLF handling affects diagnostic interpretation, not cursor byte movement

## Detailed Finding 5: Parser Options Do Not Form A Reliable Contract

### Current behavior

`JsonParseOptions::allowTrailingCommas` detects whether a closing delimiter
after a comma should be rejected. When enabled, it continues the member/value
loop, which immediately attempts to parse `]` or `}` as another value.

The mutable JSON and XML span overloads also force `inSitu = true` regardless
of the caller's option. Conversely, setting `inSitu = true` on a const input
does not provide in-situ behavior.

Some XML options apply only in certain positions. For example, processing
instruction policy is checked inside elements but top-level processing
instructions are skipped separately.

### Recommended solution

Replace unrelated boolean fields with small policy enums and type-encoded modes.

Example:

```cpp
enum class JsonCommentPolicy
{
    Reject,
    Allow,
};

enum class JsonTrailingCommaPolicy
{
    Reject,
    Allow,
};

enum class DuplicateKeyPolicy
{
    Preserve,
    Reject,
    KeepFirst,
    KeepLast,
};
```

Do not include source mutability or ownership in parse options. Those belong to
the parse entry point.

For XML:

```cpp
enum class XmlTriviaPolicy
{
    Discard,
    Preserve,
};

enum class XmlDoctypePolicy
{
    Reject,
    PreserveWithoutExpansion,
};

enum class XmlNamespacePolicy
{
    Reject,
    PreserveQualifiedNames,
    Resolve,
};
```

Each policy needs:

- a test for its default
- a positive test for every non-default mode
- a negative interaction test where two policies affect the same token

### Acceptance criteria

- every public option has at least one behavior test
- invalid option/input-mode combinations are impossible or return an explicit
  error
- default modes are strict and documented
- enabling extensions never weakens unrelated validation

## Detailed Finding 6: JSON Number Fidelity

### Current behavior

`JsonValue` has one numeric type and stores it as `F64`. The parser has a fast
path for exactly representable small integers but ultimately returns a double.

This means:

- integer values greater than 2^53 can silently lose precision
- unsigned values beyond `Int64` cannot be represented faithfully
- users cannot distinguish `1`, `1.0`, and `1e0`
- writers cannot preserve the original lexeme
- protocol sequence numbers and IDs require unchecked casts from double

NGIN currently casts parsed event sequence values from `F64` into integer
types. This makes JSON numeric fidelity part of the correctness of tool-driver
protocols.

### Recommended solution

Use a richer numeric model:

```cpp
enum class JsonValueKind : UInt8
{
    Null,
    Bool,
    Int64,
    UInt64,
    Double,
    String,
    Array,
    Object,
};
```

Parsing policy:

1. integer token with a minus sign:
   - parse as `Int64` when in range
   - otherwise follow configured big-number policy
2. non-negative integer:
   - parse as `Int64` or `UInt64` according to the chosen canonical rule
3. fraction/exponent:
   - parse as `Double`
4. out-of-range finite token:
   - reject by default
   - optionally preserve as a number lexeme for specialized consumers

Possible specialized representation:

```cpp
struct JsonNumberView
{
    std::string_view lexeme;
    JsonNumberCategory category;
};
```

The default DOM does not need arbitrary-precision arithmetic. It does need to
avoid silently changing valid 64-bit protocol values.

### Query API

Prefer checked access:

```cpp
Expected<Int64, JsonTypeError> AsInt64() const;
Expected<UInt64, JsonTypeError> AsUInt64() const;
Expected<F64, JsonTypeError> AsDouble() const;

std::optional<Int64> TryInt64() const noexcept;
```

Unchecked access can exist only with an assertion-based name such as
`AssumeInt64()` and a documented programmer-error precondition.

### Acceptance criteria

- all `Int64` and `UInt64` values round-trip exactly
- values around 2^53 have explicit tests
- integer overflow is not reported as a generic malformed token
- writers preserve integer categories
- protocol consumers no longer cast sequence numbers from `F64`

## Detailed Finding 7: XML Contract And Well-Formedness

### Current behavior

The XML parser implements useful pieces:

- elements
- attributes
- text
- CDATA
- comments and processing-instruction skipping
- doctype skipping
- built-in and numeric entity decoding
- nesting limits

It does not define or enforce a coherent XML profile. Examples include:

- names accept an ASCII-only subset
- source UTF-8 is not validated
- entity references are not validated when decoding is disabled
- raw `<` in attribute values is not rejected
- duplicate attributes are accepted
- invalid XML character references may be sanitized to U+FFFD
- an empty numeric reference can decode as zero
- comment body restrictions are not enforced
- document-level misc content is handled differently before and after the root
- doctype handling is permissive and has no explicit policy
- declarations, comments, processing instructions, and doctypes are not
  represented in the DOM
- namespaces are preserved only incidentally as colon-containing names

### Why this matters to NGIN

NGIN manifests do not require every XML facility. They do require:

- deterministic interpretation
- precise diagnostics
- safe handling of untrusted package manifests
- editor and formatter support
- comment preservation for authored files
- rejection of malformed input rather than silent normalization

The current formatter must refuse any file containing comments because the DOM
drops them:

- [manifest formatter](../../Tools/NGIN.CLI/src/Commands.cpp)
- [V4 progress note](NGIN-V4-Implementation-Progress.md)

### Recommended product decision

Adopt a strict NGIN XML profile in phase one.

Recommended profile:

- UTF-8 only
- XML 1.0 declaration accepted and validated
- elements, attributes, text, comments, CDATA, and processing instructions
- predefined and numeric character references
- exactly one root element
- comments and processing instructions allowed in valid document positions
- no DTD or entity declaration support by default
- `DOCTYPE` rejected by default
- qualified names may be preserved as raw names, but namespace resolution is
  not performed until there is a real NGIN use case
- duplicate attributes rejected
- XML character validity enforced
- all unsupported constructs rejected with a specific diagnostic

This is substantially safer than silently skipping DTD contents. It also keeps
the implementation smaller than a general validating XML processor.

If `NGIN.Base` intends to market the component as a general XML library, this
profile is insufficient. In that case the project should either fund a full
conformance effort or explicitly approve a production XML dependency. The
current standard-library-only dependency policy makes a strict documented
profile the better near-term fit.

### Entity handling

Syntax validation and decoded presentation must be separate decisions.

Even when a caller wants a raw source slice:

- every `&...;` reference must be syntactically valid
- numeric references must identify legal XML scalar values
- predefined entity names must be recognized
- unsupported named entities must be rejected

The DOM can expose either:

- decoded text by default, backed by document storage
- raw lexeme/source span for lossless tooling

The parser should never make invalid entity syntax legal merely because the
caller did not request decoding.

### Acceptance criteria

- the supported XML profile is documented independently of implementation
- a conformance-oriented corpus covers each accepted and rejected construct
- invalid UTF-8, duplicate attributes, invalid references, invalid comments,
  raw `<` in attributes, and invalid document structure are rejected
- all authored trivia needed by the formatter can be preserved
- DTD/entity expansion is unavailable by default

## Detailed Finding 8: Event Parsing Is Not Streaming

### Current behavior

The `JsonReader` and `XmlReader` interfaces are SAX-style callbacks over a
contiguous input. The `IByteReader` DOM overload reads the entire source into a
vector before parsing.

The term streaming currently describes event delivery, not incremental input.
There is no persistent parser state that can accept:

- a string split across chunks
- an escape split across chunks
- a number split across chunks
- an XML name or entity split across chunks
- partial JSONL input

The JSON event parser also constructs an arena sized from total input length,
even when the document contains no escaped strings.

### Recommended solution

Use distinct names for distinct capabilities:

- `JsonEventParser::ParseContiguous(...)`
- `XmlEventParser::ParseContiguous(...)`
- `IncrementalJsonParser::Feed(...)`
- `IncrementalXmlParser::Feed(...)`

Do not implement incremental XML merely for API symmetry. Add it only when a
real consumer needs chunked XML processing.

JSON has a demonstrated incremental use case: JSONL tool-driver events. A
reusable parser should:

- retain scratch capacity between records
- reset without releasing memory
- accept a complete line cheaply
- optionally accept chunks and report completed top-level values
- preserve exact integer sequence numbers

### Handler design

The current virtual handler incurs one indirect call per event and communicates
only acceptance/rejection through `bool`.

Prefer a handler concept:

```cpp
template<JsonEventHandler Handler>
Expected<void, ParseDiagnostic>
ParseJsonEvents(BorrowedTextView input, Handler& handler, ParseScratch& scratch);
```

A type-erased wrapper can be provided where runtime polymorphism is genuinely
needed.

Handler failure should preserve the handler's error:

```cpp
Expected<void, HandlerError> OnString(JsonStringView value);
```

If generic handler error typing makes the parser unwieldy, provide an explicit
handler-aborted result carrying a user context value. A generic
`HandlerRejected` message loses too much information.

### String lifetime during callbacks

Document callback string lifetime precisely:

- borrowed unescaped slices remain valid as long as input remains valid
- decoded scratch strings remain valid until the next callback, the next reset,
  or parse completion, whichever contract is selected
- handlers must copy values they retain beyond the stated lifetime

### Acceptance criteria

- contiguous event parsing allocates only for required decoding/scratch
- JSONL parsing can reuse one scratch object across many records
- callback errors retain consumer context
- incremental parsing, if exposed, is tested at every possible chunk boundary
- API names do not imply incremental input where none exists

## Detailed Finding 9: DOM Layout And Query Performance

### Current behavior

JSON containers and XML elements are pointer-linked objects allocated from an
arena. Their member/child vectors allocate separately. Object and attribute
indexes are optional and built manually.

This produces:

- pointer chasing between value, object, array, element, and vector storage
- storage overhead for each container object
- abandoned vector buffers in a monotonic arena after growth
- linear lookup unless a caller knows to build an index
- double lookups in some index paths through `Contains` followed by `GetRef`

For small manifests, linear lookup is often the correct choice. Building a hash
table for two or three attributes would cost more than scanning. The problem is
not that every container lacks a hash table; the problem is that storage and
lookup policy are exposed as manual mutating operations.

### Recommended JSON layout

Use a document-owned indexed representation:

```cpp
using JsonNodeId = StrongIndex<JsonNodeTag>;

struct JsonNodeRecord
{
    JsonValueKind kind;
    SourceSpan source;
    // payload: scalar value or range into another table
};

struct JsonMemberRecord
{
    StringId key;
    JsonNodeId value;
    SourceSpan source;
};
```

Arrays reference a contiguous range of `JsonNodeId` values. Objects reference a
contiguous range of members.

Public users see stable lightweight views:

```cpp
class JsonValueView;
class JsonArrayView;
class JsonObjectView;
```

Views hold a document pointer/reference plus an index, not raw pointers into
movable vectors.

Lookup policy can be internal:

- linear scan for small objects
- lazily constructed index for larger or repeatedly queried objects
- optional parse-time index when a caller selects a lookup-heavy profile

### Recommended XML layout

Use flat node and attribute tables:

```cpp
struct XmlNodeRecord
{
    XmlNodeKind kind;
    SourceSpan source;
    NodeRange children;
    AttributeRange attributes;
    StringId nameOrText;
};
```

Alternative sibling links are acceptable if they produce better construction
costs, but public APIs should expose ranges rather than storage details.

### Why indices are preferable to raw pointers

- document movement remains safe
- serialization/deserialization of internal caches is easier
- compact integer handles improve locality
- source spans and flags fit naturally in records
- builders can reserve and append tables efficiently
- invalid handles can be detected in debug builds

### Acceptance criteria

- moving a document preserves all views or explicitly invalidates them under a
  documented rule
- array/member/child iteration is contiguous
- small-object lookup has no mandatory hash allocation
- large/repeated lookup is benchmarked with and without lazy indexing
- public callers cannot mutate internal vectors directly

## Detailed Finding 10: DOM Usability And Repeated NGIN Helpers

### Current behavior

The XML DOM exposes public `name`, `attributes`, and `children` fields plus
`FindAttribute`. It does not provide:

- first-child lookup
- allocation-free filtered child iteration
- text-content access
- checked typed attribute conversion
- source locations

As a result, the same helpers are independently implemented in:

- [CLI support](../../Tools/NGIN.CLI/src/Support.hpp)
- [`NGIN.Core` loader](../../Packages/NGIN.Core/src/NGIN/Core/Loader.cpp)
- [`NGIN.Core` application parser](../../Packages/NGIN.Core/src/NGIN/Core/Application.cpp)
- [MetaGen context parser](../../Packages/NGIN.Reflection.MetaGen/src/MetaGenContext.cpp)

Some `ChildElements` helpers allocate a `std::vector` merely to iterate matching
children. Attribute helpers often allocate a `std::string` immediately.

The JSON DOM exposes unchecked `As...` functions. Calling one for the wrong
kind reads the inactive union member or dereferences an invalid pointer.

### Recommended query API

XML:

```cpp
class XmlElementView
{
public:
    std::string_view Name() const noexcept;
    SourceSpan Span() const noexcept;

    AttributeRange Attributes() const noexcept;
    std::optional<XmlAttributeView>
    Attribute(std::string_view name) const noexcept;

    ChildRange Children() const noexcept;
    FilteredChildRange Children(std::string_view name) const noexcept;
    std::optional<XmlElementView>
    FirstChild(std::string_view name) const noexcept;

    TextRange TextNodes() const noexcept;
};
```

JSON:

```cpp
class JsonObjectView
{
public:
    MemberRange Members() const noexcept;
    std::optional<JsonValueView>
    Find(std::string_view key) const noexcept;
};
```

Schema-specific conversion should generally remain above `NGIN.Base`, because
whether `"yes"` is a valid boolean is a manifest contract rather than XML
syntax. Base can provide strict lexical primitives:

```cpp
Expected<bool, LexicalError> ParseBoolean(std::string_view);
Expected<Int64, LexicalError> ParseInt64(std::string_view);
```

The V4 authoring layer can then define allowed spellings and diagnostic
messages.

### Acceptance criteria

- CLI, Core, and MetaGen no longer implement generic child/attribute traversal
  helpers
- filtered iteration does not allocate
- checked JSON access is the normal API
- schema conversion errors can include the source span of the originating
  member or attribute

## Detailed Finding 11: Diagnostics And Source Provenance

### Current behavior

`ParseError` includes:

- error code
- byte offset
- optional line and column
- allocated message string

Line/column tracking is an option that changes scanning behavior. Some fast
paths inspect text through a separate pointer without advancing the cursor, so
an error may be reported at the start or end of a token rather than at the
offending byte.

The error has no:

- source identifier/path
- token span
- related span
- expected token category
- offending character/lexeme
- structured underlying I/O error

Reader failures are converted into a generic parse error stating that reading
failed.

### Why this matters

The V4 graph contract includes diagnostics and provenance. Editor tooling needs
stable source ranges. Higher-level manifest validation should be able to point
to:

- an unknown element
- a duplicate attribute
- an invalid attribute value
- a conflicting declaration
- both locations participating in a duplicate or conflict

The syntax parser is the natural place to establish byte spans that the
authoring layer can carry forward.

### Recommended solution

Introduce shared source types:

```cpp
struct SourceId
{
    UInt32 value;
};

struct SourceSpan
{
    SourceId source;
    UIntSize begin;
    UIntSize end;
};

struct ParseDiagnostic
{
    ParseErrorCode code;
    SourceSpan primary;
    std::optional<SourceSpan> related;
    String message;
};
```

Line/column should be presentation data derived from source content:

```cpp
SourceLocation SourceMap::Locate(SourceSpan span) const;
```

I/O and parsing should remain separate:

```cpp
Expected<OwnedTextBuffer, IOError> ReadText(...);
Expected<XmlDocument, ParseDiagnostic> ParseXml(OwnedTextBuffer ...);
```

If a convenience `Parse(reader)` remains, its error type should be a tagged
union of I/O and syntax failure rather than flattening both.

### Acceptance criteria

- every DOM node/member/attribute can report its source span
- malformed token diagnostics identify the offending byte range
- source paths are attached without embedding filesystem behavior in the parser
- line/column computation does not slow successful parsing by default
- I/O errors retain their original code and context

## Detailed Finding 12: Lossless XML For Formatting And Editor Tooling

### Current behavior

The semantic XML DOM preserves elements, attributes, text, and CDATA. It drops:

- comments
- processing instructions
- XML declaration details
- doctype
- quote style
- some whitespace/trivia distinctions

`ngin manifest format` rebuilds a document from this lossy DOM. It explicitly
refuses files with comments to avoid deleting authored content.

### Recommended solution

Separate two use cases.

#### Semantic XML document

Used by:

- project/package/workspace loading
- `NGIN.Core` runtime manifest reading
- MetaGen context reading

Properties:

- compact
- decoded values
- trivia can be discarded
- strict syntax validation
- source spans retained

#### XML syntax document

Used by:

- `ngin manifest format`
- editor tooling
- future source edits/refactors

Properties:

- preserves comments, processing instructions, declaration, CDATA, and
  significant trivia
- retains raw source spans and decoded semantic views
- can be rewritten deterministically without losing authored information
- can support minimal edits without rebuilding unrelated text

Both documents may be views over the same tokenizer/tape. They do not
necessarily require two independent parsers.

### Acceptance criteria

- formatter accepts and preserves comments
- formatting does not change text or CDATA semantics
- declaration policy is deterministic
- parser validation is shared between semantic and syntax modes
- lossless mode has round-trip tests for every preserved node kind

## Detailed Finding 13: Missing Writers And Duplicated Escaping

### Current behavior

`NGIN.Base` can build DOM nodes through archives but cannot emit JSON or XML
text. The CLI contains multiple independent JSON and XML escaping/writing
implementations:

- [tooling JSON escaping](../../Tools/NGIN.CLI/src/Tooling.cpp)
- [event JSON writing](../../Tools/NGIN.CLI/src/Events.cpp)
- [command JSON/XML escaping](../../Tools/NGIN.CLI/src/Commands.cpp)
- [build metadata XML writing](../../Tools/NGIN.CLI/src/Build.cpp)

Duplicated writers increase the risk of:

- inconsistent escaping
- invalid control-character handling
- inconsistent numeric formatting
- divergent pretty-print behavior
- repeated allocations through `ostringstream`

### Recommended solution

Add direct sink-based writers before adding any generic reflection archive.

JSON:

```cpp
JsonWriter writer{sink, JsonWriteOptions{
    .format = JsonFormat::Compact,
}};

writer.BeginObject();
writer.Key("schemaVersion");
writer.String("4.0");
writer.Key("sequence");
writer.Int64(sequence);
writer.EndObject();
```

XML:

```cpp
XmlWriter writer{sink, XmlWriteOptions{
    .format = XmlFormat::Indented,
}};

writer.BeginElement("Project");
writer.Attribute("SchemaVersion", "4");
writer.EndElement();
```

Sink requirements should be minimal:

```cpp
Expected<void, SinkError> Write(std::span<const char>);
```

Adapters can target:

- `NGIN::Text::String`
- `NGIN::Containers::Vector<Byte>`
- `NGIN::IO` file writers
- a callback
- optionally `std::ostream` in a non-core adapter

Writers should also serialize DOM views:

```cpp
WriteJson(JsonDocumentView document, sink, options);
WriteXml(XmlSyntaxDocumentView document, sink, options);
```

### Acceptance criteria

- all JSON control characters and Unicode cases are tested
- XML attribute/text escaping follows the supported XML profile
- numeric output round-trips through the parser
- CLI event emission can use one reusable writer buffer
- duplicated generic escape functions are removed from CLI code

## Detailed Finding 14: Archive Layer Has No Demonstrated Contract

### Current behavior

`Archive` contains only:

- a virtual destructor
- an `ArchiveMode`

`JsonArchive` and `XmlArchive` then expose unrelated procedural operations.
There is no implemented ADL `Serialize`, no common field/sequence model, no
schema evolution policy, and no output writer.

Repository searches show no consumers beyond include-coverage tests.

The archive mutation methods also inherit the ownership, capacity, and
`noexcept` problems of the current DOM.

### Recommended solution

Delete:

- `Archive.hpp`
- `JsonArchive.hpp/.cpp`
- `XmlArchive.hpp/.cpp`

Do not immediately replace them with another universal archive abstraction.
First deliver:

- safe documents and views
- builders
- direct writers
- actual NGIN call-site migrations

Then evaluate concrete serialization use cases from:

- `NGIN.Reflection`
- graph persistence
- IPC/protocol structures
- package metadata

If a format-neutral layer is justified, prefer static serializer/deserializer
concepts:

```cpp
template<class Serializer>
Expected<void, SerializationError>
Serialize(Serializer& serializer, const MyType& value);

template<class Deserializer>
Expected<MyType, SerializationError>
Deserialize(Deserializer& deserializer);
```

Reading and writing should be separate concepts. A single runtime mode flag
forces every operation and overload to represent two unrelated directions and
weakens type checking.

### Acceptance criteria

- no generic archive abstraction exists without at least two real consumers
- writers can be adopted independently of reflection serialization
- any future generic layer defines field names, optional values, sequences,
  variants, errors, and versioning before public release

## Detailed Finding 15: Test Coverage

### Current coverage

The existing parser tests cover a small correctness baseline:

JSON:

- basic object/array/scalar parsing
- default trailing-comma rejection
- enabled comments
- invalid Unicode escape
- surrogate-pair decoding

XML:

- basic elements and attributes
- entity decoding
- mismatched tags
- numeric entity decoding

There are no substantive tests for:

- archives
- event readers
- input reader overloads
- mutable in-situ parsing
- document ownership and movement
- fixed-arena exhaustion
- interning
- container indexes
- enabled trailing commas
- depth boundaries
- duplicate keys/attributes
- invalid UTF-8
- large containers
- I/O errors
- handler rejection details
- XML comments, PI, doctype, CDATA edge cases
- malformed XML entities and character references
- source locations

### Recommended test structure

Keep focused files:

```text
tests/Serialization/
  CoreCursorTests.cpp
  JsonParserTests.cpp
  JsonOwnershipTests.cpp
  JsonEventParserTests.cpp
  JsonWriterTests.cpp
  JsonRoundTripTests.cpp
  XmlParserTests.cpp
  XmlProfileTests.cpp
  XmlOwnershipTests.cpp
  XmlEventParserTests.cpp
  XmlWriterTests.cpp
  XmlLosslessTests.cpp
  SerializationLimitTests.cpp
```

Do not create one monolithic serialization test executable if the existing
test CMake organization naturally supports focused executables.

### Corpus strategy

Add checked-in, license-compatible test fixtures for:

- JSON syntax acceptance/rejection
- UTF-8 boundaries
- numeric boundaries
- XML profile acceptance/rejection
- real NGIN manifests
- real NGIN graph/event payloads

Property-oriented tests should cover:

- parse -> write -> parse semantic equality
- randomized chunk boundaries for incremental parsing
- random valid string escaping
- stable source spans
- document movement
- deterministic OOM at every allocation point

### Fuzzing

Add parser fuzz targets where the build environment supports them:

- JSON DOM parse
- JSON event parse
- XML semantic parse
- XML syntax/lossless parse
- entity and escape decoding

The first invariant is that arbitrary bytes never cause undefined behavior,
memory corruption, termination through an incorrect `noexcept`, or unbounded
resource consumption beyond configured limits.

## Detailed Finding 16: Benchmark Coverage

### Current behavior

The benchmarks use one small and one medium string literal. They report latency
but not:

- throughput
- allocation count
- committed bytes
- used bytes
- result node count
- input size
- escaped versus unescaped content
- cold versus reused parser state

Optional comparisons with simdjson, RapidJSON, pugixml, and TinyXML2 are useful,
but the current documents are too small to characterize steady-state parser
performance.

### Recommended benchmark matrix

JSON document shapes:

- tiny configuration object
- actual JSONL event
- 1 KiB, 100 KiB, 1 MiB, and 10 MiB documents
- wide numeric array
- deeply nested arrays/objects
- many small object members
- escape-heavy strings
- long unescaped strings
- repeated keys with and without interning/indexing
- actual Composition Graph output

XML document shapes:

- small package manifest
- medium project/workspace manifest
- generated MetaGen context
- many empty elements
- attribute-heavy elements
- text-heavy content
- entity-heavy content
- comments/trivia-heavy authored manifest
- maximum supported nesting

Modes:

- owning DOM
- borrowed DOM
- in-situ DOM
- contiguous event parsing
- incremental parsing where implemented
- parse and write
- parser/scratch reuse

Metrics:

- MB/s
- latency p50/p95/p99
- allocations
- bytes committed
- bytes used
- peak resident working set where practical
- output bytes/s for writers

### Performance gates

Do not choose arbitrary absolute throughput requirements before collecting a
stable baseline on supported compilers and hardware.

Initial gates should be relative:

- no regression greater than an agreed percentage on actual NGIN manifests
- repeated JSONL parsing performs no steady-state heap allocation after warmup
  for records within retained scratch capacity
- compact DOM peak used memory is bounded by documented per-node/per-member
  costs
- strict validation does not introduce an unmeasured performance path

## Target Public Architecture

The target can be organized as follows:

```text
NGIN/Serialization/
  Core/
    SourceBuffer.hpp
    SourceSpan.hpp
    SourceMap.hpp
    ParseDiagnostic.hpp
    ParseLimits.hpp
    ParseScratch.hpp
    SegmentedArena.hpp
  JSON/
    JsonDocument.hpp
    JsonValueView.hpp
    JsonParser.hpp
    JsonEventParser.hpp
    JsonBuilder.hpp
    JsonWriter.hpp
  XML/
    XmlProfile.hpp
    XmlDocument.hpp
    XmlSyntaxDocument.hpp
    XmlElementView.hpp
    XmlParser.hpp
    XmlEventParser.hpp
    XmlBuilder.hpp
    XmlWriter.hpp
```

Names can change during implementation. The important separation is:

- shared source/diagnostic/resource machinery
- immutable semantic document views
- lossless XML syntax where needed
- mutable construction
- text emission
- contiguous events versus actual incremental parsing

## Suggested JSON API Sketch

```cpp
namespace NGIN::Serialization::JSON
{
    struct ParseOptions
    {
        CommentPolicy comments {CommentPolicy::Reject};
        TrailingCommaPolicy trailingCommas {TrailingCommaPolicy::Reject};
        DuplicateKeyPolicy duplicateKeys {DuplicateKeyPolicy::Reject};
        Utf8Policy utf8 {Utf8Policy::Validate};
    };

    Expected<Document, ParseDiagnostic>
    Parse(OwnedTextBuffer input,
          const ParseOptions& options = {},
          const ParseLimits& limits = {});

    Expected<BorrowedDocument, ParseDiagnostic>
    ParseBorrowed(BorrowedTextView input,
                  ParseScratch& scratch,
                  const ParseOptions& options = {},
                  const ParseLimits& limits = {});

    Expected<Document, ParseDiagnostic>
    ParseInSitu(MutableTextBuffer input,
                const ParseOptions& options = {},
                const ParseLimits& limits = {});
}
```

The current repository places JSON/XML types directly in
`NGIN::Serialization`. Moving to `NGIN::Serialization::JSON` and
`NGIN::Serialization::XML` is recommended because:

- generic names such as `Document`, `Parser`, and `Writer` become usable
- format ownership is clearer
- unrelated JSON/XML types do not share one flat namespace
- the breaking refactor is the least costly time to correct namespacing

## Suggested XML API Sketch

```cpp
namespace NGIN::Serialization::XML
{
    struct ParseOptions
    {
        DocumentMode documentMode {DocumentMode::Semantic};
        TriviaPolicy trivia {TriviaPolicy::Discard};
        DoctypePolicy doctype {DoctypePolicy::Reject};
        NamespacePolicy namespaces {NamespacePolicy::PreserveQualifiedNames};
        Utf8Policy utf8 {Utf8Policy::Validate};
    };

    Expected<Document, ParseDiagnostic>
    Parse(OwnedTextBuffer input,
          const ParseOptions& options = {},
          const ParseLimits& limits = {});

    Expected<SyntaxDocument, ParseDiagnostic>
    ParseSyntax(OwnedTextBuffer input,
                const ParseOptions& options = {},
                const ParseLimits& limits = {});
}
```

Entity decoding should be part of XML semantic interpretation, not a switch
that controls whether invalid syntax is noticed.

## NGIN Integration Direction

### Native CLI

The CLI should consume:

- semantic XML views for authoring and graph construction
- syntax XML views for `ngin manifest format`
- the JSON writer for graph, inspect, schema, plan, and event output
- a reusable JSON parser/scratch object for JSONL input

Generic helpers currently in `Support.hpp` should disappear after equivalent
zero-allocation view operations are available.

V4 schema validation remains in CLI-focused authoring code.

### NGIN.Core

`NGIN.Core` should consume the same semantic XML view API but retain its
deliberately smaller runtime-side manifest interpretation.

The parser should own the source, eliminating the local
`LoadedXmlDocument` lifetime wrapper and manual arena multiplier.

### NGIN.Reflection.MetaGen

MetaGen should consume semantic XML views and checked attribute helpers. It
should not allocate a vector to filter child elements or duplicate generic XML
traversal utilities.

### Composition Graph And Tool Protocols

Composition Graph JSON writing should use one canonical writer. JSON tool
protocol parsing should use exact integer access for schema versions and
sequence fields.

Source spans from XML should be retained through authored model nodes and into
graph provenance where appropriate.

## Breaking Change Inventory

The target refactor intentionally breaks:

- namespace placement for JSON/XML types
- `JsonDocument(UIntSize arenaBytes)` and `XmlDocument(UIntSize arenaBytes)`
- `JsonParseOptions::arenaBytes`
- `XmlParseOptions::arenaBytes`
- `inSitu` option flags
- pointer-returning raw DOM representation
- public `members`, `values`, `attributes`, and `children` fields
- unchecked `As...` scalar access as the primary API
- the single `F64` JSON number kind
- mutable `JsonObject::Set`
- manual `BuildIndex` and `BuildAttributeIndex`
- reader overloads that flatten I/O errors into parse errors
- `JsonReader` and `XmlReader` names if they remain contiguous-only
- `Archive`, `JsonArchive`, and `XmlArchive`
- the current XML entity-decoding default and permissive doctype behavior

No compatibility layer is recommended in the final tree.

## Implementation Workstreams

### Workstream A: Contract And Corpus

Deliver:

- documented JSON behavior and extensions
- documented NGIN XML profile
- ownership-mode contract
- parse limit contract
- diagnostic/source-span contract
- correctness fixture corpus

Acceptance gate:

- every supported and unsupported XML construct has a stated result
- every JSON option has explicit semantics
- ownership and move behavior can be described without implementation details

### Workstream B: Shared Parsing Core

Deliver:

- bounds-safe offset cursor
- owned/mutable/borrowed source buffers
- source spans and source maps
- parse limits
- reusable scratch
- segmented arena or equivalent decoded-string storage
- deterministic failing allocator tests

Acceptance gate:

- core cursor passes ASan/UBSan and short-input fuzzing
- no caller supplies a capacity multiplier
- allocation failure follows one result contract

### Workstream C: JSON Value Model And Parser

Deliver:

- indexed document tables and stable views
- integer-aware number model
- strict UTF-8 and escape validation
- working option policies
- owned, borrowed, and in-situ entry points
- object iteration and lookup

Acceptance gate:

- existing CLI JSON consumers can migrate without unsafe casts
- all 64-bit integer boundaries round-trip
- wide-array memory behavior is bounded and benchmarked

### Workstream D: JSON Writer And Event Processing

Deliver:

- sink-based JSON writer
- DOM writing
- reusable contiguous event parser
- incremental JSON input only if required for process pipe integration
- migration of duplicated CLI JSON escaping/writing

Acceptance gate:

- graph and JSONL output use the canonical writer
- repeated JSONL records require no steady-state heap allocation after warmup
- parse/write round-trip tests pass

### Workstream E: XML Semantic Parser

Deliver:

- strict NGIN XML profile
- indexed semantic document and views
- entity validation and decoding
- duplicate attribute detection
- valid UTF-8/XML character enforcement
- owned source
- zero-allocation child/attribute queries

Acceptance gate:

- CLI, Core, and MetaGen semantic XML consumers migrate
- local source-lifetime wrappers and arena multipliers are removed
- malformed profile fixtures are rejected with precise spans

### Workstream F: XML Syntax Tree And Writer

Deliver:

- comment/PI/declaration/CDATA preservation
- deterministic XML writer
- formatting policies
- manifest formatter migration

Acceptance gate:

- `ngin manifest format` accepts files containing comments
- syntax round-trip fixtures preserve required authored information
- generic CLI XML escaping helpers are reduced to the canonical writer

### Workstream G: Archive Removal And Cleanup

Deliver:

- removal of current archive files and build entries
- removal of unused index/interner APIs or replacement with internal policy
- removal of duplicated generic traversal helpers
- documentation updates

Acceptance gate:

- repository search finds no current archive references
- public serialization documentation describes only implemented, tested
  capabilities

### Workstream H: Performance Validation

Deliver:

- expanded benchmark data sets
- allocation and memory instrumentation
- actual NGIN workload fixtures
- compiler/platform baseline results

Acceptance gate:

- performance tradeoffs of strict validation are measured
- peak memory behavior is documented
- parser reuse and writer throughput are measured
- regressions are gated in the appropriate CI or release workflow

## Recommended Sequencing

Recommended order:

1. contract and corpus
2. shared source, cursor, diagnostics, and allocation core
3. JSON document/parser replacement
4. JSON writer and JSONL migration
5. XML semantic document/parser replacement
6. CLI/Core/MetaGen XML migration
7. XML syntax tree and formatter migration
8. archive deletion and generic helper cleanup
9. full benchmark and sanitizer pass

JSON should go first because:

- its grammar is smaller
- numeric fidelity is immediately relevant to tool protocols
- JSONL reuse is a concrete performance target
- the writer can eliminate multiple live CLI implementations
- the new core abstractions can then be proven before the larger XML effort

The cursor undefined behavior may be fixed immediately in the existing code if
the replacement cannot land quickly. That narrow safety correction does not
justify retaining the rest of the old API.

## Risks And Tradeoffs

### Compact tables can make mutation harder

That is intentional for parsed documents. Mutation belongs in a builder that
can use append-oriented storage and finalize into an immutable document.

### Two XML document modes increase surface area

Semantic loading and lossless formatting have genuinely different needs.
Pretending one lossy DOM satisfies both has already forced the formatter to
reject comments.

The tokenizer and source storage should be shared to minimize duplicate logic.

### Strict XML may reject manifests previously accepted

This is desirable for malformed XML. The V4 direction explicitly does not
preserve accidental permissive behavior. Diagnostics must make corrections
clear.

### Source ownership can increase retained memory

Owning the original input retains its bytes for the document lifetime. This is
usually cheaper than copying every string and is the price of safe zero-copy
views.

Borrowed parsing remains available for carefully controlled hot paths.

### General XML remains a large maintenance obligation

The recommendation deliberately scopes XML to a strict NGIN profile. If general
XML interoperability becomes a product requirement, dependency policy and
conformance cost must be revisited explicitly.

### SIMD optimization may need to be reworked

Existing scan helpers should be retained where their safety and benefit are
demonstrated. Compact storage and strict validation may change parser phases.
Optimization should follow profiles from representative data rather than
preserving current loops by default.

## Documentation Updates Required

When implementation starts, update:

- `Dependencies/NGIN/NGIN.Base/README.md`
- `Dependencies/NGIN/NGIN.Base/include/NGIN/Serialization/README.md`
- `Dependencies/NGIN/NGIN.Base/docs/SerializationPlan.md`
- `Dependencies/NGIN/NGIN.Base/OptimizationPlan.md`
- V4 implementation progress notes as formatter/provenance behavior changes
- CLI authoring documentation if strict XML diagnostics expose previously
  accepted malformed manifests

The existing `OptimizationPlan.md` already proposes compact DOM storage and
in-situ XML work, but portions are stale relative to the implementation. It
should be superseded or rewritten when this plan is adopted rather than kept as
a conflicting second roadmap.

## Definition Of Done

The refactor is complete when:

- owning documents are self-contained and safely movable
- borrowed and in-situ parsing are explicit types or entry points
- no parser caller chooses an internal arena-size multiplier
- allocation failure cannot unexpectedly terminate through `noexcept`
- cursor lookahead is bounds-safe
- every parser option has tested behavior
- JSON preserves 64-bit integer values
- the supported XML profile is strict and documented
- semantic XML loading preserves source spans
- lossless XML supports comments and formatter use
- object/member/attribute/child traversal does not require duplicated helpers
- JSON and XML have canonical sink-based writers
- JSONL parsing can reuse scratch storage
- current archive classes are removed
- representative correctness corpora, fuzz targets, and memory-aware
  benchmarks exist
- CLI, `NGIN.Core`, and MetaGen consume the new APIs without compatibility
  shims

## Final Recommendation

Proceed with the breaking refactor.

Retain the useful implementation ideas:

- contiguous parsing
- SIMD-aware delimiter scans
- explicit depth/resource limits
- arena-style decoded-string storage
- structured result errors
- event-driven parsing where it has a real consumer

Replace the contracts that currently leak implementation hazards:

- implicit borrowing
- caller-guessed arena capacity
- raw pointer DOM identity
- unchecked numeric and union access
- permissive/undefined XML behavior
- boolean option soup
- pseudo-streaming reader APIs
- unproven virtual archives

This produces a serialization foundation that better matches NGIN's V4
direction: XML remains an authoring frontend, JSON remains a stable graph and
protocol format, and both feed higher-level systems through safe, precise,
allocation-disciplined primitives.
