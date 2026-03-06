if(NOT DEFINED CLI OR NOT DEFINED PROJECT OR NOT DEFINED TARGET OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "CLI, PROJECT, TARGET, and OUTPUT must be defined")
endif()

string(REPLACE "\"" "" CLI "${CLI}")
string(REPLACE "\"" "" PROJECT "${PROJECT}")
string(REPLACE "\"" "" TARGET "${TARGET}")
string(REPLACE "\"" "" OUTPUT "${OUTPUT}")

execute_process(
  COMMAND "${CLI}" build --project "${PROJECT}" --target "${TARGET}" --output "${OUTPUT}"
  RESULT_VARIABLE ngin_build_result
  OUTPUT_VARIABLE ngin_build_stdout
  ERROR_VARIABLE ngin_build_stderr
)

if(NOT ngin_build_result EQUAL 0)
  message(FATAL_ERROR "ngin build failed\nstdout:\n${ngin_build_stdout}\nstderr:\n${ngin_build_stderr}")
endif()

if(NOT EXISTS "${OUTPUT}/${TARGET}.ngintarget")
  message(FATAL_ERROR "expected staged target manifest '${OUTPUT}/${TARGET}.ngintarget' was not produced")
endif()
