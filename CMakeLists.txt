cmake_minimum_required(VERSION 3.20)

project(NGIN VERSION 0.1.0 LANGUAGES CXX)

option(NGIN_WORKSPACE_ENABLE_INTEGRATION "Enable integration helper targets in the workspace." ON)
option(NGIN_WORKSPACE_EXPECT_COMPONENTS "Warn if declared workspace components are missing." OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(NGINWorkspace)

ngin_workspace_print_summary(
  EXPECT_COMPONENTS ${NGIN_WORKSPACE_EXPECT_COMPONENTS}
)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Packages/NGIN.Base/CMakeLists.txt")
  message(FATAL_ERROR "Packages/NGIN.Base is required to build the native ngin CLI in this workspace.")
endif()

add_subdirectory(Packages/NGIN.Base EXCLUDE_FROM_ALL)

add_custom_target(ngin.workspace
  COMMENT "NGIN workspace metadata target"
)

add_subdirectory(Tools)

set(NGIN_WORKSPACE_PROJECT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/manifests/NGIN.Workspace.nginproj")
set(NGIN_WORKSPACE_STAGE_DIR "${CMAKE_BINARY_DIR}/stage")
set(NGIN_WORKSPACE_BUILD_OUTPUT "${NGIN_WORKSPACE_STAGE_DIR}/NGIN.CoreSample")

add_custom_target(ngin.manifest.show
  COMMAND ${CMAKE_COMMAND} -E cat "${CMAKE_CURRENT_SOURCE_DIR}/manifests/platform-release.xml"
  COMMENT "Print the active NGIN platform release manifest"
  VERBATIM
)

add_custom_target(ngin.project.show
  COMMAND ${CMAKE_COMMAND} -E cat "${NGIN_WORKSPACE_PROJECT_PATH}"
  COMMENT "Print the canonical workspace project manifest"
  VERBATIM
)

add_custom_target(ngin.validate
  COMMAND $<TARGET_FILE:ngin_cli>
    project
    validate
    --project "${NGIN_WORKSPACE_PROJECT_PATH}"
    --target NGIN.CoreSample
  DEPENDS ngin_cli
  COMMENT "Validate the core sample target from the canonical workspace project"
  VERBATIM
)

add_custom_target(ngin.graph.core
  COMMAND $<TARGET_FILE:ngin_cli>
    project
    graph
    --project "${NGIN_WORKSPACE_PROJECT_PATH}"
    --target NGIN.CoreSample
  DEPENDS ngin_cli
  COMMENT "Print the resolved package and module graph for the core sample target"
  VERBATIM
)

add_custom_target(ngin.build.core
  COMMAND ${CMAKE_COMMAND} -E make_directory "${NGIN_WORKSPACE_STAGE_DIR}"
  COMMAND $<TARGET_FILE:ngin_cli>
    project
    build
    --project "${NGIN_WORKSPACE_PROJECT_PATH}"
    --target NGIN.CoreSample
    --output "${NGIN_WORKSPACE_BUILD_OUTPUT}"
  DEPENDS ngin_cli
  COMMENT "Build the staged layout for the core sample target"
  VERBATIM
)

add_custom_target(ngin.workflow
  DEPENDS
    ngin.validate
    ngin.graph.core
    ngin.build.core
  COMMENT "Run the standard workspace validate, graph, and build flow"
)

enable_testing()

add_test(NAME ngin.doctor
  COMMAND $<TARGET_FILE:ngin_cli> workspace doctor
)

add_test(NAME ngin.package.list
  COMMAND $<TARGET_FILE:ngin_cli> package list
)

add_test(NAME ngin.package.show.core
  COMMAND $<TARGET_FILE:ngin_cli> package show NGIN.Core
)

add_test(NAME ngin.validate.workspace
  COMMAND $<TARGET_FILE:ngin_cli> project validate --project "${NGIN_WORKSPACE_PROJECT_PATH}" --target NGIN.CoreSample
)

add_test(NAME ngin.graph.workspace
  COMMAND $<TARGET_FILE:ngin_cli> project graph --project "${NGIN_WORKSPACE_PROJECT_PATH}" --target NGIN.EditorSample
)

add_test(NAME ngin.build.workspace
  COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:ngin_cli>
    -DPROJECT="${NGIN_WORKSPACE_PROJECT_PATH}"
    -DTARGET=NGIN.EditorSample
    -DOUTPUT="${CMAKE_BINARY_DIR}/test-stage/NGIN.EditorSample"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunNGINBuildCheck.cmake"
)

add_test(NAME ngin.project.autodiscovery
  COMMAND $<TARGET_FILE:ngin_cli> project validate --target Tools.Cli
)
set_tests_properties(ngin.project.autodiscovery PROPERTIES
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs/examples/project-model"
)

add_test(NAME ngin.reject.legacy.json
  COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:ngin_cli>
    -DWORK_DIR="${CMAKE_BINARY_DIR}/legacy-test"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunNGINLegacyReject.cmake"
)
set_tests_properties(ngin.reject.legacy.json PROPERTIES
  PASS_REGULAR_EXPRESSION "legacy manifest rejected"
)

if(NGIN_WORKSPACE_ENABLE_INTEGRATION)
  add_custom_target(ngin.component.status
    COMMAND ${CMAKE_COMMAND}
      -DNGIN_WORKSPACE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NGINWorkspaceStatus.cmake"
  COMMENT "Show workspace component repo status for this workspace"
  VERBATIM
  )
endif()
