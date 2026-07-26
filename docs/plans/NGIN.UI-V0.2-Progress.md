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

## Milestone 18 — International Text And Practical Images

Status: Complete
Completed: 2026-07-27

Delivered:

- packaged Noto Sans Arabic and Noto Sans Symbols 2 behind the existing
  Noto Sans primary face, with application fallbacks taking precedence;
- staged all three OFL-licensed fonts, their license texts, and exact
  third-party revision and checksum records;
- added public font-coverage diagnostics for loaded family, style, source
  path, fallback use, resolved character count, and missing codepoints;
- defined the Gallery text promise as Latin, Greek, Cyrillic, Arabic,
  bidirectional text, combining sequences, and selected monochrome symbols;
- made color emoji explicitly unsupported in version 0.2 and observable as
  missing coverage instead of presenting a replacement glyph as success;
- added a backend-independent `StandardImageDecoder` for PNG, JPEG, and the
  existing PPM path while preserving asynchronous sources and cancellation;
- normalized decoded images to straight RGBA8, with opaque JPEG alpha;
- bounded encoded input to 64 MiB, decoded pixels to 256 MiB, and image
  dimensions to 16,384 pixels;
- pinned stb_image 2.30 at revision
  `013ac3beddff3dbffafd5177e7972067cd2b5083`, selected its MIT license, and
  kept it private to the NGIN.UI implementation;
- added a staged 1536 by 1024 PNG Gallery image and demonstrations of contain,
  cover, and tinted image rendering;
- updated the Typography, Images, and Diagnostics pages plus the richer-content
  and public API documentation.

Approved asset budget:

- Noto Sans Arabic variable font: 844,676 bytes,
  SHA-256 `63111B5B2E074DD48CC67692E0A2726D86EE94C1C37FE8598257B7B4E87E869E`;
- Noto Sans Symbols 2: 1,233,128 bytes,
  SHA-256 `7D5FB73B7CA67A6798101741F5D280A3D016A56A197AFCD4199DBB57B4B82A21`;
- total staged font files, including the existing Noto Sans primary:
  4,126,900 bytes;
- stb_image header: 283,010 source bytes,
  SHA-256 `594C2FE35D49488B4382DBFAEC8F98366DEFCA819D916AC95BECF3E75F4200B3`;
- Gallery sample PNG: 2,264,488 bytes,
  SHA-256 `6BFB6C7FA132A79619A8B146550931B53B08259941921EDFABB92BBBEA166F0E`.

Verification:

- all three Gallery manifests validated;
- `NGINUITests`: 119/119 passed, including deterministic PNG/JPEG,
  malformed-input, cancellation, fallback-coverage, and missing-emoji tests;
- standalone Gallery build and `--smoke`: passed;
- hosted Gallery build and `--smoke`: passed;
- Gallery headless checks: passed;
- staged output contains all fonts, the sample PNG, OFL texts, stb license, and
  third-party notice;
- contracts-only installed-package consumer: 1/1 passed;
- public API documentation coverage: 247 types documented.
