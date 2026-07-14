# Hello.Hosted

`Hello.Hosted` is the smallest example that links `NGIN.Core` and starts an
application host.

## What This Proves

- The project builds through the same CLI workflow as `Hello.Native`.
- `NGIN.Core` can load config selected from the project manifest.
- C++ code registers a real static module implementation with
  `ApplicationBuilder::AddModule<T>()`.
- The manifest declares the hosted runtime module for tooling and graph output.
- Starting the host runs the selected module and exposes a service.

The important boundary is deliberate:

```text
C++ registers module implementations.
The manifest/profile declares the runtime shape used by the CLI.
```

## Files To Read

- [`Hello.Hosted.nginproj`](Hello.Hosted.nginproj)
- [`src/main.cpp`](src/main.cpp)
- [`config/app.cfg`](config/app.cfg)

## Build And Run

From the repository root:

```bash
cmake --preset dev
cmake --build build/dev --target ngin_cli

./build/dev/Tools/NGIN.CLI/ngin validate \
  --project Examples/Hello.Hosted/Hello.Hosted.nginproj \
  --profile Debug

./build/dev/Tools/NGIN.CLI/ngin build \
  --project Examples/Hello.Hosted/Hello.Hosted.nginproj \
  --profile Debug \
  --output build/manual/Hello.Hosted

./build/dev/Tools/NGIN.CLI/ngin run \
  --project Examples/Hello.Hosted/Hello.Hosted.nginproj \
  --profile Debug \
  --output build/manual/Hello.Hosted
```

Expected output:

```text
Hello.Hosted completed successfully
```

## What This Does Not Show

This example intentionally avoids dynamic plugins. It demonstrates the
recommended hosted-runtime starting point: static module registration, staged
configuration, and CLI-owned project metadata.
