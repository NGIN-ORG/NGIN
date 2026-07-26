# NGIN.UI Version 0.2 Progress

Roadmap: [`NGIN.UI-V0.2-Roadmap.md`](NGIN.UI-V0.2-Roadmap.md)

## Milestone 17 — Text And Renderer Reliability

Status: Complete
Completed: 2026-07-26

Delivered:

- replaced the single glyph texture with a configurable, fixed page budget;
- added least-recently-used page rebuilding protected by leases retained in
  runtime glyphs and display lists;
- released old display references before relayout so unused pages can be
  safely reused;
- kept native glyph textures nearest-filtered and physical-pixel aligned while
  images remain linearly filtered;
- added page, occupancy, size, eviction, rebuild, failure, and restoration
  diagnostics to the public text system and Gallery;
- added explicit text device-loss and restoration hooks plus application-wide
  invalidation, with automatic hosting integration;
- added bounded churn, live-page, fallback, multiple-window invalidation,
  device restoration, DPI, antialiasing, descender, wrapping, centering, and
  clipping coverage;
- changed generated CLI staging to keep the compiled artifact and report the
  exact source, destination, likely running-process lock, copy error, and retry
  action.

Default texture budget:

- page size: 1024 by 1024 R8;
- maximum pages: 4;
- maximum glyph texture storage: 4 MiB;
- pages are allocated only when needed.

Verification:

- `NGINUITests`: 115/115 passed, including allocation benchmarks;
- SDL3 backend contracts: 1/1 passed;
- hosted UI lifecycle: 1/1 passed;
- CLI staging generator test: 17 assertions passed;
- standalone Gallery build and `--smoke`: passed;
- hosted Gallery build and `--smoke`: passed;
- a live Windows Gallery executable produced the expected kept-source,
  destination, running-process, copy-error, and retry diagnostic;
- public API documentation coverage: 253 types documented.
