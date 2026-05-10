#include "TestSupport.hpp"

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
    REQUIRE(ReadFile(archivePath).rfind("PK\003\004", 0) == 0);
    REQUIRE_THAT(ReadZipEntry(archivePath, "package.nginpkg"),
                 ContainsSubstring(R"(<Package SchemaVersion="4" Name="Game.Engine" Version="1.0.0">)"));

    const auto package = LoadPackageManifest(packagePath);
    REQUIRE(package.name == "Game.Engine");
    REQUIRE(package.version == "1.0.0");
    REQUIRE(package.artifacts.libraries.size() == 1);
    REQUIRE(package.artifacts.libraries[0].target == "Game::Engine");

    const auto archivedPackage = LoadPackageManifest(archivePath);
    REQUIRE(archivedPackage.name == "Game.Engine");
    REQUIRE(archivedPackage.version == "1.0.0");
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
