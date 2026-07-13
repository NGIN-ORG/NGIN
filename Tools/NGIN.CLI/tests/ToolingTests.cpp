#include "TestSupport.hpp"
#include "Tooling.hpp"

#include <NGIN/Serialization/JSON/JsonParser.hpp>

#include <atomic>
#include <csignal>
#include <future>
#include <thread>

TEST_CASE("tool scheduler is dependency-aware, bounded, deterministic, and resource-safe")
{
    const std::vector<ToolScheduleNode> nodes{
        {.identity = "analyze-a", .weight = 1, .exclusiveResource = "compiler"},
        {.identity = "analyze-b", .weight = 1, .exclusiveResource = "compiler"},
        {.identity = "scan", .weight = 1},
        {.identity = "report", .dependencies = {"analyze-a", "analyze-b", "scan"}, .weight = 1},
    };
    const auto plan = BuildToolSchedule(nodes, 2);
    REQUIRE(plan.batches.size() == 3);
    REQUIRE(plan.batches[0].nodeIndices == std::vector<std::size_t>{0, 2});
    REQUIRE(plan.batches[1].nodeIndices == std::vector<std::size_t>{1});
    REQUIRE(plan.batches[2].nodeIndices == std::vector<std::size_t>{3});

    std::atomic<int> active{0};
    std::atomic<int> maximumActive{0};
    const auto outcomes = ExecuteToolSchedule(nodes, 2, [&](std::size_t) {
        const auto current = ++active;
        maximumActive.store(std::max(maximumActive.load(), current));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        --active;
        return ToolScheduleOutcome{.status = "succeeded"};
    });
    REQUIRE(maximumActive.load() == 2);
    REQUIRE(std::ranges::all_of(outcomes, [](const auto &outcome) { return outcome.status == "succeeded"; }));
}

TEST_CASE("tool scheduler propagates dependency failures and FailFast")
{
    const std::vector<ToolScheduleNode> dependencyAware{
        {.identity = "first"},
        {.identity = "dependent", .dependencies = {"first"}},
        {.identity = "independent"},
    };
    const auto outcomes = ExecuteToolSchedule(dependencyAware, 2, [&](std::size_t index) {
        return ToolScheduleOutcome{.status = index == 0 ? "failed" : "succeeded"};
    });
    REQUIRE(outcomes[0].status == "failed");
    REQUIRE(outcomes[1].status == "skipped");
    REQUIRE_THAT(outcomes[1].skipReason, ContainsSubstring("first (failed)"));
    REQUIRE(outcomes[2].status == "succeeded");

    const std::vector<ToolScheduleNode> failFast{
        {.identity = "first", .failureStrategy = "FailFast"},
        {.identity = "later", .dependencies = {"first"}},
    };
    const auto stopped = ExecuteToolSchedule(failFast, 1, [&](std::size_t index) {
        return ToolScheduleOutcome{.status = index == 0 ? "gate-failed" : "succeeded"};
    });
    REQUIRE(stopped[0].status == "gate-failed");
    REQUIRE(stopped[1].status == "skipped");
    REQUIRE_THAT(stopped[1].skipReason, ContainsSubstring("FailFast"));
}

TEST_CASE("tool scheduler rejects missing dependencies and cycles")
{
    REQUIRE_THROWS_WITH(
        BuildToolSchedule({ToolScheduleNode{.identity = "run", .dependencies = {"missing"}}}, 1),
        ContainsSubstring("depends on missing node"));
    REQUIRE_THROWS_WITH(
        BuildToolSchedule({
            ToolScheduleNode{.identity = "a", .dependencies = {"b"}},
            ToolScheduleNode{.identity = "b", .dependencies = {"a"}},
        }, 1),
        ContainsSubstring("dependency cycle"));
}

TEST_CASE("tool driver protocol parses normalized diagnostics and completion")
{
    const auto output =
        R"json({"schemaVersion":"1.0","kind":"NGIN.ToolDriver.Event","runId":"run-1","sequence":1,"type":"run.started","data":{}})json"
        "\n"
        R"json({"schemaVersion":"1.0","kind":"NGIN.ToolDriver.Event","runId":"run-1","sequence":2,"type":"diagnostic","data":{"severity":"warning","code":"example-rule","message":"example finding","fingerprint":"abc","primaryLocation":{"file":{"absolute":"/repo/src/main.cpp","workspaceRelative":"src/main.cpp"},"range":{"start":{"line":4,"column":7},"end":{"line":4,"column":11}}},"relatedLocations":[{"file":{"absolute":"/repo/include/main.hpp","workspaceRelative":"include/main.hpp"},"range":{"start":{"line":2,"column":1}},"message":"declared here"}]}})json"
        "\n"
        R"json({"schemaVersion":"1.0","kind":"NGIN.ToolDriver.Event","runId":"run-1","sequence":3,"type":"edit.proposed","data":{"id":"fix-1","label":"Apply fix","applicability":"automatic","files":[{"path":{"absolute":"/repo/src/main.cpp","workspaceRelative":"src/main.cpp"},"expectedDigest":"abc","edits":[{"range":{"start":{"line":4,"column":7},"end":{"line":4,"column":11}},"newText":"value"}]}]}})json"
        "\n"
        R"json({"schemaVersion":"1.0","kind":"NGIN.ToolDriver.Event","runId":"run-1","sequence":4,"type":"artifact.produced","data":{"path":"/repo/out/report.sarif"}})json"
        "\n"
        R"json({"schemaVersion":"1.0","kind":"NGIN.ToolDriver.Event","runId":"run-1","sequence":5,"type":"metric","data":{"name":"files","value":12,"unit":"count"}})json"
        "\n"
        R"json({"schemaVersion":"1.0","kind":"NGIN.ToolDriver.Event","runId":"run-1","sequence":6,"type":"run.completed","data":{"status":"succeeded"}})json"
        "\n";

    const auto result = ParseToolDriverEvents(output, "run-1");
    REQUIRE(result.protocolError.empty());
    REQUIRE(result.completed);
    REQUIRE(result.executionStatus == "succeeded");
    REQUIRE(result.diagnostics.size() == 1);
    REQUIRE(result.diagnostics[0].severity == "warning");
    REQUIRE(result.diagnostics[0].code == "example-rule");
    REQUIRE(result.diagnostics[0].fingerprint == "abc");
    REQUIRE(result.diagnostics[0].primaryLocation.has_value());
    REQUIRE(result.diagnostics[0].primaryLocation->start.line == 4);
    REQUIRE(result.diagnostics[0].primaryLocation->end.has_value());
    REQUIRE(result.diagnostics[0].primaryLocation->end->column == 11);
    REQUIRE(result.diagnostics[0].relatedLocations.size() == 1);
    REQUIRE(result.edits.size() == 1);
    REQUIRE(result.edits[0].id == "fix-1");
    REQUIRE(result.edits[0].files[0].edits[0].newText == "value");
    REQUIRE(result.artifacts == std::vector<fs::path>{"/repo/out/report.sarif"});
    REQUIRE(result.metrics.size() == 1);
    REQUIRE(result.metrics[0].value == 12.0);
}

TEST_CASE("tool edits validate file digests and apply ranges")
{
    TempDir temp{};
    const auto path = temp.path() / "main.cpp";
    WriteFile(path, "int value = 1;\n");
    ToolProtocolEditSet editSet{.id = "fix", .applicability = "automatic"};
    editSet.files.push_back(ToolProtocolFileEdits{
        .file = path,
        .expectedDigest = ToolFileDigest(path),
        .edits = {ToolProtocolTextEdit{
            .start = ToolProtocolPosition{.line = 1, .column = 13},
            .end = ToolProtocolPosition{.line = 1, .column = 14},
            .newText = "2",
        }},
    });
    ApplyToolEdits({editSet}, temp.path());
    REQUIRE(ReadFile(path) == "int value = 2;\n");
    REQUIRE_THROWS_WITH(ApplyToolEdits({editSet}), ContainsSubstring("refused stale tool edit set"));
    REQUIRE_THROWS_WITH(ApplyToolEdits({editSet}, temp.path() / "different-root"),
                        ContainsSubstring("outside the workspace"));
}

TEST_CASE("tool baselines persist normalized finding fingerprints")
{
    TempDir temp{};
    const auto path = temp.path() / "baseline.json";
    WriteToolBaseline("analysis", {
        ToolProtocolDiagnostic{.severity = "warning", .code = "rule-a", .message = "a", .fingerprint = "fp-a"},
        ToolProtocolDiagnostic{.severity = "error", .code = "rule-b", .message = "b", .fingerprint = "fp-b"},
    }, path);
    REQUIRE(LoadToolBaseline(path) == std::vector<std::string>{"fp-a", "fp-b"});
    REQUIRE_THAT(ReadFile(path), ContainsSubstring(R"("kind":"NGIN.ToolBaseline")"));
}

TEST_CASE("tool driver protocol rejects missing completion and sequence mismatches")
{
    const auto missingCompletion = ParseToolDriverEvents(
        R"json({"schemaVersion":"1.0","kind":"NGIN.ToolDriver.Event","runId":"run-1","sequence":1,"type":"run.started","data":{}})json"
        "\n",
        "run-1");
    REQUIRE_FALSE(missingCompletion.protocolError.empty());
    REQUIRE_THAT(missingCompletion.protocolError, ContainsSubstring("without run.completed"));

    const auto badSequence = ParseToolDriverEvents(
        R"json({"schemaVersion":"1.0","kind":"NGIN.ToolDriver.Event","runId":"run-1","sequence":2,"type":"run.completed","data":{"status":"succeeded"}})json"
        "\n",
        "run-1");
    REQUIRE_THAT(badSequence.protocolError, ContainsSubstring("sequence is not monotonic"));
}

TEST_CASE("tool driver requests materialize selected compilation units")
{
    TempDir temp{};
    const auto source = temp.path() / "src/main.cpp";
    WriteFile(source, "int main() { return 0; }\n");
    const auto database = temp.path() / "compile_commands.json";
    WriteFile(database,
              "[{\"directory\":\"" + temp.path().generic_string() +
                  "\",\"file\":\"src/main.cpp\",\"arguments\":[\"c++\",\"-std=c++23\",\"-c\",\"src/main.cpp\"]}]");

    const auto units = LoadToolTranslationUnits(database, {source}, "linux-x64", "App");
    REQUIRE(units.size() == 1);
    REQUIRE(units[0].source == fs::weakly_canonical(source));
    REQUIRE(units[0].compiler == "c++");
    REQUIRE(units[0].arguments.size() == 4);
    REQUIRE(units[0].language == "c++");
    REQUIRE(units[0].owner == "App");
    REQUIRE_FALSE(units[0].commandDigest.empty());
}

TEST_CASE("normalized tool diagnostics deduplicate fingerprints and retain related context")
{
    ToolProtocolDiagnostic first{.severity = "warning", .code = "rule", .message = "finding", .fingerprint = "same"};
    ToolProtocolDiagnostic second{.severity = "warning", .code = "rule", .message = "finding", .fingerprint = "same"};
    second.relatedLocations.push_back(ToolProtocolLocation{
        .file = "/repo/src/other.cpp",
        .start = ToolProtocolPosition{.line = 3, .column = 2},
        .message = "instantiated here",
    });
    second.editSetIds.push_back("fix-1");

    const auto deduplicated = DeduplicateToolDiagnostics({first, second});
    REQUIRE(deduplicated.size() == 1);
    REQUIRE(deduplicated[0].relatedLocations.size() == 1);
    REQUIRE(deduplicated[0].editSetIds == std::vector<std::string>{"fix-1"});
}

#ifndef _WIN32
TEST_CASE("external tool driver receives a request file and executes through the general protocol")
{
    TempDir temp{};
    const auto driver = temp.path() / "fake-driver";
    WriteFile(driver,
              "#!/bin/sh\n"
              "request=\"$2\"\n"
              "test -f \"$request\" || exit 9\n"
              "echo 'driver detail' >&2\n"
              "if test \"$1\" = '--ngin-probe'; then\n"
              "  echo '{\"schemaVersion\":\"1.0\",\"kind\":\"NGIN.ToolDriver.Event\",\"runId\":\"run-2\",\"sequence\":1,\"type\":\"probe.completed\",\"data\":{\"available\":true,\"hostCompatible\":true,\"driverVersion\":\"1.2.0\",\"toolVersion\":\"4.0.0\",\"protocols\":[\"NGIN.ToolDriver/1\"],\"capabilities\":[\"diagnostics\"]}}'\n"
              "  exit 0\n"
              "fi\n"
              "echo '{\"schemaVersion\":\"1.0\",\"kind\":\"NGIN.ToolDriver.Event\",\"runId\":\"run-2\",\"sequence\":1,\"type\":\"run.started\",\"data\":{}}'\n"
              "echo '{\"schemaVersion\":\"1.0\",\"kind\":\"NGIN.ToolDriver.Event\",\"runId\":\"run-2\",\"sequence\":2,\"type\":\"run.completed\",\"data\":{\"status\":\"succeeded\"}}'\n");
    fs::permissions(driver, fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write);

    const ToolDriverRequest request{
        .runId = "run-2",
        .workspaceName = "Test",
        .workspaceRoot = temp.path(),
        .projectName = "App",
        .projectPath = temp.path() / "App.nginproj",
        .profile = "dev",
        .actionName = "Test.Tooling::analyze",
        .actionKind = "Analyze",
        .tool = ToolResolution{.path = temp.path() / "tool", .source = "test"},
        .hostPlatform = "linux-x64",
        .targetPlatform = "linux-x64",
        .workingDirectory = temp.path(),
        .outputDirectory = temp.path() / "out",
        .inputContract = "files/v1",
        .files = {temp.path() / "src/main.cpp"},
        .environment = {
            {.name = "MODE", .value = "strict"},
            {.name = "TOKEN", .value = "secret-value", .secret = true, .cacheKey = true},
        },
        .capabilitiesRequested = {"diagnostics"},
    };
    const auto requestPath = temp.path() / "out/request.json";
    const auto probe = ExecuteToolDriverProbe(driver, request, temp.path() / "out/probe-request.json");
    REQUIRE(probe.available);
    REQUIRE(probe.hostCompatible);
    REQUIRE(probe.driverVersion == "1.2.0");
    REQUIRE(probe.toolVersion == "4.0.0");
    REQUIRE(probe.capabilities == std::vector<std::string>{"diagnostics"});
    const auto result = ExecuteToolDriver(driver, request, requestPath);

    REQUIRE(result.processExitCode == 0);
    REQUIRE(result.protocolError.empty());
    REQUIRE(result.executionStatus == "succeeded");
    REQUIRE(result.driverLog == "driver detail\n");
    REQUIRE(fs::exists(requestPath));
    const auto requestText = ReadFile(requestPath);
    REQUIRE_THAT(requestText, ContainsSubstring(R"("kind":"NGIN.ToolDriver.Request")"));
    REQUIRE_THAT(requestText, ContainsSubstring(R"("contract":"files/v1")"));
    REQUIRE_THAT(requestText, ContainsSubstring(R"("capabilitiesRequested":["diagnostics"])"));
    REQUIRE_THAT(requestText, ContainsSubstring(R"("environment":{"MODE":"strict","TOKEN":"secret-value"})"));
    REQUIRE((fs::status(requestPath).permissions() & fs::perms::group_read) == fs::perms::none);
    REQUIRE(ToolRequestCacheKey(request).find("secret-value") == std::string::npos);

    const auto slowDriver = temp.path() / "slow-driver";
    WriteFile(slowDriver, "#!/bin/sh\nsleep 2\n");
    fs::permissions(slowDriver, fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write);
    auto timeoutRequest = request;
    timeoutRequest.runId = "run-timeout";
    timeoutRequest.timeoutMilliseconds = 50;
    const auto timedOut = ExecuteToolDriver(slowDriver, timeoutRequest,
                                            temp.path() / "timeout/request.json");
    REQUIRE(timedOut.executionStatus == "timed-out");
    REQUIRE_THAT(timedOut.protocolError, ContainsSubstring("timeout"));

    const auto crashDriver = temp.path() / "crash-driver";
    WriteFile(crashDriver,
              "#!/bin/sh\n"
              "echo '{\"schemaVersion\":\"1.0\",\"kind\":\"NGIN.ToolDriver.Event\",\"runId\":\"run-crash\",\"sequence\":1,\"type\":\"run.started\",\"data\":{}}'\n"
              "exit 7\n");
    fs::permissions(crashDriver, fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write);
    auto crashRequest = request;
    crashRequest.runId = "run-crash";
    const auto crashed = ExecuteToolDriver(crashDriver, crashRequest,
                                           temp.path() / "crash/request.json");
    REQUIRE(crashed.executionStatus == "failed");
    REQUIRE(crashed.processExitCode == 7);
    REQUIRE_THAT(crashed.protocolError, ContainsSubstring("exited with code 7"));

    auto cancellationRequest = request;
    cancellationRequest.runId = "run-cancel";
    auto cancellation = std::async(std::launch::async, [&] {
      return ExecuteToolDriver(slowDriver, cancellationRequest,
                               temp.path() / "cancel/request.json");
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::raise(SIGINT);
    const auto cancelled = cancellation.get();
    REQUIRE(cancelled.executionStatus == "cancelled");
    REQUIRE_THAT(cancelled.protocolError, ContainsSubstring("cancelled"));
}

TEST_CASE("registered bootstrap adapter exports clang-tidy replacements as general edit sets")
{
    TempDir temp{};
    const auto source = temp.path() / "main.cpp";
    WriteFile(source, "int value = 0;\n");
    const auto tool = temp.path() / "fake-clang-tidy";
    WriteFile(tool,
              "#!/bin/sh\n"
              "for arg in \"$@\"; do\n"
              "  case \"$arg\" in --export-fixes=*) fixes=${arg#--export-fixes=} ;; esac\n"
              "done\n"
              "printf '%s\\n' '---' 'MainSourceFile:  ''" + source.string() + "''' 'Diagnostics:' "
              "'  - DiagnosticName:  modernize-example' '    DiagnosticMessage:' "
              "'      Message:         ''replace type''' '      FilePath:        ''" + source.string() + "''' "
              "'      FileOffset:      0' '      Replacements:' "
              "'        - FilePath:        ''" + source.string() + "''' '          Offset:          0' "
              "'          Length:          3' '          ReplacementText: int32_t' '...' > \"$fixes\"\n"
              "echo '" + source.string() + ":1:1: warning: replace type [modernize-example]'\n"
              "exit 0\n");
    fs::permissions(tool, fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write);
    ToolDriverRequest request{
        .runId = "bootstrap-fix",
        .workspaceRoot = temp.path(),
        .projectName = "App",
        .projectPath = temp.path() / "App.nginproj",
        .profile = "dev",
        .actionName = "Example::analyze",
        .actionKind = "Analyze",
        .tool = ToolResolution{.path = tool, .source = "test"},
        .workingDirectory = temp.path(),
        .outputDirectory = temp.path() / "out",
        .inputContract = "cpp.translation-units/v1",
        .files = {source},
    };

    const auto result = ExecuteBuiltinToolAdapter("builtin.clang-tidy.v1", request, temp.path());
    REQUIRE(result.executionStatus == "succeeded");
    REQUIRE(result.diagnostics.size() == 1);
    REQUIRE(result.edits.size() == 1);
    REQUIRE(result.edits[0].applicability == "suggested");
    REQUIRE(result.edits[0].files[0].expectedDigest == ToolFileDigest(source));
    REQUIRE(result.edits[0].files[0].edits[0].newText == "int32_t");
    REQUIRE(result.diagnostics[0].editSetIds == std::vector<std::string>{result.edits[0].id});
}

TEST_CASE("analyze executes a package-provided external driver without tool-specific CLI code")
{
    TempDir temp{};
    const auto projectPath = temp.path() / "App/App.nginproj";
    WriteFile(temp.path() / "Workspace.ngin",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Workspace SchemaVersion="4" Name="ExternalDriverWorkspace">
  <Projects><Project Path="App/App.nginproj" /></Projects>
  <Packages><Source Name="local" Path="Packages" /></Packages>
</Workspace>
)xml");
    WriteFile(temp.path() / "Packages/Example.Tooling/Example.Tooling.nginpkg",
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Package SchemaVersion="4" Name="Example.Tooling" Version="1.0.0">
  <Tool Name="Example.Tooling.Product">
    <Exports><Tool Name="example-tool" Kind="Development" Executable="example-tool" /></Exports>
  </Tool>
  <ToolDrivers>
    <Driver Name="example-driver" Protocol="NGIN.ToolDriver/1" Executable="example-driver" Probe="true" Version="1.0.0">
      <Capabilities><Capability Name="diagnostics" /><Capability Name="fixes" /></Capabilities>
    </Driver>
  </ToolDrivers>
  <ToolActions>
    <Action Name="analyze" Kind="Analyze" Tool="example-tool" Driver="example-driver">
      <Accepts Contract="files/v1" />
      <Capabilities><Capability Name="diagnostics" /></Capabilities>
    </Action>
    <Action Name="format" Kind="Format" Tool="example-tool" Driver="example-driver">
      <Accepts Contract="files/v1" />
      <Capabilities><Capability Name="diagnostics" /><Capability Name="fixes" /></Capabilities>
    </Action>
    <Action Name="scan" Kind="Scan" Tool="example-tool" Driver="example-driver"><Accepts Contract="artifacts/v1" /></Action>
    <Action Name="report" Kind="Report" Tool="example-tool" Driver="example-driver"><Accepts Contract="tool.results/v1" /></Action>
  </ToolActions>
  <Features>
    <Feature Name="Analysis">
      <Tooling>
        <Run Name="example-analysis" Action="Example.Tooling::analyze">
          <Input Contract="files/v1" Scope="Product" />
          <Policy Gate="true" FailOn="Warning" />
        </Run>
        <Run Name="example-analysis-two" Action="Example.Tooling::analyze">
          <Input Contract="files/v1" Scope="Product" />
          <Policy Gate="true" FailOn="Warning" />
        </Run>
        <Run Name="example-format" Action="Example.Tooling::format">
          <Input Contract="files/v1" Scope="Product" />
          <Policy Gate="false" FailOn="Warning" />
          <Execution Cache="ReadWrite" />
          <Reports><Report Name="ci" Format="sarif" Path="$(OutputDir)/format.sarif" /></Reports>
        </Run>
        <Run Name="example-scan" Action="Example.Tooling::scan"><Input Contract="artifacts/v1" Scope="Product" /></Run>
        <Run Name="example-report" Action="Example.Tooling::report"><Input Contract="tool.results/v1" Scope="Product" /></Run>
      </Tooling>
    </Feature>
  </Features>
</Package>
)xml");
    WriteFile(projectPath,
              R"xml(<?xml version="1.0" encoding="utf-8"?>
<Project SchemaVersion="4" Name="ExternalDriver.App">
  <Application>
    <Uses>
      <Package Name="Example.Tooling" Version="[1.0.0]" Scope="Dev">
        <Feature Name="Analysis" />
      </Package>
    </Uses>
    <Build><Sources Path="src/**.cpp" /></Build>
  </Application>
</Project>
)xml");
    WriteFile(temp.path() / "App/src/main.cpp", "int main() { return 0; }\n");
    const auto sourceDigest = ToolFileDigest(temp.path() / "App/src/main.cpp");
    const auto tool = temp.path() / "example-tool";
    const auto driver = temp.path() / "example-driver";
    WriteFile(tool, "#!/bin/sh\nexit 0\n");
    WriteFile(driver,
              "#!/bin/sh\n"
              "request=\"$2\"\n"
              "run_id=$(sed -n 's/.*\"runId\":\"\\([^\"]*\\)\".*/\\1/p' \"$request\")\n"
              "if test \"$1\" = '--ngin-probe'; then\n"
              "  echo \"{\\\"schemaVersion\\\":\\\"1.0\\\",\\\"kind\\\":\\\"NGIN.ToolDriver.Event\\\",\\\"runId\\\":\\\"$run_id\\\",\\\"sequence\\\":1,\\\"type\\\":\\\"probe.completed\\\",\\\"data\\\":{\\\"available\\\":true,\\\"hostCompatible\\\":true,\\\"driverVersion\\\":\\\"1.0.0\\\",\\\"toolVersion\\\":\\\"2.0.0\\\",\\\"protocols\\\":[\\\"NGIN.ToolDriver/1\\\"],\\\"capabilities\\\":[\\\"diagnostics\\\",\\\"fixes\\\"]}}\"\n"
              "  exit 0\n"
              "fi\n"
              "if grep -q '\"kind\":\"Analyze\"' \"$request\"; then\n"
              "  if mkdir '" + (temp.path() / "driver-active").string() + "' 2>/dev/null; then\n"
              "    sleep 0.15\n"
              "    rmdir '" + (temp.path() / "driver-active").string() + "'\n"
              "  else\n"
              "    : > '" + (temp.path() / "driver-overlap").string() + "'\n"
              "  fi\n"
              "fi\n"
              "echo \"{\\\"schemaVersion\\\":\\\"1.0\\\",\\\"kind\\\":\\\"NGIN.ToolDriver.Event\\\",\\\"runId\\\":\\\"$run_id\\\",\\\"sequence\\\":1,\\\"type\\\":\\\"run.started\\\",\\\"data\\\":{}}\"\n"
              "echo \"{\\\"schemaVersion\\\":\\\"1.0\\\",\\\"kind\\\":\\\"NGIN.ToolDriver.Event\\\",\\\"runId\\\":\\\"$run_id\\\",\\\"sequence\\\":2,\\\"type\\\":\\\"progress\\\",\\\"data\\\":{\\\"stage\\\":\\\"processing\\\",\\\"message\\\":\\\"streamed before completion\\\",\\\"current\\\":1,\\\"total\\\":1}}\"\n"
              "echo \"{\\\"schemaVersion\\\":\\\"1.0\\\",\\\"kind\\\":\\\"NGIN.ToolDriver.Event\\\",\\\"runId\\\":\\\"$run_id\\\",\\\"sequence\\\":3,\\\"type\\\":\\\"diagnostic\\\",\\\"data\\\":{\\\"severity\\\":\\\"warning\\\",\\\"code\\\":\\\"example-rule\\\",\\\"message\\\":\\\"external finding\\\",\\\"editSetIds\\\":[\\\"format-fix\\\"]}}\"\n"
              "if grep -q '\"kind\":\"Format\"' \"$request\"; then\n"
              "  echo \"{\\\"schemaVersion\\\":\\\"1.0\\\",\\\"kind\\\":\\\"NGIN.ToolDriver.Event\\\",\\\"runId\\\":\\\"$run_id\\\",\\\"sequence\\\":4,\\\"type\\\":\\\"edit.proposed\\\",\\\"data\\\":{\\\"id\\\":\\\"format-fix\\\",\\\"label\\\":\\\"Format source\\\",\\\"applicability\\\":\\\"automatic\\\",\\\"files\\\":[{\\\"path\\\":{\\\"absolute\\\":\\\"" + (temp.path() / "App/src/main.cpp").string() + "\\\"},\\\"expectedDigest\\\":\\\"" + sourceDigest + "\\\",\\\"edits\\\":[{\\\"range\\\":{\\\"start\\\":{\\\"line\\\":1,\\\"column\\\":1},\\\"end\\\":{\\\"line\\\":1,\\\"column\\\":1}},\\\"newText\\\":\\\"// formatted \\\"}]}]}}\"\n"
              "  sequence=5\n"
              "else\n"
              "  sequence=4\n"
              "fi\n"
              "echo \"{\\\"schemaVersion\\\":\\\"1.0\\\",\\\"kind\\\":\\\"NGIN.ToolDriver.Event\\\",\\\"runId\\\":\\\"$run_id\\\",\\\"sequence\\\":$sequence,\\\"type\\\":\\\"run.completed\\\",\\\"data\\\":{\\\"status\\\":\\\"succeeded\\\"}}\"\n");
    fs::permissions(tool, fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write);
    fs::permissions(driver, fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write);
    ScopedEnvironmentVariable toolOverride{"NGIN_EXAMPLE_TOOL", tool.string()};
    ScopedEnvironmentVariable driverOverride{"NGIN_EXAMPLE_DRIVER", driver.string()};

    ParsedArgs args{};
    args.projectPath = projectPath.string();
    args.outputPath = (temp.path() / "out").string();
    args.eventOutputMode = EventOutputMode::JsonLines;
    args.toolJobs = 2;

    std::ostringstream captured{};
    auto *previous = std::cout.rdbuf(captured.rdbuf());
    const auto exitCode = CmdAnalyze(temp.path(), args);
    std::cout.rdbuf(previous);

    REQUIRE(exitCode == 1);
    REQUIRE(fs::exists(temp.path() / "driver-overlap"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("source":"example-tool")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("run":"example-analysis")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("message":"external finding")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("message":"streamed before completion")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("status":"gate-failed")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("category":"gate-failed")"));
    const auto normalizedResult = temp.path() / "out/tooling/example-analysis/result.json";
    REQUIRE(fs::exists(normalizedResult));
    REQUIRE_THAT(ReadFile(normalizedResult), ContainsSubstring(R"("kind":"NGIN.ToolResult")"));
    REQUIRE_THAT(ReadFile(normalizedResult), ContainsSubstring(R"("gateStatus":"failed")"));
    REQUIRE_THAT(ReadFile(normalizedResult), ContainsSubstring(R"("durationMs":)"));

    ParsedArgs resultArgs{};
    resultArgs.projectPath = projectPath.string();
    resultArgs.outputPath = (temp.path() / "out").string();
    resultArgs.toolRunName = "example-analysis";
    resultArgs.format = "json";
    captured.str({});
    captured.clear();
    previous = std::cout.rdbuf(captured.rdbuf());
    const auto resultsExitCode = CmdToolResults(temp.path(), resultArgs);
    std::cout.rdbuf(previous);
    REQUIRE(resultsExitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("kind":"NGIN.ToolResults")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("run":"example-analysis")"));

    captured.str({});
    captured.clear();
    previous = std::cout.rdbuf(captured.rdbuf());
    const auto editsExitCode = CmdToolEdits(temp.path(), resultArgs);
    std::cout.rdbuf(previous);
    REQUIRE(editsExitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("kind":"NGIN.ToolEdits")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("edits":[])"));

    ParsedArgs listArgs{};
    listArgs.projectPath = projectPath.string();
    listArgs.format = "json";
    listArgs.toolListAvailable = true;
    captured.str({});
    captured.clear();
    previous = std::cout.rdbuf(captured.rdbuf());
    const auto listExitCode = CmdToolList(temp.path(), listArgs);
    std::cout.rdbuf(previous);
    REQUIRE(listExitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("kind":"NGIN.ToolList")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("name":"example-analysis")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("actionKind":"Analyze")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("actionKind":"Format")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("availableActions")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("identity":"Example.Tooling::format")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("identity":"Example.Tooling::scan")"));

    captured.str({});
    captured.clear();
    previous = std::cout.rdbuf(captured.rdbuf());
    const auto doctorExitCode = CmdToolDoctor(temp.path(), listArgs);
    std::cout.rdbuf(previous);
    REQUIRE(doctorExitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("kind":"NGIN.ToolDoctor")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("healthy":true)"));

    listArgs.toolRunName = "example-analysis";
    captured.str({});
    captured.clear();
    previous = std::cout.rdbuf(captured.rdbuf());
    const auto focusedDoctorExitCode = CmdToolDoctor(temp.path(), listArgs);
    std::cout.rdbuf(previous);
    REQUIRE(focusedDoctorExitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("run":"example-analysis")"));
    REQUIRE_THAT(captured.str(), !ContainsSubstring(R"("run":"example-format")"));
    listArgs.toolRunName.reset();

    args.toolRunName = "example-analysis";
    captured.str({});
    captured.clear();
    previous = std::cout.rdbuf(captured.rdbuf());
    const auto runExitCode = CmdToolRun(temp.path(), args);
    std::cout.rdbuf(previous);
    REQUIRE(runExitCode == 1);
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("run":"example-analysis")"));

    args.toolRunName.reset();
    args.toolActionKind = "Format";
    args.toolCommandName = "format";
    captured.str({});
    captured.clear();
    previous = std::cout.rdbuf(captured.rdbuf());
    const auto formatExitCode = CmdAnalyze(temp.path(), args);
    std::cout.rdbuf(previous);
    INFO(captured.str());
    REQUIRE(formatExitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("run":"example-format")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("editSetId":"format-fix")"));
    const auto sarif = temp.path() / "out/tooling/example-format/format.sarif";
    REQUIRE(fs::exists(sarif));
    REQUIRE_THAT(ReadFile(sarif), ContainsSubstring(R"("version":"2.1.0")"));
    REQUIRE(NGIN::Serialization::JsonParser::Parse(ReadFile(sarif)).HasValue());
    REQUIRE_THAT(ReadFile(sarif), ContainsSubstring(R"("fixes":[)"));
    REQUIRE_THAT(ReadFile(sarif), ContainsSubstring(R"("artifacts":[)"));

    args.toolActionKind.reset();
    args.toolCommandName.reset();
    args.toolRunName = "example-format";
    captured.str({});
    captured.clear();
    previous = std::cout.rdbuf(captured.rdbuf());
    const auto formatRunExitCode = CmdToolRun(temp.path(), args);
    std::cout.rdbuf(previous);
    REQUIRE(formatRunExitCode == 0);
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("kind":"tool-cache")"));
    REQUIRE_THAT(captured.str(), ContainsSubstring(R"("status":"hit")"));

    for (const auto &[kind, runName] : std::vector<std::pair<std::string, std::string>>{
             {"Scan", "example-scan"}, {"Report", "example-report"}}) {
        args.toolRunName.reset();
        args.toolActionKind = kind;
        args.toolCommandName = kind == "Scan" ? "scan" : "report";
        captured.str({});
        captured.clear();
        previous = std::cout.rdbuf(captured.rdbuf());
        const auto semanticExitCode = CmdAnalyze(temp.path(), args);
        std::cout.rdbuf(previous);
        REQUIRE(semanticExitCode == 0);
        REQUIRE_THAT(captured.str(), ContainsSubstring("\"run\":\"" + runName + "\""));
        if (kind == "Scan") {
            REQUIRE_THAT(ReadFile(temp.path() / "out/tooling/example-scan/request.json"),
                         ContainsSubstring(R"("role":"Artifact")"));
        }
        if (kind == "Report") {
            REQUIRE_THAT(ReadFile(temp.path() / "out/tooling/example-report/request.json"),
                         ContainsSubstring(R"("priorResultPaths":[{")"));
        }
    }
}
#endif
