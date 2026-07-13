#pragma once

#include "Build.hpp"
#include "Model.hpp"

#include <filesystem>
#include <functional>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace NGIN::CLI
{
    struct ToolScheduleNode
    {
        std::string identity{};
        std::vector<std::string> dependencies{};
        std::size_t weight{1};
        std::size_t maxParallelism{1};
        std::string exclusiveResource{};
        std::string failureStrategy{"DependencyAware"};
    };

    struct ToolScheduleBatch
    {
        std::vector<std::size_t> nodeIndices{};
        std::size_t weight{};
    };

    struct ToolSchedulePlan
    {
        std::vector<ToolScheduleBatch> batches{};
    };

    struct ToolScheduleOutcome
    {
        std::string status{"succeeded"};
        std::string skipReason{};
    };

    [[nodiscard]] auto BuildToolSchedule(const std::vector<ToolScheduleNode> &nodes,
                                         std::size_t workerBudget) -> ToolSchedulePlan;
    [[nodiscard]] auto ExecuteToolSchedule(
        const std::vector<ToolScheduleNode> &nodes,
        std::size_t workerBudget,
        const std::function<ToolScheduleOutcome(std::size_t)> &execute)
        -> std::vector<ToolScheduleOutcome>;

    struct ToolProtocolPosition
    {
        std::int64_t line{1};
        std::int64_t column{1};
    };

    struct ToolProtocolLocation
    {
        fs::path file{};
        ToolProtocolPosition start{};
        std::optional<ToolProtocolPosition> end{};
        std::string message{};
    };

    struct ToolProtocolDiagnostic
    {
        std::string severity{"error"};
        std::string intrinsicSeverity{};
        std::string effectiveSeverity{};
        std::string code{};
        std::string originalSeverity{};
        std::string originalCode{};
        std::string message{};
        std::string documentationUrl{};
        std::optional<ToolProtocolLocation> primaryLocation{};
        std::vector<ToolProtocolLocation> relatedLocations{};
        std::vector<std::string> tags{};
        std::string fingerprint{};
        std::vector<std::string> editSetIds{};
        bool suppressed{false};
        bool baselined{false};
        std::string suppressionSource{};
        std::string suppressionReason{};
    };

    struct ToolProtocolTextEdit
    {
        ToolProtocolPosition start{};
        ToolProtocolPosition end{};
        std::string newText{};
    };

    struct ToolProtocolFileEdits
    {
        fs::path file{};
        std::string expectedDigest{};
        std::vector<ToolProtocolTextEdit> edits{};
    };

    struct ToolProtocolEditSet
    {
        std::string id{};
        std::string label{};
        std::string applicability{"suggested"};
        std::vector<ToolProtocolFileEdits> files{};
    };

    struct ToolProtocolMetric
    {
        std::string name{};
        double value{};
        std::string unit{};
    };

    struct ToolDriverRequest
    {
        std::string runId{};
        std::string workspaceName{};
        fs::path workspaceRoot{};
        std::string projectName{};
        fs::path projectPath{};
        std::string profile{};
        std::string actionName{};
        std::string actionKind{};
        ToolResolution tool{};
        std::string driverName{};
        std::string driverProtocol{"NGIN.ToolDriver/1"};
        ToolResolution driver{};
        std::string hostPlatform{};
        std::string targetPlatform{};
        std::string targetAbi{};
        fs::path workingDirectory{};
        fs::path outputDirectory{};
        std::vector<fs::path> configs{};
        std::string inputContract{};
        std::vector<fs::path> files{};
        struct TranslationUnit
        {
            fs::path source{};
            fs::path workingDirectory{};
            std::string compiler{};
            std::vector<std::string> arguments{};
            std::string targetPlatform{};
            std::string language{};
            std::string owner{};
            bool generated{false};
            std::string commandDigest{};
        };
        std::vector<TranslationUnit> translationUnits{};
        std::vector<fs::path> priorResultPaths{};
        struct EnvironmentValue
        {
            std::string name{};
            std::string value{};
            bool secret{};
            bool cacheKey{};
        };
        std::vector<EnvironmentValue> environment{};
        std::vector<std::string> capabilitiesRequested{};
        std::optional<std::uint64_t> timeoutMilliseconds{};
        std::size_t maximumOutputBytes{16U * 1024U * 1024U};
        std::size_t jobs{1};
    };

    [[nodiscard]] auto LoadToolTranslationUnits(const fs::path &compileCommandsPath,
                                                 const std::vector<fs::path> &selectedSources,
                                                 std::string_view targetPlatform,
                                                 std::string_view owner)
        -> std::vector<ToolDriverRequest::TranslationUnit>;
    [[nodiscard]] auto ParseToolTimeoutMilliseconds(std::string_view value)
        -> std::optional<std::uint64_t>;

    struct ToolDriverResult
    {
        std::string executionStatus{"failed"};
        int processExitCode{};
        bool completed{false};
        std::vector<ToolProtocolDiagnostic> diagnostics{};
        std::vector<ToolProtocolEditSet> edits{};
        std::vector<fs::path> artifacts{};
        std::vector<ToolProtocolMetric> metrics{};
        std::string protocolError{};
        std::string rawOutput{};
        std::string driverLog{};
        std::string cacheStatus{"disabled"};
    };

    struct ToolDriverStreamEvent
    {
        std::string type{};
        std::string stage{};
        std::string message{};
        std::optional<std::int64_t> current{};
        std::optional<std::int64_t> total{};
    };

    struct ToolDriverProbeResult
    {
        bool completed{false};
        bool available{false};
        bool hostCompatible{false};
        std::string driverVersion{};
        std::string toolVersion{};
        std::vector<std::string> protocols{};
        std::vector<std::string> capabilities{};
        std::string reason{};
        std::string protocolError{};
        std::string rawOutput{};
        std::string driverLog{};
    };

    auto WriteToolDriverRequest(const ToolDriverRequest &request, const fs::path &path) -> void;

    [[nodiscard]] auto ParseToolDriverEvents(std::string_view output, std::string_view expectedRunId)
        -> ToolDriverResult;
    [[nodiscard]] auto ParseToolDriverProbeEvents(std::string_view output, std::string_view expectedRunId)
        -> ToolDriverProbeResult;

    [[nodiscard]] auto DeduplicateToolDiagnostics(
        const std::vector<ToolProtocolDiagnostic> &diagnostics)
        -> std::vector<ToolProtocolDiagnostic>;

    [[nodiscard]] auto ToolRequestCacheKey(const ToolDriverRequest &request) -> std::string;
    [[nodiscard]] auto ReadToolResultReplay(const fs::path &path) -> ToolDriverResult;
    auto WriteToolResultReplay(const ToolDriverResult &result, std::string_view runId,
                               const fs::path &path) -> void;
    [[nodiscard]] auto ToolFileDigest(const fs::path &path) -> std::string;
    auto ApplyToolEdits(const std::vector<ToolProtocolEditSet> &editSets,
                        const std::optional<fs::path> &allowedRoot = std::nullopt) -> void;
    [[nodiscard]] auto LoadToolBaseline(const fs::path &path) -> std::vector<std::string>;
    auto WriteToolBaseline(std::string_view runName,
                           const std::vector<ToolProtocolDiagnostic> &diagnostics,
                           const fs::path &path) -> void;

    [[nodiscard]] auto ExecuteToolDriver(const fs::path &driverExecutable,
                                         const ToolDriverRequest &request,
                                         const fs::path &requestPath,
                                         const std::function<void(const ToolDriverStreamEvent &)> &observer = {})
        -> ToolDriverResult;
    [[nodiscard]] auto ExecuteToolDriverProbe(const fs::path &driverExecutable,
                                              const ToolDriverRequest &request,
                                              const fs::path &requestPath)
        -> ToolDriverProbeResult;
    [[nodiscard]] auto ProbeBuiltinToolAdapter(std::string_view adapter,
                                               const ToolResolution &tool)
        -> ToolDriverProbeResult;

    [[nodiscard]] auto ExecuteBuiltinToolAdapter(
        std::string_view adapter,
        const ToolDriverRequest &request,
        const fs::path &compilationDatabaseDirectory) -> ToolDriverResult;

    auto WriteNormalizedToolResult(const ToolDriverRequest &request,
                                   std::string_view runName,
                                   std::string_view driverName,
                                   const ToolDriverResult &result,
                                   std::optional<bool> gateFailed,
                                   const fs::path &path) -> void;

    auto WriteToolReport(const ToolDriverRequest &request,
                         std::string_view runName,
                         std::string_view driverName,
                         const ToolDriverResult &result,
                         std::optional<bool> gateFailed,
                         std::string_view format,
                         const fs::path &path) -> void;
}
