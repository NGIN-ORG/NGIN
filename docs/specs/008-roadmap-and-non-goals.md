# Spec 008: Roadmap and Non-Goals

Status: Active
Last updated: 2026-04-24

## Direction

NGIN V2 is now:

- project-first
- configuration-first
- workspace-optional
- package-reusable
- launch-manifest based

## Near-Term Follow-Up

1. Keep the NGIN tooling and `NGIN.Core` runtime boundaries explicit in docs,
   examples, and templates.
2. Extract shared project/package/workspace parsing into common libraries.
3. Expand workspace-optional flows in editor tooling.
4. Improve package installation and distribution around the V2 package contract.
5. Keep docs and examples aligned with the project-first model.

## Non-Goals

- reintroducing `variant` as an active authoring concept
- bringing back package `SourceBinding`
- reviving `workspace sync`
