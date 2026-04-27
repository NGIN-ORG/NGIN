if(NOT DEFINED CLI OR NOT DEFINED PROJECT OR NOT DEFINED CONFIGURATION OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "CLI, PROJECT, CONFIGURATION, and OUTPUT must be defined")
endif()

string(REPLACE "\"" "" CLI "${CLI}")
string(REPLACE "\"" "" PROJECT "${PROJECT}")
string(REPLACE "\"" "" CONFIGURATION "${CONFIGURATION}")
string(REPLACE "\"" "" OUTPUT "${OUTPUT}")

file(REMOVE_RECURSE "${OUTPUT}")

execute_process(
  COMMAND "${CLI}" configure --project "${PROJECT}" --configuration "${CONFIGURATION}" --output "${OUTPUT}"
  RESULT_VARIABLE ngin_configure_result
  OUTPUT_VARIABLE ngin_configure_stdout
  ERROR_VARIABLE ngin_configure_stderr
)

if(NOT ngin_configure_result EQUAL 0)
  message(FATAL_ERROR "ngin configure failed\nstdout:\n${ngin_configure_stdout}\nstderr:\n${ngin_configure_stderr}")
endif()

set(_compile_commands "${OUTPUT}/.ngin/cmake-build/compile_commands.json")
if(NOT EXISTS "${_compile_commands}")
  message(FATAL_ERROR "expected compile commands '${_compile_commands}' were not produced")
endif()

get_filename_component(_project_name "${PROJECT}" NAME)
string(REGEX REPLACE "\\.nginproj$" "" _project_stem "${_project_name}")
set(_manifest "${OUTPUT}/${_project_stem}.${CONFIGURATION}.nginlaunch")
if(EXISTS "${_manifest}")
  message(FATAL_ERROR "configure should not produce launch manifest '${_manifest}'")
endif()
