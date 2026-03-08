add_executable(ngin_cli
  src/main.cpp
)
set_target_properties(ngin_cli PROPERTIES OUTPUT_NAME ngin)
target_link_libraries(ngin_cli PRIVATE NGIN::Base)
target_compile_features(ngin_cli PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(ngin_cli PRIVATE /W4 /permissive-)
else()
  target_compile_options(ngin_cli PRIVATE -Wall -Wextra -Wpedantic)
endif()
