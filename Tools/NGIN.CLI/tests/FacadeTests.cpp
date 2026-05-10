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
    REQUIRE_THAT(captured.str(), ContainsSubstring("Staged profile:"));
    REQUIRE(fs::exists(temp.path() / "out/config/app.json"));
}

TEST_CASE("analyze command reports declared analyzer plan")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Analyze.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Analyze.App">
  <Application>
    <Quality>
      <Analyzer Name="clang-tidy" Scope="Build" Enabled="true" Severity="Error">
        <Config Path=".clang-tidy" />
      </Analyzer>
    </Quality>
  </Application>
</Project>
)xml");

    const auto project = LoadProjectManifest(projectPath);
    REQUIRE(project.quality.analyzers.size() == 1);
    REQUIRE(project.quality.analyzers[0].name == "clang-tidy");
    REQUIRE(project.quality.analyzers[0].severity == "Error");
    REQUIRE(project.quality.analyzers[0].configPath == ".clang-tidy");

    ParsedArgs args{};
    args.projectPath = projectPath.string();

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdAnalyze(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring("Analyze product: Analyze.App"));
    REQUIRE_THAT(captured.str(), ContainsSubstring("analyzer clang-tidy scope=Build severity=Error config=.clang-tidy"));

    ParsedArgs inspectArgs{};
    inspectArgs.projectPath = projectPath.string();
    inspectArgs.format = "json";
    std::ostringstream inspectCaptured{};
    previous = std::cout.rdbuf(inspectCaptured.rdbuf());
    const auto inspectExitCode = CmdInspect(temp.path(), inspectArgs);
    std::cout.rdbuf(previous);

    REQUIRE(inspectExitCode == 0);
    REQUIRE_THAT(inspectCaptured.str(), ContainsSubstring(R"("quality":{"analyzers":[)"));
    REQUIRE_THAT(inspectCaptured.str(), ContainsSubstring(R"("name":"clang-tidy")"));
    REQUIRE_THAT(inspectCaptured.str(), ContainsSubstring(R"("severity":"Error")"));
}

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
