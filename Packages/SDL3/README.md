# SDL3 source provider

This directory is the NGIN package wrapper and CMake source provider for SDL3.
It downloads the pinned upstream source archive and exposes the static SDL3
target through CMake `AddSubdirectory` mode.

The pinned revision, checksum, build options, and upstream license are recorded
in this directory.

Applications normally select
[`NGIN.UI.Backend.SDL3`](../NGIN.UI.Backend.SDL3) instead of depending on this
provider directly.
