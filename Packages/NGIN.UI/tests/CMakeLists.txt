set(_ngin_ui_have_catch2 OFF)

if(TARGET Catch2::Catch2WithMain)
    set(_ngin_ui_have_catch2 ON)
else()
    find_package(Catch2 3 CONFIG QUIET)
    if(Catch2_FOUND AND TARGET Catch2::Catch2WithMain)
        set(_ngin_ui_have_catch2 ON)
    endif()
endif()

if(NOT _ngin_ui_have_catch2)
    set(_ngin_ui_catch2_candidates
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/dev/_deps/catch2-src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/ngin-core-ci/_deps/catch2-src"
    )
    foreach(_candidate IN LISTS _ngin_ui_catch2_candidates)
        if(EXISTS "${_candidate}/CMakeLists.txt")
            add_subdirectory("${_candidate}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/catch2" EXCLUDE_FROM_ALL)
            if(TARGET Catch2::Catch2WithMain)
                set(_ngin_ui_have_catch2 ON)
                list(APPEND CMAKE_MODULE_PATH "${_candidate}/extras")
                break()
            endif()
        endif()
    endforeach()
endif()

if(NOT _ngin_ui_have_catch2)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.3
    )
    FetchContent_MakeAvailable(Catch2)
endif()

include(Catch)

add_executable(NGINUITests
    ApplicationTests.cpp
    CollectionTests.cpp
    CompositionTests.cpp
    ContractTests.cpp
    ControlsTests.cpp
    CustomElementTests.cpp
    FoundationTests.cpp
    InputTests.cpp
    LayoutTests.cpp
    StateTests.cpp
    StyleTests.cpp
    TextTests.cpp
)
if(NGIN_UI_ENABLE_NATIVE_TEXT)
    target_sources(NGINUITests PRIVATE NativeTextTests.cpp)
endif()
target_compile_features(NGINUITests PRIVATE cxx_std_23)
target_link_libraries(NGINUITests PRIVATE Catch2::Catch2WithMain NGIN::UI)

if(MSVC)
    target_compile_options(NGINUITests PRIVATE /W4 /permissive-)
else()
    target_compile_options(NGINUITests PRIVATE -Wall -Wextra -Wpedantic)
endif()

catch_discover_tests(NGINUITests TEST_PREFIX "UI.")
