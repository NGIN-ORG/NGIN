cmake_minimum_required(VERSION 3.20)
project(NGIN.UI VERSION 0.1.0 LANGUAGES CXX)

option(NGIN_UI_BUILD_TESTS "Build NGIN.UI tests" OFF)
option(NGIN_UI_BUILD_EXAMPLES "Build NGIN.UI examples" OFF)
option(NGIN_UI_STRICT_WARNINGS "Enable extra compiler warnings" ON)

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
    src/NGIN/UI/DisplayList.cpp
    src/NGIN/UI/Input.cpp
    src/NGIN/UI/Inspector.cpp
    src/NGIN/UI/Layout.cpp
    src/NGIN/UI/RuntimeTree.cpp
    src/NGIN/UI/Semantics.cpp
    src/NGIN/UI/TextEditing.cpp
    src/NGIN/UI/UIRenderer.cpp
    src/NGIN/UI/Testing/RecordingRenderBackend.cpp
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

if(NGIN_UI_STRICT_WARNINGS)
    if(MSVC)
        target_compile_options(NGIN.UI PRIVATE /W4 /permissive-)
    else()
        target_compile_options(NGIN.UI PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    endif()
endif()

include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
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

if(NGIN_UI_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()

if(NGIN_UI_BUILD_EXAMPLES)
    message(STATUS "NGIN.UI examples are reserved for the native backend slice")
endif()
