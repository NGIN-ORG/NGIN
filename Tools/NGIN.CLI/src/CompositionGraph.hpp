#pragma once

#include "ActionContract.hpp"
#include "Canonical.hpp"
#include "CompositionBoundary.hpp"
#include "ProjectModel.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace NGIN::CLI
{
    struct GraphProvenance
    {
        std::string kind{};
        std::string owner{};
        std::string document{};
        std::size_t line{0};
        std::size_t column{0};
        std::string reason{};

        [[nodiscard]] friend auto operator==(const GraphProvenance &, const GraphProvenance &) -> bool = default;
    };

    struct GraphProduct
    {
        std::string identity{};
        std::string name{};
        ProductArtifactKind artifactKind{ProductArtifactKind::Executable};
        LibraryKind libraryKind{LibraryKind::None};
        std::optional<std::string> version{};
        std::optional<std::string> license{};
        std::string languageStandard{"C++23"};
        bool languageExtensions{false};
        bool languageRequired{true};
        GraphProvenance provenance{};
    };

    struct GraphSelection
    {
        std::string identity{"Selection"};
        std::string configuration{};
        std::string targetOperatingSystem{};
        std::string targetArchitecture{};
        std::string compiler{};
        std::string compilerVersion{};
        std::string runtimeLibrary{};
        std::string optimization{"Off"};
        bool debugSymbols{false};
        bool linkTimeOptimization{false};
        std::optional<std::string> toolchainFile{};
        GraphProvenance provenance{};
    };

    struct GraphOption
    {
        std::string identity{};
        std::string owner{};
        std::string name{};
        std::string value{};
        bool artifact{false};
        GraphProvenance provenance{};
    };

    struct GraphPackageInstance
    {
        std::string identity{};
        PackageCoordinate coordinate{};
        PackageInstanceContext context{PackageInstanceContext::Target};
        std::string providerKind{};
        std::string providerIdentity{};
        std::string providerVersion{};
        std::string revision{};
        std::string integrity{};
        std::string trust{};
        std::string signature{};
        std::string artifactIdentity{};
        bool hermetic{false};
        BinaryCompatibility compatibility{};
        std::map<std::string, std::string, std::less<>> artifactOptions{};
        GraphProvenance provenance{};
    };

    struct GraphExport
    {
        std::string identity{};
        std::string packageInstance{};
        std::string name{};
        ExportUseKind kind{ExportUseKind::Library};
        GraphProvenance provenance{};
    };

    struct GraphCapabilityBinding
    {
        std::string identity{};
        CapabilityBinding binding{};
        GraphProvenance provenance{};
    };

    struct GraphAction
    {
        std::string identity{};
        ActionKind kind{ActionKind::Custom};
        std::string packageInstance{};
        std::string actionExport{};
        std::string toolExport{};
        bool deterministic{false};
        std::vector<std::string> inputs{};
        std::vector<std::string> outputs{};
        std::vector<std::string> arguments{};
        std::string workingDirectory{"."};
        std::map<std::string, std::string, std::less<>> environment{};
        std::map<std::string, std::string, std::less<>> options{};
        GraphProvenance provenance{};
    };

    struct GraphPlugin
    {
        std::string identity{};
        std::string packageInstance{};
        std::string exportName{};
        GraphProvenance provenance{};
    };

    struct GraphContribution
    {
        std::string identity{};
        std::string owner{};
        std::string kind{};
        std::string include{};
        std::string destination{};
        GraphProvenance provenance{};
    };

    struct GraphBuildItem
    {
        std::string identity{};
        std::string kind{};
        std::string path{};
        std::optional<std::string> value{};
        std::string visibility{};
        bool generated{false};
        GraphProvenance provenance{};
    };

    struct GraphRun
    {
        std::string identity{};
        std::string name{};
        bool defaultRun{false};
        std::string executableKind{};
        std::string executable{};
        std::string workingDirectory{"."};
        std::vector<std::string> arguments{};
        std::map<std::string, std::string, std::less<>> environment{};
        std::map<std::string, std::string, std::less<>> secrets{};
        GraphProvenance provenance{};
    };

    struct GraphTestRegistration
    {
        std::string identity{};
        std::string name{};
        std::vector<std::string> arguments{};
        std::map<std::string, std::string, std::less<>> environment{};
        std::optional<std::int64_t> timeoutSeconds{};
        GraphProvenance provenance{};
    };

    struct GraphBenchmarkRegistration : GraphTestRegistration
    {
        std::optional<std::int64_t> repetitions{};
        std::optional<std::int64_t> warmupSeconds{};
    };

    struct GraphPublish
    {
        std::string identity{};
        std::string name{};
        std::string outputKind{};
        std::string format{};
        std::string output{};
        GraphProvenance provenance{};
    };

    struct GraphEdge
    {
        std::string identity{};
        std::string from{};
        std::string to{};
        std::string kind{};
        std::string visibility{};
        std::string context{};
        std::string scope{};
        GraphProvenance provenance{};
    };

    struct CompositionGraphData
    {
        GraphProduct product{};
        GraphSelection selection{};
        std::vector<GraphOption> options{};
        std::vector<GraphPackageInstance> packages{};
        std::vector<GraphExport> exports{};
        std::vector<GraphCapabilityBinding> capabilities{};
        std::vector<GraphAction> actions{};
        std::vector<GraphPlugin> plugins{};
        std::vector<GraphContribution> contributions{};
        std::vector<GraphBuildItem> buildItems{};
        std::vector<GraphRun> runs{};
        std::vector<GraphTestRegistration> tests{};
        std::vector<GraphBenchmarkRegistration> benchmarks{};
        std::vector<GraphPublish> publishes{};
        std::vector<GraphEdge> edges{};
    };

    class ResolvedCompositionGraph
    {
    public:
        explicit ResolvedCompositionGraph(CompositionGraphData data);

        [[nodiscard]] auto Data() const -> const CompositionGraphData &;
        [[nodiscard]] auto CanonicalSerialization() const -> std::string;
        [[nodiscard]] auto CompositionIdentity() const -> std::string;

    private:
        std::shared_ptr<const CompositionGraphData> data_{};
        std::string canonical_{};
        std::string identity_{};
    };

    struct GraphDifference
    {
        std::string category{};
        std::string identity{};
        std::string change{};
        std::optional<std::string> before{};
        std::optional<std::string> after{};
    };

    struct GraphExplanation
    {
        std::string identity{};
        std::string category{};
        std::string value{};
        std::vector<GraphProvenance> provenance{};
        std::vector<GraphEdge> edges{};
    };

    [[nodiscard]] auto SerializeCompositionGraph(const CompositionGraphData &graph) -> std::string;
    [[nodiscard]] auto DiffCompositionGraphs(const ResolvedCompositionGraph &before,
                                             const ResolvedCompositionGraph &after)
        -> std::vector<GraphDifference>;
    [[nodiscard]] auto ExplainCompositionIdentity(const ResolvedCompositionGraph &graph, std::string_view identity)
        -> std::optional<GraphExplanation>;
}
