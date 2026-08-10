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
    WriteFile(projectPath, R"xml(<Project Name="App" Type="Application"><Dependencies>
  <Package Name="Example" Exact="1.0.0" />
</Dependencies></Project>)xml");
    WriteFile(packagePath, R"xml(<Package xmlns:cmake="urn:ngin:integration:cmake" Name="Example" Version="1.0.0">
  <Options><Boolean Name="Shared" Default="false" Artifact="true" /></Options>
  <Exports><Library Name="Core" Default="true" /></Exports>
  <Integrations><cmake:AddSubdirectory Source=".">
    <cmake:MapOption Option="Shared" Cache="BUILD_SHARED_LIBS" True="ON" False="OFF" Artifact="true" />
    <cmake:Target Export="Core" Name="Example::Core" />
  </cmake:AddSubdirectory></Integrations>
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
        R"xml(<Project Name="FindApp" Type="Application"><Dependencies><Package Name="Crypto" Exact="1.0.0" /></Dependencies></Project>)xml");
    WriteFile(packagePath, R"xml(<Package xmlns:cmake="urn:ngin:integration:cmake" Name="Crypto" Version="1.0.0">
  <Exports><Library Name="TLS" Default="true" /></Exports>
  <Integrations><cmake:FindPackage Name="OpenSSL" Config="false" Required="true" Version="3.0.0">
    <cmake:Target Export="TLS" Name="OpenSSL::SSL" />
  </cmake:FindPackage></Integrations>
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
            R"xml(<Project Name="ManualApp" Type="Application"><Dependencies><Package Name="Manual" Exact="1.0.0" /></Dependencies></Project>)xml");
        WriteFile(packagePath, R"xml(<Package xmlns:cmake="urn:ngin:integration:cmake" Name="Manual" Version="1.0.0">
  <Exports><Library Name="Core" Default="true" /></Exports>
  <Integrations><cmake:Manual Source="."><cmake:Target Export="Core" Name="Manual::Core" />
    <cmake:Select><Target OS="linux" Architecture="x64" /><cmake:Cache Name="MANUAL_LINUX" Value="ON" Type="BOOL" /></cmake:Select>
  </cmake:Manual></Integrations>
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
            R"xml(<Project Name="IsolatedApp" Type="Application"><Dependencies><Package Name="Isolated" Exact="1.0.0" /></Dependencies></Project>)xml");
        WriteFile(packagePath, R"xml(<Package xmlns:cmake="urn:ngin:integration:cmake" Name="Isolated" Version="1.0.0">
  <Exports><Library Name="Core" Default="true" /></Exports>
  <Integrations><cmake:Isolated Source="."><cmake:Install /><cmake:FindPackage Name="Isolated" Config="true">
    <cmake:Target Export="Core" Name="Isolated::Core" />
  </cmake:FindPackage></cmake:Isolated></Integrations>
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
    WriteFile(projectPath, R"xml(<Project Name="Generated" Type="Application">
  <Generate Action="Meta::Generate"><Input Include="include/**/*.hpp" /></Generate>
</Project>)xml");
    WriteFile(packagePath, R"xml(<Package xmlns:cmake="urn:ngin:integration:cmake" Name="Meta" Version="1.0.0">
  <Exports><Tool Name="MetaGen" /><Action Name="Generate" Kind="Generate" Tool="MetaGen" Deterministic="true">
    <Argument>${ProjectDir}</Argument><Argument>${ActionContext}</Argument>
    <Outputs><Source Path="generated/meta.cpp" /></Outputs>
  </Action></Exports>
  <Integrations><cmake:AddSubdirectory Source="."><cmake:Target Export="MetaGen" Name="MetaGen" /></cmake:AddSubdirectory></Integrations>
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
        .identity = "Modules", .name = "Modules", .type = ProductType::Library, .linkage = LibraryLinkage::Static};
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

TEST_CASE("CMake adapter maps every generated product artifact explicitly")
{
    const std::vector<std::tuple<ProductType, LibraryLinkage, std::string>> cases{
        {ProductType::Application, LibraryLinkage::None, "Executable"},
        {ProductType::Tool, LibraryLinkage::None, "Executable"},
        {ProductType::Test, LibraryLinkage::None, "Executable"},
        {ProductType::Benchmark, LibraryLinkage::None, "Executable"},
        {ProductType::Library, LibraryLinkage::Static, "StaticLibrary"},
        {ProductType::Library, LibraryLinkage::Shared, "SharedLibrary"},
        {ProductType::Library, LibraryLinkage::Interface, "InterfaceLibrary"},
        {ProductType::Plugin, LibraryLinkage::None, "ModuleLibrary"},
    };
    for (const auto &[type, linkage, expected] : cases)
    {
        CompositionGraphData data{};
        data.product = GraphProduct{.identity = "Product", .name = "Product", .type = type, .linkage = linkage};
        const ResolvedCompositionGraph graph{std::move(data)};
        const auto plans = DeriveCMakePlans(graph, ResolvedCMakeIntegrationBindings{});
        CAPTURE(expected);
        REQUIRE(plans.Succeeded());
        REQUIRE(plans.build->targetKind == expected);
    }
}
