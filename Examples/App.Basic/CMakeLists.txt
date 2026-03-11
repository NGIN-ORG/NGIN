cmake_minimum_required(VERSION 3.20)

project(App.Basic LANGUAGES CXX)

get_filename_component(APP_BASIC_EXAMPLE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)

add_executable(App.Basic
  src/main.cpp
)

target_compile_features(App.Basic PRIVATE cxx_std_23)
target_link_libraries(App.Basic PRIVATE NGIN::Core)
target_compile_definitions(App.Basic PRIVATE APP_BASIC_EXAMPLE_ROOT="${APP_BASIC_EXAMPLE_ROOT}")

if(TARGET NGIN.Core.BuildOptions)
  target_link_libraries(App.Basic PRIVATE NGIN.Core.BuildOptions)
endif()
