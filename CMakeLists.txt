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

set(NGIN_WORKSPACE_PROJECT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/manifests/workspace.project.json")
set(NGIN_WORKSPACE_REPORT_DIR "${CMAKE_BINARY_DIR}/reports")
set(NGIN_WORKSPACE_CACHE_DIR "${CMAKE_BINARY_DIR}/cache")
set(NGIN_WORKSPACE_LOCKFILE_PATH "${NGIN_WORKSPACE_REPORT_DIR}/ngin.workspace.lock.json")
set(NGIN_PACKAGE_RESTORE_REPORT_PATH "${NGIN_WORKSPACE_REPORT_DIR}/ngin.package.restore.json")
set(NGIN_VALIDATE_REPORT_PATH "${NGIN_WORKSPACE_REPORT_DIR}/ngin.validate.NGIN.CoreSample.json")
set(NGIN_GRAPH_REPORT_PATH "${NGIN_WORKSPACE_REPORT_DIR}/ngin.graph.NGIN.CoreSample.json")
set(NGIN_RESOLVE_REPORT_PATH "${NGIN_WORKSPACE_REPORT_DIR}/ngin.resolve.NGIN.CoreSample.json")

add_custom_target(ngin.manifest.show
  COMMAND ${CMAKE_COMMAND} -E cat "${CMAKE_CURRENT_SOURCE_DIR}/manifests/platform-release.json"
  COMMENT "Print the active NGIN platform release manifest"
  VERBATIM
)

add_custom_target(ngin.project.show
  COMMAND ${CMAKE_COMMAND} -E cat "${NGIN_WORKSPACE_PROJECT_PATH}"
  COMMENT "Print the canonical workspace project manifest"
  VERBATIM
)

add_custom_target(ngin.validate
  DEPENDS ngin.package.restore
  COMMAND ${Python3_EXECUTABLE}
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/ngin.py"
    validate
    --project "${NGIN_WORKSPACE_PROJECT_PATH}"
    --locked
    --lockfile "${NGIN_WORKSPACE_LOCKFILE_PATH}"
    --cache-dir "${NGIN_WORKSPACE_CACHE_DIR}"
    --target NGIN.CoreSample
  COMMENT "Validate the core sample target from the canonical workspace project"
  VERBATIM
)

add_custom_target(ngin.package.restore
  COMMAND ${CMAKE_COMMAND} -E make_directory "${NGIN_WORKSPACE_REPORT_DIR}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${NGIN_WORKSPACE_CACHE_DIR}"
  COMMAND ${Python3_EXECUTABLE}
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/ngin.py"
    package
    restore
    --project "${NGIN_WORKSPACE_PROJECT_PATH}"
    --lockfile "${NGIN_WORKSPACE_LOCKFILE_PATH}"
    --cache-dir "${NGIN_WORKSPACE_CACHE_DIR}"
  COMMENT "Restore the canonical workspace project into a lockfile and local cache"
  VERBATIM
)

add_custom_target(ngin.package.restore.report
  COMMAND ${CMAKE_COMMAND} -E make_directory "${NGIN_WORKSPACE_REPORT_DIR}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${NGIN_WORKSPACE_CACHE_DIR}"
  COMMAND ${Python3_EXECUTABLE}
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/ngin.py"
    package
    restore
    --project "${NGIN_WORKSPACE_PROJECT_PATH}"
    --lockfile "${NGIN_WORKSPACE_LOCKFILE_PATH}"
    --cache-dir "${NGIN_WORKSPACE_CACHE_DIR}"
    --json-report "${NGIN_PACKAGE_RESTORE_REPORT_PATH}"
  COMMENT "Restore the canonical workspace project and write a stable JSON report"
  VERBATIM
)

add_custom_target(ngin.validate.report
  DEPENDS ngin.package.restore.report
  COMMAND ${CMAKE_COMMAND} -E make_directory "${NGIN_WORKSPACE_REPORT_DIR}"
  COMMAND ${Python3_EXECUTABLE}
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/ngin.py"
    validate
    --project "${NGIN_WORKSPACE_PROJECT_PATH}"
    --locked
    --lockfile "${NGIN_WORKSPACE_LOCKFILE_PATH}"
    --cache-dir "${NGIN_WORKSPACE_CACHE_DIR}"
    --target NGIN.CoreSample
    --json-report "${NGIN_VALIDATE_REPORT_PATH}"
  COMMENT "Validate the core sample target and write a stable JSON report"
  VERBATIM
)

add_custom_target(ngin.graph.core
  DEPENDS ngin.package.restore
  COMMAND ${Python3_EXECUTABLE}
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/ngin.py"
    graph
    --project "${NGIN_WORKSPACE_PROJECT_PATH}"
    --locked
    --lockfile "${NGIN_WORKSPACE_LOCKFILE_PATH}"
    --cache-dir "${NGIN_WORKSPACE_CACHE_DIR}"
    --target NGIN.CoreSample
  COMMENT "Print the resolved package/module graph for the core sample target"
  VERBATIM
)

add_custom_target(ngin.graph.core.report
  DEPENDS ngin.package.restore.report
  COMMAND ${CMAKE_COMMAND} -E make_directory "${NGIN_WORKSPACE_REPORT_DIR}"
  COMMAND ${Python3_EXECUTABLE}
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/ngin.py"
    graph
    --project "${NGIN_WORKSPACE_PROJECT_PATH}"
    --locked
    --lockfile "${NGIN_WORKSPACE_LOCKFILE_PATH}"
    --cache-dir "${NGIN_WORKSPACE_CACHE_DIR}"
    --target NGIN.CoreSample
    --json-report "${NGIN_GRAPH_REPORT_PATH}"
  COMMENT "Write the resolved package/module graph for the core sample target as JSON"
  VERBATIM
)

add_custom_target(ngin.resolve.core
  DEPENDS ngin.package.restore
  COMMAND ${Python3_EXECUTABLE}
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/ngin.py"
    resolve
    --project "${NGIN_WORKSPACE_PROJECT_PATH}"
    --locked
    --lockfile "${NGIN_WORKSPACE_LOCKFILE_PATH}"
    --cache-dir "${NGIN_WORKSPACE_CACHE_DIR}"
    --target NGIN.CoreSample
  COMMENT "Resolve the baseline core-host target closure from the canonical workspace project"
  VERBATIM
)

add_custom_target(ngin.resolve.report.core
  DEPENDS ngin.package.restore.report
  COMMAND ${CMAKE_COMMAND} -E make_directory "${NGIN_WORKSPACE_REPORT_DIR}"
  COMMAND ${Python3_EXECUTABLE}
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/ngin.py"
    resolve
    --project "${NGIN_WORKSPACE_PROJECT_PATH}"
    --locked
    --lockfile "${NGIN_WORKSPACE_LOCKFILE_PATH}"
    --cache-dir "${NGIN_WORKSPACE_CACHE_DIR}"
    --target NGIN.CoreSample
    --json-report "${NGIN_RESOLVE_REPORT_PATH}"
  COMMENT "Write the baseline core-host target closure as a stable JSON report"
  VERBATIM
)

add_custom_target(ngin.reports
  DEPENDS
    ngin.package.restore.report
    ngin.validate.report
    ngin.graph.core.report
    ngin.resolve.report.core
  COMMENT "Generate the standard JSON report set for workspace validation"
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
