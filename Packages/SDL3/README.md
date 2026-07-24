# SDL3 Source Provider

This package is the pinned SDL source provider used by
`NGIN.UI.Backend.SDL3`.

- Release: SDL 3.4.12
- Upstream commit: `f87239e71e42da91ca317a12eefb82cfbf3393eb`
- Source archive SHA-256:
  `363AB3EB2225D700BF0B67BC16349F9949125C747AC50DA3BA805C2D3A61B2A0`
- Upstream: <https://github.com/libsdl-org/SDL>
- License: zlib; the exact notice is in [`LICENSE.txt`](LICENSE.txt)

The wrapper builds SDL statically as `SDL3::SDL3-static` and disables SDL's
tests, examples, shared library, and install targets. Consumers integrate it
through the V4 `AddSubdirectory` package model.
