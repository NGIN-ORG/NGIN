#include "Canonical.hpp"
#include "DependencyLock.hpp"
#include "PackageModel.hpp"
#include "TestSupport.hpp"

namespace
{
    [[nodiscard]] auto Package(const std::string &name, const PackageInstanceContext context,
                               std::string integrity = "sha256:artifact", const bool hermetic = true)
        -> GraphPackageInstance
    {
        const auto suffix = context == PackageInstanceContext::Host ? "host" : "target";
        return GraphPackageInstance{.identity = "sha256:" + name + "-" + suffix,
                                    .coordinate = PackageCoordinate{.name = name,
                                                                    .exactVersion = "1.2.3",
                                                                    .sourceBinding = "packages"},
                                    .context = context,
                                    .providerKind = "Conan",
                                    .providerIdentity = name + "/1.2.3@ngin/stable",
                                    .providerVersion = "1.2.3+r4",
                                    .revision = "recipe-r4:package-p9",
                                    .integrity = std::move(integrity),
                                    .artifactIdentity = name + ":package-p9",
                                    .hermetic = hermetic,
                                    .compatibility = BinaryCompatibility{.operatingSystem = "linux",
                                                                          .architecture = "x64",
                                                                          .compiler = "clang",
                                                                          .compilerVersion = "19",
                                                                          .runtimeLibrary = "libc++",
                                                                          .configuration = "Debug",
                                                                          .linkage = "Default",
                                                                          .artifactOptions = {{"Shared", "false"}}},
                                    .artifactOptions = {{"Shared", "false"}}};
    }

    [[nodiscard]] auto Graph(std::vector<GraphPackageInstance> packages) -> ResolvedCompositionGraph
    {
        CompositionGraphData data{};
        data.product = GraphProduct{.identity = "App", .name = "App", .type = ProductType::Application};
        data.selection = GraphSelection{.configuration = "Debug",
                                        .targetOperatingSystem = "linux",
                                        .targetArchitecture = "x64",
                                        .compiler = "clang",
                                        .compilerVersion = "19",
                                        .runtimeLibrary = "libc++"};
        data.packages = std::move(packages);
        return ResolvedCompositionGraph{std::move(data)};
    }
}

TEST_CASE("canonical fingerprints use stable SHA-256")
{
    REQUIRE(Sha256Fingerprint("abc") ==
            "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    REQUIRE(Graph({}).CompositionIdentity().starts_with("sha256:"));
    REQUIRE(Graph({}).CompositionIdentity().size() == 71);
}

TEST_CASE("dependency lock round-trips exact host and target PackageInstances")
{
    const auto graph = Graph({Package("Compiler", PackageInstanceContext::Host),
                              Package("Runtime", PackageInstanceContext::Target)});
    const auto lock = CreateDependencyLock(graph);
    REQUIRE(lock.Data().packages.size() == 2);
    REQUIRE(lock.Data().packages[0].providerVersion == "1.2.3+r4");
    REQUIRE(lock.Data().packages[0].artifactIdentity.ends_with(":package-p9"));
    const auto parsed = ParseDependencyLock(lock.CanonicalSerialization());
    REQUIRE(parsed.Succeeded());
    REQUIRE(parsed.lock->CanonicalSerialization() == lock.CanonicalSerialization());
    REQUIRE(parsed.lock->Fingerprint() == lock.Fingerprint());
    REQUIRE(VerifyDependencyLock(lock, *parsed.lock).reusable);
}

TEST_CASE("composition-only activation changes fingerprint without changing dependency lock")
{
    CompositionGraphData beforeData = Graph({Package("Runtime", PackageInstanceContext::Target)}).Data();
    CompositionGraphData afterData = beforeData;
    afterData.options.push_back(GraphOption{.identity = "App:Option:Diagnostics",
                                            .owner = "App",
                                            .name = "Diagnostics",
                                            .value = "verbose",
                                            .artifact = false});
    afterData.exports.push_back(GraphExport{.identity = beforeData.packages[0].identity + "::Notices",
                                            .packageInstance = beforeData.packages[0].identity,
                                            .name = "Notices",
                                            .kind = ExportUseKind::Asset});
    afterData.actions.push_back(GraphAction{.identity = "Generate:Notices",
                                            .kind = ActionKind::Generate,
                                            .packageInstance = beforeData.packages[0].identity,
                                            .actionExport = "Generate",
                                            .toolExport = "Compiler",
                                            .deterministic = true});
    const ResolvedCompositionGraph before{std::move(beforeData)};
    const ResolvedCompositionGraph after{std::move(afterData)};
    REQUIRE(before.CompositionIdentity() != after.CompositionIdentity());
    REQUIRE(CreateDependencyLock(before).CanonicalSerialization() ==
            CreateDependencyLock(after).CanonicalSerialization());
    REQUIRE_FALSE(VerifyCompositionFingerprint(before.CompositionIdentity(), after).matches);
}

TEST_CASE("artifact-affecting changes invalidate a dependency lock with field reasons")
{
    auto changed = Package("Runtime", PackageInstanceContext::Target);
    changed.integrity = "sha256:changed";
    changed.artifactIdentity = "Runtime:package-p10";
    changed.artifactOptions["Shared"] = "true";
    changed.compatibility.artifactOptions["Shared"] = "true";
    const auto expected = CreateDependencyLock(Graph({Package("Runtime", PackageInstanceContext::Target)}));
    const auto actual = CreateDependencyLock(Graph({std::move(changed)}));
    const auto verification = VerifyDependencyLock(expected, actual);
    REQUIRE_FALSE(verification.reusable);
    REQUIRE(std::ranges::any_of(verification.invalidations,
                                [](const auto &value) { return value.field == "integrity"; }));
    REQUIRE(std::ranges::any_of(verification.invalidations,
                                [](const auto &value) { return value.field == "artifactOptions"; }));
}

TEST_CASE("locked CI rejects non-hermetic and integrity-free provider results")
{
    auto package = Package("SystemSDL", PackageInstanceContext::Target, "", false);
    const auto lock = CreateDependencyLock(Graph({std::move(package)}));
    REQUIRE(VerifyDependencyLock(lock, lock, LockVerificationPolicy{.requireHermetic = false,
                                                                    .requireIntegrity = false}).reusable);
    const auto locked = VerifyDependencyLock(lock, lock, LockVerificationPolicy{.requireHermetic = true,
                                                                                .requireIntegrity = true});
    REQUIRE_FALSE(locked.reusable);
    REQUIRE(locked.invalidations.size() == 2);
}

TEST_CASE("catalog PackageProvider preserves provider-native version and honest hermeticity")
{
    TempDir temp{};
    const auto manifest = temp.path() / "Example.nginpkg";
    WriteFile(manifest, R"xml(<Package Name="Example" Version="2.1.0"><Exports><Library Name="Core" Default="true" /></Exports></Package>)xml");
    CatalogPackageProvider provider{
        "vcpkg", "workspace-vcpkg",
        {PackageProviderResult{.coordinate = PackageCoordinate{.name = "Example", .exactVersion = "2.1.0"},
                               .providerKind = "vcpkg",
                               .nativeIdentity = "example:x64-windows",
                               .nativeVersion = "2.1.0#port-version-3",
                               .revision = "baseline:abc123",
                               .integrity = "",
                               .artifactIdentity = "example:x64-windows@abc123",
                               .root = temp.path(),
                               .manifest = manifest,
                               .installedPrefix = temp.path() / "installed/x64-windows",
                               .hermetic = false,
                               .provenance = "vcpkg-manifest"}}};
    const auto resolved = provider.Resolve(PackageProviderRequest{.name = "Example",
                                                                   .sourceBinding = "workspace-vcpkg"});
    REQUIRE(resolved.Succeeded());
    REQUIRE(resolved.value->nativeVersion == "2.1.0#port-version-3");
    REQUIRE(resolved.value->revision == "baseline:abc123");
    REQUIRE_FALSE(resolved.value->hermetic);
}
