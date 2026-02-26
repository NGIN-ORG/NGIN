if(NOT DEFINED NGIN_WORKSPACE_ROOT)
  message(FATAL_ERROR "NGIN_WORKSPACE_ROOT is required")
endif()

set(_components
  NGIN.Base
  NGIN.Core
  NGIN.Reflection
  NGIN.ECS
  NGIN.Benchmark
)

foreach(_c IN LISTS _components)
  set(_dir "${NGIN_WORKSPACE_ROOT}/${_c}")
  if(EXISTS "${_dir}/CMakeLists.txt")
    message(STATUS "[present] ${_c}")
  else()
    message(STATUS "[missing] ${_c}")
  endif()
endforeach()

