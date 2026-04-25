function(_ngin_cli_apply_defaults target_name)
  target_compile_features(${target_name} PUBLIC cxx_std_20)
  target_include_directories(${target_name} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
  if(MSVC)
    target_compile_options(${target_name} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

option(NGIN_CLI_ENABLE_METAGEN "Enable MetaGen support in the native CLI." ON)

add_library(ngin_cli_model STATIC
  src/Diagnostics.cpp
)
_ngin_cli_apply_defaults(ngin_cli_model)

add_library(ngin_cli_authoring STATIC
  src/Authoring.cpp
)
target_link_libraries(ngin_cli_authoring PUBLIC ngin_cli_model PRIVATE NGIN::Base)
_ngin_cli_apply_defaults(ngin_cli_authoring)

add_library(ngin_cli_resolution STATIC
  src/Resolution.cpp
)
target_link_libraries(ngin_cli_resolution PUBLIC ngin_cli_authoring ngin_cli_model PRIVATE NGIN::Base)
_ngin_cli_apply_defaults(ngin_cli_resolution)

add_library(ngin_cli_build STATIC
  src/Build.cpp
)
target_link_libraries(ngin_cli_build PUBLIC ngin_cli_resolution ngin_cli_authoring ngin_cli_model PRIVATE NGIN::Base)
_ngin_cli_apply_defaults(ngin_cli_build)

set(_ngin_cli_metagen_sources
  src/MetaGenCommon.cpp
)
set(_ngin_cli_metagen_real OFF)
if(NGIN_CLI_ENABLE_METAGEN)
  enable_language(C)
  find_program(_ngin_cli_llvm_config llvm-config)
  set(_ngin_cli_clang_hint_args)
  if(_ngin_cli_llvm_config)
    execute_process(
      COMMAND "${_ngin_cli_llvm_config}" --prefix
      OUTPUT_VARIABLE _ngin_cli_llvm_prefix
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(_ngin_cli_llvm_prefix)
      set(LLVM_DIR "${_ngin_cli_llvm_prefix}/lib/cmake/llvm" CACHE PATH "LLVM CMake package directory" FORCE)
      set(Clang_DIR "${_ngin_cli_llvm_prefix}/lib/cmake/clang" CACHE PATH "Clang CMake package directory" FORCE)
      list(APPEND _ngin_cli_clang_hint_args
        PATHS "${_ngin_cli_llvm_prefix}/lib/cmake/clang"
        NO_DEFAULT_PATH
      )
    endif()
  endif()
  if(_ngin_cli_clang_hint_args OR Clang_DIR)
    find_package(Clang CONFIG QUIET ${_ngin_cli_clang_hint_args})
  endif()
  if(Clang_FOUND AND TARGET libclang)
    set(_ngin_cli_metagen_real ON)
    list(APPEND _ngin_cli_metagen_sources src/MetaGenClang.cpp)
  else()
    list(APPEND _ngin_cli_metagen_sources src/MetaGenStub.cpp)
    message(STATUS "NGIN.MetaGen: Clang libclang target not found; building unavailable stub")
  endif()
else()
  list(APPEND _ngin_cli_metagen_sources src/MetaGenStub.cpp)
endif()

add_library(ngin_cli_metagen STATIC
  ${_ngin_cli_metagen_sources}
)
target_link_libraries(ngin_cli_metagen PUBLIC ngin_cli_authoring ngin_cli_model PRIVATE NGIN::Base)
if(_ngin_cli_metagen_real)
  target_link_libraries(ngin_cli_metagen PRIVATE libclang)
  if(CLANG_INCLUDE_DIRS)
    target_include_directories(ngin_cli_metagen PRIVATE ${CLANG_INCLUDE_DIRS})
  endif()
  target_compile_definitions(ngin_cli_metagen PRIVATE NGIN_CLI_METAGEN_WITH_CLANG=1)
endif()
_ngin_cli_apply_defaults(ngin_cli_metagen)

target_link_libraries(ngin_cli_build PUBLIC ngin_cli_metagen)

add_library(ngin_cli_commands STATIC
  src/Commands.cpp
)
target_link_libraries(ngin_cli_commands PUBLIC ngin_cli_build ngin_cli_metagen ngin_cli_resolution ngin_cli_authoring ngin_cli_model PRIVATE NGIN::Base)
_ngin_cli_apply_defaults(ngin_cli_commands)

add_executable(ngin_cli
  src/main.cpp
)
set_target_properties(ngin_cli PROPERTIES OUTPUT_NAME ngin)
target_link_libraries(ngin_cli PRIVATE ngin_cli_commands ngin_cli_authoring)
_ngin_cli_apply_defaults(ngin_cli)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
