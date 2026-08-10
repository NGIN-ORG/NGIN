if(NOT DEFINED CLI OR NOT DEFINED PROJECT OR NOT DEFINED CONFIGURATION OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "CLI, PROJECT, CONFIGURATION, and OUTPUT must be defined")
endif()

string(REPLACE "\"" "" CLI "${CLI}")
string(REPLACE "\"" "" PROJECT "${PROJECT}")
string(REPLACE "\"" "" CONFIGURATION "${CONFIGURATION}")
string(REPLACE "\"" "" OUTPUT "${OUTPUT}")

execute_process(
  COMMAND "${CLI}" stage --project "${PROJECT}" --configuration "${CONFIGURATION}" --output "${OUTPUT}"
  RESULT_VARIABLE ngin_build_result
  OUTPUT_VARIABLE ngin_build_stdout
  ERROR_VARIABLE ngin_build_stderr
)

if(NOT ngin_build_result EQUAL 0)
  message(FATAL_ERROR "ngin build failed\nstdout:\n${ngin_build_stdout}\nstderr:\n${ngin_build_stderr}")
endif()

set(_root_config "${OUTPUT}/stage/config/root.cfg")
set(_library_config "${OUTPUT}/stage/config/library.cfg")
if(NOT EXISTS "${_root_config}")
  message(FATAL_ERROR "expected root config '${_root_config}' was not staged")
endif()
if(NOT EXISTS "${_library_config}")
  message(FATAL_ERROR "expected referenced project config '${_library_config}' was not staged")
endif()

file(READ "${_root_config}" _root_text)
file(READ "${_library_config}" _library_text)
if(NOT _root_text MATCHES "root-config-from-root-project")
  message(FATAL_ERROR "root config contents were not staged from the root project\n${_root_text}")
endif()
if(NOT _library_text MATCHES "library-config-from-referenced-project")
  message(FATAL_ERROR "referenced project config contents were not staged from the referenced project\n${_library_text}")
endif()
