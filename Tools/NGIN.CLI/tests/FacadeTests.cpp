#include "TestSupport.hpp"

TEST_CASE("test command builds and runs test product")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Arg.Tests.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Arg.Tests">
  <Test>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Run Name="unit" Args="--suite unit" />
  </Test>
</Project>
)xml");
    WriteFile(temp.path() / "src/main.cpp",
              R"cpp(#include <string>
int main(int argc, char **argv) {
    return argc == 3 && std::string(argv[1]) == "--suite" && std::string(argv[2]) == "unit" ? 0 : 7;
}
)cpp");

    ParsedArgs args{};
    args.projectPath = projectPath.string();
    args.outputPath = (temp.path() / "out").string();

    REQUIRE(CmdTest(temp.path(), args) == 0);
}

TEST_CASE("benchmark command builds and runs benchmark product")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Perf.Benchmarks.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Perf.Benchmarks">
  <Benchmark>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Run Name="bench" />
  </Benchmark>
</Project>
)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");

    ParsedArgs args{};
    args.projectPath = projectPath.string();
    args.outputPath = (temp.path() / "out").string();

    REQUIRE(CmdBenchmark(temp.path(), args) == 0);
}

TEST_CASE("stage command builds and reports staged output")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Stage.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Stage.App">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Stage>
      <Config Source="config/app.json" Target="config/app.json" />
    </Stage>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");
    WriteFile(temp.path() / "config/app.json", "{}\n");

    ParsedArgs args{};
    args.projectPath = projectPath.string();
    args.outputPath = (temp.path() / "out").string();

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdStage(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring("NGIN stage"));
    REQUIRE(fs::exists(temp.path() / "out/config/app.json"));
}

TEST_CASE("analyze command reports declared non-clang analyzer plan")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Analyze.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Analyze.App">
  <Application>
    <Quality>
      <Analyzer Name="example-analyzer" Scope="Build" Enabled="true" Severity="Error">
        <Config Path="analyzer.cfg" />
      </Analyzer>
    </Quality>
  </Application>
</Project>
)xml");

    const auto project = LoadProjectManifest(projectPath);
    REQUIRE(project.quality.analyzers.size() == 1);
    REQUIRE(project.quality.analyzers[0].name == "example-analyzer");
    REQUIRE(project.quality.analyzers[0].severity == "Error");
    REQUIRE(project.quality.analyzers[0].configPath == "analyzer.cfg");

    ParsedArgs args{};
    args.projectPath = projectPath.string();

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdAnalyze(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring("NGIN analyze"));
    REQUIRE_THAT(captured.str(), ContainsSubstring("example-analyzer  scope=Build severity=Error config=analyzer.cfg"));

    ParsedArgs inspectArgs{};
    inspectArgs.projectPath = projectPath.string();
    inspectArgs.format = "json";
    std::ostringstream inspectCaptured{};
    previous = std::cout.rdbuf(inspectCaptured.rdbuf());
    const auto inspectExitCode = CmdInspect(temp.path(), inspectArgs);
    std::cout.rdbuf(previous);

    REQUIRE(inspectExitCode == 0);
    REQUIRE_THAT(inspectCaptured.str(), ContainsSubstring(R"("quality":{"analyzers":[)"));
    REQUIRE_THAT(inspectCaptured.str(), ContainsSubstring(R"("name":"example-analyzer")"));
    REQUIRE_THAT(inspectCaptured.str(), ContainsSubstring(R"("severity":"Error")"));
}

TEST_CASE("package feature analyzer contributes clang-tidy to graph")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="AnalyzeWorkspace">
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
  <Packages>
    <Source Name="local" Path="Packages" />
  </Packages>
</Workspace>
)xml");
    WriteFile(temp.path() / "Packages/NGIN.Tooling.ClangTidy/NGIN.Tooling.ClangTidy.nginpkg",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="NGIN.Tooling.ClangTidy" Version="0.1.0">
  <Tool Name="NGIN.Tooling.ClangTidy">
    <Exports>
      <Tool Name="clang-tidy" Kind="Analyzer" Executable="clang-tidy" />
    </Exports>
  </Tool>
  <Features>
    <Feature Name="Analyzer">
      <Quality>
        <Analyzer Name="clang-tidy" Tool="clang-tidy" Severity="Warning">
          <Config Path=".clang-tidy" Optional="true" />
        </Analyzer>
      </Quality>
    </Feature>
  </Features>
</Package>
)xml");
    const auto projectPath = temp.path() / "App/App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="PackageAnalyze.App">
  <Application>
    <Uses>
      <Package Name="NGIN.Tooling.ClangTidy" Version="[0.1.0,0.2.0)" Scope="Dev">
        <Feature Name="Analyzer" />
      </Package>
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    ParsedArgs args{};
    args.projectPath = projectPath.string();
    args.graphPlan = "quality";

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdGraph(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring("analyzer clang-tidy scope=Build severity=Warning config=.clang-tidy"));

    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="PackageAnalyze.App">
  <Application>
    <Uses>
      <Package Name="NGIN.Tooling.ClangTidy" Version="[0.1.0,0.2.0)" Scope="Dev">
        <Feature Name="Analyzer" />
      </Package>
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Quality>
      <Analyzer Name="clang-tidy" Severity="Error" />
    </Quality>
  </Application>
</Project>
)xml");

    ParsedArgs inspectArgs{};
    inspectArgs.projectPath = projectPath.string();
    inspectArgs.format = "json";
    std::ostringstream inspectCaptured{};
    previous = std::cout.rdbuf(inspectCaptured.rdbuf());
    const auto inspectExitCode = CmdInspect(temp.path(), inspectArgs);
    std::cout.rdbuf(previous);

    REQUIRE(inspectExitCode == 0);
    REQUIRE_THAT(inspectCaptured.str(), ContainsSubstring(R"("name":"clang-tidy","tool":"clang-tidy","package":"NGIN.Tooling.ClangTidy","scope":"Build","severity":"Error","configPath":".clang-tidy","configOptional":true)"));
}

#ifndef _WIN32
TEST_CASE("clang-tidy analyzer emits normalized warning diagnostics")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "AnalyzeWarn.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="AnalyzeWarn.App">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Quality>
      <Analyzer Name="clang-tidy" Severity="Warning" />
    </Quality>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");
    const auto fakeTool = temp.path() / "fake-clang-tidy";
    WriteFile(fakeTool,
              "#!/bin/sh\n"
              "last=\"\"\n"
              "for arg in \"$@\"; do last=\"$arg\"; done\n"
              "echo \"$last:4:7: warning: prefer auto [modernize-use-auto]\"\n"
              "exit 0\n");
    fs::permissions(fakeTool, fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write);
    ScopedEnvironmentVariable clangTidy{"NGIN_CLANG_TIDY", fakeTool.string()};

    ParsedArgs args{};
    args.projectPath = projectPath.string();
    args.outputPath = (temp.path() / "out").string();

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdAnalyze(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring("[warning] "));
    REQUIRE_THAT(captured.str(), ContainsSubstring(":4:7: prefer auto [clang-tidy:modernize-use-auto]"));
}

TEST_CASE("clang-tidy analyzer escalates diagnostics when severity is error")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "AnalyzeError.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="AnalyzeError.App">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Quality>
      <Analyzer Name="clang-tidy" Severity="Error" />
    </Quality>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");
    const auto fakeTool = temp.path() / "fake-clang-tidy";
    WriteFile(fakeTool,
              "#!/bin/sh\n"
              "last=\"\"\n"
              "for arg in \"$@\"; do last=\"$arg\"; done\n"
              "echo \"$last:2:3: warning: issue [readability-test]\"\n"
              "exit 0\n");
    fs::permissions(fakeTool, fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write);
    ScopedEnvironmentVariable clangTidy{"NGIN_CLANG_TIDY", fakeTool.string()};

    ParsedArgs args{};
    args.projectPath = projectPath.string();
    args.outputPath = (temp.path() / "out").string();

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdAnalyze(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 1);
    REQUIRE_THAT(captured.str(), ContainsSubstring("[error] "));
    REQUIRE_THAT(captured.str(), ContainsSubstring(":2:3: issue [clang-tidy:readability-test]"));
}

TEST_CASE("clang-tidy analyzer reports missing tool")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "AnalyzeMissingTool.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="AnalyzeMissingTool.App">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Quality>
      <Analyzer Name="clang-tidy" Severity="Warning" />
    </Quality>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");
    ScopedEnvironmentVariable clangTidy{"NGIN_CLANG_TIDY", (temp.path() / "missing-clang-tidy").string()};

    ParsedArgs args{};
    args.projectPath = projectPath.string();
    args.outputPath = (temp.path() / "out").string();

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdAnalyze(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 1);
    REQUIRE_THAT(captured.str(), ContainsSubstring("could not resolve clang-tidy"));
    REQUIRE_THAT(captured.str(), ContainsSubstring("NGIN_CLANG_TIDY"));
}
#endif

TEST_CASE("publish command writes folder publish output")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Publish.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Publish.App">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Stage>
      <Config Source="config/app.json" Target="config/app.json" />
    </Stage>
    <Publish Name="folder" Kind="Folder" Output="dist/Publish.App">
      <Include Stage="all" />
    </Publish>
    <Publish Name="archive" Kind="Archive" Format="zip" Output="dist/Publish.App.zip">
      <Include Stage="all" />
    </Publish>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");
    WriteFile(temp.path() / "config/app.json", "{}\n");

    ParsedArgs args{};
    args.projectPath = projectPath.string();
    args.packageName = "folder";
    args.outputPath = (temp.path() / "build").string();

    REQUIRE(CmdPublish(temp.path(), args) == 0);
    REQUIRE(fs::exists(temp.path() / "dist/Publish.App/config/app.json"));
    REQUIRE(fs::exists(temp.path() / "dist/Publish.App/bin/Publish.App"));

    WriteFile(temp.path() / "dist/Publish.App/stale.txt", "stale\n");
    REQUIRE(fs::exists(temp.path() / "dist/Publish.App/stale.txt"));

    REQUIRE(CmdPublish(temp.path(), args) == 0);
    REQUIRE(fs::exists(temp.path() / "dist/Publish.App/config/app.json"));
    REQUIRE_FALSE(fs::exists(temp.path() / "dist/Publish.App/stale.txt"));

    ParsedArgs archiveArgs{};
    archiveArgs.projectPath = projectPath.string();
    archiveArgs.packageName = "archive";
    archiveArgs.outputPath = (temp.path() / "build-archive").string();

    REQUIRE(CmdPublish(temp.path(), archiveArgs) == 0);
    const auto archive = ReadFile(temp.path() / "dist/Publish.App.zip");
    REQUIRE(archive.rfind("PK\003\004", 0) == 0);
    REQUIRE_THAT(archive, ContainsSubstring("config/app.json"));
    REQUIRE_THAT(archive, ContainsSubstring("bin/Publish.App"));
}

TEST_CASE("launch manifests redact resolved secret variables")
{
    TempDir temp{};
    ResolvedLaunch launch{};
    launch.project.name = "Secret.App";
    launch.project.type = "Application";
    launch.profile.name = "Runtime";
    launch.profile.buildType = "Debug";
    launch.profile.operatingSystem = "linux";
    launch.profile.architecture = "x64";
    launch.profile.environmentName = "dev";
    launch.environmentVariables.push_back(EnvironmentVariable{
        .name = "API_TOKEN",
        .value = "super-secret",
        .required = true,
        .secret = true,
        .resolved = true,
        .resolvedSource = "local setting feeds.private.token",
    });
    fs::create_directories(temp.path() / "stage");

    const auto manifestPath = WriteLaunchManifest(launch, temp.path() / "stage", {});
    const auto manifest = ReadFile(manifestPath);

    REQUIRE_THAT(manifest, ContainsSubstring(R"(Name="API_TOKEN")"));
    REQUIRE_THAT(manifest, ContainsSubstring(R"(Secret="true")"));
    REQUIRE_THAT(manifest, !ContainsSubstring("super-secret"));
}

TEST_CASE("project autodiscovery resolves nearest nginproj in the current tree")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Nested/App.nginproj";
    WriteFile(projectPath,
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Nested" DefaultProfile="Runtime">
  <Application />
  <Profile Name="Runtime">
    <Defaults>
      <BuildType Name="Debug" />
      <TargetPlatform Name="linux-x64" />
      <Environment Name="dev" />
    </Defaults>
  </Profile>
</Project>
)");
    fs::create_directories(temp.path() / "Nested/Deep/Deeper");

    ScopedCurrentPath cwd(temp.path() / "Nested/Deep/Deeper");
    const auto resolved = ResolveProjectPath(std::nullopt);

    REQUIRE(fs::equivalent(resolved, projectPath));
}

TEST_CASE("build facade writes launch manifests and preserves unrelated output "
          "files")
{
    const auto projectPath = RepoRoot() / "Examples/Hello.Hosted/Hello.Hosted.nginproj";
    REQUIRE(fs::exists(projectPath));

    const auto project = LoadProjectManifest(projectPath);
    const std::optional<std::string> profileName{"Debug"};
    const auto &profile = ProfileByName(project, profileName);
    TempDir temp{};
    const auto outputDir = temp.path() / "stage";

    const auto firstBuild = BuildLaunch(project, profile, outputDir);
    REQUIRE(firstBuild.value.has_value());
    REQUIRE_FALSE(firstBuild.diagnostics.HasErrors());

    WriteFile(outputDir / "keep.txt", "preserve me\n");

    const auto secondBuild = BuildLaunch(project, profile, outputDir);
    REQUIRE(secondBuild.value.has_value());
    REQUIRE_FALSE(secondBuild.diagnostics.HasErrors());
    REQUIRE(fs::exists(outputDir / "keep.txt"));

    const auto summary = LoadLaunchManifestSummary(secondBuild.value->manifestPath);
    REQUIRE(summary.profileName == "Debug");
    REQUIRE(summary.selectedExecutable.has_value());
    REQUIRE(*summary.selectedExecutable == "Hello.Hosted");
}

TEST_CASE("clean facade removes owned generated artifacts and preserves "
          "unrelated files")
{
    const auto projectPath = RepoRoot() / "Examples/Hello.Hosted/Hello.Hosted.nginproj";
    REQUIRE(fs::exists(projectPath));

    const auto project = LoadProjectManifest(projectPath);
    const std::optional<std::string> profileName{"Debug"};
    const auto &profile = ProfileByName(project, profileName);
    TempDir temp{};
    const auto outputDir = temp.path() / "stage";

    const auto build = BuildLaunch(project, profile, outputDir);
    REQUIRE(build.value.has_value());
    REQUIRE_FALSE(build.diagnostics.HasErrors());

    WriteFile(outputDir / "keep.txt", "preserve me\n");

    const auto cleaned = CleanLaunch(project, profile, outputDir);
    REQUIRE(cleaned.value.has_value());
    REQUIRE_FALSE(cleaned.diagnostics.HasErrors());
    REQUIRE(fs::exists(outputDir / "keep.txt"));
    REQUIRE_FALSE(fs::exists(outputDir / "Hello.Hosted.Debug.nginlaunch"));
    REQUIRE_FALSE(fs::exists(outputDir / "bin" / "Hello.Hosted"));
}

TEST_CASE("rebuild semantics can be expressed as clean followed by build")
{
    const auto projectPath = RepoRoot() / "Examples/Hello.Hosted/Hello.Hosted.nginproj";
    REQUIRE(fs::exists(projectPath));

    const auto project = LoadProjectManifest(projectPath);
    const std::optional<std::string> profileName{"Debug"};
    const auto &profile = ProfileByName(project, profileName);
    TempDir temp{};
    const auto outputDir = temp.path() / "stage";

    const auto firstBuild = BuildLaunch(project, profile, outputDir);
    REQUIRE(firstBuild.value.has_value());
    REQUIRE_FALSE(firstBuild.diagnostics.HasErrors());

    const auto cleaned = CleanLaunch(project, profile, outputDir);
    REQUIRE(cleaned.value.has_value());
    REQUIRE_FALSE(cleaned.diagnostics.HasErrors());

    const auto rebuilt = BuildLaunch(project, profile, outputDir);
    REQUIRE(rebuilt.value.has_value());
    REQUIRE_FALSE(rebuilt.diagnostics.HasErrors());
    REQUIRE(fs::exists(rebuilt.value->manifestPath));

    const auto summary = LoadLaunchManifestSummary(rebuilt.value->manifestPath);
    REQUIRE(summary.profileName == "Debug");
    REQUIRE(summary.selectedExecutable.has_value());
    REQUIRE(*summary.selectedExecutable == "Hello.Hosted");
}

TEST_CASE("process execution helper runs tools directly without a shell")
{
    const auto cmake = ResolveToolPath("cmake", RepoRoot());
    REQUIRE(cmake.has_value());
    REQUIRE(RunProcess(cmake->path, {"--version"}) == 0);
}
