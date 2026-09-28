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
    AccessibilityTests.cpp
    ApplicationTests.cpp
    CollectionTests.cpp
    CommandTests.cpp
    CompositionTests.cpp
    ContractTests.cpp
    ControlsTests.cpp
    CustomElementTests.cpp
    FoundationTests.cpp
    ImageTests.cpp
    InputTests.cpp
    LayoutTests.cpp
    MotionTests.cpp
    NavigationTests.cpp
    SoftwareRendererTests.cpp
    StateTests.cpp
    StyleTests.cpp
    TextTests.cpp
    ValidationTests.cpp
    ViewModelTests.cpp
)
if(NGIN_UI_ENABLE_NATIVE_TEXT)
    target_sources(NGINUITests PRIVATE NativeTextTests.cpp)
endif()
target_compile_features(NGINUITests PRIVATE cxx_std_23)
target_link_libraries(NGINUITests PRIVATE Catch2::Catch2WithMain NGIN::UI)
target_compile_definitions(
    NGINUITests
    PRIVATE NGIN_UI_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(NGINUITests PRIVATE /W4 /permissive-)
else()
    target_compile_options(NGINUITests PRIVATE -Wall -Wextra -Wpedantic)
endif()

catch_discover_tests(NGINUITests TEST_PREFIX "UI.")

# The event-loop executor contract depends only on Execution. Keep it independently
# runnable from rendering/platform providers and their integration tests.
add_executable(NGINUIAsyncTests UIAsyncSchedulerTests.cpp)
target_compile_features(NGINUIAsyncTests PRIVATE cxx_std_23)
target_link_libraries(NGINUIAsyncTests PRIVATE Catch2::Catch2WithMain NGIN::Base::Execution)
if(MSVC)
    target_compile_options(NGINUIAsyncTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(NGINUIAsyncTests PRIVATE -Wall -Wextra -Wpedantic)
endif()
catch_discover_tests(NGINUIAsyncTests TEST_PREFIX "UI.Async.")

# Ownership and observable state have no rendering/platform dependency.
add_executable(NGINUIOwnershipTests
    UIOwnershipTests.cpp ValidationTests.cpp
    ../src/NGIN/UI/Command.cpp ../src/NGIN/UI/ViewModel.cpp ../src/NGIN/UI/State.cpp)
target_compile_features(NGINUIOwnershipTests PRIVATE cxx_std_23)
target_include_directories(NGINUIOwnershipTests PRIVATE ../include)
target_link_libraries(NGINUIOwnershipTests PRIVATE Catch2::Catch2WithMain NGIN::Base::Execution)
if(MSVC)
    target_compile_options(NGINUIOwnershipTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(NGINUIOwnershipTests PRIVATE -Wall -Wextra -Wpedantic)
endif()
catch_discover_tests(NGINUIOwnershipTests TEST_PREFIX "UI.Ownership.")
