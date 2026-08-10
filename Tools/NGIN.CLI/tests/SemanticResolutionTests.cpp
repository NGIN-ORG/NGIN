#include "TestSupport.hpp"
#include "DependencyLock.hpp"
#include "SemanticResolver.hpp"

#include <type_traits>
#include <utility>

static_assert(std::is_const_v<std::remove_reference_t<decltype(
              std::declval<const ResolvedCompositionGraph &>().Data())>>);

namespace
{
    [[nodiscard]] auto ParseProjectForResolution(const fs::path &path) -> SemanticProject
    {
        const auto authored = ParseAuthoredManifest(path);
        REQUIRE(authored.Succeeded());
        const auto project = ParseSemanticProject(std::get<AuthoredProjectManifest>(*authored.value));
        REQUIRE(project.Succeeded());
        return *project.value;
    }

    [[nodiscard]] auto Release(const fs::path &manifest, const std::string &name,
                               const std::string &version) -> DirectoryPackageRelease
    {
        return DirectoryPackageRelease{.name = name,
                                       .manifest = manifest,
                                       .root = manifest.parent_path(),
                                       .nativeIdentity = name + "@" + version,
                                       .revision = "revision-" + version,
                                       .integrity = "sha256:" + name + "-" + version};
    }

    [[nodiscard]] auto TargetSelection() -> SelectionFacts
    {
        return SelectionFacts{.configuration = Configuration{.name = "Debug"},
                              .target = Target{.name = "linux-x64",
                                               .operatingSystem = "linux",
                                               .architecture = "x64"},
                              .toolchain = Toolchain{.name = "clang",
                                                     .compiler = "clang",
                                                     .compilerVersion = "19",
                                                     .runtimeLibrary = "libc++",
                                                     .linker = "lld"}};
    }

    [[nodiscard]] auto HostSelection() -> SelectionFacts
    {
        auto selection = TargetSelection();
        selection.target.name = "host";
        return selection;
    }
}

TEST_CASE("semantic resolver closes version diamonds and export requirements deterministically")
{
    TempDir temp{};
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }");
    const auto projectPath = temp.path() / "App.nginproj";
    WriteFile(projectPath, R"xml(<Project Name="App" Type="Application"><Dependencies>
  <Package Name="Framework" Compatible="1" />
  <Package Name="Other" Compatible="1" />
</Dependencies></Project>)xml");

    const auto base = [&](const std::string &version) {
        const auto path = temp.path() / ("Base-" + version + ".nginpkg");
        WriteFile(path, "<Package Name=\"Base\" Version=\"" + version +
                            "\"><Exports><Library Name=\"Core\" Default=\"true\" /></Exports></Package>");
        return Release(path, "Base", version);
    };
    const auto frameworkPath = temp.path() / "Framework.nginpkg";
    WriteFile(frameworkPath, R"xml(<Package Name="Framework" Version="1.0.0"><Requires>
  <Package Name="Base"><Version AtLeast="1.0.0" Before="2.0.0" /></Package>
</Requires><Exports><Library Name="Framework" Default="true" /></Exports></Package>)xml");
    const auto otherPath = temp.path() / "Other.nginpkg";
    WriteFile(otherPath, R"xml(<Package Name="Other" Version="1.0.0"><Requires>
  <Package Name="Base"><Version AtLeast="1.5.0" Before="3.0.0" /></Package>
</Requires><Exports><Library Name="Other" Default="true" /></Exports></Package>)xml");
    DirectoryPackageProvider provider{
        "local", {base("1.4.0"), base("1.8.0"), base("2.5.0"),
                  Release(frameworkPath, "Framework", "1.0.0"), Release(otherPath, "Other", "1.0.0")}};
    const auto project = ParseProjectForResolution(projectPath);
    const SemanticResolutionRequest request{.project = project,
                                            .projectDirectory = temp.path(),
                                            .workspaceRoot = temp.path(),
                                            .targetSelection = TargetSelection(),
                                            .hostSelection = HostSelection(),
                                            .packageProviders = {&provider}};
    const auto first = ResolveComposition(request);
    REQUIRE(first.Succeeded());
    const auto second = ResolveComposition(request);
    REQUIRE(second.Succeeded());
    REQUIRE(first.graph->CanonicalSerialization() == second.graph->CanonicalSerialization());
    REQUIRE(first.graph->CompositionIdentity() == second.graph->CompositionIdentity());
    REQUIRE(first.graph->Data().packages.size() == 3);
    REQUIRE(std::ranges::any_of(first.graph->Data().packages, [](const GraphPackageInstance &package) {
        return package.coordinate.name == "Base" && package.coordinate.exactVersion == "1.8.0";
    }));
    REQUIRE(first.graph->Data().exports.size() == 3);
    REQUIRE(first.graph->Data().buildItems.size() == 1);
    REQUIRE(first.graph->Data().edges.size() >= 6);
    REQUIRE(first.graph->CanonicalSerialization().find(temp.path().generic_string()) == std::string::npos);
    REQUIRE(first.graph->CanonicalSerialization().find("CMake") == std::string::npos);

    auto equivalentRequest = request;
    equivalentRequest.targetSelection.target.name = "equivalent-target-alias";
    equivalentRequest.hostSelection.target.name = "equivalent-host-alias";
    const auto equivalent = ResolveComposition(equivalentRequest);
    REQUIRE(equivalent.Succeeded());
    REQUIRE(equivalent.graph->CompositionIdentity() == first.graph->CompositionIdentity());
    REQUIRE(equivalent.graph->CanonicalSerialization() == first.graph->CanonicalSerialization());

    auto changedRequest = request;
    changedRequest.targetSelection.configuration.name = "Release";
    const auto changed = ResolveComposition(changedRequest);
    REQUIRE(changed.Succeeded());
    const auto differences = DiffCompositionGraphs(*first.graph, *changed.graph);
    REQUIRE(std::ranges::any_of(differences, [](const GraphDifference &difference) {
        return difference.category == "selection" && difference.identity == "Selection" &&
               difference.change == "Changed";
    }));
    const auto explanation = ExplainCompositionIdentity(*first.graph, "App");
    REQUIRE(explanation.has_value());
    REQUIRE(explanation->category == "product");
    REQUIRE_FALSE(explanation->provenance[0].document.empty());
}

TEST_CASE("semantic resolver reaches a stable closure for package dependency cycles")
{
    TempDir temp{};
    WriteFile(temp.path() / "src/main.cpp", "");
    const auto projectPath = temp.path() / "CycleApp.nginproj";
    WriteFile(projectPath,
              R"xml(<Project Name="CycleApp" Type="Application"><Dependencies><Package Name="A" Exact="1.0.0" /></Dependencies></Project>)xml");
    const auto aPath = temp.path() / "A.nginpkg";
    const auto bPath = temp.path() / "B.nginpkg";
    WriteFile(aPath, R"xml(<Package Name="A" Version="1.0.0"><Requires><Package Name="B" Exact="1.0.0" /></Requires><Exports><Library Name="A" Default="true" /></Exports></Package>)xml");
    WriteFile(bPath, R"xml(<Package Name="B" Version="1.0.0"><Requires><Package Name="A" Exact="1.0.0" /></Requires><Exports><Library Name="B" Default="true" /></Exports></Package>)xml");
    DirectoryPackageProvider provider{"local", {Release(aPath, "A", "1.0.0"), Release(bPath, "B", "1.0.0")}};
    const auto resolved = ResolveComposition(SemanticResolutionRequest{
        .project = ParseProjectForResolution(projectPath),
        .projectDirectory = temp.path(),
        .workspaceRoot = temp.path(),
        .targetSelection = TargetSelection(),
        .hostSelection = HostSelection(),
        .packageProviders = {&provider}});
    REQUIRE(resolved.Succeeded());
    REQUIRE(resolved.graph->Data().packages.size() == 2);
    REQUIRE(std::ranges::count_if(resolved.graph->Data().edges, [](const GraphEdge &edge) {
        return edge.kind == "PackageRequirement";
    }) == 2);
}

TEST_CASE("capability binding activates an otherwise inactive implementation export")
{
    TempDir temp{};
    WriteFile(temp.path() / "src/main.cpp", "");
    const auto projectPath = temp.path() / "CapabilityApp.nginproj";
    WriteFile(projectPath, R"xml(<Project Name="CapabilityApp" Type="Application"><Dependencies>
  <Package Name="Consumer" Exact="1.0.0" />
  <Package Name="Security" Exact="1.0.0" />
</Dependencies></Project>)xml");
    const auto consumerPath = temp.path() / "Consumer.nginpkg";
    WriteFile(consumerPath, R"xml(<Package Name="Consumer" Version="1.0.0"><Requires>
  <Capability Name="Example.TLS" Domain="Link" Compatible="1" />
</Requires><Exports><Library Name="Consumer" Default="true" /></Exports></Package>)xml");
    const auto securityPath = temp.path() / "Security.nginpkg";
    WriteFile(securityPath, R"xml(<Package Name="Security" Version="1.0.0"><Exports>
  <Library Name="Crypto" Default="true" />
  <Library Name="TLS"><Provides><Capability Name="Example.TLS" Domain="Link" Version="1.2.0" /></Provides>
    <RuntimeFiles><File Include="bin/tls.so" Into="bin" /></RuntimeFiles>
  </Library>
</Exports></Package>)xml");
    DirectoryPackageProvider provider{
        "local", {Release(consumerPath, "Consumer", "1.0.0"), Release(securityPath, "Security", "1.0.0")}};
    const auto resolved = ResolveComposition(SemanticResolutionRequest{
        .project = ParseProjectForResolution(projectPath),
        .projectDirectory = temp.path(),
        .workspaceRoot = temp.path(),
        .targetSelection = TargetSelection(),
        .hostSelection = HostSelection(),
        .packageProviders = {&provider}});
    REQUIRE(resolved.Succeeded());
    REQUIRE(resolved.graph->Data().capabilities.size() == 1);
    REQUIRE(resolved.graph->Data().capabilities[0].binding.capability == "Example.TLS");
    REQUIRE(resolved.graph->Data().capabilities[0].binding.version == "1.2.0");
    REQUIRE_FALSE(resolved.graph->Data().capabilities[0].provenance.document.empty());
    REQUIRE(resolved.graph->Data().capabilities[0].provenance.line > 0);
    REQUIRE(std::ranges::any_of(resolved.graph->Data().exports, [](const GraphExport &item) {
        return item.name == "TLS";
    }));
    REQUIRE(std::ranges::any_of(resolved.graph->Data().contributions, [](const GraphContribution &item) {
        return item.owner.find("::TLS") != std::string::npos && item.destination == "bin";
    }));
}

TEST_CASE("resolved graph includes explicit host Actions Plugins generated items and provenance")
{
    TempDir temp{};
    WriteFile(temp.path() / "src/main.cpp", "");
    const auto projectPath = temp.path() / "Rich.nginproj";
    WriteFile(projectPath, R"xml(<Project Name="Rich" Type="Application"><Dependencies>
  <Package Name="Extensions" Exact="1.0.0"><Use Plugin="Telemetry" /></Package>
</Dependencies><Generate Action="Meta::Generate"><Input Include="include/**/*.hpp" /></Generate></Project>)xml");
    const auto extensionsPath = temp.path() / "Extensions.nginpkg";
    WriteFile(extensionsPath, R"xml(<Package Name="Extensions" Version="1.0.0"><Exports>
  <Plugin Name="Telemetry"><RuntimeFiles><File Include="plugins/telemetry.so" Into="plugins" /></RuntimeFiles></Plugin>
</Exports></Package>)xml");
    const auto metaPath = temp.path() / "Meta.nginpkg";
    WriteFile(metaPath, R"xml(<Package Name="Meta" Version="1.0.0"><Exports>
  <Tool Name="MetaGen" /><Action Name="Generate" Kind="Generate" Tool="MetaGen" Deterministic="true">
    <Outputs><Source Path="generated/meta.cpp" /></Outputs>
  </Action>
</Exports></Package>)xml");
    DirectoryPackageProvider provider{
        "local", {Release(extensionsPath, "Extensions", "1.0.0"), Release(metaPath, "Meta", "1.0.0")}};
    const auto resolved = ResolveComposition(SemanticResolutionRequest{
        .project = ParseProjectForResolution(projectPath),
        .projectDirectory = temp.path(),
        .workspaceRoot = temp.path(),
        .targetSelection = TargetSelection(),
        .hostSelection = HostSelection(),
        .packageProviders = {&provider}});
    REQUIRE(resolved.Succeeded());
    REQUIRE(resolved.graph->Data().actions.size() == 1);
    REQUIRE(resolved.graph->Data().plugins.size() == 1);
    REQUIRE(std::ranges::any_of(resolved.graph->Data().packages, [](const GraphPackageInstance &package) {
        return package.coordinate.name == "Meta" && package.context == PackageInstanceContext::Host;
    }));
    const auto dependencyLock = CreateDependencyLock(*resolved.graph);
    REQUIRE(std::ranges::any_of(dependencyLock.Data().packages, [](const DependencyLockEntry &package) {
        return package.coordinate.name == "Meta" && package.context == PackageInstanceContext::Host;
    }));
    REQUIRE(std::ranges::any_of(resolved.graph->Data().buildItems, [](const GraphBuildItem &item) {
        return item.path == "generated/meta.cpp" && item.generated;
    }));
    REQUIRE_FALSE(resolved.graph->Data().actions[0].provenance.document.empty());
    REQUIRE(resolved.graph->Data().actions[0].provenance.line > 0);
    REQUIRE_FALSE(resolved.graph->Data().selection.provenance.document.empty());
    REQUIRE(resolved.graph->Data().selection.provenance.line > 0);
}
