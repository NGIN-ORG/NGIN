---
title: Install NGIN
description: Build the NGIN CLI from the repository and confirm the installation.
---

# Install NGIN

This guide builds the development CLI from the NGIN repository. You need Git,
CMake 3.20 or newer, Ninja, and a C++23-capable compiler.

## Clone the complete repository

NGIN contains source-backed dependencies, so clone its submodules as well:

```bash
git clone --recursive https://github.com/NGIN-ORG/NGIN.git
cd NGIN
```

If you already cloned without submodules, initialize them in place:

```bash
git submodule update --init --recursive
```

## Build the CLI

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli
```

The executable is written to:

```text
build/dev/Tools/NGIN.CLI/ngin
```

On Windows, the filename is `ngin.exe`.

## Confirm it works

```bash
./build/dev/Tools/NGIN.CLI/ngin
./build/dev/Tools/NGIN.CLI/ngin schema --format json
```

The first command prints the command map. The second prints the machine-readable
manifest schema. Add the executable to `PATH` if you want to invoke it simply as
`ngin`.

## Build the smallest example

```bash
./build/dev/Tools/NGIN.CLI/ngin build \
  --project Examples/Hello.Native/Hello.Native.nginproj \
  --configuration Debug \
  --output build/hello-native
```

Continue with [your first project](./first-project.md) to author a product from
an empty directory.
