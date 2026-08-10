cmake_minimum_required(VERSION 3.28)

find_program(NGIN_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)

if(NOT TARGET clang-tidy)
  add_executable(clang-tidy IMPORTED GLOBAL)
  set_target_properties(clang-tidy PROPERTIES
    IMPORTED_LOCATION "${NGIN_CLANG_TIDY_EXECUTABLE}")
endif()
