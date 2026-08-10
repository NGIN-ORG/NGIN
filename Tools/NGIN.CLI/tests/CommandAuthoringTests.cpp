#include "TestSupport.hpp"
#include "AuthoredManifest.hpp"

TEST_CASE("direct project authoring rejects legacy product wrappers")
{
    const auto wrapped = ParseAuthoredManifestText(
        R"xml(<Project Name="Legacy"><Application><Build /></Application></Project>)xml",
        "Legacy.nginproj");

    REQUIRE_FALSE(wrapped.Succeeded());
    REQUIRE(std::ranges::any_of(wrapped.diagnostics, [](const ManifestDiagnostic &diagnostic) {
        return diagnostic.code == "NGIN1003" && diagnostic.message.find("Application") != std::string::npos;
    }));
}

TEST_CASE("new command creates product-first project skeletons")
{
    TempDir temp{};

    REQUIRE(CmdNew(temp.path(), "app", "Hello.Native") == 0);

    const auto projectPath = temp.path() / "Hello.Native/Hello.Native.nginproj";
    REQUIRE(fs::exists(projectPath));
    REQUIRE(fs::exists(temp.path() / "Hello.Native/src/main.cpp"));

    const auto project = ParseAuthoredManifest(projectPath);
    REQUIRE(project.Succeeded());
    const auto &authored = std::get<AuthoredProjectManifest>(*project.value);
    REQUIRE(authored.type == "Application");
    REQUIRE_THAT(ReadFile(projectPath), ContainsSubstring(R"(<Source Include="src/**/*.cpp" />)"));
    REQUIRE_THAT(ReadFile(projectPath), !ContainsSubstring("SchemaVersion"));

    REQUIRE(CmdNew(temp.path(), "lib", "Game.Engine") == 0);
    const auto libraryPath = temp.path() / "Game.Engine/Game.Engine.nginproj";
    REQUIRE(fs::exists(libraryPath));
    REQUIRE(fs::exists(temp.path() / "Game.Engine/include/Game.Engine.hpp"));
    REQUIRE(fs::exists(temp.path() / "Game.Engine/src/Game.Engine.cpp"));

    const auto library = ParseAuthoredManifest(libraryPath);
    REQUIRE(library.Succeeded());
    REQUIRE(std::get<AuthoredProjectManifest>(*library.value).type == "Library");

    for (const auto &[kind, type] : std::vector<std::pair<std::string, std::string>>{
             {"tool", "Tool"}, {"test", "Test"}, {"benchmark", "Benchmark"},
             {"plugin", "Plugin"}, {"external", "External"}}) {
        const auto projectName = "Example." + type;
        REQUIRE(CmdNew(temp.path(), kind, projectName) == 0);
        const auto parsed = ParseAuthoredManifest(temp.path() / projectName / (projectName + ".nginproj"));
        REQUIRE(parsed.Succeeded());
        REQUIRE(std::get<AuthoredProjectManifest>(*parsed.value).type == type);
    }
}

TEST_CASE("package add update and remove author typed package dependencies")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Hello.Native.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project Name="Hello.Native" Type="Application" />
)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");

    ParsedArgs args{};
    args.projectPath = projectPath.string();
    args.packageName = "NGIN.Core";
    args.compatibleVersion = "0.4";
    args.exportUses = {"Library:Core", "Plugin:Renderer"};
    args.optionAssignments = {"Tracing=true"};

    REQUIRE(CmdPackageAdd(temp.path(), args) == 0);

    const auto text = ReadFile(projectPath);
    REQUIRE_THAT(text, ContainsSubstring(R"(<Dependencies>)"));
    REQUIRE_THAT(text, ContainsSubstring(R"xml(<Package Name="NGIN.Core" Compatible="0.4">)xml"));
    REQUIRE_THAT(text, ContainsSubstring(R"(<Use Library="Core" />)"));
    REQUIRE_THAT(text, ContainsSubstring(R"(<Use Plugin="Renderer" />)"));
    REQUIRE_THAT(text, ContainsSubstring(R"(<Option Name="Tracing" Value="true" />)"));
    REQUIRE_THAT(text, !ContainsSubstring("Scope="));

    ParsedArgs updateArgs{};
    updateArgs.projectPath = projectPath.string();
    updateArgs.packageName = "NGIN.Core";
    updateArgs.exactVersion = "0.4.7";

    REQUIRE(CmdPackageUpdate(temp.path(), updateArgs) == 0);

    const auto updatedText = ReadFile(projectPath);
    REQUIRE_THAT(updatedText, ContainsSubstring(R"xml(<Package Name="NGIN.Core" Exact="0.4.7">)xml"));
    REQUIRE_THAT(updatedText, ContainsSubstring(R"(<Use Library="Core" />)"));
    REQUIRE_THAT(updatedText, ContainsSubstring(R"(<Option Name="Tracing" Value="true" />)"));

    ParsedArgs removeArgs{};
    removeArgs.projectPath = projectPath.string();
    removeArgs.packageName = "NGIN.Core";

    REQUIRE(CmdPackageRemove(temp.path(), removeArgs) == 0);

    REQUIRE(ParseAuthoredManifest(projectPath).Succeeded());
    REQUIRE_THAT(ReadFile(projectPath), !ContainsSubstring(R"(Name="NGIN.Core")"));
}

TEST_CASE("project-reference add authors direct project dependencies")
{
    TempDir temp{};
    const auto appPath = temp.path() / "App/App.nginproj";
    const auto libraryPath = temp.path() / "Lib/Lib.nginproj";
    WriteFile(appPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project Name="App" Type="Application" />
)xml");
    WriteFile(libraryPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project Name="Lib" Type="Library" />
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");
    WriteFile(temp.path() / "Lib/src/lib.cpp", "void lib() {}\n");

    ParsedArgs args{};
    args.projectPath = appPath.string();
    args.packageName = "../Lib/Lib.nginproj";

    REQUIRE(CmdProjectReferenceAdd(temp.path(), args) == 0);

    REQUIRE(ParseAuthoredManifest(appPath).Succeeded());
    REQUIRE_THAT(ReadFile(appPath), ContainsSubstring(R"(<Project Name="Lib" Path="../Lib/Lib.nginproj" />)"));
}

TEST_CASE("action add authors friendly typed action selection")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "App/App.nginproj";
    WriteFile(projectPath,
              R"xml(<Project Name="App" Type="Application" />)xml");

    ParsedArgs args{};
    args.projectPath = projectPath.string();
    args.packageName = "Example.Tooling::Codegen";
    args.toolActionKind = "Generate";
    REQUIRE(CmdToolActionAdd(temp.path(), args) == 0);

    REQUIRE_THAT(ReadFile(projectPath), ContainsSubstring(R"(<Generate Action="Example.Tooling::Codegen" />)"));
    REQUIRE(ParseAuthoredManifest(projectPath).Succeeded());
}

TEST_CASE("format command preserves comments in direct manifests")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "Format.App.nginproj";
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?><Project Name="Format.App" Type="Application"><!-- source rationale --><Build><Source Include="src/**/*.cpp" /><Define Name="FORMAT_APP" Value="1" /></Build></Project>)xml");
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }\n");

    ParsedArgs args{};
    args.projectPath = projectPath.string();

    REQUIRE(CmdManifestFormat(temp.path(), args) == 0);

    const auto formatted = ReadFile(projectPath);
    REQUIRE_THAT(formatted, ContainsSubstring("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n\n"));
    REQUIRE_THAT(formatted, ContainsSubstring("  <!--source rationale-->\n"));
    REQUIRE_THAT(formatted, ContainsSubstring("    <Source Include=\"src/**/*.cpp\" />\n"));
    REQUIRE_THAT(formatted, ContainsSubstring("    <Define Name=\"FORMAT_APP\" Value=\"1\" />\n"));
    REQUIRE(ParseAuthoredManifest(projectPath).Succeeded());
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
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("generatedFrom": "ManifestSpec")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("extension": ".nginproj")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("root": "Project")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("id": "project.root")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("semanticValidator": "validate-workspace")"));
    REQUIRE_THAT(captured.str(), !ContainsSubstring("schemaVersion"));
    REQUIRE_THAT(captured.str(), !ContainsSubstring(R"("Type": "Module")"));
}

TEST_CASE("validate uses structural and semantic models for every manifest kind")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "App.nginproj";
    const auto packagePath = temp.path() / "Example.nginpkg";
    const auto workspacePath = temp.path() / "Workspace.ngin";
    WriteFile(projectPath, R"xml(<Project Name="App" Type="Application" />)xml");
    WriteFile(packagePath, R"xml(<Package Name="Example" Version="1.0.0">
  <Exports><Library Name="Example" Default="true" /></Exports>
</Package>)xml");
    WriteFile(workspacePath, R"xml(<Workspace Name="Demo">
  <Projects><Project Path="App.nginproj" /></Projects>
</Workspace>)xml");

    for (const auto &path : {projectPath, packagePath, workspacePath}) {
        ParsedArgs args{};
        args.projectPath = path.string();
        INFO(path);
        CHECK(CmdValidate(temp.path(), args) == 0);
    }
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
