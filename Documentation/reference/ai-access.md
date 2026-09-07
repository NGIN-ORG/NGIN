---
title: Documentation for AI
description: Retrieve NGIN documentation as focused raw Markdown pages, library bundles, or discoverable LLM indexes.
---

# Documentation for AI

The guides, project-system reference, and AI-facing bundles come from the same
Markdown source. Each library's Doxygen reference comes directly from its C++
headers and is served at `/reference/doxygen/<library>/index.html`; it is not
included in the Markdown bundles. Choose a library on the
[reference page](../reference.md#c-api-reference) or use the checked-out
headers to verify exact declarations.

## Per-page Markdown

Every authored Markdown page is available through the explicit raw namespace:

```text
/libraries/base/memory-containers
/raw/libraries/base/memory-containers.md
```

The page action bar provides **Copy page** and **View raw** controls. Build
output additionally preserves `.md` files beside the rendered routes for
static integrations, but `/raw/` is the stable public contract in development
and production.

## Discovery indexes

| Endpoint | Purpose |
| --- | --- |
| `/llms.txt` | Compact categorized index of every page |
| `/llms-full.txt` | Complete documentation corpus |
| `/llms/api.txt` | C++ API usage guides |
| `/llms/base.txt` | NGIN.Base overview and subsystem pages |
| `/llms/core.txt` | NGIN.Core overview and subsystem pages |
| `/llms/reflection.txt` | NGIN.Reflection documentation |
| `/llms/ecs.txt` | NGIN.ECS documentation |
| `/llms/ui.txt` | NGIN.UI documentation |
| `/llms/log.txt` | NGIN.Log documentation |

Prefer the smallest source that covers the question. A focused page preserves
more model context than the full corpus.

Library bundles include the library's learning pages and API usage guides.
For example, `/llms/base.txt` contains the Async learning path;
`/llms/api.txt` contains API guides across all libraries. Exact C++ declarations
live in Doxygen and the public headers.

## Version awareness

NGIN is experimental. An AI agent should identify the documentation revision
or release and avoid mixing manifest or API contracts from unrelated versions.
When exact syntax matters, compare the page with:

```bash
ngin schema --format json
ngin
```

## Citation guidance

Link the focused human-readable route when answering a person. Retain the raw
Markdown URL when the next consumer is another tool or agent.
