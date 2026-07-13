#include "TestSupport.hpp"

TEST_CASE("workspace, project, and package manifests parse through authoring "
          "facades") {
  TempDir temp{};
  WriteFile(temp.path() / "Workspace.ngin",
            R"(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="TempWorkspace" PlatformVersion="0.1.0">
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
  <Packages>
    <Source Name="local" Path="Packages" />
    <Version Name="Package.Core" Range=">=1.0.0 &lt;2.0.0" />
  </Packages>
</Workspace>
)");
  WriteFile(temp.path() / "Packages/Sample/Sample.nginpkg",
            R"(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Sample.Package" Version="1.0.0">
  <Build Backend="CMake" Mode="Manual" />
  <Library Name="Sample.Package">
    <Exports>
      <LibraryTarget Name="Sample::Package" />
    </Exports>
  </Library>
</Package>
)");
  WriteFile(temp.path() / "App/App.nginproj",
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Sample.App" DefaultProfile="Runtime">
  <Application>
    <Uses>
      <Package Name="Sample.Package" Version=">=1.0.0 &lt;2.0.0" Scope="Target" />
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Launch Executable="$(OutputName)" WorkingDirectory="." />
  </Application>
  <Profile Name="Runtime">
    <Defaults>
      <BuildType Name="Debug" />
      <TargetPlatform Name="linux-x64" />
      <Environment Name="dev" />
    </Defaults>
  </Profile>
</Project>
)xml");
  WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

  const auto workspace = LoadWorkspaceManifest(temp.path());
  const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
  const auto package =
      LoadPackageManifest(temp.path() / "Packages/Sample/Sample.nginpkg");
  const auto catalog = LoadPackageCatalog(workspace, project.path);

  REQUIRE(workspace.name == "TempWorkspace");
  REQUIRE(project.name == "Sample.App");
  REQUIRE(package.name == "Sample.Package");
  REQUIRE(package.modules.empty());
  REQUIRE(catalog.contains("Sample.Package"));
}

TEST_CASE("conditions support When nodes and item selectors") {
  TempDir temp{};
  const auto projectPath = temp.path() / "Game.Server.nginproj";
  WriteFile(projectPath,
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Game.Server">
  <Conditions>
    <Condition Name="linux-debug">
      <All>
        <When OperatingSystem="linux" />
        <When BuildType="Debug" />
      </All>
    </Condition>
  </Conditions>
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
      <Define Name="GAME_LINUX_DEBUG" Value="1" When="linux-debug" />
    </Build>
  </Application>
</Project>
)xml");

  const auto project = LoadProjectManifest(projectPath);

  REQUIRE(std::any_of(project.conditions.begin(), project.conditions.end(),
                      [](const ConditionDefinition &condition) {
                        return condition.name == "linux-debug";
                      }));
  REQUIRE(project.build.compileDefinitions.size() == 1);
  REQUIRE(project.build.compileDefinitions[0].value == "GAME_LINUX_DEBUG=1");
  REQUIRE(project.build.compileDefinitions[0].selectors.conditionRefs.size() ==
          1);
  REQUIRE(project.build.compileDefinitions[0].selectors.conditionRefs[0] ==
          "linux-debug");
}

TEST_CASE("package manifest parses product exports and feature contributions") {
  TempDir temp{};
  const auto packagePath = temp.path() / "NGIN.Core.nginpkg";
  WriteFile(packagePath,
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="NGIN.Core" Version="0.1.0">
  <Metadata>
    <Description>Optional hosted runtime for NGIN applications.</Description>
  </Metadata>
  <Uses>
    <Package Name="NGIN.Base" Version="[0.1.0,0.2.0)" Scope="Target" />
  </Uses>
  <Library Name="NGIN.Core">
    <Exports>
      <Headers Path="include/**.hpp" />
      <Binary Path="lib/linux-x64/libNGIN.Core.a"
              TargetPlatform="linux-x64"
              Abi="linux-x64-clang18-libc++-cxx23" />
      <LibraryTarget Name="NGIN::Core" />
    </Exports>
  </Library>
  <Features>
    <Feature Name="Reflection">
      <Uses>
        <Tool Name="NGIN.Reflection.MetaGen" Version="[0.1.0,0.2.0)" Scope="Build" />
      </Uses>
      <Build>
        <Define Name="NGIN_CORE_REFLECTION" Value="1" Visibility="Public" />
      </Build>
      <Provides>
        <Capability Name="Reflection" />
      </Provides>
    </Feature>
  </Features>
</Package>
)xml");

  const auto package = LoadPackageManifest(packagePath);

  REQUIRE(package.name == "NGIN.Core");
  REQUIRE(package.version == "0.1.0");
  REQUIRE(package.dependencies.size() == 1);
  REQUIRE(package.dependencies[0].name == "NGIN.Base");
  REQUIRE(package.dependencies[0].scope == "Target");
  REQUIRE(package.artifacts.libraries.size() == 2);
  REQUIRE(package.artifacts.libraries[0].target == "NGIN::Core");
  REQUIRE(package.inputs.size() == 1);
  REQUIRE(package.inputs[0].kind == "Source");
  REQUIRE(package.inputs[0].role == "Header");
  REQUIRE(package.inputs[0].includePatterns[0] == "include/**.hpp");
  REQUIRE(package.features.size() == 1);
  REQUIRE(package.features[0].name == "Reflection");
  REQUIRE(package.features[0].packageRefs.size() == 1);
  REQUIRE(package.features[0].packageRefs[0].name == "NGIN.Reflection.MetaGen");
  REQUIRE(package.features[0].packageRefs[0].scope == "Build");
  REQUIRE(package.features[0].build.compileDefinitions.size() == 1);
  REQUIRE(package.features[0].build.compileDefinitions[0].value ==
          "NGIN_CORE_REFLECTION=1");
  REQUIRE(package.features[0].provides.size() == 1);
  REQUIRE(package.features[0].provides[0].name == "Reflection");
}

TEST_CASE("package manifest parses tool exports") {
  TempDir temp{};
  const auto packagePath = temp.path() / "NGIN.Reflection.MetaGen.nginpkg";
  WriteFile(packagePath,
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="NGIN.Reflection.MetaGen" Version="0.1.0">
  <Tool Name="NGIN.Reflection.MetaGen">
    <Exports>
      <Tool Name="NGIN.Reflection.MetaGen" Executable="bin/ngin-metagen" />
    </Exports>
  </Tool>
</Package>
)xml");

  const auto package = LoadPackageManifest(packagePath);

  REQUIRE(package.name == "NGIN.Reflection.MetaGen");
  REQUIRE(package.tools.size() == 1);
  REQUIRE(package.tools[0].name == "NGIN.Reflection.MetaGen");
  REQUIRE(package.tools[0].executable == "bin/ngin-metagen");
  REQUIRE(package.artifacts.executables.size() == 1);
  REQUIRE(package.artifacts.executables[0].name == "NGIN.Reflection.MetaGen");
}

TEST_CASE("tool actions reject capabilities not provided by their driver") {
  TempDir temp{};
  const auto packagePath = temp.path() / "Invalid.Tooling.nginpkg";
  WriteFile(packagePath,
            R"xml(<Package SchemaVersion="4" Name="Invalid.Tooling" Version="1.0.0">
  <Tool Name="Invalid.Tooling"><Exports><Tool Name="tool" Executable="tool" /></Exports></Tool>
  <ToolDrivers>
    <Driver Name="driver" Protocol="NGIN.ToolDriver/1" Executable="driver">
      <Capabilities><Capability Name="diagnostics" /></Capabilities>
    </Driver>
  </ToolDrivers>
  <ToolActions>
    <Action Name="format" Kind="Format" Tool="tool" Driver="driver">
      <Accepts Contract="files/v1" />
      <Capabilities><Capability Name="edits" /></Capabilities>
    </Action>
  </ToolActions>
</Package>)xml");

  REQUIRE_THROWS_WITH(LoadPackageManifest(packagePath),
                      ContainsSubstring("capability 'edits' not provided by driver 'driver'"));
}

TEST_CASE("tool actions declare explicit environment and secret requirements") {
  TempDir temp{};
  const auto packagePath = temp.path() / "Environment.Tooling.nginpkg";
  WriteFile(packagePath,
            R"xml(<Package SchemaVersion="4" Name="Environment.Tooling" Version="1.0.0">
  <Tool Name="Environment.Tooling"><Exports><Tool Name="tool" Executable="tool" /></Exports></Tool>
  <ToolDrivers><Driver Name="driver" Protocol="NGIN.ToolDriver/1" Executable="driver" /></ToolDrivers>
  <ToolActions>
    <Action Name="scan" Kind="Scan" Tool="tool" Driver="driver">
      <Accepts Contract="artifacts/v1" />
      <Environment>
        <Variable Name="SCAN_MODE" Required="false" />
        <Variable Name="SCAN_TOKEN" Secret="true" CacheKey="true" />
      </Environment>
    </Action>
  </ToolActions>
</Package>)xml");

  const auto package = LoadPackageManifest(packagePath);
  REQUIRE(package.toolActions.size() == 1);
  REQUIRE(package.toolActions[0].environment.size() == 2);
  REQUIRE_FALSE(package.toolActions[0].environment[0].required);
  REQUIRE(package.toolActions[0].environment[1].secret);
  REQUIRE(package.toolActions[0].environment[1].cacheKey);
}

TEST_CASE("tool runs parse dependency and scheduler resource declarations") {
  TempDir temp{};
  const auto projectPath = temp.path() / "Scheduler.App.nginproj";
  WriteFile(projectPath,
            R"xml(<Project SchemaVersion="4" Name="Scheduler.App">
  <Application>
    <Tooling>
      <Run Name="analyze" DisplayName="Project Analysis"
           Description="Analyze the selected project inputs."
           Action="Example.Tooling::analyze">
        <Input Contract="files/v1" Scope="Product" Merge="Append">
          <Include Path="src/**" />
        </Input>
        <Execution Jobs="2" Timeout="30s" Cache="ReadWrite"
                   FailureStrategy="DependencyAware" Weight="2"
                   MaxParallelism="3" ExclusiveResource="compiler-db" />
        <Policy Gate="true" FailOn="Warning">
          <Severity Rule="style-rule" To="info" />
          <Suppress Fingerprint="known-finding" Reason="accepted for now" Expires="2099-12-31" />
          <Budget Rule="security-rule" Max="0" />
        </Policy>
      </Run>
      <Run Name="report" Action="Example.Tooling::report">
        <DependsOn Run="analyze" />
      </Run>
    </Tooling>
  </Application>
</Project>)xml");

  const auto project = LoadProjectManifest(projectPath);
  REQUIRE(project.tooling.runs.size() == 2);
  const auto &analyze = project.tooling.runs[0];
  REQUIRE(analyze.displayName == "Project Analysis");
  REQUIRE(analyze.description == "Analyze the selected project inputs.");
  REQUIRE(analyze.input.merge == "Append");
  REQUIRE(analyze.input.scopeExplicit);
  REQUIRE(analyze.execution.weight == 2);
  REQUIRE(analyze.execution.maxParallelism == 3);
  REQUIRE(analyze.execution.exclusiveResource == "compiler-db");
  REQUIRE(analyze.policy.severityMappings.size() == 1);
  REQUIRE(analyze.policy.severityMappings[0].severity == "info");
  REQUIRE(analyze.policy.suppressions.size() == 1);
  REQUIRE(analyze.policy.suppressions[0].reason == "accepted for now");
  REQUIRE(analyze.policy.ruleBudgets.size() == 1);
  REQUIRE(analyze.policy.ruleBudgets[0].maximum == 0);
  REQUIRE(project.tooling.runs[1].dependencies == std::vector<std::string>{"analyze"});
}

TEST_CASE("package manifest parses external provider build metadata") {
  TempDir temp{};
  const auto packagePath = temp.path() / "fmt.nginpkg";
  WriteFile(packagePath,
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="fmt" Version="10.2.1">
  <Build Backend="CMake"
         Mode="FindPackage"
         Provider="vcpkg"
         ProviderPackage="fmt"
         ProviderVersion="10.2.1"
         CMakePackage="fmt" />
  <Library Name="fmt">
    <Exports>
      <LibraryTarget Name="fmt::fmt" />
    </Exports>
  </Library>
</Package>
)xml");

  const auto package = LoadPackageManifest(packagePath);

  REQUIRE(package.build.backend == "CMake");
  REQUIRE(package.build.mode == "FindPackage");
  REQUIRE(package.build.provider == "vcpkg");
  REQUIRE(package.build.providerPackage == "fmt");
  REQUIRE(package.build.providerVersion == "10.2.1");
  REQUIRE(package.build.cmakePackage == "fmt");
}

TEST_CASE("package manifest parses provider package metadata without provider "
          "binding") {
  TempDir temp{};
  const auto packagePath = temp.path() / "openssl.nginpkg";
  WriteFile(packagePath,
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="OpenSSL" Version="3.0.0">
  <Build Backend="CMake"
         Mode="FindPackage"
         ProviderPackage="openssl"
         ProviderVersion="3.0.0"
         CMakePackage="OpenSSL"
         Linkage="Static;Shared"
         RuntimeDeployment="PackageRuntimeLibraries"
         RuntimeArtifacts="libcrypto">
    <Options>
      <Option Name="NGIN_BASE_CRYPTO_WITH_OPENSSL" Value="ON" />
    </Options>
  </Build>
</Package>
)xml");

  const auto package = LoadPackageManifest(packagePath);

  REQUIRE(package.build.mode == "FindPackage");
  REQUIRE(package.build.provider.empty());
  REQUIRE(package.build.providerPackage == "openssl");
  REQUIRE(package.build.providerVersion == "3.0.0");
  REQUIRE(package.build.cmakePackage == "OpenSSL");
  REQUIRE(package.build.linkage == "Static;Shared");
  REQUIRE(package.build.runtimeDeployment == "PackageRuntimeLibraries");
  REQUIRE(package.build.runtimeArtifacts == "libcrypto");
  REQUIRE(package.build.options.size() == 1);
  REQUIRE(package.build.options[0].name == "NGIN_BASE_CRYPTO_WITH_OPENSSL");
  REQUIRE(package.build.options[0].value == "ON");
}

TEST_CASE(
    "package manifest rejects provider metadata outside FindPackage mode") {
  TempDir temp{};
  const auto packagePath = temp.path() / "bad.nginpkg";
  WriteFile(packagePath,
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="bad" Version="1.0.0">
  <Build Backend="CMake" Mode="AddSubdirectory" Provider="vcpkg" />
</Package>
)xml");

  REQUIRE_THROWS_WITH(
      LoadPackageManifest(packagePath),
      ContainsSubstring(
          R"(Provider is only supported with Mode="FindPackage")"));
}
