cmake_minimum_required(VERSION 3.20)
project(NGIN.UI VERSION 0.1.0 LANGUAGES CXX)

option(NGIN_UI_BUILD_TESTS "Build NGIN.UI tests" OFF)
option(NGIN_UI_BUILD_EXAMPLES "Build NGIN.UI examples" OFF)
option(NGIN_UI_BUILD_BENCHMARKS "Build NGIN.UI release-budget benchmarks" OFF)
option(NGIN_UI_BUILD_DOCS "Generate browsable NGIN.UI API documentation" OFF)
option(NGIN_UI_STRICT_WARNINGS "Enable extra compiler warnings" ON)
option(NGIN_UI_ENABLE_NATIVE_TEXT
       "Build the FreeType and HarfBuzz native text services" ON)
option(NGIN_UI_FETCH_THIRD_PARTY
       "Fetch pinned native dependencies instead of using installed packages" ON)
set(
    NGIN_UI_BUNDLED_FONT_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoSans/NotoSans-Variable.ttf"
    CACHE FILEPATH
    "Path to the default OFL-licensed font"
)

if(NOT TARGET NGIN::Base)
    find_package(NGINBase CONFIG QUIET)
    if(NOT NGINBase_FOUND)
        set(_ngin_ui_base_wrapper "${CMAKE_CURRENT_LIST_DIR}/../NGIN.Base")
        set(_ngin_ui_base_source "${CMAKE_CURRENT_LIST_DIR}/../../Dependencies/NGIN/NGIN.Base")
        if(EXISTS "${_ngin_ui_base_wrapper}/CMakeLists.txt")
            add_subdirectory("${_ngin_ui_base_wrapper}" "${CMAKE_CURRENT_BINARY_DIR}/_ngin_base" EXCLUDE_FROM_ALL)
        elseif(EXISTS "${_ngin_ui_base_source}/CMakeLists.txt")
            set(NGIN_BASE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
            set(NGIN_BASE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
            set(NGIN_BASE_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
            add_subdirectory("${_ngin_ui_base_source}" "${CMAKE_CURRENT_BINARY_DIR}/_ngin_base" EXCLUDE_FROM_ALL)
        else()
            message(FATAL_ERROR "NGIN.UI requires NGIN.Base")
        endif()
    endif()
endif()

set(NGIN_UI_SOURCES
    src/NGIN/UI/Application.cpp
    src/NGIN/UI/Composer.cpp
    src/NGIN/UI/Controls.cpp
    src/NGIN/UI/CustomElement.cpp
    src/NGIN/UI/DisplayList.cpp
    src/NGIN/UI/Image.cpp
    src/NGIN/UI/Input.cpp
    src/NGIN/UI/Inspector.cpp
    src/NGIN/UI/Layout.cpp
    src/NGIN/UI/NativeText.cpp
    src/NGIN/UI/RuntimeTree.cpp
    src/NGIN/UI/Semantics.cpp
    src/NGIN/UI/TextEditing.cpp
    src/NGIN/UI/UIRenderer.cpp
    src/NGIN/UI/Testing/RecordingRenderBackend.cpp
    src/NGIN/UI/Testing/SoftwareRenderBackend.cpp
    src/NGIN/UI/Testing/TestPlatformBackend.cpp
)

add_library(NGIN.UI STATIC ${NGIN_UI_SOURCES})
add_library(NGIN::UI ALIAS NGIN.UI)

target_compile_features(NGIN.UI PUBLIC cxx_std_23)
set_target_properties(NGIN.UI PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME NGINUI
    EXPORT_NAME UI
)

target_include_directories(NGIN.UI
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(NGIN.UI PUBLIC NGIN::Base)

if(NGIN_UI_ENABLE_NATIVE_TEXT)
    include(cmake/NGINUIThirdParty.cmake)
    ngin_ui_configure_native_text(NGIN.UI)
endif()

if(NGIN_UI_STRICT_WARNINGS)
    if(MSVC)
        target_compile_options(NGIN.UI PRIVATE /W4 /permissive-)
    else()
        target_compile_options(NGIN.UI PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    endif()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
    DIRECTORY assets/fonts/NotoSans/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/NGIN.UI/fonts/NotoSans
)
install(FILES THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DATADIR}/NGIN.UI)
install(
    TARGETS NGIN.UI
    EXPORT NGINUITargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(
    EXPORT NGINUITargets
    FILE NGINUITargets.cmake
    NAMESPACE NGIN::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NGINUI
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/NGINUIConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NGINUIConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/NGINUIConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/NGINUI"
)
install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/NGINUIConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/NGINUIConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/NGINUI"
)

if(NGIN_UI_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()

if(NGIN_UI_BUILD_EXAMPLES)
    message(STATUS "NGIN.UI examples are reserved for the native backend slice")
endif()

if(NGIN_UI_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(NGIN_UI_BUILD_DOCS)
    find_package(Doxygen REQUIRED)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_target(
        NGINUIApiDocsCheck
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_public_api_docs.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/NGIN/UI"
            "${CMAKE_CURRENT_SOURCE_DIR}/../NGIN.UI.Hosting/include/NGIN/UI"
            "${CMAKE_CURRENT_SOURCE_DIR}/../NGIN.UI.Backend.SDL3/include/NGIN/UI"
        COMMENT "Checking NGIN.UI public API documentation coverage"
        VERBATIM
    )
    set(DOXYGEN_PROJECT_NAME "NGIN.UI")
    set(DOXYGEN_PROJECT_NUMBER "${PROJECT_VERSION}")
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/docs")
    set(DOXYGEN_GENERATE_HTML YES)
    set(DOXYGEN_GENERATE_LATEX NO)
    set(DOXYGEN_EXTRACT_ALL YES)
    set(DOXYGEN_EXTRACT_PRIVATE NO)
    set(DOXYGEN_EXTRACT_STATIC YES)
    set(DOXYGEN_RECURSIVE YES)
    set(DOXYGEN_WARN_IF_UNDOCUMENTED NO)
    set(DOXYGEN_WARN_IF_DOC_ERROR YES)
    set(DOXYGEN_WARN_AS_ERROR FAIL_ON_WARNINGS)
    set(
        DOXYGEN_USE_MDFILE_AS_MAINPAGE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../docs/api/ngin-ui-mainpage.md"
    )
    doxygen_add_docs(
        NGINUIDocs
        "${CMAKE_CURRENT_SOURCE_DIR}/include/NGIN/UI"
        "${CMAKE_CURRENT_SOURCE_DIR}/../NGIN.UI.Hosting/include/NGIN/UI"
        "${CMAKE_CURRENT_SOURCE_DIR}/../NGIN.UI.Backend.SDL3/include/NGIN/UI"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../docs/api/ngin-ui-mainpage.md"
        COMMENT "Generating NGIN.UI browsable API reference"
    )
    add_dependencies(NGINUIDocs NGINUIApiDocsCheck)
endif()
