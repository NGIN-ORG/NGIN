#include "DeploymentPlans.hpp"
#include "ProjectModel.hpp"
#include "SemanticResolver.hpp"
#include "TestSupport.hpp"

namespace
{
    [[nodiscard]] auto DeploymentGraph(const fs::path &projectManifest) -> ResolvedCompositionGraph
    {
        CompositionGraphData data{};
        data.product = GraphProduct{.identity = "Gallery",
                                    .name = "Gallery",
                                    .type = ProductType::Application,
                                    .version = "1.4.0",
                                    .license = "MIT"};
        data.selection = GraphSelection{.configuration = "Debug",
                                        .targetOperatingSystem = "linux",
                                        .targetArchitecture = "x64",
                                        .compiler = "clang",
                                        .compilerVersion = "19",
                                        .runtimeLibrary = "libc++"};
        data.packages.push_back(GraphPackageInstance{.identity = "runtime-instance",
                                                     .coordinate = PackageCoordinate{.name = "Runtime",
                                                                                     .exactVersion = "1.0.0"}});
        data.plugins.push_back(GraphPlugin{.identity = "runtime-instance::Telemetry",
                                           .packageInstance = "runtime-instance",
                                           .exportName = "Telemetry"});
        data.contributions.push_back(GraphContribution{.identity = "project-config",
                                                        .owner = "Gallery",
                                                        .kind = "ProjectFile",
                                                        .include = "config/app.cfg",
                                                        .destination = "config/app.cfg",
                                                        .provenance = GraphProvenance{.document = projectManifest.generic_string(),
                                                                                     .reason = "authored project stage input"}});
        data.contributions.push_back(GraphContribution{.identity = "runtime-file",
                                                        .owner = "runtime-instance::Core",
                                                        .kind = "RuntimeFile",
                                                        .include = "bin/runtime.so",
                                                        .destination = "bin",
                                                        .provenance = GraphProvenance{.reason = "active Library runtime file"}});
        data.contributions.push_back(GraphContribution{.identity = "runtime-notices",
                                                        .owner = "runtime-instance::Core",
                                                        .kind = "Notice",
                                                        .include = "LICENSES/**",
                                                        .destination = "notices/Runtime",
                                                        .provenance = GraphProvenance{.reason = "package legal notice"}});
        data.launches.push_back(GraphLaunch{.identity = "Gallery:Launch:Development",
                                            .name = "Development",
                                            .defaultLaunch = true,
                                            .executableKind = "Product",
                                            .executable = "Gallery",
                                            .workingDirectory = ".",
                                            .arguments = {"--asset", "one", "--asset", "two"},
                                            .environment = {{"LD_LIBRARY_PATH", "/custom/lib"}, {"LOG_LEVEL", "debug"}},
                                            .secrets = {{"API_TOKEN", "gallery/token"}}});
        data.testing = GraphTesting{.identity = "Gallery:Testing",
                                    .arguments = {"--reporter", "console"},
                                    .timeoutSeconds = 60};
        data.publishes.push_back(GraphPublish{.identity = "Gallery:Publish:portable",
                                              .name = "portable",
                                              .outputKind = "Archive",
                                              .format = "zip",
                                              .output = "dist/${project.name}-${project.version}.zip"});
        data.edges.push_back(GraphEdge{.identity = "test-dependency",
                                       .from = "Gallery",
                                       .to = "catch-instance",
                                       .kind = "ProjectDependency",
                                       .context = "Test"});
        data.edges.push_back(GraphEdge{.identity = "publish-dependency",
                                       .from = "Gallery",
                                       .to = "packager-instance",
                                       .kind = "ProjectDependency",
                                       .context = "Publish",
                                       .scope = "portable"});
        return ResolvedCompositionGraph{std::move(data)};
    }
}

TEST_CASE("project semantic model captures direct Stage Launch Testing and Publish intent")
{
    const auto authored = ParseAuthoredManifestText(R"xml(<Project Name="Gallery" Type="Application" Version="1.4.0">
  <Metadata><License>MIT</License></Metadata>
  <Stage><File Include="config/app.cfg" Into="config/app.cfg" /></Stage>
  <Launch Name="Development" Default="true">
    <Argument>--asset</Argument><Argument>one</Argument>
    <Environment Name="LOG_LEVEL" Value="debug" />
    <Secret Name="API_TOKEN" From="gallery/token" />
  </Launch>
  <Testing><Argument>--reporter</Argument><Argument>console</Argument><Timeout Seconds="60" /></Testing>
  <Publish Name="portable"><Archive Format="zip" Output="dist/${project.name}-${project.version}.zip" /></Publish>
</Project>)xml", "Gallery.nginproj");
    INFO((authored.diagnostics.empty() ? "" : authored.diagnostics.front().message));
    REQUIRE(authored.Succeeded());
    const auto semantic = ParseSemanticProject(std::get<AuthoredProjectManifest>(*authored.value));
    INFO((semantic.diagnostics.empty() ? "" : semantic.diagnostics.front().message));
    REQUIRE(semantic.Succeeded());
    REQUIRE(semantic.value->stage.size() == 1);
    REQUIRE(semantic.value->launches.size() == 1);
    REQUIRE(semantic.value->launches[0].product == "Gallery");
    REQUIRE(semantic.value->launches[0].arguments == std::vector<std::string>{"--asset", "one"});
    REQUIRE(semantic.value->launches[0].secrets.at("API_TOKEN") == "gallery/token");
    REQUIRE(semantic.value->testing->timeoutSeconds == 60);
    REQUIRE(semantic.value->publishes[0].kind == PublishOutputKind::Archive);
}

TEST_CASE("resolver projects deployment intent into the backend-free graph")
{
    TempDir temp{};
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }");
    WriteFile(temp.path() / "config/app.cfg", "config");
    const auto projectPath = temp.path() / "Gallery.nginproj";
    WriteFile(projectPath, R"xml(<Project Name="Gallery" Type="Application" Version="1.4.0">
  <Build><Source Include="src/main.cpp" /></Build>
  <Stage><File Include="config/app.cfg" Into="config/app.cfg" /></Stage>
  <Launch Name="Development" Default="true"><Argument>--dev</Argument></Launch>
  <Testing><Timeout Seconds="30" /></Testing>
  <Publish Name="portable"><Archive Format="zip" Output="dist/Gallery.zip" /></Publish>
</Project>)xml");
    const auto authored = ParseAuthoredManifest(projectPath);
    REQUIRE(authored.Succeeded());
    const auto semantic = ParseSemanticProject(std::get<AuthoredProjectManifest>(*authored.value));
    REQUIRE(semantic.Succeeded());
    const SelectionFacts selection{.configuration = Configuration{.name = "Debug"},
                                   .target = Target{.name = "linux-x64", .operatingSystem = "linux",
                                                    .architecture = "x64"},
                                   .toolchain = Toolchain{.name = "clang", .compiler = "clang",
                                                          .compilerVersion = "19", .runtimeLibrary = "libc++"}};
    const auto resolved = ResolveComposition(SemanticResolutionRequest{.project = *semantic.value,
                                                                        .projectDirectory = temp.path(),
                                                                        .workspaceRoot = temp.path(),
                                                                        .targetSelection = selection,
                                                                        .hostSelection = selection});
    INFO((resolved.diagnostics.empty() ? "" : resolved.diagnostics.front().message));
    REQUIRE(resolved.Succeeded());
    REQUIRE(resolved.graph->Data().launches.size() == 1);
    REQUIRE(resolved.graph->Data().launches[0].executable == "Gallery");
    REQUIRE(resolved.graph->Data().testing->timeoutSeconds == 30);
    REQUIRE(resolved.graph->Data().publishes.size() == 1);
    REQUIRE(std::ranges::any_of(resolved.graph->Data().contributions,
                                [](const auto &value) { return value.kind == "ProjectFile"; }));
    REQUIRE(resolved.graph->CanonicalSerialization().find("CPACK_") == std::string::npos);
    const auto explained = ExplainCompositionIdentity(*resolved.graph, "Gallery:Launch:Development");
    REQUIRE(explained.has_value());
    REQUIRE(explained->category == "launch");
}

TEST_CASE("deployment plans preserve ownership repeated arguments environments and typed publish inputs")
{
    TempDir temp{};
    WriteFile(temp.path() / "project/config/app.cfg", "config");
    WriteFile(temp.path() / "package/bin/runtime.so", "runtime");
    WriteFile(temp.path() / "package/LICENSES/LICENSE.txt", "notice");
    WriteFile(temp.path() / "artifacts/Gallery", "executable");
    WriteFile(temp.path() / "artifacts/Telemetry.plugin", "plugin");
    WriteFile(temp.path() / "artifacts/Gallery.debug", "symbols");
    const auto graph = DeploymentGraph(temp.path() / "project/Gallery.nginproj");
    const StagePlanBindings bindings{.projectRoot = temp.path() / "project",
                                     .stageRoot = temp.path() / "stage",
                                     .packageRoots = {{"runtime-instance", temp.path() / "package"}},
                                     .productArtifacts = {{"Gallery", temp.path() / "artifacts/Gallery"}},
                                     .pluginArtifacts = {{"runtime-instance::Telemetry",
                                                          temp.path() / "artifacts/Telemetry.plugin"}},
                                     .symbolArtifacts = {{"Gallery", {temp.path() / "artifacts/Gallery.debug"}}}};
    const auto stage = DeriveStagePlan(graph, bindings);
    INFO((stage.diagnostics.empty() ? "" : stage.diagnostics.front().message));
    REQUIRE(stage.Succeeded());
    REQUIRE(stage.plan->items.size() == 6);
    REQUIRE(std::ranges::all_of(stage.plan->items, [](const auto &item) {
        return !item.owner.empty() && !item.reason.empty();
    }));
    REQUIRE(stage.plan->plan.identity == FingerprintStagePlan(*stage.plan));

    const auto launch = DeriveLaunchPlan(graph, *stage.plan, bindings);
    REQUIRE(launch.Succeeded());
    REQUIRE(launch.plan->arguments == std::vector<std::string>{"--asset", "one", "--asset", "two"});
    REQUIRE(launch.plan->environment.at("LD_LIBRARY_PATH").ends_with(":/custom/lib"));
    REQUIRE(launch.plan->environment.at("LOG_LEVEL") == "debug");
    REQUIRE(launch.plan->secretReferences.at("API_TOKEN") == "gallery/token");
    REQUIRE(std::ranges::none_of(launch.plan->prerequisites,
                                 [](const auto &value) { return value.find("Telemetry") != std::string::npos; }));
    REQUIRE(launch.plan->plan.identity == FingerprintLaunchPlan(*launch.plan));

    const auto test = DeriveTestPlan(graph, *stage.plan);
    REQUIRE(test.Succeeded());
    REQUIRE(test.plan->dependencyInstances == std::vector<std::string>{"catch-instance"});
    REQUIRE(test.plan->timeoutSeconds == 60);

    const auto publish = DerivePublishPlan(graph, *stage.plan, "portable");
    REQUIRE(publish.Succeeded());
    REQUIRE(publish.plan->output == "dist/Gallery-1.4.0.zip");
    REQUIRE(publish.plan->license == "MIT");
    REQUIRE(publish.plan->dependencyInstances == std::vector<std::string>{"packager-instance"});
    REQUIRE(std::ranges::any_of(publish.plan->inputs,
                                [](const auto &input) { return input.category == "Notice"; }));
    REQUIRE(std::ranges::any_of(publish.plan->inputs,
                                [](const auto &input) { return input.category == "Symbol"; }));
    REQUIRE_THAT(GenerateCPackConfiguration(*publish.plan), ContainsSubstring("CPACK_GENERATOR \"ZIP\""));
    REQUIRE(graph.CanonicalSerialization().find("CPACK_") == std::string::npos);

    const auto executed = ExecuteStagePlan(*stage.plan);
    INFO((executed.diagnostics.empty() ? "" : executed.diagnostics.front().message));
    REQUIRE(executed.Succeeded());
    REQUIRE(ReadFile(temp.path() / "stage/config/app.cfg") == "config");
    REQUIRE(ReadFile(temp.path() / "stage/bin/runtime.so") == "runtime");
    REQUIRE(ReadFile(temp.path() / "stage/notices/Runtime/LICENSE.txt") == "notice");
}

TEST_CASE("StagePlan rejects missing sources unsafe destinations and ownership collisions before execution")
{
    TempDir temp{};
    WriteFile(temp.path() / "project/config/a.cfg", "a");
    WriteFile(temp.path() / "project/config/b.cfg", "b");
    auto data = DeploymentGraph(temp.path() / "project/Gallery.nginproj").Data();
    data.plugins.clear();
    data.contributions.clear();
    data.contributions.push_back(GraphContribution{.identity = "a", .owner = "Gallery", .kind = "ProjectFile",
                                                    .include = "config/a.cfg", .destination = "config/app.cfg"});
    data.contributions.push_back(GraphContribution{.identity = "b", .owner = "Gallery", .kind = "ProjectFile",
                                                    .include = "config/b.cfg", .destination = "config/app.cfg"});
    data.contributions.push_back(GraphContribution{.identity = "missing", .owner = "Gallery", .kind = "ProjectFile",
                                                    .include = "missing.cfg", .destination = "missing.cfg"});
    const ResolvedCompositionGraph graph{std::move(data)};
    const auto plan = DeriveStagePlan(graph, StagePlanBindings{.projectRoot = temp.path() / "project",
                                                               .stageRoot = temp.path() / "stage"});
    REQUIRE_FALSE(plan.Succeeded());
    REQUIRE(std::ranges::any_of(plan.diagnostics, [](const auto &diagnostic) {
        return diagnostic.code == "NGIN7201";
    }));
    REQUIRE(std::ranges::any_of(plan.diagnostics, [](const auto &diagnostic) {
        return diagnostic.code == "NGIN7202";
    }));

    StagePlan unsafe{.stageRoot = (temp.path() / "stage").generic_string(),
                     .items = {StagePlanItem{.identity = "unsafe", .owner = "Gallery", .reason = "test",
                                            .source = (temp.path() / "project/config/a.cfg").generic_string(),
                                            .destination = "../escape.cfg"}}};
    const auto executed = ExecuteStagePlan(unsafe);
    REQUIRE_FALSE(executed.Succeeded());
    REQUIRE_FALSE(fs::exists(temp.path() / "escape.cfg"));
}

TEST_CASE("StagePlan applies only explicitly owned replacements")
{
    TempDir temp{};
    WriteFile(temp.path() / "one/common.dll", "one");
    WriteFile(temp.path() / "two/common.dll", "two");
    CompositionGraphData data{};
    data.product = GraphProduct{.identity = "Bundle", .name = "Bundle", .type = ProductType::External};
    data.selection = GraphSelection{.targetOperatingSystem = "windows", .targetArchitecture = "x64"};
    data.contributions = {
        GraphContribution{.identity = "one", .owner = "one-instance::Runtime", .kind = "RuntimeFile",
                          .include = "common.dll", .destination = "bin",
                          .provenance = GraphProvenance{.reason = "first provider"}},
        GraphContribution{.identity = "two", .owner = "two-instance::Runtime", .kind = "RuntimeFile",
                          .include = "common.dll", .destination = "bin",
                          .provenance = GraphProvenance{.reason = "selected replacement"}}};
    const ResolvedCompositionGraph graph{std::move(data)};
    const auto rejected = DeriveStagePlan(graph, StagePlanBindings{
        .projectRoot = temp.path(), .stageRoot = temp.path() / "stage",
        .packageRoots = {{"one-instance", temp.path() / "one"}, {"two-instance", temp.path() / "two"}}});
    REQUIRE_FALSE(rejected.Succeeded());

    const auto replaced = DeriveStagePlan(graph, StagePlanBindings{
        .projectRoot = temp.path(), .stageRoot = temp.path() / "stage",
        .packageRoots = {{"one-instance", temp.path() / "one"}, {"two-instance", temp.path() / "two"}},
        .allowedReplacements = {{"bin/common.dll", "two-instance::Runtime"}},
        .targetCaseInsensitive = true});
    REQUIRE(replaced.Succeeded());
    REQUIRE(replaced.plan->items.size() == 1);
    REQUIRE(replaced.plan->items[0].owner == "two-instance::Runtime");
    REQUIRE_THAT(replaced.plan->items[0].reason, ContainsSubstring("explicitly replaces"));
}
