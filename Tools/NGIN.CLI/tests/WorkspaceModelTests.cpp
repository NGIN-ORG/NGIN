#include "TestSupport.hpp"

#include "AuthoredManifest.hpp"
#include "WorkspaceModel.hpp"

#include <catch2/catch_test_macros.hpp>

namespace NGIN::CLI::Tests
{
    namespace
    {
        [[nodiscard]] auto DiagnosticsText(const std::vector<ManifestDiagnostic> &diagnostics) -> std::string
        {
            std::string result{};
            for (const auto &diagnostic : diagnostics) result += diagnostic.message + "\n";
            return result;
        }

        [[nodiscard]] auto ParseWorkspaceFile(const std::filesystem::path &path) -> SemanticWorkspaceResult
        {
            const auto authored = ParseAuthoredManifest(path);
            REQUIRE(authored.Succeeded());
            REQUIRE(std::holds_alternative<AuthoredWorkspaceManifest>(*authored.value));
            return ParseSemanticWorkspace(std::get<AuthoredWorkspaceManifest>(*authored.value));
        }
    }

    TEST_CASE("Workspace model discovers projects and resolves typed policy", "[workspace-model]")
    {
        TempDir temp{};
        WriteFile(temp.path() / "apps/Hello.nginproj", R"xml(<Executable Name="Hello">
  <Uses><Package Name="fmt" Version="11" /></Uses>
</Executable>)xml");
        WriteFile(temp.path() / "packages/fmt.nginpkg", R"xml(<Package Name="fmt" Version="11.0.2">
  <Library Name="fmt" />
</Package>)xml");
        const auto workspacePath = temp.path() / "NGIN.ngin";
        WriteFile(workspacePath, R"xml(<Workspace Name="Example">
  <Discover>
    <Projects Include="apps/**/*.nginproj" />
    <Packages Include="packages/**/*.nginpkg" />
  </Discover>
  <Configurations><Configuration Name="Debug" /></Configurations>
  <Targets><Target Name="windows-x64" OS="windows" Architecture="x64"><Alias Name="desktop" /></Target></Targets>
  <Toolchains><Toolchain Name="msvc" Compiler="msvc" /></Toolchains>
  <Versions><Package Name="fmt" AtLeast="11.0.0" Before="12.0.0" /></Versions>
  <Profiles Default="desktop"><Profile Name="desktop" Configuration="Debug" Target="desktop" Toolchain="msvc" /></Profiles>
</Workspace>)xml");

        const auto result = ParseWorkspaceFile(workspacePath);
        INFO(DiagnosticsText(result.diagnostics));
        REQUIRE(result.Succeeded());
        REQUIRE(result.value->projects.size() == 1);
        REQUIRE(result.value->projects.front().project.has_value());
        CHECK(result.value->projects.front().project->name == "Hello");
        CHECK(result.value->projects.front().system == ProjectSystem::Ngin);
        CHECK(result.value->centralVersions.contains("fmt"));
        CHECK(result.value->localPackages.at("fmt").coordinate.exactVersion == "11.0.2");
        CHECK(result.value->selection.defaults.target == "desktop");
        REQUIRE(result.value->outputRoot.has_value());
        CHECK(result.value->outputRoot->value == ".ngin/build");
    }

    TEST_CASE("Workspace project discovery prunes generated and version-control directories", "[workspace-model]")
    {
        TempDir temp{};
        WriteFile(temp.path() / "apps/Hello.nginproj", "<Executable Name=\"Hello\" />");
        WriteFile(temp.path() / "apps/.ngin/build/Generated.nginproj", "not XML");
        WriteFile(temp.path() / "apps/build/Generated.nginproj", "not XML");
        WriteFile(temp.path() / "apps/.git/Generated.nginproj", "not XML");
        const auto workspacePath = temp.path() / "NGIN.ngin";
        WriteFile(workspacePath, R"xml(<Workspace Name="Example">
  <Discover><Projects Include="apps/**/*.nginproj" /></Discover>
</Workspace>)xml");

        const auto result = ParseWorkspaceFile(workspacePath);
        INFO(DiagnosticsText(result.diagnostics));
        REQUIRE(result.Succeeded());
        REQUIRE(result.value->projects.size() == 1);
        REQUIRE(result.value->projects.front().project.has_value());
        CHECK(result.value->projects.front().project->name == "Hello");
    }

    TEST_CASE("Workspace model deduplicates overlapping discovery", "[workspace-model]")
    {
        TempDir temp{};
        WriteFile(temp.path() / "apps/Hello.nginproj", "<Executable Name=\"Hello\" />");
        const auto workspacePath = temp.path() / "NGIN.ngin";
        WriteFile(workspacePath, R"xml(<Workspace Name="Invalid">
  <Discover>
    <Projects Include="apps/Hello.nginproj" />
    <Projects Include="apps/**/*.nginproj" />
  </Discover>
</Workspace>)xml");

        const auto result = ParseWorkspaceFile(workspacePath);
        REQUIRE(result.Succeeded());
        CHECK(result.value->projects.size() == 1);
        const auto text = DiagnosticsText(result.diagnostics);
        CHECK(text.find("deduplicated") != std::string::npos);
    }

    TEST_CASE("Workspace model warns about unused central versions", "[workspace-model]")
    {
        TempDir temp{};
        WriteFile(temp.path() / "Hello.nginproj", "<Executable Name=\"Hello\" />");
        const auto workspacePath = temp.path() / "NGIN.ngin";
        WriteFile(workspacePath, R"xml(<Workspace Name="Invalid">
  <Discover><Projects Include="Hello.nginproj" /></Discover>
  <Versions><Package Name="fmt" Exact="11.0.2" /></Versions>
</Workspace>)xml");

        const auto result = ParseWorkspaceFile(workspacePath);
        CHECK(result.Succeeded());
        CHECK(DiagnosticsText(result.diagnostics).find("central Version 'fmt' is unused") != std::string::npos);
    }

    TEST_CASE("Workspace model discovers explicit CMake projects", "[workspace-model]")
    {
        TempDir temp{};
        WriteFile(temp.path() / "libraries/Math/CMakeLists.txt",
                  "cmake_minimum_required(VERSION 3.20)\nproject(Math)\nadd_library(Math STATIC math.cpp)\n");
        WriteFile(temp.path() / "libraries/Math/math.cpp", "int math_value() { return 42; }\n");
        const auto workspacePath = temp.path() / "NGIN.ngin";
        WriteFile(workspacePath, R"xml(<Workspace Name="Mixed">
  <Discover><Projects Include="libraries/Math" System="CMake" /></Discover>
</Workspace>)xml");

        const auto result = ParseWorkspaceFile(workspacePath);
        INFO(DiagnosticsText(result.diagnostics));
        REQUIRE(result.Succeeded());
        REQUIRE(result.value->projects.size() == 1);
        const auto &project = result.value->projects.front();
        CHECK(project.name == "Math");
        CHECK(project.system == ProjectSystem::CMake);
        CHECK(project.root == fs::weakly_canonical(temp.path() / "libraries/Math"));
        CHECK_FALSE(project.project.has_value());
        CHECK(project.capabilities.contains("Configure"));
        CHECK(project.capabilities.contains("BuildTarget"));
        CHECK_FALSE(project.capabilities.contains("Run"));
    }

    TEST_CASE("Workspace model infers CMake projects and rejects recursive CMake globs", "[workspace-model]")
    {
        TempDir temp{};
        WriteFile(temp.path() / "Library/CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\nproject(Library)\n");
        WriteFile(temp.path() / "NGIN.ngin", R"xml(<Workspace Name="Mixed">
  <Discover><Projects Include="Library" /></Discover>
</Workspace>)xml");
        const auto inferred = ParseWorkspaceFile(temp.path() / "NGIN.ngin");
        INFO(DiagnosticsText(inferred.diagnostics));
        REQUIRE(inferred.Succeeded());
        CHECK(inferred.value->projects.front().system == ProjectSystem::CMake);

        WriteFile(temp.path() / "NGIN.ngin", R"xml(<Workspace Name="Mixed">
  <Discover><Projects Include="**" System="CMake" /></Discover>
</Workspace>)xml");
        const auto globbed = ParseWorkspaceFile(temp.path() / "NGIN.ngin");
        CHECK_FALSE(globbed.Succeeded());
        CHECK(DiagnosticsText(globbed.diagnostics).find("exact directory path") != std::string::npos);
    }

    TEST_CASE("Workspace project identities are scoped to the authored workspace", "[workspace-model]")
    {
        TempDir temp{};
        WriteFile(temp.path() / "Library/CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\nproject(Library)\n");
        WriteFile(temp.path() / "One.ngin", R"xml(<Workspace Name="One">
  <Discover><Projects Include="Library" System="CMake" /></Discover>
</Workspace>)xml");
        WriteFile(temp.path() / "Two.ngin", R"xml(<Workspace Name="Two">
  <Discover><Projects Include="Library" System="CMake" /></Discover>
</Workspace>)xml");
        const auto one = ParseWorkspaceFile(temp.path() / "One.ngin");
        const auto two = ParseWorkspaceFile(temp.path() / "Two.ngin");
        REQUIRE(one.Succeeded());
        REQUIRE(two.Succeeded());
        CHECK(one.value->projects.front().name == two.value->projects.front().name);
        CHECK(one.value->projects.front().id != two.value->projects.front().id);
    }

    TEST_CASE("Workspace package development metadata is non-semantic navigation state", "[workspace-model]")
    {
        TempDir temp{};
        WriteFile(temp.path() / "packages/Math/Math.nginpkg", R"xml(<Package Name="Math" Version="1.0.0">
  <Development Project="../../libraries/Math" />
  <Library Name="Math" />
</Package>)xml");
        WriteFile(temp.path() / "NGIN.ngin", R"xml(<Workspace Name="Mixed">
  <Discover><Packages Include="packages/**/*.nginpkg" /></Discover>
</Workspace>)xml");

        const auto result = ParseWorkspaceFile(temp.path() / "NGIN.ngin");
        INFO(DiagnosticsText(result.diagnostics));
        REQUIRE(result.Succeeded());
        REQUIRE(result.value->localPackages.at("Math").developmentProject.has_value());
        CHECK(result.value->localPackages.at("Math").developmentProject->value == "../../libraries/Math");
    }
}
