add_executable(NGINUIHostingTests HostingTests.cpp)
target_compile_features(NGINUIHostingTests PRIVATE cxx_std_23)
target_link_libraries(NGINUIHostingTests PRIVATE NGIN::UI::Hosting)

if(MSVC)
    target_compile_options(NGINUIHostingTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        NGINUIHostingTests
        PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
    )
endif()

add_test(NAME UI.Hosting.Lifecycle COMMAND NGINUIHostingTests)
