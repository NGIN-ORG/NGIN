if(NOT DEFINED CLI OR NOT DEFINED PROJECT OR NOT DEFINED CONFIGURATION OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "CLI, PROJECT, CONFIGURATION, and OUTPUT must be defined")
endif()

string(REPLACE "\"" "" CLI "${CLI}")
string(REPLACE "\"" "" PROJECT "${PROJECT}")
string(REPLACE "\"" "" CONFIGURATION "${CONFIGURATION}")
string(REPLACE "\"" "" OUTPUT "${OUTPUT}")

file(REMOVE_RECURSE "${OUTPUT}")

execute_process(
  COMMAND "${CLI}" build --project "${PROJECT}" --configuration "${CONFIGURATION}" --output "${OUTPUT}"
  RESULT_VARIABLE ngin_first_build_result
  OUTPUT_VARIABLE ngin_first_build_stdout
  ERROR_VARIABLE ngin_first_build_stderr
)

if(NOT ngin_first_build_result EQUAL 0)
  message(FATAL_ERROR "initial ngin build failed\nstdout:\n${ngin_first_build_stdout}\nstderr:\n${ngin_first_build_stderr}")
endif()

file(WRITE "${OUTPUT}/keep.txt" "preserve-me\n")

execute_process(
  COMMAND "${CLI}" build --project "${PROJECT}" --configuration "${CONFIGURATION}" --output "${OUTPUT}"
  RESULT_VARIABLE ngin_second_build_result
  OUTPUT_VARIABLE ngin_second_build_stdout
  ERROR_VARIABLE ngin_second_build_stderr
)

if(NOT ngin_second_build_result EQUAL 0)
  message(FATAL_ERROR "second ngin build failed\nstdout:\n${ngin_second_build_stdout}\nstderr:\n${ngin_second_build_stderr}")
endif()

if(NOT EXISTS "${OUTPUT}/keep.txt")
  message(FATAL_ERROR "rebuild removed unrelated file '${OUTPUT}/keep.txt'")
endif()

file(READ "${OUTPUT}/keep.txt" _keep_text)
if(NOT _keep_text STREQUAL "preserve-me\n")
  message(FATAL_ERROR "rebuild modified unrelated file '${OUTPUT}/keep.txt'\n${_keep_text}")
endif()
