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

include(CTest)

add_subdirectory(Tools)
add_subdirectory(Packages/NGIN.Reflection.MetaGen EXCLUDE_FROM_ALL)

set(NGIN_WORKSPACE_MANIFEST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/NGIN.ngin")
set(NGIN_WORKSPACE_STAGE_DIR "${CMAKE_BINARY_DIR}/stage")
set(NGIN_HELLO_NATIVE_PROJECT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Examples/Hello.Native/Hello.Native.nginproj")
set(NGIN_HELLO_HOSTED_PROJECT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Examples/Hello.Hosted/Hello.Hosted.nginproj")
set(NGIN_HELLO_REFLECTION_PROJECT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Examples/Hello.Reflection/Hello.Reflection.nginproj")
set(NGIN_HELLO_ANALYZER_PROJECT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Examples/Hello.Analyzer/Hello.Analyzer.nginproj")
set(NGIN_HELLO_FORMATTER_PROJECT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Examples/Hello.Formatter/Hello.Formatter.nginproj")
set(NGIN_HELLO_HOSTED_BUILD_OUTPUT "${NGIN_WORKSPACE_STAGE_DIR}/Hello.Hosted/Debug")
set(NGIN_PROJECTREF_CONFIG_PROJECT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Tools/NGIN.CLI/tests/fixtures/ProjectRef.Config/Root/ProjectRef.Config.Root.nginproj")
set(NGIN_PROJECTREF_CONFIG_COLLISION_PROJECT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Tools/NGIN.CLI/tests/fixtures/ProjectRef.Config/CollisionRoot/ProjectRef.Config.CollisionRoot.nginproj")

add_custom_target(ngin.manifest.show
  COMMAND ${CMAKE_COMMAND} -E cat "${NGIN_WORKSPACE_MANIFEST_PATH}"
  COMMENT "Print the active NGIN workspace manifest"
  VERBATIM
)

add_custom_target(ngin.project.show
  COMMAND ${CMAKE_COMMAND} -E cat "${NGIN_HELLO_HOSTED_PROJECT_PATH}"
  COMMENT "Print the canonical Hello.Hosted project manifest"
  VERBATIM
)

add_custom_target(ngin.validate
  COMMAND $<TARGET_FILE:ngin_cli>
    validate
    --project "${NGIN_HELLO_HOSTED_PROJECT_PATH}"
    --profile Debug
  DEPENDS ngin_cli
  COMMENT "Validate the Hello.Hosted Debug configuration"
  VERBATIM
)

add_custom_target(ngin.graph.core
  COMMAND $<TARGET_FILE:ngin_cli>
    graph
    --project "${NGIN_HELLO_HOSTED_PROJECT_PATH}"
    --profile Debug
  DEPENDS ngin_cli
  COMMENT "Print the resolved package and module graph for Hello.Hosted"
  VERBATIM
)

add_custom_target(ngin.build.core
  COMMAND ${CMAKE_COMMAND} -E make_directory "${NGIN_WORKSPACE_STAGE_DIR}"
  COMMAND $<TARGET_FILE:ngin_cli>
    build
    --project "${NGIN_HELLO_HOSTED_PROJECT_PATH}"
    --profile Debug
    --output "${NGIN_HELLO_HOSTED_BUILD_OUTPUT}"
  DEPENDS ngin_cli
  COMMENT "Build the staged layout for Hello.Hosted"
  VERBATIM
)

add_custom_target(ngin.workflow
  DEPENDS
    ngin.validate
    ngin.graph.core
    ngin.build.core
  COMMENT "Run the standard workspace validate, graph, and build flow"
)

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
  COMMAND $<TARGET_FILE:ngin_cli> validate --project "${NGIN_HELLO_HOSTED_PROJECT_PATH}" --profile Debug
)

add_test(NAME ngin.validate.hello_native
  COMMAND $<TARGET_FILE:ngin_cli> validate --project "${NGIN_HELLO_NATIVE_PROJECT_PATH}" --profile Debug
)

add_test(NAME ngin.validate.hello_hosted
  COMMAND $<TARGET_FILE:ngin_cli> validate --project "${NGIN_HELLO_HOSTED_PROJECT_PATH}" --profile Debug
)

add_test(NAME ngin.validate.hello_reflection
  COMMAND $<TARGET_FILE:ngin_cli> validate --project "${NGIN_HELLO_REFLECTION_PROJECT_PATH}" --profile Debug
)

add_test(NAME ngin.validate.hello_analyzer
  COMMAND $<TARGET_FILE:ngin_cli> validate --project "${NGIN_HELLO_ANALYZER_PROJECT_PATH}" --profile Debug.Analyzer
)

add_test(NAME ngin.graph.hello_analyzer_tooling
  COMMAND $<TARGET_FILE:ngin_cli> graph --project "${NGIN_HELLO_ANALYZER_PROJECT_PATH}" --profile Debug.Analyzer --tooling-plan
)

add_test(NAME ngin.validate.hello_formatter
  COMMAND $<TARGET_FILE:ngin_cli> validate --project "${NGIN_HELLO_FORMATTER_PROJECT_PATH}" --profile Debug.Formatter
)

add_test(NAME ngin.graph.hello_formatter_tooling
  COMMAND $<TARGET_FILE:ngin_cli> graph --project "${NGIN_HELLO_FORMATTER_PROJECT_PATH}" --profile Debug.Formatter --tooling-plan
)

add_test(NAME ngin.graph.workspace
  COMMAND $<TARGET_FILE:ngin_cli> graph --project "${NGIN_HELLO_HOSTED_PROJECT_PATH}" --profile Debug
)

add_test(NAME ngin.configure.hello_native
  COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:ngin_cli>
    -DPROJECT=${NGIN_HELLO_NATIVE_PROJECT_PATH}
    -DPROFILE=Debug
    -DOUTPUT=${CMAKE_BINARY_DIR}/test-stage/Hello.Native/Configure
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunNGINConfigureCheck.cmake"
)

add_test(NAME ngin.build.workspace
  COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:ngin_cli>
    -DPROJECT=${NGIN_HELLO_HOSTED_PROJECT_PATH}
    -DPROFILE=Debug
    -DOUTPUT=${CMAKE_BINARY_DIR}/test-stage/Hello.Hosted/Debug
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunNGINBuildCheck.cmake"
)

add_test(NAME ngin.projectref.config.build
  COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:ngin_cli>
    -DPROJECT=${NGIN_PROJECTREF_CONFIG_PROJECT_PATH}
    -DPROFILE=Runtime
    -DOUTPUT=${CMAKE_BINARY_DIR}/test-stage/ProjectRef.Config/Runtime
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunNGINProjectRefConfigCheck.cmake"
)

add_test(NAME ngin.projectref.config.collision
  COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:ngin_cli>
    -DPROJECT=${NGIN_PROJECTREF_CONFIG_COLLISION_PROJECT_PATH}
    -DPROFILE=Runtime
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunNGINProjectRefConfigCollisionCheck.cmake"
)

add_test(NAME ngin.project.autodiscovery
  COMMAND $<TARGET_FILE:ngin_cli> validate
)
set_tests_properties(ngin.project.autodiscovery PROPERTIES
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Examples/Hello.Hosted"
)

add_test(NAME ngin.build.output.preserve
  COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:ngin_cli>
    -DPROJECT=${NGIN_HELLO_HOSTED_PROJECT_PATH}
    -DPROFILE=Debug
    -DOUTPUT=${CMAKE_BINARY_DIR}/test-stage/Hello.Hosted/Preserve
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunNGINOutputPreserveCheck.cmake"
)

add_test(NAME ngin.reject.legacy.json
  COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:ngin_cli>
    -DWORK_DIR=${CMAKE_BINARY_DIR}/legacy-test
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
