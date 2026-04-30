function(_ngin_cli_apply_defaults target_name)
  target_compile_features(${target_name} PUBLIC cxx_std_20)
  target_include_directories(${target_name} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
  if(MSVC)
    target_compile_options(${target_name} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

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

add_library(ngin_cli_commands STATIC
  src/Commands.cpp
)
target_link_libraries(ngin_cli_commands PUBLIC ngin_cli_build ngin_cli_resolution ngin_cli_authoring ngin_cli_model PRIVATE NGIN::Base)
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
