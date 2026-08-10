#include "AuthoredManifest.hpp"
#include "ManifestCli.hpp"
#include "ManifestFormatter.hpp"
#include "ManifestPaths.hpp"
#include "PackageModel.hpp"
#include "ProjectModel.hpp"
#include "Selection.hpp"
#include "TestSupport.hpp"
#include "WorkspaceModel.hpp"

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

TEST_CASE("portable path and glob authoring is rooted deterministic and traversal-safe")
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
    const Preset preset{.name = "release",
                        .command = "build",
                        .selection = SelectionRequest{.configuration = "Release",
                                                      .target = "host",
                                                      .toolchain = "default",
                                                      .options = {{"Tracing", "false"}}}};
    const auto expanded = ExpandPreset(preset, "build", SelectionRequest{});
    REQUIRE(expanded.Succeeded());
    REQUIRE(expanded.value->configuration == "Release");
    REQUIRE(expanded.value->target == "host");
    REQUIRE(expanded.value->options.at("Tracing") == "false");
    REQUIRE_FALSE(ExpandPreset(preset, "test", SelectionRequest{}).Succeeded());
}

TEST_CASE("every checked-in authored manifest uses the direct grammar")
{
    std::vector<fs::path> manifests{RepoRoot() / "NGIN.ngin",
                                    RepoRoot() / "Tools/NGIN.CLI/NGIN.CLI.nginproj"};
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
    REQUIRE(manifests.size() == 49);
    for (const auto &path : manifests)
    {
        INFO(path.string());
        const auto authored = ParseAuthoredManifest(path);
        REQUIRE(authored.Succeeded());
        std::visit([&](const auto &manifest) {
            using T = std::decay_t<decltype(manifest)>;
            if constexpr (std::is_same_v<T, AuthoredProjectManifest>)
                REQUIRE(ParseSemanticProject(manifest).Succeeded());
            else if constexpr (std::is_same_v<T, AuthoredPackageManifest>)
                REQUIRE(ParseSemanticPackage(manifest).Succeeded());
            else
                REQUIRE(ParseSemanticWorkspace(manifest).Succeeded());
        }, *authored.value);
    }
}
