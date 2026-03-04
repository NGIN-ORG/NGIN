cmake_minimum_required(VERSION 3.20)
project(NGIN.Runtime VERSION 0.1 LANGUAGES CXX)

cmake_policy(SET CMP0077 NEW)

option(NGIN_RUNTIME_BUILD_STATIC "Build NGIN.Runtime as a static library" ON)
option(NGIN_RUNTIME_BUILD_SHARED "Build NGIN.Runtime as a shared library" OFF)
option(NGIN_RUNTIME_BUILD_TESTS "Build NGIN.Runtime tests" ON)
option(NGIN_RUNTIME_BUILD_EXAMPLES "Build NGIN.Runtime examples" ON)
option(NGIN_RUNTIME_BUILD_BENCHMARKS "Build NGIN.Runtime benchmarks" OFF)
option(NGIN_RUNTIME_ENABLE_ASAN "Enable Address + Undefined Sanitizers (GNU/Clang)" OFF)
option(NGIN_RUNTIME_ENABLE_TSAN "Enable ThreadSanitizer (GNU/Clang)" OFF)
option(NGIN_RUNTIME_ENABLE_LTO "Enable Link Time Optimization for Release/RelWithDebInfo" OFF)
option(NGIN_RUNTIME_STRICT_WARNINGS "Enable extra warning flags" ON)
option(NGIN_RUNTIME_EXPORT_COMPILE_COMMANDS "Generate compile_commands.json" ON)

if(NGIN_RUNTIME_ENABLE_ASAN AND NGIN_RUNTIME_ENABLE_TSAN)
  message(FATAL_ERROR "Cannot enable ASAN and TSAN simultaneously")
endif()

if(NOT NGIN_RUNTIME_BUILD_STATIC AND NOT NGIN_RUNTIME_BUILD_SHARED)
  message(FATAL_ERROR "At least one of static or shared build options must be enabled.")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ${NGIN_RUNTIME_EXPORT_COMPILE_COMMANDS})

function(ngin_runtime_enable_warnings tgt)
  if(NGIN_RUNTIME_STRICT_WARNINGS)
    get_target_property(_ngin_runtime_target_type ${tgt} TYPE)
    set(_ngin_runtime_visibility PRIVATE)
    if(_ngin_runtime_target_type STREQUAL "INTERFACE_LIBRARY")
      set(_ngin_runtime_visibility INTERFACE)
    endif()

    if(MSVC)
      target_compile_options(${tgt} ${_ngin_runtime_visibility} /W4 /permissive-)
    else()
      target_compile_options(${tgt} ${_ngin_runtime_visibility} -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow)
    endif()
  endif()
endfunction()

#-------------------------------------------------------------------------------
# Locate NGIN.Base
#-------------------------------------------------------------------------------
if(NOT TARGET NGIN::Base)
  find_package(NGINBase CONFIG QUIET)
  if(NOT NGINBase_FOUND)
    set(_base_dir "${CMAKE_CURRENT_LIST_DIR}/../NGIN.Base")
    if(EXISTS "${_base_dir}/CMakeLists.txt")
      message(STATUS "NGIN.Runtime: using sibling NGIN.Base from ${_base_dir}")
      set(NGIN_BASE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
      set(NGIN_BASE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
      set(NGIN_BASE_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
      add_subdirectory("${_base_dir}" _ngin_base EXCLUDE_FROM_ALL)
    elseif(DEFINED NGIN_BASE_SOURCE_DIR)
      message(STATUS "NGIN.Runtime: using NGIN.Base from ${NGIN_BASE_SOURCE_DIR}")
      set(NGIN_BASE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
      set(NGIN_BASE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
      set(NGIN_BASE_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
      add_subdirectory("${NGIN_BASE_SOURCE_DIR}" _ngin_base EXCLUDE_FROM_ALL)
    else()
      message(FATAL_ERROR "NGIN.Base not found. Install NGINBase or set NGIN_BASE_SOURCE_DIR.")
    endif()
  endif()
endif()

#-------------------------------------------------------------------------------
# Locate NGIN.Log
#-------------------------------------------------------------------------------
if(NOT TARGET NGIN::Log)
  find_package(NGINLog CONFIG QUIET)
  if(NOT NGINLog_FOUND)
    set(_log_dir "${CMAKE_CURRENT_LIST_DIR}/../NGIN.Log")
    if(EXISTS "${_log_dir}/CMakeLists.txt")
      message(STATUS "NGIN.Runtime: using sibling NGIN.Log from ${_log_dir}")
      set(NGIN_LOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
      set(NGIN_LOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
      set(NGIN_LOG_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
      add_subdirectory("${_log_dir}" _ngin_log EXCLUDE_FROM_ALL)
    elseif(DEFINED NGIN_LOG_SOURCE_DIR)
      message(STATUS "NGIN.Runtime: using NGIN.Log from ${NGIN_LOG_SOURCE_DIR}")
      set(NGIN_LOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
      set(NGIN_LOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
      set(NGIN_LOG_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
      add_subdirectory("${NGIN_LOG_SOURCE_DIR}" _ngin_log EXCLUDE_FROM_ALL)
    else()
      message(FATAL_ERROR "NGIN.Log not found. Install NGINLog or set NGIN_LOG_SOURCE_DIR.")
    endif()
  endif()
endif()

add_library(NGIN.Runtime.BuildOptions INTERFACE)
add_library(NGIN::Runtime::BuildOptions ALIAS NGIN.Runtime.BuildOptions)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  if(NGIN_RUNTIME_ENABLE_ASAN)
    target_compile_options(NGIN.Runtime.BuildOptions INTERFACE
      $<BUILD_INTERFACE:-fsanitize=address,undefined>
      $<BUILD_INTERFACE:-fno-omit-frame-pointer>
    )
    target_link_options(NGIN.Runtime.BuildOptions INTERFACE $<BUILD_INTERFACE:-fsanitize=address,undefined>)
  endif()
  if(NGIN_RUNTIME_ENABLE_TSAN)
    target_compile_options(NGIN.Runtime.BuildOptions INTERFACE
      $<BUILD_INTERFACE:-fsanitize=thread>
      $<BUILD_INTERFACE:-fno-omit-frame-pointer>
    )
    target_link_options(NGIN.Runtime.BuildOptions INTERFACE $<BUILD_INTERFACE:-fsanitize=thread>)
  endif()
endif()

set(NGIN_RUNTIME_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/NGIN/Runtime/Config.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/NGIN/Runtime/Events.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/NGIN/Runtime/Kernel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/NGIN/Runtime/Loader.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/NGIN/Runtime/Services.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/NGIN/Runtime/Tasks.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/NGIN/Runtime/Versioning.cpp
)

set(NGIN_RUNTIME_EXPORT_TARGETS)
set(ngin_runtime_primary_target "")

if(NGIN_RUNTIME_BUILD_STATIC OR NGIN_RUNTIME_BUILD_SHARED)
  add_library(NGIN.Runtime.Object OBJECT ${NGIN_RUNTIME_SOURCES})
  target_compile_features(NGIN.Runtime.Object PRIVATE cxx_std_23)
  set_target_properties(NGIN.Runtime.Object PROPERTIES CXX_EXTENSIONS OFF)
  target_include_directories(NGIN.Runtime.Object PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(NGIN.Runtime.Object PRIVATE NGIN::Base NGIN::Log NGIN.Runtime.BuildOptions)
  ngin_runtime_enable_warnings(NGIN.Runtime.Object)
endif()

if(NGIN_RUNTIME_BUILD_SHARED AND TARGET NGIN.Runtime.Object)
  set_property(TARGET NGIN.Runtime.Object PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

if(NGIN_RUNTIME_BUILD_STATIC)
  add_library(NGIN.Runtime.Static STATIC)
  if(TARGET NGIN.Runtime.Object)
    target_sources(NGIN.Runtime.Static PRIVATE $<TARGET_OBJECTS:NGIN.Runtime.Object>)
  endif()
  target_compile_features(NGIN.Runtime.Static PUBLIC cxx_std_23)
  set_target_properties(NGIN.Runtime.Static PROPERTIES CXX_EXTENSIONS OFF)
  target_include_directories(NGIN.Runtime.Static
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_link_libraries(NGIN.Runtime.Static PUBLIC NGIN::Base NGIN::Log)
  target_link_libraries(NGIN.Runtime.Static PRIVATE NGIN.Runtime.BuildOptions)
  target_compile_definitions(NGIN.Runtime.Static PUBLIC NGIN_RUNTIME_STATIC)
  set_target_properties(NGIN.Runtime.Static PROPERTIES OUTPUT_NAME "NGINRuntime" EXPORT_NAME RuntimeStatic)
  ngin_runtime_enable_warnings(NGIN.Runtime.Static)
  list(APPEND NGIN_RUNTIME_EXPORT_TARGETS NGIN.Runtime.Static)
  add_library(NGIN::Runtime::Static ALIAS NGIN.Runtime.Static)
  set(ngin_runtime_primary_target NGIN.Runtime.Static)
endif()

if(NGIN_RUNTIME_BUILD_SHARED)
  add_library(NGIN.Runtime.Shared SHARED)
  if(TARGET NGIN.Runtime.Object)
    target_sources(NGIN.Runtime.Shared PRIVATE $<TARGET_OBJECTS:NGIN.Runtime.Object>)
  endif()
  target_compile_features(NGIN.Runtime.Shared PUBLIC cxx_std_23)
  set_target_properties(NGIN.Runtime.Shared PROPERTIES CXX_EXTENSIONS OFF)
  target_include_directories(NGIN.Runtime.Shared
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_link_libraries(NGIN.Runtime.Shared PUBLIC NGIN::Base NGIN::Log)
  target_link_libraries(NGIN.Runtime.Shared PRIVATE NGIN.Runtime.BuildOptions)
  target_compile_definitions(NGIN.Runtime.Shared PUBLIC NGIN_RUNTIME_SHARED)
  target_compile_definitions(NGIN.Runtime.Shared PRIVATE NGIN_RUNTIME_SHARED_BUILD)
  set_target_properties(NGIN.Runtime.Shared PROPERTIES OUTPUT_NAME "NGINRuntime" EXPORT_NAME RuntimeShared)
  if(NOT WIN32)
    set_target_properties(NGIN.Runtime.Shared PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR} VERSION ${PROJECT_VERSION})
  endif()
  ngin_runtime_enable_warnings(NGIN.Runtime.Shared)
  list(APPEND NGIN_RUNTIME_EXPORT_TARGETS NGIN.Runtime.Shared)
  add_library(NGIN::Runtime::Shared ALIAS NGIN.Runtime.Shared)
  set(ngin_runtime_primary_target NGIN.Runtime.Shared)
endif()

if(NOT ngin_runtime_primary_target)
  message(FATAL_ERROR "No primary NGIN.Runtime target could be determined.")
endif()

add_library(NGIN::Runtime ALIAS ${ngin_runtime_primary_target})

if(NGIN_RUNTIME_ENABLE_LTO)
  if(NGIN_RUNTIME_ENABLE_ASAN OR NGIN_RUNTIME_ENABLE_TSAN)
    message(WARNING "LTO requested but sanitizers are enabled; skipping IPO/LTO.")
  else()
    include(CheckIPOSupported)
    check_ipo_supported(RESULT NGIN_RUNTIME_HAS_IPO OUTPUT NGIN_RUNTIME_IPO_ERR)
    if(NGIN_RUNTIME_HAS_IPO)
      foreach(tgt IN ITEMS NGIN.Runtime.Object NGIN.Runtime.Static NGIN.Runtime.Shared)
        if(TARGET ${tgt})
          foreach(cfg IN ITEMS Release RelWithDebInfo MinSizeRel)
            set_property(TARGET ${tgt} PROPERTY INTERPROCEDURAL_OPTIMIZATION_${cfg} ON)
          endforeach()
        endif()
      endforeach()
    else()
      message(WARNING "LTO requested but not supported: ${NGIN_RUNTIME_IPO_ERR}")
    endif()
  endif()
endif()

include(GNUInstallDirs)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(
  TARGETS ${NGIN_RUNTIME_EXPORT_TARGETS}
  EXPORT NGINRuntimeTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

set_target_properties(NGIN.Runtime.BuildOptions PROPERTIES EXPORT_NAME RuntimeBuildOptions)
install(TARGETS NGIN.Runtime.BuildOptions EXPORT NGINRuntimeTargets)

install(
  EXPORT NGINRuntimeTargets
  FILE NGINRuntimeTargets.cmake
  NAMESPACE NGIN::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NGINRuntime
)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/NGINRuntimeConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NGINRuntimeConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/NGINRuntimeConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NGINRuntime
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/NGINRuntimeConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/NGINRuntimeConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NGINRuntime
)

if(NGIN_RUNTIME_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()

if(NGIN_RUNTIME_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(NGIN_RUNTIME_BUILD_BENCHMARKS)
  # Reserved for future runtime benchmarks.
endif()
