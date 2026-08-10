cmake_minimum_required(VERSION 3.28)

find_program(NGIN_CLANG_FORMAT_EXECUTABLE NAMES clang-format REQUIRED)

if(NOT TARGET clang-format)
  add_executable(clang-format IMPORTED GLOBAL)
  set_target_properties(clang-format PROPERTIES
    IMPORTED_LOCATION "${NGIN_CLANG_FORMAT_EXECUTABLE}")
endif()
