#include "TestSupport.hpp"
#include "Overlay.hpp"

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
      <Uses>
        <Package Name="Package.Helper" Version="[1.0.0,2.0.0)" Scope="Build" />
      </Uses>
      <Build>
        <Define Name="PACKAGE_CORE_DIAGNOSTICS" Value="1" Visibility="Public" />
      </Build>
    </Feature>
  </Features>
</Package>
)xml");
    WriteFile(temp.path() / "Packages/Helper/Helper.nginpkg",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Package.Helper" Version="1.0.0">
  <Library Name="Package.Helper">
    <Exports>
      <LibraryTarget Name="Package::Helper" />
    </Exports>
  </Library>
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
    REQUIRE(shipping.value->orderedPackages.size() == 2);
    REQUIRE(std::any_of(shipping.value->orderedPackages.begin(), shipping.value->orderedPackages.end(),
                        [](const ResolvedPackage &package) {
                            return package.manifest.name == "Package.Core";
                        }));
    REQUIRE(std::any_of(shipping.value->orderedPackages.begin(), shipping.value->orderedPackages.end(),
                        [](const ResolvedPackage &package) {
                            return package.manifest.name == "Package.Helper";
                        }));
    REQUIRE(shipping.value->packageScopes.at("Package.Helper") == "Build");
    REQUIRE(shipping.value->selectedPackageFeatures.size() == 1);
    REQUIRE(shipping.value->selectedPackageFeatures[0].packageName == "Package.Core");
    REQUIRE(shipping.value->selectedPackageFeatures[0].featureName == "Diagnostics");
}

TEST_CASE("tool run overlays append input filters and merge named configs and reports")
{
    ProjectManifest project{};
    project.name = "Overlay.App";
    ToolRunDefinition base{};
    base.name = "audit";
    base.displayName = "Base Audit";
    base.action = "Example.Tooling::analyze";
    base.hasInput = true;
    base.input.contract = "files/v1";
    base.input.scope = "ProductClosure";
    base.input.includes = {"src/**"};
    base.input.excludes = {"src/vendor/**"};
    base.configs = {
        ToolConfigDefinition{.name = "primary", .path = "base.yml"},
        ToolConfigDefinition{.name = "secondary", .path = "secondary.yml"},
    };
    base.reports = {
        ToolReportDefinition{.name = "json", .format = "json", .path = "base.json"},
    };
    base.hasPolicy = true;
    base.policy.gate = false;
    base.policy.gateExplicit = true;
    base.policy.failOn = "Error";
    base.policy.failOnExplicit = true;
    base.policy.ruleBudgets.push_back({.rule = "security", .maximum = 0});
    base.hasExecution = true;
    base.execution.cache = "ReadWrite";
    base.execution.cacheExplicit = true;
    base.execution.weight = 4;
    base.execution.weightExplicit = true;
    base.provenance = {.sourceKind = "package-feature", .sourceName = "Example::Audit"};
    project.tooling.runs.push_back(base);

    ProfileDefinition profile{};
    profile.name = "ci";
    ToolRunDefinition overlay{};
    overlay.name = "audit";
    overlay.displayName = "CI Audit";
    overlay.hasInput = true;
    overlay.input.merge = "Append";
    overlay.input.includes = {"include/**"};
    overlay.input.excludes = {"generated/**"};
    overlay.configs = {
        ToolConfigDefinition{.name = "primary", .path = "ci.yml"},
    };
    overlay.reports = {
        ToolReportDefinition{.name = "json", .format = "json", .path = "ci.json"},
        ToolReportDefinition{.name = "sarif", .format = "sarif", .path = "ci.sarif"},
    };
    overlay.hasPolicy = true;
    overlay.policy.gate = true;
    overlay.policy.gateExplicit = true;
    overlay.hasExecution = true;
    overlay.execution.cache = "ReadOnly";
    overlay.execution.cacheExplicit = true;
    overlay.provenance = {.sourceKind = "project-profile", .sourceName = "ci"};
    profile.tooling.runs.push_back(overlay);

    const auto effective = EffectiveToolRuns(project, profile, {});
    REQUIRE(effective.size() == 1);
    const auto &run = effective.at("audit");
    REQUIRE(run.action == "Example.Tooling::analyze");
    REQUIRE(run.displayName == "CI Audit");
    REQUIRE(run.input.contract == "files/v1");
    REQUIRE(run.input.scope == "ProductClosure");
    REQUIRE(run.input.includes == std::vector<std::string>{"src/**", "include/**"});
    REQUIRE(run.input.excludes == std::vector<std::string>{"src/vendor/**", "generated/**"});
    REQUIRE(run.configs.size() == 2);
    REQUIRE(run.configs[0].name == "primary");
    REQUIRE(run.configs[0].path == "ci.yml");
    REQUIRE(run.configs[1].name == "secondary");
    REQUIRE(run.reports.size() == 2);
    REQUIRE(run.reports[0].path == "ci.json");
    REQUIRE(run.reports[1].format == "sarif");
    REQUIRE(run.policy.gate);
    REQUIRE(run.policy.failOn == "Error");
    REQUIRE(run.policy.ruleBudgets.size() == 1);
    REQUIRE(run.execution.cache == "ReadOnly");
    REQUIRE(run.execution.weight == 4);
    REQUIRE(run.originProvenance.sourceKind == "package-feature");
    REQUIRE(run.provenance.sourceKind == "project-profile");
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

TEST_CASE("package feature stage contributions keep package-feature provenance")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="FeatureStageWorkspace">
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
  <Packages>
    <Source Name="local" Path="Packages" />
  </Packages>
</Workspace>
)xml");
    WriteFile(temp.path() / "Packages/Assets/Assets.nginpkg",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Package.Assets" Version="1.0.0">
  <Library Name="Package.Assets">
    <Exports>
      <LibraryTarget Name="Package::Assets" />
    </Exports>
  </Library>
  <Features>
    <Feature Name="Config">
      <Inputs>
        <Configs>
          <File Path="content/feature.json" Target="config/feature.json" />
        </Configs>
      </Inputs>
    </Feature>
  </Features>
</Package>
)xml");
    WriteFile(temp.path() / "Packages/Assets/content/feature.json", "{}\n");
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="FeatureStage.App" DefaultProfile="dev">
  <Application>
    <Uses>
      <Package Name="Package.Assets" Version="[1.0.0,2.0.0)">
        <Feature Name="Config" />
      </Package>
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
  <Profile Name="dev" />
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    ParsedArgs args{};
    args.projectPath = (temp.path() / "App/App.nginproj").string();
    args.profileName = "dev";
    args.format = "json";
    args.graphPlan = "stage";

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdGraph(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("target":"config/feature.json")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("sourceKind":"package-feature")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("sourceName":"Package.Assets::Config")"));
}

TEST_CASE("profile overlays carry selectors and can override staged outputs "
          "and environment")
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
    REQUIRE(std::any_of(resolved.value->environmentVariables.begin(), resolved.value->environmentVariables.end(),
                        [](const EnvironmentVariable &variable) {
                            return variable.name == "GAME_ENV" && variable.value == "production";
                        }));
    REQUIRE(std::any_of(resolved.value->inputs.begin(), resolved.value->inputs.end(), [](const ResolvedInput &input) {
        return input.kind == "Config" && input.source == "config/prod.json" &&
               input.stagedRelativePath == fs::path("config/app.json");
    }));
    REQUIRE_FALSE(
        std::any_of(resolved.value->inputs.begin(), resolved.value->inputs.end(), [](const ResolvedInput &input) {
            return input.kind == "Config" && input.source == "config/base.json";
        }));
}

TEST_CASE("profile build overlays replace and remove by identity")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "BuildOverlay.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="BuildOverlay.App" DefaultProfile="shipping">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
      <Define Name="APP_MODE" Value="dev" />
      <Define Name="APP_DEBUG" Value="1" />
      <CompileOption Value="-Wextra" />
    </Build>
  </Application>
  <Profile Name="shipping">
    <Application>
      <Build>
        <Define Name="APP_MODE" Value="shipping" />
        <Define Remove="APP_DEBUG" />
        <CompileOption Remove="-Wextra" />
      </Build>
    </Application>
  </Profile>
</Project>
)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");

    ParsedArgs graphArgs{};
    graphArgs.projectPath = projectPath.string();
    graphArgs.profileName = "shipping";
    graphArgs.format = "json";

    std::ostringstream graphCaptured{};
    auto *previous = std::cout.rdbuf(graphCaptured.rdbuf());
    const auto graphExitCode = CmdGraph(temp.path(), graphArgs);
    std::cout.rdbuf(previous);

    REQUIRE(graphExitCode == 0);
    REQUIRE_THAT(graphCaptured.str(), ContainsSubstring("APP_MODE=shipping"));
    REQUIRE(graphCaptured.str().find("APP_MODE=dev") == std::string::npos);
    REQUIRE(graphCaptured.str().find("APP_DEBUG=1") == std::string::npos);

    ParsedArgs explainArgs{};
    explainArgs.projectPath = projectPath.string();
    explainArgs.profileName = "shipping";
    explainArgs.packageName = "define:APP_MODE";

    std::ostringstream explainCaptured{};
    previous = std::cout.rdbuf(explainCaptured.rdbuf());
    const auto explainExitCode = CmdExplainObject(temp.path(), explainArgs);
    std::cout.rdbuf(previous);

    REQUIRE(explainExitCode == 0);
    REQUIRE_THAT(explainCaptured.str(), ContainsSubstring("value: APP_MODE=shipping"));
    REQUIRE_THAT(explainCaptured.str(), ContainsSubstring("owner: project-profile shipping"));
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
    REQUIRE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const std::string &message) {
        return message.find("input destination collision at 'config/app.json'") != std::string::npos;
    }));
}

TEST_CASE("profile overlays remove inherited staged outputs by target identity")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "StageRemove.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="StageRemove.App" DefaultProfile="dev">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Stage>
      <Config Source="config/base.json" Target="config/app.json" />
    </Stage>
  </Application>
  <Profile Name="dev">
    <Application>
      <Stage>
        <Config Remove="config/app.json" />
      </Stage>
    </Application>
  </Profile>
</Project>
)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");
    WriteFile(temp.path() / "config/base.json", "{}\n");

    const auto project = LoadProjectManifest(projectPath);
    const auto resolved = ResolveLaunch(project, ProfileByName(project, "dev"));

    REQUIRE_FALSE(resolved.diagnostics.HasErrors());
    REQUIRE(resolved.value.has_value());
    REQUIRE_FALSE(std::any_of(resolved.value->inputs.begin(), resolved.value->inputs.end(),
                              [](const ResolvedInput &input) {
                                  return input.kind == "Config" &&
                                         input.stagedRelativePath == fs::path("config/app.json");
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
    REQUIRE(resolved.value->generators[0].declaration.outputs[0].includePatterns[0] ==
            "$(GeneratedDir)/reflection/shipping/**.cpp");
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

    auto loadError = [&](const std::string &name, const std::string &body) -> std::string {
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

    const auto defineError = loadError("DuplicateDefine.nginproj",
                                       R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="DuplicateDefine.App">
  <Application>
    <Build>
      <Define Name="APP_MODE" Value="dev" />
      <Define Name="APP_MODE" Value="shipping" />
    </Build>
  </Application>
</Project>
)xml");
    REQUIRE_THAT(defineError, ContainsSubstring("duplicate define 'APP_MODE' in the same overlay scope"));

    const auto includePathError = loadError("DuplicateIncludePath.nginproj",
                                            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="DuplicateIncludePath.App">
  <Application>
    <Build>
      <IncludePath Path="include/../include" Visibility="Public" />
      <IncludePath Path="include" Visibility="Public" />
    </Build>
  </Application>
</Project>
)xml");
    REQUIRE_THAT(includePathError,
                 ContainsSubstring("duplicate include path 'include|Public' in the same overlay scope"));

    const auto compileOptionError = loadError("DuplicateCompileOption.nginproj",
                                              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="DuplicateCompileOption.App">
  <Application>
    <Build>
      <CompileOption Value="-Wall" />
      <CompileOption Value="-Wall" />
    </Build>
  </Application>
</Project>
)xml");
    REQUIRE_THAT(compileOptionError, ContainsSubstring("duplicate compile option '-Wall' in the same overlay scope"));

    const auto linkOptionError = loadError("DuplicateLinkOption.nginproj",
                                           R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="DuplicateLinkOption.App">
  <Application>
    <Build>
      <LinkLibrary Name="m" />
      <LinkOption Value="m" />
    </Build>
  </Application>
</Project>
)xml");
    REQUIRE_THAT(linkOptionError, ContainsSubstring("duplicate link option 'm' in the same overlay scope"));

    const auto toolRunError = loadError("DuplicateToolRun.nginproj",
                                         R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="DuplicateToolRun.App">
  <Application>
    <Tooling>
      <Run Name="static-analysis" Action="Example.Tooling::analyze" />
      <Run Name="static-analysis" Action="Example.Tooling::scan" />
    </Tooling>
  </Application>
</Project>
)xml");
    REQUIRE_THAT(toolRunError, ContainsSubstring("duplicate tool run 'static-analysis' in the same overlay scope"));

    const auto generatorError = loadError("DuplicateGenerator.nginproj",
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

    const auto publishError = loadError("DuplicatePublish.nginproj",
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

    const auto packageOutputError = loadError("DuplicatePackageOutput.nginproj",
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
    REQUIRE_THAT(packageOutputError,
                 ContainsSubstring("duplicate package output 'Output.Core' in the same overlay scope"));

    const auto launchError = loadError("DuplicateLaunch.nginproj",
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

    const auto runtimeError = loadError("DuplicateRuntime.nginproj",
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

    const auto environmentError = loadError("DuplicateEnvironment.nginproj",
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
    REQUIRE_THAT(environmentError, ContainsSubstring("duplicate environment variable 'APP_ENV' in "
                                                     "the same overlay scope"));
}
