if(NOT DEFINED CLI OR NOT DEFINED PROJECT OR NOT DEFINED PROFILE)
  message(FATAL_ERROR "CLI, PROJECT, and PROFILE must be defined")
endif()

string(REPLACE "\"" "" CLI "${CLI}")
string(REPLACE "\"" "" PROJECT "${PROJECT}")
string(REPLACE "\"" "" PROFILE "${PROFILE}")

execute_process(
  COMMAND "${CLI}" validate --project "${PROJECT}" --profile "${PROFILE}"
  RESULT_VARIABLE ngin_validate_result
  OUTPUT_VARIABLE ngin_validate_stdout
  ERROR_VARIABLE ngin_validate_stderr
)

if(ngin_validate_result EQUAL 0)
  message(FATAL_ERROR "expected ProjectRef config collision validation failure but command succeeded")
endif()

if(NOT ngin_validate_stdout MATCHES "config input destination collision")
  message(FATAL_ERROR "expected collision message was not reported\nstdout:\n${ngin_validate_stdout}\nstderr:\n${ngin_validate_stderr}")
endif()
