# Spec 005: Roadmap

Status: Active roadmap  
Last updated: 2026-03-07

## Direction

NGIN is moving toward a simpler, package-first platform:

- XML manifests
- native C++ CLI
- project and target driven composition
- packages as the primary reusable unit

## Next Steps

1. Strengthen the staged target contract so future `run` support has a clean input.
2. Keep tightening package validation and package-provided content staging.
3. Connect staged output more directly to `NGIN.Core` startup.
4. Complete dynamic plugin productization inside the package model.
5. Build proof products on top of the same host model.

## Roadmap Themes

- keep the concept model small
- keep authored manifests easy to understand and modify
- avoid reintroducing parallel workflows for packages, modules, and plugins
- only add new system layers when proof products force them
