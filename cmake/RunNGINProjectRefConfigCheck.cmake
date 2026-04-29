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

set(_manifest "${OUTPUT}/ProjectRef.Config.Root.${PROFILE}.nginlaunch")
if(NOT EXISTS "${_manifest}")
  message(FATAL_ERROR "expected launch manifest '${_manifest}' was not produced")
endif()

set(_root_config "${OUTPUT}/config/root.cfg")
set(_library_config "${OUTPUT}/config/library.cfg")
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

file(READ "${_manifest}" _manifest_text)
if(NOT _manifest_text MATCHES "<Input Kind=\"Config\" Path=\"config/root.cfg\" OwnerKind=\"project\" Owner=\"ProjectRef.Config.Root\".*Destination=\"config/root.cfg\"")
  message(FATAL_ERROR "root config metadata was not written to the launch manifest\n${_manifest_text}")
endif()
if(NOT _manifest_text MATCHES "<Input Kind=\"Config\" Path=\"config/library.cfg\" OwnerKind=\"project\" Owner=\"ProjectRef.Config.Library\".*Destination=\"config/library.cfg\"")
  message(FATAL_ERROR "referenced project config metadata was not written to the launch manifest\n${_manifest_text}")
endif()
