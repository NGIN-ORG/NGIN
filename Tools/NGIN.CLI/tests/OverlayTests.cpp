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
