#include "TestSupport.hpp"

#include "CMakeProjectSystem.hpp"

#include <catch2/catch_test_macros.hpp>

namespace NGIN::CLI::Tests
{
    TEST_CASE("CMake project system configures inspects builds and tests an explicit project", "[cmake-project]")
    {
        TempDir temp{};
        WriteFile(temp.path() / "CMakeLists.txt", R"cmake(cmake_minimum_required(VERSION 3.20)
project(Math LANGUAGES CXX)
add_library(Math STATIC math.cpp)
add_library(MathMirror STATIC math.cpp)
add_executable(MathTests test.cpp)
target_link_libraries(MathTests PRIVATE Math)
enable_testing()
add_test(NAME Math.addition COMMAND MathTests)
)cmake");
        WriteFile(temp.path() / "math.cpp", "int add(int left, int right) { return left + right; }\n");
        WriteFile(temp.path() / "test.cpp",
                  "int add(int, int); int main() { return add(20, 22) == 42 ? 0 : 1; }\n");
        WriteFile(temp.path() / "CMakePresets.json", R"json({
  "version": 3,
  "configurePresets": [
    {
      "name": "base",
      "hidden": true,
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/out/${presetName}"
    },
    {
      "name": "tests",
      "displayName": "Tests",
      "description": "Configure focused tests",
      "inherits": "base",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug", "PRIVATE_TOKEN": "must-not-leak" }
    },
    {
      "name": "multi",
      "generator": "Ninja Multi-Config",
      "binaryDir": "${sourceDir}/out/${presetName}"
    }
  ],
  "buildPresets": [
    { "name": "tests-build", "configurePreset": "tests", "targets": ["MathTests"] }
  ],
  "testPresets": [
    { "name": "tests-run", "configurePreset": "tests", "output": { "outputOnFailure": true } }
  ]
})json");

        const CMakeOperationRequest request{.projectRoot = temp.path(),
                                            .configurePreset = "tests",
                                            .configuration = "Debug"};
        const auto before = InspectCMakeProject(request);
        CHECK_FALSE(before.configured);
        REQUIRE(before.configurePresets.size() == 2);
        const auto testsPreset = std::ranges::find(before.configurePresets, "tests", &CMakePreset::name);
        REQUIRE(testsPreset != before.configurePresets.end());
        CHECK(testsPreset->description == "Configure focused tests");
        REQUIRE(before.buildPresets.size() == 1);
        CHECK(before.buildPresets.front().configurePreset == "tests");
        REQUIRE(before.testPresets.size() == 1);

        REQUIRE(ConfigureCMakeProject(request) == 0);
        const auto snapshot = InspectCMakeProject(request);
        INFO(SerializeCMakeProjectSnapshot(snapshot));
        REQUIRE(snapshot.configured);
        CHECK(snapshot.buildDirectory == temp.path() / "out/tests");
        const auto library = std::ranges::find(snapshot.targets, "Math", &CMakeTarget::name);
        REQUIRE(library != snapshot.targets.end());
        CHECK(library->type == "STATIC_LIBRARY");
        CHECK(std::ranges::any_of(library->sources,
                                  [&](const CMakeSource &source) { return source.path == (temp.path() / "math.cpp"); }));
        CHECK(library->declaration.has_value());
        CHECK(library->declaration->ends_with("CMakeLists.txt"));
        REQUIRE(library->compileGroups.size() == 1);
        CHECK(library->compileGroups.front().language == "CXX");
        const auto owners = std::ranges::count_if(snapshot.targets, [&](const CMakeTarget &target) {
            return std::ranges::any_of(target.sources, [&](const CMakeSource &source) {
                return source.path == (temp.path() / "math.cpp");
            });
        });
        CHECK(owners == 2);

        const auto serialized = SerializeCMakeProjectSnapshot(snapshot);
        CHECK(serialized.find("must-not-leak") == std::string::npos);
        CHECK(serialized.find(R"("projectSystem":"CMake")") != std::string::npos);
        CHECK(serialized.find(R"("BuildTarget")") != std::string::npos);

        REQUIRE(BuildCMakeProject(CMakeOperationRequest{.projectRoot = temp.path(),
                                                        .configurePreset = "tests",
                                                        .configuration = "Debug",
                                                        .target = std::ranges::find(snapshot.targets, "MathTests", &CMakeTarget::name)->id}) == 0);
        REQUIRE(TestCMakeProject(CMakeOperationRequest{.projectRoot = temp.path(),
                                                       .configurePreset = "tests",
                                                       .configuration = "Debug",
                                                       .tests = {"Math.addition"}}) == 0);

        const CMakeOperationRequest multi{.projectRoot = temp.path(),
                                          .configurePreset = "multi",
                                          .configuration = "Release"};
        REQUIRE(ConfigureCMakeProject(multi) == 0);
        const auto multiSnapshot = InspectCMakeProject(multi);
        CHECK(multiSnapshot.multiConfig);
        CHECK(std::ranges::find(multiSnapshot.configurations, "Release") != multiSnapshot.configurations.end());

        WriteFile(temp.path() / "CMakeLists.txt", "this is not valid CMake syntax (\n");
        auto changed = fs::last_write_time(temp.path() / "CMakeLists.txt");
        fs::last_write_time(temp.path() / "CMakeLists.txt", changed + std::chrono::seconds(10));
        CHECK(ConfigureCMakeProject(request) != 0);
        const auto retained = InspectCMakeProject(request);
        CHECK(retained.configured);
        CHECK(retained.stale);
    }

    TEST_CASE("CMake project system requires an authored configure preset", "[cmake-project]")
    {
        TempDir temp{};
        WriteFile(temp.path() / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\nproject(NoPreset)\n");
        const auto snapshot = InspectCMakeProject(CMakeOperationRequest{.projectRoot = temp.path()});
        CHECK_FALSE(snapshot.configured);
        CHECK(snapshot.configurePresets.empty());
        CHECK_THROWS_WITH(ConfigureCMakeProject(CMakeOperationRequest{.projectRoot = temp.path()}),
                          "CMake configure requires --configure-preset");
    }
}
