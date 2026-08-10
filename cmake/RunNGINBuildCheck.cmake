if(NOT DEFINED CLI OR NOT DEFINED PROJECT OR NOT DEFINED CONFIGURATION OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "CLI, PROJECT, CONFIGURATION, and OUTPUT must be defined")
endif()

string(REPLACE "\"" "" CLI "${CLI}")
string(REPLACE "\"" "" PROJECT "${PROJECT}")
string(REPLACE "\"" "" CONFIGURATION "${CONFIGURATION}")
string(REPLACE "\"" "" OUTPUT "${OUTPUT}")

execute_process(
  COMMAND "${CLI}" build --project "${PROJECT}" --configuration "${CONFIGURATION}" --output "${OUTPUT}"
  RESULT_VARIABLE ngin_build_result
  OUTPUT_VARIABLE ngin_build_stdout
  ERROR_VARIABLE ngin_build_stderr
)

if(NOT ngin_build_result EQUAL 0)
  message(FATAL_ERROR "ngin build failed\nstdout:\n${ngin_build_stdout}\nstderr:\n${ngin_build_stderr}")
endif()

set(_binary "${OUTPUT}/cmake")
if(NOT IS_DIRECTORY "${_binary}")
  message(FATAL_ERROR "expected CMake build directory '${_binary}' was not produced")
endif()
