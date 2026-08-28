#include "CMakeAdapter.hpp"
#include "SemanticResolver.hpp"
#include "TestSupport.hpp"

#include <tuple>
#include <type_traits>
#include <utility>

static_assert(std::is_const_v<
              std::remove_reference_t<decltype(std::declval<const ResolvedCMakeIntegrationBindings &>().Data())>>);

namespace
{
    [[nodiscard]] auto Selection() -> SelectionFacts
    {
        return SelectionFacts{.configuration = Configuration{.name = "Debug"},
                              .target = Target{.name = "linux-x64", .operatingSystem = "linux", .architecture = "x64"},
                              .toolchain = Toolchain{.name = "clang",
                                                     .compiler = "clang",
                                                     .compilerVersion = "19",
                                                     .runtimeLibrary = "libc++",
                                                     .linker = "lld"}};
    }

    [[nodiscard]] auto ParseProject(const fs::path &path) -> SemanticProject
    {
        const auto authored = ParseAuthoredManifest(path);
        REQUIRE(authored.Succeeded());
        const auto semantic = ParseSemanticProject(std::get<AuthoredProjectManifest>(*authored.value));
        REQUIRE(semantic.Succeeded());
        return *semantic.value;
    }

    [[nodiscard]] auto Resolve(const fs::path &root, const fs::path &projectPath, const fs::path &packagePath,
                               const std::string &name) -> SemanticResolutionResult
    {
        DirectoryPackageProvider provider{"local",
                                          {DirectoryPackageRelease{.name = name,
                                                                   .manifest = packagePath,
                                                                   .root = packagePath.parent_path(),
                                                                   .nativeIdentity = "local/" + name + "@1.0.0",
                                                                   .revision = "revision-1.0.0",
                                                                   .integrity = "sha256:" + name}}};
        auto host = Selection();
        host.target.name = "host";
        return ResolveComposition(SemanticResolutionRequest{.project = ParseProject(projectPath),
                                                            .projectDirectory = root,
                                                            .workspaceRoot = root,
                                                            .targetSelection = Selection(),
                                                            .hostSelection = host,
                                                            .packageProviders = {&provider}});
    }

    [[nodiscard]] auto Diagnostics(const SemanticResolutionResult &result) -> std::string
    {
        std::string text{};
        for (const auto &diagnostic : result.diagnostics) text += diagnostic.code + ": " + diagnostic.message + "\n";
        return text;
    }
}

TEST_CASE("CMake AddSubdirectory bindings stay outside the semantic graph and derive deterministic plans")
{
    TempDir temp{};
    WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }");
    WriteFile(temp.path() / "package/CMakeLists.txt", "add_library(Example::Core INTERFACE IMPORTED GLOBAL)\n");
    const auto projectPath = temp.path() / "App.nginproj";
    const auto packagePath = temp.path() / "package/Example.nginpkg";
    WriteFile(projectPath, R"xml(<Executable Name="App"><Uses>
  <Package Name="Example" Exact="1.0.0" />
</Uses></Executable>)xml");
    WriteFile(packagePath, R"xml(<Package xmlns:cmake="urn:ngin:adapter:cmake" Name="Example" Version="1.0.0">
  <Options><Boolean Name="Shared" Default="false" Artifact="true" /></Options>
  <Library Name="Core" Default="true" />
  <Adapters><cmake:AddSubdirectory Source=".">
    <cmake:MapOption Option="Shared" Cache="BUILD_SHARED_LIBS" True="ON" False="OFF" Artifact="true" />
    <cmake:Target Export="Core" Name="Example::Core" />
  </cmake:AddSubdirectory></Adapters>
</Package>)xml");

    const auto resolved = Resolve(temp.path(), projectPath, packagePath, "Example");
    INFO(Diagnostics(resolved));
    REQUIRE(resolved.Succeeded());
    REQUIRE(resolved.cmakeIntegrations.Data().size() == 1);
    REQUIRE(resolved.cmakeIntegrations.Data()[0].kind == CMakeIntegrationKind::AddSubdirectory);
    REQUIRE(resolved.cmakeIntegrations.Data()[0].cache.size() == 1);
    REQUIRE(resolved.cmakeIntegrations.Data()[0].cache[0].name == "BUILD_SHARED_LIBS");
    REQUIRE(resolved.cmakeIntegrations.Data()[0].cache[0].value == "OFF");
    REQUIRE(resolved.graph->CanonicalSerialization().find("CMake") == std::string::npos);
    REQUIRE(resolved.graph->CanonicalSerialization().find("Example::Core") == std::string::npos);

    const auto plans = DeriveCMakePlans(*resolved.graph, resolved.cmakeIntegrations,
                                        CMakeAdapterContext{.generator = "Ninja",
                                                            .toolchainFile = "cmake/toolchains/linux-clang.cmake",
                                                            .crossCompiling = true});
    INFO((plans.diagnostics.empty() ? std::string{} : plans.diagnostics[0].code + ": " + plans.diagnostics[0].message));
    REQUIRE(plans.Succeeded());
    REQUIRE(plans.build->plan.identity.starts_with("sha256:"));
    REQUIRE(plans.build->plan.identity == FingerprintBuildPlan(*plans.build));
    REQUIRE(plans.actions->plan.identity == FingerprintActionPlan(*plans.actions));
    REQUIRE(plans.build->links.size() == 1);
    REQUIRE(plans.build->links[0].targetName == "Example::Core");
    REQUIRE(plans.build->packages.size() == 1);
    const auto serialization = SerializeBuildPlan(*plans.build);
    REQUIRE(serialization == SerializeBuildPlan(*plans.build));
    REQUIRE_THAT(serialization, ContainsSubstring("\"generator\":\"Ninja\""));
    REQUIRE_THAT(serialization, ContainsSubstring("linux-clang.cmake"));
    const auto cmake = GenerateCMakeProject(*plans.build, *plans.actions);
    REQUIRE_THAT(cmake, ContainsSubstring("add_subdirectory("));
    REQUIRE_THAT(cmake, ContainsSubstring("target_link_libraries(App PRIVATE Example::Core)"));
    REQUIRE_THAT(cmake, ContainsSubstring("set(BUILD_SHARED_LIBS \"OFF\" CACHE STRING"));
    REQUIRE_THAT(cmake, ContainsSubstring("PROPERTY CXX_STANDARD 23"));
}

TEST_CASE("CMake FindPackage integration maps exact semantic Exports")
{
    TempDir temp{};
    WriteFile(temp.path() / "src/main.cpp", "");
    const auto projectPath = temp.path() / "FindApp.nginproj";
    const auto packagePath = temp.path() / "Crypto.nginpkg";
    WriteFile(
        projectPath,
        R"xml(<Executable Name="FindApp"><Uses><Package Name="Crypto" Exact="1.0.0" /></Uses></Executable>)xml");
    WriteFile(packagePath, R"xml(<Package xmlns:cmake="urn:ngin:adapter:cmake" Name="Crypto" Version="1.0.0">
  <Library Name="TLS" Default="true" />
  <Adapters><cmake:FindPackage Name="OpenSSL" Config="false" Required="true" Version="3.0.0">
    <cmake:Target Export="TLS" Name="OpenSSL::SSL" />
  </cmake:FindPackage></Adapters>
</Package>)xml");
    const auto resolved = Resolve(temp.path(), projectPath, packagePath, "Crypto");
    INFO(Diagnostics(resolved));
    REQUIRE(resolved.Succeeded());
    REQUIRE(resolved.cmakeIntegrations.Data()[0].kind == CMakeIntegrationKind::FindPackage);
    REQUIRE(resolved.cmakeIntegrations.Data()[0].findPackage->name == "OpenSSL");
    const auto plans = DeriveCMakePlans(*resolved.graph, resolved.cmakeIntegrations);
    INFO((plans.diagnostics.empty() ? std::string{} : plans.diagnostics[0].code + ": " + plans.diagnostics[0].message));
    REQUIRE(plans.Succeeded());
    REQUIRE_THAT(GenerateCMakeProject(*plans.build, *plans.actions),
                 ContainsSubstring("find_package(OpenSSL 3.0.0 REQUIRED)"));
}

TEST_CASE("CPS components become portable CMake imported targets")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "CpsApp.nginproj";
    const auto packagePath = temp.path() / "portable/Portable.nginpkg";
    WriteFile(projectPath,
              R"xml(<Executable Name="CpsApp"><Uses><Package Name="Portable" Exact="1.0.0"><Library Name="Core" /><Tool Name="Compiler" /><Plugin Name="Extension" /></Package></Uses></Executable>)xml");
    WriteFile(packagePath, R"xml(<Package Name="Portable" Version="1.0.0">
  <Import Cps="Portable.cps" />
</Package>)xml");
    WriteFile(temp.path() / "portable/Portable.cps", R"json({
  "cps_version": "0.15.0",
  "name": "Portable",
  "version": "1.0.0",
  "cps_path": "@prefix@/portable",
  "default_components": ["Core"],
  "components": {
    "Core": {
      "type": "interface",
      "includes": ["@prefix@/include"],
      "definitions": {"*": {"PORTABLE_CPS": "1"}}
    },
    "Compiler": {"type": "executable", "location": "@prefix@/bin/compiler"},
    "Extension": {"type": "module", "location": "@prefix@/lib/extension.so"}
  }
})json");

    const auto resolved = Resolve(temp.path(), projectPath, packagePath, "Portable");
    INFO(Diagnostics(resolved));
    REQUIRE(resolved.Succeeded());
    REQUIRE(resolved.cmakeIntegrations.Data().size() == 2);
    REQUIRE(std::ranges::all_of(resolved.cmakeIntegrations.Data(),
                                [](const auto &binding) { return binding.kind == CMakeIntegrationKind::Cps; }));
    const auto plans = DeriveCMakePlans(*resolved.graph, resolved.cmakeIntegrations);
    REQUIRE(plans.Succeeded());
    const auto generated = GenerateCMakeProject(*plans.build, *plans.actions);
    CHECK_THAT(generated, ContainsSubstring("add_library(Portable::Core INTERFACE IMPORTED GLOBAL)"));
    CHECK_THAT(generated, ContainsSubstring("INTERFACE_INCLUDE_DIRECTORIES"));
    CHECK_THAT(generated, ContainsSubstring("PORTABLE_CPS=1"));
    CHECK_THAT(generated, ContainsSubstring("add_executable(Portable::Compiler IMPORTED GLOBAL)"));
    CHECK_THAT(generated, ContainsSubstring("add_library(Portable::Extension MODULE IMPORTED GLOBAL)"));
    CHECK_THAT(generated, ContainsSubstring("target_link_libraries(CpsApp PRIVATE Portable::Core)"));
}

TEST_CASE("CMake Manual Isolated and structured selection remain explicit adapter modes")
{
    SECTION("Manual")
    {
        TempDir temp{};
        WriteFile(temp.path() / "src/main.cpp", "");
        WriteFile(temp.path() / "CMakeLists.txt", "add_library(Manual::Core INTERFACE IMPORTED GLOBAL)\n");
        const auto projectPath = temp.path() / "ManualApp.nginproj";
        const auto packagePath = temp.path() / "Manual.nginpkg";
        WriteFile(
            projectPath,
            R"xml(<Executable Name="ManualApp"><Uses><Package Name="Manual" Exact="1.0.0" /></Uses></Executable>)xml");
        WriteFile(packagePath, R"xml(<Package xmlns:cmake="urn:ngin:adapter:cmake" Name="Manual" Version="1.0.0">
  <Library Name="Core" Default="true" />
  <Adapters><cmake:Manual Source="."><cmake:Target Export="Core" Name="Manual::Core" />
    <cmake:When OS="linux" Architecture="x64"><cmake:Cache Name="MANUAL_LINUX" Value="ON" Type="BOOL" /></cmake:When>
  </cmake:Manual></Adapters>
</Package>)xml");
        const auto resolved = Resolve(temp.path(), projectPath, packagePath, "Manual");
        INFO(Diagnostics(resolved));
        REQUIRE(resolved.Succeeded());
        REQUIRE(resolved.cmakeIntegrations.Data()[0].kind == CMakeIntegrationKind::Manual);
        REQUIRE(resolved.cmakeIntegrations.Data()[0].cache[0].name == "MANUAL_LINUX");
        const auto plans = DeriveCMakePlans(*resolved.graph, resolved.cmakeIntegrations);
        REQUIRE(plans.Succeeded());
        REQUIRE_THAT(GenerateCMakeProject(*plans.build, *plans.actions), ContainsSubstring("add_subdirectory("));
    }

    SECTION("Isolated")
    {
        TempDir temp{};
        WriteFile(temp.path() / "src/main.cpp", "");
        WriteFile(temp.path() / "CMakeLists.txt",
                  "add_library(IsolatedCore STATIC empty.cpp)\ninstall(TARGETS IsolatedCore EXPORT IsolatedTargets)\n");
        WriteFile(temp.path() / "empty.cpp", "");
        const auto projectPath = temp.path() / "IsolatedApp.nginproj";
        const auto packagePath = temp.path() / "Isolated.nginpkg";
        WriteFile(
            projectPath,
            R"xml(<Executable Name="IsolatedApp"><Uses><Package Name="Isolated" Exact="1.0.0" /></Uses></Executable>)xml");
        WriteFile(packagePath, R"xml(<Package xmlns:cmake="urn:ngin:adapter:cmake" Name="Isolated" Version="1.0.0">
  <Library Name="Core" Default="true" />
  <Adapters><cmake:Isolated Source="."><cmake:Install /><cmake:FindPackage Name="Isolated" Config="true">
    <cmake:Target Export="Core" Name="Isolated::Core" />
  </cmake:FindPackage></cmake:Isolated></Adapters>
</Package>)xml");
        const auto resolved = Resolve(temp.path(), projectPath, packagePath, "Isolated");
        INFO(Diagnostics(resolved));
        REQUIRE(resolved.Succeeded());
        REQUIRE(resolved.cmakeIntegrations.Data()[0].kind == CMakeIntegrationKind::Isolated);
        REQUIRE(resolved.cmakeIntegrations.Data()[0].installBeforeUse);
        REQUIRE(resolved.cmakeIntegrations.Data()[0].findPackage->name == "Isolated");
        const auto plans = DeriveCMakePlans(*resolved.graph, resolved.cmakeIntegrations);
        REQUIRE(plans.Succeeded());
        REQUIRE(plans.build->packages[0].installBeforeUse);
        REQUIRE(plans.build->packages[0].binaryDirectory.size() < 32);
        const auto generated = GenerateCMakeProject(*plans.build, *plans.actions);
        REQUIRE_THAT(generated, ContainsSubstring("list(PREPEND CMAKE_PREFIX_PATH"));
        REQUIRE_THAT(generated, ContainsSubstring("find_package(Isolated CONFIG REQUIRED)"));
    }
}

TEST_CASE("CMake ActionPlan binds selected Actions to host Tool targets")
{
    TempDir temp{};
    WriteFile(temp.path() / "src/main.cpp", "");
    WriteFile(temp.path() / "package/CMakeLists.txt", "add_executable(MetaGen IMPORTED GLOBAL)\n");
    const auto projectPath = temp.path() / "Generated.nginproj";
    const auto packagePath = temp.path() / "package/Meta.nginpkg";
    WriteFile(projectPath, R"xml(<Executable Name="Generated">
  <Generate Using="Meta/Generate"><Header Include="include/**/*.hpp" /></Generate>
</Executable>)xml");
    WriteFile(packagePath, R"xml(<Package xmlns:cmake="urn:ngin:adapter:cmake" Name="Meta" Version="1.0.0">
  <Tool Name="MetaGen" /><Generator Name="Generate" Tool="MetaGen" Deterministic="true">
    <Argument>${ProjectDir}</Argument><Argument>${ActionContext}</Argument>
    <Outputs><Source Path="generated/meta.cpp" /></Outputs>
  </Generator>
  <Adapters><cmake:AddSubdirectory Source="."><cmake:Target Export="MetaGen" Name="MetaGen" /></cmake:AddSubdirectory></Adapters>
</Package>)xml");
    const auto resolved = Resolve(temp.path(), projectPath, packagePath, "Meta");
    INFO(Diagnostics(resolved));
    REQUIRE(resolved.Succeeded());
    const auto packageIdentity = resolved.cmakeIntegrations.Data()[0].packageInstance;
    const auto package =
        std::ranges::find(resolved.graph->Data().packages, packageIdentity, &GraphPackageInstance::identity);
    REQUIRE(package != resolved.graph->Data().packages.end());
    REQUIRE(package->context == PackageInstanceContext::Host);
    const auto plans = DeriveCMakePlans(*resolved.graph, resolved.cmakeIntegrations,
                                        CMakeAdapterContext{.projectRoot = "/workspace/project",
                                                            .buildRoot = "/workspace/build",
                                                            .actionOutputRoot = "/workspace/actions",
                                                            .actionContextRoot = "/workspace/contexts"});
    INFO((plans.diagnostics.empty() ? std::string{} : plans.diagnostics[0].code + ": " + plans.diagnostics[0].message));
    REQUIRE(plans.Succeeded());
    REQUIRE(plans.actions->steps.size() == 1);
    REQUIRE(plans.actions->steps[0].toolTarget == "MetaGen");
    REQUIRE(plans.actions->steps[0].outputs == std::vector<std::string>{"/workspace/actions/generated/meta.cpp"});
    REQUIRE(plans.actions->steps[0].arguments[0] == "/workspace/project");
    REQUIRE(plans.actions->steps[0].arguments[1] == "/workspace/contexts/Meta__Generate.xml");
    const auto generated = GenerateCMakeProject(*plans.build, *plans.actions);
    REQUIRE_THAT(generated, ContainsSubstring("add_custom_command(OUTPUT \"/workspace/actions/generated/meta.cpp\""));
    REQUIRE_THAT(generated, ContainsSubstring("DEPENDS MetaGen"));
    REQUIRE_THAT(generated, ContainsSubstring("add_dependencies(Generated ngin_action_Meta__Generate)"));
}

TEST_CASE("CMake adapter rejects unsupported semantic capabilities explicitly")
{
    CompositionGraphData data{};
    data.product = GraphProduct{
        .identity = "Modules", .name = "Modules", .artifactKind = ProductArtifactKind::Library,
        .libraryKind = LibraryKind::Static};
    data.buildItems.push_back(GraphBuildItem{
        .identity = "CxxModule:src/core.cppm", .kind = "CxxModule", .path = "src/core.cppm", .visibility = "Public"});
    const ResolvedCompositionGraph graph{std::move(data)};
    const auto plans =
        DeriveCMakePlans(graph, ResolvedCMakeIntegrationBindings{},
                         CMakeAdapterContext{.capabilities = CMakeAdapterCapabilities{.cxxModules = false}});
    REQUIRE_FALSE(plans.Succeeded());
    REQUIRE_THAT(plans.diagnostics[0].message, ContainsSubstring("cannot represent C++ module"));

    const auto crossPlans =
        DeriveCMakePlans(graph, ResolvedCMakeIntegrationBindings{},
                         CMakeAdapterContext{.capabilities = CMakeAdapterCapabilities{.crossCompilation = false},
                                             .crossCompiling = true});
    REQUIRE_FALSE(crossPlans.Succeeded());
    REQUIRE(std::ranges::any_of(crossPlans.diagnostics, [](const ManifestDiagnostic &diagnostic) {
        return diagnostic.message.find("cross compilation") != std::string::npos;
    }));
}

TEST_CASE("CMake adapter emits valued preprocessor definitions")
{
    CompositionGraphData data{};
    data.product = GraphProduct{
        .identity = "Versioned", .name = "Versioned", .artifactKind = ProductArtifactKind::Executable};
    data.buildItems.push_back(GraphBuildItem{.identity = "Define:APP_VERSION",
                                             .kind = "Define",
                                             .path = "APP_VERSION",
                                             .value = "\"1.2.3\"",
                                             .visibility = "Private"});

    const ResolvedCompositionGraph graph{std::move(data)};
    const auto plans = DeriveCMakePlans(graph, ResolvedCMakeIntegrationBindings{});
    REQUIRE(plans.Succeeded());
    REQUIRE(plans.build->items[0].value == "APP_VERSION=\"1.2.3\"");
    REQUIRE_THAT(GenerateCMakeProject(*plans.build, *plans.actions),
                 ContainsSubstring("target_compile_definitions(Versioned PRIVATE \"APP_VERSION=\\\"1.2.3\\\"\")"));
}

TEST_CASE("CMake package integrations are generated dependency first")
{
    CompositionGraphData data{};
    data.product = GraphProduct{.identity = "App", .name = "App",
                                .artifactKind = ProductArtifactKind::Executable};
    data.packages = {
        GraphPackageInstance{.identity = "a-dependent", .coordinate = {.name = "Dependent", .exactVersion = "1.0.0"}},
        GraphPackageInstance{.identity = "z-dependency", .coordinate = {.name = "Dependency", .exactVersion = "1.0.0"}},
    };
    data.edges.push_back(GraphEdge{.identity = "requires",
                                   .from = "a-dependent",
                                   .to = "z-dependency",
                                   .kind = "PackageRequirement"});
    const ResolvedCompositionGraph graph{std::move(data)};
    const ResolvedCMakeIntegrationBindings bindings{{
        CMakeIntegrationBindings{.packageInstance = "a-dependent", .source = "/packages/dependent"},
        CMakeIntegrationBindings{.packageInstance = "z-dependency", .source = "/packages/dependency"},
    }};

    const auto plans = DeriveCMakePlans(graph, bindings);
    REQUIRE(plans.Succeeded());
    REQUIRE(plans.build->packages.size() == 2);
    CHECK(plans.build->packages[0].packageInstance == "z-dependency");
    CHECK(plans.build->packages[1].packageInstance == "a-dependent");
    const auto generated = GenerateCMakeProject(*plans.build, *plans.actions);
    CHECK(generated.find("/packages/dependency") < generated.find("/packages/dependent"));
}

TEST_CASE("CMake adapter maps every generated product artifact explicitly")
{
    const std::vector<std::tuple<ProductArtifactKind, LibraryKind, std::string>> cases{
        {ProductArtifactKind::Executable, LibraryKind::None, "Executable"},
        {ProductArtifactKind::Library, LibraryKind::Static, "StaticLibrary"},
        {ProductArtifactKind::Library, LibraryKind::Shared, "SharedLibrary"},
        {ProductArtifactKind::Library, LibraryKind::Interface, "InterfaceLibrary"},
        {ProductArtifactKind::Library, LibraryKind::Plugin, "ModuleLibrary"},
    };
    for (const auto &[artifactKind, libraryKind, expected] : cases)
    {
        CompositionGraphData data{};
        data.product = GraphProduct{.identity = "Product", .name = "Product", .artifactKind = artifactKind,
                                    .libraryKind = libraryKind};
        const ResolvedCompositionGraph graph{std::move(data)};
        const auto plans = DeriveCMakePlans(graph, ResolvedCMakeIntegrationBindings{});
        CAPTURE(expected);
        REQUIRE(plans.Succeeded());
        REQUIRE(plans.build->targetKind == expected);
    }
}
