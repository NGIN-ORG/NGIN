---
title: Daily CLI workflow
description: Validate, explain, build, stage, and run a product with the cheapest useful command at each step.
---

# Daily CLI workflow

## 1. Validate authored input

```bash
ngin validate --project App.nginproj --configuration Debug
```

Validation catches document shape and local semantic errors without compiling.

## 2. Inspect resolution when needed

```bash
ngin inspect --effective --project App.nginproj --configuration Debug
ngin graph --project App.nginproj --configuration Debug
```

`inspect --effective` shows normalized manifest facts before package resolution.
`graph` shows the resolved composition including packages and derived identity.

## 3. Restore package inputs

```bash
ngin restore
ngin package verify-lock
```

Restore when the product graph or package sources change. A valid lock proves
the selected acquired instances, while the graph fingerprint covers semantic
activation and selection.

## 4. Build

```bash
ngin build --configuration Debug
```

Build configures generated state when required and invokes the backend.

## 5. Stage and run

```bash
ngin stage --configuration Debug
ngin run --configuration Debug
```

`run` uses the selected RunPlan and staged layout. Process arguments after `--`
are forwarded to the product:

```bash
ngin run --configuration Debug -- --verbose input.txt
```

## When a build surprises you

Inspect effective input and graph provenance before editing or deleting
generated output. Generated CMake reflects the resolved model; it does not own
the product contract.
