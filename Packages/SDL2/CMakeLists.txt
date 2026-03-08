if(TARGET SDL2::SDL2)
  return()
endif()

find_package(SDL2 CONFIG QUIET)
if(NOT TARGET SDL2::SDL2)
  find_package(SDL2 QUIET)
endif()

if(NOT TARGET SDL2::SDL2)
  message(FATAL_ERROR "Packages/SDL2 requires an SDL2 CMake package that exports SDL2::SDL2")
endif()
