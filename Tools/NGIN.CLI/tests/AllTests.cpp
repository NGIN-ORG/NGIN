#include "Authoring.hpp"
#include "Build.hpp"
#include "Diagnostics.hpp"
#include "Resolution.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace Catch::Matchers;
    using namespace NGIN::CLI;

    class ScopedCurrentPath
    {
    public:
        explicit ScopedCurrentPath(const fs::path &path)
            : previous_(fs::current_path())
        {
            fs::current_path(path);
        }

        ~ScopedCurrentPath()
        {
            std::error_code error;
            fs::current_path(previous_, error);
        }

    private:
        fs::path previous_{};
    };

    class TempDir
    {
    public:
        TempDir()
        {
            const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
            path_ = fs::temp_directory_path() / ("ngin-cli-tests-" + std::to_string(now) + "-" + std::to_string(std::rand()));
            fs::create_directories(path_);
        }

        ~TempDir()
        {
            std::error_code error;
            fs::remove_all(path_, error);
        }

        [[nodiscard]] auto path() const -> const fs::path &
        {
            return path_;
        }

    private:
        fs::path path_{};
    };

    auto WriteFile(const fs::path &path, const std::string &content) -> void
    {
        fs::create_directories(path.parent_path());
        std::ofstream out(path);
        out << content;
    }

    [[nodiscard]] auto RepoRoot() -> fs::path
    {
        return fs::path(NGIN_CLI_TEST_REPO_ROOT);
    }

    [[nodiscard]] auto DiagnosticMessages(const DiagnosticReport &report) -> std::vector<std::string>
    {
        std::vector<std::string> messages{};
        for (const auto &entry : report.entries)
        {
            messages.push_back(entry.message);
        }
        return messages;
    }
}

TEST_CASE("workspace, project, and package manifests parse through authoring facades")
{
    TempDir temp{};
    WriteFile(
        temp.path() / "Workspace.ngin",
        R"(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="2" Name="TempWorkspace" PlatformVersion="0.1.0">
  <PackageSources>
    <PackageSource Path="Packages" />
  </PackageSources>
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
</Workspace>
)");
    WriteFile(
        temp.path() / "Packages/Sample/Sample.nginpkg",
        R"(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="2" Name="Sample.Package" Version="1.0.0">
  <Build Backend="CMake" Mode="Manual" />
  <Modules>
    <Module Name="Sample.Module" Family="Core" Type="Runtime" StartupStage="Features" />
  </Modules>
</Package>
)");
    WriteFile(
        temp.path() / "App/App.nginproj",
        R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="2" Name="Sample.App" Type="Application" DefaultConfiguration="Runtime">
  <SourceRoots>
    <SourceRoot Path="src" />
  </SourceRoots>
  <Output Kind="Executable" Name="Sample.App" Target="SampleApp" />
  <References>
    <Package Name="Sample.Package" />
  </References>
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Configurations>
    <Configuration Name="Runtime" BuildConfiguration="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev">
      <Launch Executable="Sample.App" WorkingDirectory="." />
    </Configuration>
  </Configurations>
</Project>
)");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    const auto workspace = LoadWorkspaceManifest(temp.path());
    const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
    const auto package = LoadPackageManifest(temp.path() / "Packages/Sample/Sample.nginpkg");
    const auto catalog = LoadPackageCatalog(workspace, project.path);

    REQUIRE(workspace.name == "TempWorkspace");
    REQUIRE(project.name == "Sample.App");
    REQUIRE(package.name == "Sample.Package");
    REQUIRE(catalog.contains("Sample.Package"));
}

TEST_CASE("project parsing rejects missing required output metadata")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Invalid.nginproj";
    WriteFile(
        projectPath,
        R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="2" Name="Invalid" Type="Application" DefaultConfiguration="Runtime">
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Configurations>
    <Configuration Name="Runtime" BuildConfiguration="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Configurations>
</Project>
)");

    REQUIRE_THROWS_WITH(
        LoadProjectManifest(projectPath),
        ContainsSubstring("missing <Output>"));
}

TEST_CASE("project autodiscovery resolves nearest nginproj in the current tree")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Nested/App.nginproj";
    WriteFile(
        projectPath,
        R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="2" Name="Nested" Type="Application" DefaultConfiguration="Runtime">
  <Output Kind="Executable" Name="Nested" Target="NestedTarget" />
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Configurations>
    <Configuration Name="Runtime" BuildConfiguration="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Configurations>
</Project>
)");
    fs::create_directories(temp.path() / "Nested/Deep/Deeper");

    ScopedCurrentPath cwd(temp.path() / "Nested/Deep/Deeper");
    const auto resolved = ResolveProjectPath(std::nullopt);

    REQUIRE(fs::equivalent(resolved, projectPath));
}

TEST_CASE("resolution reports package dependency cycles")
{
    TempDir temp{};
    WriteFile(
        temp.path() / "Workspace.ngin",
        R"(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="2" Name="CycleWorkspace">
  <PackageSources>
    <PackageSource Path="Packages" />
  </PackageSources>
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
</Workspace>
)");
    WriteFile(
        temp.path() / "Packages/A/A.nginpkg",
        R"(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="2" Name="Package.A" Version="1.0.0">
  <Dependencies>
    <Dependency Name="Package.B" />
  </Dependencies>
  <Build Backend="CMake" Mode="Manual" />
  <Modules>
    <Module Name="Module.A" Family="Core" Type="Runtime" StartupStage="Features" />
  </Modules>
</Package>
)");
    WriteFile(
        temp.path() / "Packages/B/B.nginpkg",
        R"(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="2" Name="Package.B" Version="1.0.0">
  <Dependencies>
    <Dependency Name="Package.A" />
  </Dependencies>
  <Build Backend="CMake" Mode="Manual" />
  <Modules>
    <Module Name="Module.B" Family="Core" Type="Runtime" StartupStage="Features" />
  </Modules>
</Package>
)");
    WriteFile(
        temp.path() / "App/App.nginproj",
        R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="2" Name="Cycle.App" Type="Application" DefaultConfiguration="Runtime">
  <Output Kind="Executable" Name="Cycle.App" Target="CycleApp" />
  <References>
    <Package Name="Package.A" />
  </References>
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Configurations>
    <Configuration Name="Runtime" BuildConfiguration="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Configurations>
</Project>
)");

    const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
    const std::optional<std::string> configurationName{"Runtime"};
    const auto &configuration = ConfigurationByName(project, configurationName);
    const auto resolved = ResolveLaunch(project, configuration);

    REQUIRE_FALSE(resolved.value.has_value());
    REQUIRE_THAT(resolved.diagnostics.entries.front().message, ContainsSubstring("dependency cycle"));
}

TEST_CASE("resolution reports project config source collisions")
{
    const auto projectPath = RepoRoot() / "Examples/ProjectRef.Config/CollisionRoot/ProjectRef.Config.CollisionRoot.nginproj";
    REQUIRE(fs::exists(projectPath));

    const auto project = LoadProjectManifest(projectPath);
    const std::optional<std::string> configurationName{"Runtime"};
    const auto &configuration = ConfigurationByName(project, configurationName);
    const auto resolved = ResolveLaunch(project, configuration);

    REQUIRE_FALSE(resolved.value.has_value());

    bool foundCollision = false;
    for (const auto &message : DiagnosticMessages(resolved.diagnostics))
    {
        if (message.find("config source destination collision") != std::string::npos)
        {
            foundCollision = true;
            break;
        }
    }
    REQUIRE(foundCollision);
}

TEST_CASE("build facade writes launch manifests and preserves unrelated output files")
{
    const auto projectPath = RepoRoot() / "Examples/App.Basic/App.Basic.nginproj";
    REQUIRE(fs::exists(projectPath));

    const auto project = LoadProjectManifest(projectPath);
    const std::optional<std::string> configurationName{"Runtime"};
    const auto &configuration = ConfigurationByName(project, configurationName);
    TempDir temp{};
    const auto outputDir = temp.path() / "stage";

    const auto firstBuild = BuildLaunch(project, configuration, outputDir);
    REQUIRE(firstBuild.value.has_value());
    REQUIRE_FALSE(firstBuild.diagnostics.HasErrors());

    WriteFile(outputDir / "keep.txt", "preserve me\n");

    const auto secondBuild = BuildLaunch(project, configuration, outputDir);
    REQUIRE(secondBuild.value.has_value());
    REQUIRE_FALSE(secondBuild.diagnostics.HasErrors());
    REQUIRE(fs::exists(outputDir / "keep.txt"));

    const auto summary = LoadLaunchManifestSummary(secondBuild.value->manifestPath);
    REQUIRE(summary.configurationName == "Runtime");
    REQUIRE(summary.selectedExecutable.has_value());
    REQUIRE(*summary.selectedExecutable == "App.Basic");
}

  TEST_CASE("clean facade removes owned generated artifacts and preserves unrelated files")
  {
    const auto projectPath = RepoRoot() / "Examples/App.Basic/App.Basic.nginproj";
    REQUIRE(fs::exists(projectPath));

    const auto project = LoadProjectManifest(projectPath);
    const std::optional<std::string> configurationName{"Runtime"};
    const auto &configuration = ConfigurationByName(project, configurationName);
    TempDir temp{};
    const auto outputDir = temp.path() / "stage";

    const auto build = BuildLaunch(project, configuration, outputDir);
    REQUIRE(build.value.has_value());
    REQUIRE_FALSE(build.diagnostics.HasErrors());

    WriteFile(outputDir / "keep.txt", "preserve me\n");

    const auto cleaned = CleanLaunch(project, configuration, outputDir);
    REQUIRE(cleaned.value.has_value());
    REQUIRE_FALSE(cleaned.diagnostics.HasErrors());
    REQUIRE(fs::exists(outputDir / "keep.txt"));
    REQUIRE_FALSE(fs::exists(outputDir / "App.Basic.Runtime.nginlaunch"));
    REQUIRE_FALSE(fs::exists(outputDir / "bin" / "App.Basic"));
  }

  TEST_CASE("rebuild semantics can be expressed as clean followed by build")
  {
    const auto projectPath = RepoRoot() / "Examples/App.Basic/App.Basic.nginproj";
    REQUIRE(fs::exists(projectPath));

    const auto project = LoadProjectManifest(projectPath);
    const std::optional<std::string> configurationName{"Runtime"};
    const auto &configuration = ConfigurationByName(project, configurationName);
    TempDir temp{};
    const auto outputDir = temp.path() / "stage";

    const auto firstBuild = BuildLaunch(project, configuration, outputDir);
    REQUIRE(firstBuild.value.has_value());
    REQUIRE_FALSE(firstBuild.diagnostics.HasErrors());

    const auto cleaned = CleanLaunch(project, configuration, outputDir);
    REQUIRE(cleaned.value.has_value());
    REQUIRE_FALSE(cleaned.diagnostics.HasErrors());

    const auto rebuilt = BuildLaunch(project, configuration, outputDir);
    REQUIRE(rebuilt.value.has_value());
    REQUIRE_FALSE(rebuilt.diagnostics.HasErrors());
    REQUIRE(fs::exists(rebuilt.value->manifestPath));

    const auto summary = LoadLaunchManifestSummary(rebuilt.value->manifestPath);
    REQUIRE(summary.configurationName == "Runtime");
    REQUIRE(summary.selectedExecutable.has_value());
    REQUIRE(*summary.selectedExecutable == "App.Basic");
  }

TEST_CASE("process execution helper runs tools directly without a shell")
{
    REQUIRE(RunProcess("cmake", {"--version"}) == 0);
}
