# Installation

NGIN.ECS needs CMake 3.20+, a C++23 compiler, and NGIN.Base 0.1.x. Choose one
of the following setups.

## Installed Package

Install NGIN.Base first, then NGIN.ECS to the same prefix:

```sh
cmake -S path/to/NGIN.ECS -B build/ecs -DCMAKE_INSTALL_PREFIX=/opt/ngin
cmake --build build/ecs --config Release
cmake --install build/ecs --config Release
```

Consume the exported target:

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyGame LANGUAGES CXX)

find_package(NGINECS 0.4 CONFIG REQUIRED)

add_executable(MyGame main.cpp)
target_link_libraries(MyGame PRIVATE NGIN::ECS)
target_compile_features(MyGame PRIVATE cxx_std_23)
set_target_properties(MyGame PROPERTIES CXX_EXTENSIONS OFF)
```

If CMake cannot find the package, add the install prefix to
`CMAKE_PREFIX_PATH`. `NGINECSConfig.cmake` finds NGIN.Base for you.

## Source Tree

```cmake
set(NGIN_ECS_BUILD_TESTS OFF CACHE BOOL "")
set(NGIN_ECS_BUILD_EXAMPLES OFF CACHE BOOL "")
set(NGIN_ECS_BUILD_BENCHMARKS OFF CACHE BOOL "")
add_subdirectory(path/to/NGIN.ECS)

target_link_libraries(MyGame PRIVATE NGIN::ECS)
```

If `NGIN::Base` does not already exist, ECS looks for an installed NGIN.Base
and then for a sibling `NGIN.Base` source directory.

## NGIN Workspace

Declare the package directly on the project:

```xml
<Executable Name="MyGame">
  <Uses><Package Name="NGIN.ECS" Version="0.4" /></Uses>
  <Build><Source Include="src/**/*.cpp" /></Build>
</Executable>
```

The workspace must provide local sources or providers for `NGIN.Base` and
`NGIN.ECS`. See `Examples/Hello.ECS` for a working product-first project.

## Verify The Setup

Compile this file and link it to `NGIN::ECS`:

```cpp
#include <NGIN/ECS/ECS.hpp>

int main()
{
    NGIN::ECS::Simulation simulation;
    return simulation.GetWorld().Stats().AliveEntities == 0 ? 0 : 1;
}
```

The library is static and depends only on `NGIN::Base`; it does not bring in
NGIN.Core or NGIN.Reflection.
