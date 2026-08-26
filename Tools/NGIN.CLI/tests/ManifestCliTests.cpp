#include "AuthoredManifest.hpp"
#include "ActionDiagnostics.hpp"
#include "ManifestCli.hpp"
#include "ManifestArtifacts.hpp"
#include "ManifestFormatter.hpp"
#include "ManifestPaths.hpp"
#include "PackageModel.hpp"
#include "ProjectModel.hpp"
#include "Selection.hpp"
#include "TestSupport.hpp"
#include "WorkspaceModel.hpp"

namespace
{
    class ScopedStreamCapture
    {
      public:
        explicit ScopedStreamCapture(std::ostream &stream) : stream_(stream)
        {
            previous_ = stream.rdbuf(buffer_.rdbuf());
        }
        ~ScopedStreamCapture() { stream_.rdbuf(previous_); }
        [[nodiscard]] auto Text() const -> std::string { return buffer_.str(); }

      private:
        std::ostream &stream_;
        std::streambuf *previous_{};
        std::ostringstream buffer_{};
    };
} // namespace

TEST_CASE("superseded manifest forms are rejected without compatibility parsing")
{
    const std::vector<std::string> rejected{
        R"(<Project SchemaVersion="4" Name="Old" Type="Application" />)",
        R"(<Project Name="Old"><Application /></Project>)",
        R"(<Project Name="Old" Type="Application"><Profile Name="Debug" /></Project>)",
        R"(<Project Name="Old" Type="Application"><Runtime /></Project>)",
        R"(<Project Name="Old" Type="Module" />)",
        R"(<Project Name="Old" Type="Application"><Dependencies><Package Name="Core" Scope="Target" /></Dependencies></Project>)",
        R"(<Package Name="Old" Version="1.0.0"><Exports><Library Name="Core" /></Exports><Features /></Package>)"};
    for (const auto &source : rejected)
    {
        INFO(source);
        const auto parsed = ParseAuthoredManifestText(source, "rejections/legacy.nginproj");
        CHECK_FALSE(parsed.Succeeded());
    }
}

TEST_CASE("CLI authoring creates and edits only the direct model")
{
    TempDir temp{};
    REQUIRE(NewProject(temp.path(), "app", "Hello") == 0);
    const auto projectPath = temp.path() / "Hello/Hello.nginproj";
    auto text = ReadFile(projectPath);
    REQUIRE_THAT(text, ContainsSubstring(R"(<Project Name="Hello" Type="Application">)"));
    REQUIRE_THAT(text, !ContainsSubstring("SchemaVersion"));

    CliArguments add{};
    add.projectPath = projectPath.string();
    add.packageName = "Example.Core";
    add.compatibleVersion = "1";
    add.exportUses = {"Library:Core"};
    REQUIRE(AddPackage(temp.path(), add) == 0);
    text = ReadFile(projectPath);
    REQUIRE_THAT(text, ContainsSubstring(R"(<Package Name="Example.Core" Compatible="1">)"));
    REQUIRE_THAT(text, ContainsSubstring(R"(<Use Library="Core" />)"));
    REQUIRE(ParseAuthoredManifest(projectPath).Succeeded());

    CliArguments remove{};
    remove.projectPath = projectPath.string();
    remove.packageName = "Example.Core";
    REQUIRE(RemovePackage(temp.path(), remove) == 0);
    REQUIRE_THAT(ReadFile(projectPath), !ContainsSubstring("Example.Core"));
}

TEST_CASE("formatter preserves comments in direct manifests")
{
    const auto formatted = FormatManifestXml(
        R"(<Project Name="App" Type="Application"><!-- rationale --><Build><Source Include="src/**/*.cpp" /></Build></Project>)");
    REQUIRE_THAT(formatted, ContainsSubstring("<!--rationale-->"));
    REQUIRE_THAT(formatted, ContainsSubstring(R"(<Source Include="src/**/*.cpp" />)"));
}

TEST_CASE("Action diagnostics preserve tool identity and source ranges as structured JSON")
{
    const auto diagnostics = ParseActionDiagnostics(
        "C:\\work\\src\\main.cpp:8:16: warning: 42 is a magic number [readability-magic-numbers]\n"
        "/work/src/main.cpp:2:4: error: broken source\n",
        "NGIN.Tooling.ClangTidy::Analyze");
    REQUIRE(diagnostics.size() == 2);
    CHECK(diagnostics[0].file == "C:\\work\\src\\main.cpp");
    CHECK(diagnostics[0].startLine == 8);
    CHECK(diagnostics[0].startColumn == 16);
    CHECK(diagnostics[0].endColumn == 17);
    CHECK(diagnostics[0].code == "readability-magic-numbers");
    CHECK(diagnostics[0].source == "NGIN.Tooling.ClangTidy::Analyze");
    CHECK(diagnostics[1].severity == "error");

    const auto json = SerializeActionDiagnostics(diagnostics);
    CHECK_THAT(json, ContainsSubstring(R"("kind":"NGIN.ActionDiagnostics")"));
    CHECK_THAT(json, ContainsSubstring(R"("state":"complete")"));
    CHECK_THAT(json, ContainsSubstring(R"("line":8)"));
    CHECK_THAT(json, ContainsSubstring(R"("fixes":[])"));
}

TEST_CASE("editor metadata carries authoritative enumeration choices")
{
    const auto metadata = GenerateManifestArtifacts().at("manifest-editor-metadata.json");
    CHECK_THAT(metadata, ContainsSubstring(R"("name": "Type", "type": "enumeration", "required": true, "values": ["Application", "Library", "Tool", "Test", "Benchmark", "Plugin", "External"])"));
    CHECK_THAT(metadata, ContainsSubstring(R"("name": "Linkage", "type": "enumeration", "required": false, "values": ["Static", "Shared", "Interface"])"));
    CHECK_THAT(metadata, !ContainsSubstring(R"("Module")"));
    CHECK_THAT(metadata, !ContainsSubstring(R"("HeaderOnly")"));
}

TEST_CASE("portable path and glob authoring is rooted deterministic and "
          "traversal-safe")
{
    const auto current = NormalizePortablePath(".", PortablePathBase::Manifest);
    REQUIRE(current.Succeeded());
    REQUIRE(current.value->value == ".");

    TempDir temp{};
    WriteFile(temp.path() / "src/z.cpp", "");
    WriteFile(temp.path() / "src/nested/a.cpp", "");
    WriteFile(temp.path() / "outside.cpp", "");
    const auto expanded = ExpandPortableGlob(temp.path(), "src/**/*.cpp", false);
    REQUIRE(expanded.Succeeded());
    REQUIRE(expanded.matches.size() == 2);
    REQUIRE(expanded.matches[0].value == "src/nested/a.cpp");
    REQUIRE(expanded.matches[1].value == "src/z.cpp");
    REQUIRE_FALSE(ExpandPortableGlob(temp.path(), "../*.cpp", false).Succeeded());
}

TEST_CASE("presets expand concrete inputs without becoming a selection dimension")
{
    const Preset preset{
        .name = "release",
        .command = "build",
        .selection = SelectionRequest{
            .configuration = "Release", .target = "host", .toolchain = "default", .options = {{"Tracing", "false"}}}};
    const auto expanded = ExpandPreset(preset, "build", SelectionRequest{});
    REQUIRE(expanded.Succeeded());
    REQUIRE(expanded.value->configuration == "Release");
    REQUIRE(expanded.value->target == "host");
    REQUIRE(expanded.value->options.at("Tracing") == "false");
    REQUIRE_FALSE(ExpandPreset(preset, "test", SelectionRequest{}).Succeeded());
}

TEST_CASE("CLI accepts an explicit Launch selection for run workflows")
{
    std::vector<std::string> storage{"ngin", "run", "--project", "App.nginproj", "--launch", "diagnostics"};
    std::vector<char *> arguments{};
    arguments.reserve(storage.size());
    for (auto &value : storage) arguments.push_back(value.data());
    const auto parsed = ParseCliArguments(static_cast<int>(arguments.size()), arguments.data(), 2);
    REQUIRE(parsed.projectPath == "App.nginproj");
    REQUIRE(parsed.launch == "diagnostics");
}

TEST_CASE("project Refinements apply selected payloads with deterministic "
          "specificity")
{
    const auto authored = ParseAuthoredManifest(RepoRoot() / "Examples/Hello.Native/Hello.Native.nginproj");
    REQUIRE(authored.Succeeded());
    const auto project = ParseSemanticProject(std::get<AuthoredProjectManifest>(*authored.value));
    REQUIRE(project.Succeeded());

    SelectionFacts selection{};
    selection.configuration.name = "Debug";
    auto refined = ApplyProjectRefinements(*project.value, selection);
    REQUIRE(refined.Succeeded());
    REQUIRE(std::ranges::any_of(refined.value->build.declarations, [](const BuildItemDeclaration &item) {
        return item.kind == BuildItemKind::Define && item.pattern == "HELLO_NATIVE_LOCAL_DEBUG";
    }));

    selection.configuration.name = "Release";
    refined = ApplyProjectRefinements(*project.value, selection);
    REQUIRE(refined.Succeeded());
    REQUIRE_FALSE(std::ranges::any_of(refined.value->build.declarations, [](const BuildItemDeclaration &item) {
        return item.kind == BuildItemKind::Define && item.pattern == "HELLO_NATIVE_LOCAL_DEBUG";
    }));
}

TEST_CASE("equal-specificity Refinement writes conflict and a more specific "
          "write wins")
{
    const auto parseProject = [](const std::string_view source) {
        const auto authored = ParseAuthoredManifestText(std::string(source), "refinements/App.nginproj");
        REQUIRE(authored.Succeeded());
        return ParseSemanticProject(std::get<AuthoredProjectManifest>(*authored.value));
    };
    SelectionFacts selection{};
    selection.configuration.name = "Debug";
    selection.target.name = "host";
    selection.target.operatingSystem = "windows";

    const auto conflicting = parseProject(R"(<Project Name="App" Type="Application">
  <Refinements>
    <Refinement><Select><Configuration Name="Debug" /></Select><Build><Define Name="MODE" Value="one" /></Build></Refinement>
    <Refinement><Select><Configuration Name="Debug" /></Select><Build><Define Name="MODE" Value="two" /></Build></Refinement>
  </Refinements>
</Project>)");
    REQUIRE(conflicting.Succeeded());
    const auto conflict = ApplyProjectRefinements(*conflicting.value, selection);
    REQUIRE_FALSE(conflict.Succeeded());
    REQUIRE(std::ranges::any_of(conflict.diagnostics,
                                [](const ManifestDiagnostic &diagnostic) { return diagnostic.code == "NGIN2006"; }));

    const auto specific = parseProject(R"(<Project Name="App" Type="Application">
  <Refinements>
    <Refinement><Select><Configuration Name="Debug" /></Select><Build><Define Name="MODE" Value="one" /></Build></Refinement>
    <Refinement><Select><Configuration Name="Debug" /><Target OS="windows" /></Select><Build><Define Name="MODE" Value="two" /></Build></Refinement>
  </Refinements>
</Project>)");
    REQUIRE(specific.Succeeded());
    const auto winner = ApplyProjectRefinements(*specific.value, selection);
    REQUIRE(winner.Succeeded());
    const auto define = std::ranges::find_if(winner.value->build.declarations, [](const BuildItemDeclaration &item) {
        return item.kind == BuildItemKind::Define && item.pattern == "MODE";
    });
    REQUIRE(define != winner.value->build.declarations.end());
    REQUIRE(define->value == "two");
}

TEST_CASE("every checked-in authored manifest uses the direct grammar")
{
    std::vector<fs::path> manifests{RepoRoot() / "NGIN.ngin", RepoRoot() / "Tools/NGIN.CLI/NGIN.CLI.nginproj"};
    for (const auto &relative : {"Examples", "Packages", "Tools/NGIN.CLI/tests/fixtures", "docs/examples"})
    {
        for (const auto &entry : fs::recursive_directory_iterator(RepoRoot() / relative))
        {
            if (!entry.is_regular_file()) continue;
            const auto extension = entry.path().extension();
            if (extension == ".ngin" || extension == ".nginproj" || extension == ".nginpkg")
                manifests.push_back(entry.path());
        }
    }
    std::ranges::sort(manifests);
    manifests.erase(std::unique(manifests.begin(), manifests.end()), manifests.end());
    REQUIRE(manifests.size() == 50);
    for (const auto &path : manifests)
    {
        INFO(path.string());
        const auto authored = ParseAuthoredManifest(path);
        REQUIRE(authored.Succeeded());
        std::visit(
            [&](const auto &manifest) {
                using T = std::decay_t<decltype(manifest)>;
                if constexpr (std::is_same_v<T, AuthoredProjectManifest>)
                    REQUIRE(ParseSemanticProject(manifest).Succeeded());
                else if constexpr (std::is_same_v<T, AuthoredPackageManifest>)
                    REQUIRE(ParseSemanticPackage(manifest).Succeeded());
                else
                    REQUIRE(ParseSemanticWorkspace(manifest).Succeeded());
            },
            *authored.value);
    }
}

TEST_CASE("project references contribute child composition identity and reject "
          "cycles")
{
    SECTION("resolved child fingerprint")
    {
        CliArguments arguments{};
        arguments.projectPath = (RepoRoot() / "Tools/NGIN.CLI/tests/fixtures/ProjectRef.Config/Root/"
                                              "ProjectRef.Config.Root.nginproj")
                                    .string();
        arguments.format = "json";
        ScopedStreamCapture output{std::cout};
        REQUIRE(InspectComposition(RepoRoot(), arguments) == 0);
        REQUIRE_THAT(output.Text(), ContainsSubstring("Project:ProjectRef.Config.Library@sha256:"));
    }

    SECTION("cycle")
    {
        TempDir temp{};
        const auto first = temp.path() / "First.nginproj";
        const auto second = temp.path() / "Second.nginproj";
        WriteFile(
            first,
            R"(<Project Name="First" Type="Library" Linkage="Static"><Dependencies><Project Name="Second" Path="Second.nginproj" /></Dependencies></Project>)");
        WriteFile(
            second,
            R"(<Project Name="Second" Type="Library" Linkage="Static"><Dependencies><Project Name="First" Path="First.nginproj" /></Dependencies></Project>)");
        CliArguments arguments{};
        arguments.projectPath = first.string();
        ScopedStreamCapture errors{std::cerr};
        REQUIRE(InspectComposition(temp.path(), arguments) == 1);
        REQUIRE_THAT(errors.Text(), ContainsSubstring("Project dependency cycle"));
    }
}

TEST_CASE("Directory PackageProvider sources discover manifests and honor "
          "workspace bindings")
{
    TempDir temp{};
    const auto project = temp.path() / "App/App.nginproj";
    const auto workspace = temp.path() / "Workspace.ngin";
    WriteFile(
        project,
        R"(<Project Name="App" Type="Application"><Dependencies><Package Name="Example" Exact="1.2.3" /></Dependencies><Build><Source Include="src/**/*.cpp" /></Build></Project>)");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }");
    WriteFile(temp.path() / "Packages/Example/CMakeLists.txt",
              "add_library(Example::Core INTERFACE IMPORTED GLOBAL)\n");
    WriteFile(
        temp.path() / "Packages/Example/Example.nginpkg",
        R"(<Package xmlns:cmake="urn:ngin:integration:cmake" Name="Example" Version="1.2.3"><Exports><Library Name="Core" Default="true" /></Exports><Integrations><cmake:Manual Source="."><cmake:Target Export="Core" Name="Example::Core" /></cmake:Manual></Integrations></Package>)");
    WriteFile(
        workspace,
        R"(<Workspace Name="Fixture"><Projects><Project Path="App/App.nginproj" /></Projects><Packages><Source Name="local" Kind="Directory" Path="Packages" /><Binding Package="Example" Source="local" Coordinate="Example" /></Packages></Workspace>)");

    CliArguments arguments{};
    arguments.projectPath = project.string();
    arguments.workspacePath = workspace.string();
    arguments.format = "json";
    ScopedStreamCapture output{std::cout};
    REQUIRE(InspectComposition(temp.path(), arguments) == 0);
    REQUIRE_THAT(output.Text(), ContainsSubstring(R"("providerKind":"Directory")"));
    REQUIRE_THAT(output.Text(), ContainsSubstring(R"("source":"local")"));
}
