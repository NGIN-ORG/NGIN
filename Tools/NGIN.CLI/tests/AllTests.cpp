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

    [[nodiscard]] auto ReadFile(const fs::path &path) -> std::string
    {
        std::ifstream input(path);
        std::ostringstream content{};
        content << input.rdbuf();
        return content.str();
    }

    [[nodiscard]] auto ContainsDiagnostic(const std::vector<std::string> &diagnostics, std::string_view text) -> bool
    {
        return std::any_of(diagnostics.begin(),
                           diagnostics.end(),
                           [&](const std::string &diagnostic)
                           {
                               return diagnostic.find(text) != std::string::npos;
                           });
    }

    [[nodiscard]] auto CountOccurrences(const std::string &text, const std::string &needle) -> std::size_t
    {
        std::size_t count = 0;
        std::size_t offset = 0;
        while ((offset = text.find(needle, offset)) != std::string::npos)
        {
            ++count;
            offset += needle.size();
        }
        return count;
    }

} // namespace

TEST_CASE("workspace, project, and package manifests parse through authoring "
          "facades")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="3" Name="TempWorkspace" PlatformVersion="0.1.0">
  <PackageSources>
    <PackageSource Path="Packages" />
  </PackageSources>
  <DependencyPolicy VersionResolution="HighestCompatible">
    <Versions>
      <Package Name="Package.Core" VersionRange=">=1.0.0 &lt;2.0.0" />
    </Versions>
  </DependencyPolicy>
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
</Workspace>
)");
    WriteFile(temp.path() / "Packages/Sample/Sample.nginpkg",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="3" Name="Sample.Package" Version="1.0.0">
  <Build Backend="CMake" Mode="Manual" />
</Package>
)");
    WriteFile(temp.path() / "App/App.nginproj",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Sample.App" Type="Application" DefaultProfile="Runtime">
  <Inputs>
    <Sources Path="src" />
  </Inputs>
  <Output Kind="Executable" Name="Sample.App" Target="SampleApp" />
  <References>
    <Package Name="Sample.Package" />
  </References>
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev">
      <Launch Executable="Sample.App" WorkingDirectory="." />
    </Profile>
  </Profiles>
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
    REQUIRE(package.modules.empty());
    REQUIRE(catalog.contains("Sample.Package"));
}

TEST_CASE("project generators parse and legacy metagen opt in is rejected")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Meta.App" Type="Application" DefaultProfile="Runtime">
  <Inputs>
    <Sources Path="src" />
  </Inputs>
  <Output Kind="Executable" Name="Meta.App" Target="MetaApp" />
  <Build Backend="CMake" Mode="Generated" Language="CXX" LanguageStandard="23" />
  <Generators>
    <Generator Name="ReflectionMetaGen" Kind="Command">
      <Tool Executable="tools/ngin-metagen" />
      <Arguments>
        <Arg Value="--context" />
        <Arg Path="$(GeneratorContext)" />
      </Arguments>
      <Outputs>
        <Generated Role="Source" Path="$(GeneratedDir)/reflection/$(ProjectName).reflection.generated.cpp" />
      </Outputs>
    </Generator>
  </Generators>
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Profiles>
</Project>
)xml");

    const auto project = LoadProjectManifest(projectPath);
    REQUIRE(project.generators.size() == 1);
    REQUIRE(project.generators.front().name == "ReflectionMetaGen");
    REQUIRE(project.generators.front().outputs.size() == 1);
    REQUIRE(project.generators.front().arguments.size() == 2);
    REQUIRE(project.generators.front().arguments.back().path == "$(GeneratorContext)");

    WriteFile(temp.path() / "BuiltInTool.nginpkg",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="3" Name="BuiltIn.Tool" Version="0.1.0">
  <Tools>
    <Tool Name="MetaGen" Kind="Generator" BuiltIn="MetaGen" />
  </Tools>
</Package>
)");
    REQUIRE_THROWS_WITH(LoadPackageManifest(temp.path() / "BuiltInTool.nginpkg"), ContainsSubstring("unsupported attribute 'BuiltIn'"));

    WriteFile(temp.path() / "KindMetaGen.nginproj",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Kind.MetaGen.App" Type="Application" DefaultProfile="Runtime">
  <Inputs>
    <Sources Path="src" />
  </Inputs>
  <Output Kind="Executable" Name="Kind.MetaGen.App" Target="KindMetaGenApp" />
  <Build Backend="CMake" Mode="Generated" />
  <Generators>
    <Generator Name="ReflectionMetaGen" Kind="MetaGen" Tool="MetaGen">
      <Outputs>
        <Generated Role="Source" Path="$(GeneratedDir)/reflection/Kind.MetaGen.App.reflection.generated.cpp" />
      </Outputs>
    </Generator>
  </Generators>
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Profiles>
</Project>
)");
    REQUIRE_THROWS_WITH(LoadProjectManifest(temp.path() / "KindMetaGen.nginproj"), ContainsSubstring("unsupported generator kind"));

    WriteFile(temp.path() / "Legacy.nginproj",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Legacy.Meta.App" Type="Application" DefaultProfile="Runtime">
  <Inputs>
    <Sources Path="src" />
  </Inputs>
  <Output Kind="Executable" Name="Legacy.Meta.App" Target="LegacyMetaApp" />
  <Build Backend="CMake" Mode="Generated">
    <MetaGen Enabled="true" />
  </Build>
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Profiles>
</Project>
)");
    REQUIRE_THROWS_WITH(LoadProjectManifest(temp.path() / "Legacy.nginproj"), ContainsSubstring("<Build><MetaGen> is no longer supported"));
}

TEST_CASE("package tools and command generators parse")
{
    TempDir temp{};
    const auto packagePath = temp.path() / "Generated.Tools.nginpkg";
    WriteFile(packagePath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="3" Name="Generated.Tools" Version="0.1.0">
  <Tools>
    <Tool Name="SchemaCompiler" Kind="Generator" Executable="bin/schema-compiler" />
  </Tools>
  <Features>
    <Feature Name="Schema">
      <Generators>
        <Generator Name="SchemaCodegen" Kind="Command" Tool="SchemaCompiler">
          <Arguments>
            <Arg Value="--input" />
            <Arg Path="schemas/app.schema.json" />
            <Arg Value="--output" />
            <Arg Path="$(GeneratedDir)/schema/app_schema.cpp" />
          </Arguments>
          <Inputs>
            <ToolInputs>
              schemas/app.schema.json
            </ToolInputs>
          </Inputs>
          <Outputs>
            <Generated Role="Source" Path="$(GeneratedDir)/schema/app_schema.cpp" />
          </Outputs>
        </Generator>
      </Generators>
    </Feature>
  </Features>
</Package>
)xml");

    const auto package = LoadPackageManifest(packagePath);
    REQUIRE(package.tools.size() == 1);
    REQUIRE(package.tools.front().name == "SchemaCompiler");
    REQUIRE(package.features.size() == 1);
    REQUIRE(package.features.front().generators.size() == 1);
    const auto &generator = package.features.front().generators.front();
    REQUIRE(generator.name == "SchemaCodegen");
    REQUIRE(generator.kind == "Command");
    REQUIRE(generator.toolName == "SchemaCompiler");
    REQUIRE(generator.arguments.size() == 4);
    REQUIRE(generator.inputs.size() == 1);
    REQUIRE(generator.outputs.size() == 1);
    REQUIRE(generator.outputs.front().role == "Source");
}

TEST_CASE("generator declarations require explicit outputs")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Missing.Output.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Missing.Output" Template="Application" DefaultProfile="Runtime">
  <Generators>
    <Generator Name="NoOutputs" Kind="Command">
      <Tool Executable="tools/schema-compiler" />
    </Generator>
  </Generators>
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" Platform="linux-x64" Environment="dev" />
  </Profiles>
</Project>
)xml");

    REQUIRE_THROWS_WITH(LoadProjectManifest(projectPath), ContainsSubstring("must declare <Outputs>"));
}

TEST_CASE("normalized project inputs parse selector metadata and reject legacy roots")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Typed.nginproj";
    WriteFile(projectPath,
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Typed" Type="Application" DefaultProfile="Runtime">
  <Inputs>
    <Headers Path="include">
      include/Typed/App.hpp
    </Headers>
    <Sources Path="src" />
    <Sources Path="src/linux" OperatingSystem="linux" Architecture="x64" />
    <Sources>
      <File Path="src/debug.cpp" BuildType="Debug" />
      <File Path="src/listed.cpp" OperatingSystem="linux" />
    </Sources>
  </Inputs>
  <Output Kind="Executable" Name="Typed" Target="Typed" />
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Profiles>
</Project>
)");

    const auto project = LoadProjectManifest(projectPath);
    REQUIRE(project.inputs.size() == 6);
    REQUIRE(project.inputs[0].visibility == "Public");
    REQUIRE(project.inputs[0].mode == "Directory");
    REQUIRE(project.inputs[0].role == "Header");
    REQUIRE(project.inputs[1].visibility == "Public");
    REQUIRE(project.inputs[1].mode == "File");
    REQUIRE(project.inputs[3].selectors.operatingSystem == "linux");
    REQUIRE(project.inputs[3].selectors.architecture == "x64");
    REQUIRE(project.inputs[4].selectors.buildType == "Debug");
    REQUIRE(project.inputs[5].path == "src/listed.cpp");
    REQUIRE(project.inputs[5].selectors.operatingSystem == "linux");

    const auto mixedPath = temp.path() / "Mixed.nginproj";
    WriteFile(mixedPath,
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Mixed" Type="Application" DefaultProfile="Runtime">
  <SourceRoots>
    <SourceRoot Path="src" />
  </SourceRoots>
  <Output Kind="Executable" Name="Mixed" Target="Mixed" />
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Profiles>
</Project>
)");
    REQUIRE_THROWS_WITH(LoadProjectManifest(mixedPath), ContainsSubstring("legacy <SourceRoots>"));
}

TEST_CASE("structured conditions normalize with direct selectors and inherited group selection")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Conditions.nginproj";
    WriteFile(projectPath,
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Conditions" Type="Application" DefaultProfile="Runtime">
  <Conditions>
    <Condition Name="DesktopHost">
      <Any>
        <Match OperatingSystem="linux" />
        <Match OperatingSystem="windows" />
      </Any>
    </Condition>
    <Condition Name="DebugBuild" BuildType="Debug" />
    <Condition Name="DesktopDebug">
      <All>
        <ConditionRef Name="DesktopHost" />
        <ConditionRef Name="DebugBuild" />
      </All>
    </Condition>
  </Conditions>
  <Inputs>
    <Sources Condition="DesktopHost">
      <Directory Path="src" BuildType="Debug" />
      <File Path="src/listed.cpp" Condition="DebugBuild" />
    </Sources>
  </Inputs>
  <Output Kind="Executable" Name="Conditions" Target="Conditions" />
  <Build Backend="CMake" Mode="Generated" Language="CXX" LanguageStandard="23">
    <CompileDefinitions Condition="DesktopHost">
      <Definition Value="CONDITIONS_DESKTOP" Visibility="Private" />
      <Definition Value="CONDITIONS_DESKTOP_DEBUG" Visibility="Private" BuildType="Debug" />
      <Definition Value="CONDITIONS_DESKTOP_RELEASE" Visibility="Private" BuildType="Release" />
      <Definition Value="CONDITIONS_DESKTOP_DEBUG_REF" Visibility="Private" Condition="DebugBuild" />
    </CompileDefinitions>
  </Build>
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Profiles>
</Project>
)");

    const auto project = LoadProjectManifest(projectPath);
    const std::optional<std::string> profileName{"Runtime"};
    const auto &profile = ProfileByName(project, profileName);
    REQUIRE(project.conditions.size() >= 3);
    REQUIRE(project.inputs.size() == 2);
    REQUIRE(project.inputs[0].selectors.conditionRefs.size() == 1);
    REQUIRE(project.inputs[0].selectors.buildType == "Debug");
    REQUIRE(SelectionMatches(project, project.inputs[0].selectors, profile));
    REQUIRE(SelectionMatches(project, project.inputs[1].selectors, profile));
    REQUIRE(SelectionMatches(project, project.build.compileDefinitions[0].selectors, profile));
    REQUIRE(SelectionMatches(project, project.build.compileDefinitions[1].selectors, profile));
    REQUIRE_FALSE(SelectionMatches(project, project.build.compileDefinitions[2].selectors, profile));
    REQUIRE(SelectionMatches(project, project.build.compileDefinitions[3].selectors, profile));

    const auto unknownPath = temp.path() / "Unknown.nginproj";
    WriteFile(unknownPath,
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Unknown" Type="Application" DefaultProfile="Runtime">
  <Inputs>
    <Sources Path="src" Condition="Missing" />
  </Inputs>
  <Output Kind="Executable" Name="Unknown" Target="Unknown" />
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Profiles>
</Project>
)");
    REQUIRE_THROWS_WITH(LoadProjectManifest(unknownPath), ContainsSubstring("unknown condition 'Missing'"));

    const auto cyclePath = temp.path() / "Cycle.nginproj";
    WriteFile(cyclePath,
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Cycle" Type="Application" DefaultProfile="Runtime">
  <Conditions>
    <Condition Name="A"><ConditionRef Name="B" /></Condition>
    <Condition Name="B"><ConditionRef Name="A" /></Condition>
  </Conditions>
  <Inputs>
    <Sources Path="src" />
  </Inputs>
  <Output Kind="Executable" Name="Cycle" Target="Cycle" />
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Profiles>
</Project>
)");
    REQUIRE_THROWS_WITH(LoadProjectManifest(cyclePath), ContainsSubstring("condition reference cycle"));
}

TEST_CASE("built-in conditions select references runtime entries and features")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "BuiltinSelection.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="BuiltinSelection" Template="Application" DefaultProfile="Runtime">
  <Output Kind="Executable" Name="BuiltinSelection" Target="BuiltinSelection" />
  <References>
    <Project Path="Missing.ReleaseOnly.nginproj" Condition="Release" />
    <Package Name="Missing.ReleaseOnly" Condition="Release" />
  </References>
  <Runtime>
    <EnableModules>
      <ModuleRef Name="Missing.ReleaseOnly" Condition="Release" />
    </EnableModules>
  </Runtime>
  <Environments>
    <Environment Name="development">
      <Features>
        <Feature Name="DebugFeature" Condition="Debug" />
        <Feature Name="ReleaseFeature" Condition="Release" />
      </Features>
    </Environment>
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" Platform="linux-x64" Environment="development" />
  </Profiles>
</Project>
)xml");

    const auto project = LoadProjectManifest(projectPath);
    const std::optional<std::string> profileName{"Runtime"};
    const auto &profile = ProfileByName(project, profileName);
    SelectorSet debugSelector{};
    debugSelector.conditionRefs.push_back("Debug");
    SelectorSet releaseSelector{};
    releaseSelector.conditionRefs.push_back("Release");
    REQUIRE(SelectionMatches(project, debugSelector, profile));
    REQUIRE_FALSE(SelectionMatches(project, releaseSelector, profile));

    const auto resolved = ResolveLaunch(project, profile);
    REQUIRE_FALSE(resolved.diagnostics.HasErrors());
    REQUIRE(resolved.value.has_value());
    REQUIRE(resolved.value->environmentFeatures.size() == 1);
    REQUIRE(resolved.value->environmentFeatures.front().name == "DebugFeature");
}

TEST_CASE("package local conditions select package inputs")
{
    TempDir temp{};
    const auto packagePath = temp.path() / "Package.Condition.nginpkg";
    WriteFile(packagePath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="3" Name="Package.Condition" Version="0.1.0">
  <Conditions>
    <Condition Name="LinuxOnly" OperatingSystem="linux" />
  </Conditions>
  <Inputs>
    <ToolInputs Condition="LinuxOnly">
      tools/schema.json
    </ToolInputs>
    <ToolInputs Condition="Windows">
      tools/windows.json
    </ToolInputs>
  </Inputs>
</Package>
)xml");

    const auto package = LoadPackageManifest(packagePath);
    ProfileDefinition profile{};
    profile.name = "Runtime";
    profile.buildType = "Debug";
    profile.platform = "linux-x64";
    profile.operatingSystem = "linux";
    profile.architecture = "x64";
    profile.environmentName = "development";

    REQUIRE(package.conditions.size() >= 1);
    REQUIRE(package.inputs.size() == 2);
    REQUIRE(SelectionMatches(package.conditions, package.inputs[0].selectors, profile));
    REQUIRE_FALSE(SelectionMatches(package.conditions, package.inputs[1].selectors, profile));
}

TEST_CASE("explain condition prints focused condition trace")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Explain.Condition.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="ExplainCondition" Template="Application" DefaultProfile="Runtime">
  <Output Kind="Executable" Name="ExplainCondition" Target="ExplainCondition" />
  <Environments>
    <Environment Name="development" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" Platform="linux-x64" Environment="development" />
  </Profiles>
</Project>
)xml");

    ParsedArgs args{};
    args.projectPath = projectPath.string();
    args.profileName = "Runtime";
    args.packageName = "Debug";

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdExplainCondition(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring("Condition: Debug"));
    REQUIRE_THAT(captured.str(), ContainsSubstring("result: matched"));
    REQUIRE_THAT(captured.str(), ContainsSubstring("origin: built-in"));
}

TEST_CASE("profiles inherit scalar settings and append authored contributions")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Profiles.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Profiles" Template="Application" DefaultProfile="Runtime">
  <Defaults BuildType="Debug" Platform="linux-x64" Environment="dev" />
  <Output Kind="Executable" Name="Profiles" Target="Profiles" />
  <Inputs>
    <Configs>
      config/project.cfg
    </Configs>
  </Inputs>
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Profiles>
    <Profile Name="Base">
      <Launch Executable="$(OutputName)" WorkingDirectory="." />
      <References>
        <Package Name="Profile.Base" />
      </References>
      <Inputs>
        <Configs>
          config/profile-base.cfg
        </Configs>
      </Inputs>
    </Profile>
    <Profile Name="Runtime" Extends="Base" BuildType="Release">
      <References>
        <Package Name="Profile.Runtime" />
      </References>
      <Inputs>
        <Configs>
          config/runtime.cfg
        </Configs>
      </Inputs>
    </Profile>
  </Profiles>
</Project>
)xml");

    const auto project = LoadProjectManifest(projectPath);
    const std::optional<std::string> profileName{"Runtime"};
    const auto &profile = ProfileByName(project, profileName);

    REQUIRE(project.profiles.size() == 2);
    REQUIRE(profile.name == "Runtime");
    REQUIRE(profile.buildType == "Release");
    REQUIRE(profile.platform == "linux-x64");
    REQUIRE(profile.operatingSystem == "linux");
    REQUIRE(profile.architecture == "x64");
    REQUIRE(profile.environmentName == "dev");
    REQUIRE(profile.launch.executable == "Profiles");
    REQUIRE(profile.inputs.size() == 2);
    REQUIRE(profile.inputs[0].kind == "Config");
    REQUIRE(profile.inputs[0].path == "config/profile-base.cfg");
    REQUIRE(profile.inputs[1].kind == "Config");
    REQUIRE(profile.inputs[1].path == "config/runtime.cfg");
    REQUIRE(profile.packageRefs.size() == 2);
    REQUIRE(profile.packageRefs[0].name == "Profile.Base");
    REQUIRE(profile.packageRefs[1].name == "Profile.Runtime");
}

TEST_CASE("model defaults and templates resolve through workspace and project includes")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="3" Name="Model.Workspace">
  <Includes>
    <Include Path="workspace.nginmodel" />
  </Includes>
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
</Workspace>
)xml");
    WriteFile(temp.path() / "workspace.nginmodel",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Model SchemaVersion="3" Name="Workspace.Model">
  <Defaults BuildType="Debug" Platform="linux-x64" Environment="workspace" />
  <ProfileTemplates>
    <ProfileTemplate Name="WorkspaceRuntime">
      <Launch Executable="$(OutputName)" WorkingDirectory="." />
      <References>
        <Package Name="Workspace.Template" />
      </References>
    </ProfileTemplate>
  </ProfileTemplates>
</Model>
)xml");
    WriteFile(temp.path() / "App/project.nginmodel",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Model SchemaVersion="3" Name="Project.Model">
  <Defaults Environment="project" />
  <ProjectTemplates>
    <ProjectTemplate Name="AuthoredTool" Type="Tool" OutputKind="Executable" />
  </ProjectTemplates>
  <ProfileTemplates>
    <ProfileTemplate Name="ProjectRuntime" Extends="WorkspaceRuntime">
      <Inputs>
        <Configs>
          config/template.cfg
        </Configs>
      </Inputs>
    </ProfileTemplate>
  </ProfileTemplates>
</Model>
)xml");
    WriteFile(temp.path() / "App/App.nginproj",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Model.App" Template="AuthoredTool" DefaultProfile="Runtime">
  <Includes>
    <Include Path="project.nginmodel" />
  </Includes>
  <Environments>
    <Environment Name="workspace" />
    <Environment Name="project" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime" Template="ProjectRuntime" />
  </Profiles>
</Project>
)xml");

    const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
    const std::optional<std::string> runtimeProfile{"Runtime"};
    const auto &profile = ProfileByName(project, runtimeProfile);

    REQUIRE(project.type == "Tool");
    REQUIRE(project.output.kind == "Executable");
    REQUIRE(project.output.name == "Model.App");
    REQUIRE(project.build.backend == "CMake");
    REQUIRE(project.build.mode == "Generated");
    REQUIRE(profile.buildType == "Debug");
    REQUIRE(profile.platform == "linux-x64");
    REQUIRE(profile.environmentName == "project");
    REQUIRE(profile.launch.executable == "Model.App");
    REQUIRE(profile.packageRefs.size() == 1);
    REQUIRE(profile.packageRefs[0].name == "Workspace.Template");
    REQUIRE(profile.inputs.size() == 1);
    REQUIRE(profile.inputs[0].kind == "Config");
    REQUIRE(profile.inputs[0].path == "config/template.cfg");
}

TEST_CASE("project local profile values override profile templates")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Override.App" Template="Application" DefaultProfile="Runtime">
  <Defaults BuildType="Debug" Platform="linux-x64" Environment="template-env" />
  <ProfileTemplates>
    <ProfileTemplate Name="RuntimeTemplate" BuildType="Release" Environment="template-env">
      <Launch Executable="$(OutputName)" WorkingDirectory="." />
    </ProfileTemplate>
  </ProfileTemplates>
  <Environments>
    <Environment Name="template-env" />
    <Environment Name="local-env" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime"
             Template="RuntimeTemplate"
             BuildType="Debug"
             Environment="local-env" />
  </Profiles>
</Project>
)xml");

    const auto project = LoadProjectManifest(projectPath);
    const std::optional<std::string> runtimeProfile{"Runtime"};
    const auto &profile = ProfileByName(project, runtimeProfile);

    REQUIRE(profile.buildType == "Debug");
    REQUIRE(profile.environmentName == "local-env");
    REQUIRE(profile.launch.executable == "Override.App");
}

TEST_CASE("model include diagnostics report missing files and cycles")
{
    TempDir temp{};
    const auto missingPath = temp.path() / "Missing.nginproj";
    WriteFile(missingPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Missing.Model" Template="Application" DefaultProfile="Runtime">
  <Includes>
    <Include Path="missing.nginmodel" />
  </Includes>
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime" Environment="dev" />
  </Profiles>
</Project>
)xml");
    REQUIRE_THROWS_WITH(LoadProjectManifest(missingPath), ContainsSubstring("included model file does not exist"));

    WriteFile(temp.path() / "a.nginmodel",
              R"xml(<Model SchemaVersion="3" Name="A"><Includes><Include Path="b.nginmodel" /></Includes></Model>)xml");
    WriteFile(temp.path() / "b.nginmodel",
              R"xml(<Model SchemaVersion="3" Name="B"><Includes><Include Path="a.nginmodel" /></Includes></Model>)xml");
    const auto cyclePath = temp.path() / "Cycle.nginproj";
    WriteFile(cyclePath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Cycle.Model" Template="Application" DefaultProfile="Runtime">
  <Includes>
    <Include Path="a.nginmodel" />
  </Includes>
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime" Environment="dev" />
  </Profiles>
</Project>
)xml");
    REQUIRE_THROWS_WITH(LoadProjectManifest(cyclePath), ContainsSubstring("model include cycle"));
}

TEST_CASE("unknown project and profile templates fail validation")
{
    TempDir temp{};
    const auto projectTemplatePath = temp.path() / "UnknownProjectTemplate.nginproj";
    WriteFile(projectTemplatePath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Unknown.Project.Template" Template="Missing" DefaultProfile="Runtime">
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime" Environment="dev" />
  </Profiles>
</Project>
)xml");
    REQUIRE_THROWS_WITH(LoadProjectManifest(projectTemplatePath), ContainsSubstring("unknown project template"));

    const auto profileTemplatePath = temp.path() / "UnknownProfileTemplate.nginproj";
    WriteFile(profileTemplatePath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Unknown.Profile.Template" Template="Application" DefaultProfile="Runtime">
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime" Template="Missing" Environment="dev" />
  </Profiles>
</Project>
)xml");
    REQUIRE_THROWS_WITH(LoadProjectManifest(profileTemplatePath), ContainsSubstring("unknown profile template"));
}

TEST_CASE("generated CMake applies typed source selectors and selected build settings")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Library/Typed.Library.nginproj";
    WriteFile(projectPath,
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Typed.Library" Type="Library" DefaultProfile="Runtime">
  <Conditions>
    <Condition Name="LinuxDesktop" OperatingSystem="linux" />
  </Conditions>
  <Inputs>
    <Headers Path="include">
      include/Typed/Library.hpp
    </Headers>
    <Sources Path="src" Include="**/*.cpp" Exclude="**/*.generated.cpp" />
    <Sources Path="src/linux" OperatingSystem="linux" />
    <Sources Path="src/windows" OperatingSystem="windows" />
    <Sources Path="src/conditional-enabled" Condition="LinuxDesktop" BuildType="Debug" />
    <Sources Path="src/conditional-disabled" Condition="LinuxDesktop" BuildType="Release" />
    <Sources>
      manual/manual.cpp
    </Sources>
  </Inputs>
  <Output Kind="StaticLibrary" Name="Typed.Library" Target="TypedLibrary" />
  <Build Backend="CMake" Mode="Generated" Language="CXX" LanguageStandard="23">
    <CompileDefinitions>
      <Definition Value="TYPED_LIBRARY_BUILD" Visibility="Private" />
      <Definition Value="TYPED_LIBRARY_LINUX" Visibility="Private" OperatingSystem="linux" />
      <Definition Value="TYPED_LIBRARY_WINDOWS" Visibility="Private" OperatingSystem="windows" />
      <Definition Value="TYPED_LIBRARY_DEBUG" Visibility="Private" BuildType="Debug" />
      <Definition Value="TYPED_LIBRARY_WHEN" Visibility="Private" Condition="LinuxDesktop" />
      <Definition Value="TYPED_LIBRARY_WHEN_DEBUG" Visibility="Private" Condition="LinuxDesktop" BuildType="Debug" />
      <Definition Value="TYPED_LIBRARY_WHEN_RELEASE" Visibility="Private" Condition="LinuxDesktop" BuildType="Release" />
    </CompileDefinitions>
    <CompileOptions Condition="LinuxDesktop">
      <Option Value="-DTYPED_GROUP_OPTION" Visibility="Private" BuildType="Debug" />
      <Option Value="-DTYPED_GROUP_RELEASE_OPTION" Visibility="Private" BuildType="Release" />
    </CompileOptions>
  </Build>
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Profiles>
</Project>
)");
    WriteFile(temp.path() / "Library/include/Typed/Library.hpp", "#pragma once\nvoid typed_library();\n");
    WriteFile(temp.path() / "Library/src/core.cpp", "#include <Typed/Library.hpp>\nvoid typed_library() {}\n");
    WriteFile(temp.path() / "Library/src/ignored.generated.cpp", "void typed_library_generated() {}\n");
    WriteFile(temp.path() / "Library/src/linux/linux.cpp", "void typed_library_linux() {}\n");
    WriteFile(temp.path() / "Library/src/windows/windows.cpp", "void typed_library_windows() {}\n");
    WriteFile(temp.path() / "Library/src/conditional-enabled/enabled.cpp", "void typed_library_conditional_enabled() {}\n");
    WriteFile(temp.path() / "Library/src/conditional-disabled/disabled.cpp", "void typed_library_conditional_disabled() {}\n");
    WriteFile(temp.path() / "Library/manual/manual.cpp", "void typed_library_manual() {}\n");

    const auto project = LoadProjectManifest(projectPath);
    const std::optional<std::string> profileName{"Runtime"};
    const auto &profile = ProfileByName(project, profileName);
    const auto configured = ConfigureLaunch(project, profile, temp.path() / "stage");
    REQUIRE(configured.value.has_value());
    REQUIRE_FALSE(configured.diagnostics.HasErrors());

    const auto generated = ReadFile(temp.path() / "stage/.ngin/cmake-src/CMakeLists.txt");
    REQUIRE_THAT(generated, ContainsSubstring("target_include_directories(\"TypedLibrary\" PUBLIC \"" + (temp.path() / "Library/include").generic_string() + "\")"));
    REQUIRE_THAT(generated, ContainsSubstring("target_include_directories(\"TypedLibrary\" PRIVATE \"" + (temp.path() / "Library/src").generic_string() + "\")"));
    REQUIRE_THAT(generated, ContainsSubstring("target_include_directories(\"TypedLibrary\" PRIVATE \"" + (temp.path() / "Library/src/linux").generic_string() + "\")"));
    REQUIRE_THAT(generated, !ContainsSubstring((temp.path() / "Library/src/windows").generic_string()));
    REQUIRE_THAT(generated, ContainsSubstring("TYPED_LIBRARY_LINUX"));
    REQUIRE_THAT(generated, ContainsSubstring("TYPED_LIBRARY_DEBUG"));
    REQUIRE_THAT(generated, ContainsSubstring("TYPED_LIBRARY_WHEN"));
    REQUIRE_THAT(generated, ContainsSubstring("TYPED_LIBRARY_WHEN_DEBUG"));
    REQUIRE_THAT(generated, !ContainsSubstring("TYPED_LIBRARY_WINDOWS"));
    REQUIRE_THAT(generated, !ContainsSubstring("TYPED_LIBRARY_WHEN_RELEASE"));
    REQUIRE_THAT(generated, ContainsSubstring("-DTYPED_GROUP_OPTION"));
    REQUIRE_THAT(generated, !ContainsSubstring("-DTYPED_GROUP_RELEASE_OPTION"));
    REQUIRE(CountOccurrences(generated, (temp.path() / "Library/src/core.cpp").generic_string()) == 1);
    REQUIRE(CountOccurrences(generated, (temp.path() / "Library/src/linux/linux.cpp").generic_string()) == 1);
    REQUIRE(CountOccurrences(generated, (temp.path() / "Library/src/windows/windows.cpp").generic_string()) == 0);
    REQUIRE(CountOccurrences(generated, (temp.path() / "Library/src/conditional-enabled/enabled.cpp").generic_string()) == 1);
    REQUIRE(CountOccurrences(generated, (temp.path() / "Library/src/conditional-disabled/disabled.cpp").generic_string()) == 0);
    REQUIRE(CountOccurrences(generated, (temp.path() / "Library/src/ignored.generated.cpp").generic_string()) == 0);
    REQUIRE(CountOccurrences(generated, (temp.path() / "Library/manual/manual.cpp").generic_string()) == 1);
}

TEST_CASE("environment variables resolve from explicit value, process environment, and local settings")
{
    TempDir temp{};
    ScopedEnvironmentVariable home("HOME", (temp.path() / "Home").string());
    ScopedEnvironmentVariable token("NGIN_TEST_ENV_TOKEN", "from-process");

    WriteFile(temp.path() / "Home/.ngin/settings.nginsettings",
              R"(<?xml version="1.0" encoding="utf-8"?>
<LocalSettings SchemaVersion="1">
  <Settings>
    <Setting Key="sdk.vulkan.root" Value="/global/vulkan" />
  </Settings>
</LocalSettings>
)");
    WriteFile(temp.path() / "App/.ngin/local/user.nginsettings",
              R"(<?xml version="1.0" encoding="utf-8"?>
<LocalSettings SchemaVersion="1">
  <Settings>
    <Setting Key="feeds.private.token" Value="local-secret" Secret="true" />
    <Setting Key="sdk.vulkan.root" Value="/local/vulkan" />
  </Settings>
</LocalSettings>
)");
    WriteFile(temp.path() / "App/App.nginproj",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Settings.App" Type="Application" DefaultProfile="Runtime">
  <Output Kind="Executable" Name="Settings.App" Target="SettingsApp" />
  <LocalSettings>
    <Import Path=".ngin/local/user.nginsettings" Optional="false" />
  </LocalSettings>
  <Environments>
    <Environment Name="dev">
      <Variables>
        <Variable Name="API_ENDPOINT" Value="https://dev.example.com" />
        <Variable Name="API_TOKEN" FromLocalSetting="feeds.private.token" Required="true" Secret="true" />
        <Variable Name="VULKAN_SDK" FromLocalSetting="sdk.vulkan.root" Required="true" />
        <Variable Name="ENV_TOKEN" FromEnvironment="NGIN_TEST_ENV_TOKEN" Required="true" />
      </Variables>
    </Environment>
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Profiles>
</Project>
)");

    const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
    const std::optional<std::string> profileName{"Runtime"};
    const auto &profile = ProfileByName(project, profileName);
    const auto resolved = ResolveLaunch(project, profile);

    REQUIRE(resolved.value.has_value());
    REQUIRE_FALSE(resolved.diagnostics.HasErrors());
    REQUIRE(resolved.value->environmentVariables.size() == 4);
    REQUIRE(resolved.value->environmentVariables[0].value == "https://dev.example.com");
    REQUIRE(resolved.value->environmentVariables[1].value == "local-secret");
    REQUIRE(resolved.value->environmentVariables[1].secret);
    REQUIRE(resolved.value->environmentVariables[2].value == "/local/vulkan");
    REQUIRE(resolved.value->environmentVariables[3].value == "from-process");
}

TEST_CASE("environment variables resolve from user-global settings when repo-local values are absent")
{
    TempDir temp{};
    ScopedEnvironmentVariable home("HOME", (temp.path() / "Home").string());
    WriteFile(temp.path() / "Home/.ngin/settings.nginsettings",
              R"(<?xml version="1.0" encoding="utf-8"?>
<LocalSettings SchemaVersion="1">
  <Settings>
    <Setting Key="sdk.vulkan.root" Value="/global/vulkan" />
  </Settings>
</LocalSettings>
)");
    WriteFile(temp.path() / "App/App.nginproj",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Global.Settings.App" Type="Application" DefaultProfile="Runtime">
  <Output Kind="Executable" Name="Global.Settings.App" Target="GlobalSettingsApp" />
  <Environments>
    <Environment Name="dev">
      <Variables>
        <Variable Name="VULKAN_SDK" FromLocalSetting="sdk.vulkan.root" Required="true" />
      </Variables>
    </Environment>
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Profiles>
</Project>
)");

    const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
    const std::optional<std::string> profileName{"Runtime"};
    const auto &profile = ProfileByName(project, profileName);
    const auto resolved = ResolveLaunch(project, profile);

    REQUIRE(resolved.value.has_value());
    REQUIRE_FALSE(resolved.diagnostics.HasErrors());
    REQUIRE(resolved.value->environmentVariables.front().value == "/global/vulkan");
    REQUIRE_THAT(resolved.value->environmentVariables.front().resolvedSource, ContainsSubstring("user-global"));
}

TEST_CASE("missing required variable sources are reported without values")
{
    TempDir temp{};
    ScopedEnvironmentVariable home("HOME", (temp.path() / "Home").string());
    ScopedUnsetEnvironmentVariable missingToken("NGIN_TEST_MISSING_TOKEN");
    WriteFile(temp.path() / "App/App.nginproj",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Missing.Settings.App" Type="Application" DefaultProfile="Runtime">
  <Output Kind="Executable" Name="Missing.Settings.App" Target="MissingSettingsApp" />
  <Environments>
    <Environment Name="dev">
      <Variables>
        <Variable Name="API_TOKEN" FromEnvironment="NGIN_TEST_MISSING_TOKEN" Required="true" Secret="true" />
      </Variables>
    </Environment>
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Profiles>
</Project>
)");

    const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
    const std::optional<std::string> profileName{"Runtime"};
    const auto &profile = ProfileByName(project, profileName);
    const auto resolved = ResolveLaunch(project, profile);

    REQUIRE_FALSE(resolved.value.has_value());
    REQUIRE(ContainsDiagnostic(DiagnosticMessages(resolved.diagnostics), "missing required secret variable 'API_TOKEN'"));
}

TEST_CASE("variable declarations reject conflicting sources and literal secrets")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Invalid.nginproj";
    WriteFile(projectPath,
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Invalid.Settings.App" Type="Application" DefaultProfile="Runtime">
  <Output Kind="Executable" Name="Invalid.Settings.App" Target="InvalidSettingsApp" />
  <Environments>
    <Environment Name="dev">
      <Variables>
        <Variable Name="BAD" Value="literal" FromEnvironment="BAD" />
      </Variables>
    </Environment>
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Profiles>
</Project>
)");
    REQUIRE_THROWS_WITH(LoadProjectManifest(projectPath), ContainsSubstring("must declare exactly one"));

    WriteFile(projectPath,
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Invalid.Settings.App" Type="Application" DefaultProfile="Runtime">
  <Output Kind="Executable" Name="Invalid.Settings.App" Target="InvalidSettingsApp" />
  <Environments>
    <Environment Name="dev">
      <Variables>
        <Variable Name="BAD_SECRET" Value="literal" Secret="true" />
      </Variables>
    </Environment>
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Profiles>
</Project>
)");
    REQUIRE_THROWS_WITH(LoadProjectManifest(projectPath), ContainsSubstring("may not combine Secret"));
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

TEST_CASE("project parsing infers output metadata for vnext manifests")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Invalid.nginproj";
    WriteFile(projectPath,
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Invalid" Type="Application" DefaultProfile="Runtime">
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Profiles>
</Project>
)");

    const auto project = LoadProjectManifest(projectPath);
    REQUIRE(project.output.kind == "Executable");
    REQUIRE(project.output.name == "Invalid");
    REQUIRE(project.output.target == "Invalid");
}

TEST_CASE("project autodiscovery resolves nearest nginproj in the current tree")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Nested/App.nginproj";
    WriteFile(projectPath,
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Nested" Type="Application" DefaultProfile="Runtime">
  <Output Kind="Executable" Name="Nested" Target="NestedTarget" />
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Profiles>
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
    WriteFile(temp.path() / "Workspace.ngin",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="3" Name="CycleWorkspace">
  <PackageSources>
    <PackageSource Path="Packages" />
  </PackageSources>
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
</Workspace>
)");
    WriteFile(temp.path() / "Packages/A/A.nginpkg",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="3" Name="Package.A" Version="1.0.0">
  <Dependencies>
    <PackageRef Name="Package.B" VersionRange=">=1.0.0 &lt;2.0.0" />
  </Dependencies>
  <Build Backend="CMake" Mode="Manual" />
  <Modules>
    <Module Name="Module.A" Family="Core" Type="Runtime" StartupStage="Features" />
  </Modules>
</Package>
)");
    WriteFile(temp.path() / "Packages/B/B.nginpkg",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="3" Name="Package.B" Version="1.0.0">
  <Dependencies>
    <PackageRef Name="Package.A" VersionRange=">=1.0.0 &lt;2.0.0" />
  </Dependencies>
  <Build Backend="CMake" Mode="Manual" />
  <Modules>
    <Module Name="Module.B" Family="Core" Type="Runtime" StartupStage="Features" />
  </Modules>
</Package>
)");
    WriteFile(temp.path() / "App/App.nginproj",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Cycle.App" Type="Application" DefaultProfile="Runtime">
  <Output Kind="Executable" Name="Cycle.App" Target="CycleApp" />
  <References>
    <Package Name="Package.A" VersionRange=">=1.0.0 &lt;2.0.0" />
  </References>
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Profiles>
</Project>
)");

    const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
    const std::optional<std::string> profileName{"Runtime"};
    const auto &profile = ProfileByName(project, profileName);
    const auto resolved = ResolveLaunch(project, profile);

    REQUIRE_FALSE(resolved.value.has_value());
    REQUIRE_THAT(resolved.diagnostics.entries.front().message, ContainsSubstring("dependency cycle"));
}

TEST_CASE("package features select dependencies inputs variables and capabilities")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="3" Name="FeatureWorkspace">
  <PackageSources>
    <PackageSource Path="Packages" />
  </PackageSources>
  <DependencyPolicy VersionResolution="HighestCompatible">
    <Versions>
      <Package Name="Package.Core" VersionRange=">=1.0.0 &lt;2.0.0" />
    </Versions>
  </DependencyPolicy>
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
</Workspace>
)");
    WriteFile(temp.path() / "Packages/Core/Core.nginpkg",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="3" Name="Package.Core" Version="1.0.0">
  <Features>
    <Feature Name="Diagnostics">
      <Provides>
        <Capability Name="Diagnostics" />
      </Provides>
      <Dependencies>
        <PackageRef Name="Package.Tools" VersionRange=">=1.0.0 &lt;2.0.0" />
      </Dependencies>
      <Inputs>
        <Configs>
          config/diagnostics.cfg
        </Configs>
      </Inputs>
      <Variables>
        <Variable Name="DIAGNOSTICS_ENABLED" Value="1" />
      </Variables>
    </Feature>
  </Features>
</Package>
)");
    WriteFile(temp.path() / "Packages/Core/config/diagnostics.cfg", "enabled=true\n");
    WriteFile(temp.path() / "Packages/Tools/Tools.nginpkg",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="3" Name="Package.Tools" Version="1.0.0" />
)");
    WriteFile(temp.path() / "App/App.nginproj",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Feature.App" Type="Application" DefaultProfile="Runtime">
  <Output Kind="Executable" Name="Feature.App" Target="FeatureApp" />
  <Features>
    <Use Package="Package.Core" Feature="Diagnostics" />
  </Features>
  <Environments>
    <Environment Name="local" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" OperatingSystem="linux" Architecture="x64" Environment="local" />
  </Profiles>
</Project>
)");

    const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
    const std::optional<std::string> profileName{"Runtime"};
    const auto &profile = ProfileByName(project, profileName);
    const auto resolved = ResolveLaunch(project, profile);

    REQUIRE(resolved.value.has_value());
    REQUIRE_FALSE(resolved.diagnostics.HasErrors());
    REQUIRE(resolved.value->orderedPackages.size() == 2);
    REQUIRE(resolved.value->selectedPackageFeatures.size() == 1);
    REQUIRE(resolved.value->selectedPackageFeatures.front().featureName == "Diagnostics");
    REQUIRE(resolved.value->capabilityProviders.size() == 1);
    REQUIRE(resolved.value->capabilityProviders.front().capability == "Diagnostics");
    REQUIRE(std::any_of(resolved.value->inputs.begin(), resolved.value->inputs.end(), [](const ResolvedInput &input)
    {
        return input.ownerKind == "package-feature" && input.kind == "Config" && input.source == "config/diagnostics.cfg";
    }));
    REQUIRE(std::any_of(resolved.value->environmentVariables.begin(), resolved.value->environmentVariables.end(), [](const EnvironmentVariable &variable)
    {
        return variable.name == "DIAGNOSTICS_ENABLED" && variable.value == "1";
    }));
}

TEST_CASE("package feature uses require explicit version range or dependency policy")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="3" Name="MissingPolicyWorkspace">
  <PackageSources>
    <PackageSource Path="Packages" />
  </PackageSources>
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
</Workspace>
)");
    WriteFile(temp.path() / "Packages/Core/Core.nginpkg",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="3" Name="Package.Core" Version="1.0.0">
  <Features>
    <Feature Name="Diagnostics" />
  </Features>
</Package>
)");
    WriteFile(temp.path() / "App/App.nginproj",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="MissingPolicy.App" Type="Application" DefaultProfile="Runtime">
  <Output Kind="Executable" Name="MissingPolicy.App" Target="MissingPolicyApp" />
  <Features>
    <Use Package="Package.Core" Feature="Diagnostics" />
  </Features>
  <Environments>
    <Environment Name="local" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" OperatingSystem="linux" Architecture="x64" Environment="local" />
  </Profiles>
</Project>
)");

    const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
    const std::optional<std::string> profileName{"Runtime"};
    const auto &profile = ProfileByName(project, profileName);
    const auto resolved = ResolveLaunch(project, profile);

    REQUIRE_FALSE(resolved.value.has_value());
    REQUIRE(ContainsDiagnostic(DiagnosticMessages(resolved.diagnostics),
                               "must declare VersionRange or be covered by DependencyPolicy"));
}

TEST_CASE("package lock command writes and verifies deterministic local lock")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="3" Name="LockWorkspace">
  <PackageSources>
    <PackageSource Path="Packages" />
  </PackageSources>
  <DependencyPolicy VersionResolution="HighestCompatible">
    <Versions>
      <Package Name="Package.Core" VersionRange=">=1.0.0 &lt;2.0.0" />
    </Versions>
  </DependencyPolicy>
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
</Workspace>
)");
    WriteFile(temp.path() / "Packages/Core/Core.nginpkg",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="3" Name="Package.Core" Version="1.0.0">
  <Features>
    <Feature Name="Diagnostics">
      <Provides>
        <Capability Name="Diagnostics" />
      </Provides>
    </Feature>
  </Features>
</Package>
)");
    WriteFile(temp.path() / "App/App.nginproj",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Lock.App" Type="Application" DefaultProfile="Runtime">
  <Output Kind="Executable" Name="Lock.App" Target="LockApp" />
  <Features>
    <Use Package="Package.Core" Feature="Diagnostics" />
  </Features>
  <Environments>
    <Environment Name="local" />
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" OperatingSystem="linux" Architecture="x64" Environment="local" />
  </Profiles>
</Project>
)");

    const auto lockPath = temp.path() / "lock/ngin.lock";
    ParsedArgs lockArgs{};
    lockArgs.projectPath = (temp.path() / "App/App.nginproj").string();
    lockArgs.profileName = "Runtime";
    lockArgs.outputPath = lockPath.string();
    REQUIRE(CmdPackageLock(temp.path(), lockArgs) == 0);
    REQUIRE(fs::exists(lockPath));
    REQUIRE_THAT(ReadFile(lockPath), ContainsSubstring(R"(<Feature Package="Package.Core" Name="Diagnostics")"));

    ParsedArgs verifyArgs{};
    verifyArgs.projectPath = lockArgs.projectPath;
    verifyArgs.profileName = lockArgs.profileName;
    verifyArgs.lockPath = lockPath.string();
    REQUIRE(CmdPackageVerifyLock(temp.path(), verifyArgs) == 0);
}

TEST_CASE("inspect command emits resolved read-only json")
{
    TempDir temp{};
    ScopedEnvironmentVariable token{"NGIN_TEST_INSPECT_TOKEN", "inspect-secret"};
    WriteFile(temp.path() / "Workspace.ngin",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="3" Name="InspectWorkspace">
  <PackageSources>
    <PackageSource Path="Packages" />
  </PackageSources>
  <DependencyPolicy VersionResolution="HighestCompatible">
    <Versions>
      <Package Name="Package.Core" VersionRange=">=1.0.0 &lt;2.0.0" />
    </Versions>
  </DependencyPolicy>
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
</Workspace>
)");
    WriteFile(temp.path() / "Packages/Core/Core.nginpkg",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="3" Name="Package.Core" Version="1.0.0">
  <Features>
    <Feature Name="Diagnostics" Description="Diagnostic hooks">
      <Provides>
        <Capability Name="Diagnostics" />
      </Provides>
      <Generators>
        <Generator Name="PackageGenerator" Kind="Command">
          <Tool Executable="tools/gen" />
          <Outputs>
            <Generated Role="Source" Path="$(GeneratedDir)/package.generated.cpp" />
          </Outputs>
        </Generator>
      </Generators>
    </Feature>
  </Features>
</Package>
)");
    WriteFile(temp.path() / "App/App.nginproj",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="3" Name="Inspect.App" Type="Application" DefaultProfile="Runtime">
  <Inputs>
    <Sources Path="src" />
    <Configs>
      config/runtime.cfg
    </Configs>
  </Inputs>
  <Output Kind="Executable" Name="Inspect.App" Target="InspectApp" />
  <Features>
    <Use Package="Package.Core" Feature="Diagnostics" />
  </Features>
  <Generators>
    <Generator Name="WindowsOnlyGenerator" Kind="Command" Platform="windows-x64">
      <Tool Executable="tools/gen" />
      <Outputs>
        <Generated Role="Source" Path="$(GeneratedDir)/windows.generated.cpp" />
      </Outputs>
    </Generator>
  </Generators>
  <Environments>
    <Environment Name="local">
      <Variables>
        <Variable Name="INSPECT_TOKEN" FromEnvironment="NGIN_TEST_INSPECT_TOKEN" Secret="true" />
      </Variables>
    </Environment>
  </Environments>
  <Profiles>
    <Profile Name="Runtime" BuildType="Debug" Platform="linux-x64" OperatingSystem="linux" Architecture="x64" Environment="local" />
  </Profiles>
</Project>
)");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");
    WriteFile(temp.path() / "App/config/runtime.cfg", "name=inspect\n");

    const auto outputDir = temp.path() / "inspect-output";
    ParsedArgs args{};
    args.projectPath = (temp.path() / "App/App.nginproj").string();
    args.profileName = "Runtime";
    args.outputPath = outputDir.string();
    args.format = "json";

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdInspect(temp.path(), args);
    std::cout.rdbuf(previous);

    const auto json = captured.str();
    REQUIRE(exitCode == 0);
    REQUIRE_THAT(json, ContainsSubstring(R"("schemaVersion": 1)"));
    REQUIRE_THAT(json, ContainsSubstring(R"("name":"Package.Core")"));
    REQUIRE_THAT(json, ContainsSubstring(R"("feature":"Diagnostics")"));
    REQUIRE_THAT(json, ContainsSubstring(R"("state":"selected")"));
    REQUIRE_THAT(json, ContainsSubstring(R"("name":"PackageGenerator")"));
    REQUIRE_THAT(json, ContainsSubstring(R"("state":"active")"));
    REQUIRE_THAT(json, ContainsSubstring(R"("name":"WindowsOnlyGenerator")"));
    REQUIRE_THAT(json, ContainsSubstring(R"("state":"excluded")"));
    REQUIRE_THAT(json, ContainsSubstring(R"("Config")"));
    REQUIRE_THAT(json, ContainsSubstring(R"("value":"<redacted>")"));
    REQUIRE_FALSE(fs::exists(outputDir));

    args.format = "xml";
    REQUIRE_THROWS_WITH(CmdInspect(temp.path(), args), ContainsSubstring("inspect supports only --format json"));
}

TEST_CASE("resolution reports project config input collisions")
{
    const auto projectPath = RepoRoot() / "Tools/NGIN.CLI/tests/fixtures/ProjectRef.Config/CollisionRoot/"
                                          "ProjectRef.Config.CollisionRoot.nginproj";
    REQUIRE(fs::exists(projectPath));

    const auto project = LoadProjectManifest(projectPath);
    const std::optional<std::string> profileName{"Runtime"};
    const auto &profile = ProfileByName(project, profileName);
    const auto resolved = ResolveLaunch(project, profile);

    REQUIRE_FALSE(resolved.value.has_value());

    bool foundCollision = false;
    for (const auto &message : DiagnosticMessages(resolved.diagnostics))
    {
        if (message.find("input destination collision") != std::string::npos)
        {
            foundCollision = true;
            break;
        }
    }
    REQUIRE(foundCollision);
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
