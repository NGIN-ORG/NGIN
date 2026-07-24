add_executable(NGINUISDL3Tests BackendContractTests.cpp)
target_compile_features(NGINUISDL3Tests PRIVATE cxx_std_23)
target_link_libraries(NGINUISDL3Tests PRIVATE NGIN::UI::Backend::SDL3)

if(MSVC)
    target_compile_options(NGINUISDL3Tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        NGINUISDL3Tests
        PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
    )
endif()

add_test(NAME UI.SDL3Backend.Contracts COMMAND NGINUISDL3Tests)
