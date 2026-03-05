cmake_minimum_required(VERSION 3.20)
project(NGIN.Core VERSION 0.1 LANGUAGES CXX)

cmake_policy(SET CMP0077 NEW)

option(NGIN_CORE_BUILD_STATIC "Build NGIN.Core as a static library" ON)
option(NGIN_CORE_BUILD_SHARED "Build NGIN.Core as a shared library" OFF)
option(NGIN_CORE_BUILD_TESTS "Build NGIN.Core tests" ON)
option(NGIN_CORE_BUILD_EXAMPLES "Build NGIN.Core examples" ON)
option(NGIN_CORE_BUILD_BENCHMARKS "Build NGIN.Core benchmarks" OFF)
option(NGIN_CORE_ENABLE_ASAN "Enable Address + Undefined Sanitizers (GNU/Clang)" OFF)
option(NGIN_CORE_ENABLE_TSAN "Enable ThreadSanitizer (GNU/Clang)" OFF)
option(NGIN_CORE_ENABLE_LTO "Enable Link Time Optimization for Release/RelWithDebInfo" OFF)
option(NGIN_CORE_STRICT_WARNINGS "Enable extra warning flags" ON)
option(NGIN_CORE_EXPORT_COMPILE_COMMANDS "Generate compile_commands.json" ON)

if(NGIN_CORE_ENABLE_ASAN AND NGIN_CORE_ENABLE_TSAN)
  message(FATAL_ERROR "Cannot enable ASAN and TSAN simultaneously")
endif()

if(NOT NGIN_CORE_BUILD_STATIC AND NOT NGIN_CORE_BUILD_SHARED)
  message(FATAL_ERROR "At least one of static or shared build options must be enabled.")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ${NGIN_CORE_EXPORT_COMPILE_COMMANDS})

function(ngin_core_enable_warnings tgt)
  if(NGIN_CORE_STRICT_WARNINGS)
    get_target_property(_ngin_core_target_type ${tgt} TYPE)
    set(_ngin_core_visibility PRIVATE)
    if(_ngin_core_target_type STREQUAL "INTERFACE_LIBRARY")
      set(_ngin_core_visibility INTERFACE)
    endif()

    if(MSVC)
      target_compile_options(${tgt} ${_ngin_core_visibility} /W4 /permissive-)
    else()
      target_compile_options(${tgt} ${_ngin_core_visibility} -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow)
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
      message(STATUS "NGIN.Core: using sibling NGIN.Base from ${_base_dir}")
      set(NGIN_BASE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
      set(NGIN_BASE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
      set(NGIN_BASE_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
      add_subdirectory("${_base_dir}" _ngin_base EXCLUDE_FROM_ALL)
    elseif(DEFINED NGIN_BASE_SOURCE_DIR)
      message(STATUS "NGIN.Core: using NGIN.Base from ${NGIN_BASE_SOURCE_DIR}")
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
      message(STATUS "NGIN.Core: using sibling NGIN.Log from ${_log_dir}")
      set(NGIN_LOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
      set(NGIN_LOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
      set(NGIN_LOG_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
      add_subdirectory("${_log_dir}" _ngin_log EXCLUDE_FROM_ALL)
    elseif(DEFINED NGIN_LOG_SOURCE_DIR)
      message(STATUS "NGIN.Core: using NGIN.Log from ${NGIN_LOG_SOURCE_DIR}")
      set(NGIN_LOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
      set(NGIN_LOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
      set(NGIN_LOG_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
      add_subdirectory("${NGIN_LOG_SOURCE_DIR}" _ngin_log EXCLUDE_FROM_ALL)
    else()
      message(FATAL_ERROR "NGIN.Log not found. Install NGINLog or set NGIN_LOG_SOURCE_DIR.")
    endif()
  endif()
endif()

add_library(NGIN.Core.BuildOptions INTERFACE)
add_library(NGIN::Core::BuildOptions ALIAS NGIN.Core.BuildOptions)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  if(NGIN_CORE_ENABLE_ASAN)
    target_compile_options(NGIN.Core.BuildOptions INTERFACE
      $<BUILD_INTERFACE:-fsanitize=address,undefined>
      $<BUILD_INTERFACE:-fno-omit-frame-pointer>
    )
    target_link_options(NGIN.Core.BuildOptions INTERFACE $<BUILD_INTERFACE:-fsanitize=address,undefined>)
  endif()
  if(NGIN_CORE_ENABLE_TSAN)
    target_compile_options(NGIN.Core.BuildOptions INTERFACE
      $<BUILD_INTERFACE:-fsanitize=thread>
      $<BUILD_INTERFACE:-fno-omit-frame-pointer>
    )
    target_link_options(NGIN.Core.BuildOptions INTERFACE $<BUILD_INTERFACE:-fsanitize=thread>)
  endif()
endif()

set(NGIN_CORE_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/NGIN/Core/Config.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/NGIN/Core/Events.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/NGIN/Core/Kernel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/NGIN/Core/Loader.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/NGIN/Core/Services.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/NGIN/Core/Tasks.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/NGIN/Core/Versioning.cpp
)

set(NGIN_CORE_EXPORT_TARGETS)
set(ngin_core_primary_target "")

if(NGIN_CORE_BUILD_STATIC OR NGIN_CORE_BUILD_SHARED)
  add_library(NGIN.Core.Object OBJECT ${NGIN_CORE_SOURCES})
  target_compile_features(NGIN.Core.Object PRIVATE cxx_std_23)
  set_target_properties(NGIN.Core.Object PROPERTIES CXX_EXTENSIONS OFF)
  target_include_directories(NGIN.Core.Object PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(NGIN.Core.Object PRIVATE NGIN::Base NGIN::Log NGIN.Core.BuildOptions)
  ngin_core_enable_warnings(NGIN.Core.Object)
endif()

if(NGIN_CORE_BUILD_SHARED AND TARGET NGIN.Core.Object)
  set_property(TARGET NGIN.Core.Object PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

if(NGIN_CORE_BUILD_STATIC)
  add_library(NGIN.Core.Static STATIC)
  if(TARGET NGIN.Core.Object)
    target_sources(NGIN.Core.Static PRIVATE $<TARGET_OBJECTS:NGIN.Core.Object>)
  endif()
  target_compile_features(NGIN.Core.Static PUBLIC cxx_std_23)
  set_target_properties(NGIN.Core.Static PROPERTIES CXX_EXTENSIONS OFF)
  target_include_directories(NGIN.Core.Static
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_link_libraries(NGIN.Core.Static PUBLIC NGIN::Base NGIN::Log)
  target_link_libraries(NGIN.Core.Static PRIVATE NGIN.Core.BuildOptions)
  target_compile_definitions(NGIN.Core.Static PUBLIC NGIN_CORE_STATIC)
  set_target_properties(NGIN.Core.Static PROPERTIES OUTPUT_NAME "NGINCore" EXPORT_NAME "Core::Static")
  ngin_core_enable_warnings(NGIN.Core.Static)
  list(APPEND NGIN_CORE_EXPORT_TARGETS NGIN.Core.Static)
  add_library(NGIN::Core::Static ALIAS NGIN.Core.Static)
  set(ngin_core_primary_target NGIN.Core.Static)
endif()

if(NGIN_CORE_BUILD_SHARED)
  add_library(NGIN.Core.Shared SHARED)
  if(TARGET NGIN.Core.Object)
    target_sources(NGIN.Core.Shared PRIVATE $<TARGET_OBJECTS:NGIN.Core.Object>)
  endif()
  target_compile_features(NGIN.Core.Shared PUBLIC cxx_std_23)
  set_target_properties(NGIN.Core.Shared PROPERTIES CXX_EXTENSIONS OFF)
  target_include_directories(NGIN.Core.Shared
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_link_libraries(NGIN.Core.Shared PUBLIC NGIN::Base NGIN::Log)
  target_link_libraries(NGIN.Core.Shared PRIVATE NGIN.Core.BuildOptions)
  target_compile_definitions(NGIN.Core.Shared PUBLIC NGIN_CORE_SHARED)
  target_compile_definitions(NGIN.Core.Shared PRIVATE NGIN_CORE_SHARED_BUILD)
  set_target_properties(NGIN.Core.Shared PROPERTIES OUTPUT_NAME "NGINCore" EXPORT_NAME "Core::Shared")
  if(NOT WIN32)
    set_target_properties(NGIN.Core.Shared PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR} VERSION ${PROJECT_VERSION})
  endif()
  ngin_core_enable_warnings(NGIN.Core.Shared)
  list(APPEND NGIN_CORE_EXPORT_TARGETS NGIN.Core.Shared)
  add_library(NGIN::Core::Shared ALIAS NGIN.Core.Shared)
  set(ngin_core_primary_target NGIN.Core.Shared)
endif()

if(NOT ngin_core_primary_target)
  message(FATAL_ERROR "No primary NGIN.Core target could be determined.")
endif()

add_library(NGIN::Core ALIAS ${ngin_core_primary_target})

if(NGIN_CORE_ENABLE_LTO)
  if(NGIN_CORE_ENABLE_ASAN OR NGIN_CORE_ENABLE_TSAN)
    message(WARNING "LTO requested but sanitizers are enabled; skipping IPO/LTO.")
  else()
    include(CheckIPOSupported)
    check_ipo_supported(RESULT NGIN_CORE_HAS_IPO OUTPUT NGIN_CORE_IPO_ERR)
    if(NGIN_CORE_HAS_IPO)
      foreach(tgt IN ITEMS NGIN.Core.Object NGIN.Core.Static NGIN.Core.Shared)
        if(TARGET ${tgt})
          foreach(cfg IN ITEMS Release RelWithDebInfo MinSizeRel)
            set_property(TARGET ${tgt} PROPERTY INTERPROCEDURAL_OPTIMIZATION_${cfg} ON)
          endforeach()
        endif()
      endforeach()
    else()
      message(WARNING "LTO requested but not supported: ${NGIN_CORE_IPO_ERR}")
    endif()
  endif()
endif()

include(GNUInstallDirs)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(
  TARGETS ${NGIN_CORE_EXPORT_TARGETS}
  EXPORT NGINCoreTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

set_target_properties(NGIN.Core.BuildOptions PROPERTIES EXPORT_NAME "Core::BuildOptions")
install(TARGETS NGIN.Core.BuildOptions EXPORT NGINCoreTargets)

install(
  EXPORT NGINCoreTargets
  FILE NGINCoreTargets.cmake
  NAMESPACE NGIN::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NGINCore
)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/NGINCoreConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NGINCoreConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/NGINCoreConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NGINCore
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/NGINCoreConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/NGINCoreConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NGINCore
)

if(NGIN_CORE_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()

if(NGIN_CORE_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(NGIN_CORE_BUILD_BENCHMARKS)
  # Reserved for future runtime benchmarks.
endif()
