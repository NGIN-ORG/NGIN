---
title: Troubleshoot install and configure
description: Fix missing CLI, wrong working directory, manifest selection, profile conflicts, toolchain detection, and stale configuration.
---

# Troubleshoot install and configure

## `ngin: command not found`

Confirm the CLI was built and call it by explicit path first:

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli
./build/dev/Tools/NGIN.CLI/ngin
```

If the explicit path works, add its directory to your user `PATH` or keep using
the explicit path. Do not copy a development executable into a system directory
as a debugging shortcut; it becomes easy to run a stale binary.

## NGIN selected the wrong project

Pass the authored file explicitly:

```bash
ngin validate --project Examples/Hello.Native/Hello.Native.nginproj
```

Use `--workspace <file.ngin>` when selection depends on workspace discovery,
profiles, or package providers. Paths are resolved from the command's working
directory unless the command contract says otherwise.

## Manifest root is rejected

A `.nginproj` is product-first. Its root is one `<Executable>` or one
`<Library Kind="Static|Shared|Interface|Plugin">`. Generic `<Project>` wrappers
and old Module product roots are not accepted.

```xml
<Executable Name="Hello">
  <Build>
    <Source Include="src/**/*.cpp" />
  </Build>
</Executable>
```

Run `ngin format --check --project ...` for canonical formatting and
`ngin schema --format json` for the executable's current schema.

## Profile conflicts with an explicit option

Profiles expand to configuration, target, toolchain, Run, and Option facts.
Passing an explicit selection that contradicts a profile is an error. Inspect
the expansion:

```bash
ngin inspect --effective --project App.nginproj --profile Debug
```

Remove the conflicting command option, change the profile, or select a profile
whose intent matches the operation. The profile label itself is not graph
identity; its expanded facts are.

## Toolchain or generator is unavailable

Check the native tools directly:

```bash
cmake --version
c++ --version
ninja --version
```

Then inspect the resolved toolchain with `ngin graph --format json`. NGIN
generates a backend plan; it does not bundle the compiler, CMake, SDK, or system
headers.

## Configure output looks stale

First rerun `ngin configure` with the same explicit selections. If the
generated tree is disposable and a clean configuration is genuinely needed,
remove only that exact generated output directory, then configure again. Do
not delete a workspace root or authored directory. Preserve the failing tree
until you have collected logs when diagnosing a regression.

## CMake workspace project behaves differently

Explicit CMake workspace projects use a project directory and CMake preset
selection. Configure presets define persistent context; build/test presets are
operation choices. In the current contract, CMake projects do not support NGIN
Stage, Run, Debug, Benchmark, or Composition Graph commands.

