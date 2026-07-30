cmake_minimum_required(VERSION 3.20)
project(NGIN.UI.Accessibility.Windows VERSION 0.1.0 LANGUAGES CXX)

option(NGIN_UI_ACCESSIBILITY_WINDOWS_STRICT_WARNINGS
       "Enable extra compiler warnings" ON)
option(NGIN_UI_ACCESSIBILITY_WINDOWS_BUILD_TESTS
       "Build Windows UI Automation provider tests" OFF)

if(NOT TARGET NGIN::UI)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../NGIN.UI"
        "${CMAKE_CURRENT_BINARY_DIR}/_ngin_ui"
        EXCLUDE_FROM_ALL
    )
endif()

add_library(NGIN.UI.Accessibility.Windows STATIC)
add_library(NGIN::UI::Accessibility::Windows
            ALIAS NGIN.UI.Accessibility.Windows)
target_sources(NGIN.UI.Accessibility.Windows PRIVATE
    $<$<PLATFORM_ID:Windows>:src/WindowsAccessibilityBackend.cpp>
    $<$<NOT:$<PLATFORM_ID:Windows>>:src/UnsupportedAccessibilityBackend.cpp>
)
target_compile_features(NGIN.UI.Accessibility.Windows PUBLIC cxx_std_23)
set_target_properties(NGIN.UI.Accessibility.Windows PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME NGINUIAccessibilityWindows
    EXPORT_NAME UI.Accessibility.Windows
)
target_include_directories(NGIN.UI.Accessibility.Windows
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(NGIN.UI.Accessibility.Windows PUBLIC NGIN::UI)
if(WIN32)
    target_link_libraries(NGIN.UI.Accessibility.Windows
        PRIVATE Uiautomationcore OleAut32 Ole32 Comctl32)
endif()

if(NGIN_UI_ACCESSIBILITY_WINDOWS_STRICT_WARNINGS)
    if(MSVC)
        target_compile_options(NGIN.UI.Accessibility.Windows
            PRIVATE /W4 /permissive-)
    else()
        target_compile_options(NGIN.UI.Accessibility.Windows
            PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
    TARGETS NGIN.UI.Accessibility.Windows
    EXPORT NGINUIAccessibilityWindowsTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(WIN32 AND NGIN_UI_ACCESSIBILITY_WINDOWS_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_executable(NGINUIAccessibilityWindowsTests tests/ProviderTests.cpp)
    target_compile_features(NGINUIAccessibilityWindowsTests PRIVATE cxx_std_23)
    target_link_libraries(NGINUIAccessibilityWindowsTests
        PRIVATE NGIN::UI::Accessibility::Windows Uiautomationcore
                Ole32 OleAut32)
    add_test(NAME NGIN.UI.Accessibility.Windows.Provider
             COMMAND NGINUIAccessibilityWindowsTests)
endif()
