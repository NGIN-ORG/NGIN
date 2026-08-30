---
title: Your first serialized document
description: Parse and inspect complete JSON and XML documents with checked views and diagnostics.
---

# Your first serialized document

## Parse JSON

```cpp
#include <NGIN/Serialization/JSON.hpp>

#include <iostream>

int main() {
    using namespace NGIN::Serialization;

    auto parsed = JSON::Parse(OwnedTextBuffer {
        R"({"name":"Ada","count":3})"});

    if (!parsed) {
        const auto& error = parsed.error();
        std::cerr << error.location.line << ':'
                  << error.location.column << '\n';
        return 1;
    }

    auto object = parsed->Root().TryObject();
    if (!object) {
        return 2;
    }

    auto name = object->Find("name");
    auto count = object->Find("count");
    if (!name || !count) {
        return 3;
    }

    std::cout << name->TryString().value_or("?") << ' '
              << count->TryInt64().value_or(0) << '\n';
}
```

Expected output:

```text
Ada 3
```

The returned `Document` owns parsed state. `Root`, `ObjectView`, and member
`ValueView` objects borrow that state and must not outlive the document.

## Parse XML

```cpp
auto parsed = NGIN::Serialization::XML::Parse(
    NGIN::Serialization::OwnedTextBuffer {
        R"(<user count="3">Ada</user>)"});

if (!parsed) {
    return Report(parsed.error());
}

auto root = parsed->Root();
auto count = root.Attribute("count");
auto name = root.FirstText();
```

XML semantic parsing validates structure/entities and normalizes line endings.
Use syntax parsing instead when editor/formatter output must retain trivia and
original authored bytes.

## Build setup

```cmake
find_package(NGINBase CONFIG REQUIRED COMPONENTS Serialization)
target_link_libraries(MyTarget PRIVATE NGIN::Base::Serialization)
target_compile_features(MyTarget PRIVATE cxx_std_23)
```

## Next

Read [ownership and limits](./ownership-limits.md), then the format-specific
[JSON](./json.md) or [XML](./xml.md) guide.

