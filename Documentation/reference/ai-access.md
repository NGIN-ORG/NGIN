---
title: Documentation for AI
description: Retrieve NGIN documentation as focused raw Markdown pages, library bundles, or discoverable LLM indexes.
---

# Documentation for AI

The rendered website and AI-facing files come from the same Markdown source.
There is no separate AI summary database that can silently drift.

## Per-page Markdown

Every rendered page is available through the explicit raw Markdown namespace:

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
| `/llms/api.txt` | C++ symbol reference and API guides |
| `/llms/base.txt` | NGIN.Base overview and subsystem pages |
| `/llms/core.txt` | NGIN.Core overview and subsystem pages |
| `/llms/reflection.txt` | NGIN.Reflection documentation |
| `/llms/ecs.txt` | NGIN.ECS documentation |
| `/llms/ui.txt` | NGIN.UI documentation |
| `/llms/log.txt` | NGIN.Log documentation |

Prefer the smallest source that covers the question. A focused page preserves
more model context than the full corpus.

Library bundles include the library's learning pages, API guides, and C++
symbol reference. For example, `/llms/base.txt` contains the complete Async
learning path and its per-symbol reference; `/llms/api.txt` contains API
material across all libraries.

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
