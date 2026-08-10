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
        WriteFile(temp.path() / "apps/Hello.nginproj", R"xml(<Project Name="Hello" Type="Application">
  <Dependencies><Package Name="fmt" Compatible="11" /></Dependencies>
</Project>)xml");
        WriteFile(temp.path() / "packages/fmt.nginpkg", R"xml(<Package Name="fmt" Version="11.0.2">
  <Exports><Library Name="fmt" Default="true" /></Exports>
</Package>)xml");
        const auto workspacePath = temp.path() / "NGIN.ngin";
        WriteFile(workspacePath, R"xml(<Workspace Name="Example">
  <Projects><Project Include="apps/**/*.nginproj" /></Projects>
  <Configurations><Configuration Name="Debug" /></Configurations>
  <Targets><Target Name="windows-x64" OS="windows" Architecture="x64"><Alias Name="desktop" /></Target></Targets>
  <Toolchains><Toolchain Name="msvc" Compiler="msvc" /></Toolchains>
  <Defaults><OutputRoot Path="build" /><Configuration Name="Debug" /><Target Name="desktop" /><Toolchain Name="msvc" /></Defaults>
  <Packages>
    <Source Name="local" Kind="Directory" Path="packages" />
    <LocalPackage Name="fmt" Manifest="packages/fmt.nginpkg" Root="packages" />
    <Version Name="fmt" AtLeast="11.0.0" Before="12.0.0" />
    <Binding Package="fmt" Source="local" Coordinate="fmt" />
  </Packages>
  <Policies>
    <PackageProviders IntegrityRequired="true" Locked="true"><Allow Kind="Directory" /></PackageProviders>
    <Compatibility><Target Name="desktop" /><Toolchain Name="msvc" /></Compatibility>
  </Policies>
</Workspace>)xml");

        const auto result = ParseWorkspaceFile(workspacePath);
        INFO(DiagnosticsText(result.diagnostics));
        REQUIRE(result.Succeeded());
        REQUIRE(result.value->projects.size() == 1);
        CHECK(result.value->projects.front().project.name == "Hello");
        CHECK(result.value->centralVersions.contains("fmt"));
        CHECK(result.value->localPackages.at("fmt").coordinate.exactVersion == "11.0.2");
        CHECK(result.value->providerPolicy.allowedKinds.contains("Directory"));
        CHECK(result.value->providerPolicy.allowedKinds.size() == 1);
        CHECK(result.value->compatibilityPolicy.targets.contains("desktop"));
    }

    TEST_CASE("Workspace model rejects overlapping discovery and policy conflicts", "[workspace-model]")
    {
        TempDir temp{};
        WriteFile(temp.path() / "apps/Hello.nginproj", "<Project Name=\"Hello\" Type=\"Application\" />");
        const auto workspacePath = temp.path() / "NGIN.ngin";
        WriteFile(workspacePath, R"xml(<Workspace Name="Invalid">
  <Projects>
    <Project Path="apps/Hello.nginproj" />
    <Project Include="apps/**/*.nginproj" />
  </Projects>
  <Policies><PackageProviders Locked="true" AllowNonHermetic="true" /></Policies>
</Workspace>)xml");

        const auto result = ParseWorkspaceFile(workspacePath);
        CHECK_FALSE(result.Succeeded());
        const auto text = DiagnosticsText(result.diagnostics);
        CHECK(text.find("more than one workspace declaration") != std::string::npos);
        CHECK(text.find("cannot allow non-hermetic") != std::string::npos);
        CHECK(text.find("requires IntegrityRequired") != std::string::npos);
    }

    TEST_CASE("Workspace model rejects unused central versions", "[workspace-model]")
    {
        TempDir temp{};
        WriteFile(temp.path() / "Hello.nginproj", "<Project Name=\"Hello\" Type=\"Application\" />");
        const auto workspacePath = temp.path() / "NGIN.ngin";
        WriteFile(workspacePath, R"xml(<Workspace Name="Invalid">
  <Projects><Project Path="Hello.nginproj" /></Projects>
  <Packages><Version Name="fmt" Exact="11.0.2" /></Packages>
</Workspace>)xml");

        const auto result = ParseWorkspaceFile(workspacePath);
        CHECK_FALSE(result.Succeeded());
        CHECK(DiagnosticsText(result.diagnostics).find("central Version 'fmt' is unused") != std::string::npos);
    }
}
