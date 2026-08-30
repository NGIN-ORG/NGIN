---
title: Contributing to NGIN
description: Find the correct ownership boundary, documentation contract, and verification path before changing NGIN.
---

# Contributing to NGIN

NGIN is organized by ownership boundary. Find the nearest `AGENTS.md`, inspect
the current code and focused tests, and change the smallest surface that owns
the behavior.

## Main ownership map

| Change | Primary location |
| --- | --- |
| CLI and project semantics | `Tools/NGIN.CLI/` |
| VS Code integration | `Tools/NGIN.VSCode/` |
| Package exposure and providers | `Packages/` |
| First-party library implementation | `Dependencies/NGIN/` |
| Canonical runnable behavior | `Examples/` |
| New documentation site | `Documentation/` |

## Continue

- [Documentation contribution](./contributing/documentation.md)
- [Architecture and generated boundaries](./contributing/architecture.md)

Generated build trees and staged layouts are outputs. Never edit them to
implement behavior.
