#include "TestSupport.hpp"

TEST_CASE("workspace parses projects, package sources, and central package "
          "versions")
{
    TempDir temp{};
    WriteFile(temp.path() / "build/platforms.ngin.xml",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Definitions SchemaVersion="4" Name="Game.Platforms">
  <Platforms>
    <Platform Name="linux-x64"
              OperatingSystem="linux"
              Architecture="x64"
              Abi="linux-x64-clang18-libc++-cxx23" />
  </Platforms>
  <Toolchains>
    <Toolchain Name="clang-lld"
               Compiler="clang"
               CompilerVersion="18"
               Linker="lld"
               Generator="Ninja"
               CppStandardLibrary="libc++" />
  </Toolchains>
</Definitions>
)xml");
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="Game" DefaultProfile="dev">
  <Imports>
    <Import Path="build/platforms.ngin.xml" />
  </Imports>
  <Projects>
    <Project Path="Game.Client/Game.Client.nginproj" />
  </Projects>
  <Packages>
    <Source Name="local" Path="packages" />
    <Source Name="remote" Url="https://packages.example.invalid/v1/index.json" />
    <Version Name="NGIN.Core" Range="[0.1.0,0.2.0)" />
  </Packages>
</Workspace>
)xml");

    const auto workspace = LoadWorkspaceManifest(temp.path());

    REQUIRE(workspace.name == "Game");
    REQUIRE(workspace.defaultProfile == "dev");
    REQUIRE(workspace.imports.size() == 1);
    REQUIRE(workspace.platforms.size() == 1);
    REQUIRE(workspace.platforms[0].name == "linux-x64");
    REQUIRE(workspace.platforms[0].abi == "linux-x64-clang18-libc++-cxx23");
    REQUIRE(workspace.toolchains.size() == 1);
    REQUIRE(workspace.toolchains[0].name == "clang-lld");
    REQUIRE(workspace.toolchains[0].compiler == "clang");
    REQUIRE(workspace.projects.size() == 1);
    REQUIRE(workspace.projects[0] == (temp.path() / "Game.Client/Game.Client.nginproj").lexically_normal());
    REQUIRE(workspace.packageSources.size() == 1);
    REQUIRE(workspace.packageSources[0] == (temp.path() / "packages").lexically_normal());
    REQUIRE(workspace.packageSourceUrls.size() == 1);
    REQUIRE(workspace.packageSourceUrls[0] == "https://packages.example.invalid/v1/index.json");
    REQUIRE(workspace.dependencyVersions.at("NGIN.Core") == "[0.1.0,0.2.0)");
}

TEST_CASE("workspace parses external package providers")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="Providers">
  <Packages>
    <Provider Name="vcpkg" Kind="Vcpkg" Root="Tools/vcpkg" Triplet="x64-linux" />
    <Provider Name="conan" Kind="Conan" Profile="linux-gcc-debug" />
  </Packages>
</Workspace>
)xml");

    const auto workspace = LoadWorkspaceManifest(temp.path());

    REQUIRE(workspace.externalPackageProviders.size() == 2);
    REQUIRE(workspace.externalPackageProviders.at("vcpkg").kind == "Vcpkg");
    REQUIRE(workspace.externalPackageProviders.at("vcpkg").root == (temp.path() / "Tools/vcpkg").lexically_normal());
    REQUIRE(workspace.externalPackageProviders.at("vcpkg").triplet == "x64-linux");
    REQUIRE(workspace.externalPackageProviders.at("conan").kind == "Conan");
    REQUIRE(workspace.externalPackageProviders.at("conan").profile == "linux-gcc-debug");
}

TEST_CASE("workspace rejects unknown external package provider kind")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="Providers">
  <Packages>
    <Provider Name="custom" Kind="Custom" />
  </Packages>
</Workspace>
)xml");

    REQUIRE_THROWS_WITH(LoadWorkspaceManifest(temp.path()), ContainsSubstring("unknown package provider kind 'Custom'"));
}

TEST_CASE("workspace default profile participates in command profile selection")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="ProfileWorkspace" DefaultProfile="shipping">
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
</Workspace>
)xml");
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Profile.App" DefaultProfile="dev">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
  <Profile Name="dev">
    <Defaults>
      <BuildType Name="Debug" />
    </Defaults>
  </Profile>
  <Profile Name="shipping">
    <Defaults>
      <BuildType Name="Release" />
    </Defaults>
  </Profile>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    ParsedArgs args{};
    args.projectPath = (temp.path() / "App/App.nginproj").string();
    args.format = "json";

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdInspect(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("profile":"shipping")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("name":"BuildType","value":"Release")"));
}

TEST_CASE("configuration overrides default profile build type")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "App/App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Config.App" DefaultProfile="dev">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
  <Profile Name="dev">
    <Defaults>
      <BuildType Name="Debug" />
    </Defaults>
  </Profile>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    ParsedArgs args{};
    args.projectPath = projectPath.string();
    args.configurationName = "Release";
    args.format = "json";

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdInspect(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("profile":"dev")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("name":"BuildType","value":"Release")"));
}

TEST_CASE("configuration selects matching common profile")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "App/App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="ConfigProfile.App" DefaultProfile="dev">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
  <Profile Name="dev">
    <Defaults>
      <BuildType Name="Debug" />
    </Defaults>
  </Profile>
  <Profile Name="Release">
    <Defaults>
      <BuildType Name="Release" />
      <Environment Name="production" />
    </Defaults>
  </Profile>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    ParsedArgs args{};
    args.projectPath = projectPath.string();
    args.configurationName = "Release";
    args.format = "json";

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdInspect(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("profile":"Release")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("name":"Environment","value":"production")"));
}

TEST_CASE("configuration can override an explicit profile build type")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "App/App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="ProfileConfig.App" DefaultProfile="dev">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
  <Profile Name="Runtime">
    <Defaults>
      <BuildType Name="Debug" />
      <Environment Name="development" />
    </Defaults>
  </Profile>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    ParsedArgs args{};
    args.projectPath = projectPath.string();
    args.profileName = "Runtime";
    args.configurationName = "Release";
    args.format = "json";

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdInspect(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("profile":"Runtime")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("name":"BuildType","value":"Release")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("name":"Environment","value":"development")"));
}

TEST_CASE("configuration rejects custom scenario names")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "App/App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="CustomConfig.App" DefaultProfile="dev">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
  <Profile Name="dev">
    <Defaults>
      <BuildType Name="Debug" />
    </Defaults>
  </Profile>
  <Profile Name="Shipping">
    <Defaults>
      <BuildType Name="Release" />
    </Defaults>
  </Profile>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    ParsedArgs args{};
    args.projectPath = projectPath.string();
    args.configurationName = "Shipping";
    args.format = "json";

    REQUIRE_THROWS_WITH(CmdInspect(temp.path(), args), ContainsSubstring("unknown configuration 'Shipping'"));
}

TEST_CASE("workspace profile policy applies to projects without local profiles")
{
    TempDir temp{};
    WriteFile(temp.path() / "build/platforms.ngin.xml",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Definitions SchemaVersion="4" Name="Policy.Platforms">
  <Platforms>
    <Platform Name="windows-x64"
              OperatingSystem="windows"
              Architecture="x64"
              Abi="windows-x64-msvc-v143-md-cxx23" />
  </Platforms>
</Definitions>
)xml");
    WriteFile(temp.path() / "build/toolchains.ngin.xml",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Definitions SchemaVersion="4" Name="Policy.Toolchains">
  <Toolchains>
    <Toolchain Name="msvc-v143"
               Compiler="msvc"
               CompilerVersion="v143"
               Linker="link"
               Generator="Ninja"
               RuntimeLibrary="MD" />
  </Toolchains>
</Definitions>
)xml");
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="WorkspacePolicy" DefaultProfile="shipping">
  <Imports>
    <Import Path="build/platforms.ngin.xml" />
    <Import Path="build/toolchains.ngin.xml" />
  </Imports>
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
  <Packages>
    <Source Path="Packages" />
  </Packages>
  <Profiles>
    <Profile Name="shipping">
      <Defaults>
        <BuildType Name="Release" />
        <TargetPlatform Name="windows-x64" />
        <HostPlatform Name="host" />
        <Toolchain Name="msvc-v143" />
        <Environment Name="production" />
      </Defaults>
      <Build>
        <Define Name="WORKSPACE_SHIPPING" Value="1" />
      </Build>
      <Quality>
        <Analyzer Name="workspace-clang-tidy" Severity="Error">
          <Config Path=".clang-tidy" />
        </Analyzer>
      </Quality>
      <Environment>
        <Env Name="WORKSPACE_ENV" Value="production" />
      </Environment>
      <Stage>
        <Config Source="config/workspace.json"
                Target="config/app.json"
                Collision="Override" />
      </Stage>
      <Uses>
        <Package Name="Workspace.Policy.Package"
                 Version="[1.0.0,2.0.0)"
                 Scope="Dev">
          <Feature Name="Diagnostics" />
        </Package>
      </Uses>
      <Application>
        <Build>
          <Define Name="WORKSPACE_APPLICATION" Value="1" />
        </Build>
        <Quality>
          <Analyzer Name="workspace-app-analyzer" Severity="Error" />
        </Quality>
        <Environment>
          <Env Name="WORKSPACE_APP_ENV" Value="1" />
        </Environment>
        <Stage>
          <Content Source="assets/**"
                   Target="assets" />
        </Stage>
        <Uses>
          <Tool Name="Workspace.Policy.Tool"
                Version="[1.0.0,2.0.0)" />
        </Uses>
        <Runtime>
          <Module Name="Workspace.Policy.Module"
                  Stage="Startup">
            <Provides Service="Workspace.Policy.Ready" />
          </Module>
        </Runtime>
      </Application>
      <Library>
        <Build>
          <Define Name="WORKSPACE_LIBRARY_ONLY" Value="1" />
        </Build>
      </Library>
    </Profile>
  </Profiles>
</Workspace>
)xml");
    WriteFile(temp.path() / "Packages/Workspace.Policy.Package/Workspace.Policy.Package.nginpkg",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Workspace.Policy.Package" Version="1.0.0">
  <Library Name="Workspace.Policy.Package">
    <Exports>
      <LibraryTarget Name="Workspace::Policy::Package" />
    </Exports>
  </Library>
  <Features>
    <Feature Name="Diagnostics">
      <Build>
        <Define Name="WORKSPACE_POLICY_DIAGNOSTICS" Value="1" Visibility="Public" />
      </Build>
    </Feature>
  </Features>
</Package>
)xml");
    WriteFile(temp.path() / "Packages/Workspace.Policy.Tool/Workspace.Policy.Tool.nginpkg",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Workspace.Policy.Tool" Version="1.0.0">
  <Tool Name="Workspace.Policy.Tool">
    <Exports>
      <Tool Name="Workspace.Policy.Tool" Executable="workspace-policy-tool" />
    </Exports>
  </Tool>
</Package>
)xml");
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Workspace.Policy.App">
  <Application />
</Project>
)xml");
    WriteFile(temp.path() / "App/config/workspace.json", "{}\n");
    WriteFile(temp.path() / "App/assets/readme.txt", "asset\n");

    ParsedArgs args{};
    args.projectPath = (temp.path() / "App/App.nginproj").string();
    args.format = "json";

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdInspect(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("profile":"shipping")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("name":"BuildType","value":"Release")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("targetPlatform":"windows-x64")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("toolchain":"msvc-v143")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("abiTag":"windows-x64-msvc-v143-md-cxx23")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("environment":"production")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("name":"WORKSPACE_ENV","value":"production")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("name":"WORKSPACE_APP_ENV","value":"1")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("target":"config/app.json")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("target":"assets/readme.txt")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("runtimeModules":1)"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("name":"Workspace.Policy.Package","version":"1.0.0")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("scope":"Dev")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("name":"Workspace.Policy.Tool","version":"1.0.0")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("scope":"Build")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("package":"Workspace.Policy.Package")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("feature":"Diagnostics")"));

    ParsedArgs explainArgs{};
    explainArgs.projectPath = args.projectPath;
    explainArgs.profileName = "shipping";
    explainArgs.packageName = "define:WORKSPACE_SHIPPING";
    std::ostringstream explainCaptured{};
    previous = std::cout.rdbuf(explainCaptured.rdbuf());
    const auto explainExitCode = CmdExplainObject(temp.path(), explainArgs);
    std::cout.rdbuf(previous);

    REQUIRE(explainExitCode == 0);
    REQUIRE_THAT(explainCaptured.str(), ContainsSubstring("value: WORKSPACE_SHIPPING=1"));

    explainArgs.packageName = "define:WORKSPACE_APPLICATION";
    std::ostringstream appExplainCaptured{};
    previous = std::cout.rdbuf(appExplainCaptured.rdbuf());
    const auto appExplainExitCode = CmdExplainObject(temp.path(), explainArgs);
    std::cout.rdbuf(previous);

    REQUIRE(appExplainExitCode == 0);
    REQUIRE_THAT(appExplainCaptured.str(), ContainsSubstring("value: WORKSPACE_APPLICATION=1"));

    explainArgs.packageName = "define:WORKSPACE_LIBRARY_ONLY";
    std::ostringstream libraryExplainCaptured{};
    previous = std::cout.rdbuf(libraryExplainCaptured.rdbuf());
    const auto libraryExplainExitCode = CmdExplainObject(temp.path(), explainArgs);
    std::cout.rdbuf(previous);

    REQUIRE(libraryExplainExitCode == 0);
    REQUIRE_THAT(libraryExplainCaptured.str(), ContainsSubstring("result: not selected"));

    ParsedArgs analyzeArgs{};
    analyzeArgs.projectPath = args.projectPath;
    analyzeArgs.profileName = "shipping";
    std::ostringstream analyzeCaptured{};
    previous = std::cout.rdbuf(analyzeCaptured.rdbuf());
    const auto analyzeExitCode = CmdAnalyze(temp.path(), analyzeArgs);
    std::cout.rdbuf(previous);

    REQUIRE(analyzeExitCode == 0);
    REQUIRE_THAT(analyzeCaptured.str(), ContainsSubstring("workspace-clang-tidy  scope=Build "
                                                          "severity=Error config=.clang-tidy"));
    REQUIRE_THAT(analyzeCaptured.str(), ContainsSubstring("workspace-app-analyzer  scope=Build severity=Error"));

    ParsedArgs runtimeExplainArgs{};
    runtimeExplainArgs.projectPath = args.projectPath;
    runtimeExplainArgs.profileName = "shipping";
    runtimeExplainArgs.packageName = "runtime-module:Workspace.Policy.Module";
    std::ostringstream runtimeExplainCaptured{};
    previous = std::cout.rdbuf(runtimeExplainCaptured.rdbuf());
    const auto runtimeExplainExitCode = CmdExplainObject(temp.path(), runtimeExplainArgs);
    std::cout.rdbuf(previous);

    REQUIRE(runtimeExplainExitCode == 0);
    REQUIRE_THAT(runtimeExplainCaptured.str(), ContainsSubstring("result: selected"));
}

TEST_CASE("workspace profiles contribute named product overlays")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="NamedWorkspace" DefaultProfile="ci">
  <Projects>
    <Project Path="App/App.nginproj" />
    <Project Path="Lib/Lib.nginproj" />
  </Projects>
  <Profiles>
    <Profile Name="ci">
      <Generate>
        <Generator Name="WorkspaceGen">
          <Tool Name="workspace-gen" Executable="workspace-gen" />
          <Outputs>
            <Sources Path="$(GeneratedDir)/workspace/**.cpp" />
          </Outputs>
        </Generator>
      </Generate>
      <Launch Name="ci" Args="--ci" />
      <Publish Name="archive" Kind="Archive" Format="zip" Output="dist/workspace.zip" />
      <PackageOutput Name="Workspace.Generic" Version="1.0.0">
        <Exports>
          <Capability Name="Workspace.Generic" />
        </Exports>
      </PackageOutput>
      <Application>
        <Launch Name="app-policy" Args="--app-policy" />
      </Application>
      <Library>
        <PackageOutput Name="Workspace.Library" Version="1.0.0">
          <Exports>
            <Capability Name="Workspace.Library" />
          </Exports>
        </PackageOutput>
      </Library>
    </Profile>
  </Profiles>
</Workspace>
)xml");
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="NamedWorkspace.App">
  <Application />
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");
    WriteFile(temp.path() / "Lib/Lib.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="NamedWorkspace.Lib">
  <Library />
</Project>
)xml");
    WriteFile(temp.path() / "Lib/src/lib.cpp", "int named_workspace_lib() { return 1; }\n");

    ParsedArgs graphArgs{};
    graphArgs.projectPath = (temp.path() / "App/App.nginproj").string();
    graphArgs.profileName = "ci";
    graphArgs.graphPlan = "launch";
    graphArgs.launchName = "ci";

    std::ostringstream launchCaptured{};
    auto *previous = std::cout.rdbuf(launchCaptured.rdbuf());
    const auto launchExitCode = CmdGraph(temp.path(), graphArgs);
    std::cout.rdbuf(previous);

    REQUIRE(launchExitCode == 0);
    REQUIRE_THAT(launchCaptured.str(), ContainsSubstring("launch ci [selected]"));
    REQUIRE_THAT(launchCaptured.str(), ContainsSubstring("args: --ci"));
    REQUIRE_THAT(launchCaptured.str(), ContainsSubstring("launch app-policy"));

    ParsedArgs inspectArgs{};
    inspectArgs.projectPath = graphArgs.projectPath;
    inspectArgs.profileName = "ci";
    inspectArgs.format = "json";

    std::ostringstream inspectCaptured{};
    previous = std::cout.rdbuf(inspectCaptured.rdbuf());
    const auto inspectExitCode = CmdInspect(temp.path(), inspectArgs);
    std::cout.rdbuf(previous);

    REQUIRE(inspectExitCode == 0);
    REQUIRE_THAT(inspectCaptured.str(), ContainsSubstring(R"("name":"WorkspaceGen")"));
    REQUIRE_THAT(inspectCaptured.str(), ContainsSubstring(R"("output":"dist/workspace.zip")"));
    REQUIRE_THAT(inspectCaptured.str(), ContainsSubstring(R"("name":"Workspace.Generic","version":"1.0.0")"));
    REQUIRE(inspectCaptured.str().find("Workspace.Library") == std::string::npos);

    ParsedArgs libOutputs{};
    libOutputs.projectPath = (temp.path() / "Lib/Lib.nginproj").string();
    libOutputs.profileName = "ci";
    libOutputs.graphPlan = "package-output";

    std::ostringstream libCaptured{};
    previous = std::cout.rdbuf(libCaptured.rdbuf());
    const auto libExitCode = CmdGraph(temp.path(), libOutputs);
    std::cout.rdbuf(previous);

    REQUIRE(libExitCode == 0);
    REQUIRE_THAT(libCaptured.str(), ContainsSubstring("package-output Workspace.Generic version=1.0.0"));
    REQUIRE_THAT(libCaptured.str(), ContainsSubstring("package-output Workspace.Library version=1.0.0"));
}

TEST_CASE("project profiles override workspace named product overlays")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "App/App.nginproj";
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="PrecedenceWorkspace" DefaultProfile="shipping">
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
  <Profiles>
    <Profile Name="shipping">
      <Launch Name="app" Args="--workspace" />
      <Publish Remove="folder" />
      <Publish Name="archive" Kind="Archive" Format="zip" Output="dist/workspace.zip" />
      <PackageOutput Name="Precedence.App" Version="1.0.0">
        <Exports>
          <Capability Name="Workspace.Package" />
        </Exports>
      </PackageOutput>
    </Profile>
  </Profiles>
</Workspace>
)xml");
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Precedence.App" DefaultProfile="shipping">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Launch Name="app" Args="--base" />
    <Publish Name="folder" Kind="Folder" Output="dist/base-folder" />
    <PackageOutput Name="Precedence.App" Version="0.5.0">
      <Exports>
        <Capability Name="Base.Package" />
      </Exports>
    </PackageOutput>
  </Application>
  <Profile Name="shipping">
    <Application>
      <Launch Name="app" Args="--project" />
      <Publish Name="folder" Kind="Folder" Output="dist/project-folder" />
      <Publish Name="archive" Kind="Archive" Format="zip" Output="dist/project.zip" />
      <PackageOutput Name="Precedence.App" Version="2.0.0">
        <Exports>
          <Capability Name="Project.Package" />
        </Exports>
      </PackageOutput>
    </Application>
  </Profile>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    ParsedArgs launchArgs{};
    launchArgs.projectPath = projectPath.string();
    launchArgs.profileName = "shipping";
    launchArgs.graphPlan = "launch";

    std::ostringstream launchCaptured{};
    auto *previous = std::cout.rdbuf(launchCaptured.rdbuf());
    const auto launchExitCode = CmdGraph(temp.path(), launchArgs);
    std::cout.rdbuf(previous);

    REQUIRE(launchExitCode == 0);
    REQUIRE_THAT(launchCaptured.str(), ContainsSubstring("launch app [selected]"));
    REQUIRE_THAT(launchCaptured.str(), ContainsSubstring("args: --project"));
    REQUIRE(launchCaptured.str().find("--workspace") == std::string::npos);

    ParsedArgs publishArgs{};
    publishArgs.projectPath = projectPath.string();
    publishArgs.profileName = "shipping";
    publishArgs.graphPlan = "publish";

    std::ostringstream publishCaptured{};
    previous = std::cout.rdbuf(publishCaptured.rdbuf());
    const auto publishExitCode = CmdGraph(temp.path(), publishArgs);
    std::cout.rdbuf(previous);

    REQUIRE(publishExitCode == 0);
    REQUIRE_THAT(publishCaptured.str(), ContainsSubstring("publish folder kind=Folder output=dist/project-folder"));
    REQUIRE_THAT(publishCaptured.str(), ContainsSubstring("publish archive kind=Archive output=dist/project.zip"));
    REQUIRE(publishCaptured.str().find("dist/workspace.zip") == std::string::npos);

    ParsedArgs outputArgs{};
    outputArgs.projectPath = projectPath.string();
    outputArgs.profileName = "shipping";
    outputArgs.graphPlan = "package-output";

    std::ostringstream outputCaptured{};
    previous = std::cout.rdbuf(outputCaptured.rdbuf());
    const auto outputExitCode = CmdGraph(temp.path(), outputArgs);
    std::cout.rdbuf(previous);

    REQUIRE(outputExitCode == 0);
    REQUIRE_THAT(outputCaptured.str(), ContainsSubstring("package-output Precedence.App version=2.0.0"));
    REQUIRE(outputCaptured.str().find("version=1.0.0") == std::string::npos);
    REQUIRE(outputCaptured.str().find("version=0.5.0") == std::string::npos);
}

TEST_CASE("project profile build and analyzer overlays override workspace policy")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "App/App.nginproj";
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="BuildPrecedenceWorkspace" DefaultProfile="shipping">
  <Projects>
    <Project Path="App/App.nginproj" />
    <Project Path="Lib/Lib.nginproj" />
  </Projects>
  <Profiles>
    <Profile Name="shipping">
      <Build>
        <Define Name="APP_MODE" Value="workspace" />
      </Build>
      <Application>
        <Build>
          <Define Name="APP_ONLY" Value="1" />
        </Build>
      </Application>
      <Quality>
        <Analyzer Name="clang-tidy" Severity="Warning" />
      </Quality>
    </Profile>
  </Profiles>
</Workspace>
)xml");
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="BuildPrecedence.App" DefaultProfile="shipping">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
  <Profile Name="shipping">
    <Application>
      <Build>
        <Define Name="APP_MODE" Value="project" />
      </Build>
      <Quality>
        <Analyzer Name="clang-tidy" Severity="Error" />
      </Quality>
    </Application>
  </Profile>
</Project>
)xml");
    WriteFile(temp.path() / "Lib/Lib.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="BuildPrecedence.Lib" DefaultProfile="shipping">
  <Library>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Library>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");
    WriteFile(temp.path() / "Lib/src/lib.cpp", "int lib() { return 1; }\n");

    ParsedArgs appArgs{};
    appArgs.projectPath = projectPath.string();
    appArgs.profileName = "shipping";
    appArgs.format = "json";

    std::ostringstream appCaptured{};
    auto *previous = std::cout.rdbuf(appCaptured.rdbuf());
    const auto appExitCode = CmdGraph(temp.path(), appArgs);
    std::cout.rdbuf(previous);

    REQUIRE(appExitCode == 0);
    REQUIRE_THAT(appCaptured.str(), ContainsSubstring("APP_MODE=project"));
    REQUIRE_THAT(appCaptured.str(), ContainsSubstring("APP_ONLY=1"));
    REQUIRE(appCaptured.str().find("APP_MODE=workspace") == std::string::npos);
    REQUIRE_THAT(appCaptured.str(), ContainsSubstring(R"("name":"clang-tidy")"));
    REQUIRE_THAT(appCaptured.str(), ContainsSubstring(R"("severity":"Error")"));

    ParsedArgs buildPlanArgs = appArgs;
    buildPlanArgs.graphPlan = "build";
    std::ostringstream buildPlanCaptured{};
    previous = std::cout.rdbuf(buildPlanCaptured.rdbuf());
    const auto buildPlanExitCode = CmdGraph(temp.path(), buildPlanArgs);
    std::cout.rdbuf(previous);

    REQUIRE(buildPlanExitCode == 0);
    REQUIRE_THAT(buildPlanCaptured.str(),
                 ContainsSubstring(R"("sourceKind":"project-profile","sourceName":"shipping")"));
    REQUIRE_THAT(buildPlanCaptured.str(),
                 ContainsSubstring(R"("sourceKind":"workspace-product-profile","sourceName":"shipping")"));

    ParsedArgs libArgs{};
    libArgs.projectPath = (temp.path() / "Lib/Lib.nginproj").string();
    libArgs.profileName = "shipping";
    libArgs.format = "json";

    std::ostringstream libCaptured{};
    previous = std::cout.rdbuf(libCaptured.rdbuf());
    const auto libExitCode = CmdGraph(temp.path(), libArgs);
    std::cout.rdbuf(previous);

    REQUIRE(libExitCode == 0);
    REQUIRE_THAT(libCaptured.str(), ContainsSubstring("APP_MODE=workspace"));
    REQUIRE(libCaptured.str().find("APP_ONLY=1") == std::string::npos);
}

TEST_CASE("workspace duplicate named overlay identities are rejected")
{
    TempDir temp{};

    auto loadError = [&](const std::string &body) -> std::string {
        WriteFile(temp.path() / "Workspace.ngin", body);
        try
        {
            (void)LoadWorkspaceManifest(temp.path());
        }
        catch (const std::exception &ex)
        {
            return ex.what();
        }
        return {};
    };

    const auto defineError = loadError(
        R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="DuplicateDefine">
  <Profiles>
    <Profile Name="ci">
      <Build>
        <Define Name="CI_MODE" Value="a" />
        <Define Name="CI_MODE" Value="b" />
      </Build>
    </Profile>
  </Profiles>
</Workspace>
)xml");
    REQUIRE_THAT(defineError, ContainsSubstring("duplicate define 'CI_MODE' in the same overlay scope"));

    const auto includePathError = loadError(
        R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="DuplicateIncludePath">
  <Profiles>
    <Profile Name="ci">
      <Build>
        <IncludePath Path="include/../include" Visibility="Public" />
        <IncludePath Path="include" Visibility="Public" />
      </Build>
    </Profile>
  </Profiles>
</Workspace>
)xml");
    REQUIRE_THAT(includePathError,
                 ContainsSubstring("duplicate include path 'include|Public' in the same overlay scope"));

    const auto compileOptionError = loadError(
        R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="DuplicateCompileOption">
  <Profiles>
    <Profile Name="ci">
      <Build>
        <CompileOption Value="-Wall" />
        <CompileOption Value="-Wall" />
      </Build>
    </Profile>
  </Profiles>
</Workspace>
)xml");
    REQUIRE_THAT(compileOptionError, ContainsSubstring("duplicate compile option '-Wall' in the same overlay scope"));

    const auto linkOptionError = loadError(
        R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="DuplicateLinkOption">
  <Profiles>
    <Profile Name="ci">
      <Build>
        <LinkLibrary Name="m" />
        <LinkOption Value="m" />
      </Build>
    </Profile>
  </Profiles>
</Workspace>
)xml");
    REQUIRE_THAT(linkOptionError, ContainsSubstring("duplicate link option 'm' in the same overlay scope"));

    const auto analyzerError = loadError(
        R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="DuplicateAnalyzer">
  <Profiles>
    <Profile Name="ci">
      <Quality>
        <Analyzer Name="clang-tidy" Severity="Warning" />
        <Analyzer Name="clang-tidy" Severity="Error" />
      </Quality>
    </Profile>
  </Profiles>
</Workspace>
)xml");
    REQUIRE_THAT(analyzerError, ContainsSubstring("duplicate analyzer 'clang-tidy' in the same overlay scope"));

    const auto launchError = loadError(
        R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="DuplicateLaunch">
  <Profiles>
    <Profile Name="ci">
      <Launch Name="app" Args="--a" />
      <Launch Name="app" Args="--b" />
    </Profile>
  </Profiles>
</Workspace>
)xml");
    REQUIRE_THAT(launchError, ContainsSubstring("duplicate launch 'app' in the same overlay scope"));

    const auto generatorError = loadError(
        R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="DuplicateGenerator">
  <Profiles>
    <Profile Name="ci">
      <Generate>
        <Generator Name="Codegen">
          <Tool Name="tool" Executable="tool" />
          <Outputs>
            <Sources Path="$(GeneratedDir)/a/**.cpp" />
          </Outputs>
        </Generator>
        <Generator Name="Codegen">
          <Tool Name="tool" Executable="tool" />
          <Outputs>
            <Sources Path="$(GeneratedDir)/b/**.cpp" />
          </Outputs>
        </Generator>
      </Generate>
    </Profile>
  </Profiles>
</Workspace>
)xml");
    REQUIRE_THAT(generatorError, ContainsSubstring("duplicate generator 'Codegen' in the same overlay scope"));

    const auto publishError = loadError(
        R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="DuplicatePublish">
  <Profiles>
    <Profile Name="shipping">
      <Publish Name="folder" Kind="Folder" Output="dist/a" />
      <Publish Name="folder" Kind="Folder" Output="dist/b" />
    </Profile>
  </Profiles>
</Workspace>
)xml");
    REQUIRE_THAT(publishError, ContainsSubstring("duplicate publish 'folder' in the same overlay scope"));

    const auto packageOutputError = loadError(
        R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="DuplicatePackageOutput">
  <Profiles>
    <Profile Name="shipping">
      <PackageOutput Name="App.Bundle" Version="1.0.0" />
      <PackageOutput Name="App.Bundle" Version="2.0.0" />
    </Profile>
  </Profiles>
</Workspace>
)xml");
    REQUIRE_THAT(packageOutputError,
                 ContainsSubstring("duplicate package output 'App.Bundle' in the same overlay scope"));
}

TEST_CASE("workspace build defaults apply unless project declares explicit "
          "build settings")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="BuildDefaults">
  <Projects>
    <Project Path="Inherited/Inherited.nginproj" />
    <Project Path="Explicit/Explicit.nginproj" />
  </Projects>
  <Defaults>
    <Language Standard="C++20" Required="true" Extensions="false" />
    <Backend Name="CMake" Mode="Generated" />
  </Defaults>
</Workspace>
)xml");
    WriteFile(temp.path() / "Inherited/Inherited.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Inherited.App">
  <Application />
</Project>
)xml");
    WriteFile(temp.path() / "Inherited/src/main.cpp", "int main() { return 0; }\n");
    WriteFile(temp.path() / "Explicit/Explicit.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Explicit.App">
  <Application>
    <Build>
      <Language Standard="C++23" />
    </Build>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "Explicit/src/main.cpp", "int main() { return 0; }\n");

    const std::optional<std::string> selectedProfile{};
    const auto inherited = LoadProjectManifest(temp.path() / "Inherited/Inherited.nginproj");
    const auto &inheritedProfile = ProfileByName(inherited, selectedProfile);
    const auto inheritedResolved = ResolveLaunch(inherited, inheritedProfile);

    REQUIRE_FALSE(inheritedResolved.diagnostics.HasErrors());
    REQUIRE(inheritedResolved.value.has_value());
    REQUIRE(inheritedResolved.value->project.build.language == "CXX");
    REQUIRE(inheritedResolved.value->project.build.languageStandard == "20");
    REQUIRE(inheritedResolved.value->project.build.backend == "CMake");
    REQUIRE(inheritedResolved.value->project.build.mode == "Generated");

    const auto explicitProject = LoadProjectManifest(temp.path() / "Explicit/Explicit.nginproj");
    const auto &explicitProfile = ProfileByName(explicitProject, selectedProfile);
    const auto explicitResolved = ResolveLaunch(explicitProject, explicitProfile);

    REQUIRE_FALSE(explicitResolved.diagnostics.HasErrors());
    REQUIRE(explicitResolved.value.has_value());
    REQUIRE(explicitResolved.value->project.build.language == "CXX");
    REQUIRE(explicitResolved.value->project.build.languageStandard == "23");
}
