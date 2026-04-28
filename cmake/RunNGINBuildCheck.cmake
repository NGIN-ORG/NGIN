if(NOT DEFINED CLI OR NOT DEFINED PROJECT OR NOT DEFINED PROFILE OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "CLI, PROJECT, PROFILE, and OUTPUT must be defined")
endif()

string(REPLACE "\"" "" CLI "${CLI}")
string(REPLACE "\"" "" PROJECT "${PROJECT}")
string(REPLACE "\"" "" PROFILE "${PROFILE}")
string(REPLACE "\"" "" OUTPUT "${OUTPUT}")

execute_process(
  COMMAND "${CLI}" build --project "${PROJECT}" --profile "${PROFILE}" --output "${OUTPUT}"
  RESULT_VARIABLE ngin_build_result
  OUTPUT_VARIABLE ngin_build_stdout
  ERROR_VARIABLE ngin_build_stderr
)

if(NOT ngin_build_result EQUAL 0)
  message(FATAL_ERROR "ngin build failed\nstdout:\n${ngin_build_stdout}\nstderr:\n${ngin_build_stderr}")
endif()

get_filename_component(_project_name "${PROJECT}" NAME)
string(REGEX REPLACE "\\.nginproj$" "" _project_stem "${_project_name}")
set(_manifest "${OUTPUT}/${_project_stem}.${PROFILE}.nginlaunch")
if(NOT EXISTS "${_manifest}")
  message(FATAL_ERROR "expected launch manifest '${_manifest}' was not produced")
endif()
