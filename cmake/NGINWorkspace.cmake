include_guard(GLOBAL)

set(NGIN_WORKSPACE_KNOWN_COMPONENTS
  NGIN.Base
  NGIN.Log
  NGIN.Core
  NGIN.Reflection
  NGIN.ECS
  NGIN.Benchmark
)

function(ngin_workspace_component_dir out_var component_name)
  if(component_name STREQUAL "NGIN.Core")
    set(_candidate "${CMAKE_SOURCE_DIR}/Packages/NGIN.Core")
  else()
    set(_candidate "${CMAKE_SOURCE_DIR}/Dependencies/NGIN/${component_name}")
  endif()
  if(EXISTS "${_candidate}/CMakeLists.txt")
    set(${out_var} "${_candidate}" PARENT_SCOPE)
  else()
    set(${out_var} "" PARENT_SCOPE)
  endif()
endfunction()

function(ngin_workspace_print_summary)
  cmake_parse_arguments(NWS "" "EXPECT_COMPONENTS" "" ${ARGN})

  message(STATUS "NGIN workspace version: ${PROJECT_VERSION}")
  message(STATUS "Workspace manifest: ${CMAKE_SOURCE_DIR}/NGIN.ngin")

  foreach(_component IN LISTS NGIN_WORKSPACE_KNOWN_COMPONENTS)
    ngin_workspace_component_dir(_dir "${_component}")
    if(_dir)
      message(STATUS "  component present: ${_component} -> ${_dir}")
    elseif(NWS_EXPECT_COMPONENTS)
      message(WARNING "  component missing: ${_component}")
    else()
      message(STATUS "  component optional/missing: ${_component}")
    endif()
  endforeach()
endfunction()
