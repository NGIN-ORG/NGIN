cmake_minimum_required(VERSION 3.20)
project(NGIN.UI.Backend.SDL3 VERSION 0.4.0 LANGUAGES CXX)

option(NGIN_UI_SDL3_STRICT_WARNINGS "Enable extra compiler warnings" ON)
option(NGIN_UI_SDL3_BUILD_TESTS "Build NGIN.UI SDL3 backend tests" OFF)

if(NOT TARGET NGIN::UI)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../NGIN.UI"
        "${CMAKE_CURRENT_BINARY_DIR}/_ngin_ui"
        EXCLUDE_FROM_ALL
    )
endif()
if(NOT TARGET SDL3::SDL3-static)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../SDL3"
        "${CMAKE_CURRENT_BINARY_DIR}/_sdl3"
        EXCLUDE_FROM_ALL
    )
endif()

add_library(NGIN.UI.Backend.SDL3 STATIC
    src/SDLPlatformBackend.cpp
    src/SDLGpuRenderBackend.cpp
)
add_library(NGIN::UI::Backend::SDL3 ALIAS NGIN.UI.Backend.SDL3)
target_compile_features(NGIN.UI.Backend.SDL3 PUBLIC cxx_std_23)
set_target_properties(NGIN.UI.Backend.SDL3 PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME NGINUISDL3
    EXPORT_NAME UI.Backend.SDL3
)
target_include_directories(NGIN.UI.Backend.SDL3
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(NGIN.UI.Backend.SDL3
    PUBLIC NGIN::UI
    PRIVATE SDL3::SDL3-static
)
if(WIN32)
    target_link_libraries(NGIN.UI.Backend.SDL3 PRIVATE user32)
endif()

if(NGIN_UI_SDL3_STRICT_WARNINGS)
    if(MSVC)
        target_compile_options(NGIN.UI.Backend.SDL3 PRIVATE /W4 /permissive-)
    else()
        target_compile_options(NGIN.UI.Backend.SDL3 PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    endif()
endif()

include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
    TARGETS NGIN.UI.Backend.SDL3
    EXPORT NGINUISDL3Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(
    FILES THIRD_PARTY_NOTICES.md SDL3-LICENSE.txt
    DESTINATION ${CMAKE_INSTALL_DATADIR}/NGIN.UI.Backend.SDL3
)

if(NGIN_UI_SDL3_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()
