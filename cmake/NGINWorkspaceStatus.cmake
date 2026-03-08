if(NOT DEFINED NGIN_WORKSPACE_ROOT)
  message(FATAL_ERROR "NGIN_WORKSPACE_ROOT is required")
endif()

set(_components
  NGIN.Base
  NGIN.Log
  NGIN.Core
  NGIN.Reflection
  NGIN.ECS
  NGIN.Benchmark
)

foreach(_c IN LISTS _components)
  if(_c STREQUAL "NGIN.Core")
    set(_dir "${NGIN_WORKSPACE_ROOT}/Packages/NGIN.Core")
  else()
    set(_dir "${NGIN_WORKSPACE_ROOT}/Dependencies/NGIN/${_c}")
  endif()
  if(EXISTS "${_dir}/CMakeLists.txt")
    message(STATUS "[present] ${_c}")
  else()
    message(STATUS "[missing] ${_c}")
  endif()
endforeach()
