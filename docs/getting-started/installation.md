# Installation

NGIN is currently built from source. It requires:

- Git
- CMake 3.20 or newer
- Ninja
- a C++23-capable compiler

Reflection generation additionally requires LLVM and libclang. The CLI and
ordinary native projects do not.

## Build the CLI

Clone the repository and its submodules:

```bash
git clone --recursive https://github.com/NGIN-ORG/NGIN.git
cd NGIN
```

For an existing checkout, initialize missing submodules with:

```bash
git submodule update --init --recursive
```

Configure and build:

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli
```

The executable is written to:

```text
build/dev/Tools/NGIN.CLI/ngin
```

On Windows it is `build\dev\Tools\NGIN.CLI\ngin.exe`.

## Check the installation

Run the CLI without arguments to print its command list:

```bash
./build/dev/Tools/NGIN.CLI/ngin
```

Then validate the smallest example:

```bash
./build/dev/Tools/NGIN.CLI/ngin validate \
  --project Examples/Hello.Native/Hello.Native.nginproj \
  --configuration Debug
```

Add the executable's directory to `PATH` if you want to invoke it as `ngin`.

## Next step

Continue with [Your first project](first-project.md), or open the runnable
[examples](../../Examples/README.md).
