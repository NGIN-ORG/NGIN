if(TARGET NGIN::Base)
  return()
endif()

set(_ngin_base_source_dir "${CMAKE_CURRENT_LIST_DIR}/../../Dependencies/NGIN/NGIN.Base")
if(NOT EXISTS "${_ngin_base_source_dir}/CMakeLists.txt")
  message(FATAL_ERROR "Packages/NGIN.Base requires Dependencies/NGIN/NGIN.Base")
endif()

set(NGIN_BASE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NGIN_BASE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NGIN_BASE_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
add_subdirectory("${_ngin_base_source_dir}" "${CMAKE_CURRENT_BINARY_DIR}/_ngin_base" EXCLUDE_FROM_ALL)
