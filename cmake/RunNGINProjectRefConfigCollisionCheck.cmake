if(NOT DEFINED CLI OR NOT DEFINED PROJECT OR NOT DEFINED CONFIGURATION OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "CLI, PROJECT, CONFIGURATION, and OUTPUT must be defined")
endif()

string(REPLACE "\"" "" CLI "${CLI}")
string(REPLACE "\"" "" PROJECT "${PROJECT}")
string(REPLACE "\"" "" CONFIGURATION "${CONFIGURATION}")
string(REPLACE "\"" "" OUTPUT "${OUTPUT}")

execute_process(
  COMMAND "${CLI}" stage --project "${PROJECT}" --configuration "${CONFIGURATION}" --output "${OUTPUT}"
  RESULT_VARIABLE ngin_validate_result
  OUTPUT_VARIABLE ngin_validate_stdout
  ERROR_VARIABLE ngin_validate_stderr
)

if(ngin_validate_result EQUAL 0)
  message(FATAL_ERROR "expected ProjectRef config collision validation failure but command succeeded")
endif()

if(NOT ngin_validate_stderr MATCHES "StagePlan collision")
  message(FATAL_ERROR "expected collision message was not reported\nstdout:\n${ngin_validate_stdout}\nstderr:\n${ngin_validate_stderr}")
endif()
