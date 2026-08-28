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
    WriteFile(projectPath, R"xml(<Executable Name="App"><Uses>
  <Package Name="Framework" Version="1" />
  <Package Name="Other" Version="1" />
</Uses></Executable>)xml");

    const auto base = [&](const std::string &version) {
        const auto path = temp.path() / ("Base-" + version + ".nginpkg");
        WriteFile(path, "<Package Name=\"Base\" Version=\"" + version +
                            "\"><Library Name=\"Core\" Default=\"true\" /></Package>");
        return Release(path, "Base", version);
    };
    const auto frameworkPath = temp.path() / "Framework.nginpkg";
    WriteFile(frameworkPath, R"xml(<Package Name="Framework" Version="1.0.0"><Uses>
  <Package Name="Base"><Version AtLeast="1.0.0" Before="2.0.0" /></Package>
</Uses><Library Name="Framework" /></Package>)xml");
    const auto otherPath = temp.path() / "Other.nginpkg";
    WriteFile(otherPath, R"xml(<Package Name="Other" Version="1.0.0"><Uses>
  <Package Name="Base"><Version AtLeast="1.5.0" Before="3.0.0" /></Package>
</Uses><Library Name="Other" /></Package>)xml");
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

TEST_CASE("semantic resolver preserves define values in the Composition Graph")
{
    TempDir temp{};
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }");
    const auto projectPath = temp.path() / "App.nginproj";
    WriteFile(projectPath, R"xml(<Executable Name="App"><Build>
  <Define Name="APP_VERSION" Value="&quot;1.2.3&quot;" Visibility="Private" />
</Build></Executable>)xml");

    const auto resolved = ResolveComposition(SemanticResolutionRequest{
        .project = ParseProjectForResolution(projectPath),
        .projectDirectory = temp.path(),
        .workspaceRoot = temp.path(),
        .targetSelection = TargetSelection(),
        .hostSelection = HostSelection()});
    REQUIRE(resolved.Succeeded());
    const auto define = std::ranges::find_if(resolved.graph->Data().buildItems, [](const GraphBuildItem &item) {
        return item.kind == "Define" && item.path == "APP_VERSION";
    });
    REQUIRE(define != resolved.graph->Data().buildItems.end());
    REQUIRE(define->value == std::optional<std::string>{"\"1.2.3\""});
    REQUIRE_THAT(resolved.graph->CanonicalSerialization(), ContainsSubstring("\"value\":\"\\\"1.2.3\\\"\""));

    WriteFile(projectPath, R"xml(<Executable Name="App"><Build>
  <Define Name="APP_VERSION" Value="&quot;2.0.0&quot;" Visibility="Private" />
</Build></Executable>)xml");
    const auto changed = ResolveComposition(SemanticResolutionRequest{
        .project = ParseProjectForResolution(projectPath),
        .projectDirectory = temp.path(),
        .workspaceRoot = temp.path(),
        .targetSelection = TargetSelection(),
        .hostSelection = HostSelection()});
    REQUIRE(changed.Succeeded());
    const auto differences = DiffCompositionGraphs(*resolved.graph, *changed.graph);
    REQUIRE(std::ranges::any_of(differences, [](const GraphDifference &difference) {
        return difference.category == "buildItem" && difference.identity == "Define:APP_VERSION:0" &&
               difference.change == "Changed";
    }));
}

TEST_CASE("semantic resolver reaches a stable closure for package dependency cycles")
{
    TempDir temp{};
    WriteFile(temp.path() / "src/main.cpp", "");
    const auto projectPath = temp.path() / "CycleApp.nginproj";
    WriteFile(projectPath,
              R"xml(<Executable Name="CycleApp"><Uses><Package Name="A" Exact="1.0.0" /></Uses></Executable>)xml");
    const auto aPath = temp.path() / "A.nginpkg";
    const auto bPath = temp.path() / "B.nginpkg";
    WriteFile(aPath, R"xml(<Package Name="A" Version="1.0.0"><Uses><Package Name="B" Exact="1.0.0" /></Uses><Library Name="A" /></Package>)xml");
    WriteFile(bPath, R"xml(<Package Name="B" Version="1.0.0"><Uses><Package Name="A" Exact="1.0.0" /></Uses><Library Name="B" /></Package>)xml");
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
    WriteFile(projectPath, R"xml(<Executable Name="CapabilityApp"><Uses>
  <Package Name="Consumer" Exact="1.0.0" />
  <Package Name="Security" Exact="1.0.0" />
</Uses></Executable>)xml");
    const auto consumerPath = temp.path() / "Consumer.nginpkg";
    WriteFile(consumerPath, R"xml(<Package Name="Consumer" Version="1.0.0"><Uses>
  <Capability Name="Example.TLS" Domain="Link" Version="1" />
</Uses><Library Name="Consumer" /></Package>)xml");
    const auto securityPath = temp.path() / "Security.nginpkg";
    WriteFile(securityPath, R"xml(<Package Name="Security" Version="1.0.0">
  <Library Name="Crypto" Default="true" />
  <Library Name="TLS"><Provides Name="Example.TLS" Domain="Link" Version="1.2.0" />
    <RuntimeFiles><File From="bin/tls.so" To="bin" /></RuntimeFiles>
  </Library>
</Package>)xml");
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
    WriteFile(projectPath, R"xml(<Executable Name="Rich"><Uses>
  <Package Name="Extensions" Exact="1.0.0"><Plugin Name="Telemetry" /></Package>
</Uses><Generate Using="Meta/Generate"><Header Include="include/**/*.hpp" /></Generate></Executable>)xml");
    const auto extensionsPath = temp.path() / "Extensions.nginpkg";
    WriteFile(extensionsPath, R"xml(<Package Name="Extensions" Version="1.0.0">
  <Plugin Name="Telemetry"><RuntimeFiles><File From="plugins/telemetry.so" To="plugins" /></RuntimeFiles></Plugin>
</Package>)xml");
    const auto metaPath = temp.path() / "Meta.nginpkg";
    WriteFile(metaPath, R"xml(<Package Name="Meta" Version="1.0.0">
  <Tool Name="MetaGen" /><Generator Name="Generate" Tool="MetaGen" Deterministic="true">
    <Outputs><Source Path="generated/meta.cpp" /></Outputs>
  </Generator>
</Package>)xml");
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

TEST_CASE("CPS imports compiled components and package overlays attach capabilities")
{
    TempDir temp{};
    const auto cpsPath = temp.path() / "Portable.cps";
    WriteFile(cpsPath, R"json({
  "cps_version": "0.15.0",
  "name": "Portable",
  "version": "1.2.3",
  "prefix": "/opt/portable",
  "default_components": ["Core"],
  "requires": {"Threads": {"components": ["Threads"]}},
  "components": {
    "Core": {"type": "archive", "location": "lib/libportable.a"},
    "Plugin": {"type": "module", "location": "lib/portable-plugin.so", "requires": [":Core"]},
    "Tool": {"type": "executable", "location": "bin/portable-tool"}
  }
})json");
    const auto overlayPath = temp.path() / "Portable.nginpkg";
    WriteFile(overlayPath, R"xml(<Package Name="Portable" Version="1.2.3">
  <Import Cps="Portable.cps" />
  <Capabilities><Provide Name="Example.Portable" Version="1" Component="Portable:Core" /></Capabilities>
</Package>)xml");
    const auto authored = ParseAuthoredManifest(overlayPath);
    REQUIRE(authored.Succeeded());
    const auto semantic = ParseSemanticPackage(std::get<AuthoredPackageManifest>(*authored.value));
    REQUIRE(semantic.Succeeded());
    REQUIRE(semantic.value->exports.size() == 3);
    CHECK(semantic.value->exports.at("Core").kind == ExportUseKind::Library);
    CHECK(semantic.value->exports.at("Core").defaultExport);
    CHECK(semantic.value->exports.at("Plugin").kind == ExportUseKind::Plugin);
    CHECK(semantic.value->exports.at("Tool").kind == ExportUseKind::Tool);
    REQUIRE(semantic.value->exports.at("Core").cps.has_value());
    CHECK(semantic.value->exports.at("Core").cps->type == "archive");
    REQUIRE(semantic.value->exports.at("Core").cps->location.has_value());
    CHECK(*semantic.value->exports.at("Core").cps->location == (temp.path() / "lib/libportable.a").generic_string());
    REQUIRE(semantic.value->exports.at("Core").capabilities.size() == 1);
    CHECK(semantic.value->exports.at("Core").capabilities.front().name == "Example.Portable");
    REQUIRE(std::ranges::any_of(semantic.value->requirements, [](const SemanticRequirement &requirement) {
        const auto *package = std::get_if<SemanticPackageRequirement>(&requirement);
        return package != nullptr && package->name == "Threads";
    }));
}
