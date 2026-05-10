#include "TestSupport.hpp"

TEST_CASE("workspace parses projects, package sources, and central package versions")
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
    REQUIRE_THAT(analyzeCaptured.str(), ContainsSubstring("analyzer workspace-clang-tidy scope=Build severity=Error config=.clang-tidy"));
    REQUIRE_THAT(analyzeCaptured.str(), ContainsSubstring("analyzer workspace-app-analyzer scope=Build severity=Error"));

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

TEST_CASE("workspace build defaults apply unless project declares explicit build settings")
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
