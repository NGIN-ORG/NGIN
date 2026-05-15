cmake_minimum_required(VERSION 3.20)

project(NGIN.Reflection.MetaGen VERSION 0.1 LANGUAGES CXX)

option(NGIN_REFLECTION_METAGEN_ENABLE_CLANG "Enable NGIN.Reflection.MetaGen Clang backend." ON)

if(NOT TARGET NGIN::Base)
  add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../NGIN.Base" "${CMAKE_CURRENT_BINARY_DIR}/_ngin_base" EXCLUDE_FROM_ALL)
endif()

set(_ngin_reflection_metagen_sources
  src/MetaGenCommon.cpp
  src/MetaGenContext.cpp
  src/main.cpp
)

set(_ngin_reflection_metagen_with_clang OFF)
if(NGIN_REFLECTION_METAGEN_ENABLE_CLANG)
  enable_language(C)
  find_program(_ngin_reflection_metagen_llvm_config llvm-config)
  set(_ngin_reflection_metagen_clang_hint_args)
  if(_ngin_reflection_metagen_llvm_config)
    execute_process(
      COMMAND "${_ngin_reflection_metagen_llvm_config}" --prefix
      OUTPUT_VARIABLE _ngin_reflection_metagen_llvm_prefix
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(_ngin_reflection_metagen_llvm_prefix)
      set(LLVM_DIR "${_ngin_reflection_metagen_llvm_prefix}/lib/cmake/llvm" CACHE PATH "LLVM CMake package directory" FORCE)
      set(Clang_DIR "${_ngin_reflection_metagen_llvm_prefix}/lib/cmake/clang" CACHE PATH "Clang CMake package directory" FORCE)
      list(APPEND _ngin_reflection_metagen_clang_hint_args
        PATHS "${_ngin_reflection_metagen_llvm_prefix}/lib/cmake/clang"
        NO_DEFAULT_PATH
      )
    endif()
  endif()
  if(_ngin_reflection_metagen_clang_hint_args OR Clang_DIR)
    find_package(Clang CONFIG QUIET ${_ngin_reflection_metagen_clang_hint_args})
  endif()
  if(WIN32 AND NOT Clang_FOUND)
    find_package(Clang CONFIG QUIET)
  endif()
  if(WIN32 AND NOT TARGET libclang)
    set(_ngin_reflection_metagen_clang_include_dir "${LIBCLANG_INCLUDE_DIR}")
    set(_ngin_reflection_metagen_clang_library "${LIBCLANG_LIBRARY}")
    if(NOT _ngin_reflection_metagen_clang_include_dir AND DEFINED ENV{LIBCLANG_INCLUDE_DIR})
      set(_ngin_reflection_metagen_clang_include_dir "$ENV{LIBCLANG_INCLUDE_DIR}")
    endif()
    if(NOT _ngin_reflection_metagen_clang_library AND DEFINED ENV{LIBCLANG_LIBRARY})
      set(_ngin_reflection_metagen_clang_library "$ENV{LIBCLANG_LIBRARY}")
    endif()
    if(_ngin_reflection_metagen_clang_include_dir AND _ngin_reflection_metagen_clang_library)
      add_library(libclang UNKNOWN IMPORTED)
      set_target_properties(libclang PROPERTIES
        IMPORTED_LOCATION "${_ngin_reflection_metagen_clang_library}"
        INTERFACE_INCLUDE_DIRECTORIES "${_ngin_reflection_metagen_clang_include_dir}"
      )
      set(CLANG_INCLUDE_DIRS "${_ngin_reflection_metagen_clang_include_dir}")
    endif()
  endif()
  if(TARGET libclang)
    set(_ngin_reflection_metagen_with_clang ON)
    list(APPEND _ngin_reflection_metagen_sources src/MetaGenClang.cpp)
  else()
    list(APPEND _ngin_reflection_metagen_sources src/MetaGenStub.cpp)
    message(STATUS "NGIN.Reflection.MetaGen: Clang libclang target not found; building unavailable stub")
  endif()
else()
  list(APPEND _ngin_reflection_metagen_sources src/MetaGenStub.cpp)
endif()

add_executable(ngin_reflection_metagen
  ${_ngin_reflection_metagen_sources}
)
set_target_properties(ngin_reflection_metagen PROPERTIES OUTPUT_NAME ngin-metagen)
target_compile_features(ngin_reflection_metagen PRIVATE cxx_std_20)
target_include_directories(ngin_reflection_metagen PRIVATE src)
target_link_libraries(ngin_reflection_metagen PRIVATE NGIN::Base)
if(_ngin_reflection_metagen_with_clang)
  target_link_libraries(ngin_reflection_metagen PRIVATE libclang)
  if(CLANG_INCLUDE_DIRS)
    target_include_directories(ngin_reflection_metagen PRIVATE ${CLANG_INCLUDE_DIRS})
  endif()
  target_compile_definitions(ngin_reflection_metagen PRIVATE NGIN_REFLECTION_METAGEN_WITH_CLANG=1)
endif()

if(MSVC)
  target_compile_options(ngin_reflection_metagen PRIVATE /W4 /permissive-)
else()
  target_compile_options(ngin_reflection_metagen PRIVATE -Wall -Wextra -Wpedantic)
endif()
