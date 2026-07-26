# NGIN.UI third-party notices

The native text and SDL3 backend implementations use pinned upstream sources.
The source revisions and archive hashes below are part of the reproducible
build contract.

| Component | Revision | License | Integration |
| --- | --- | --- | --- |
| FreeType 2.14.3 | `0a0221a1347e2f1e07c395263540026e9a0aa7c7` | FreeType License (FTL) or GPL-2.0; NGIN.UI uses the FTL option | Private static source build |
| HarfBuzz 14.2.1 | `56feae4035bdd48f62ba2b8d8c16232d4d89b3a4` | MIT-style HarfBuzz license | Private static source build |
| Noto Sans variable | `google/fonts@6f2d0cd65e61cb237409c0802757a1e55481422d` | SIL Open Font License 1.1 | Bundled runtime font |
| Noto Sans Arabic variable | `google/fonts@6f2d0cd65e61cb237409c0802757a1e55481422d` | SIL Open Font License 1.1 | Bundled runtime fallback font |
| Noto Sans Symbols 2 | `google/fonts@6f2d0cd65e61cb237409c0802757a1e55481422d` | SIL Open Font License 1.1 | Bundled runtime fallback font |
| stb_image 2.30 | `013ac3beddff3dbffafd5177e7972067cd2b5083` | MIT or public domain; NGIN.UI uses the MIT option | Private PNG/JPEG source build |
| SDL 3.4.12 | `f87239e71e42da91ca317a12eefb82cfbf3393eb` | zlib License | SDL3 backend source build |

The complete font licenses are installed beside their fonts under
`assets/fonts`. The stb dual-license text is installed from
`assets/licenses/stb-LICENSE.txt`; NGIN.UI selects its MIT option. The source
archives contain the complete FreeType, HarfBuzz, and SDL license files.
Distributions that ship the linked libraries must retain those notices.

Upstream projects:

- <https://github.com/freetype/freetype>
- <https://github.com/harfbuzz/harfbuzz>
- <https://github.com/google/fonts/tree/main/ofl/notosans>
- <https://github.com/google/fonts/tree/main/ofl/notosansarabic>
- <https://github.com/google/fonts/tree/main/ofl/notosanssymbols2>
- <https://github.com/nothings/stb>
- <https://github.com/libsdl-org/SDL>
