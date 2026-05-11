#include "TestSupport.hpp"

TEST_CASE("profile Uses overlays select package features")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="FeatureOverlayWorkspace">
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
  <Packages>
    <Source Name="local" Path="Packages" />
  </Packages>
</Workspace>
)xml");
    WriteFile(temp.path() / "Packages/Core/Core.nginpkg",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Package.Core" Version="1.0.0">
  <Library Name="Package.Core">
    <Exports>
      <LibraryTarget Name="Package::Core" />
    </Exports>
  </Library>
  <Features>
    <Feature Name="Diagnostics">
      <Build>
        <Define Name="PACKAGE_CORE_DIAGNOSTICS" Value="1" Visibility="Public" />
      </Build>
    </Feature>
  </Features>
</Package>
)xml");
    WriteFile(temp.path() / "Packages/Extra/Extra.nginpkg",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Package.Extra" Version="1.0.0">
  <Library Name="Package.Extra">
    <Exports>
      <LibraryTarget Name="Package::Extra" />
    </Exports>
  </Library>
</Package>
)xml");
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="FeatureOverlay.App" DefaultProfile="dev">
  <Application>
    <Uses>
      <Package Name="Package.Core" Version="[1.0.0,2.0.0)" Scope="Target" />
      <Package Name="Package.Extra" Version="[1.0.0,2.0.0)" Scope="Runtime" />
    </Uses>
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
    <Application>
      <Uses>
        <Package Name="Package.Core" Version="[1.0.0,2.0.0)" Scope="Target">
          <Feature Name="Diagnostics" />
        </Package>
        <Package Remove="Package.Extra" />
      </Uses>
    </Application>
  </Profile>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
    const auto dev = ResolveLaunch(project, ProfileByName(project, "dev"));
    const auto shipping = ResolveLaunch(project, ProfileByName(project, "shipping"));

    REQUIRE_FALSE(dev.diagnostics.HasErrors());
    REQUIRE(dev.value.has_value());
    REQUIRE(dev.value->selectedPackageFeatures.empty());
    REQUIRE(dev.value->orderedPackages.size() == 2);
    REQUIRE_FALSE(shipping.diagnostics.HasErrors());
    REQUIRE(shipping.value.has_value());
    REQUIRE(shipping.value->orderedPackages.size() == 1);
    REQUIRE(shipping.value->orderedPackages[0].manifest.name == "Package.Core");
    REQUIRE(shipping.value->selectedPackageFeatures.size() == 1);
    REQUIRE(shipping.value->selectedPackageFeatures[0].packageName == "Package.Core");
    REQUIRE(shipping.value->selectedPackageFeatures[0].featureName == "Diagnostics");
}

TEST_CASE("profile Uses overlays can remove project references")
{
    TempDir temp{};
    WriteFile(temp.path() / "Lib/Lib.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Overlay.Lib">
  <Library>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Library>
</Project>
)xml");
    WriteFile(temp.path() / "Lib/src/lib.cpp", "int overlay_lib() { return 1; }\n");
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="ProjectRemove.App" DefaultProfile="dev">
  <Application>
    <Uses>
      <Project Name="Overlay.Lib" Path="../Lib/Lib.nginproj" />
    </Uses>
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
    <Application>
      <Uses>
        <Project Remove="../Lib/Lib.nginproj" />
      </Uses>
    </Application>
  </Profile>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
    const auto dev = ResolveLaunch(project, ProfileByName(project, "dev"));
    const auto shipping = ResolveLaunch(project, ProfileByName(project, "shipping"));

    REQUIRE_FALSE(dev.diagnostics.HasErrors());
    REQUIRE(dev.value.has_value());
    REQUIRE(dev.value->projectUnits.size() == 2);
    REQUIRE_FALSE(shipping.diagnostics.HasErrors());
    REQUIRE(shipping.value.has_value());
    REQUIRE(shipping.value->projectUnits.size() == 1);
    REQUIRE(shipping.value->projectUnits[0].project.name == "ProjectRemove.App");
}

TEST_CASE("profile overlays carry selectors and can override staged outputs and environment")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Overlay.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Overlay.App" DefaultProfile="shipping">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
      <Define Name="GAME_MODE" Value="base" />
    </Build>
    <Stage>
      <Config Source="config/base.json" Target="config/app.json" />
    </Stage>
    <Environment>
      <Env Name="GAME_ENV" Value="base" />
    </Environment>
    <Launch Name="app" Args="--base" />
  </Application>
  <Profile Name="shipping">
    <Defaults>
      <BuildType Name="Release" />
      <Environment Name="production" />
    </Defaults>
    <Application>
      <Build>
        <Define Name="GAME_MODE" Value="shipping" />
      </Build>
      <Stage>
        <Config Source="config/prod.json" Target="config/app.json" Collision="Override" />
      </Stage>
      <Environment>
        <Env Name="GAME_ENV" Value="production" />
      </Environment>
      <Launch Name="app" Args="--shipping" />
    </Application>
  </Profile>
</Project>
)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");
    WriteFile(temp.path() / "config/base.json", "{}\n");
    WriteFile(temp.path() / "config/prod.json", "{}\n");

    const auto project = LoadProjectManifest(projectPath);
    const std::optional<std::string> selectedProfile{};
    const auto &profile = ProfileByName(project, selectedProfile);

    REQUIRE(profile.name == "shipping");
    REQUIRE(profile.launch.args == "--shipping");
    REQUIRE(project.build.compileDefinitions.size() == 2);
    REQUIRE(project.build.compileDefinitions[0].value == "GAME_MODE=base");
    REQUIRE_FALSE(project.build.compileDefinitions[0].selectors.profile.has_value());
    REQUIRE(project.build.compileDefinitions[1].value == "GAME_MODE=shipping");
    REQUIRE(project.build.compileDefinitions[1].selectors.profile == "shipping");

    const auto resolved = ResolveLaunch(project, profile);

    REQUIRE_FALSE(resolved.diagnostics.HasErrors());
    REQUIRE(resolved.value.has_value());
    REQUIRE(resolved.value->profile.launch.args == "--shipping");
    REQUIRE(std::any_of(resolved.value->environmentVariables.begin(),
                        resolved.value->environmentVariables.end(),
                        [](const EnvironmentVariable &variable)
                        { return variable.name == "GAME_ENV" && variable.value == "production"; }));
    REQUIRE(std::any_of(resolved.value->inputs.begin(),
                        resolved.value->inputs.end(),
                        [](const ResolvedInput &input)
                        {
                            return input.kind == "Config"
                                   && input.source == "config/prod.json"
                                   && input.stagedRelativePath == fs::path("config/app.json");
                        }));
    REQUIRE_FALSE(std::any_of(resolved.value->inputs.begin(),
                              resolved.value->inputs.end(),
                              [](const ResolvedInput &input)
                              { return input.kind == "Config" && input.source == "config/base.json"; }));
}

TEST_CASE("stage identity reports collisions without explicit override")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Collision.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Collision.App">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Stage>
      <Config Source="config/a.json" Target="config/app.json" />
      <Config Source="config/b.json" Target="config/app.json" />
    </Stage>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");
    WriteFile(temp.path() / "config/a.json", "{}\n");
    WriteFile(temp.path() / "config/b.json", "{}\n");

    const auto project = LoadProjectManifest(projectPath);
    REQUIRE(project.inputs.size() == 3);

    const std::optional<std::string> selectedProfile{};
    const auto &profile = ProfileByName(project, selectedProfile);
    const auto resolved = ResolveLaunch(project, profile);
    const auto diagnostics = DiagnosticMessages(resolved.diagnostics);

    REQUIRE(resolved.diagnostics.HasErrors());
    REQUIRE(std::any_of(
        diagnostics.begin(),
        diagnostics.end(),
        [](const std::string &message)
        {
            return message.find("input destination collision at 'config/app.json'") != std::string::npos;
        }));
}

TEST_CASE("profile overlays can remove and replace generator identities")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "GeneratorOverlay.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="GeneratorOverlay.App" DefaultProfile="shipping">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Generate>
      <Generator Name="Assets" Phase="Generate">
        <Tool Name="asset-tool" Executable="asset-tool" />
        <Outputs>
          <Files Path="$(GeneratedDir)/assets/dev/**" />
        </Outputs>
      </Generator>
      <Generator Name="Reflection" Phase="Generate">
        <Tool Name="reflect-tool" Executable="reflect-tool" />
        <Outputs>
          <Sources Path="$(GeneratedDir)/reflection/dev/**.cpp" />
        </Outputs>
      </Generator>
    </Generate>
  </Application>
  <Profile Name="shipping">
    <Defaults>
      <BuildType Name="Release" />
    </Defaults>
    <Application>
      <Generate>
        <Generator Remove="Assets" />
        <Generator Name="Reflection" Phase="Generate">
          <Tool Name="reflect-tool" Executable="reflect-tool" />
          <Outputs>
            <Sources Path="$(GeneratedDir)/reflection/shipping/**.cpp" />
          </Outputs>
        </Generator>
      </Generate>
    </Application>
  </Profile>
</Project>
)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");

    const auto project = LoadProjectManifest(projectPath);
    const std::optional<std::string> profileName{"shipping"};
    const auto &profile = ProfileByName(project, profileName);
    const auto resolved = ResolveLaunch(project, profile);

    REQUIRE_FALSE(resolved.diagnostics.HasErrors());
    REQUIRE(resolved.value.has_value());
    REQUIRE(resolved.value->generators.size() == 1);
    REQUIRE(resolved.value->generators[0].declaration.name == "Reflection");
    REQUIRE(resolved.value->generators[0].declaration.outputs.size() == 1);
    REQUIRE(resolved.value->generators[0].declaration.outputs[0].includePatterns.size() == 1);
    REQUIRE(resolved.value->generators[0].declaration.outputs[0].includePatterns[0] == "$(GeneratedDir)/reflection/shipping/**.cpp");
}

TEST_CASE("profile overlays can remove and replace publish identities")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "PublishOverlay.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="PublishOverlay.App" DefaultProfile="shipping">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Publish Name="folder" Kind="Folder" Output="dist/base-folder" />
    <Publish Name="archive" Kind="Archive" Format="zip" Output="dist/base.zip" />
  </Application>
  <Profile Name="shipping">
    <Defaults>
      <BuildType Name="Release" />
    </Defaults>
    <Application>
      <Publish Remove="folder" />
      <Publish Name="archive" Kind="Archive" Format="zip" Output="dist/shipping.zip">
        <Include Stage="all" RuntimeDependencies="true" Symbols="false" />
      </Publish>
    </Application>
  </Profile>
</Project>
)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");

    ParsedArgs args{};
    args.projectPath = projectPath.string();
    args.profileName = "shipping";
    args.graphPlan = "publish";

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdGraph(temp.path(), args);
    std::cout.rdbuf(previous);

    const auto output = captured.str();
    REQUIRE(exitCode == 0);
    REQUIRE_THAT(output, ContainsSubstring("publish archive kind=Archive output=dist/shipping.zip"));
    REQUIRE(output.find("publish folder") == std::string::npos);
    REQUIRE(output.find("dist/base.zip") == std::string::npos);
}

TEST_CASE("profile overlays can remove and replace package output identities")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "PackageOutputOverlay.Library.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="PackageOutputOverlay.Library" DefaultProfile="shipping">
  <Library>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <PackageOutput Name="Overlay.Core" Version="1.0.0">
      <Metadata>
        <Description>Base package output.</Description>
        <License>MIT</License>
      </Metadata>
      <Exports>
        <Library Name="Overlay::Core" />
        <Capability Name="Overlay.Core" />
      </Exports>
    </PackageOutput>
    <PackageOutput Name="Overlay.DevOnly" Version="1.0.0">
      <Exports>
        <Capability Name="Overlay.DevOnly" />
      </Exports>
    </PackageOutput>
  </Library>
  <Profile Name="shipping">
    <Defaults>
      <BuildType Name="Release" />
    </Defaults>
    <Library>
      <PackageOutput Remove="Overlay.DevOnly" />
      <PackageOutput Name="Overlay.Core" Version="2.0.0">
        <Metadata>
          <Description>Shipping package output.</Description>
          <License>Apache-2.0</License>
        </Metadata>
        <Exports>
          <Library Name="Overlay::CoreShipping" />
          <Capability Name="Overlay.Core.Shipping" />
        </Exports>
      </PackageOutput>
    </Library>
  </Profile>
</Project>
)xml");
    WriteFile(temp.path() / "src/lib.cpp", "int overlay_core() { return 1; }\n");

    ParsedArgs graphArgs{};
    graphArgs.projectPath = projectPath.string();
    graphArgs.profileName = "shipping";
    graphArgs.graphPlan = "package-output";

    std::ostringstream graphCaptured{};
    auto *previous = std::cout.rdbuf(graphCaptured.rdbuf());
    const auto graphExitCode = CmdGraph(temp.path(), graphArgs);
    std::cout.rdbuf(previous);

    const auto graphOutput = graphCaptured.str();
    REQUIRE(graphExitCode == 0);
    REQUIRE_THAT(graphOutput, ContainsSubstring("package-output Overlay.Core version=2.0.0"));
    REQUIRE(graphOutput.find("Overlay.DevOnly") == std::string::npos);
    REQUIRE(graphOutput.find("version=1.0.0") == std::string::npos);

    ParsedArgs packArgs{};
    packArgs.projectPath = projectPath.string();
    packArgs.profileName = "shipping";
    packArgs.packageName = "Overlay.Core";
    packArgs.outputPath = (temp.path() / "out").string();

    std::ostringstream packCaptured{};
    previous = std::cout.rdbuf(packCaptured.rdbuf());
    const auto packExitCode = CmdPackagePack(temp.path(), packArgs);
    std::cout.rdbuf(previous);

    REQUIRE(packExitCode == 0);
    const auto manifest = ReadFile(temp.path() / "out/Overlay.Core.nginpkg");
    REQUIRE_THAT(manifest, ContainsSubstring(R"(<Package SchemaVersion="4" Name="Overlay.Core" Version="2.0.0">)"));
    REQUIRE_THAT(manifest, ContainsSubstring("<Description>Shipping package output.</Description>"));
    REQUIRE_THAT(manifest, ContainsSubstring(R"(<LibraryTarget Name="Overlay::CoreShipping" />)"));
    REQUIRE_THAT(manifest, ContainsSubstring(R"(<Capability Name="Overlay.Core.Shipping" />)"));
    REQUIRE(manifest.find("Base package output") == std::string::npos);
}

TEST_CASE("profile overlays can remove and replace launch identities")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "LaunchOverlay.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="LaunchOverlay.App" DefaultProfile="shipping">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Launch Name="app" Args="--base" />
    <Launch Name="tools" Args="--tools" />
  </Application>
  <Profile Name="shipping">
    <Defaults>
      <BuildType Name="Release" />
    </Defaults>
    <Application>
      <Launch Name="app" Args="--shipping" />
      <Launch Remove="tools" />
    </Application>
  </Profile>
</Project>
)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");

    const auto project = LoadProjectManifest(projectPath);
    const std::optional<std::string> shippingProfile{"shipping"};
    const auto &profile = ProfileByName(project, shippingProfile);

    REQUIRE(project.launches.size() == 2);
    REQUIRE(profile.launches.size() == 2);
    REQUIRE(profile.launch.args == "--shipping");

    ParsedArgs args{};
    args.projectPath = projectPath.string();
    args.profileName = "shipping";
    args.graphPlan = "launch";

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdGraph(temp.path(), args);
    std::cout.rdbuf(previous);

    const auto output = captured.str();
    REQUIRE(exitCode == 0);
    REQUIRE_THAT(output, ContainsSubstring("launch app [selected]"));
    REQUIRE_THAT(output, ContainsSubstring("args: --shipping"));
    REQUIRE(output.find("launch tools") == std::string::npos);
    REQUIRE(output.find("--tools") == std::string::npos);
}

TEST_CASE("same-scope duplicate overlay identities are rejected")
{
    TempDir temp{};

    auto loadError = [&](const std::string &name, const std::string &body) -> std::string
    {
        const auto projectPath = temp.path() / name;
        WriteFile(projectPath, body);
        try
        {
            (void)LoadProjectManifest(projectPath);
        }
        catch (const std::exception &ex)
        {
            return ex.what();
        }
        return {};
    };

    const auto generatorError = loadError(
        "DuplicateGenerator.nginproj",
        R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="DuplicateGenerator.App">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
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
  </Application>
</Project>
)xml");
    REQUIRE_THAT(generatorError, ContainsSubstring("duplicate generator 'Codegen' in the same overlay scope"));

    const auto publishError = loadError(
        "DuplicatePublish.nginproj",
        R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="DuplicatePublish.App">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Publish Name="folder" Kind="Folder" Output="dist/a" />
    <Publish Name="folder" Kind="Folder" Output="dist/b" />
  </Application>
</Project>
)xml");
    REQUIRE_THAT(publishError, ContainsSubstring("duplicate publish 'folder' in the same overlay scope"));

    const auto packageOutputError = loadError(
        "DuplicatePackageOutput.nginproj",
        R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="DuplicatePackageOutput.Library">
  <Library>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <PackageOutput Name="Output.Core" Version="1.0.0" />
    <PackageOutput Name="Output.Core" Version="2.0.0" />
  </Library>
</Project>
)xml");
    REQUIRE_THAT(packageOutputError, ContainsSubstring("duplicate package output 'Output.Core' in the same overlay scope"));

    const auto launchError = loadError(
        "DuplicateLaunch.nginproj",
        R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="DuplicateLaunch.App">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Launch Name="app" Args="--a" />
    <Launch Name="app" Args="--b" />
  </Application>
</Project>
)xml");
    REQUIRE_THAT(launchError, ContainsSubstring("duplicate launch 'app' in the same overlay scope"));

    const auto runtimeError = loadError(
        "DuplicateRuntime.nginproj",
        R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="DuplicateRuntime.App">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Runtime>
      <Module Name="App.Startup" />
      <Module Name="App.Startup" />
    </Runtime>
  </Application>
</Project>
)xml");
    REQUIRE_THAT(runtimeError, ContainsSubstring("duplicate runtime module 'App.Startup' in the same overlay scope"));

    const auto environmentError = loadError(
        "DuplicateEnvironment.nginproj",
        R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="DuplicateEnvironment.App">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Environment>
      <Env Name="APP_ENV" Value="dev" />
      <Secret Name="APP_ENV" From="local:app.env" />
    </Environment>
  </Application>
</Project>
)xml");
    REQUIRE_THAT(environmentError, ContainsSubstring("duplicate environment variable 'APP_ENV' in the same overlay scope"));
}
