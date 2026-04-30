include(CTest)

set(_ngin_cli_have_catch2 OFF)
set(_ngin_cli_catch2_extra_dir "")

if(TARGET Catch2::Catch2WithMain)
  set(_ngin_cli_have_catch2 ON)
endif()

if(NOT _ngin_cli_have_catch2)
  find_package(Catch2 3 CONFIG QUIET)
  if(Catch2_FOUND AND TARGET Catch2::Catch2WithMain)
    set(_ngin_cli_have_catch2 ON)
  endif()
endif()

if(NOT _ngin_cli_have_catch2)
  set(_ngin_cli_local_candidates
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../Packages/NGIN.Core/build/tests/_deps/catch2-src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../Packages/NGIN.Core/build/_deps/catch2-src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../Dependencies/NGIN/NGIN.Base/build/tests/_deps/catch2-src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../Dependencies/NGIN/NGIN.Base/build/_deps/catch2-src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../Dependencies/NGIN/NGIN.Reflection/build/tests/_deps/catch2-src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../Dependencies/NGIN/NGIN.Reflection/build/_deps/catch2-src"
  )

  foreach(_cand IN LISTS _ngin_cli_local_candidates)
    if(EXISTS "${_cand}/CMakeLists.txt")
      add_subdirectory("${_cand}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/catch2" EXCLUDE_FROM_ALL)
      if(TARGET Catch2::Catch2WithMain)
        set(_ngin_cli_have_catch2 ON)
        set(_ngin_cli_catch2_extra_dir "${_cand}/extras")
        break()
      endif()
    endif()
  endforeach()
endif()

if(NOT _ngin_cli_have_catch2)
  include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/CPM.cmake")
  set(CPM_SOURCE_CACHE "${PROJECT_BINARY_DIR}/cpm-cache" CACHE PATH "CPM cache")

  CPMAddPackage(
    NAME Catch2
    VERSION 3.5.3
    GITHUB_REPOSITORY catchorg/Catch2
    OPTIONS
      "CATCH_ENABLE_COVERAGE OFF"
      "CATCH_ENABLE_WERROR OFF"
      "CATCH_INSTALL_DOCS OFF"
      "CATCH_INSTALL_EXTRAS OFF"
  )

  if(Catch2_SOURCE_DIR)
    set(_ngin_cli_catch2_extra_dir "${Catch2_SOURCE_DIR}/extras")
  endif()

  if(TARGET Catch2::Catch2WithMain)
    set(_ngin_cli_have_catch2 ON)
  endif()
endif()

if(NOT _ngin_cli_have_catch2)
  message(FATAL_ERROR "Catch2::Catch2WithMain was not found or created")
endif()

if(_ngin_cli_catch2_extra_dir)
  list(APPEND CMAKE_MODULE_PATH "${_ngin_cli_catch2_extra_dir}")
endif()
include(Catch)

add_executable(NGINCliTests
  AllTests.cpp
)
target_compile_features(NGINCliTests PRIVATE cxx_std_20)
target_compile_definitions(NGINCliTests PRIVATE NGIN_CLI_TEST_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(NGINCliTests PRIVATE
  Catch2::Catch2WithMain
  ngin_cli_commands
  ngin_cli_build
  ngin_cli_resolution
  ngin_cli_authoring
  ngin_cli_model
)

if(MSVC)
  target_compile_options(NGINCliTests PRIVATE /W4 /permissive-)
else()
  target_compile_options(NGINCliTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

catch_discover_tests(NGINCliTests
  TEST_PREFIX "CLI."
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

if(TARGET ngin_cli)
  add_dependencies(ngin_cli NGINCliTests)
endif()
