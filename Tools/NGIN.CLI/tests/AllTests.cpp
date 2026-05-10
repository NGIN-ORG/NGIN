#include "Authoring.hpp"
#include "Build.hpp"
#include "Commands.hpp"
#include "Diagnostics.hpp"
#include "Resolution.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
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
        explicit ScopedCurrentPath(const fs::path &path) : previous_(fs::current_path())
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

    class ScopedEnvironmentVariable
    {
    public:
        ScopedEnvironmentVariable(const std::string &name, const std::string &value) : name_(name)
        {
            if (const auto *existing = std::getenv(name.c_str()); existing != nullptr)
            {
                previous_ = existing;
            }
            setenv(name.c_str(), value.c_str(), 1);
        }

        ~ScopedEnvironmentVariable()
        {
            if (previous_.has_value())
            {
                setenv(name_.c_str(), previous_->c_str(), 1);
            }
            else
            {
                unsetenv(name_.c_str());
            }
        }

    private:
        std::string name_{};
        std::optional<std::string> previous_{};
    };

    class ScopedUnsetEnvironmentVariable
    {
    public:
        explicit ScopedUnsetEnvironmentVariable(const std::string &name) : name_(name)
        {
            if (const auto *existing = std::getenv(name.c_str()); existing != nullptr)
            {
                previous_ = existing;
            }
            unsetenv(name.c_str());
        }

        ~ScopedUnsetEnvironmentVariable()
        {
            if (previous_.has_value())
            {
                setenv(name_.c_str(), previous_->c_str(), 1);
            }
        }

    private:
        std::string name_{};
        std::optional<std::string> previous_{};
    };

    class TempDir
    {
    public:
        TempDir()
        {
            const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
            path_ = fs::temp_directory_path() /
                    ("ngin-cli-tests-" + std::to_string(now) + "-" + std::to_string(std::rand()));
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

    [[nodiscard]] auto ReadFile(const fs::path &path) -> std::string
    {
        std::ifstream input(path);
        std::ostringstream content{};
        content << input.rdbuf();
        return content.str();
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

} // namespace

TEST_CASE("workspace, project, and package manifests parse through authoring "
          "facades")
{
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
    const auto package = LoadPackageManifest(temp.path() / "Packages/Sample/Sample.nginpkg");
    const auto catalog = LoadPackageCatalog(workspace, project.path);

    REQUIRE(workspace.name == "TempWorkspace");
    REQUIRE(project.name == "Sample.App");
    REQUIRE(package.name == "Sample.Package");
    REQUIRE(package.modules.empty());
    REQUIRE(catalog.contains("Sample.Package"));
}

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
    REQUIRE(profile.environmentName == "development");
    REQUIRE(profile.launch.executable == "Hello.Native");
    REQUIRE(profile.launch.args.empty());
    REQUIRE(project.inputs.size() == 1);
    REQUIRE(project.inputs[0].kind == "Source");
    REQUIRE(project.inputs[0].role == "Source");
    REQUIRE(project.inputs[0].path == "src");
    REQUIRE(project.inputs[0].mode == "Directory");
}

TEST_CASE("new command creates product-first project skeletons")
{
    TempDir temp{};

    REQUIRE(CmdNew(temp.path(), "app", "Hello.Native") == 0);

    const auto projectPath = temp.path() / "Hello.Native/Hello.Native.nginproj";
    REQUIRE(fs::exists(projectPath));
    REQUIRE(fs::exists(temp.path() / "Hello.Native/src/main.cpp"));

    const auto project = LoadProjectManifest(projectPath);
    REQUIRE(project.productKind == "Application");
    REQUIRE(project.output.kind == "Executable");

    REQUIRE(CmdNew(temp.path(), "lib", "Game.Engine") == 0);
    const auto libraryPath = temp.path() / "Game.Engine/Game.Engine.nginproj";
    REQUIRE(fs::exists(libraryPath));
    REQUIRE(fs::exists(temp.path() / "Game.Engine/include/Game.Engine.hpp"));
    REQUIRE(fs::exists(temp.path() / "Game.Engine/src/Game.Engine.cpp"));

    const auto library = LoadProjectManifest(libraryPath);
    REQUIRE(library.productKind == "Library");
    REQUIRE(library.output.kind == "StaticLibrary");
}

TEST_CASE("package add update and remove edit Uses package dependencies")
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
    args.packageName = "NGIN.Core";
    args.versionRange = "[0.1.0,0.2.0)";
    args.scope = "Target;Runtime";

    REQUIRE(CmdPackageAdd(temp.path(), args) == 0);

    const auto text = ReadFile(projectPath);
    REQUIRE_THAT(text, ContainsSubstring(R"(<Application>)"));
    REQUIRE_THAT(text, ContainsSubstring(R"(<Uses>)"));
    REQUIRE_THAT(text, ContainsSubstring(R"xml(<Package Name="NGIN.Core" Version="[0.1.0,0.2.0)" Scope="Target;Runtime" />)xml"));

    const auto project = LoadProjectManifest(projectPath);
    REQUIRE(project.packageRefs.size() == 1);
    REQUIRE(project.packageRefs[0].name == "NGIN.Core");
    REQUIRE(project.packageRefs[0].versionRange == "[0.1.0,0.2.0)");
    REQUIRE(project.packageRefs[0].scope == "Target;Runtime");

    ParsedArgs updateArgs{};
    updateArgs.projectPath = projectPath.string();
    updateArgs.packageName = "NGIN.Core";
    updateArgs.versionRange = "[0.2.0,0.3.0)";
    updateArgs.scope = "Build";

    REQUIRE(CmdPackageUpdate(temp.path(), updateArgs) == 0);

    const auto updatedProject = LoadProjectManifest(projectPath);
    REQUIRE(updatedProject.packageRefs.size() == 1);
    REQUIRE(updatedProject.packageRefs[0].name == "NGIN.Core");
    REQUIRE(updatedProject.packageRefs[0].versionRange == "[0.2.0,0.3.0)");
    REQUIRE(updatedProject.packageRefs[0].scope == "Build");

    ParsedArgs removeArgs{};
    removeArgs.projectPath = projectPath.string();
    removeArgs.packageName = "NGIN.Core";

    REQUIRE(CmdPackageRemove(temp.path(), removeArgs) == 0);

    const auto removedProject = LoadProjectManifest(projectPath);
    REQUIRE(removedProject.packageRefs.empty());
    REQUIRE_THAT(ReadFile(projectPath), !ContainsSubstring(R"(Name="NGIN.Core")"));
}

TEST_CASE("project-reference add edits Uses project dependencies")
{
    TempDir temp{};
    const auto appPath = temp.path() / "App/App.nginproj";
    const auto libraryPath = temp.path() / "Lib/Lib.nginproj";
    WriteFile(appPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="App">
  <Application />
</Project>
)xml");
    WriteFile(libraryPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Lib">
  <Library />
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");
    WriteFile(temp.path() / "Lib/src/lib.cpp", "void lib() {}\n");

    ParsedArgs args{};
    args.projectPath = appPath.string();
    args.packageName = "../Lib/Lib.nginproj";

    REQUIRE(CmdProjectReferenceAdd(temp.path(), args) == 0);

    const auto app = LoadProjectManifest(appPath);
    REQUIRE(app.projectRefs.size() == 1);
    REQUIRE(app.projectRefs.front().path == fs::weakly_canonical(libraryPath));
    REQUIRE_THAT(ReadFile(appPath), ContainsSubstring(R"(<Project Name="Lib" Path="../Lib/Lib.nginproj" />)"));
}

TEST_CASE("format command rewrites product manifests with deterministic XML layout")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Format.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?><Project SchemaVersion="4" Name="Format.App"><Application><Build><Sources Path="src/**.cpp" /><Define Name="FORMAT_APP" Value="1" /></Build></Application></Project>)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");

    ParsedArgs args{};
    args.projectPath = projectPath.string();

    REQUIRE(CmdFormat(temp.path(), args) == 0);

    const auto formatted = ReadFile(projectPath);
    REQUIRE_THAT(formatted, ContainsSubstring("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n\n"));
    REQUIRE_THAT(formatted, ContainsSubstring("  <Application>\n"));
    REQUIRE_THAT(formatted, ContainsSubstring("      <Sources Path=\"src/**.cpp\" />\n"));
    REQUIRE_THAT(formatted, ContainsSubstring("      <Define Name=\"FORMAT_APP\" Value=\"1\" />\n"));

    const auto project = LoadProjectManifest(projectPath);
    REQUIRE(project.name == "Format.App");
    REQUIRE(project.build.compileDefinitions.size() == 1);
    REQUIRE(project.build.compileDefinitions.front().value == "FORMAT_APP=1");
}

TEST_CASE("schema command emits editor metadata")
{
    TempDir temp{};
    ParsedArgs args{};
    args.format = "json";

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdSchema(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("schemaVersion": "4.0")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("productKinds": ["Application", "Library", "Tool", "Test", "Benchmark", "Plugin", "Module", "External"])"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("dependencyScopes": ["Build", "Target", "Runtime", "Test", "Dev", "Publish"])"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("overlayOperations": ["Remove"])"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("Application": ["Runtime", "Launch", "Publish"])"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("publishKinds": ["Folder", "Archive"])"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("archiveFormats": ["zip"])"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("environment")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("publish")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("package-output")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("convention")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("env")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("analyzer")"));
}

TEST_CASE("package pack writes package manifest from PackageOutput")
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
        <License>MIT</License>
      </Metadata>
      <Exports>
        <Headers Path="include/**.hpp" />
        <Library Name="Game::Engine" />
        <Capability Name="Game.Engine" />
      </Exports>
      <Compatibility>
        <TargetPlatform Name="linux-x64" />
        <Abi Tag="linux-x64-clang18-libc++-cxx23" />
      </Compatibility>
    </PackageOutput>
  </Library>
</Project>
)xml");

    ParsedArgs args{};
    args.projectPath = projectPath.string();
    args.outputPath = (temp.path() / "out").string();

    REQUIRE(CmdPackagePack(temp.path(), args) == 0);

    const auto packagePath = temp.path() / "out/Game.Engine.nginpkg";
    const auto archivePath = temp.path() / "out/Game.Engine.nginpack";
    REQUIRE(fs::exists(packagePath));
    REQUIRE(fs::exists(archivePath));
    REQUIRE_THAT(ReadFile(packagePath), ContainsSubstring(R"(<Package SchemaVersion="4" Name="Game.Engine" Version="1.0.0">)"));
    REQUIRE_THAT(ReadFile(packagePath), ContainsSubstring(R"(<LibraryTarget Name="Game::Engine" />)"));
    REQUIRE_THAT(ReadFile(packagePath), ContainsSubstring(R"(<Capability Name="Game.Engine" />)"));
    REQUIRE_THAT(ReadFile(archivePath), ContainsSubstring("NGINPACK/1"));
    REQUIRE_THAT(ReadFile(archivePath), ContainsSubstring("Manifest: package.nginpkg"));
    REQUIRE_THAT(ReadFile(archivePath), ContainsSubstring(R"(<Package SchemaVersion="4" Name="Game.Engine" Version="1.0.0">)"));

    const auto package = LoadPackageManifest(packagePath);
    REQUIRE(package.name == "Game.Engine");
    REQUIRE(package.version == "1.0.0");
    REQUIRE(package.artifacts.libraries.size() == 1);
    REQUIRE(package.artifacts.libraries[0].target == "Game::Engine");

    const auto archivedPackage = LoadPackageManifest(archivePath);
    REQUIRE(archivedPackage.name == "Game.Engine");
    REQUIRE(archivedPackage.version == "1.0.0");
}

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

TEST_CASE("conditions support When nodes and item selectors")
{
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

    REQUIRE(std::any_of(project.conditions.begin(),
                        project.conditions.end(),
                        [](const ConditionDefinition &condition)
                        { return condition.name == "linux-debug"; }));
    REQUIRE(project.build.compileDefinitions.size() == 1);
    REQUIRE(project.build.compileDefinitions[0].value == "GAME_LINUX_DEBUG=1");
    REQUIRE(project.build.compileDefinitions[0].selectors.conditionRefs.size() == 1);
    REQUIRE(project.build.compileDefinitions[0].selectors.conditionRefs[0] == "linux-debug");
}

TEST_CASE("workspace parses projects, package sources, and central package versions")
{
    TempDir temp{};
    WriteFile(temp.path() / "build/platforms.ngin.xml",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Definitions SchemaVersion="4" Name="Game.Platforms">
  <Platforms>
    <Platform Name="linux-x64"
              OperatingSystem="linux"
              Architecture="x64"
              Abi="linux-x64-clang18-libc++-cxx23" />
  </Platforms>
  <Toolchains>
    <Toolchain Name="clang-lld"
               Compiler="clang"
               CompilerVersion="18"
               Linker="lld"
               Generator="Ninja"
               CppStandardLibrary="libc++" />
  </Toolchains>
</Definitions>
)xml");
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="Game" DefaultProfile="dev">
  <Imports>
    <Import Path="build/platforms.ngin.xml" />
  </Imports>
  <Projects>
    <Project Path="Game.Client/Game.Client.nginproj" />
  </Projects>
  <Packages>
    <Source Name="local" Path="packages" />
    <Source Name="remote" Url="https://packages.example.invalid/v1/index.json" />
    <Version Name="NGIN.Core" Range="[0.1.0,0.2.0)" />
  </Packages>
</Workspace>
)xml");

    const auto workspace = LoadWorkspaceManifest(temp.path());

    REQUIRE(workspace.name == "Game");
    REQUIRE(workspace.defaultProfile == "dev");
    REQUIRE(workspace.imports.size() == 1);
    REQUIRE(workspace.platforms.size() == 1);
    REQUIRE(workspace.platforms[0].name == "linux-x64");
    REQUIRE(workspace.platforms[0].abi == "linux-x64-clang18-libc++-cxx23");
    REQUIRE(workspace.toolchains.size() == 1);
    REQUIRE(workspace.toolchains[0].name == "clang-lld");
    REQUIRE(workspace.toolchains[0].compiler == "clang");
    REQUIRE(workspace.projects.size() == 1);
    REQUIRE(workspace.projects[0] == (temp.path() / "Game.Client/Game.Client.nginproj").lexically_normal());
    REQUIRE(workspace.packageSources.size() == 1);
    REQUIRE(workspace.packageSources[0] == (temp.path() / "packages").lexically_normal());
    REQUIRE(workspace.packageSourceUrls.size() == 1);
    REQUIRE(workspace.packageSourceUrls[0] == "https://packages.example.invalid/v1/index.json");
    REQUIRE(workspace.dependencyVersions.at("NGIN.Core") == "[0.1.0,0.2.0)");
}

TEST_CASE("workspace default profile participates in command profile selection")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="ProfileWorkspace" DefaultProfile="shipping">
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
</Workspace>
)xml");
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Profile.App" DefaultProfile="dev">
  <Application>
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
  </Profile>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    ParsedArgs args{};
    args.projectPath = (temp.path() / "App/App.nginproj").string();
    args.format = "json";

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdInspect(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("profile":"shipping")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("name":"BuildType","value":"Release")"));
}

TEST_CASE("workspace profile policy applies to projects without local profiles")
{
    TempDir temp{};
    WriteFile(temp.path() / "build/platforms.ngin.xml",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Definitions SchemaVersion="4" Name="Policy.Platforms">
  <Platforms>
    <Platform Name="windows-x64"
              OperatingSystem="windows"
              Architecture="x64"
              Abi="windows-x64-msvc-v143-md-cxx23" />
  </Platforms>
</Definitions>
)xml");
    WriteFile(temp.path() / "build/toolchains.ngin.xml",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Definitions SchemaVersion="4" Name="Policy.Toolchains">
  <Toolchains>
    <Toolchain Name="msvc-v143"
               Compiler="msvc"
               CompilerVersion="v143"
               Linker="link"
               Generator="Ninja"
               RuntimeLibrary="MD" />
  </Toolchains>
</Definitions>
)xml");
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="WorkspacePolicy" DefaultProfile="shipping">
  <Imports>
    <Import Path="build/platforms.ngin.xml" />
    <Import Path="build/toolchains.ngin.xml" />
  </Imports>
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
  <Packages>
    <Source Path="Packages" />
  </Packages>
  <Profiles>
    <Profile Name="shipping">
      <Defaults>
        <BuildType Name="Release" />
        <TargetPlatform Name="windows-x64" />
        <HostPlatform Name="host" />
        <Toolchain Name="msvc-v143" />
        <Environment Name="production" />
      </Defaults>
      <Build>
        <Define Name="WORKSPACE_SHIPPING" Value="1" />
      </Build>
      <Quality>
        <Analyzer Name="workspace-clang-tidy" Severity="Error">
          <Config Path=".clang-tidy" />
        </Analyzer>
      </Quality>
      <Environment>
        <Env Name="WORKSPACE_ENV" Value="production" />
      </Environment>
      <Stage>
        <Config Source="config/workspace.json"
                Target="config/app.json"
                Collision="Override" />
      </Stage>
      <Uses>
        <Package Name="Workspace.Policy.Package"
                 Version="[1.0.0,2.0.0)"
                 Scope="Dev">
          <Feature Name="Diagnostics" />
        </Package>
      </Uses>
      <Application>
        <Build>
          <Define Name="WORKSPACE_APPLICATION" Value="1" />
        </Build>
        <Quality>
          <Analyzer Name="workspace-app-analyzer" Severity="Error" />
        </Quality>
        <Environment>
          <Env Name="WORKSPACE_APP_ENV" Value="1" />
        </Environment>
        <Stage>
          <Content Source="assets/**"
                   Target="assets" />
        </Stage>
        <Uses>
          <Tool Name="Workspace.Policy.Tool"
                Version="[1.0.0,2.0.0)" />
        </Uses>
        <Runtime>
          <Module Name="Workspace.Policy.Module"
                  Stage="Startup">
            <Provides Service="Workspace.Policy.Ready" />
          </Module>
        </Runtime>
      </Application>
      <Library>
        <Build>
          <Define Name="WORKSPACE_LIBRARY_ONLY" Value="1" />
        </Build>
      </Library>
    </Profile>
  </Profiles>
</Workspace>
)xml");
    WriteFile(temp.path() / "Packages/Workspace.Policy.Package/Workspace.Policy.Package.nginpkg",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Workspace.Policy.Package" Version="1.0.0">
  <Library Name="Workspace.Policy.Package">
    <Exports>
      <LibraryTarget Name="Workspace::Policy::Package" />
    </Exports>
  </Library>
  <Features>
    <Feature Name="Diagnostics">
      <Build>
        <Define Name="WORKSPACE_POLICY_DIAGNOSTICS" Value="1" Visibility="Public" />
      </Build>
    </Feature>
  </Features>
</Package>
)xml");
    WriteFile(temp.path() / "Packages/Workspace.Policy.Tool/Workspace.Policy.Tool.nginpkg",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Workspace.Policy.Tool" Version="1.0.0">
  <Tool Name="Workspace.Policy.Tool">
    <Exports>
      <Tool Name="Workspace.Policy.Tool" Executable="workspace-policy-tool" />
    </Exports>
  </Tool>
</Package>
)xml");
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Workspace.Policy.App">
  <Application />
</Project>
)xml");
    WriteFile(temp.path() / "App/config/workspace.json", "{}\n");
    WriteFile(temp.path() / "App/assets/readme.txt", "asset\n");

    ParsedArgs args{};
    args.projectPath = (temp.path() / "App/App.nginproj").string();
    args.format = "json";

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdInspect(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("profile":"shipping")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("name":"BuildType","value":"Release")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("targetPlatform":"windows-x64")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("toolchain":"msvc-v143")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("abiTag":"windows-x64-msvc-v143-md-cxx23")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("environment":"production")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("name":"WORKSPACE_ENV","value":"production")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("name":"WORKSPACE_APP_ENV","value":"1")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("target":"config/app.json")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("target":"assets/readme.txt")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("runtimeModules":1)"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("name":"Workspace.Policy.Package","version":"1.0.0")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("scope":"Dev")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("name":"Workspace.Policy.Tool","version":"1.0.0")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("scope":"Build")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("package":"Workspace.Policy.Package")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("feature":"Diagnostics")"));

    ParsedArgs explainArgs{};
    explainArgs.projectPath = args.projectPath;
    explainArgs.profileName = "shipping";
    explainArgs.packageName = "define:WORKSPACE_SHIPPING";
    std::ostringstream explainCaptured{};
    previous = std::cout.rdbuf(explainCaptured.rdbuf());
    const auto explainExitCode = CmdExplainObject(temp.path(), explainArgs);
    std::cout.rdbuf(previous);

    REQUIRE(explainExitCode == 0);
    REQUIRE_THAT(explainCaptured.str(), ContainsSubstring("value: WORKSPACE_SHIPPING=1"));

    explainArgs.packageName = "define:WORKSPACE_APPLICATION";
    std::ostringstream appExplainCaptured{};
    previous = std::cout.rdbuf(appExplainCaptured.rdbuf());
    const auto appExplainExitCode = CmdExplainObject(temp.path(), explainArgs);
    std::cout.rdbuf(previous);

    REQUIRE(appExplainExitCode == 0);
    REQUIRE_THAT(appExplainCaptured.str(), ContainsSubstring("value: WORKSPACE_APPLICATION=1"));

    explainArgs.packageName = "define:WORKSPACE_LIBRARY_ONLY";
    std::ostringstream libraryExplainCaptured{};
    previous = std::cout.rdbuf(libraryExplainCaptured.rdbuf());
    const auto libraryExplainExitCode = CmdExplainObject(temp.path(), explainArgs);
    std::cout.rdbuf(previous);

    REQUIRE(libraryExplainExitCode == 0);
    REQUIRE_THAT(libraryExplainCaptured.str(), ContainsSubstring("result: not selected"));

    ParsedArgs analyzeArgs{};
    analyzeArgs.projectPath = args.projectPath;
    analyzeArgs.profileName = "shipping";
    std::ostringstream analyzeCaptured{};
    previous = std::cout.rdbuf(analyzeCaptured.rdbuf());
    const auto analyzeExitCode = CmdAnalyze(temp.path(), analyzeArgs);
    std::cout.rdbuf(previous);

    REQUIRE(analyzeExitCode == 0);
    REQUIRE_THAT(analyzeCaptured.str(), ContainsSubstring("analyzer workspace-clang-tidy scope=Build severity=Error config=.clang-tidy"));
    REQUIRE_THAT(analyzeCaptured.str(), ContainsSubstring("analyzer workspace-app-analyzer scope=Build severity=Error"));

    ParsedArgs runtimeExplainArgs{};
    runtimeExplainArgs.projectPath = args.projectPath;
    runtimeExplainArgs.profileName = "shipping";
    runtimeExplainArgs.packageName = "runtime-module:Workspace.Policy.Module";
    std::ostringstream runtimeExplainCaptured{};
    previous = std::cout.rdbuf(runtimeExplainCaptured.rdbuf());
    const auto runtimeExplainExitCode = CmdExplainObject(temp.path(), runtimeExplainArgs);
    std::cout.rdbuf(previous);

    REQUIRE(runtimeExplainExitCode == 0);
    REQUIRE_THAT(runtimeExplainCaptured.str(), ContainsSubstring("result: selected"));
}

TEST_CASE("workspace build defaults apply unless project declares explicit build settings")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="BuildDefaults">
  <Projects>
    <Project Path="Inherited/Inherited.nginproj" />
    <Project Path="Explicit/Explicit.nginproj" />
  </Projects>
  <Defaults>
    <Language Standard="C++20" Required="true" Extensions="false" />
    <Backend Name="CMake" Mode="Generated" />
  </Defaults>
</Workspace>
)xml");
    WriteFile(temp.path() / "Inherited/Inherited.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Inherited.App">
  <Application />
</Project>
)xml");
    WriteFile(temp.path() / "Inherited/src/main.cpp", "int main() { return 0; }\n");
    WriteFile(temp.path() / "Explicit/Explicit.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Explicit.App">
  <Application>
    <Build>
      <Language Standard="C++23" />
    </Build>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "Explicit/src/main.cpp", "int main() { return 0; }\n");

    const std::optional<std::string> selectedProfile{};
    const auto inherited = LoadProjectManifest(temp.path() / "Inherited/Inherited.nginproj");
    const auto &inheritedProfile = ProfileByName(inherited, selectedProfile);
    const auto inheritedResolved = ResolveLaunch(inherited, inheritedProfile);

    REQUIRE_FALSE(inheritedResolved.diagnostics.HasErrors());
    REQUIRE(inheritedResolved.value.has_value());
    REQUIRE(inheritedResolved.value->project.build.language == "CXX");
    REQUIRE(inheritedResolved.value->project.build.languageStandard == "20");
    REQUIRE(inheritedResolved.value->project.build.backend == "CMake");
    REQUIRE(inheritedResolved.value->project.build.mode == "Generated");

    const auto explicitProject = LoadProjectManifest(temp.path() / "Explicit/Explicit.nginproj");
    const auto &explicitProfile = ProfileByName(explicitProject, selectedProfile);
    const auto explicitResolved = ResolveLaunch(explicitProject, explicitProfile);

    REQUIRE_FALSE(explicitResolved.diagnostics.HasErrors());
    REQUIRE(explicitResolved.value.has_value());
    REQUIRE(explicitResolved.value->project.build.language == "CXX");
    REQUIRE(explicitResolved.value->project.build.languageStandard == "23");
}

TEST_CASE("package sources list reports workspace package sources")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="SourceWorkspace">
  <Projects>
  </Projects>
  <Packages>
    <Source Name="local" Path="Packages" />
    <Source Name="remote" Url="https://packages.example.invalid/v1/index.json" />
    <PackageProvider Name="Package.Core" Path="Providers/Core" />
  </Packages>
</Workspace>
)xml");
    fs::create_directories(temp.path() / "Packages");
    fs::create_directories(temp.path() / "Providers/Core");

    ParsedArgs args{};
    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdPackageSourcesList(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring("Package sources for workspace: SourceWorkspace"));
    REQUIRE_THAT(captured.str(), ContainsSubstring((temp.path() / "Packages").lexically_normal().string()));
    REQUIRE_THAT(captured.str(), ContainsSubstring("https://packages.example.invalid/v1/index.json"));
    REQUIRE_THAT(captured.str(), ContainsSubstring("Package.Core ->"));
}

TEST_CASE("package sources add and remove edit workspace sources")
{
    TempDir temp{};
    const auto workspacePath = temp.path() / "Workspace.ngin";
    WriteFile(workspacePath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="SourceEditWorkspace">
  <Projects>
  </Projects>
</Workspace>
)xml");

    ParsedArgs addArgs{};
    addArgs.packageName = "local";
    addArgs.featureName = "Packages";

    std::ostringstream addCaptured{};
    auto *previous = std::cout.rdbuf(addCaptured.rdbuf());
    const auto addExitCode = CmdPackageSourcesAdd(temp.path(), addArgs);
    std::cout.rdbuf(previous);

    REQUIRE(addExitCode == 0);
    REQUIRE_THAT(addCaptured.str(), ContainsSubstring("Added package source"));

    const auto afterAdd = ReadFile(workspacePath);
    REQUIRE_THAT(afterAdd, ContainsSubstring(R"(<Packages>)"));
    REQUIRE_THAT(afterAdd, ContainsSubstring(R"(<Source Name="local" Path="Packages" />)"));

    const auto workspace = LoadWorkspaceManifest(temp.path());
    REQUIRE(workspace.packageSources.size() == 1);
    REQUIRE(workspace.packageSources.front() == (temp.path() / "Packages").lexically_normal());

    ParsedArgs removeArgs{};
    removeArgs.packageName = "local";

    std::ostringstream removeCaptured{};
    previous = std::cout.rdbuf(removeCaptured.rdbuf());
    const auto removeExitCode = CmdPackageSourcesRemove(temp.path(), removeArgs);
    std::cout.rdbuf(previous);

    REQUIRE(removeExitCode == 0);
    REQUIRE_THAT(removeCaptured.str(), ContainsSubstring("Removed package source"));

    const auto afterRemove = ReadFile(workspacePath);
    REQUIRE_THAT(afterRemove, !ContainsSubstring(R"(Name="local")"));
}

TEST_CASE("file URL package source participates in package catalog")
{
    TempDir temp{};
    const auto feedRoot = temp.path() / "feed";
    WriteFile(temp.path() / "Workspace.ngin",
              "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
              "<Workspace SchemaVersion=\"4\" Name=\"FileFeedWorkspace\">\n"
              "  <Projects>\n"
              "    <Project Path=\"App/App.nginproj\" />\n"
              "  </Projects>\n"
              "  <Packages>\n"
              "    <Source Name=\"feed\" Url=\"file://" + feedRoot.generic_string() + "\" />\n"
              "  </Packages>\n"
              "</Workspace>\n");
    WriteFile(feedRoot / "Core/Core.nginpkg",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Package.Core" Version="1.0.0">
  <Library Name="Package.Core">
    <Exports>
      <LibraryTarget Name="Package::Core" />
    </Exports>
  </Library>
</Package>
)xml");
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="FileFeed.App">
  <Application>
    <Uses>
      <Package Name="Package.Core" Version="[1.0.0,2.0.0)" Scope="Target" />
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
    const std::optional<std::string> selectedProfile{};
    const auto &profile = ProfileByName(project, selectedProfile);
    const auto resolved = ResolveLaunch(project, profile);

    REQUIRE_FALSE(resolved.diagnostics.HasErrors());
    REQUIRE(resolved.value.has_value());
    REQUIRE(resolved.value->orderedPackages.size() == 1);
    REQUIRE(resolved.value->orderedPackages.front().manifest.name == "Package.Core");
}

TEST_CASE("static package feed index participates in package restore")
{
    TempDir temp{};
    const auto feedRoot = temp.path() / "feed";
    const auto feedIndex = feedRoot / "index.nginfeed";
    WriteFile(temp.path() / "Workspace.ngin",
              "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
              "<Workspace SchemaVersion=\"4\" Name=\"StaticFeedWorkspace\">\n"
              "  <Projects>\n"
              "    <Project Path=\"App/App.nginproj\" />\n"
              "  </Projects>\n"
              "  <Packages>\n"
              "    <Source Name=\"feed\" Url=\"file://" + feedIndex.generic_string() + "\" />\n"
              "  </Packages>\n"
              "</Workspace>\n");
    const std::string packageManifest = R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Package.Core" Version="1.0.0">
  <Library Name="Package.Core">
    <Exports>
      <LibraryTarget Name="Package::Core" />
    </Exports>
  </Library>
</Package>
)xml";
    WriteFile(feedRoot / "Core/Core.nginpack",
              std::string("NGINPACK/1\n")
                  + "Name: Package.Core\n"
                  + "Version: 1.0.0\n"
                  + "Manifest: package.nginpkg\n"
                  + "Manifest-Length: " + std::to_string(packageManifest.size()) + "\n\n"
                  + packageManifest);
    WriteFile(feedIndex,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<PackageFeed SchemaVersion="4">
  <Packages>
    <Package Name="Package.Core" Version="1.0.0" Path="Core/Core.nginpack" />
  </Packages>
</PackageFeed>
)xml");
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="StaticFeed.App">
  <Application>
    <Uses>
      <Package Name="Package.Core" Version="[1.0.0,2.0.0)" Scope="Target" />
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    ParsedArgs args{};
    args.projectPath = (temp.path() / "App/App.nginproj").string();
    args.outputPath = (temp.path() / "store").string();

    REQUIRE(CmdRestore(temp.path(), args) == 0);
    REQUIRE(fs::exists(temp.path() / "store/Package.Core/1.0.0/Core.nginpack"));
    REQUIRE(fs::exists(temp.path() / "store/Package.Core/1.0.0/package.nginpkg"));
}

TEST_CASE("package manifest parses product exports and feature contributions")
{
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
    REQUIRE(package.features[0].build.compileDefinitions[0].value == "NGIN_CORE_REFLECTION=1");
    REQUIRE(package.features[0].provides.size() == 1);
    REQUIRE(package.features[0].provides[0].name == "Reflection");
}

TEST_CASE("package manifest parses tool exports")
{
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

TEST_CASE("resolved package scopes flow into graph metadata")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="ScopeWorkspace">
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
  <Packages>
    <Source Name="local" Path="Packages" />
  </Packages>
</Workspace>
)xml");
    const std::string coreManifest = R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Package.Core" Version="1.0.0">
  <Library Name="Package.Core">
    <Exports>
      <LibraryTarget Name="Package::Core" />
    </Exports>
  </Library>
</Package>
)xml";
    WriteFile(temp.path() / "Packages/Core/Core.nginpack",
              "NGINPACK/1\n"
              "Name: Package.Core\n"
              "Version: 1.0.0\n"
              "Manifest: package.nginpkg\n"
              "Manifest-Length: "
                  + std::to_string(coreManifest.size()) + "\n\n"
                  + coreManifest);
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Scope.App">
  <Application>
    <Uses>
      <Package Name="Package.Core" Version="[1.0.0,2.0.0)" Scope="Build;Target;Runtime" />
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
    const std::optional<std::string> selectedProfile{};
    const auto &profile = ProfileByName(project, selectedProfile);
    const auto resolved = ResolveLaunch(project, profile);

    const auto diagnostics = DiagnosticMessages(resolved.diagnostics);
    const auto firstDiagnostic = diagnostics.empty() ? std::string{"no diagnostics"} : diagnostics.front();
    INFO(firstDiagnostic);
    REQUIRE_FALSE(resolved.diagnostics.HasErrors());
    REQUIRE(resolved.value.has_value());
    REQUIRE(resolved.value->packageScopes.at("Package.Core") == "Build;Runtime;Target");

    ParsedArgs inspectArgs{};
    inspectArgs.projectPath = (temp.path() / "App/App.nginproj").string();
    inspectArgs.format = "json";
    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdInspect(temp.path(), inspectArgs);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("closures":["Host","Target","Runtime"])"));

    ParsedArgs graphArgs{};
    graphArgs.projectPath = (temp.path() / "App/App.nginproj").string();
    graphArgs.format = "json";
    graphArgs.graphPlan = "package";
    std::ostringstream graphCaptured{};
    previous = std::cout.rdbuf(graphCaptured.rdbuf());
    const auto graphExitCode = CmdGraph(temp.path(), graphArgs);
    std::cout.rdbuf(previous);

    REQUIRE(graphExitCode == 0);
    REQUIRE_THAT(graphCaptured.str(), ContainsSubstring(R"("plan": "package")"));
    REQUIRE_THAT(graphCaptured.str(), ContainsSubstring(R"("name":"Package.Core","version":"1.0.0")"));
    REQUIRE_THAT(graphCaptured.str(), ContainsSubstring(R"("closures":["Host","Target","Runtime"])"));
    REQUIRE_THAT(graphCaptured.str(), ContainsSubstring(R"("reason":"resolved package dependency")"));
}

TEST_CASE("package resolution reports conflicting dependency version ranges")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="ConflictWorkspace">
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
  <Packages>
    <Source Name="local" Path="Packages" />
  </Packages>
</Workspace>
)xml");
    const std::string coreManifest = R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Package.Core" Version="1.0.0">
  <Library Name="Package.Core">
    <Exports>
      <LibraryTarget Name="Package::Core" />
    </Exports>
  </Library>
</Package>
)xml";
    WriteFile(temp.path() / "Packages/Core/Core.nginpack",
              std::string("NGINPACK/1\n")
                  + "Name: Package.Core\n"
                  + "Version: 1.0.0\n"
                  + "Manifest: package.nginpkg\n"
                  + "Manifest-Length: " + std::to_string(coreManifest.size()) + "\n\n"
                  + coreManifest);
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Conflict.App">
  <Application>
    <Uses>
      <Package Name="Package.Core" Version="[1.0.0,2.0.0)" Scope="Target" />
      <Package Name="Package.Core" Version="[2.0.0,3.0.0)" Scope="Runtime" />
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
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
            return message.find("conflicting version ranges") != std::string::npos;
        }));
}

TEST_CASE("package resolution validates later transitive package ranges")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="TransitiveConflictWorkspace">
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
  <Packages>
    <Source Name="local" Path="Packages" />
  </Packages>
</Workspace>
)xml");
    const std::string coreManifest = R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Package.Core" Version="1.0.0">
  <Library Name="Package.Core">
    <Exports>
      <LibraryTarget Name="Package::Core" />
    </Exports>
  </Library>
</Package>
)xml";
    WriteFile(temp.path() / "Packages/Core/Core.nginpack",
              std::string("NGINPACK/1\n")
                  + "Name: Package.Core\n"
                  + "Version: 1.0.0\n"
                  + "Manifest: package.nginpkg\n"
                  + "Manifest-Length: " + std::to_string(coreManifest.size()) + "\n\n"
                  + coreManifest);
    WriteFile(temp.path() / "Packages/Feature/Feature.nginpkg",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Package.Feature" Version="1.0.0">
  <Uses>
    <Package Name="Package.Core" Version="[2.0.0,3.0.0)" Scope="Target" />
  </Uses>
  <Library Name="Package.Feature">
    <Exports>
      <LibraryTarget Name="Package::Feature" />
    </Exports>
  </Library>
</Package>
)xml");
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Transitive.Conflict.App">
  <Application>
    <Uses>
      <Package Name="Package.Core" Version="[1.0.0,2.0.0)" Scope="Target" />
      <Package Name="Package.Feature" Version="[1.0.0,2.0.0)" Scope="Target" />
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
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
            return message.find("does not satisfy later range '[2.0.0,3.0.0)'") != std::string::npos;
        }));
}

TEST_CASE("package provider override is exposed in resolved package metadata")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="ProviderWorkspace">
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
  <Packages>
    <Source Name="local" Path="Packages" />
    <PackageProvider Name="Package.Core" Path="Providers/Core" />
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
</Package>
)xml");
    fs::create_directories(temp.path() / "Providers/Core");
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Provider.App">
  <Application>
    <Uses>
      <Package Name="Package.Core" Version="[1.0.0,2.0.0)" Scope="Target" />
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
    const std::optional<std::string> selectedProfile{};
    const auto &profile = ProfileByName(project, selectedProfile);
    const auto resolved = ResolveLaunch(project, profile);

    REQUIRE_FALSE(resolved.diagnostics.HasErrors());
    REQUIRE(resolved.value.has_value());
    REQUIRE(resolved.value->orderedPackages.size() == 1);
    REQUIRE(resolved.value->orderedPackages[0].source == "provider");
    REQUIRE(resolved.value->orderedPackages[0].sourceDirectory == (temp.path() / "Providers/Core").lexically_normal());

    ParsedArgs args{};
    args.projectPath = (temp.path() / "App/App.nginproj").string();
    args.format = "json";
    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdInspect(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("source":"provider")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring((temp.path() / "Providers/Core").lexically_normal().string()));
}

TEST_CASE("restore writes package store and lock file")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="RestoreWorkspace">
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
  <Packages>
    <Source Name="local" Path="Packages" />
  </Packages>
</Workspace>
)xml");
    const std::string restoreCoreManifest = R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Package.Core" Version="1.0.0">
  <Library Name="Package.Core">
    <Exports>
      <LibraryTarget Name="Package::Core" />
    </Exports>
  </Library>
</Package>
)xml";
    WriteFile(temp.path() / "Packages/Core/Core.nginpack",
              std::string("NGINPACK/1\n")
                  + "Name: Package.Core\n"
                  + "Version: 1.0.0\n"
                  + "Manifest: package.nginpkg\n"
                  + "Manifest-Length: " + std::to_string(restoreCoreManifest.size()) + "\n\n"
                  + restoreCoreManifest);
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Restore.App">
  <Application>
    <Uses>
      <Package Name="Package.Core" Version="[1.0.0,2.0.0)" Scope="Target" />
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

    ParsedArgs args{};
    args.projectPath = (temp.path() / "App/App.nginproj").string();
    args.outputPath = (temp.path() / "store").string();

    REQUIRE(CmdRestore(temp.path(), args) == 0);
    REQUIRE(fs::exists(temp.path() / "store/Package.Core/1.0.0/Core.nginpack"));
    REQUIRE(fs::exists(temp.path() / "store/Package.Core/1.0.0/package.nginpkg"));
    REQUIRE_THAT(ReadFile(temp.path() / "store/Package.Core/1.0.0/package.nginpkg"),
                 ContainsSubstring(R"(<Package SchemaVersion="4" Name="Package.Core" Version="1.0.0">)"));
    REQUIRE(fs::exists(temp.path() / "ngin.lock"));
    REQUIRE_THAT(ReadFile(temp.path() / "ngin.lock"), ContainsSubstring(R"(Scope="Target")"));

    ParsedArgs lockedArgs{};
    lockedArgs.projectPath = (temp.path() / "App/App.nginproj").string();
    lockedArgs.outputPath = (temp.path() / "locked-store").string();
    lockedArgs.locked = true;

    REQUIRE(CmdRestore(temp.path(), lockedArgs) == 0);
    REQUIRE(fs::exists(temp.path() / "locked-store/Package.Core/1.0.0/Core.nginpack"));
    REQUIRE(fs::exists(temp.path() / "locked-store/Package.Core/1.0.0/package.nginpkg"));

    WriteFile(temp.path() / "ngin.lock", "<Lock />\n");
    REQUIRE(CmdRestore(temp.path(), lockedArgs) == 1);
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
