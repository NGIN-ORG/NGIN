#include "TestSupport.hpp"

TEST_CASE("inspect emits product identity")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Hello.Native.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Hello.Native">
  <Application />
</Project>
)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");

    ParsedArgs args{};
    args.projectPath = projectPath.string();
    args.format = "json";

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdInspect(temp.path(), args);
    std::cout.rdbuf(previous);

    const auto json = captured.str();
    REQUIRE(exitCode == 0);
    REQUIRE_THAT(json, ContainsSubstring(R"("schemaVersion": "4.0")"));
    REQUIRE_THAT(json, ContainsSubstring(R"("kind": "NGIN.CompositionGraph")"));
    REQUIRE_THAT(json, ContainsSubstring(R"("identity": {"project":"Hello.Native")"));
    REQUIRE_THAT(json, ContainsSubstring(R"("product":"Application","profile":"dev")"));
    REQUIRE_THAT(json, ContainsSubstring(R"("conventions": [)"));
    REQUIRE_THAT(json, ContainsSubstring(R"("name":"NGIN.Cpp.Defaults")"));
    REQUIRE_THAT(json, ContainsSubstring(R"("properties": [)"));
    REQUIRE_THAT(json, ContainsSubstring(R"("sourceKind":"convention")"));
    REQUIRE_THAT(json, ContainsSubstring(R"("reason":"selected by named language convention")"));
    REQUIRE_THAT(json, ContainsSubstring(R"("selection": {"profile":"dev","hostPlatform":"host","targetPlatform":"linux-x64")"));
    REQUIRE_THAT(json, ContainsSubstring(R"("facetsSummary":)"));
    REQUIRE_THAT(json, ContainsSubstring(R"("sources":1)"));
    REQUIRE_THAT(json, ContainsSubstring(R"("plans": {"packages":)"));
    REQUIRE_THAT(json, ContainsSubstring(R"("build":{"defines":[)"));
    REQUIRE_THAT(json, ContainsSubstring(R"("kind":"Source","role":"Source","source":"src")"));
    REQUIRE_THAT(json, ContainsSubstring(R"("stage":{"files":[])"));
    REQUIRE_THAT(json, ContainsSubstring(R"("product": {"kind":"Application")"));
    REQUIRE_THAT(json, ContainsSubstring(R"("kind":"Application")"));
    REQUIRE_THAT(json, ContainsSubstring(R"("outputType":"Executable")"));
    REQUIRE_THAT(json, ContainsSubstring(R"("outputName":"Hello.Native")"));
}

TEST_CASE("diff compares resolved profile graph slices")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Diff.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Diff.App" DefaultProfile="dev">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Launch Name="app" Args="--base" />
  </Application>
  <Profile Name="dev">
    <Defaults>
      <BuildType Name="Debug" />
      <Toolchain Name="clang-lld" />
      <Environment Name="development" />
    </Defaults>
    <Application>
      <Build>
        <Define Name="GAME_DEBUG" Value="1" />
      </Build>
      <Stage>
        <Config Source="config/dev.json" Target="config/app.json" />
      </Stage>
      <Environment>
        <Env Name="GAME_ENV" Value="development" />
      </Environment>
      <Quality>
        <Analyzer Name="clang-tidy" Severity="Warning" />
      </Quality>
      <Launch Name="app" Args="--dev" />
    </Application>
  </Profile>
  <Profile Name="shipping">
    <Defaults>
      <BuildType Name="Release" />
      <Environment Name="production" />
    </Defaults>
    <Application>
      <Build>
        <Define Name="GAME_SHIPPING" Value="1" />
      </Build>
      <Stage>
        <Config Source="config/prod.json" Target="config/app.json" />
      </Stage>
      <Environment>
        <Env Name="GAME_ENV" Value="production" />
      </Environment>
      <Quality>
        <Analyzer Name="clang-tidy" Severity="Error" />
      </Quality>
      <Launch Name="app" Args="--shipping" />
    </Application>
  </Profile>
</Project>
)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");
    WriteFile(temp.path() / "config/dev.json", "{}\n");
    WriteFile(temp.path() / "config/prod.json", "{}\n");

    ParsedArgs args{};
    args.projectPath = projectPath.string();
    args.fromProfileName = "dev";
    args.toProfileName = "shipping";

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdDiff(temp.path(), args);
    std::cout.rdbuf(previous);

    const auto diff = captured.str();
    REQUIRE(exitCode == 0);
    REQUIRE_THAT(diff, ContainsSubstring("BuildType changed: Debug -> Release"));
    REQUIRE_THAT(diff, ContainsSubstring("Defines added:"));
    REQUIRE_THAT(diff, ContainsSubstring("+ GAME_SHIPPING=1"));
    REQUIRE_THAT(diff, ContainsSubstring("Defines removed:"));
    REQUIRE_THAT(diff, ContainsSubstring("- GAME_DEBUG=1"));
    REQUIRE_THAT(diff, ContainsSubstring("config/app.json changed: config/dev.json -> config/prod.json"));
    REQUIRE_THAT(diff, ContainsSubstring("GAME_ENV changed: development -> production"));
    REQUIRE_THAT(diff, ContainsSubstring("Args changed: --dev -> --shipping"));
    REQUIRE_THAT(diff, ContainsSubstring("Analyzers added:"));
    REQUIRE_THAT(diff, ContainsSubstring("+ clang-tidy scope=Build severity=Error"));
    REQUIRE_THAT(diff, ContainsSubstring("Analyzers removed:"));
    REQUIRE_THAT(diff, ContainsSubstring("- clang-tidy scope=Build severity=Warning"));
}

TEST_CASE("diff compares package lock files")
{
    TempDir temp{};
    const auto oldLock = temp.path() / "old.ngin.lock";
    const auto newLock = temp.path() / "new.ngin.lock";
    WriteFile(oldLock,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<LockFile SchemaVersion="1" Project="App" Profile="dev">
  <Packages>
    <Package Name="Package.Core" Version="1.0.0" Source="local" Scope="Target" />
    <Package Name="Package.Old" Version="0.9.0" Source="local" Scope="Runtime" />
  </Packages>
</LockFile>
)xml");
    WriteFile(newLock,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<LockFile SchemaVersion="1" Project="App" Profile="dev">
  <Packages>
    <Package Name="Package.Added" Version="2.0.0" Source="local" Scope="Build" />
    <Package Name="Package.Core" Version="1.1.0" Source="feed" Scope="Target;Runtime" />
  </Packages>
</LockFile>
)xml");

    ParsedArgs args{};
    args.fromLockPath = oldLock.string();
    args.toLockPath = newLock.string();

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdDiff(temp.path(), args);
    std::cout.rdbuf(previous);

    const auto diff = captured.str();
    REQUIRE(exitCode == 0);
    REQUIRE_THAT(diff, ContainsSubstring("Lock diff"));
    REQUIRE_THAT(diff, ContainsSubstring("Package added: Package.Added 2.0.0"));
    REQUIRE_THAT(diff, ContainsSubstring("Package removed: Package.Old 0.9.0"));
    REQUIRE_THAT(diff, ContainsSubstring("Package changed: Package.Core 1.0.0 -> 1.1.0"));
    REQUIRE_THAT(diff, ContainsSubstring("Package scope changed: Package.Core Target -> Target;Runtime"));
    REQUIRE_THAT(diff, ContainsSubstring("Package source changed: Package.Core local -> feed"));
}

TEST_CASE("explain object syntax answers resolved graph objects")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Explain.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Explain.App" DefaultProfile="dev">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
      <Define Name="EXPLAIN_APP" Value="1" />
    </Build>
    <Stage>
      <Config Source="config/app.json" Target="config/app.json" />
    </Stage>
    <Launch Name="app" Args="--config config/app.json" />
    <Environment>
      <Env Name="EXPLAIN_ENV" Value="dev" />
    </Environment>
    <Quality>
      <Analyzer Name="clang-tidy" Severity="Error" />
    </Quality>
    <Publish Name="folder" Kind="Folder" Output="dist/Plan.App">
      <Include Stage="all" Symbols="false" />
    </Publish>
    <Publish Name="archive" Kind="Archive" Format="zip" Output="dist/Plan.App.zip">
      <Include Stage="all" RuntimeDependencies="true" />
    </Publish>
    <PackageOutput Name="Explain.App" Version="1.0.0">
      <Metadata>
        <Description>Explainable package output.</Description>
        <License>MIT</License>
      </Metadata>
      <Exports>
        <Capability Name="Explain.App" />
      </Exports>
    </PackageOutput>
  </Application>
  <Profile Name="dev">
    <Defaults>
      <BuildType Name="Debug" />
      <Toolchain Name="clang-lld" />
      <Environment Name="development" />
    </Defaults>
  </Profile>
</Project>
)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");
    WriteFile(temp.path() / "config/app.json", "{}\n");

    auto explain = [&](std::string object)
    {
        ParsedArgs args{};
        args.projectPath = projectPath.string();
        args.profileName = "dev";
        args.packageName = std::move(object);

        std::ostringstream captured{};
        auto *previous = std::cout.rdbuf(captured.rdbuf());
        const auto exitCode = CmdExplainObject(temp.path(), args);
        std::cout.rdbuf(previous);
        REQUIRE(exitCode == 0);
        return captured.str();
    };

    REQUIRE_THAT(explain("property:BuildType"), ContainsSubstring("value: Debug"));
    REQUIRE_THAT(explain("property:Toolchain"), ContainsSubstring("value: clang-lld"));
    REQUIRE_THAT(explain("property:Language"), ContainsSubstring("convention: NGIN.Cpp.Defaults"));
    REQUIRE_THAT(explain("convention:NGIN.Cpp.Defaults"), ContainsSubstring("reason: project did not declare a language override"));
    REQUIRE_THAT(explain("convention:NGIN.Cpp.Defaults"), ContainsSubstring("provenance: convention NGIN.Cpp.Defaults"));
    REQUIRE_THAT(explain("define:EXPLAIN_APP"), ContainsSubstring("value: EXPLAIN_APP=1"));
    REQUIRE_THAT(explain("source:src/main.cpp"), ContainsSubstring("role: Source"));
    REQUIRE_THAT(explain("stage:config/app.json"), ContainsSubstring("source: config/app.json"));
    REQUIRE_THAT(explain("launch:app"), ContainsSubstring("args: --config config/app.json"));
    REQUIRE_THAT(explain("publish:archive"), ContainsSubstring("format: zip"));
    REQUIRE_THAT(explain("publish:archive"), ContainsSubstring("includeRuntimeDependencies: true"));
    REQUIRE_THAT(explain("package-output:Explain.App"), ContainsSubstring("description: Explainable package output."));
    REQUIRE_THAT(explain("package-output:Explain.App"), ContainsSubstring("capabilities: 1"));
    REQUIRE_THAT(explain("env:EXPLAIN_ENV"), ContainsSubstring("value: dev"));
    REQUIRE_THAT(explain("analyzer:clang-tidy"), ContainsSubstring("severity: Error"));
}

TEST_CASE("graph plan switches print focused resolved plans")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Plan.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Plan.App" DefaultProfile="dev">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
      <Define Name="PLAN_APP" Value="1" />
    </Build>
    <Stage>
      <Config Source="config/app.json" Target="config/app.json" />
    </Stage>
    <Launch Name="app" Args="--config config/app.json" />
    <Environment>
      <Env Name="PLAN_ENV" Value="dev" />
      <Secret Name="PLAN_TOKEN" From="local:plan.token" Required="false" />
    </Environment>
    <Quality>
      <Analyzer Name="clang-tidy" Severity="Error" />
    </Quality>
    <Publish Name="folder" Kind="Folder" Output="dist/Plan.App">
      <Include Stage="all" Symbols="false" />
    </Publish>
    <PackageOutput Name="Plan.App" Version="1.0.0">
      <Metadata>
        <Description>Planned package output.</Description>
        <License>MIT</License>
      </Metadata>
      <Exports>
        <Capability Name="Plan.App" />
      </Exports>
    </PackageOutput>
  </Application>
  <Profile Name="dev">
    <Defaults>
      <BuildType Name="Debug" />
      <Environment Name="development" />
    </Defaults>
  </Profile>
</Project>
)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");
    WriteFile(temp.path() / "config/app.json", "{}\n");

    auto graph = [&](std::string plan)
    {
        ParsedArgs args{};
        args.projectPath = projectPath.string();
        args.profileName = "dev";
        args.graphPlan = std::move(plan);

        std::ostringstream captured{};
        auto *previous = std::cout.rdbuf(captured.rdbuf());
        const auto exitCode = CmdGraph(temp.path(), args);
        std::cout.rdbuf(previous);
        REQUIRE(exitCode == 0);
        return captured.str();
    };

    REQUIRE_THAT(graph("build"), ContainsSubstring("Build plan for profile: dev"));
    REQUIRE_THAT(graph("build"), ContainsSubstring("PLAN_APP=1"));
    REQUIRE_THAT(graph("stage"), ContainsSubstring("config/app.json <- config/app.json"));
    REQUIRE_THAT(graph("launch"), ContainsSubstring("name: app"));
    REQUIRE_THAT(graph("environment"), ContainsSubstring("env PLAN_ENV=dev"));
    REQUIRE_THAT(graph("environment"), ContainsSubstring("env PLAN_TOKEN=<redacted>"));
    REQUIRE_THAT(graph("package-output"), ContainsSubstring("package-output Plan.App version=1.0.0"));
    REQUIRE_THAT(graph("package-output"), ContainsSubstring("capabilities=1"));
    REQUIRE_THAT(graph("publish"), ContainsSubstring("publish folder kind=Folder output=dist/Plan.App"));
    REQUIRE_THAT(graph("quality"), ContainsSubstring("analyzer clang-tidy scope=Build severity=Error"));

    ParsedArgs jsonArgs{};
    jsonArgs.projectPath = projectPath.string();
    jsonArgs.profileName = "dev";
    jsonArgs.format = "json";
    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdGraph(temp.path(), jsonArgs);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("schemaVersion": "4.0")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("kind": "NGIN.CompositionGraph")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("identity": {"project":"Plan.App")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("product":"Application","profile":"dev")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("conventions": [)"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("name":"NGIN.Cpp.Defaults")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("properties": [)"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("sourceKind":"convention")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("sourceKind":"project","sourceName":"Plan.App")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("plans": {)"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("packageOutputs":[{"name":"Plan.App")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("stage":{"files":[)"));

    ParsedArgs stageJsonArgs{};
    stageJsonArgs.projectPath = projectPath.string();
    stageJsonArgs.profileName = "dev";
    stageJsonArgs.format = "json";
    stageJsonArgs.graphPlan = "stage";
    std::ostringstream stageJsonCaptured{};
    previous = std::cout.rdbuf(stageJsonCaptured.rdbuf());
    const auto stageJsonExitCode = CmdGraph(temp.path(), stageJsonArgs);
    std::cout.rdbuf(previous);

    REQUIRE(stageJsonExitCode == 0);
    REQUIRE_THAT(stageJsonCaptured.str(), ContainsSubstring(R"("kind": "NGIN.CompositionGraphPlan")"));
    REQUIRE_THAT(stageJsonCaptured.str(), ContainsSubstring(R"("plan": "stage")"));
    REQUIRE_THAT(stageJsonCaptured.str(), ContainsSubstring(R"("target":"config/app.json")"));
    REQUIRE_THAT(stageJsonCaptured.str(), ContainsSubstring(R"("provenance":{"sourceKind":"project","sourceName":"Plan.App")"));

    ParsedArgs environmentJsonArgs{};
    environmentJsonArgs.projectPath = projectPath.string();
    environmentJsonArgs.profileName = "dev";
    environmentJsonArgs.format = "json";
    environmentJsonArgs.graphPlan = "environment";
    std::ostringstream environmentJsonCaptured{};
    previous = std::cout.rdbuf(environmentJsonCaptured.rdbuf());
    const auto environmentJsonExitCode = CmdGraph(temp.path(), environmentJsonArgs);
    std::cout.rdbuf(previous);

    REQUIRE(environmentJsonExitCode == 0);
    REQUIRE_THAT(environmentJsonCaptured.str(), ContainsSubstring(R"("plan": "environment")"));
    REQUIRE_THAT(environmentJsonCaptured.str(), ContainsSubstring(R"("name":"PLAN_ENV")"));
    REQUIRE_THAT(environmentJsonCaptured.str(), ContainsSubstring(R"("name":"PLAN_TOKEN","value":"<redacted>","secret":true)"));
    REQUIRE_THAT(environmentJsonCaptured.str(), ContainsSubstring(R"("reason":"secret environment contribution")"));

    ParsedArgs packageOutputJsonArgs{};
    packageOutputJsonArgs.projectPath = projectPath.string();
    packageOutputJsonArgs.profileName = "dev";
    packageOutputJsonArgs.format = "json";
    packageOutputJsonArgs.graphPlan = "package-output";
    std::ostringstream packageOutputJsonCaptured{};
    previous = std::cout.rdbuf(packageOutputJsonCaptured.rdbuf());
    const auto packageOutputJsonExitCode = CmdGraph(temp.path(), packageOutputJsonArgs);
    std::cout.rdbuf(previous);

    REQUIRE(packageOutputJsonExitCode == 0);
    REQUIRE_THAT(packageOutputJsonCaptured.str(), ContainsSubstring(R"("plan": "package-output")"));
    REQUIRE_THAT(packageOutputJsonCaptured.str(), ContainsSubstring(R"("name":"Plan.App","version":"1.0.0")"));
    REQUIRE_THAT(packageOutputJsonCaptured.str(), ContainsSubstring(R"("capabilities":1)"));
    REQUIRE_THAT(packageOutputJsonCaptured.str(), ContainsSubstring(R"("reason":"source product package output")"));

    ParsedArgs buildJsonArgs{};
    buildJsonArgs.projectPath = projectPath.string();
    buildJsonArgs.profileName = "dev";
    buildJsonArgs.format = "json";
    buildJsonArgs.graphPlan = "build";
    std::ostringstream buildJsonCaptured{};
    previous = std::cout.rdbuf(buildJsonCaptured.rdbuf());
    const auto buildJsonExitCode = CmdGraph(temp.path(), buildJsonArgs);
    std::cout.rdbuf(previous);

    REQUIRE(buildJsonExitCode == 0);
    REQUIRE_THAT(buildJsonCaptured.str(), ContainsSubstring(R"("plan": "build")"));
    REQUIRE_THAT(buildJsonCaptured.str(), ContainsSubstring(R"("value":"PLAN_APP=1")"));
    REQUIRE_THAT(buildJsonCaptured.str(), ContainsSubstring(R"("reason":"selected compile definition")"));
    REQUIRE_THAT(buildJsonCaptured.str(), ContainsSubstring(R"("kind":"Source","role":"Source","source":"src/main.cpp")"));
}
