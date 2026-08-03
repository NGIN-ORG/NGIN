# NGIN libraries

The NGIN project tool does not require these libraries. Add only the parts your
application needs.

| Library | Purpose | Start here |
| --- | --- | --- |
| `NGIN.Base` | Foundational types, memory, async, I/O, networking, crypto, and serialization | [README](../../Dependencies/NGIN/NGIN.Base/README.md) |
| `NGIN.Log` | Structured application logging | [README](../../Dependencies/NGIN/NGIN.Log/README.md) |
| `NGIN.Core` | Optional application host, services, modules, configuration, and lifecycle | [README](../../Packages/NGIN.Core/README.md) |
| `NGIN.Reflection` | Runtime reflection metadata and access | [README](../../Dependencies/NGIN/NGIN.Reflection/README.md) |
| `NGIN.ECS` | Entity-component storage, queries, systems, and scheduling | [README](../../Dependencies/NGIN/NGIN.ECS/README.md) |
| `NGIN.UI` | Backend-neutral desktop UI toolkit | [README](../../Packages/NGIN.UI/README.md) |

Integration packages provide reflection generation, the SDL3 UI backend,
Windows accessibility, `NGIN.Core` UI hosting, and development-tool wrappers.
See the [packages index](../../Packages/README.md).

All libraries are experimental unless their own compatibility policy says
otherwise. Their public headers and generated API documentation are the API
source of truth.
