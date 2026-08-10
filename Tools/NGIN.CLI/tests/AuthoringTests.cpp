#include "AuthoredManifest.hpp"
#include "CompositionBoundary.hpp"
#include "ManifestArtifacts.hpp"
#include "ManifestPaths.hpp"
#include "Overlay.hpp"
#include "Placeholders.hpp"
#include "ProjectModel.hpp"
#include "TestSupport.hpp"

namespace
{
  [[nodiscard]] auto ManifestDiagnosticCodes(
      const AuthoredManifestResult &result) -> std::vector<std::string> {
    std::vector<std::string> codes{};
    std::ranges::transform(
        result.diagnostics, std::back_inserter(codes),
        [](const ManifestDiagnostic &diagnostic) { return diagnostic.code; });
    return codes;
  }
}

TEST_CASE("ManifestSpec describes the only pre-release document grammar") {
  const auto &spec = CurrentManifestSpec();

  REQUIRE(spec.Documents().size() == 3);
  REQUIRE(spec.Namespaces().size() == 1);
  REQUIRE(spec.Namespaces()[0].uri == std::string(CMakeIntegrationNamespace));

  const auto &project = spec.Element("project.root");
  REQUIRE(std::ranges::none_of(project.attributes, [](const auto &attribute) {
    return attribute.name == "SchemaVersion" ||
           attribute.name == "DefaultProfile";
  }));
  const auto type = std::ranges::find(project.attributes, "Type",
                                      &ManifestAttributeSpec::name);
  REQUIRE(type != project.attributes.end());
  REQUIRE(std::ranges::find(type->allowedValues, "Module") ==
          type->allowedValues.end());
}

TEST_CASE("semantic package identity and backend bindings are separate types") {
  PackageCoordinate coordinate{
      .name = "Example",
      .versionConstraint = "Compatible:1",
      .sourceBinding = "local",
  };
  PackageProviderResult provider{
      .coordinate = coordinate,
      .provider = "Directory",
      .providerIdentity = "Packages/Example",
      .exactVersion = "1.2.0",
      .integrity = "sha256:example",
      .root = "Packages/Example",
      .hermetic = true,
  };
  PackageInstance instance{
      .providerResult = provider,
      .compatibility = BinaryCompatibility{.operatingSystem = "windows",
                                            .architecture = "x64"},
      .identity = "Example@1.2.0/windows-x64",
  };
  IntegrationBindings bindings = CMakeIntegrationBindings{
      .packageInstance = instance.identity,
      .mode = "FindPackage",
      .targets = {{.exportName = "Core", .targetName = "Example::Core"}},
  };

  REQUIRE(instance.providerResult.coordinate.name == "Example");
  REQUIRE(std::get<CMakeIntegrationBindings>(bindings).targets[0].exportName ==
          "Core");
}

TEST_CASE("source-located authored parser accepts all manifest model fixtures") {
  const auto fixtureRoot =
      RepoRoot() / "Tools/NGIN.CLI/tests/fixtures/manifest-model";
  const std::vector<fs::path> fixtures{
      "minimal-application.nginproj", "framework-application.nginproj",
      "telemetry-plugin.nginproj",    "multi-export.nginpkg",
      "actions.nginpkg",              "workspace.ngin",
  };

  for (const auto &fixture : fixtures) {
    CAPTURE(fixture);
    const auto parsed = ParseAuthoredManifest(fixtureRoot / fixture);
    const auto diagnostic =
        parsed.diagnostics.empty()
            ? std::string{}
            : parsed.diagnostics.front().code + ": " +
                  parsed.diagnostics.front().message;
    INFO(diagnostic);
    REQUIRE(parsed.Succeeded());
  }

  const auto parsed =
      ParseAuthoredManifest(fixtureRoot / "minimal-application.nginproj");
  const auto &project = std::get<AuthoredProjectManifest>(*parsed.value);
  REQUIRE(project.name == "Hello.Native");
  REQUIRE(project.type == "Application");
  REQUIRE(project.root.source.begin.line == 2);
  REQUIRE(project.root.Attribute("Name")->source.begin.line == 2);
  REQUIRE_FALSE(project.manifest.canonicalPath.empty());
}

TEST_CASE("structural diagnostics reject unknown core vocabulary at source") {
  const auto parsed = ParseAuthoredManifestText(
      R"xml(<Project Name="Example" Type="Application">
  <Features Enabled="true" />
</Project>)xml",
      "Unknown.nginproj");

  REQUIRE_FALSE(parsed.Succeeded());
  REQUIRE(ManifestDiagnosticCodes(parsed) == std::vector<std::string>{"NGIN1003"});
  REQUIRE(parsed.diagnostics[0].source.begin.line == 2);
  REQUIRE(parsed.diagnostics[0].source.begin.column == 3);
}

TEST_CASE("structural diagnostics reject unknown attributes and invalid scalars") {
  const auto parsed = ParseAuthoredManifestText(
      R"xml(<Project Name="Example" Type="Module" SchemaVersion="4" />)xml",
      "Unknown.nginproj");

  REQUIRE_FALSE(parsed.Succeeded());
  const auto codes = ManifestDiagnosticCodes(parsed);
  REQUIRE(std::ranges::count(codes, "NGIN1004") == 1);
  REQUIRE(std::ranges::count(codes, "NGIN1007") == 1);
}

TEST_CASE("parser cardinality matches generated XSD assertions") {
  const auto missing = ParseAuthoredManifestText(
      R"xml(<Package Name="Example" Version="1.0.0" />)xml",
      "Example.nginpkg");
  REQUIRE_FALSE(missing.Succeeded());
  REQUIRE(ManifestDiagnosticCodes(missing) ==
          std::vector<std::string>{"NGIN1006"});

  const auto duplicate = ParseAuthoredManifestText(
      R"xml(<Project Name="Example" Type="Application">
  <Metadata />
  <Metadata />
</Project>)xml",
      "Example.nginproj");
  REQUIRE_FALSE(duplicate.Succeeded());
  REQUIRE(ManifestDiagnosticCodes(duplicate) ==
          std::vector<std::string>{"NGIN1006"});
}

TEST_CASE("only registered integration namespaces are accepted") {
  const auto accepted = ParseAuthoredManifestText(
      R"xml(<Package xmlns:build="urn:ngin:integration:cmake" Name="Example" Version="1.0.0">
  <Exports><Library Name="Core" Default="true" /></Exports>
  <Integrations>
    <build:FindPackage Name="Example">
      <build:Target Export="Core" Name="Example::Core" />
    </build:FindPackage>
  </Integrations>
</Package>)xml",
      "Example.nginpkg");
  REQUIRE(accepted.Succeeded());

  const auto unknown = ParseAuthoredManifestText(
      R"xml(<Package xmlns:other="urn:example:other" Name="Example" Version="1.0.0">
  <Exports><Library Name="Core" /></Exports>
  <Integrations><other:Build /></Integrations>
</Package>)xml",
      "Example.nginpkg");
  REQUIRE_FALSE(unknown.Succeeded());
  const auto unknownCodes = ManifestDiagnosticCodes(unknown);
  REQUIRE(std::ranges::find(unknownCodes, "NGIN1002") != unknownCodes.end());

  const auto misplaced = ParseAuthoredManifestText(
      R"xml(<Package xmlns:cmake="urn:ngin:integration:cmake" Name="Example" Version="1.0.0">
  <Exports><Library Name="Core"><cmake:Target Export="Core" Name="Example::Core" /></Library></Exports>
</Package>)xml",
      "Example.nginpkg");
  REQUIRE_FALSE(misplaced.Succeeded());
  const auto misplacedCodes = ManifestDiagnosticCodes(misplaced);
  REQUIRE(std::ranges::find(misplacedCodes, "NGIN1009") !=
          misplacedCodes.end());
}

TEST_CASE("semantic validator hooks run after structural parsing") {
  bool called = false;
  AuthoredManifestParseOptions options{};
  options.semanticValidators.push_back(
      [&](const AuthoredManifest &, std::vector<ManifestDiagnostic> &diagnostics) {
        called = true;
        diagnostics.push_back(ManifestDiagnostic{
            .severity = ManifestDiagnosticSeverity::Warning,
            .code = "TEST2000",
            .message = "semantic hook called",
        });
      });

  const auto parsed = ParseAuthoredManifestText(
      R"xml(<Project Name="Example" Type="Application" />)xml",
      "Example.nginproj", options);
  REQUIRE(called);
  REQUIRE(parsed.Succeeded());
  REQUIRE(parsed.diagnostics.size() == 1);
  REQUIRE(parsed.diagnostics[0].code == "TEST2000");
}

TEST_CASE("generated manifest schemas and editor metadata have no drift") {
  const auto schemaRoot = RepoRoot() / "Tools/NGIN.VSCode/schemas";
  const auto generated = GenerateManifestArtifacts();

  REQUIRE(generated.size() == 5);
  for (const auto &[name, contents] : generated) {
    CAPTURE(name);
    REQUIRE(ReadFile(schemaRoot / name) == contents);
  }

  REQUIRE_THAT(generated.at("project.xsd"),
               ContainsSubstring("<xs:element name=\"Project\""));
  REQUIRE_THAT(generated.at("project.xsd"),
               ContainsSubstring("<xs:assert test=\"count(Metadata) le 1\""));
  REQUIRE_THAT(generated.at("package.xsd"),
               ContainsSubstring("<xs:assert test=\"count(Exports) ge 1\""));
  REQUIRE_THAT(generated.at("package.xsd"),
               ContainsSubstring("ref=\"cmake:FindPackage\""));
  REQUIRE_THAT(generated.at("cmake-integration.xsd"),
               ContainsSubstring("targetNamespace=\"urn:ngin:integration:cmake\""));
  REQUIRE_THAT(generated.at("manifest-editor-metadata.json"),
               ContainsSubstring("\"semanticValidator\""));
}

TEST_CASE("portable paths normalize to owner-relative semantic identities") {
  const auto normalized = NormalizePortablePath(
      "src/./ui/../main.cpp", PortablePathBase::Manifest);
  REQUIRE(normalized.Succeeded());
  REQUIRE(normalized.value->value == "src/main.cpp");
  const auto sibling =
      NormalizePortablePath("../sibling", PortablePathBase::Manifest);
  REQUIRE(sibling.Succeeded());
  REQUIRE(sibling.value->value == "../sibling");
  REQUIRE_FALSE(NormalizePortablePath("C:/secret", PortablePathBase::Manifest)
                    .Succeeded());
  REQUIRE_FALSE(NormalizePortablePath("src\\main.cpp",
                                      PortablePathBase::Manifest)
                    .Succeeded());
  REQUIRE_FALSE(NormalizeStageDestination("../../outside").Succeeded());

  TempDir temp{};
  const auto workspace = temp.path() / "workspace";
  const auto manifestDirectory = workspace / "App";
  WriteFile(workspace / "Sibling/value.txt", "value");
  fs::create_directories(manifestDirectory);
  const auto siblingFile = NormalizePortablePath(
      "../Sibling/value.txt", PortablePathBase::Manifest);
  const auto resolved = ResolvePortablePath(*siblingFile.value,
                                            manifestDirectory, workspace,
                                            workspace);
  REQUIRE(resolved.Succeeded());
  REQUIRE(resolved.value->base == PortablePathBase::Workspace);
  REQUIRE(resolved.value->value == "Sibling/value.txt");

  TempDir otherCheckout{};
  const auto otherWorkspace = otherCheckout.path() / "workspace";
  const auto otherManifest = otherWorkspace / "App";
  WriteFile(otherWorkspace / "Sibling/value.txt", "value");
  fs::create_directories(otherManifest);
  const auto otherResolved = ResolvePortablePath(
      *siblingFile.value, otherManifest, otherWorkspace, otherWorkspace);
  REQUIRE(otherResolved.Succeeded());
  REQUIRE(*otherResolved.value == *resolved.value);
}

TEST_CASE("portable glob expansion is deterministic and enforces case policy") {
  TempDir temp{};
  WriteFile(temp.path() / "src/z.cpp", "");
  WriteFile(temp.path() / "src/a.cpp", "");
  WriteFile(temp.path() / "src/nested/b.cpp", "");
  WriteFile(temp.path() / "include/a.hpp", "");

  const auto expanded =
      ExpandPortableGlob(temp.path(), "src/**/*.cpp", false);
  REQUIRE(expanded.Succeeded());
  REQUIRE(expanded.matches ==
          std::vector<PortablePath>{{.value = "src/a.cpp"},
                                    {.value = "src/nested/b.cpp"},
                                    {.value = "src/z.cpp"}});
  REQUIRE(GlobMatchesPortable("**/*.cpp", "main.cpp"));
  REQUIRE(GlobMatchesPortable("src/[ab].cpp", "src/a.cpp"));
  REQUIRE_FALSE(GlobMatchesPortable("src/**x.cpp", "src/x.cpp"));

  const std::vector<PortablePath> collision{
      {.value = "Assets/Icon.png"}, {.value = "assets/icon.png"}};
  REQUIRE(ValidateTargetPathCaseCollisions(collision, false).empty());
  const auto diagnostics = ValidateTargetPathCaseCollisions(collision, true);
  REQUIRE(diagnostics.size() == 1);
  REQUIRE(diagnostics[0].code == "NGIN2009");
}

TEST_CASE("resolved symlinks cannot escape an authored path boundary") {
  TempDir temp{};
  const auto root = temp.path() / "root";
  const auto outside = temp.path() / "outside";
  WriteFile(outside / "secret.txt", "secret");
  fs::create_directories(root);
  std::error_code error{};
  fs::create_directory_symlink(outside, root / "link", error);
  if (error) {
    WARN("symlink creation is unavailable in this environment: " << error.message());
  } else {
    const auto authored = NormalizePortablePath(
        "link/secret.txt", PortablePathBase::Manifest);
    REQUIRE(authored.Succeeded());
    REQUIRE_FALSE(ResolvePortablePath(*authored.value, root, root, root)
                      .Succeeded());
  }
}

TEST_CASE("typed placeholders enforce registry phases and canonical identity") {
  const std::map<std::string, PlaceholderValue, std::less<>> values{
      {"project.name", {PlaceholderType::Identifier, "Gallery"}},
      {"project.version", {PlaceholderType::SemanticVersion, "1.2.0"}},
      {"configuration", {PlaceholderType::Identifier, "Debug"}},
      {"target.os", {PlaceholderType::Identifier, "windows"}},
      {"target.architecture", {PlaceholderType::Identifier, "x64"}},
      {"output.name", {PlaceholderType::Filename, "Gallery.exe"}},
      {"workspace.root", {PlaceholderType::Path, "C:/work/NGIN"}},
  };
  const auto publish = ExpandPlaceholders(
      "dist/${project.name}-${project.version}.zip", PlaceholderPhase::Publish,
      values, true);
  REQUIRE(publish.Succeeded());
  REQUIRE(*publish.value == "dist/Gallery-1.2.0.zip");

  REQUIRE_FALSE(ExpandPlaceholders("${project.version}", PlaceholderPhase::Stage,
                                   values)
                    .Succeeded());
  REQUIRE_FALSE(
      ExpandPlaceholders("${unknown}", PlaceholderPhase::Output, values)
          .Succeeded());
  const auto local = ExpandPlaceholders("${workspace.root}",
                                        PlaceholderPhase::LocalExecution,
                                        values);
  REQUIRE(local.Succeeded());
  REQUIRE_FALSE(local.canonicalIdentity);
  REQUIRE_FALSE(ExpandPlaceholders("${workspace.root}",
                                   PlaceholderPhase::LocalExecution, values,
                                   true)
                    .Succeeded());

  auto recursiveValues = values;
  recursiveValues["project.name"].value = "${configuration}";
  REQUIRE_FALSE(ExpandPlaceholders("${project.name}", PlaceholderPhase::Output,
                                   recursiveValues)
                    .Succeeded());
}

TEST_CASE("project conventions discover deterministic C++ build inputs") {
  TempDir temp{};
  const auto projectPath = temp.path() / "Convention.nginproj";
  WriteFile(projectPath,
            R"(<Project Name="Convention" Type="Application" />)");
  WriteFile(temp.path() / "src/main.cpp", "int main() { return 0; }");
  WriteFile(temp.path() / "src/api.ixx", "export module api;");
  WriteFile(temp.path() / "include/api.hpp", "#pragma once");
  WriteFile(temp.path() / "assets/ui/icon.txt", "icon");
  WriteFile(temp.path() / "build/generated.cpp", "");

  const auto authored = ParseAuthoredManifest(projectPath);
  REQUIRE(authored.Succeeded());
  const auto project = ParseSemanticProject(
      std::get<AuthoredProjectManifest>(*authored.value));
  REQUIRE(project.Succeeded());
  const auto build = ResolveProjectBuild(*project.value, temp.path());
  REQUIRE(build.Succeeded());
  REQUIRE(build.language.standard == "C++23");
  REQUIRE(build.items.size() == 4);
  REQUIRE(std::ranges::is_sorted(build.items, {}, &ResolvedBuildItem::identity));
  REQUIRE(std::ranges::none_of(build.items, [](const ResolvedBuildItem &item) {
    return item.path.value.starts_with("build/");
  }));
  const auto resource = std::ranges::find_if(build.items, [](const ResolvedBuildItem &item) {
    return item.kind == BuildItemKind::Resource;
  });
  REQUIRE(resource != build.items.end());
  REQUIRE(resource->path.value == "assets/ui/icon.txt");
  REQUIRE(resource->destination->value == "assets/ui/icon.txt");
}

TEST_CASE("build Include Remove and Update laws do not depend on XML order") {
  TempDir temp{};
  WriteFile(temp.path() / "src/keep.cpp", "");
  WriteFile(temp.path() / "src/remove.cpp", "");
  WriteFile(temp.path() / "src/generated.cpp", "");
  WriteFile(temp.path() / "src/excluded.cpp", "");

  const auto resolve = [&](const std::string &name,
                           const std::string &items) {
    const auto path = temp.path() / (name + ".nginproj");
    WriteFile(path, "<Project Name=\"" + name +
                        "\" Type=\"Application\"><Build Conventions=\"false\">" +
                        items + "</Build></Project>");
    const auto authored = ParseAuthoredManifest(path);
    REQUIRE(authored.Succeeded());
    const auto project = ParseSemanticProject(
        std::get<AuthoredProjectManifest>(*authored.value));
    REQUIRE(project.Succeeded());
    return ResolveProjectBuild(*project.value, temp.path());
  };
  const std::string include =
      R"(<Source Include="src/**/*.cpp" Exclude="src/excluded.cpp" />)";
  const std::string remove = R"(<Source Remove="src/remove.cpp" />)";
  const std::string update =
      R"(<Source Update="src/generated.cpp" Generated="true" />)";
  const auto first = resolve("First", remove + update + include);
  const auto second = resolve("Second", include + update + remove);
  REQUIRE(first.Succeeded());
  REQUIRE(second.Succeeded());
  REQUIRE(first.items.size() == 2);
  REQUIRE(first.items.size() == second.items.size());
  for (std::size_t index = 0; index < first.items.size(); ++index) {
    REQUIRE(first.items[index].identity == second.items[index].identity);
    REQUIRE(first.items[index].generated == second.items[index].generated);
  }
  const auto generated = std::ranges::find_if(first.items, [](const auto &item) {
    return item.path.value == "src/generated.cpp";
  });
  REQUIRE(generated != first.items.end());
  REQUIRE(generated->generated);
  REQUIRE(generated->origin == BuildItemOriginKind::Updated);
}

TEST_CASE("ineffective build operations fail unless AllowEmpty is explicit") {
  TempDir temp{};
  const auto resolve = [&](const bool allowEmpty) {
    const auto path = temp.path() /
                      (allowEmpty ? "Allowed.nginproj" : "Rejected.nginproj");
    WriteFile(path,
              "<Project Name=\"Operations\" Type=\"Application\"><Build "
              "Conventions=\"false\"><Source Remove=\"src/missing.cpp\" "
              "AllowEmpty=\"" +
                  std::string(allowEmpty ? "true" : "false") +
                  "\" /></Build></Project>");
    const auto authored = ParseAuthoredManifest(path);
    REQUIRE(authored.Succeeded());
    const auto project = ParseSemanticProject(
        std::get<AuthoredProjectManifest>(*authored.value));
    REQUIRE(project.Succeeded());
    return ResolveProjectBuild(*project.value, temp.path());
  };
  REQUIRE_FALSE(resolve(false).Succeeded());
  REQUIRE(resolve(true).Succeeded());
}

TEST_CASE("build item identities reject incompatible duplicate contributions") {
  TempDir temp{};
  WriteFile(temp.path() / "src/main.cpp", "");
  const auto projectPath = temp.path() / "Duplicates.nginproj";
  WriteFile(projectPath, R"xml(<Project Name="Duplicates" Type="Application">
  <Build Conventions="false">
    <Source Include="src/main.cpp" />
    <Source Include="src/main.cpp" Generated="true" />
  </Build>
</Project>)xml");

  const auto authored = ParseAuthoredManifest(projectPath);
  REQUIRE(authored.Succeeded());
  const auto project = ParseSemanticProject(
      std::get<AuthoredProjectManifest>(*authored.value));
  REQUIRE(project.Succeeded());
  const auto build = ResolveProjectBuild(*project.value, temp.path());
  REQUIRE_FALSE(build.Succeeded());
  REQUIRE(std::ranges::any_of(build.diagnostics, [](const ManifestDiagnostic &diagnostic) {
    return diagnostic.code == "NGIN3004";
  }));
}

TEST_CASE("typed build settings remain backend-neutral and graph-ready") {
  TempDir temp{};
  const auto projectPath = temp.path() / "Library.nginproj";
  WriteFile(projectPath, R"xml(<Project Name="Example.Library" Type="Library" Linkage="Shared">
  <Build Conventions="false">
    <Language Standard="C++23" Extensions="false" Required="true" />
    <Header Include="generated/api.hpp" Visibility="Public" Generated="true" />
    <IncludeDirectory Path="include" Visibility="Public" System="false" />
    <Define Name="EXAMPLE_EXPORT" Value="1" Visibility="Public" />
    <CompileOption Value="-Wall" Visibility="Private" />
    <LinkOption Value="-Wl,--as-needed" Visibility="Private" />
    <PrecompiledHeader Path="include/pch.hpp" Visibility="Private" />
    <UnityBuild Enabled="true" BatchSize="8" />
  </Build>
</Project>)xml");
  fs::create_directories(temp.path() / "include");

  const auto authored = ParseAuthoredManifest(projectPath);
  REQUIRE(authored.Succeeded());
  const auto project = ParseSemanticProject(
      std::get<AuthoredProjectManifest>(*authored.value));
  REQUIRE(project.Succeeded());
  const auto build = ResolveProjectBuild(*project.value, temp.path());
  REQUIRE(build.Succeeded());
  REQUIRE(project.value->linkage == LibraryLinkage::Shared);
  REQUIRE(build.unityBuild->enabled);
  REQUIRE(build.unityBuild->batchSize == 8);
  REQUIRE(build.items.size() == 6);
  REQUIRE(std::ranges::any_of(build.items, [](const ResolvedBuildItem &item) {
    return item.kind == BuildItemKind::Define && item.detail == "EXAMPLE_EXPORT" &&
           item.value == "1" && item.visibility == BuildVisibility::Public;
  }));
}

TEST_CASE("Interface Library rejects compiled sources but accepts generated headers") {
  TempDir temp{};
  const auto projectPath = temp.path() / "Interface.nginproj";
  WriteFile(projectPath, R"xml(<Project Name="Example.Interface" Type="Library" Linkage="Interface">
  <Build Conventions="false">
    <Source Include="src/compiled.cpp" />
    <Header Include="generated/api.hpp" Generated="true" Visibility="Interface" />
  </Build>
</Project>)xml");
  WriteFile(temp.path() / "src/compiled.cpp", "");
  const auto authored = ParseAuthoredManifest(projectPath);
  REQUIRE(authored.Succeeded());
  const auto project = ParseSemanticProject(
      std::get<AuthoredProjectManifest>(*authored.value));
  REQUIRE(project.Succeeded());
  REQUIRE_FALSE(ResolveProjectBuild(*project.value, temp.path()).Succeeded());
}

TEST_CASE("host target platforms resolve to the detected host identity") {
  const PlatformIdentity windowsHost{
      .name = "windows-x64",
      .operatingSystem = "windows",
      .architecture = "x64",
  };
  const PlatformIdentity linuxHost{
      .name = "linux-x64",
      .operatingSystem = "linux",
      .architecture = "x64",
  };
  const std::span<const WorkspaceManifest::Platform> noWorkspacePlatforms{};

  REQUIRE(ResolvePlatformIdentity("host", noWorkspacePlatforms, windowsHost).name == "windows-x64");
  REQUIRE(ResolvePlatformIdentity("host", noWorkspacePlatforms, linuxHost).name == "linux-x64");
  REQUIRE(ResolvePlatformIdentity("linux-x64", noWorkspacePlatforms, windowsHost).name == "linux-x64");
  REQUIRE_THROWS_WITH(ResolvePlatformIdentity("unknown", noWorkspacePlatforms, linuxHost),
                      "unknown target platform 'unknown'");
}

TEST_CASE("workspace-defined target platforms retain their declared identity") {
  const std::vector<WorkspaceManifest::Platform> platforms{{
      .name = "desktop",
      .operatingSystem = "windows",
      .architecture = "x64",
  }};
  const PlatformIdentity linuxHost{
      .name = "linux-x64",
      .operatingSystem = "linux",
      .architecture = "x64",
  };

  const auto resolved = ResolvePlatformIdentity("desktop", platforms, linuxHost);
  REQUIRE(resolved.name == "desktop");
  REQUIRE(resolved.operatingSystem == "windows");
  REQUIRE(resolved.architecture == "x64");
}

TEST_CASE("CMake and self-hosted CLI manifest versions stay synchronized") {
  const auto project = LoadProjectManifest(
      RepoRoot() / "Tools/NGIN.CLI/NGIN.CLI.nginproj");
  const auto rootCMake = ReadFile(RepoRoot() / "CMakeLists.txt");

  REQUIRE_FALSE(project.version.empty());
  REQUIRE_THAT(rootCMake,
               ContainsSubstring("project(NGIN VERSION " + project.version +
                                 " LANGUAGES CXX)"));
  REQUIRE(project.output.name == "ngin");
  REQUIRE(project.output.target == "ngin");
}

TEST_CASE(
    "self-hosted CLI release profiles expose only matching publish targets") {
  const auto project =
      LoadProjectManifest(RepoRoot() / "Tools/NGIN.CLI/NGIN.CLI.nginproj");
  const auto publishNames = [&](const std::string &profileName) {
    const auto publishes =
        EffectivePublishes(project, ProfileByName(project, profileName));
    std::vector<std::string> names{};
    std::transform(
        publishes.begin(), publishes.end(), std::back_inserter(names),
        [](const PublishDefinition &publish) { return publish.name; });
    std::sort(names.begin(), names.end());
    return names;
  };

  const std::vector<std::string> linuxThin{"linux-deb", "linux-tgz"};
  const std::vector<std::string> linuxBundled{"linux-deb-bundled",
                                              "linux-tgz-bundled"};
  const std::vector<std::string> windowsThin{"windows-msi", "windows-zip"};
  const std::vector<std::string> windowsBundled{"windows-msi-bundled",
                                                "windows-zip-bundled"};

  REQUIRE(project.publishes.empty());
  REQUIRE(publishNames("Release.Linux") == linuxThin);
  REQUIRE(publishNames("Release.Linux.Bundled") == linuxBundled);
  REQUIRE(publishNames("Release.Windows") == windowsThin);
  REQUIRE(publishNames("Release.Windows.Bundled") == windowsBundled);
}

TEST_CASE("workspace, project, and package manifests parse through authoring "
          "facades") {
  TempDir temp{};
  WriteFile(temp.path() / "Workspace.ngin",
            R"(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="TempWorkspace" PlatformVersion="0.1.0">
  <Projects>
    <Project Path="App/App.nginproj" />
  </Projects>
  <Packages>
    <Source Name="local" Path="Packages" />
    <Version Name="Package.Core" Range=">=1.0.0 &lt;2.0.0" />
  </Packages>
</Workspace>
)");
  WriteFile(temp.path() / "Packages/Sample/Sample.nginpkg",
            R"(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Sample.Package" Version="1.0.0">
  <Build Backend="CMake" Mode="Manual" />
  <Library Name="Sample.Package">
    <Exports>
      <LibraryTarget Name="Sample::Package" />
    </Exports>
  </Library>
</Package>
)");
  WriteFile(temp.path() / "App/App.nginproj",
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Sample.App" DefaultProfile="Runtime">
  <Application>
    <Uses>
      <Package Name="Sample.Package" Version=">=1.0.0 &lt;2.0.0" Scope="Target" />
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Launch Executable="$(OutputName)" WorkingDirectory="." />
  </Application>
  <Profile Name="Runtime">
    <Defaults>
      <Optimization Mode="Off" />
      <DebugSymbols Enabled="true" />
      <LinkTimeOptimization Enabled="false" />
      <TargetPlatform Name="linux-x64" />
      <Environment Name="dev" />
    </Defaults>
  </Profile>
</Project>
)xml");
  WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");

  const auto workspace = LoadWorkspaceManifest(temp.path());
  const auto project = LoadProjectManifest(temp.path() / "App/App.nginproj");
  const auto package =
      LoadPackageManifest(temp.path() / "Packages/Sample/Sample.nginpkg");
  const auto catalog = LoadPackageCatalog(workspace, project.path);

  REQUIRE(workspace.name == "TempWorkspace");
  REQUIRE(project.name == "Sample.App");
  REQUIRE(package.name == "Sample.Package");
  REQUIRE(package.modules.empty());
  REQUIRE(catalog.contains("Sample.Package"));
}

TEST_CASE("conditions support When nodes and item selectors") {
  TempDir temp{};
  const auto projectPath = temp.path() / "Game.Server.nginproj";
  WriteFile(projectPath,
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Game.Server">
  <Conditions>
    <Condition Name="linux-debug">
      <All>
        <When OperatingSystem="linux" />
        <When Profile="dev" />
      </All>
    </Condition>
  </Conditions>
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
      <Define Name="GAME_LINUX_DEBUG" Value="1" When="linux-debug" />
    </Build>
  </Application>
</Project>
)xml");

  const auto project = LoadProjectManifest(projectPath);

  REQUIRE(std::any_of(project.conditions.begin(), project.conditions.end(),
                      [](const ConditionDefinition &condition) {
                        return condition.name == "linux-debug";
                      }));
  REQUIRE(project.build.compileDefinitions.size() == 1);
  REQUIRE(project.build.compileDefinitions[0].value == "GAME_LINUX_DEBUG=1");
  REQUIRE(project.build.compileDefinitions[0].selectors.conditionRefs.size() ==
          1);
  REQUIRE(project.build.compileDefinitions[0].selectors.conditionRefs[0] ==
          "linux-debug");
}

TEST_CASE("profile build traits derive backend configuration and toolchain options") {
  TempDir temp{};
  const auto projectPath = temp.path() / "Traits.App.nginproj";
  WriteFile(projectPath,
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="Traits.App" DefaultProfile="clang-asan">
  <Application>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
  </Application>
  <Profile Name="clang-asan">
    <Defaults>
      <Toolchain Name="clang" />
    </Defaults>
    <Application>
      <Build>
        <Optimization Mode="Off" />
        <DebugSymbols Enabled="true" />
        <LinkTimeOptimization Enabled="false" />
        <CompileOption Value="-fsanitize=address" Toolchain="clang" />
        <CompileOption Value="/fsanitize=address" Toolchain="msvc" />
        <LinkOption Value="-fsanitize=address" Toolchain="clang" />
      </Build>
    </Application>
  </Profile>
</Project>
)xml");

  const auto project = LoadProjectManifest(projectPath);
  const auto &profile = ProfileByName(project, "clang-asan");
  REQUIRE(profile.optimization == "Off");
  REQUIRE(profile.debugSymbols);
  REQUIRE_FALSE(profile.linkTimeOptimization);
  REQUIRE(BackendConfiguration(profile) == "Debug");

  const auto compileOptions = EffectiveBuildSettings(
      project, profile, project.build.compileOptions, "CompileOption");
  REQUIRE(compileOptions.size() == 1);
  REQUIRE(compileOptions[0].value == "-fsanitize=address");
  const auto linkOptions = EffectiveBuildSettings(
      project, profile, project.build.linkOptions, "LinkOption");
  REQUIRE(linkOptions.size() == 1);
  REQUIRE(linkOptions[0].value == "-fsanitize=address");
}

TEST_CASE("unknown optimization mode is rejected") {
  TempDir temp{};
  const auto projectPath = temp.path() / "InvalidOptimization.nginproj";
  WriteFile(projectPath,
            R"xml(<Project SchemaVersion="4" Name="InvalidOptimization">
  <Application>
    <Build>
      <Optimization Mode="Maximum" />
    </Build>
  </Application>
</Project>
)xml");

  REQUIRE_THROWS_WITH(LoadProjectManifest(projectPath),
                      ContainsSubstring("unknown optimization mode 'Maximum'"));
}

TEST_CASE("package manifest parses product exports and feature contributions") {
  TempDir temp{};
  const auto packagePath = temp.path() / "NGIN.Core.nginpkg";
  WriteFile(packagePath,
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="NGIN.Core" Version="0.1.0">
  <Metadata>
    <Description>Optional hosted runtime for NGIN applications.</Description>
  </Metadata>
  <Uses>
    <Package Name="NGIN.Base" Version="[0.1.0,0.2.0)" Scope="Target" />
  </Uses>
  <Library Name="NGIN.Core">
    <Exports>
      <Headers Path="include/**.hpp" />
      <Binary Path="lib/linux-x64/libNGIN.Core.a"
              TargetPlatform="linux-x64"
              Abi="linux-x64-clang18-libc++-cxx23" />
      <LibraryTarget Name="NGIN::Core" />
    </Exports>
  </Library>
  <Features>
    <Feature Name="Reflection">
      <Uses>
        <Tool Name="NGIN.Reflection.MetaGen" Version="[0.1.0,0.2.0)" Scope="Build" />
      </Uses>
      <Build>
        <Options>
          <Option Name="NGIN_CORE_FEATURE_REFLECTION" Value="ON" />
        </Options>
        <Define Name="NGIN_CORE_REFLECTION" Value="1" Visibility="Public" />
      </Build>
      <Provides>
        <Capability Name="Reflection" />
      </Provides>
    </Feature>
  </Features>
</Package>
)xml");

  const auto package = LoadPackageManifest(packagePath);

  REQUIRE(package.name == "NGIN.Core");
  REQUIRE(package.version == "0.1.0");
  REQUIRE(package.dependencies.size() == 1);
  REQUIRE(package.dependencies[0].name == "NGIN.Base");
  REQUIRE(package.dependencies[0].scope == "Target");
  REQUIRE(package.artifacts.libraries.size() == 2);
  REQUIRE(package.artifacts.libraries[0].target == "NGIN::Core");
  REQUIRE(package.inputs.size() == 1);
  REQUIRE(package.inputs[0].kind == "Source");
  REQUIRE(package.inputs[0].role == "Header");
  REQUIRE(package.inputs[0].includePatterns[0] == "include/**.hpp");
  REQUIRE(package.features.size() == 1);
  REQUIRE(package.features[0].name == "Reflection");
  REQUIRE(package.features[0].packageRefs.size() == 1);
  REQUIRE(package.features[0].packageRefs[0].name == "NGIN.Reflection.MetaGen");
  REQUIRE(package.features[0].packageRefs[0].scope == "Build");
  REQUIRE(package.features[0].buildOptions.size() == 1);
  REQUIRE(package.features[0].buildOptions[0].name ==
          "NGIN_CORE_FEATURE_REFLECTION");
  REQUIRE(package.features[0].buildOptions[0].value == "ON");
  REQUIRE(package.features[0].build.compileDefinitions.size() == 1);
  REQUIRE(package.features[0].build.compileDefinitions[0].value ==
          "NGIN_CORE_REFLECTION=1");
  REQUIRE(package.features[0].provides.size() == 1);
  REQUIRE(package.features[0].provides[0].name == "Reflection");
}

TEST_CASE("package manifest parses tool exports") {
  TempDir temp{};
  const auto packagePath = temp.path() / "NGIN.Reflection.MetaGen.nginpkg";
  WriteFile(packagePath,
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="NGIN.Reflection.MetaGen" Version="0.1.0">
  <Tool Name="NGIN.Reflection.MetaGen">
    <Exports>
      <Tool Name="NGIN.Reflection.MetaGen" Executable="bin/ngin-metagen" />
    </Exports>
  </Tool>
</Package>
)xml");

  const auto package = LoadPackageManifest(packagePath);

  REQUIRE(package.name == "NGIN.Reflection.MetaGen");
  REQUIRE(package.tools.size() == 1);
  REQUIRE(package.tools[0].name == "NGIN.Reflection.MetaGen");
  REQUIRE(package.tools[0].executable == "bin/ngin-metagen");
  REQUIRE(package.artifacts.executables.size() == 1);
  REQUIRE(package.artifacts.executables[0].name == "NGIN.Reflection.MetaGen");
}

TEST_CASE("tool actions reject capabilities not provided by their driver") {
  TempDir temp{};
  const auto packagePath = temp.path() / "Invalid.Tooling.nginpkg";
  WriteFile(packagePath,
            R"xml(<Package SchemaVersion="4" Name="Invalid.Tooling" Version="1.0.0">
  <Tool Name="Invalid.Tooling"><Exports><Tool Name="tool" Executable="tool" /></Exports></Tool>
  <ToolDrivers>
    <Driver Name="driver" Protocol="NGIN.ToolDriver/1" Executable="driver">
      <Capabilities><Capability Name="diagnostics" /></Capabilities>
    </Driver>
  </ToolDrivers>
  <ToolActions>
    <Action Name="format" Kind="Format" Tool="tool" Driver="driver">
      <Accepts Contract="files/v1" />
      <Capabilities><Capability Name="edits" /></Capabilities>
    </Action>
  </ToolActions>
</Package>)xml");

  REQUIRE_THROWS_WITH(LoadPackageManifest(packagePath),
                      ContainsSubstring("capability 'edits' not provided by driver 'driver'"));
}

TEST_CASE("tool drivers preserve package-owned argument templates") {
  TempDir temp{};
  const auto packagePath = temp.path() / "Transform.Tooling.nginpkg";
  WriteFile(packagePath,
            R"xml(<Package SchemaVersion="4" Name="Transform.Tooling" Version="1.0.0">
  <ToolDrivers>
    <Driver Name="driver" Protocol="NGIN.ToolDriver/1" Adapter="builtin.stdout-transform.v1">
      <Arguments>
        <Arg Value="--config=$(Config)" />
        <Arg Value="$(InputContentFile)" />
      </Arguments>
      <ProbeArguments><Arg Value="--version" /></ProbeArguments>
    </Driver>
  </ToolDrivers>
</Package>)xml");

  const auto package = LoadPackageManifest(packagePath);
  REQUIRE(package.toolDrivers.size() == 1);
  REQUIRE(package.toolDrivers[0].probeArguments == std::vector<std::string>{"--version"});
  REQUIRE(package.toolDrivers[0].arguments ==
          std::vector<std::string>{"--config=$(Config)", "$(InputContentFile)"});
}

TEST_CASE("tool actions declare explicit environment and secret requirements") {
  TempDir temp{};
  const auto packagePath = temp.path() / "Environment.Tooling.nginpkg";
  WriteFile(packagePath,
            R"xml(<Package SchemaVersion="4" Name="Environment.Tooling" Version="1.0.0">
  <Tool Name="Environment.Tooling"><Exports><Tool Name="tool" Executable="tool" /></Exports></Tool>
  <ToolDrivers><Driver Name="driver" Protocol="NGIN.ToolDriver/1" Executable="driver" /></ToolDrivers>
  <ToolActions>
    <Action Name="scan" Kind="Scan" Tool="tool" Driver="driver">
      <Accepts Contract="artifacts/v1" />
      <Environment>
        <Variable Name="SCAN_MODE" Required="false" />
        <Variable Name="SCAN_TOKEN" Secret="true" CacheKey="true" />
      </Environment>
    </Action>
  </ToolActions>
</Package>)xml");

  const auto package = LoadPackageManifest(packagePath);
  REQUIRE(package.toolActions.size() == 1);
  REQUIRE(package.toolActions[0].environment.size() == 2);
  REQUIRE_FALSE(package.toolActions[0].environment[0].required);
  REQUIRE(package.toolActions[0].environment[1].secret);
  REQUIRE(package.toolActions[0].environment[1].cacheKey);
}

TEST_CASE("tool runs parse dependency and scheduler resource declarations") {
  TempDir temp{};
  const auto projectPath = temp.path() / "Scheduler.App.nginproj";
  WriteFile(projectPath,
            R"xml(<Project SchemaVersion="4" Name="Scheduler.App">
  <Application>
    <Tooling>
      <Run Name="analyze" DisplayName="Project Analysis"
           Description="Analyze the selected project inputs."
           Action="Example.Tooling::analyze">
        <Input Contract="files/v1" Scope="Product" Merge="Append">
          <Include Path="src/**" />
        </Input>
        <Execution Jobs="2" Timeout="30s" Cache="ReadWrite"
                   FailureStrategy="DependencyAware" Weight="2"
                   MaxParallelism="3" ExclusiveResource="compiler-db" />
        <Policy Gate="true" FailOn="Warning">
          <Severity Rule="style-rule" To="info" />
          <Suppress Fingerprint="known-finding" Reason="accepted for now" Expires="2099-12-31" />
          <Budget Rule="security-rule" Max="0" />
        </Policy>
      </Run>
      <Run Name="report" Action="Example.Tooling::report">
        <DependsOn Run="analyze" />
      </Run>
    </Tooling>
  </Application>
</Project>)xml");

  const auto project = LoadProjectManifest(projectPath);
  REQUIRE(project.tooling.runs.size() == 2);
  const auto &analyze = project.tooling.runs[0];
  REQUIRE(analyze.displayName == "Project Analysis");
  REQUIRE(analyze.description == "Analyze the selected project inputs.");
  REQUIRE(analyze.input.merge == "Append");
  REQUIRE(analyze.input.scopeExplicit);
  REQUIRE(analyze.execution.weight == 2);
  REQUIRE(analyze.execution.maxParallelism == 3);
  REQUIRE(analyze.execution.exclusiveResource == "compiler-db");
  REQUIRE(analyze.policy.severityMappings.size() == 1);
  REQUIRE(analyze.policy.severityMappings[0].severity == "info");
  REQUIRE(analyze.policy.suppressions.size() == 1);
  REQUIRE(analyze.policy.suppressions[0].reason == "accepted for now");
  REQUIRE(analyze.policy.ruleBudgets.size() == 1);
  REQUIRE(analyze.policy.ruleBudgets[0].maximum == 0);
  REQUIRE(project.tooling.runs[1].dependencies == std::vector<std::string>{"analyze"});
}

TEST_CASE("package manifest parses external provider build metadata") {
  TempDir temp{};
  const auto packagePath = temp.path() / "fmt.nginpkg";
  WriteFile(packagePath,
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="fmt" Version="10.2.1">
  <Build Backend="CMake"
         Mode="FindPackage"
         Provider="vcpkg"
         ProviderPackage="fmt"
         ProviderVersion="10.2.1"
         CMakePackage="fmt" />
  <Library Name="fmt">
    <Exports>
      <LibraryTarget Name="fmt::fmt" />
    </Exports>
  </Library>
</Package>
)xml");

  const auto package = LoadPackageManifest(packagePath);

  REQUIRE(package.build.backend == "CMake");
  REQUIRE(package.build.mode == "FindPackage");
  REQUIRE(package.build.provider == "vcpkg");
  REQUIRE(package.build.providerPackage == "fmt");
  REQUIRE(package.build.providerVersion == "10.2.1");
  REQUIRE(package.build.cmakePackage == "fmt");
}

TEST_CASE("package manifest parses provider package metadata without provider "
          "binding") {
  TempDir temp{};
  const auto packagePath = temp.path() / "openssl.nginpkg";
  WriteFile(packagePath,
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="OpenSSL" Version="3.0.0">
  <Build Backend="CMake"
         Mode="FindPackage"
         ProviderPackage="openssl"
         ProviderVersion="3.0.0"
         CMakePackage="OpenSSL"
         Linkage="Static;Shared"
         RuntimeDeployment="PackageRuntimeLibraries"
         RuntimeArtifacts="libcrypto">
    <Options>
      <Option Name="NGIN_BASE_CRYPTO_WITH_OPENSSL" Value="ON" />
    </Options>
  </Build>
</Package>
)xml");

  const auto package = LoadPackageManifest(packagePath);

  REQUIRE(package.build.mode == "FindPackage");
  REQUIRE(package.build.provider.empty());
  REQUIRE(package.build.providerPackage == "openssl");
  REQUIRE(package.build.providerVersion == "3.0.0");
  REQUIRE(package.build.cmakePackage == "OpenSSL");
  REQUIRE(package.build.linkage == "Static;Shared");
  REQUIRE(package.build.runtimeDeployment == "PackageRuntimeLibraries");
  REQUIRE(package.build.runtimeArtifacts == "libcrypto");
  REQUIRE(package.build.options.size() == 1);
  REQUIRE(package.build.options[0].name == "NGIN_BASE_CRYPTO_WITH_OPENSSL");
  REQUIRE(package.build.options[0].value == "ON");
}

TEST_CASE(
    "package manifest rejects provider metadata outside FindPackage mode") {
  TempDir temp{};
  const auto packagePath = temp.path() / "bad.nginpkg";
  WriteFile(packagePath,
            R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="bad" Version="1.0.0">
  <Build Backend="CMake" Mode="AddSubdirectory" Provider="vcpkg" />
</Package>
)xml");

  REQUIRE_THROWS_WITH(
      LoadPackageManifest(packagePath),
      ContainsSubstring(
          R"(Provider is only supported with Mode="FindPackage")"));
}
