add_executable(NGINUIBenchmarks Benchmark.cpp)
target_compile_features(NGINUIBenchmarks PRIVATE cxx_std_23)
target_link_libraries(NGINUIBenchmarks PRIVATE NGIN::UI)

if(MSVC)
    target_compile_options(NGINUIBenchmarks PRIVATE /W4 /permissive-)
else()
    target_compile_options(
        NGINUIBenchmarks PRIVATE -Wall -Wextra -Wpedantic
    )
endif()

if(BUILD_TESTING)
    add_test(NAME UI.Benchmarks COMMAND NGINUIBenchmarks)
    set_tests_properties(UI.Benchmarks PROPERTIES LABELS "benchmark")
endif()
