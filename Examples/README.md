# Examples

The examples are executable documentation. Each one demonstrates a small,
current path through the project system or an NGIN library.

## Learning path

| Step | Example | What it adds |
| --- | --- | --- |
| 1 | [Hello.Native](Hello.Native) | Plain C++ application and CLI workflow |
| 2 | [Hello.Hosted](Hello.Hosted) | `NGIN.Core` host, module, and staged config |
| 3 | [Hello.Reflection](Hello.Reflection) | Package-provided reflection generation |
| 4 | [Hello.ECS](Hello.ECS) | Entity-component storage and scheduling |
| 5 | [Hello.Analyzer](Hello.Analyzer) | Clang-Tidy package action |
| 6 | [Hello.Formatter](Hello.Formatter) | Clang-Format checks and edits |
| 7 | [NGIN.UI.Gallery](NGIN.UI.Gallery) | Standalone UI controls and features |
| 8 | [NGIN.UI.Gallery.Hosted](NGIN.UI.Gallery.Hosted) | The same UI through `NGIN.Core` |
| 9 | [Hello.Benchmark](Hello.Benchmark) | Executable registration through `ngin benchmark` |
| 10 | [Hello.Plugin](Hello.Plugin) | Loadable Plugin library artifact without runtime activation |

[NGIN.UI.MultiPage](NGIN.UI.MultiPage) demonstrates a smaller hosted,
service-driven UI application. [Hello.GameOfLife](Hello.GameOfLife) combines
NGIN.UI and NGIN.ECS in a larger interactive example.

## Common commands

From the repository root:

```bash
ngin validate --project Examples/Hello.Native/Hello.Native.nginproj --configuration Debug
ngin build --project Examples/Hello.Native/Hello.Native.nginproj --configuration Debug
ngin run --project Examples/Hello.Native/Hello.Native.nginproj --configuration Debug
```

Replace the project path with the example you want. Examples that require an
external tool or platform dependency say so in their README.

Generated output belongs under `build/` and is not part of the example source.
