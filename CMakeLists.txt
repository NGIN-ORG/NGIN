cmake_minimum_required(VERSION 3.20)

project(NGIN VERSION 0.1.0 LANGUAGES CXX)

option(NGIN_WORKSPACE_ENABLE_INTEGRATION "Enable integration helper targets in the workspace." ON)
option(NGIN_WORKSPACE_EXPECT_COMPONENTS "Warn if sibling component repos are missing." OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(NGINWorkspace)

ngin_workspace_print_summary(
  EXPECT_COMPONENTS ${NGIN_WORKSPACE_EXPECT_COMPONENTS}
)

add_custom_target(ngin.workspace
  COMMENT "NGIN workspace metadata target"
)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_custom_target(ngin.manifest.show
  COMMAND ${CMAKE_COMMAND} -E cat "${CMAKE_CURRENT_SOURCE_DIR}/manifests/platform-release.json"
  COMMENT "Print the active NGIN platform release manifest"
  VERBATIM
)

add_custom_target(ngin.spec001.validate
  COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/ngin-sync.py" validate-spec001
  COMMENT "Validate Spec 001 dependency and target graph rules"
  VERBATIM
)

add_custom_target(ngin.spec001.resolve.core
  COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/ngin-sync.py" resolve-target --target NGIN.CoreSample
  COMMENT "Resolve baseline core-host target closure from Spec 001 metadata"
  VERBATIM
)

if(NGIN_WORKSPACE_ENABLE_INTEGRATION)
  add_custom_target(ngin.component.status
    COMMAND ${CMAKE_COMMAND}
      -DNGIN_WORKSPACE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NGINWorkspaceStatus.cmake"
    COMMENT "Show sibling component repo status for this workspace"
    VERBATIM
  )
endif()
