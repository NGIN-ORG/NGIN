cmake_minimum_required(VERSION 3.20)
project(NGIN.UI.Hosting VERSION 0.1.0 LANGUAGES CXX)

option(NGIN_UI_HOSTING_BUILD_TESTS "Build NGIN.UI hosting tests" OFF)
option(NGIN_UI_HOSTING_STRICT_WARNINGS "Enable extra compiler warnings" ON)

if(NOT TARGET NGIN::Core)
    set(NGIN_CORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(NGIN_CORE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(NGIN_CORE_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../NGIN.Core"
        "${CMAKE_CURRENT_BINARY_DIR}/_ngin_core"
        EXCLUDE_FROM_ALL
    )
endif()
if(NOT TARGET NGIN::UI)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../NGIN.UI"
        "${CMAKE_CURRENT_BINARY_DIR}/_ngin_ui"
        EXCLUDE_FROM_ALL
    )
endif()

add_library(NGIN.UI.Hosting STATIC src/Hosting.cpp)
add_library(NGIN::UI::Hosting ALIAS NGIN.UI.Hosting)
target_compile_features(NGIN.UI.Hosting PUBLIC cxx_std_23)
set_target_properties(NGIN.UI.Hosting PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME NGINUIHosting
    EXPORT_NAME UI.Hosting
)
target_include_directories(NGIN.UI.Hosting
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(NGIN.UI.Hosting PUBLIC NGIN::Core NGIN::UI)

if(NGIN_UI_HOSTING_STRICT_WARNINGS)
    if(MSVC)
        target_compile_options(NGIN.UI.Hosting PRIVATE /W4 /permissive-)
    else()
        target_compile_options(
            NGIN.UI.Hosting
            PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
        )
    endif()
endif()

include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
    TARGETS NGIN.UI.Hosting
    EXPORT NGINUIHostingTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(NGIN_UI_HOSTING_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()
