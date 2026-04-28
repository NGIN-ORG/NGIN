#include "Authoring.hpp"
#include "Build.hpp"
#include "Diagnostics.hpp"
#include "MetaGen.hpp"
#include "MetaGenCommon.hpp"
#include "Resolution.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

    auto WriteMetaGenFixture(TempDir &temp, const std::string &header) -> fs::path
    {
        const auto projectPath = temp.path() / "App/App.nginproj";
        WriteFile(projectPath,
                  R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="2" Name="Meta.Property.App" Type="Application" DefaultConfiguration="Runtime">
  <SourceRoots>
    <SourceRoot Path="src" />
  </SourceRoots>
  <Output Kind="Executable" Name="Meta.Property.App" Target="MetaPropertyApp" />
  <Build Backend="CMake" Mode="Generated" Language="CXX" LanguageStandard="23">
    <MetaGen Enabled="true" />
  </Build>
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Configurations>
    <Configuration Name="Runtime" BuildConfiguration="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Configurations>
</Project>
)");
        WriteFile(temp.path() / "App/src/Player.hpp", header);
        WriteFile(temp.path() / "App/src/main.cpp", R"(#include "Player.hpp"
int main() { return 0; }
)");
        return projectPath;
    }
} // namespace

TEST_CASE("workspace, project, and package manifests parse through authoring "
          "facades")
{
    TempDir temp{};
    WriteFile(temp.path() / "Workspace.ngin",
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
    WriteFile(temp.path() / "Packages/Sample/Sample.nginpkg",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="2" Name="Sample.Package" Version="1.0.0">
  <Build Backend="CMake" Mode="Manual" />
</Package>
)");
    WriteFile(temp.path() / "App/App.nginproj",
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
    REQUIRE(package.modules.empty());
    REQUIRE(catalog.contains("Sample.Package"));
}

TEST_CASE("metagen annotation parser accepts macro payloads")
{
    const auto reflect =
        NGIN::CLI::MetaGen::ParseAnnotation("ngin.reflect:name = \"Demo::Player\", category = runtime");
    REQUIRE(reflect.kind == "reflect");
    REQUIRE(reflect.options.at("name") == "Demo::Player");
    REQUIRE(reflect.options.at("category") == "runtime");

    const auto ignore = NGIN::CLI::MetaGen::ParseAnnotation("ngin.ignore");
    REQUIRE(ignore.kind == "ignore");
    REQUIRE(ignore.options.empty());
}

TEST_CASE("metagen emits method-backed properties")
{
    TempDir temp{};
    const auto projectPath = WriteMetaGenFixture(temp,
                                                 R"(#pragma once
#include <NGIN/MetaGen/Annotations.hpp>
namespace Demo {
struct NGIN_REFLECT(name = "Demo::Player") Player {
    int score{7};
    int readOnly{3};
    NGIN_PROPERTY(name = "score")
    int GetScore() const { return score; }
    NGIN_PROPERTY(name = "score")
    void SetScore(int value) { score = value; }
    NGIN_PROPERTY(name = "read_only")
    int GetReadOnly() const { return readOnly; }
};
}
)");

    const auto project = LoadProjectManifest(projectPath);
    const auto result =
        GenerateMetaData(RepoRoot(), project, project.configurations.front(), temp.path() / "generated");
    if (!result.available)
    {
        SKIP("MetaGen was built without Clang support");
    }
    REQUIRE(result.diagnostics.empty());
    REQUIRE(result.generatedFiles.size() == 1);

    const auto generated = ReadFile(result.generatedFiles.front());
    REQUIRE_THAT(generated,
                 ContainsSubstring(R"(type.Property<&Demo::Player::GetScore, &Demo::Player::SetScore>("score");)"));
    REQUIRE_THAT(generated, ContainsSubstring(R"(type.Property<&Demo::Player::GetReadOnly>("read_only");)"));
}

TEST_CASE("metagen rejects invalid property method signatures")
{
    TempDir temp{};
    const auto projectPath = WriteMetaGenFixture(temp,
                                                 R"(#pragma once
#include <NGIN/MetaGen/Annotations.hpp>
namespace Demo {
struct NGIN_REFLECT(name = "Demo::Player") Player {
    NGIN_PROPERTY(name = "score")
    int SetScore(int value) { return value; }
};
}
)");

    const auto project = LoadProjectManifest(projectPath);
    const auto result =
        GenerateMetaData(RepoRoot(), project, project.configurations.front(), temp.path() / "generated");
    if (!result.available)
    {
        SKIP("MetaGen was built without Clang support");
    }
    REQUIRE(ContainsDiagnostic(result.diagnostics, "must be a getter with no parameters and a non-void return"));
}

TEST_CASE("metagen rejects setter-only properties")
{
    TempDir temp{};
    const auto projectPath = WriteMetaGenFixture(temp,
                                                 R"(#pragma once
#include <NGIN/MetaGen/Annotations.hpp>
namespace Demo {
struct NGIN_REFLECT(name = "Demo::Player") Player {
    NGIN_PROPERTY(name = "score")
    void SetScore(int value) { (void)value; }
};
}
)");

    const auto project = LoadProjectManifest(projectPath);
    const auto result =
        GenerateMetaData(RepoRoot(), project, project.configurations.front(), temp.path() / "generated");
    if (!result.available)
    {
        SKIP("MetaGen was built without Clang support");
    }
    REQUIRE(ContainsDiagnostic(result.diagnostics, "has a setter but no getter"));
}

TEST_CASE("project build descriptor parses metagen opt in")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "App.nginproj";
    WriteFile(projectPath,
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="2" Name="Meta.App" Type="Application" DefaultConfiguration="Runtime">
  <SourceRoots>
    <SourceRoot Path="src" />
  </SourceRoots>
  <Output Kind="Executable" Name="Meta.App" Target="MetaApp" />
  <Build Backend="CMake" Mode="Generated" Language="CXX" LanguageStandard="23">
    <MetaGen Enabled="true" />
  </Build>
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Configurations>
    <Configuration Name="Runtime" BuildConfiguration="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Configurations>
</Project>
)");

    const auto project = LoadProjectManifest(projectPath);
    REQUIRE(project.build.metaGenEnabled);
}

TEST_CASE("typed project sources parse selector metadata and reject mixed legacy roots")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Typed.nginproj";
    WriteFile(projectPath,
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="2" Name="Typed" Type="Application" DefaultConfiguration="Runtime">
  <Sources>
    <Public>
      <Root Path="include" />
      <File Path="include/Typed/App.hpp" />
    </Public>
    <Private>
      <Root Path="src" />
      <Root Path="src/linux" OperatingSystem="linux" Architecture="x64" />
      <File Path="src/debug.cpp" BuildConfiguration="Debug" />
      <Files OperatingSystem="linux">
        src/listed.cpp
      </Files>
    </Private>
  </Sources>
  <Output Kind="Executable" Name="Typed" Target="Typed" />
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Configurations>
    <Configuration Name="Runtime" BuildConfiguration="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Configurations>
</Project>
)");

    const auto project = LoadProjectManifest(projectPath);
    REQUIRE(project.sources.has_value());
    REQUIRE(project.sources->publicSources.roots.size() == 1);
    REQUIRE(project.sources->publicSources.files.size() == 1);
    REQUIRE(project.sources->privateSources.roots.size() == 2);
    REQUIRE(project.sources->privateSources.files.size() == 2);
    REQUIRE(project.sources->privateSources.roots.back().selectors.operatingSystem == "linux");
    REQUIRE(project.sources->privateSources.roots.back().selectors.architecture == "x64");
    REQUIRE(project.sources->privateSources.files.front().selectors.buildConfiguration == "Debug");
    REQUIRE(project.sources->privateSources.files.back().path == "src/listed.cpp");
    REQUIRE(project.sources->privateSources.files.back().selectors.operatingSystem == "linux");

    const auto mixedPath = temp.path() / "Mixed.nginproj";
    WriteFile(mixedPath,
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="2" Name="Mixed" Type="Application" DefaultConfiguration="Runtime">
  <SourceRoots>
    <SourceRoot Path="src" />
  </SourceRoots>
  <Sources>
    <Private>
      <Root Path="src" />
    </Private>
  </Sources>
  <Output Kind="Executable" Name="Mixed" Target="Mixed" />
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Configurations>
    <Configuration Name="Runtime" BuildConfiguration="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Configurations>
</Project>
)");
    REQUIRE_THROWS_WITH(LoadProjectManifest(mixedPath), ContainsSubstring("may not declare both <SourceRoots> and <Sources>"));
}

TEST_CASE("generated CMake applies typed source selectors and selected build settings")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Library/Typed.Library.nginproj";
    WriteFile(projectPath,
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="2" Name="Typed.Library" Type="Library" DefaultConfiguration="Runtime">
  <Sources>
    <Public>
      <Root Path="include" />
      <File Path="include/Typed/Library.hpp" />
    </Public>
    <Private>
      <Root Path="src" Include="**/*.cpp" Exclude="**/*.generated.cpp" />
      <Root Path="src/linux" OperatingSystem="linux" />
      <Root Path="src/windows" OperatingSystem="windows" />
      <Files>
        manual/manual.cpp
      </Files>
    </Private>
  </Sources>
  <Output Kind="StaticLibrary" Name="Typed.Library" Target="TypedLibrary" />
  <Build Backend="CMake" Mode="Generated" Language="CXX" LanguageStandard="23">
    <CompileDefinitions>
      <Definition Value="TYPED_LIBRARY_BUILD" Visibility="Private" />
      <Definition Value="TYPED_LIBRARY_LINUX" Visibility="Private" OperatingSystem="linux" />
      <Definition Value="TYPED_LIBRARY_WINDOWS" Visibility="Private" OperatingSystem="windows" />
      <Definition Value="TYPED_LIBRARY_DEBUG" Visibility="Private" BuildConfiguration="Debug" />
    </CompileDefinitions>
  </Build>
  <Environments>
    <Environment Name="dev" />
  </Environments>
  <Configurations>
    <Configuration Name="Runtime" BuildConfiguration="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Configurations>
</Project>
)");
    WriteFile(temp.path() / "Library/include/Typed/Library.hpp", "#pragma once\nvoid typed_library();\n");
    WriteFile(temp.path() / "Library/src/core.cpp", "#include <Typed/Library.hpp>\nvoid typed_library() {}\n");
    WriteFile(temp.path() / "Library/src/ignored.generated.cpp", "void typed_library_generated() {}\n");
    WriteFile(temp.path() / "Library/src/linux/linux.cpp", "void typed_library_linux() {}\n");
    WriteFile(temp.path() / "Library/src/windows/windows.cpp", "void typed_library_windows() {}\n");
    WriteFile(temp.path() / "Library/manual/manual.cpp", "void typed_library_manual() {}\n");

    const auto project = LoadProjectManifest(projectPath);
    const std::optional<std::string> configurationName{"Runtime"};
    const auto &configuration = ConfigurationByName(project, configurationName);
    const auto configured = ConfigureLaunch(project, configuration, temp.path() / "stage");
    REQUIRE(configured.value.has_value());
    REQUIRE_FALSE(configured.diagnostics.HasErrors());

    const auto generated = ReadFile(temp.path() / "stage/.ngin/cmake-src/CMakeLists.txt");
    REQUIRE_THAT(generated, ContainsSubstring("target_include_directories(\"TypedLibrary\" PUBLIC \"" + (temp.path() / "Library/include").generic_string() + "\")"));
    REQUIRE_THAT(generated, ContainsSubstring("target_include_directories(\"TypedLibrary\" PRIVATE \"" + (temp.path() / "Library/src").generic_string() + "\")"));
    REQUIRE_THAT(generated, ContainsSubstring("target_include_directories(\"TypedLibrary\" PRIVATE \"" + (temp.path() / "Library/src/linux").generic_string() + "\")"));
    REQUIRE_THAT(generated, !ContainsSubstring((temp.path() / "Library/src/windows").generic_string()));
    REQUIRE_THAT(generated, ContainsSubstring("TYPED_LIBRARY_LINUX"));
    REQUIRE_THAT(generated, ContainsSubstring("TYPED_LIBRARY_DEBUG"));
    REQUIRE_THAT(generated, !ContainsSubstring("TYPED_LIBRARY_WINDOWS"));
    REQUIRE(CountOccurrences(generated, (temp.path() / "Library/src/core.cpp").generic_string()) == 1);
    REQUIRE(CountOccurrences(generated, (temp.path() / "Library/src/linux/linux.cpp").generic_string()) == 1);
    REQUIRE(CountOccurrences(generated, (temp.path() / "Library/src/windows/windows.cpp").generic_string()) == 0);
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
<Project SchemaVersion="2" Name="Settings.App" Type="Application" DefaultConfiguration="Runtime">
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
  <Configurations>
    <Configuration Name="Runtime" BuildConfiguration="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Configurations>
</Project>
)");

    const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
    const std::optional<std::string> configurationName{"Runtime"};
    const auto &configuration = ConfigurationByName(project, configurationName);
    const auto resolved = ResolveLaunch(project, configuration);

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
<Project SchemaVersion="2" Name="Global.Settings.App" Type="Application" DefaultConfiguration="Runtime">
  <Output Kind="Executable" Name="Global.Settings.App" Target="GlobalSettingsApp" />
  <Environments>
    <Environment Name="dev">
      <Variables>
        <Variable Name="VULKAN_SDK" FromLocalSetting="sdk.vulkan.root" Required="true" />
      </Variables>
    </Environment>
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
<Project SchemaVersion="2" Name="Missing.Settings.App" Type="Application" DefaultConfiguration="Runtime">
  <Output Kind="Executable" Name="Missing.Settings.App" Target="MissingSettingsApp" />
  <Environments>
    <Environment Name="dev">
      <Variables>
        <Variable Name="API_TOKEN" FromEnvironment="NGIN_TEST_MISSING_TOKEN" Required="true" Secret="true" />
      </Variables>
    </Environment>
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
    REQUIRE(ContainsDiagnostic(DiagnosticMessages(resolved.diagnostics), "missing required secret variable 'API_TOKEN'"));
}

TEST_CASE("variable declarations reject conflicting sources and literal secrets")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Invalid.nginproj";
    WriteFile(projectPath,
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="2" Name="Invalid.Settings.App" Type="Application" DefaultConfiguration="Runtime">
  <Output Kind="Executable" Name="Invalid.Settings.App" Target="InvalidSettingsApp" />
  <Environments>
    <Environment Name="dev">
      <Variables>
        <Variable Name="BAD" Value="literal" FromEnvironment="BAD" />
      </Variables>
    </Environment>
  </Environments>
  <Configurations>
    <Configuration Name="Runtime" BuildConfiguration="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Configurations>
</Project>
)");
    REQUIRE_THROWS_WITH(LoadProjectManifest(projectPath), ContainsSubstring("must declare exactly one"));

    WriteFile(projectPath,
              R"(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="2" Name="Invalid.Settings.App" Type="Application" DefaultConfiguration="Runtime">
  <Output Kind="Executable" Name="Invalid.Settings.App" Target="InvalidSettingsApp" />
  <Environments>
    <Environment Name="dev">
      <Variables>
        <Variable Name="BAD_SECRET" Value="literal" Secret="true" />
      </Variables>
    </Environment>
  </Environments>
  <Configurations>
    <Configuration Name="Runtime" BuildConfiguration="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />
  </Configurations>
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
    launch.configuration.name = "Runtime";
    launch.configuration.buildConfiguration = "Debug";
    launch.configuration.operatingSystem = "linux";
    launch.configuration.architecture = "x64";
    launch.configuration.environmentName = "dev";
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

TEST_CASE("project parsing rejects missing required output metadata")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Invalid.nginproj";
    WriteFile(projectPath,
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

    REQUIRE_THROWS_WITH(LoadProjectManifest(projectPath), ContainsSubstring("missing <Output>"));
}

TEST_CASE("project autodiscovery resolves nearest nginproj in the current tree")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Nested/App.nginproj";
    WriteFile(projectPath,
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
    WriteFile(temp.path() / "Workspace.ngin",
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
    WriteFile(temp.path() / "Packages/A/A.nginpkg",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="2" Name="Package.A" Version="1.0.0">
  <Dependencies>
    <PackageRef Name="Package.B" />
  </Dependencies>
  <Build Backend="CMake" Mode="Manual" />
  <Modules>
    <Module Name="Module.A" Family="Core" Type="Runtime" StartupStage="Features" />
  </Modules>
</Package>
)");
    WriteFile(temp.path() / "Packages/B/B.nginpkg",
              R"(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="2" Name="Package.B" Version="1.0.0">
  <Dependencies>
    <PackageRef Name="Package.A" />
  </Dependencies>
  <Build Backend="CMake" Mode="Manual" />
  <Modules>
    <Module Name="Module.B" Family="Core" Type="Runtime" StartupStage="Features" />
  </Modules>
</Package>
)");
    WriteFile(temp.path() / "App/App.nginproj",
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
    const auto projectPath = RepoRoot() / "Examples/ProjectRef.Config/CollisionRoot/"
                                          "ProjectRef.Config.CollisionRoot.nginproj";
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

TEST_CASE("build facade writes launch manifests and preserves unrelated output "
          "files")
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

TEST_CASE("clean facade removes owned generated artifacts and preserves "
          "unrelated files")
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
    const auto cmake = ResolveToolPath("cmake", RepoRoot());
    REQUIRE(cmake.has_value());
    REQUIRE(RunProcess(cmake->path, {"--version"}) == 0);
}
