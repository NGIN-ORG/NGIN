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
        CHECK(result.value->projects.front().project.name == "Hello");
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
        CHECK(result.value->projects.front().project.name == "Hello");
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
}
