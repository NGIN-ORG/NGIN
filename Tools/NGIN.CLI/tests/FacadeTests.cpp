#include "TestSupport.hpp"
#include "Publishing.hpp"

TEST_CASE("NGIN CLI installer identifier has the stable upgrade GUID") {
  REQUIRE(DeterministicInstallerGuid("NGIN-ORG.NGIN.CLI") ==
          "bc787581-23bf-5e17-87ae-864415448920");
  REQUIRE(DeterministicInstallerGuid("example.product") ==
          DeterministicInstallerGuid("example.product"));
  REQUIRE(DeterministicInstallerGuid("example.product") !=
          DeterministicInstallerGuid("example.other"));
}

namespace
{
    auto WriteAnalyzeToolingPackage(const fs::path &root, const fs::path &projectRelativePath) -> void
    {
        WriteFile(root / "Workspace.ngin",
                  "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                  "<Workspace SchemaVersion=\"4\" Name=\"AnalyzeWorkspace\">\n"
                  "  <Projects><Project Path=\"" + projectRelativePath.generic_string() + "\" /></Projects>\n"
                  "  <Packages><Source Name=\"local\" Path=\"Packages\" /></Packages>\n"
                  "</Workspace>\n");
        WriteFile(root / "Packages/NGIN.Tooling.ClangTidy/NGIN.Tooling.ClangTidy.nginpkg",
                  R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="NGIN.Tooling.ClangTidy" Version="0.1.0">
  <Tool Name="NGIN.Tooling.ClangTidy">
    <Exports>
      <Tool Name="clang-tidy" Kind="Development" Executable="clang-tidy" />
    </Exports>
  </Tool>
  <ToolDrivers>
    <Driver Name="clang-tidy-driver" Protocol="NGIN.ToolDriver/1" Adapter="builtin.clang-tidy.v1">
      <Capabilities><Capability Name="diagnostics" /></Capabilities>
    </Driver>
  </ToolDrivers>
  <ToolActions>
    <Action Name="analyze" Kind="Analyze" Tool="clang-tidy" Driver="clang-tidy-driver">
      <Accepts Contract="cpp.translation-units/v1" />
      <Capabilities><Capability Name="diagnostics" /></Capabilities>
    </Action>
  </ToolActions>
  <Features>
    <Feature Name="Analyzer">
      <Tooling>
        <Run Name="cpp-static-analysis" Action="NGIN.Tooling.ClangTidy::analyze">
          <Input Contract="cpp.translation-units/v1" Scope="Product" />
          <Config Path=".clang-tidy" Optional="true" />
          <Policy Gate="false" FailOn="Warning" />
        </Run>
      </Tooling>
    </Feature>
  </Features>
</Package>
)xml");
    }
}

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

TEST_CASE("legacy analyzer authoring is rejected")
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

    REQUIRE_THROWS_WITH(LoadProjectManifest(projectPath),
                        ContainsSubstring("legacy <Quality>/<Analyzer> is not supported"));
}

TEST_CASE("package feature analyzer contributes clang-tidy to graph")
{
    TempDir temp{};
    WriteAnalyzeToolingPackage(temp.path(), "App/App.nginproj");
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
    args.graphPlan = "tooling";

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdGraph(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring("run cpp-static-analysis action=NGIN.Tooling.ClangTidy::analyze"));
    REQUIRE_THAT(captured.str(), ContainsSubstring("driver=clang-tidy-driver"));

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
    <Tooling>
      <Run Name="cpp-static-analysis" Action="NGIN.Tooling.ClangTidy::analyze">
        <Policy Gate="true" FailOn="Warning" />
      </Run>
    </Tooling>
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
    REQUIRE_THAT(inspectCaptured.str(), ContainsSubstring(R"("tooling":{"tools":[)"));
    REQUIRE_THAT(inspectCaptured.str(), ContainsSubstring(R"("runs":[)"));
    REQUIRE_THAT(inspectCaptured.str(), ContainsSubstring(R"("name":"cpp-static-analysis","displayName":")"));
    REQUIRE_THAT(inspectCaptured.str(), ContainsSubstring(R"("action":"NGIN.Tooling.ClangTidy::analyze","kind":"Analyze")"));
    REQUIRE_THAT(inspectCaptured.str(), ContainsSubstring(R"("gate":true,"failOn":"Warning")"));
}

#ifndef _WIN32
TEST_CASE("clang-tidy analyzer emits normalized warning diagnostics")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "AnalyzeWarn.App.nginproj";
    WriteAnalyzeToolingPackage(temp.path(), projectPath.filename());
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="AnalyzeWarn.App">
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

    args.toolNoConfigure = true;
    captured.str({});
    captured.clear();
    previous = std::cout.rdbuf(captured.rdbuf());
    const auto freshExitCode = CmdAnalyze(temp.path(), args);
    std::cout.rdbuf(previous);
    REQUIRE(freshExitCode == 0);

    auto changedManifest = ReadFile(projectPath);
    changedManifest.replace(changedManifest.find("</Project>"),
                            std::string{"</Project>"}.size(), "  \n</Project>");
    WriteFile(projectPath, changedManifest);
    captured.str({});
    captured.clear();
    previous = std::cout.rdbuf(captured.rdbuf());
    const auto staleExitCode = CmdAnalyze(temp.path(), args);
    std::cout.rdbuf(previous);
    REQUIRE(staleExitCode == 2);
    REQUIRE_THAT(captured.str(), ContainsSubstring("fresh compatible compilation-unit plan"));
}

TEST_CASE("tool gate fails on warnings without rewriting intrinsic severity")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "AnalyzeError.App.nginproj";
    WriteAnalyzeToolingPackage(temp.path(), projectPath.filename());
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="AnalyzeError.App">
  <Application>
    <Uses>
      <Package Name="NGIN.Tooling.ClangTidy" Version="[0.1.0,0.2.0)" Scope="Dev">
        <Feature Name="Analyzer" />
      </Package>
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Tooling>
      <Run Name="cpp-static-analysis" Action="NGIN.Tooling.ClangTidy::analyze">
        <Policy Gate="true" FailOn="Warning" />
      </Run>
    </Tooling>
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
    REQUIRE_THAT(captured.str(), ContainsSubstring("[warning] "));
    REQUIRE_THAT(captured.str(), ContainsSubstring(":2:3: issue [clang-tidy:readability-test]"));
}

TEST_CASE("clang-tidy analyzer reports missing tool")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "AnalyzeMissingTool.App.nginproj";
    WriteAnalyzeToolingPackage(temp.path(), projectPath.filename());
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="AnalyzeMissingTool.App">
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
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");
    ScopedEnvironmentVariable clangTidy{"NGIN_CLANG_TIDY", (temp.path() / "missing-clang-tidy").string()};

    ParsedArgs args{};
    args.projectPath = projectPath.string();
    args.outputPath = (temp.path() / "out").string();

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdAnalyze(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 2);
    REQUIRE_THAT(captured.str(), ContainsSubstring("could not resolve tool 'clang-tidy'"));
}
#endif

TEST_CASE("publish command writes folder publish output")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Publish.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Publish.App" Version="1.2.3">
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
    <Publish Name="tgz" Kind="Archive" Format="tgz" Output="dist/Publish.App-$(ProjectVersion).tgz">
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
    REQUIRE(fs::exists(temp.path() / "dist/Publish.App/bin" / PlatformExecutableName("Publish.App")));

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
    const auto archivePath = temp.path() / "dist/Publish.App.zip";
    const auto archiveEntries = ZipCentralEntries(archivePath);
    REQUIRE(std::any_of(archiveEntries.begin(), archiveEntries.end(), [](const ZipCentralEntry &entry) {
        return entry.path == "config/app.json";
    }));
    REQUIRE(std::any_of(archiveEntries.begin(), archiveEntries.end(), [](const ZipCentralEntry &entry) {
        return entry.path == "bin/" + PlatformExecutableName("Publish.App");
    }));
    REQUIRE_FALSE(std::any_of(archiveEntries.begin(), archiveEntries.end(), [](const ZipCentralEntry &entry) {
        return entry.path.starts_with(".ngin/");
    }));

#ifndef _WIN32
    ParsedArgs tgzArgs{};
    tgzArgs.projectPath = projectPath.string();
    tgzArgs.packageName = "tgz";
    tgzArgs.outputPath = (temp.path() / "build-archive").string();
    tgzArgs.eventOutputMode = EventOutputMode::JsonLines;
    std::ostringstream publishEvents{};
    auto *previous = std::cout.rdbuf(publishEvents.rdbuf());
    const auto tgzExitCode = CmdPublish(temp.path(), tgzArgs);
    std::cout.rdbuf(previous);
    REQUIRE(tgzExitCode == 0);
    REQUIRE(fs::exists(temp.path() / "dist/Publish.App-1.2.3.tgz"));
    REQUIRE_THAT(publishEvents.str(), ContainsSubstring(R"("type":"artifact.produced")"));
    REQUIRE_THAT(publishEvents.str(), ContainsSubstring(R"("publish":"tgz")"));
    REQUIRE_THAT(publishEvents.str(), ContainsSubstring(R"("kind":"Archive")"));
    REQUIRE_THAT(publishEvents.str(), ContainsSubstring(R"("format":"tgz")"));
    REQUIRE_THAT(publishEvents.str(), ContainsSubstring(R"("version":"1.2.3")"));
#endif
}

TEST_CASE("installer publish validation fails before building")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "InvalidInstaller.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="InvalidInstaller.App">
  <Application>
    <Publish Name="installer" Kind="Installer" Format="deb" Output="dist/app.deb">
      <Installer Identifier="org.example.invalid" Vendor="Example" Contact="dev@example.org" />
    </Publish>
  </Application>
</Project>
)xml");

    ParsedArgs args{};
    args.projectPath = projectPath.string();
    args.packageName = "installer";
    REQUIRE_THROWS_WITH(CmdPublish(temp.path(), args),
                        Catch::Matchers::ContainsSubstring("requires Project Version"));

    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="InvalidInstaller.App" Version="1.0.0">
  <Application>
    <Publish Name="installer" Kind="Installer" Format=")xml"
#ifdef _WIN32
              "deb"
#else
              "msi"
#endif
              R"xml(" Output="dist/app.native">
      <Installer Identifier="org.example.invalid" Vendor="Example" Contact="dev@example.org" />
    </Publish>
  </Application>
</Project>
)xml");
#ifdef _WIN32
    REQUIRE_THROWS_WITH(CmdPublish(temp.path(), args),
                        Catch::Matchers::ContainsSubstring("requires a Linux target profile"));
#else
    REQUIRE_THROWS_WITH(CmdPublish(temp.path(), args),
                        Catch::Matchers::ContainsSubstring("requires a Windows target profile"));
#endif
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

TEST_CASE("build facade rejects a foreign target before backend configuration")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Foreign.Target.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Foreign.Target">
  <Application />
</Project>
)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");

    const auto project = LoadProjectManifest(projectPath);
    auto profile = ProfileByName(project, std::nullopt);
    const auto host = DetectHostPlatform();
    ApplyTargetPlatform(profile, host.operatingSystem == "windows" ? "linux-x64" : "windows-x64");
    const auto outputDir = temp.path() / "stage";

    const auto configured = ConfigureLaunch(project, profile, outputDir);
    REQUIRE_FALSE(configured.value.has_value());
    REQUIRE(configured.diagnostics.HasErrors());
    REQUIRE_THAT(configured.diagnostics.entries.front().message,
                 ContainsSubstring("does not match build host '" + host.name + "'"));
    REQUIRE_THAT(configured.diagnostics.entries.front().message,
                 ContainsSubstring("cross-compilation is not supported"));
    REQUIRE_FALSE(fs::exists(outputDir));

    const auto built = BuildLaunch(project, profile, outputDir);
    REQUIRE_FALSE(built.value.has_value());
    REQUIRE(built.diagnostics.HasErrors());
    REQUIRE_FALSE(fs::exists(outputDir));
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

TEST_CASE("build facade emits backend lifecycle events")
{
    const auto projectPath = RepoRoot() / "Examples/Hello.Native/Hello.Native.nginproj";
    REQUIRE(fs::exists(projectPath));

    const auto project = LoadProjectManifest(projectPath);
    const std::optional<std::string> profileName{"Debug"};
    const auto &profile = ProfileByName(project, profileName);
    TempDir temp{};

    RecordingCliEventSink sink{};
    CliEventEmitter events{&sink};
    events.SetCommand("build");
    events.SetSelection(project.name, profile.name);
    std::vector<BackendStepResult> backendSteps{};
    BuildExecutionOptions options{
        .backendOutput = BackendOutputMode::Compact,
        .backendSteps = &backendSteps,
        .interactiveProgress = false,
        .events = &events,
    };

    const auto built = BuildLaunch(project, profile, temp.path() / "stage", options);
    REQUIRE(built.value.has_value());
    REQUIRE_FALSE(built.diagnostics.HasErrors());

    const auto &emitted = sink.Events();
    REQUIRE(std::any_of(emitted.begin(), emitted.end(), [](const CliEvent &event) { return event.type == CliEventType::PhaseStarted; }));
    REQUIRE(std::any_of(emitted.begin(), emitted.end(), [](const CliEvent &event) { return event.type == CliEventType::PhaseCompleted; }));
    REQUIRE_FALSE(std::any_of(emitted.begin(), emitted.end(), [](const CliEvent &event) { return event.type == CliEventType::BackendOutput; }));
}

TEST_CASE("build command JSONL emits clean event stream")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Jsonl.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Jsonl.App">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");

    ParsedArgs args{};
    args.argv = {"build", "--project", projectPath.string(), "--events", "jsonl"};
    args.projectPath = projectPath.string();
    args.outputPath = (temp.path() / "out").string();
    args.configurationName = "Release";
    args.eventOutputMode = EventOutputMode::JsonLines;
    args.backendOutputMode = BackendOutputMode::Compact;

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdBuild(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    const auto output = captured.str();
    REQUIRE_THAT(output, ContainsSubstring(R"("kind":"NGIN.CLI.Event")"));
    REQUIRE_THAT(output, ContainsSubstring(R"("type":"command.started")"));
    REQUIRE_THAT(output, ContainsSubstring(R"("type":"command.selection")"));
    REQUIRE_THAT(output, ContainsSubstring(R"("configuration":"Release")"));
    REQUIRE_THAT(output, ContainsSubstring(R"("configurationSource":"buildTypeOverride")"));
    REQUIRE_THAT(output, ContainsSubstring(R"("buildType":"Release")"));
    REQUIRE_THAT(output, ContainsSubstring(R"("type":"phase.started")"));
    REQUIRE_THAT(output, ContainsSubstring(R"("type":"phase.completed")"));
    REQUIRE_THAT(output, ContainsSubstring(R"("type":"artifact.produced")"));
    REQUIRE_THAT(output, ContainsSubstring(R"("type":"summary")"));
    REQUIRE_THAT(output, ContainsSubstring(R"("type":"command.completed")"));
    REQUIRE_THAT(output, !ContainsSubstring(R"("type":"backend.output")"));

    std::istringstream lines{output};
    std::string line{};
    while (std::getline(lines, line))
    {
        REQUIRE(line.rfind(R"({"schemaVersion":"1.0","kind":"NGIN.CLI.Event")", 0) == 0);
    }
}

#ifndef _WIN32
TEST_CASE("configure command JSONL captures generator stdout")
{
    TempDir temp{};
    const auto generatorTool = temp.path() / "noisy-generator";
    WriteFile(generatorTool,
              "#!/bin/sh\n"
              "out=\"$1\"\n"
              "mkdir -p \"$(dirname \"$out\")\"\n"
              "echo \"generator-raw-output\"\n"
              "printf 'int generated_symbol() { return 0; }\\n' > \"$out\"\n"
              "exit 0\n");
    fs::permissions(generatorTool, fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write);

    const auto projectPath = temp.path() / "Jsonl.Generated.App.nginproj";
    const auto manifest = std::string{"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                                      "<Project SchemaVersion=\"4\" Name=\"Jsonl.Generated.App\">\n"
                                      "  <Application>\n"
                                      "    <Build>\n"
                                      "      <Sources Path=\"src/**.cpp\" />\n"
                                      "    </Build>\n"
                                      "    <Generate>\n"
                                      "      <Generator Name=\"NoisyGen\">\n"
                                      "        <Tool Executable=\""} +
                          generatorTool.string() +
                          "\" />\n"
                          "        <Args>\n"
                          "          <Arg Path=\"$(GeneratedDir)/noisy.cpp\" />\n"
                          "        </Args>\n"
                          "        <Outputs>\n"
                          "          <Sources Path=\"$(GeneratedDir)/noisy.cpp\" />\n"
                          "        </Outputs>\n"
                          "      </Generator>\n"
                          "    </Generate>\n"
                          "  </Application>\n"
                          "</Project>\n";
    WriteFile(projectPath, manifest);
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");

    ParsedArgs args{};
    args.argv = {"configure", "--project", projectPath.string(), "--events", "jsonl"};
    args.projectPath = projectPath.string();
    args.outputPath = (temp.path() / "out").string();
    args.eventOutputMode = EventOutputMode::JsonLines;
    args.backendOutputMode = BackendOutputMode::Compact;

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdConfigure(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    const auto output = captured.str();
    REQUIRE_THAT(output, !ContainsSubstring("generator-raw-output"));

    std::istringstream lines{output};
    std::string line{};
    while (std::getline(lines, line))
    {
        REQUIRE(line.rfind(R"({"schemaVersion":"1.0","kind":"NGIN.CLI.Event")", 0) == 0);
    }
}
#endif

TEST_CASE("failed JSONL build includes compact backend output event")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Broken.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Broken.App">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return ; }\n");

    ParsedArgs args{};
    args.argv = {"build", "--project", projectPath.string(), "--events", "jsonl"};
    args.projectPath = projectPath.string();
    args.outputPath = (temp.path() / "out").string();
    args.eventOutputMode = EventOutputMode::JsonLines;
    args.backendOutputMode = BackendOutputMode::Compact;

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdBuild(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 1);
    const auto output = captured.str();
    REQUIRE_THAT(output, ContainsSubstring(R"("type":"phase.failed")"));
    REQUIRE_THAT(output, ContainsSubstring(R"("type":"backend.output")"));
    REQUIRE_THAT(output, ContainsSubstring(R"("status":"failed")"));
}

TEST_CASE("JSONL build stream mode emits backend output before phase completion")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Stream.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Stream.App">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");

    ParsedArgs args{};
    args.argv = {"build", "--project", projectPath.string(), "--events", "jsonl", "--backend-output", "stream"};
    args.projectPath = projectPath.string();
    args.outputPath = (temp.path() / "out").string();
    args.eventOutputMode = EventOutputMode::JsonLines;
    args.backendOutputMode = BackendOutputMode::Stream;

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdBuild(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    const auto output = captured.str();
    const auto backendOutput = output.find(R"("type":"backend.output")");
    const auto buildCompleted = output.find(R"("type":"phase.completed","command":"build")", backendOutput == std::string::npos ? 0 : backendOutput);
    REQUIRE(backendOutput != std::string::npos);
    REQUIRE(buildCompleted != std::string::npos);
    REQUIRE(backendOutput < buildCompleted);
}

TEST_CASE("package pack and lock commands emit JSONL lifecycle events")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Game.Engine.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Game.Engine">
  <Library>
    <PackageOutput Name="Game.Engine" Version="1.0.0">
      <Metadata>
        <Description>Engine package.</Description>
      </Metadata>
      <Exports>
        <Library Name="Game::Engine" />
      </Exports>
    </PackageOutput>
  </Library>
</Project>
)xml");

    ParsedArgs packArgs{};
    packArgs.argv = {"package", "pack", "--project", projectPath.string(), "--events", "jsonl"};
    packArgs.projectPath = projectPath.string();
    packArgs.outputPath = (temp.path() / "dist").string();
    packArgs.eventOutputMode = EventOutputMode::JsonLines;

    std::ostringstream packOutput{};
    auto *previous = std::cout.rdbuf(packOutput.rdbuf());
    const auto packExitCode = CmdPackagePack(temp.path(), packArgs);
    std::cout.rdbuf(previous);

    REQUIRE(packExitCode == 0);
    REQUIRE_THAT(packOutput.str(), ContainsSubstring(R"("type":"artifact.produced")"));
    REQUIRE_THAT(packOutput.str(), ContainsSubstring(R"("kind":"package-manifest")"));
    REQUIRE_THAT(packOutput.str(), ContainsSubstring(R"("kind":"package-archive")"));
    REQUIRE_THAT(packOutput.str(), ContainsSubstring(R"("type":"command.completed")"));

    ParsedArgs lockArgs{};
    lockArgs.argv = {"package", "lock", "--project", projectPath.string(), "--events", "jsonl"};
    lockArgs.projectPath = projectPath.string();
    lockArgs.outputPath = (temp.path() / "ngin.lock").string();
    lockArgs.eventOutputMode = EventOutputMode::JsonLines;

    std::ostringstream lockOutput{};
    previous = std::cout.rdbuf(lockOutput.rdbuf());
    const auto lockExitCode = CmdPackageLock(temp.path(), lockArgs);
    std::cout.rdbuf(previous);

    REQUIRE(lockExitCode == 0);
    REQUIRE_THAT(lockOutput.str(), ContainsSubstring(R"("kind":"lock-file")"));
    REQUIRE_THAT(lockOutput.str(), ContainsSubstring(R"("type":"summary")"));
    REQUIRE_THAT(lockOutput.str(), ContainsSubstring(R"("status":"success")"));
}

TEST_CASE("restore and run commands emit JSONL lifecycle events")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Run.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Run.App">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "src/main.cpp", "#include <iostream>\nint main() { std::cout << \"hello-run\\n\"; return 0; }\n");

    ParsedArgs restoreArgs{};
    restoreArgs.argv = {"restore", "--project", projectPath.string(), "--events", "jsonl"};
    restoreArgs.projectPath = projectPath.string();
    restoreArgs.outputPath = (temp.path() / "store").string();
    restoreArgs.eventOutputMode = EventOutputMode::JsonLines;

    std::ostringstream restoreOutput{};
    auto *previous = std::cout.rdbuf(restoreOutput.rdbuf());
    const auto restoreExitCode = CmdRestore(temp.path(), restoreArgs);
    std::cout.rdbuf(previous);

    REQUIRE(restoreExitCode == 0);
    REQUIRE_THAT(restoreOutput.str(), ContainsSubstring(R"("phase":"restore")"));
    REQUIRE_THAT(restoreOutput.str(), ContainsSubstring(R"("kind":"lock-file")"));
    REQUIRE_THAT(restoreOutput.str(), ContainsSubstring(R"("type":"command.completed")"));

    ParsedArgs runArgs{};
    runArgs.argv = {"run", "--project", projectPath.string(), "--events", "jsonl"};
    runArgs.projectPath = projectPath.string();
    runArgs.outputPath = (temp.path() / "out").string();
    runArgs.eventOutputMode = EventOutputMode::JsonLines;
    runArgs.backendOutputMode = BackendOutputMode::Compact;

    std::ostringstream runOutput{};
    previous = std::cout.rdbuf(runOutput.rdbuf());
    const auto runExitCode = CmdRun(temp.path(), runArgs);
    std::cout.rdbuf(previous);

    REQUIRE(runExitCode == 0);
    REQUIRE_THAT(runOutput.str(), ContainsSubstring(R"("phase":"run")"));
    REQUIRE_THAT(runOutput.str(), ContainsSubstring(R"("type":"backend.output")"));
    REQUIRE_THAT(runOutput.str(), ContainsSubstring("hello-run"));
    REQUIRE_THAT(runOutput.str(), ContainsSubstring(R"("status":"success")"));
}
