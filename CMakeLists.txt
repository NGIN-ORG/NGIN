cmake_minimum_required(VERSION 3.20)

project(NGIN VERSION 0.1.0 LANGUAGES CXX)

option(NGIN_WORKSPACE_ENABLE_INTEGRATION "Enable integration helper targets in the workspace." ON)
option(NGIN_WORKSPACE_EXPECT_COMPONENTS "Warn if sibling component repos are missing." OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(NGINWorkspace)

ngin_workspace_print_summary(
  EXPECT_COMPONENTS ${NGIN_WORKSPACE_EXPECT_COMPONENTS}
)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/NGIN.Base/CMakeLists.txt")
  message(FATAL_ERROR "NGIN.Base is required to build the native ngin CLI in this workspace.")
endif()

set(NGIN_BASE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NGIN_BASE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NGIN_BASE_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
add_subdirectory(NGIN.Base EXCLUDE_FROM_ALL)

add_custom_target(ngin.workspace
  COMMENT "NGIN workspace metadata target"
)

add_executable(ngin_cli
  tools/ngin.cpp
)
set_target_properties(ngin_cli PROPERTIES OUTPUT_NAME ngin)
target_link_libraries(ngin_cli PRIVATE NGIN::Base)
target_compile_features(ngin_cli PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(ngin_cli PRIVATE /W4 /permissive-)
else()
  target_compile_options(ngin_cli PRIVATE -Wall -Wextra -Wpedantic)
endif()

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
    validate
    --project "${NGIN_WORKSPACE_PROJECT_PATH}"
    --target NGIN.CoreSample
  DEPENDS ngin_cli
  COMMENT "Validate the core sample target from the canonical workspace project"
  VERBATIM
)

add_custom_target(ngin.graph.core
  COMMAND $<TARGET_FILE:ngin_cli>
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
  COMMAND $<TARGET_FILE:ngin_cli> doctor
)

add_test(NAME ngin.package.list
  COMMAND $<TARGET_FILE:ngin_cli> package list
)

add_test(NAME ngin.package.show.core
  COMMAND $<TARGET_FILE:ngin_cli> package show NGIN.Core
)

add_test(NAME ngin.validate.workspace
  COMMAND $<TARGET_FILE:ngin_cli> validate --project "${NGIN_WORKSPACE_PROJECT_PATH}" --target NGIN.CoreSample
)

add_test(NAME ngin.graph.workspace
  COMMAND $<TARGET_FILE:ngin_cli> graph --project "${NGIN_WORKSPACE_PROJECT_PATH}" --target NGIN.EditorSample
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
  COMMAND $<TARGET_FILE:ngin_cli> validate --target Tools.Cli
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
    COMMENT "Show sibling component repo status for this workspace"
    VERBATIM
  )
endif()
