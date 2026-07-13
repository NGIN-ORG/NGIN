#include "TestSupport.hpp"

TEST_CASE("minimal application project normalizes to generated executable")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Hello.Native.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Hello.Native">
  <Application />
</Project>
)xml");

    const auto project = LoadProjectManifest(projectPath);
    const std::optional<std::string> selectedProfile{};
    const auto &profile = ProfileByName(project, selectedProfile);

    REQUIRE(project.name == "Hello.Native");
    REQUIRE(project.type == "Application");
    REQUIRE(project.defaultProfile == "dev");
    REQUIRE(project.output.kind == "Executable");
    REQUIRE(project.output.name == "Hello.Native");
    REQUIRE(project.output.target == "Hello.Native");
    REQUIRE(project.build.backend == "CMake");
    REQUIRE(project.build.mode == "Generated");
    REQUIRE(project.build.language == "CXX");
    REQUIRE(project.build.languageStandard == "23");
    REQUIRE(profile.name == "dev");
    REQUIRE(profile.buildType == "Debug");
    REQUIRE(profile.platform == DetectHostPlatform().name);
    REQUIRE(profile.operatingSystem == DetectHostPlatform().operatingSystem);
    REQUIRE(profile.architecture == DetectHostPlatform().architecture);
    REQUIRE(profile.environmentName == "development");
    REQUIRE(profile.launch.executable == "Hello.Native");
    REQUIRE(profile.launch.args.empty());
    REQUIRE(project.inputs.size() == 1);
    REQUIRE(project.inputs[0].kind == "Source");
    REQUIRE(project.inputs[0].role == "Source");
    REQUIRE(project.inputs[0].path == "src");
    REQUIRE(project.inputs[0].mode == "Directory");
}

TEST_CASE("minimal library project normalizes to static library without launch")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Game.Engine.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Game.Engine">
  <Library />
</Project>
)xml");

    const auto project = LoadProjectManifest(projectPath);
    const std::optional<std::string> selectedProfile{};
    const auto &profile = ProfileByName(project, selectedProfile);

    REQUIRE(project.name == "Game.Engine");
    REQUIRE(project.type == "Library");
    REQUIRE(project.output.kind == "StaticLibrary");
    REQUIRE_FALSE(profile.launch.executable.has_value());
}

TEST_CASE("external project exports imported interface target")
{
    TempDir temp{};
    const auto externalPath = temp.path() / "External/System.OpenSSL.nginproj";
    WriteFile(externalPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="System.OpenSSL">
  <External>
    <Exports>
      <LibraryTarget Name="OpenSSL::SSL" />
      <IncludePath Path="include" />
      <Define Name="HAS_OPENSSL" Value="1" />
    </Exports>
  </External>
</Project>
)xml");
    WriteFile(temp.path() / "External/include/openssl.hpp", "#pragma once\n");

    const auto appPath = temp.path() / "App/App.nginproj";
    WriteFile(appPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="External.App">
  <Application>
    <Uses>
      <Project Name="System.OpenSSL" Path="../External/System.OpenSSL.nginproj" />
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    const auto external = LoadProjectManifest(externalPath);
    REQUIRE(external.productKind == "External");
    REQUIRE(external.inputs.empty());
    REQUIRE(external.output.target == "OpenSSL::SSL");
    REQUIRE(external.build.includeDirectories.size() == 1);
    REQUIRE(external.build.includeDirectories[0].visibility == "Interface");

    const auto app = LoadProjectManifest(appPath);
    const std::optional<std::string> selectedProfile{};
    const auto &profile = ProfileByName(app, selectedProfile);
    const auto configured = ConfigureLaunch(app, profile, temp.path() / "out");
    REQUIRE(configured.value.has_value());
    REQUIRE_FALSE(configured.diagnostics.HasErrors());

    const auto generated = ReadFile(temp.path() / "out/.ngin/cmake-src/CMakeLists.txt");
    REQUIRE_THAT(generated, ContainsSubstring(R"(add_library("OpenSSL::SSL" INTERFACE IMPORTED))"));
    REQUIRE_THAT(generated, ContainsSubstring("target_include_directories(\"OpenSSL::SSL\" INTERFACE \"" + (temp.path() / "External/include").generic_string() + "\")"));
    REQUIRE_THAT(generated, ContainsSubstring(R"(target_compile_definitions("OpenSSL::SSL" INTERFACE "HAS_OPENSSL=1"))"));
    REQUIRE_THAT(generated, ContainsSubstring(R"(target_link_libraries("External.App" PRIVATE "OpenSSL::SSL"))"));
    REQUIRE_THAT(generated, !ContainsSubstring("CMAKE_SUPPRESS_REGENERATION"));
}

TEST_CASE("tool run and package output metadata parse")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Game.AssetCompiler.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Game.AssetCompiler">
  <Tool>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Run Name="compile-example-assets"
         WorkingDirectory="$(StageDir)"
         Args="--input ../Assets/raw --output ../Assets/compiled" />
    <PackageOutput Name="Game.AssetCompiler" Version="1.0.0">
      <Metadata>
        <Description>Asset compiler tool.</Description>
        <License>MIT</License>
      </Metadata>
      <Exports>
        <Tool Name="Game.AssetCompiler" />
        <Capability Name="Game.AssetPipeline" />
      </Exports>
      <Compatibility>
        <TargetPlatform Name="linux-x64" />
        <Abi Tag="$(ResolvedAbiTag)" />
      </Compatibility>
    </PackageOutput>
  </Tool>
</Project>
)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");

    const auto project = LoadProjectManifest(projectPath);
    const std::optional<std::string> selectedProfile{};
    const auto &profile = ProfileByName(project, selectedProfile);

    REQUIRE(project.productKind == "Tool");
    REQUIRE(project.type == "Tool");
    REQUIRE(project.output.kind == "Executable");
    REQUIRE(profile.launch.args == "--input ../Assets/raw --output ../Assets/compiled");
    REQUIRE(project.packageOutputs.size() == 1);
    REQUIRE(project.packageOutputs[0].name == "Game.AssetCompiler");
    REQUIRE(project.packageOutputs[0].version == "1.0.0");
    REQUIRE(project.packageOutputs[0].description == "Asset compiler tool.");
    REQUIRE(project.packageOutputs[0].license == "MIT");
    REQUIRE(project.packageOutputs[0].tools.size() == 1);
    REQUIRE(project.packageOutputs[0].capabilities.size() == 1);
    REQUIRE(project.packageOutputs[0].targetPlatforms[0] == "linux-x64");
    REQUIRE(project.packageOutputs[0].abiTag == "$(ResolvedAbiTag)");

    ParsedArgs args{};
    args.projectPath = projectPath.string();
    args.format = "json";
    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdInspect(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("packageOutputs":[{"name":"Game.AssetCompiler")"));
}

TEST_CASE("test product can reference a project and test package")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Game.Engine.Tests.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Game.Engine.Tests">
  <Test>
    <Uses>
      <Project Name="Game.Engine" Path="../Game.Engine/Game.Engine.nginproj" />
      <Package Name="Catch2" Version="[3.0.0,4.0.0)" Scope="Test" />
    </Uses>
  </Test>
</Project>
)xml");

    const auto project = LoadProjectManifest(projectPath);

    REQUIRE(project.type == "Application");
    REQUIRE(project.output.kind == "Executable");
    REQUIRE(project.projectRefs.size() == 1);
    REQUIRE(project.projectRefs[0].path == (temp.path() / "../Game.Engine/Game.Engine.nginproj").lexically_normal());
    REQUIRE(project.packageRefs.size() == 1);
    REQUIRE(project.packageRefs[0].name == "Catch2");
    REQUIRE(project.packageRefs[0].versionRange == "[3.0.0,4.0.0)");
    REQUIRE(project.packageRefs[0].scope == "Test");
}

TEST_CASE("hosted application parses runtime dependency, features, stage, environment, and module")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Game.Server.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Game.Server" DefaultProfile="shipping">
  <Application>
    <Uses>
      <Runtime Name="NGIN.Core" Version="[0.1.0,0.2.0)" Scope="Target;Runtime">
        <Feature Name="Diagnostics" />
      </Runtime>
      <Tool Name="NGIN.Reflection.MetaGen" Version="[0.1.0,0.2.0)" Scope="Build" />
    </Uses>
    <Build>
      <Language Standard="C++20" />
      <Sources Path="src/**.cpp" />
      <Headers Path="include/**.hpp" Visibility="Private" />
      <IncludePath Path="include" />
      <Define Name="GAME_SERVER" Value="1" />
      <CompileOption Value="-Wall" />
    </Build>
    <Stage>
      <Config Source="config/server.default.json" Target="config/server.json" />
      <Content Source="runtime/**" Target="runtime" />
    </Stage>
    <Runtime>
      <Module Name="Game.Server.Startup" Stage="Startup">
        <Provides Service="Game.Server.Ready" />
      </Module>
    </Runtime>
    <Environment>
      <Env Name="GAME_CONFIG" Value="config/server.json" />
      <Secret Name="GAME_PRIVATE_TOKEN" From="local:game.private.token" Required="false" />
    </Environment>
    <Launch Name="server" WorkingDirectory="$(StageDir)" Args="--config config/server.json" />
  </Application>
  <Profile Name="shipping">
    <Defaults>
      <BuildType Name="Release" />
      <HostPlatform Name="host" />
      <TargetPlatform Name="linux-x64" />
      <Environment Name="production" />
    </Defaults>
    <Application>
      <Build>
        <Define Name="GAME_SHIPPING" Value="1" />
      </Build>
    </Application>
  </Profile>
</Project>
)xml");

    const auto project = LoadProjectManifest(projectPath);
    const std::optional<std::string> selectedProfile{};
    const auto &profile = ProfileByName(project, selectedProfile);

    REQUIRE(project.defaultProfile == "shipping");
    REQUIRE(profile.name == "shipping");
    REQUIRE(profile.buildType == "Release");
    REQUIRE(profile.hostPlatform == "host");
    REQUIRE(profile.environmentName == "production");
    REQUIRE(profile.launch.args == "--config config/server.json");
    REQUIRE(project.build.languageStandard == "20");
    REQUIRE(project.packageRefs.size() == 2);
    REQUIRE(project.packageRefs[0].name == "NGIN.Core");
    REQUIRE(project.packageRefs[0].scope == "Target;Runtime");
    REQUIRE(project.packageRefs[1].name == "NGIN.Reflection.MetaGen");
    REQUIRE(project.packageRefs[1].scope == "Build");
    REQUIRE(project.packageFeatureUses.size() == 1);
    REQUIRE(project.packageFeatureUses[0].packageName == "NGIN.Core");
    REQUIRE(project.packageFeatureUses[0].featureName == "Diagnostics");
    REQUIRE(project.runtime.modules.size() == 1);
    REQUIRE(project.runtime.modules[0].name == "Game.Server.Startup");
    REQUIRE(project.runtime.modules[0].providesServices.size() == 1);
    REQUIRE(project.inputs.size() >= 4);
    REQUIRE(project.build.compileDefinitions.size() == 2);
    REQUIRE(project.environments.size() == 2);
}
