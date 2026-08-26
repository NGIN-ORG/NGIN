cmake_minimum_required(VERSION 3.20)

project(NGIN.Reflection.MetaGen VERSION 0.1 LANGUAGES CXX)

if(NOT TARGET NGIN::Base)
  add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../NGIN.Base" "${CMAKE_CURRENT_BINARY_DIR}/_ngin_base" EXCLUDE_FROM_ALL)
endif()

set(_ngin_reflection_metagen_core_sources
  src/MetaGenCommon.cpp
  src/MetaGenContext.cpp
  src/MetaGenEmitter.cpp
  src/MetaGenPreprocessor.cpp
  src/MetaGenScanner.cpp
  src/MetaGenScannerFrontend.cpp
  src/ReflectionModel.cpp
)

add_library(ngin_reflection_metagen_core STATIC ${_ngin_reflection_metagen_core_sources})
target_compile_features(ngin_reflection_metagen_core PUBLIC cxx_std_23)
target_include_directories(ngin_reflection_metagen_core PUBLIC src)
target_link_libraries(ngin_reflection_metagen_core PUBLIC NGIN::Base::Serialization)

add_executable(ngin_reflection_metagen src/main.cpp)
set_target_properties(ngin_reflection_metagen PROPERTIES OUTPUT_NAME ngin-metagen)
target_link_libraries(ngin_reflection_metagen PRIVATE ngin_reflection_metagen_core)

if(MSVC)
  target_compile_options(ngin_reflection_metagen_core PRIVATE /W4 /permissive-)
  target_compile_options(ngin_reflection_metagen PRIVATE /W4 /permissive-)
else()
  target_compile_options(ngin_reflection_metagen_core PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(ngin_reflection_metagen PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
  add_executable(ngin_reflection_metagen_tests tests/ScannerTests.cpp)
  target_link_libraries(ngin_reflection_metagen_tests PRIVATE ngin_reflection_metagen_core)
  if(MSVC)
    target_compile_options(ngin_reflection_metagen_tests PRIVATE /W4 /permissive-)
  else()
    target_compile_options(ngin_reflection_metagen_tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME NGIN.Reflection.MetaGen.Scanner COMMAND ngin_reflection_metagen_tests)
endif()
