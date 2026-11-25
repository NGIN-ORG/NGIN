# Keep UI backends separate from the UI core

## Status

Accepted

## Context

A UI toolkit needs stable composition, layout, input, text, and accessibility
contracts without forcing every consumer onto one windowing or rendering API.

## Decision

`NGIN.UI` owns backend-neutral UI behavior. Platform and renderer integration
lives in packages such as `NGIN.UI.Backend.SDL3`. `NGIN.UI.Hosting` separately
connects UI lifecycle to `NGIN.Core`.

## Consequences

The UI core remains testable with headless backends and can support other native
backends. Applications must select a concrete backend for real windows, and
backend capability negotiation remains an explicit startup concern.

## Embedding

A retained `Surface` owns composition, layout, interaction, semantics, and
preparation of draw data. Both native windows and engine-owned targets use it.
An engine selects the target, drives time and input, and presents the frame.
Layout and painting are independent stages within the UI library. A shared
render packet carries geometry across the backend boundary. Embedding contracts
and resource lifetimes are described in the
[components and surfaces guide](../../guides/ngin-ui-surfaces.md).
