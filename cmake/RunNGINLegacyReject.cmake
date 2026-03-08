if(NOT DEFINED CLI OR NOT DEFINED WORK_DIR)
  message(FATAL_ERROR "CLI and WORK_DIR must be defined")
endif()

string(REPLACE "\"" "" CLI "${CLI}")
string(REPLACE "\"" "" WORK_DIR "${WORK_DIR}")

file(MAKE_DIRECTORY "${WORK_DIR}")
file(WRITE "${WORK_DIR}/legacy.project" "{\n  \"name\": \"Legacy\"\n}\n")

execute_process(
  COMMAND "${CLI}" project validate --project "${WORK_DIR}/legacy.project"
  RESULT_VARIABLE legacy_result
  OUTPUT_VARIABLE legacy_stdout
  ERROR_VARIABLE legacy_stderr
)

if(legacy_result EQUAL 0)
  message(FATAL_ERROR "expected legacy manifest rejection but command succeeded")
endif()

message("legacy manifest rejected")
