---
title: Start with NGIN
description: What NGIN is, how its project tool and C++ libraries work, and how to get started.
---

# What is NGIN?

NGIN is an open-source toolkit for developing C++ applications. It includes
the `ngin` command-line tool for building, running, and testing projects, and
C++ libraries for user interfaces, networking, asynchronous tasks, logging,
reflection, and entity-component systems.

You describe a project's source files, dependencies, and build settings in a
`.nginproj` file. The `ngin` tool uses that file to generate a CMake build and
invoke your C++ compiler. The libraries supply reusable code for the
application itself and can be used independently of the project tool.

The goal of NGIN is to reduce the build configuration and application
infrastructure that C++ developers need to write and maintain.

**[Install NGIN](./start/installation.md) → [Build your first project](./start/first-project.md)**

## Start with a program you can run

The first walkthrough takes you from an empty directory to an executable
that prints `Hello from NGIN!`. You create two files: your C++ source and a
small project manifest that tells NGIN what to build.

```text
Hello/
├── Hello.nginproj
└── src/
    └── main.cpp
```

1. **[Set up the tools](./start/installation.md).** Build the CLI and check that
   it runs. You will need Git, CMake, Ninja, and a C++23-capable compiler.
2. **[Create and run Hello](./start/first-project.md).** Write the two files,
   validate the project, and build your first executable.
3. **Make it yours.** Change the message in `main.cpp`, then build and run again
   from the `Hello` directory:

```bash
ngin build --project Hello.nginproj --configuration Debug
ngin run --project Hello.nginproj --configuration Debug
```

You should see your updated message. That is the starting point for your own
application. The example uses the C++ standard library; NGIN libraries are
available when you want to add more.

## What will you build next?

Pick something you want your application to do. Each path below starts with
code you can try and explains how the pieces fit together.

| Add to your application | Start here |
| --- | --- |
| A native window with text and interactive controls | [Create a UI with NGIN.UI](./libraries/ui/quick-start.md) |
| Entities and systems that advance a simulation | [Run your first NGIN.ECS simulation](./libraries/ecs/quick-start.md) |
| Background tasks, cancellation, and asynchronous operations | [Write your first async operation](./libraries/base/async/first-task.md) |
| Services and modules with a shared startup and shutdown lifecycle | [Create an NGIN.Core host](./libraries/core/quick-start.md) |
| Logs with structured attributes and console output | [Add NGIN.Log](./libraries/log/quick-start.md) |
| Runtime access to type metadata and registered members | [Try NGIN.Reflection](./libraries/reflection/quick-start.md) |

Already working on an application? Go straight to the guide that fits your
next feature. The [library overview](./libraries.md) also covers memory,
containers, I/O, networking, serialization, cryptography, and math.

## Give the project room to grow

Once the first executable works, bring the rest of the application into the
same workflow:

- **[Add a package](./project-system/packages.md)** to use a library or a
  development tool.
- **[Stage application files](./project-system/build-stage-run.md)** so assets
  and runtime dependencies are available when the program launches.
- **[Create a workspace](./project-system/workspaces.md)** when several projects
  need shared package discovery and build profiles.

NGIN builds through CMake and your native compiler. When you want to understand
how a project is resolved, the [mental model](./start/mental-model.md) connects
your project file to the build, and the
[Composition Graph guide](./project-system/composition-graph.md) shows how to
inspect the result.

## Keep these nearby

- [C++ API reference](./reference.md#c-api-reference) — choose a library and
  browse its Doxygen declarations.
- [CLI and manifest reference](./reference.md) — look up commands and project
  file syntax.
- [Troubleshooting](./troubleshooting/index.md) — investigate a failed build,
  missing dependency, or launch problem.
- [Contributor path](./start/choose-your-path.md#ngin-contributor) — work on
  NGIN itself.
