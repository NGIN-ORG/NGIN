#include "Commands.hpp"

#include "Authoring.hpp"
#include "Build.hpp"
#include "Diagnostics.hpp"
#include "Overlay.hpp"
#include "Resolution.hpp"
#include "Support.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace NGIN::CLI
{
    namespace
    {
        struct LoadedInvocation
        {
            ProjectManifest project{};
            ProfileDefinition profile{};
            std::optional<std::string> configurationName{};
            std::optional<std::string> configurationSource{};
        };

        [[nodiscard]] auto IsQuiet(const ParsedArgs &args) -> bool
        {
            return args.verbosity == OutputVerbosity::Quiet || args.eventOutputMode == EventOutputMode::JsonLines;
        }

        [[nodiscard]] auto IsVerbose(const ParsedArgs &args) -> bool
        {
            if (args.eventOutputMode == EventOutputMode::JsonLines)
            {
                return false;
            }
            return args.verbosity == OutputVerbosity::Verbose || args.verbosity == OutputVerbosity::Trace;
        }

        [[nodiscard]] auto UseColor(const ParsedArgs &args) -> bool
        {
            if (args.colorMode == OutputColorMode::Never)
            {
                return false;
            }
            if (args.colorMode == OutputColorMode::Always)
            {
                return true;
            }
            if (std::getenv("NO_COLOR") != nullptr)
            {
                return false;
            }
#ifndef _WIN32
            return isatty(fileno(stdout)) != 0;
#else
            return false;
#endif
        }

        [[nodiscard]] auto IsInteractiveTerminal() -> bool
        {
#ifndef _WIN32
            return isatty(fileno(stdout)) != 0;
#else
            return false;
#endif
        }

        [[nodiscard]] auto Style(const ParsedArgs &args, std::string_view code, std::string_view text) -> std::string
        {
            if (!UseColor(args))
            {
                return std::string{text};
            }
            return "\033[" + std::string{code} + "m" + std::string{text} + "\033[0m";
        }

        auto PrintTitle(const ParsedArgs &args, std::string_view title) -> void
        {
            if (IsQuiet(args))
            {
                return;
            }
            std::cout << Style(args, "1;36", title) << "\n";
        }

        auto PrintSection(const ParsedArgs &args, std::string_view title) -> void
        {
            if (IsQuiet(args))
            {
                return;
            }
            std::cout << "\n" << Style(args, "1", title) << "\n";
        }

        auto PrintField(const ParsedArgs &args, std::string_view name, const std::string &value) -> void
        {
            if (IsQuiet(args))
            {
                return;
            }
            std::cout << "  " << Style(args, "2", name) << "  " << value << "\n";
        }

        template <typename Value>
        auto PrintField(const ParsedArgs &args, std::string_view name, const Value &value) -> void
        {
            std::ostringstream out{};
            out << value;
            PrintField(args, name, out.str());
        }

        auto PrintItem(const ParsedArgs &args, const std::string &label, const std::string &detail = {}) -> void
        {
            if (IsQuiet(args))
            {
                return;
            }
            std::cout << "  - " << label;
            if (!detail.empty())
            {
                std::cout << "  " << detail;
            }
            std::cout << "\n";
        }

        auto PrintSuccess(const ParsedArgs &args, std::string_view text) -> void
        {
            if (IsQuiet(args))
            {
                return;
            }
            std::cout << "\n" << Style(args, "32", text) << "\n";
        }

        auto PrintFailure(const ParsedArgs &args, std::string_view text) -> void
        {
            if (IsQuiet(args))
            {
                return;
            }
            std::cout << "\n" << Style(args, "31", text) << "\n";
        }

        [[nodiscard]] auto BuildOptionsForArgs(const ParsedArgs &args, std::vector<BackendStepResult> &backendSteps,
                                               CliEventEmitter *events = nullptr) -> BuildExecutionOptions
        {
            return BuildExecutionOptions{
                .backendOutput =
                    args.verbosity == OutputVerbosity::Quiet && args.eventOutputMode != EventOutputMode::JsonLines
                        ? BackendOutputMode::Silent
                        : args.backendOutputMode,
                .backendSteps = &backendSteps,
                .interactiveProgress = args.backendOutputMode == BackendOutputMode::Compact && !IsQuiet(args) &&
                                       args.colorMode != OutputColorMode::Never && IsInteractiveTerminal(),
                .events = events != nullptr && events->Enabled() ? events : nullptr,
            };
        }

        [[nodiscard]] auto FormatDuration(int milliseconds) -> std::string
        {
            std::ostringstream out{};
            out.setf(std::ios::fixed);
            out.precision(milliseconds >= 10'000 ? 0 : 1);
            out << static_cast<double>(milliseconds) / 1000.0 << "s";
            return out.str();
        }

        auto PrintBackendSteps(const ParsedArgs &args, const std::vector<BackendStepResult> &steps, bool includeOutput)
            -> void
        {
            if (IsQuiet(args) || steps.empty())
            {
                return;
            }
            PrintSection(args, "Backend");
            for (const auto &step : steps)
            {
                const auto status = step.exitCode == 0 ? "complete" : "failed";
                PrintItem(args, step.name + " " + status, FormatDuration(step.durationMilliseconds));
            }
            if (!includeOutput)
            {
                return;
            }
            bool anyOutput = false;
            for (const auto &step : steps)
            {
                if (!step.output.empty())
                {
                    anyOutput = true;
                    break;
                }
            }
            if (!anyOutput)
            {
                return;
            }
            PrintSection(args, "Backend output");
            for (const auto &step : steps)
            {
                if (step.output.empty())
                {
                    continue;
                }
                std::cout << "  [" << step.name << "]\n";
                std::istringstream lines{step.output};
                std::string line{};
                while (std::getline(lines, line))
                {
                    std::cout << "    " << line << "\n";
                }
            }
        }

        [[nodiscard]] auto CommandDisplayName(std::string command) -> std::string
        {
            if (command == "package pack")
            {
                return "Package pack";
            }
            if (command == "package lock")
            {
                return "Package lock";
            }
            if (command == "package verify-lock")
            {
                return "Package lock verification";
            }
            if (!command.empty())
            {
                command[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(command[0])));
            }
            return command;
        }

        class HumanCliEventSink final : public ICliEventSink
        {
          public:
            HumanCliEventSink(const ParsedArgs &args, std::string command)
                : args_(args), command_(std::move(command)), displayName_(CommandDisplayName(command_))
            {
            }

            auto Emit(const CliEvent &event) -> void override
            {
                if (args_.verbosity == OutputVerbosity::Quiet)
                {
                    return;
                }
                switch (event.type)
                {
                case CliEventType::CommandSelection:
                    PrintTitle(args_, "NGIN " + command_);
                    PrintField(args_, "product", event.project);
                    PrintField(args_, "profile", event.profile);
                    if (const auto configuration = event.data.String("configuration"); configuration.has_value())
                    {
                        PrintField(args_, "configuration", *configuration);
                    }
                    break;
                case CliEventType::PhaseCompleted:
                    RecordPhase(event, 0);
                    break;
                case CliEventType::PhaseFailed:
                    RecordPhase(event, static_cast<int>(event.data.Number("exitCode").value_or(1)));
                    failed_ = true;
                    break;
                case CliEventType::BackendOutput:
                    RecordBackendOutput(event);
                    break;
                case CliEventType::Diagnostic:
                    if (command_ != "analyze")
                    {
                        diagnostics_.push_back(event);
                    }
                    break;
                case CliEventType::Summary:
                    PrintSummary(event);
                    break;
                case CliEventType::CommandCompleted:
                    PrintCompletion(event);
                    break;
                default:
                    break;
                }
            }

          private:
            auto RecordPhase(const CliEvent &event, int exitCode) -> void
            {
                steps_.push_back(BackendStepResult{
                    .name = event.data.String("label").value_or(event.data.String("phase").value_or("phase")),
                    .exitCode = exitCode,
                    .durationMilliseconds = static_cast<int>(event.data.Number("durationMs").value_or(0)),
                    .output = {},
                });
            }

            auto RecordBackendOutput(const CliEvent &event) -> void
            {
                const auto text = event.data.String("text").value_or("");
                if (text.empty())
                {
                    return;
                }
                const auto phase = event.data.String("phase").value_or("");
                if (args_.backendOutputMode == BackendOutputMode::Stream || phase == "run")
                {
                    std::cout << text << std::flush;
                    return;
                }
                auto stepIt = std::find_if(steps_.rbegin(), steps_.rend(), [&](const BackendStepResult &step) {
                    return Lower(step.name).find(phase) != std::string::npos;
                });
                if (stepIt != steps_.rend())
                {
                    stepIt->output += text;
                }
                else
                {
                    backendOutput_.push_back(text);
                }
            }

            auto PrintSummary(const CliEvent &event) -> void
            {
                if (command_ == "configure")
                {
                    PrintField(args_, "output", event.data.String("output").value_or(""));
                    if (event.data.Bool("configured").value_or(false))
                    {
                        if (const auto buildDir = event.data.String("buildDir"); buildDir.has_value())
                        {
                            PrintField(args_, "build dir", *buildDir);
                        }
                        if (const auto compileDb = event.data.String("compileDatabase"); compileDb.has_value())
                        {
                            PrintField(args_, "compile db", *compileDb);
                        }
                    }
                    else
                    {
                        PrintField(args_, "native build", "(none)");
                    }
                    return;
                }

                if (command_ == "build" || command_ == "stage" || command_ == "rebuild" || command_ == "run" ||
                    command_ == "test" || command_ == "benchmark")
                {
                    PrintOptionalField(event, "output", "output");
                    PrintOptionalField(event, "launch", "launch");
                    const auto executable = event.data.String("executable").value_or("");
                    PrintField(args_, "executable", executable.empty() ? "(none)" : executable);
                    return;
                }

                if (command_ == "publish")
                {
                    PrintOptionalField(event, "publish", "publish");
                    PrintOptionalField(event, "kind", "kind");
                    PrintOptionalField(event, "format", "format");
                    PrintOptionalField(event, "output", "output");
                    return;
                }

                if (command_ == "restore")
                {
                    PrintOptionalField(event, "store", "store");
                    PrintOptionalField(event, "lock", "lock");
                    PrintOptionalField(event, "locked", "locked");
                    if (const auto packages = event.data.Number("packages"); packages.has_value())
                    {
                        PrintField(args_, "packages", *packages);
                    }
                    return;
                }

                if (command_ == "package pack")
                {
                    PrintOptionalField(event, "project", "project");
                    PrintOptionalField(event, "package", "package");
                    PrintOptionalField(event, "version", "version");
                    PrintOptionalField(event, "manifest", "manifest");
                    PrintOptionalField(event, "archive", "archive");
                    return;
                }

                if (command_ == "package lock")
                {
                    PrintOptionalField(event, "path", "path");
                    if (const auto packages = event.data.Number("packages"); packages.has_value())
                    {
                        PrintField(args_, "packages", *packages);
                    }
                    if (const auto features = event.data.Number("features"); features.has_value())
                    {
                        PrintField(args_, "features", *features);
                    }
                    return;
                }

                if (command_ == "analyze")
                {
                    if (const auto sources = event.data.Number("sources"); sources.has_value())
                    {
                        PrintField(args_, "sources", *sources == 0 ? "(none)" : std::to_string(*sources));
                    }
                }
            }

            auto PrintOptionalField(const CliEvent &event, std::string_view eventName, std::string_view label) -> void
            {
                if (const auto value = event.data.String(eventName); value.has_value() && !value->empty())
                {
                    PrintField(args_, label, *value);
                }
            }

            auto PrintCompletion(const CliEvent &event) -> void
            {
                const auto status = event.data.String("status").value_or(failed_ ? "failed" : "success");
                const auto includeOutput = status != "success";
                PrintBackendSteps(args_, steps_, includeOutput);
                if (includeOutput && !backendOutput_.empty())
                {
                    PrintSection(args_, "Backend output");
                    for (const auto &output : backendOutput_)
                    {
                        std::istringstream lines{output};
                        std::string line{};
                        while (std::getline(lines, line))
                        {
                            std::cout << "    " << line << "\n";
                        }
                    }
                }
                PrintDiagnostics();
                if (status == "success")
                {
                    PrintSuccess(args_, displayName_ + " complete");
                }
                else
                {
                    PrintFailure(args_, displayName_ + " failed");
                }
            }

            auto PrintDiagnostics() -> void
            {
                if (diagnostics_.empty())
                {
                    return;
                }
                PrintSection(args_, displayName_ + " diagnostics");
                for (const auto &event : diagnostics_)
                {
                    const auto severity = event.data.String("severity").value_or("error");
                    const auto message = event.data.String("message").value_or("");
                    const auto subject = event.data.String("subject").value_or("");
                    if (message.empty())
                    {
                        continue;
                    }
                    std::cout << "  - ";
                    if (!subject.empty())
                    {
                        std::cout << subject << ": ";
                    }
                    std::cout << severity << ": " << message << "\n";
                }
            }

            const ParsedArgs &args_;
            std::string command_{};
            std::string displayName_{};
            std::vector<BackendStepResult> steps_{};
            std::vector<std::string> backendOutput_{};
            std::vector<CliEvent> diagnostics_{};
            bool failed_{false};
        };

        class CommandEventSession
        {
          public:
            CommandEventSession(const ParsedArgs &args, std::string command)
                : args_(args), command_(std::move(command)), jsonSink_(std::cout), humanSink_(args_, command_),
                  events_(&composite_)
            {
                if (args_.eventOutputMode == EventOutputMode::JsonLines)
                {
                    composite_.Add(jsonSink_);
                }
                else
                {
                    composite_.Add(humanSink_);
                }
                events_.SetCommand(command_);
            }

            [[nodiscard]] auto Events() -> CliEventEmitter &
            {
                return events_;
            }

          private:
            const ParsedArgs &args_;
            std::string command_{};
            JsonLinesCliEventSink jsonSink_;
            HumanCliEventSink humanSink_;
            CompositeCliEventSink composite_{};
            CliEventEmitter events_;
        };

        [[nodiscard]] auto CommandDurationMilliseconds(std::chrono::steady_clock::time_point started) -> std::int64_t
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started)
                .count();
        }

        auto EmitCommandStarted(CliEventEmitter &events, const ParsedArgs &args) -> void
        {
            EventData data{};
            data.AddStringArray("argv", args.argv);
            data.AddString("workingDirectory", fs::current_path().string());
            events.Emit(CliEventType::CommandStarted, std::move(data));
        }

        auto EmitCommandCompleted(CliEventEmitter &events, std::string status, int exitCode,
                                  std::chrono::steady_clock::time_point started) -> int
        {
            events.Emit(CliEventType::CommandCompleted,
                        EventData{}
                            .AddString("status", std::move(status))
                            .AddNumber("exitCode", exitCode)
                            .AddNumber("durationMs", CommandDurationMilliseconds(started)));
            return exitCode;
        }

        auto EmitSelection(CliEventEmitter &events, const LoadedInvocation &invocation) -> void
        {
            const auto &project = invocation.project;
            const auto &profile = invocation.profile;
            events.SetSelection(project.name, profile.name);
            EventData data{};
            data.AddString("projectPath", project.path.string())
                .AddString("productKind", project.productKind)
                .AddString("hostPlatform", profile.hostPlatform)
                .AddString("targetPlatform", profile.platform)
                .AddString("buildType", profile.buildType)
                .AddString("toolchain", profile.toolchain);
            if (invocation.configurationName.has_value())
            {
                data.AddString("configuration", *invocation.configurationName);
            }
            if (invocation.configurationSource.has_value())
            {
                data.AddString("configurationSource", *invocation.configurationSource);
            }
            events.Emit(CliEventType::CommandSelection, std::move(data));
        }

        auto EmitDiagnostics(CliEventEmitter &events, const DiagnosticReport &report, std::string source) -> void
        {
            for (const auto &entry : report.entries)
            {
                EventData data{};
                data.AddString("severity", entry.severity == DiagnosticSeverity::Warning ? "warning" : "error")
                    .AddString("source", source)
                    .AddString("message", entry.message);
                if (!entry.subject.empty())
                {
                    data.AddString("subject", entry.subject);
                }
                events.Emit(CliEventType::Diagnostic, std::move(data));
            }
        }

        auto EmitConfiguredArtifacts(CliEventEmitter &events, const ConfiguredBuildPaths &configured) -> void
        {
            events.Emit(
                CliEventType::ArtifactProduced,
                EventData{}.AddString("kind", "stage-directory").AddString("path", configured.outputDir.string()));
            if (configured.compileCommandsPath.has_value())
            {
                events.Emit(CliEventType::ArtifactProduced,
                            EventData{}
                                .AddString("kind", "compile-database")
                                .AddString("path", configured.compileCommandsPath->string()));
            }
            EventData summary{};
            summary.AddString("output", configured.outputDir.string()).AddBool("configured", configured.configured);
            if (configured.buildDir.has_value())
            {
                summary.AddString("buildDir", configured.buildDir->string());
            }
            if (configured.compileCommandsPath.has_value())
            {
                summary.AddString("compileDatabase", configured.compileCommandsPath->string());
            }
            events.Emit(CliEventType::Summary, std::move(summary));
        }

        auto EmitBuildArtifactsAndSummary(CliEventEmitter &events, const ProjectManifest &project,
                                          const ProfileDefinition &profile, const GeneratedLaunchPaths &built,
                                          const LaunchManifestSummary &summary) -> void
        {
            events.Emit(CliEventType::ArtifactProduced,
                        EventData{}.AddString("kind", "stage-directory").AddString("path", built.outputDir.string()));
            events.Emit(
                CliEventType::ArtifactProduced,
                EventData{}.AddString("kind", "launch-manifest").AddString("path", built.manifestPath.string()));
            if (summary.selectedExecutable.has_value() && !summary.selectedExecutable->empty())
            {
                events.Emit(CliEventType::ArtifactProduced,
                            EventData{}
                                .AddString("kind", "executable")
                                .AddString("name", *summary.selectedExecutable)
                                .AddString("path", (built.outputDir / "bin" / *summary.selectedExecutable).string()));
            }

            EventData data{};
            data.AddString("output", built.outputDir.string())
                .AddString("launch", built.manifestPath.string())
                .AddString("executable", summary.selectedExecutable.value_or(""))
                .AddNumber("packages", 0)
                .AddNumber("sources", 0)
                .AddNumber("headers", 0)
                .AddNumber("stagedFiles", 0);

            const auto resolvedResult = ResolveLaunch(project, profile);
            if (resolvedResult.value.has_value() && !resolvedResult.diagnostics.HasErrors())
            {
                std::size_t sources = 0;
                std::size_t headers = 0;
                std::size_t staged = 0;
                for (const auto &input : resolvedResult.value->inputs)
                {
                    const auto kind = Lower(input.kind);
                    const auto role = Lower(input.role);
                    if (kind == "source" || role == "source")
                    {
                        ++sources;
                    }
                    else if (kind == "header" || role == "header")
                    {
                        ++headers;
                    }
                    if (!input.stagedRelativePath.empty())
                    {
                        ++staged;
                    }
                }
                data = EventData{};
                data.AddString("output", built.outputDir.string())
                    .AddString("launch", built.manifestPath.string())
                    .AddString("executable", summary.selectedExecutable.value_or(""))
                    .AddNumber("packages", static_cast<std::int64_t>(resolvedResult.value->orderedPackages.size()))
                    .AddNumber("sources", static_cast<std::int64_t>(sources))
                    .AddNumber("headers", static_cast<std::int64_t>(headers))
                    .AddNumber("stagedFiles", static_cast<std::int64_t>(staged));
            }
            events.Emit(CliEventType::Summary, std::move(data));
        }

        struct InputSummary
        {
            std::size_t sources{};
            std::size_t headers{};
            std::size_t config{};
            std::size_t content{};
            std::size_t staged{};
        };

        [[nodiscard]] auto SummarizeInputs(const std::vector<ResolvedInput> &inputs) -> InputSummary
        {
            InputSummary summary{};
            for (const auto &input : inputs)
            {
                const auto kind = Lower(input.kind);
                const auto role = Lower(input.role);
                const auto contentKind = Lower(input.contentKind);
                if (kind == "source" || role == "source")
                {
                    ++summary.sources;
                }
                else if (kind == "header" || role == "header")
                {
                    ++summary.headers;
                }
                else if (kind == "config" || contentKind == "config")
                {
                    ++summary.config;
                }
                else if (kind == "content" || contentKind == "content" || contentKind == "asset")
                {
                    ++summary.content;
                }
                if (!input.stagedRelativePath.empty())
                {
                    ++summary.staged;
                }
            }
            return summary;
        }

        auto PrintVerboseResolvedDetails(const ParsedArgs &args, const ProjectManifest &project,
                                         const ProfileDefinition &profile, const fs::path &outputDir) -> void
        {
            if (!IsVerbose(args))
            {
                return;
            }
            const auto resolvedResult = ResolveLaunch(project, profile);
            if (!resolvedResult.value.has_value() || resolvedResult.diagnostics.HasErrors())
            {
                return;
            }
            const auto &resolved = *resolvedResult.value;
            const auto inputSummary = SummarizeInputs(resolved.inputs);

            PrintSection(args, "Selection");
            PrintField(args, "product kind", resolved.project.productKind);
            PrintField(args, "build type", resolved.profile.buildType);
            PrintField(args, "host platform", resolved.profile.hostPlatform);
            PrintField(args, "target platform", resolved.profile.platform);
            if (!resolved.profile.toolchain.empty())
            {
                PrintField(args, "toolchain", resolved.profile.toolchain);
            }
            if (!resolved.targetAbiTag.empty())
            {
                PrintField(args, "abi", resolved.targetAbiTag);
            }

            PrintSection(args, "Inputs");
            PrintField(args, "sources", inputSummary.sources);
            PrintField(args, "headers", inputSummary.headers);
            PrintField(args, "config", inputSummary.config);
            PrintField(args, "content", inputSummary.content);
            PrintField(args, "staged", inputSummary.staged);

            PrintSection(args, "Graph");
            PrintField(args, "packages", resolved.orderedPackages.size());
            PrintField(args, "features", resolved.selectedPackageFeatures.size());
            PrintField(args, "generators", resolved.generators.size());
            PrintField(args, "libraries", resolved.libraries.size());
            PrintField(args, "executables", resolved.executables.size());
            PrintField(args, "environment", resolved.environmentVariables.size());
            PrintField(args, "runtime modules", resolved.requiredModules.size() + resolved.optionalModules.size());

            if (!resolved.orderedPackages.empty())
            {
                PrintSection(args, "Packages");
                for (const auto &package : resolved.orderedPackages)
                {
                    std::ostringstream detail{};
                    detail << package.manifest.version;
                    if (const auto scope = resolved.packageScopes.find(package.manifest.name);
                        scope != resolved.packageScopes.end() && !scope->second.empty())
                    {
                        detail << " scope=" << scope->second;
                    }
                    detail << " source=" << package.source;
                    PrintItem(args, package.manifest.name, detail.str());
                }
            }

            if (!resolved.generators.empty())
            {
                PrintSection(args, "Generators");
                for (const auto &generator : resolved.generators)
                {
                    std::ostringstream detail{};
                    detail << "owner=" << generator.ownerKind << ":" << generator.ownerName;
                    if (!generator.declaration.kind.empty())
                    {
                        detail << " kind=" << generator.declaration.kind;
                    }
                    PrintItem(args, generator.declaration.name, detail.str());
                }
            }

            PrintSection(args, "Backend paths");
            PrintField(args, "output", outputDir);
            PrintField(args, "build dir", outputDir / ".ngin" / "cmake-build");
            PrintField(args, "generated dir", outputDir / ".ngin" / "generated");
        }

        [[nodiscard]] auto SelectLaunch(const ProjectManifest &project, const ProfileDefinition &profile,
                                        const std::optional<std::string> &requestedName) -> LaunchDefinition
        {
            const auto launches = EffectiveLaunches(project, profile);
            const auto findLaunch = [&](const std::string &name) -> const LaunchDefinition * {
                const auto it = std::find_if(launches.begin(), launches.end(),
                                             [&](const LaunchDefinition &launch) { return launch.name == name; });
                return it == launches.end() ? nullptr : &*it;
            };

            if (requestedName.has_value())
            {
                if (const auto *launch = findLaunch(*requestedName); launch != nullptr)
                {
                    return *launch;
                }
                throw std::runtime_error("profile '" + profile.name + "' does not declare launch '" + *requestedName +
                                         "'");
            }
            if (!profile.launch.name.empty())
            {
                if (const auto *launch = findLaunch(profile.launch.name); launch != nullptr)
                {
                    return *launch;
                }
            }
            if (const auto *launch = findLaunch("default"); launch != nullptr)
            {
                return *launch;
            }
            if (launches.size() == 1)
            {
                return launches.front();
            }
            if (launches.empty())
            {
                return profile.launch;
            }
            throw std::runtime_error("profile '" + profile.name +
                                     "' has multiple launch entries; pass --launch <name>");
        }

        [[nodiscard]] auto HasEffectiveProfile(const ProjectManifest &project,
                                               const std::optional<WorkspaceManifest> &workspace,
                                               const std::string &name) -> bool
        {
            const auto hasProjectProfile =
                std::any_of(project.profiles.begin(), project.profiles.end(),
                            [&](const ProfileDefinition &profile) { return profile.name == name; });
            if (hasProjectProfile)
            {
                return true;
            }
            return workspace.has_value() &&
                   std::any_of(workspace->profiles.begin(), workspace->profiles.end(),
                               [&](const WorkspaceManifest::ProfilePolicy &profile) { return profile.name == name; });
        }

        [[nodiscard]] auto SupportedBuildTypesForMessage() -> std::string
        {
            return "Debug, Release, RelWithDebInfo, or MinSizeRel";
        }

        [[nodiscard]] auto ResolveInvocation(const ParsedArgs &args) -> LoadedInvocation
        {
            auto project = LoadProjectManifest(ResolveProjectPath(args.projectPath));
            std::optional<WorkspaceManifest> workspace{};
            if (const auto workspaceRoot = RootDirFrom(project.path.parent_path()); workspaceRoot.has_value())
            {
                workspace = TryLoadWorkspaceManifest(*workspaceRoot);
            }
            project = ProjectWithWorkspacePolicy(std::move(project), workspace);
            std::optional<std::string> selectedProfile = args.profileName;
            std::optional<std::string> configurationSource{};
            if (args.configurationName.has_value())
            {
                if (!IsSupportedBuildType(*args.configurationName))
                {
                    throw std::runtime_error("unknown configuration '" + *args.configurationName + "'; expected " +
                                             SupportedBuildTypesForMessage() +
                                             ". Use --profile for custom product scenarios.");
                }
                if (selectedProfile.has_value())
                {
                    configurationSource = "buildTypeOverride";
                }
                else if (HasEffectiveProfile(project, workspace, *args.configurationName))
                {
                    selectedProfile = args.configurationName;
                    configurationSource = "profile";
                }
                else
                {
                    configurationSource = "buildTypeOverride";
                }
            }
            if (!selectedProfile.has_value())
            {
                if (workspace.has_value() && !workspace->defaultProfile.empty())
                {
                    if (HasEffectiveProfile(project, workspace, workspace->defaultProfile))
                    {
                        selectedProfile = workspace->defaultProfile;
                    }
                }
            }
            auto profile = ProfileWithWorkspacePolicy(project, workspace, selectedProfile);
            if (args.configurationName.has_value() && configurationSource.has_value() &&
                *configurationSource == "buildTypeOverride")
            {
                profile.buildType = *args.configurationName;
            }
            profile.launch = SelectLaunch(project, profile, args.launchName);
            return LoadedInvocation{
                .project = project,
                .profile = std::move(profile),
                .configurationName = args.configurationName,
                .configurationSource = std::move(configurationSource),
            };
        }

        [[nodiscard]] auto ReadTextIfExists(const fs::path &path) -> std::string
        {
            std::ifstream input(path);
            if (!input)
            {
                return {};
            }
            std::ostringstream content{};
            content << input.rdbuf();
            return content.str();
        }

        [[nodiscard]] auto EscapeXml(const std::string &value) -> std::string
        {
            std::string escaped{};
            for (const char ch : value)
            {
                switch (ch)
                {
                case '&':
                    escaped += "&amp;";
                    break;
                case '<':
                    escaped += "&lt;";
                    break;
                case '>':
                    escaped += "&gt;";
                    break;
                case '"':
                    escaped += "&quot;";
                    break;
                case '\'':
                    escaped += "&apos;";
                    break;
                default:
                    escaped += ch;
                    break;
                }
            }
            return escaped;
        }

        auto WriteTextFile(const fs::path &path, const std::string &contents) -> void;

        [[nodiscard]] auto DefaultLockPath(const ResolvedLaunch &resolved) -> fs::path
        {
            if (resolved.workspace.has_value())
            {
                return resolved.workspace->path.parent_path() / "ngin.lock";
            }
            return resolved.project.path.parent_path() / "ngin.lock";
        }

        [[nodiscard]] auto DefaultPackageStorePath(const ResolvedLaunch &resolved) -> fs::path
        {
            if (resolved.workspace.has_value())
            {
                return resolved.workspace->path.parent_path() / ".ngin" / "packages";
            }
            return resolved.project.path.parent_path() / ".ngin" / "packages";
        }

        [[nodiscard]] auto SanitizePathComponent(std::string value) -> std::string
        {
            for (auto &ch : value)
            {
                const auto valid = std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-' || ch == '.';
                if (!valid)
                {
                    ch = '_';
                }
            }
            return value.empty() ? "default" : value;
        }

        [[nodiscard]] auto ProviderStateRoot(const ResolvedLaunch &resolved, std::string_view providerName) -> fs::path
        {
            const auto root = resolved.workspace.has_value() ? resolved.workspace->path.parent_path()
                                                             : resolved.project.path.parent_path();
            return root / ".ngin" / "providers" / SanitizePathComponent(std::string{providerName}) /
                   SanitizePathComponent(resolved.profile.name);
        }

        [[nodiscard]] auto ProviderMetadataPath(const ResolvedLaunch &resolved, std::string_view providerName) -> fs::path
        {
            return ProviderStateRoot(resolved, providerName) / "ngin-provider.xml";
        }

        [[nodiscard]] auto ProviderPackageName(const PackageManifest &manifest) -> std::string
        {
            return manifest.build.providerPackage.empty() ? manifest.name : manifest.build.providerPackage;
        }

        [[nodiscard]] auto ProviderPackageVersion(const PackageManifest &manifest) -> std::string
        {
            return manifest.build.providerVersion.empty() ? manifest.version : manifest.build.providerVersion;
        }

        [[nodiscard]] auto CMakePackageName(const PackageManifest &manifest) -> std::string
        {
            return manifest.build.cmakePackage.empty() ? manifest.name : manifest.build.cmakePackage;
        }

        [[nodiscard]] auto ProviderToolExecutable(const WorkspaceManifest::PackageProvider &provider) -> fs::path
        {
            const auto executableName = Lower(provider.kind) == "vcpkg" ? std::string{"vcpkg"} : std::string{"conan"};
            if (!provider.root.empty())
            {
#if defined(_WIN32)
                const auto rooted = provider.root / (executableName + ".exe");
#else
                const auto rooted = provider.root / executableName;
#endif
                if (fs::exists(rooted))
                {
                    return rooted;
                }
            }
            if (const auto resolved = ResolveToolPath(executableName, provider.root.empty() ? std::nullopt
                                                                                            : std::optional<fs::path>{provider.root});
                resolved.has_value())
            {
                return resolved->path;
            }
            return {};
        }

        [[nodiscard]] auto VcpkgRootForProvider(const WorkspaceManifest::PackageProvider &provider) -> fs::path
        {
            if (!provider.root.empty())
            {
                return provider.root;
            }
            if (const auto *root = std::getenv("VCPKG_ROOT"); root != nullptr && root[0] != '\0')
            {
                return fs::path{root};
            }
            return {};
        }

        auto EmitRestoreFailure(CliEventEmitter &events, std::chrono::steady_clock::time_point restoreStarted,
                                std::chrono::steady_clock::time_point commandStarted, std::string message,
                                std::string subject = {}) -> int
        {
            events.Emit(CliEventType::Diagnostic,
                        EventData{}
                            .AddString("severity", "error")
                            .AddString("source", "ngin restore")
                            .AddString("message", std::move(message))
                            .AddString("subject", std::move(subject)));
            events.Emit(CliEventType::PhaseFailed,
                        EventData{}
                            .AddString("phase", "restore")
                            .AddString("label", "Package restore")
                            .AddNumber("durationMs", CommandDurationMilliseconds(restoreStarted))
                            .AddNumber("exitCode", 1));
            return EmitCommandCompleted(events, "failed", 1, commandStarted);
        }

        struct ProviderRestoreGroup
        {
            WorkspaceManifest::PackageProvider provider{};
            std::vector<const ResolvedPackage *> packages{};
        };

        [[nodiscard]] auto CollectProviderRestoreGroups(const ResolvedLaunch &resolved,
                                                        std::string &error) -> std::vector<ProviderRestoreGroup>
        {
            std::vector<ProviderRestoreGroup> groups{};
            std::unordered_map<std::string, std::size_t> indexByProvider{};
            for (const auto &package : resolved.orderedPackages)
            {
                const auto &providerName = package.manifest.build.provider;
                if (providerName.empty())
                {
                    continue;
                }
                if (!resolved.workspace.has_value())
                {
                    error = "package '" + package.manifest.name + "' references provider '" + providerName +
                            "' without a workspace";
                    return {};
                }
                const auto providerIt = resolved.workspace->externalPackageProviders.find(providerName);
                if (providerIt == resolved.workspace->externalPackageProviders.end())
                {
                    error = "package '" + package.manifest.name + "' references unknown package provider '" +
                            providerName + "'";
                    return {};
                }
                auto [it, inserted] = indexByProvider.emplace(providerName, groups.size());
                if (inserted)
                {
                    groups.push_back(ProviderRestoreGroup{.provider = providerIt->second});
                }
                groups[it->second].packages.push_back(&package);
            }
            return groups;
        }

        auto WriteProviderMetadata(const ResolvedLaunch &resolved, const ProviderRestoreGroup &group,
                                   const fs::path &toolchainFile, const fs::path &installRoot,
                                   const fs::path &prefixPath) -> void
        {
            const auto metadataPath = ProviderMetadataPath(resolved, group.provider.name);
            std::ostringstream out{};
            out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
            out << "<ProviderRestore SchemaVersion=\"1\" Provider=\"" << EscapeXml(group.provider.name)
                << "\" Kind=\"" << EscapeXml(group.provider.kind)
                << "\" Profile=\"" << EscapeXml(resolved.profile.name)
                << "\" Root=\"" << EscapeXml(ProviderStateRoot(resolved, group.provider.name).string()) << "\"";
            if (!toolchainFile.empty())
            {
                out << " ToolchainFile=\"" << EscapeXml(toolchainFile.string()) << "\"";
            }
            if (!installRoot.empty())
            {
                out << " InstallRoot=\"" << EscapeXml(installRoot.string()) << "\"";
            }
            if (!prefixPath.empty())
            {
                out << " PrefixPath=\"" << EscapeXml(prefixPath.string()) << "\"";
            }
            out << ">\n";
            out << "  <Packages>\n";
            for (const auto *package : group.packages)
            {
                out << "    <Package Name=\"" << EscapeXml(package->manifest.name)
                    << "\" ProviderPackage=\"" << EscapeXml(ProviderPackageName(package->manifest))
                    << "\" ProviderVersion=\"" << EscapeXml(ProviderPackageVersion(package->manifest))
                    << "\" CMakePackage=\"" << EscapeXml(CMakePackageName(package->manifest)) << "\" />\n";
            }
            out << "  </Packages>\n";
            out << "</ProviderRestore>\n";
            WriteTextFile(metadataPath, out.str());
        }

        auto RunProviderRestoreProcess(const WorkspaceManifest::PackageProvider &provider,
                                       const std::vector<std::string> &arguments, const fs::path &workingDirectory,
                                       const ParsedArgs &args, CliEventEmitter &events) -> ProcessResult
        {
            const auto executable = ProviderToolExecutable(provider);
            if (executable.empty())
            {
                return ProcessResult{.exitCode = 127,
                                     .output = "missing tool: " + Lower(provider.kind) +
                                               ". Install it, set PATH, or configure provider Root."};
            }
            const auto callback =
                args.backendOutputMode == BackendOutputMode::Stream
                    ? std::function<void(std::string_view)>{[&](std::string_view text) {
                          events.Emit(CliEventType::BackendOutput,
                                      EventData{}
                                          .AddString("phase", "restore")
                                          .AddString("stream", "combined")
                                          .AddString("text", std::string{text}));
                      }}
                    : std::function<void(std::string_view)>{};
            return RunProcessCapture(executable, arguments, workingDirectory, callback);
        }

        auto RestoreExternalProviderPackages(const ResolvedLaunch &resolved, const ParsedArgs &args,
                                             CliEventEmitter &events, std::string &error) -> bool
        {
            auto groups = CollectProviderRestoreGroups(resolved, error);
            if (!error.empty())
            {
                return false;
            }
            for (const auto &group : groups)
            {
                const auto stateRoot = ProviderStateRoot(resolved, group.provider.name);
                fs::create_directories(stateRoot);

                if (group.provider.kind == "Vcpkg")
                {
                    const auto vcpkgRoot = VcpkgRootForProvider(group.provider);
                    if (vcpkgRoot.empty())
                    {
                        error = "vcpkg provider '" + group.provider.name +
                                "' requires Root or VCPKG_ROOT so CMake can use the vcpkg toolchain file";
                        return false;
                    }
                    const auto toolchainFile = vcpkgRoot / "scripts" / "buildsystems" / "vcpkg.cmake";
                    const auto installRoot = stateRoot / "installed";

                    std::ostringstream manifest{};
                    manifest << "{\n"
                             << "  \"name\": \"ngin-" << SanitizePathComponent(resolved.project.name) << "\",\n"
                             << "  \"version-string\": \"0.0.0\",\n"
                             << "  \"dependencies\": [\n";
                    for (std::size_t index = 0; index < group.packages.size(); ++index)
                    {
                        if (index != 0)
                        {
                            manifest << ",\n";
                        }
                        manifest << "    " << JsonString(ProviderPackageName(group.packages[index]->manifest));
                    }
                    manifest << "\n  ]\n}\n";
                    WriteTextFile(stateRoot / "vcpkg.json", manifest.str());

                    std::vector<std::string> arguments{
                        "install",
                        "--x-manifest-root",
                        stateRoot.string(),
                        "--x-install-root",
                        installRoot.string(),
                    };
                    if (!group.provider.triplet.empty())
                    {
                        arguments.push_back("--triplet");
                        arguments.push_back(group.provider.triplet);
                    }
                    const auto result = RunProviderRestoreProcess(group.provider, arguments, stateRoot, args, events);
                    if (result.exitCode != 0)
                    {
                        if (!result.output.empty() && args.backendOutputMode != BackendOutputMode::Silent &&
                            args.backendOutputMode != BackendOutputMode::Stream)
                        {
                            events.Emit(CliEventType::BackendOutput, EventData{}
                                                                          .AddString("phase", "restore")
                                                                          .AddString("stream", "combined")
                                                                          .AddString("text", result.output));
                        }
                        error = "vcpkg provider '" + group.provider.name + "' restore failed";
                        return false;
                    }
                    WriteProviderMetadata(resolved, group, toolchainFile, installRoot, {});
                    continue;
                }

                if (group.provider.kind == "Conan")
                {
                    std::ostringstream conanfile{};
                    conanfile << "[requires]\n";
                    for (const auto *package : group.packages)
                    {
                        conanfile << ProviderPackageName(package->manifest) << "/"
                                  << ProviderPackageVersion(package->manifest) << "\n";
                    }
                    conanfile << "\n[generators]\nCMakeDeps\n";
                    WriteTextFile(stateRoot / "conanfile.txt", conanfile.str());

                    std::vector<std::string> arguments{
                        "install",
                        stateRoot.string(),
                        "--output-folder",
                        stateRoot.string(),
                        "--build=missing",
                    };
                    if (!group.provider.profile.empty())
                    {
                        arguments.push_back("--profile");
                        arguments.push_back(group.provider.profile);
                    }
                    const auto result = RunProviderRestoreProcess(group.provider, arguments, stateRoot, args, events);
                    if (result.exitCode != 0)
                    {
                        if (!result.output.empty() && args.backendOutputMode != BackendOutputMode::Silent &&
                            args.backendOutputMode != BackendOutputMode::Stream)
                        {
                            events.Emit(CliEventType::BackendOutput, EventData{}
                                                                          .AddString("phase", "restore")
                                                                          .AddString("stream", "combined")
                                                                          .AddString("text", result.output));
                        }
                        error = "Conan provider '" + group.provider.name + "' restore failed";
                        return false;
                    }
                    WriteProviderMetadata(resolved, group, {}, {}, stateRoot);
                    continue;
                }

                error = "package provider '" + group.provider.name + "' has unsupported kind '" + group.provider.kind + "'";
                return false;
            }
            return true;
        }

        [[nodiscard]] auto GenerateLockFile(const ResolvedLaunch &resolved) -> std::string
        {
            std::ostringstream out{};
            out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
            out << "<LockFile SchemaVersion=\"1\" Project=\"" << EscapeXml(resolved.project.name) << "\" Profile=\""
                << EscapeXml(resolved.profile.name) << "\" BuildType=\"" << EscapeXml(resolved.profile.buildType)
                << "\" Toolchain=\"" << EscapeXml(resolved.profile.toolchain) << "\" Platform=\""
                << EscapeXml(resolved.profile.platform) << "\" Environment=\""
                << EscapeXml(resolved.profile.environmentName) << "\">\n";
            out << "  <Packages>\n";
            for (const auto &package : resolved.orderedPackages)
            {
                out << "    <Package Name=\"" << EscapeXml(package.manifest.name) << "\" Version=\""
                    << EscapeXml(package.manifest.version) << "\" Manifest=\""
                    << EscapeXml(package.manifest.path.string()) << "\" Source=\"" << EscapeXml(package.source) << "\"";
                if (!package.manifest.build.provider.empty())
                {
                    std::string providerKind{};
                    if (resolved.workspace.has_value())
                    {
                        if (const auto providerIt =
                                resolved.workspace->externalPackageProviders.find(package.manifest.build.provider);
                            providerIt != resolved.workspace->externalPackageProviders.end())
                        {
                            providerKind = providerIt->second.kind;
                        }
                    }
                    out << " Provider=\"" << EscapeXml(package.manifest.build.provider) << "\"";
                    if (!providerKind.empty())
                    {
                        out << " ProviderKind=\"" << EscapeXml(providerKind) << "\"";
                    }
                    out << " ProviderPackage=\"" << EscapeXml(ProviderPackageName(package.manifest)) << "\""
                        << " ProviderVersion=\"" << EscapeXml(ProviderPackageVersion(package.manifest)) << "\""
                        << " CMakePackage=\"" << EscapeXml(CMakePackageName(package.manifest)) << "\""
                        << " ProviderMetadata=\""
                        << EscapeXml(ProviderMetadataPath(resolved, package.manifest.build.provider).string()) << "\"";
                }
                if (const auto scopeIt = resolved.packageScopes.find(package.manifest.name);
                    scopeIt != resolved.packageScopes.end() && !scopeIt->second.empty())
                {
                    out << " Scope=\"" << EscapeXml(scopeIt->second) << "\"";
                }
                if (!package.sourceDirectory.empty())
                {
                    out << " ProviderRoot=\"" << EscapeXml(package.sourceDirectory.string()) << "\"";
                }
                out << " />\n";
            }
            out << "  </Packages>\n";
            out << "  <Features>\n";
            for (const auto &feature : resolved.selectedPackageFeatures)
            {
                out << "    <Feature Package=\"" << EscapeXml(feature.packageName) << "\" Name=\""
                    << EscapeXml(feature.featureName) << "\" Version=\"" << EscapeXml(feature.packageVersion)
                    << "\" Manifest=\"" << EscapeXml(feature.manifestPath.string()) << "\" />\n";
            }
            out << "  </Features>\n";
            out << "  <Capabilities>\n";
            for (const auto &provider : resolved.capabilityProviders)
            {
                out << "    <Capability Name=\"" << EscapeXml(provider.capability) << "\" Package=\""
                    << EscapeXml(provider.packageName) << "\" Feature=\"" << EscapeXml(provider.featureName)
                    << "\" Exclusive=\"" << (provider.exclusive ? "true" : "false") << "\" />\n";
            }
            out << "  </Capabilities>\n";
            out << "  <PackageEdges>\n";
            for (const auto &[packageName, deps] : resolved.packageEdges)
            {
                out << "    <Package Name=\"" << EscapeXml(packageName) << "\">\n";
                for (const auto &dep : deps)
                {
                    out << "      <Dependency Name=\"" << EscapeXml(dep) << "\" />\n";
                }
                out << "    </Package>\n";
            }
            out << "  </PackageEdges>\n";
            out << "</LockFile>\n";
            return out.str();
        }

        [[nodiscard]] auto ContainsGitignoreEntry(const std::string &text, std::string_view entry) -> bool
        {
            std::istringstream lines(text);
            std::string line;
            while (std::getline(lines, line))
            {
                if (line == entry)
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] auto HasSelection(const SelectorSet &selectors) -> bool
        {
            return selectors.impossible || selectors.profile.has_value() || selectors.operatingSystem.has_value() ||
                   selectors.platform.has_value() || selectors.architecture.has_value() ||
                   selectors.buildType.has_value() || selectors.environment.has_value() ||
                   !selectors.conditionRefs.empty();
        }

        [[nodiscard]] auto EscapeJson(const std::string &value) -> std::string
        {
            std::string escaped{};
            escaped.reserve(value.size() + 2);
            for (const unsigned char ch : value)
            {
                switch (ch)
                {
                case '\\':
                    escaped += "\\\\";
                    break;
                case '"':
                    escaped += "\\\"";
                    break;
                case '\b':
                    escaped += "\\b";
                    break;
                case '\f':
                    escaped += "\\f";
                    break;
                case '\n':
                    escaped += "\\n";
                    break;
                case '\r':
                    escaped += "\\r";
                    break;
                case '\t':
                    escaped += "\\t";
                    break;
                default:
                    if (ch < 0x20)
                    {
                        constexpr char hex[] = "0123456789abcdef";
                        escaped += "\\u00";
                        escaped += hex[(ch >> 4) & 0x0f];
                        escaped += hex[ch & 0x0f];
                    }
                    else
                    {
                        escaped += static_cast<char>(ch);
                    }
                    break;
                }
            }
            return escaped;
        }

        [[nodiscard]] auto Json(const std::string &value) -> std::string
        {
            return "\"" + EscapeJson(value) + "\"";
        }

        [[nodiscard]] auto JsonPath(const fs::path &path) -> std::string
        {
            return Json(path.generic_string());
        }

        auto PrintSelectorSummary(const SelectorSet &selectors) -> void
        {
            bool first = true;
            const auto append = [&](const std::string &name, const std::optional<std::string> &value) {
                if (!value.has_value())
                {
                    return;
                }
                if (!first)
                {
                    std::cout << ", ";
                }
                std::cout << name << "=\"" << *value << "\"";
                first = false;
            };
            for (const auto &condition : selectors.conditionRefs)
            {
                if (!first)
                {
                    std::cout << ", ";
                }
                std::cout << "Condition=\"" << condition << "\"";
                first = false;
            }
            append("Profile", selectors.profile);
            append("Platform", selectors.platform);
            append("OperatingSystem", selectors.operatingSystem);
            append("Architecture", selectors.architecture);
            append("BuildType", selectors.buildType);
            append("Environment", selectors.environment);
            if (selectors.impossible)
            {
                if (!first)
                {
                    std::cout << ", ";
                }
                std::cout << "contradictory";
            }
        }

        auto PrintConditionalBuildSettings(const std::string_view label, const ProjectManifest &project,
                                           const ProfileDefinition &profile, const std::vector<BuildSetting> &settings)
            -> void
        {
            bool printedHeader = false;
            for (const auto &setting : settings)
            {
                if (!HasSelection(setting.selectors))
                {
                    continue;
                }
                if (!printedHeader)
                {
                    std::cout << "    " << label << ":\n";
                    printedHeader = true;
                }
                std::cout << "      - " << setting.value << " "
                          << (SelectionMatches(project, setting.selectors, profile) ? "included" : "excluded") << " (";
                PrintSelectorSummary(setting.selectors);
                std::cout << ")\n";
            }
        }

        auto PrintConditionalProjectReferences(const std::string_view label, const ProjectManifest &project,
                                               const ProfileDefinition &profile,
                                               const std::vector<ProjectReference> &references) -> void
        {
            bool printedHeader = false;
            for (const auto &reference : references)
            {
                if (!HasSelection(reference.selectors))
                {
                    continue;
                }
                if (!printedHeader)
                {
                    std::cout << "    " << label << ":\n";
                    printedHeader = true;
                }
                std::cout << "      - " << reference.path.string() << " "
                          << (SelectionMatches(project, reference.selectors, profile) ? "included" : "excluded")
                          << " (";
                PrintSelectorSummary(reference.selectors);
                std::cout << ")\n";
            }
        }

        auto PrintConditionalPackageReferences(const std::string_view label, const ProjectManifest &project,
                                               const ProfileDefinition &profile,
                                               const std::vector<PackageReference> &references) -> void
        {
            bool printedHeader = false;
            for (const auto &reference : references)
            {
                if (!HasSelection(reference.selectors))
                {
                    continue;
                }
                if (!printedHeader)
                {
                    std::cout << "    " << label << ":\n";
                    printedHeader = true;
                }
                std::cout << "      - " << reference.name << " "
                          << (SelectionMatches(project, reference.selectors, profile) ? "included" : "excluded")
                          << " (";
                PrintSelectorSummary(reference.selectors);
                std::cout << ")\n";
            }
        }

        auto PrintConditionalRuntimeRefs(const std::string_view label, const ProjectManifest &project,
                                         const ProfileDefinition &profile,
                                         const std::vector<RuntimeReference> &references) -> void
        {
            bool printedHeader = false;
            for (const auto &reference : references)
            {
                if (!HasSelection(reference.selectors))
                {
                    continue;
                }
                if (!printedHeader)
                {
                    std::cout << "    " << label << ":\n";
                    printedHeader = true;
                }
                std::cout << "      - " << reference.name << " "
                          << (SelectionMatches(project, reference.selectors, profile) ? "included" : "excluded")
                          << " (";
                PrintSelectorSummary(reference.selectors);
                std::cout << ")\n";
            }
        }

        [[nodiscard]] auto ProductKindFromNewKind(std::string kind) -> std::string
        {
            std::transform(kind.begin(), kind.end(), kind.begin(),
                           [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            if (kind == "app" || kind == "application")
            {
                return "Application";
            }
            if (kind == "lib" || kind == "library")
            {
                return "Library";
            }
            if (kind == "tool")
            {
                return "Tool";
            }
            if (kind == "test")
            {
                return "Test";
            }
            if (kind == "benchmark")
            {
                return "Benchmark";
            }
            if (kind == "plugin")
            {
                return "Plugin";
            }
            throw std::runtime_error("unknown project template kind '" + kind + "'");
        }

        auto WriteNewFile(const fs::path &path, const std::string &contents) -> void
        {
            if (fs::exists(path))
            {
                throw std::runtime_error(path.string() + ": file already exists");
            }
            if (!path.parent_path().empty())
            {
                fs::create_directories(path.parent_path());
            }
            std::ofstream out(path);
            out << contents;
        }

        auto WriteTextFile(const fs::path &path, const std::string &contents) -> void
        {
            std::ofstream out(path);
            if (!out)
            {
                throw std::runtime_error(path.string() + ": failed to open for writing");
            }
            out << contents;
        }

        [[nodiscard]] auto FindProductOpenTagEnd(const std::string &text, const std::string &productKind) -> std::size_t
        {
            const auto start = text.find("<" + productKind);
            if (start == std::string::npos)
            {
                throw std::runtime_error("project file does not contain <" + productKind + ">");
            }
            const auto end = text.find('>', start);
            if (end == std::string::npos)
            {
                throw std::runtime_error("project file has an unterminated <" + productKind + "> element");
            }
            return end;
        }

        [[nodiscard]] auto IsSelfClosingTag(const std::string &text, const std::size_t openTagEnd) -> bool
        {
            if (openTagEnd == 0)
            {
                return false;
            }
            std::size_t index = openTagEnd;
            while (index > 0)
            {
                --index;
                const auto ch = static_cast<unsigned char>(text[index]);
                if (!std::isspace(ch))
                {
                    return text[index] == '/';
                }
            }
            return false;
        }

        [[nodiscard]] auto InsertUseLine(std::string text, const std::string &productKind,
                                         const std::string &dependencyLine) -> std::string
        {
            const auto productOpenEnd = FindProductOpenTagEnd(text, productKind);
            if (IsSelfClosingTag(text, productOpenEnd))
            {
                const auto productOpenStart = text.rfind('<', productOpenEnd);
                const auto indentStart = text.rfind('\n', productOpenStart == std::string::npos ? 0 : productOpenStart);
                const auto indent = indentStart == std::string::npos
                                        ? std::string{}
                                        : text.substr(indentStart + 1, productOpenStart - indentStart - 1);
                const auto replacement = "<" + productKind + ">\n" + indent + "  <Uses>\n" + dependencyLine + indent +
                                         "  </Uses>\n" + indent + "</" + productKind + ">";
                const auto replaceStart = productOpenStart;
                text.replace(replaceStart, productOpenEnd - replaceStart + 1, replacement);
                return text;
            }

            const auto productClose = text.find("</" + productKind + ">", productOpenEnd);
            if (productClose == std::string::npos)
            {
                throw std::runtime_error("project file has no closing </" + productKind + "> element");
            }
            const auto usesOpen = text.find("<Uses>", productOpenEnd);
            const auto usesClose = text.find("</Uses>", productOpenEnd);
            if (usesOpen != std::string::npos && usesClose != std::string::npos && usesOpen < productClose &&
                usesClose < productClose)
            {
                text.insert(usesClose, dependencyLine);
                return text;
            }

            const auto block = std::string("\n    <Uses>\n") + dependencyLine + "    </Uses>\n";
            text.insert(productOpenEnd + 1, block);
            return text;
        }

        [[nodiscard]] auto InsertPackageUse(std::string text, const std::string &productKind,
                                            const std::string &packageName, const std::string &versionRange,
                                            const std::string &scope) -> std::string
        {
            const auto dependencyLine = std::string("      <Package Name=\"") + EscapeXml(packageName) +
                                        "\" Version=\"" + EscapeXml(versionRange) + "\" Scope=\"" + EscapeXml(scope) +
                                        "\" />\n";
            return InsertUseLine(std::move(text), productKind, dependencyLine);
        }

        [[nodiscard]] auto InsertProjectReferenceUse(std::string text, const std::string &productKind,
                                                     const std::string &projectName, const std::string &projectPath)
            -> std::string
        {
            const auto dependencyLine = std::string("      <Project Name=\"") + EscapeXml(projectName) + "\" Path=\"" +
                                        EscapeXml(projectPath) + "\" />\n";
            return InsertUseLine(std::move(text), productKind, dependencyLine);
        }

        [[nodiscard]] auto RemovePackageUse(std::string text, const std::string &productKind,
                                            const std::string &packageName) -> std::string
        {
            const auto productOpenEnd = FindProductOpenTagEnd(text, productKind);
            const auto productClose = text.find("</" + productKind + ">", productOpenEnd);
            if (productClose == std::string::npos)
            {
                throw std::runtime_error("project file has no closing </" + productKind + "> element");
            }
            const auto packageNeedle = "<Package Name=\"" + packageName + "\"";
            const auto packageStart = text.find(packageNeedle, productOpenEnd);
            if (packageStart == std::string::npos || packageStart > productClose)
            {
                throw std::runtime_error("project does not reference package '" + packageName + "'");
            }
            const auto tagEnd = text.find('>', packageStart);
            if (tagEnd == std::string::npos || tagEnd > productClose)
            {
                throw std::runtime_error("project package reference for '" + packageName + "' is malformed");
            }
            const auto lineStartBefore = text.rfind('\n', packageStart);
            const auto lineStart = lineStartBefore == std::string::npos ? packageStart : lineStartBefore + 1;
            const auto lineEnd = text.find('\n', tagEnd);
            const auto eraseEnd = lineEnd == std::string::npos ? tagEnd + 1 : lineEnd + 1;
            text.erase(lineStart, eraseEnd - lineStart);
            return text;
        }

        [[nodiscard]] auto UpdatePackageUse(std::string text, const std::string &productKind,
                                            const std::string &packageName, const std::string &versionRange,
                                            const std::string &scope) -> std::string
        {
            const auto productOpenEnd = FindProductOpenTagEnd(text, productKind);
            const auto productClose = text.find("</" + productKind + ">", productOpenEnd);
            if (productClose == std::string::npos)
            {
                throw std::runtime_error("project file has no closing </" + productKind + "> element");
            }
            const auto packageNeedle = "<Package Name=\"" + packageName + "\"";
            const auto packageStart = text.find(packageNeedle, productOpenEnd);
            if (packageStart == std::string::npos || packageStart > productClose)
            {
                throw std::runtime_error("project does not reference package '" + packageName + "'");
            }
            const auto tagEnd = text.find('>', packageStart);
            if (tagEnd == std::string::npos || tagEnd > productClose)
            {
                throw std::runtime_error("project package reference for '" + packageName + "' is malformed");
            }
            const auto lineStartBefore = text.rfind('\n', packageStart);
            const auto lineStart = lineStartBefore == std::string::npos ? packageStart : lineStartBefore + 1;
            const auto lineEnd = text.find('\n', tagEnd);
            const auto eraseEnd = lineEnd == std::string::npos ? tagEnd + 1 : lineEnd;
            const auto indent = text.substr(lineStart, packageStart - lineStart);
            const auto replacement = indent + "<Package Name=\"" + EscapeXml(packageName) + "\" Version=\"" +
                                     EscapeXml(versionRange) + "\" Scope=\"" + EscapeXml(scope) + "\" />";
            text.replace(lineStart, eraseEnd - lineStart, replacement);
            return text;
        }

        [[nodiscard]] auto GeneratePackageOutputManifest(const PackageOutputDefinition &output) -> std::string
        {
            std::ostringstream manifest{};
            manifest << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n\n";
            manifest << "<Package SchemaVersion=\"4\" Name=\"" << EscapeXml(output.name) << "\" Version=\""
                     << EscapeXml(output.version) << "\">\n";
            if (!output.description.empty() || !output.license.empty())
            {
                manifest << "  <Metadata>\n";
                if (!output.description.empty())
                {
                    manifest << "    <Description>" << EscapeXml(output.description) << "</Description>\n";
                }
                if (!output.license.empty())
                {
                    manifest << "    <License>" << EscapeXml(output.license) << "</License>\n";
                }
                manifest << "  </Metadata>\n\n";
            }
            if (!output.headers.empty() || !output.libraries.empty() || !output.capabilities.empty())
            {
                manifest << "  <Library Name=\"" << EscapeXml(output.name) << "\">\n";
                manifest << "    <Exports>\n";
                for (const auto &header : output.headers)
                {
                    manifest << "      <Headers Path=\"" << EscapeXml(header) << "\" />\n";
                }
                for (const auto &library : output.libraries)
                {
                    manifest << "      <LibraryTarget Name=\"" << EscapeXml(library) << "\" />\n";
                }
                for (const auto &capability : output.capabilities)
                {
                    manifest << "      <Capability Name=\"" << EscapeXml(capability) << "\" />\n";
                }
                manifest << "    </Exports>\n";
                manifest << "  </Library>\n";
            }
            if (!output.tools.empty())
            {
                manifest << "  <Tool Name=\"" << EscapeXml(output.name) << "\">\n";
                manifest << "    <Exports>\n";
                for (const auto &tool : output.tools)
                {
                    manifest << "      <Tool Name=\"" << EscapeXml(tool) << "\" Executable=\"" << EscapeXml(tool)
                             << "\" />\n";
                }
                manifest << "    </Exports>\n";
                manifest << "  </Tool>\n";
            }
            if (!output.targetPlatforms.empty() || !output.abiTag.empty())
            {
                manifest << "  <Compatibility>\n";
                for (const auto &platform : output.targetPlatforms)
                {
                    manifest << "    <TargetPlatform Name=\"" << EscapeXml(platform) << "\" />\n";
                }
                if (!output.abiTag.empty())
                {
                    manifest << "    <Abi Tag=\"" << EscapeXml(output.abiTag) << "\" />\n";
                }
                manifest << "  </Compatibility>\n";
            }
            manifest << "</Package>\n";
            return manifest.str();
        }

        [[nodiscard]] auto PackageClosuresForScope(const std::string &scope) -> std::vector<std::string>
        {
            std::set<std::string> scopes{};
            std::stringstream stream{scope.empty() ? "Target" : scope};
            std::string part{};
            while (std::getline(stream, part, ';'))
            {
                if (!part.empty())
                {
                    scopes.insert(part);
                }
            }

            std::vector<std::string> closures{};
            auto addIf = [&](const std::string &scopeName, const std::string &closureName) {
                if (scopes.contains(scopeName))
                {
                    closures.push_back(closureName);
                }
            };
            addIf("Build", "Host");
            addIf("Target", "Target");
            addIf("Runtime", "Runtime");
            addIf("Test", "Test");
            addIf("Dev", "Dev");
            addIf("Publish", "Publish");
            return closures;
        }

        [[nodiscard]] auto TrimView(std::string_view text) -> std::string_view
        {
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
            {
                text.remove_prefix(1);
            }
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
            {
                text.remove_suffix(1);
            }
            return text;
        }

        [[nodiscard]] auto HasElementChildren(const XmlElement &element) -> bool
        {
            for (NGIN::UIntSize index = 0; index < element.children.Size(); ++index)
            {
                if (element.children[index].type == XmlNode::Type::Element)
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] auto HasNonWhitespaceText(const XmlElement &element) -> bool
        {
            for (NGIN::UIntSize index = 0; index < element.children.Size(); ++index)
            {
                const auto &child = element.children[index];
                if ((child.type == XmlNode::Type::Text || child.type == XmlNode::Type::CData) &&
                    !TrimView(child.text).empty())
                {
                    return true;
                }
            }
            return false;
        }

        auto WriteFormattedElement(std::ostream &out, const XmlElement &element, const int indent) -> void
        {
            const std::string pad(static_cast<std::size_t>(indent), ' ');
            out << pad << "<" << element.name;
            for (NGIN::UIntSize index = 0; index < element.attributes.Size(); ++index)
            {
                const auto &attribute = element.attributes[index];
                out << " " << attribute.name << "=\"" << EscapeXml(std::string(attribute.value)) << "\"";
            }

            const auto hasElements = HasElementChildren(element);
            const auto hasText = HasNonWhitespaceText(element);
            if (!hasElements && !hasText)
            {
                out << " />\n";
                return;
            }

            if (!hasElements)
            {
                out << ">";
                for (NGIN::UIntSize index = 0; index < element.children.Size(); ++index)
                {
                    const auto &child = element.children[index];
                    const auto text = TrimView(child.text);
                    if (text.empty())
                    {
                        continue;
                    }
                    if (child.type == XmlNode::Type::CData)
                    {
                        out << "<![CDATA[" << text << "]]>";
                    }
                    else
                    {
                        out << EscapeXml(std::string(text));
                    }
                }
                out << "</" << element.name << ">\n";
                return;
            }

            out << ">\n";
            for (NGIN::UIntSize index = 0; index < element.children.Size(); ++index)
            {
                const auto &child = element.children[index];
                if (child.type == XmlNode::Type::Element && child.element != nullptr)
                {
                    WriteFormattedElement(out, *child.element, indent + 2);
                }
                else
                {
                    const auto text = TrimView(child.text);
                    if (text.empty())
                    {
                        continue;
                    }
                    out << std::string(static_cast<std::size_t>(indent + 2), ' ');
                    if (child.type == XmlNode::Type::CData)
                    {
                        out << "<![CDATA[" << text << "]]>\n";
                    }
                    else
                    {
                        out << EscapeXml(std::string(text)) << "\n";
                    }
                }
            }
            out << pad << "</" << element.name << ">\n";
        }

        [[nodiscard]] auto FormatXmlManifest(const fs::path &path) -> std::string
        {
            const auto existing = ReadText(path);
            if (existing.find("<!--") != std::string::npos)
            {
                throw std::runtime_error("format currently refuses XML comments so it does "
                                         "not drop authored comments");
            }
            const auto loaded = LoadXml(path);
            const auto *rootElement = loaded.document.Root();
            if (rootElement == nullptr)
            {
                throw std::runtime_error(path.string() + ": missing XML root element");
            }
            std::ostringstream formatted{};
            formatted << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n\n";
            WriteFormattedElement(formatted, *rootElement, 0);
            return formatted.str();
        }

        [[nodiscard]] auto IsClangTidyAnalyzer(const AnalyzerDefinition &analyzer) -> bool
        {
            return Lower(analyzer.name) == "clang-tidy" || Lower(AnalyzerToolName(analyzer)) == "clang-tidy";
        }

        [[nodiscard]] auto ResolveAnalyzerTool(const ResolvedLaunch &resolved, const AnalyzerDefinition &analyzer)
            -> std::optional<ToolResolution>
        {
            auto toolName = AnalyzerToolName(analyzer);
            if (!analyzer.packageName.empty())
            {
                const auto packageIt = std::find_if(
                    resolved.orderedPackages.begin(), resolved.orderedPackages.end(),
                    [&](const ResolvedPackage &package) { return package.manifest.name == analyzer.packageName; });
                if (packageIt != resolved.orderedPackages.end())
                {
                    const auto toolIt =
                        std::find_if(packageIt->manifest.tools.begin(), packageIt->manifest.tools.end(),
                                     [&](const ToolDeclaration &tool) { return tool.name == toolName; });
                    if (toolIt != packageIt->manifest.tools.end() && !toolIt->executable.empty())
                    {
                        toolName = toolIt->executable;
                    }
                }
            }
            const auto searchRoot = resolved.workspace.has_value()
                                        ? std::optional<fs::path>{resolved.workspace->path.parent_path()}
                                        : std::optional<fs::path>{resolved.project.path.parent_path()};
            return ResolveToolPath(toolName, searchRoot);
        }

        [[nodiscard]] auto ResolveAnalyzerSources(const ResolvedLaunch &resolved) -> std::vector<fs::path>
        {
            std::vector<fs::path> sources{};
            std::unordered_set<std::string> seen{};
            for (const auto &input : resolved.inputs)
            {
                if (input.role != "Source")
                {
                    continue;
                }
                const auto path = input.absoluteSourcePath.empty() ? fs::path(input.source) : input.absoluteSourcePath;
                const auto extension = Lower(path.extension().string());
                if (extension != ".cpp" && extension != ".cc" && extension != ".cxx" && extension != ".c++")
                {
                    continue;
                }
                const auto key = path.lexically_normal().string();
                if (seen.insert(key).second)
                {
                    sources.push_back(path);
                }
            }
            return sources;
        }

        struct AnalyzerFinding
        {
            std::string severity{};
            fs::path file{};
            std::string line{};
            std::string column{};
            std::string message{};
        };

        [[nodiscard]] auto NormalizeClangTidyMessage(std::string message) -> std::string
        {
            const std::regex checkSuffix{R"( \[([A-Za-z0-9_.\-]+)\]$)"};
            std::smatch match{};
            if (std::regex_search(message, match, checkSuffix))
            {
                const auto check = match[1].str();
                if (check.rfind("clang-tidy:", 0) != 0)
                {
                    message = std::regex_replace(message, checkSuffix, " [clang-tidy:" + check + "]");
                }
            }
            return message;
        }

        [[nodiscard]] auto ClangTidyCodeFromMessage(const std::string &message) -> std::string
        {
            const std::regex checkSuffix{R"(\[clang-tidy:([A-Za-z0-9_.\-]+)\]$)"};
            std::smatch match{};
            if (std::regex_search(message, match, checkSuffix))
            {
                return match[1].str();
            }
            return {};
        }

        [[nodiscard]] auto ParseClangTidyFindings(const std::string &output, const std::string &configuredSeverity)
            -> std::vector<AnalyzerFinding>
        {
            std::vector<AnalyzerFinding> findings{};
            const std::regex diagnosticPattern{R"(^(.+):([0-9]+):([0-9]+):\s*(warning|error):\s*(.+)$)"};
            std::istringstream lines{output};
            std::string line{};
            while (std::getline(lines, line))
            {
                std::smatch match{};
                if (!std::regex_match(line, match, diagnosticPattern))
                {
                    continue;
                }
                findings.push_back(AnalyzerFinding{
                    .severity = Lower(configuredSeverity) == "error" ? "error" : "warning",
                    .file = fs::path(match[1].str()),
                    .line = match[2].str(),
                    .column = match[3].str(),
                    .message = NormalizeClangTidyMessage(match[5].str()),
                });
            }
            return findings;
        }

        auto PrintAnalyzerFinding(const AnalyzerFinding &finding) -> void
        {
            std::cout << "[" << finding.severity << "] " << finding.file.string() << ":" << finding.line << ":"
                      << finding.column << ": " << finding.message << "\n";
        }

        [[nodiscard]] auto SelectPublish(const std::vector<PublishDefinition> &publishes,
                                         const std::optional<std::string> &name) -> const PublishDefinition &
        {
            if (publishes.empty())
            {
                throw std::runtime_error("project does not declare Publish");
            }
            if (name.has_value())
            {
                const auto it = std::find_if(publishes.begin(), publishes.end(),
                                             [&](const PublishDefinition &publish) { return publish.name == *name; });
                if (it == publishes.end())
                {
                    throw std::runtime_error("project does not declare Publish '" + *name + "'");
                }
                return *it;
            }
            if (publishes.size() == 1)
            {
                return publishes.front();
            }
            const auto it = std::find_if(publishes.begin(), publishes.end(),
                                         [](const PublishDefinition &publish) { return publish.name == "default"; });
            if (it != publishes.end())
            {
                return *it;
            }
            throw std::runtime_error("publish requires a name when the project declares multiple publishes");
        }

        [[nodiscard]] auto IsPublishInternalPath(const fs::path &relativePath) -> bool
        {
            const auto begin = relativePath.begin();
            return begin != relativePath.end() && begin->generic_string() == ".ngin";
        }

        auto CopyDirectoryContents(const fs::path &source, const fs::path &destination) -> void
        {
            fs::create_directories(destination);
            for (fs::recursive_directory_iterator it(source), end; it != end; ++it)
            {
                const auto &entry = *it;
                const auto relative = fs::relative(entry.path(), source);
                if (IsPublishInternalPath(relative))
                {
                    if (entry.is_directory())
                    {
                        it.disable_recursion_pending();
                    }
                    continue;
                }
                const auto target = destination / relative;
                if (entry.is_directory())
                {
                    fs::create_directories(target);
                }
                else if (entry.is_regular_file())
                {
                    fs::create_directories(target.parent_path());
                    fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
                }
            }
        }

        [[nodiscard]] auto ReadBinaryFile(const fs::path &path) -> std::string
        {
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                throw std::runtime_error(path.string() + ": failed to open for reading");
            }
            std::ostringstream content{};
            content << input.rdbuf();
            return content.str();
        }

        auto WriteU16(std::ostream &out, std::uint16_t value) -> void
        {
            out.put(static_cast<char>(value & 0xffU));
            out.put(static_cast<char>((value >> 8U) & 0xffU));
        }

        auto WriteU32(std::ostream &out, std::uint32_t value) -> void
        {
            out.put(static_cast<char>(value & 0xffU));
            out.put(static_cast<char>((value >> 8U) & 0xffU));
            out.put(static_cast<char>((value >> 16U) & 0xffU));
            out.put(static_cast<char>((value >> 24U) & 0xffU));
        }

        [[nodiscard]] auto Crc32(std::string_view data) -> std::uint32_t
        {
            std::uint32_t crc = 0xffffffffU;
            for (const unsigned char ch : data)
            {
                crc ^= ch;
                for (int bit = 0; bit < 8; ++bit)
                {
                    const auto mask = 0U - (crc & 1U);
                    crc = (crc >> 1U) ^ (0xedb88320U & mask);
                }
            }
            return ~crc;
        }

        struct ZipEntry
        {
            fs::path sourcePath{};
            std::string archivePath{};
            std::string contents{};
            std::uint32_t crc{};
            std::uint32_t offset{};
        };

        [[nodiscard]] auto GatherZipEntries(const fs::path &source) -> std::vector<ZipEntry>
        {
            std::vector<ZipEntry> entries{};
            for (fs::recursive_directory_iterator it(source), end; it != end; ++it)
            {
                const auto &entry = *it;
                const auto relative = fs::relative(entry.path(), source);
                if (IsPublishInternalPath(relative))
                {
                    if (entry.is_directory())
                    {
                        it.disable_recursion_pending();
                    }
                    continue;
                }
                if (!entry.is_regular_file())
                {
                    continue;
                }
                ZipEntry zipEntry{};
                zipEntry.sourcePath = entry.path();
                zipEntry.archivePath = relative.generic_string();
                entries.push_back(std::move(zipEntry));
            }
            std::sort(entries.begin(), entries.end(),
                      [](const ZipEntry &left, const ZipEntry &right) { return left.archivePath < right.archivePath; });
            return entries;
        }

        auto WriteZipArchive(const fs::path &sourceDirectory, const fs::path &archivePath) -> void
        {
            auto entries = GatherZipEntries(sourceDirectory);
            if (!archivePath.parent_path().empty())
            {
                fs::create_directories(archivePath.parent_path());
            }

            std::ofstream out(archivePath, std::ios::binary);
            if (!out)
            {
                throw std::runtime_error(archivePath.string() + ": failed to open archive for writing");
            }

            for (auto &entry : entries)
            {
                entry.contents = ReadBinaryFile(entry.sourcePath);
                entry.crc = Crc32(entry.contents);
                entry.offset = static_cast<std::uint32_t>(out.tellp());

                WriteU32(out, 0x04034b50U);
                WriteU16(out, 20);
                WriteU16(out, 0);
                WriteU16(out, 0);
                WriteU16(out, 0);
                WriteU16(out, 0);
                WriteU32(out, entry.crc);
                WriteU32(out, static_cast<std::uint32_t>(entry.contents.size()));
                WriteU32(out, static_cast<std::uint32_t>(entry.contents.size()));
                WriteU16(out, static_cast<std::uint16_t>(entry.archivePath.size()));
                WriteU16(out, 0);
                out.write(entry.archivePath.data(), static_cast<std::streamsize>(entry.archivePath.size()));
                out.write(entry.contents.data(), static_cast<std::streamsize>(entry.contents.size()));
            }

            const auto centralDirectoryOffset = static_cast<std::uint32_t>(out.tellp());
            for (const auto &entry : entries)
            {
                WriteU32(out, 0x02014b50U);
                WriteU16(out, 20);
                WriteU16(out, 20);
                WriteU16(out, 0);
                WriteU16(out, 0);
                WriteU16(out, 0);
                WriteU16(out, 0);
                WriteU32(out, entry.crc);
                WriteU32(out, static_cast<std::uint32_t>(entry.contents.size()));
                WriteU32(out, static_cast<std::uint32_t>(entry.contents.size()));
                WriteU16(out, static_cast<std::uint16_t>(entry.archivePath.size()));
                WriteU16(out, 0);
                WriteU16(out, 0);
                WriteU16(out, 0);
                WriteU16(out, 0);
                WriteU32(out, 0);
                WriteU32(out, entry.offset);
                out.write(entry.archivePath.data(), static_cast<std::streamsize>(entry.archivePath.size()));
            }
            const auto centralDirectorySize = static_cast<std::uint32_t>(out.tellp()) - centralDirectoryOffset;

            WriteU32(out, 0x06054b50U);
            WriteU16(out, 0);
            WriteU16(out, 0);
            WriteU16(out, static_cast<std::uint16_t>(entries.size()));
            WriteU16(out, static_cast<std::uint16_t>(entries.size()));
            WriteU32(out, centralDirectorySize);
            WriteU32(out, centralDirectoryOffset);
            WriteU16(out, 0);
        }

        [[nodiscard]] auto IsProbablyUrl(const std::string &value) -> bool
        {
            return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
        }

        [[nodiscard]] auto InsertPackageSource(std::string text, const std::string &name, const std::string &location)
            -> std::string
        {
            if (text.find("<Source Name=\"" + name + "\"") != std::string::npos)
            {
                throw std::runtime_error("workspace already declares package source '" + name + "'");
            }
            const auto sourceLine = std::string("    <Source Name=\"") + EscapeXml(name) + "\" " +
                                    (IsProbablyUrl(location) ? "Url=\"" : "Path=\"") + EscapeXml(location) + "\" />\n";
            auto packagesOpen = text.find("<Packages>");
            auto packagesClose = text.find("</Packages>");
            if (packagesOpen != std::string::npos && packagesClose != std::string::npos && packagesOpen < packagesClose)
            {
                text.insert(packagesClose, sourceLine);
                return text;
            }
            const auto workspaceClose = text.find("</Workspace>");
            if (workspaceClose == std::string::npos)
            {
                throw std::runtime_error("workspace file has no closing </Workspace> element");
            }
            const auto block = std::string("  <Packages>\n") + sourceLine + "  </Packages>\n";
            text.insert(workspaceClose, block);
            return text;
        }

        [[nodiscard]] auto RemovePackageSource(std::string text, const std::string &name) -> std::string
        {
            const auto sourceStart = text.find("<Source Name=\"" + name + "\"");
            if (sourceStart == std::string::npos)
            {
                throw std::runtime_error("workspace does not declare package source '" + name + "'");
            }
            const auto tagEnd = text.find('>', sourceStart);
            if (tagEnd == std::string::npos)
            {
                throw std::runtime_error("workspace package source '" + name + "' is malformed");
            }
            const auto lineStartBefore = text.rfind('\n', sourceStart);
            const auto lineStart = lineStartBefore == std::string::npos ? sourceStart : lineStartBefore + 1;
            const auto lineEnd = text.find('\n', tagEnd);
            const auto eraseEnd = lineEnd == std::string::npos ? tagEnd + 1 : lineEnd + 1;
            text.erase(lineStart, eraseEnd - lineStart);
            return text;
        }

        struct DiffSnapshot
        {
            std::map<std::string, std::string> selection{};
            std::map<std::string, std::string> packages{};
            std::map<std::string, std::string> stagedFiles{};
            std::map<std::string, std::string> environment{};
            std::map<std::string, std::string> launch{};
            std::map<std::string, std::string> launches{};
            std::set<std::string> defines{};
            std::set<std::string> packageFeatures{};
            std::set<std::string> generators{};
            std::set<std::string> generatedOutputs{};
            std::set<std::string> runtimeModules{};
            std::set<std::string> plugins{};
            std::set<std::string> artifacts{};
            std::set<std::string> publishes{};
            std::set<std::string> analyzers{};
        };

        [[nodiscard]] auto RedactedEnvironmentValue(const EnvironmentVariable &variable) -> std::string
        {
            if (variable.secret)
            {
                return "<redacted>";
            }
            if (!variable.resolved && variable.value.empty())
            {
                return "<missing>";
            }
            return variable.value;
        }

        [[nodiscard]] auto BuildDiffSnapshot(const ResolvedLaunch &resolved) -> DiffSnapshot
        {
            DiffSnapshot snapshot{};
            snapshot.selection.emplace("BuildType", resolved.profile.buildType);
            snapshot.selection.emplace("HostPlatform", resolved.profile.hostPlatform);
            snapshot.selection.emplace("TargetPlatform", resolved.profile.platform);
            snapshot.selection.emplace("Toolchain", resolved.profile.toolchain);
            snapshot.selection.emplace("OperatingSystem", resolved.profile.operatingSystem);
            snapshot.selection.emplace("Architecture", resolved.profile.architecture);
            snapshot.selection.emplace("Environment", resolved.profile.environmentName);

            for (const auto &unit : resolved.projectUnits)
            {
                for (const auto &setting : unit.project.build.compileDefinitions)
                {
                    if (SelectionMatches(unit.project, setting.selectors, unit.profile))
                    {
                        snapshot.defines.insert(setting.value);
                    }
                }
            }
            for (const auto &feature : resolved.selectedPackageFeatures)
            {
                for (const auto &setting : feature.build.compileDefinitions)
                {
                    snapshot.defines.insert(setting.value);
                }
            }

            for (const auto &package : resolved.orderedPackages)
            {
                auto value = package.manifest.version;
                if (const auto scopeIt = resolved.packageScopes.find(package.manifest.name);
                    scopeIt != resolved.packageScopes.end() && !scopeIt->second.empty())
                {
                    value += " scope=" + scopeIt->second;
                }
                snapshot.packages[package.manifest.name] = std::move(value);
            }
            for (const auto &feature : resolved.selectedPackageFeatures)
            {
                snapshot.packageFeatures.insert(feature.packageName + "/" + feature.featureName);
            }
            for (const auto &generator : resolved.generators)
            {
                snapshot.generators.insert(generator.declaration.name);
                for (const auto &output : generator.declaration.outputs)
                {
                    snapshot.generatedOutputs.insert(output.kind + ":" + output.role + ":" + output.path);
                }
            }
            for (const auto &input : resolved.inputs)
            {
                if (!input.stagedRelativePath.empty())
                {
                    snapshot.stagedFiles[input.stagedRelativePath.generic_string()] = input.source;
                }
            }
            for (const auto &variable : resolved.environmentVariables)
            {
                snapshot.environment[variable.name] = RedactedEnvironmentValue(variable);
            }
            for (const auto &module : resolved.requiredModules)
            {
                snapshot.runtimeModules.insert("required:" + module);
            }
            for (const auto &module : resolved.optionalModules)
            {
                snapshot.runtimeModules.insert("optional:" + module);
            }
            for (const auto &plugin : resolved.enabledPlugins)
            {
                snapshot.plugins.insert(plugin);
            }
            for (const auto &library : resolved.libraries)
            {
                snapshot.artifacts.insert("library:" + library.name);
            }
            for (const auto &executable : resolved.executables)
            {
                snapshot.artifacts.insert("executable:" + executable.name);
            }
            for (const auto &publish : EffectivePublishes(resolved.project, resolved.profile))
            {
                snapshot.publishes.insert(publish.name + " kind=" + publish.kind + " output=" + publish.output);
            }
            for (const auto &[_, analyzer] :
                 EffectiveAnalyzers(resolved.project, resolved.profile, resolved.selectedPackageFeatures))
            {
                if (analyzer.enabled)
                {
                    auto value = analyzer.name + " scope=" + analyzer.scope + " severity=" + analyzer.severity;
                    if (!analyzer.configPath.empty())
                    {
                        value += " config=" + analyzer.configPath;
                    }
                    snapshot.analyzers.insert(std::move(value));
                }
            }
            snapshot.launch["WorkingDirectory"] = resolved.profile.launch.workingDirectory;
            snapshot.launch["Args"] = resolved.profile.launch.args;
            snapshot.launch["Name"] = resolved.profile.launch.name;
            snapshot.launch["Executable"] =
                resolved.selectedExecutable.has_value() ? resolved.selectedExecutable->name : "";
            for (const auto &launch : EffectiveLaunches(resolved.project, resolved.profile))
            {
                auto value = std::string("executable=") +
                             launch.executable.value_or(
                                 resolved.project.output.kind == "Executable" ? resolved.project.output.name : "") +
                             " workingDirectory=" + launch.workingDirectory + " args=" + launch.args;
                if (launch.name == resolved.profile.launch.name)
                {
                    value += " selected=true";
                }
                snapshot.launches[launch.name] = std::move(value);
            }
            return snapshot;
        }

        auto PrintMapDiff(const std::string &label, const std::map<std::string, std::string> &from,
                          const std::map<std::string, std::string> &to, bool &anyDiff) -> void
        {
            std::set<std::string> keys{};
            for (const auto &[key, _] : from)
            {
                keys.insert(key);
            }
            for (const auto &[key, _] : to)
            {
                keys.insert(key);
            }
            bool printedHeader = false;
            const auto header = [&]() {
                if (!printedHeader)
                {
                    std::cout << label << ":\n";
                    printedHeader = true;
                }
            };
            for (const auto &key : keys)
            {
                const auto fromIt = from.find(key);
                const auto toIt = to.find(key);
                if (fromIt == from.end())
                {
                    anyDiff = true;
                    header();
                    std::cout << "  " << key << " added: " << toIt->second << "\n";
                    continue;
                }
                if (toIt == to.end())
                {
                    anyDiff = true;
                    header();
                    std::cout << "  " << key << " removed: " << fromIt->second << "\n";
                    continue;
                }
                if (fromIt->second != toIt->second)
                {
                    anyDiff = true;
                    header();
                    std::cout << "  " << key << " changed: " << fromIt->second << " -> " << toIt->second << "\n";
                }
            }
        }

        auto PrintSetDiff(const std::string &label, const std::set<std::string> &from, const std::set<std::string> &to,
                          bool &anyDiff) -> void
        {
            std::vector<std::string> added{};
            std::vector<std::string> removed{};
            std::set_difference(to.begin(), to.end(), from.begin(), from.end(), std::back_inserter(added));
            std::set_difference(from.begin(), from.end(), to.begin(), to.end(), std::back_inserter(removed));
            if (added.empty() && removed.empty())
            {
                return;
            }
            anyDiff = true;
            if (!added.empty())
            {
                std::cout << label << " added:\n";
                for (const auto &item : added)
                {
                    std::cout << "  + " << item << "\n";
                }
            }
            if (!removed.empty())
            {
                std::cout << label << " removed:\n";
                for (const auto &item : removed)
                {
                    std::cout << "  - " << item << "\n";
                }
            }
        }

        struct LockPackageEntry
        {
            std::string version{};
            std::string scope{};
            std::string source{};
        };

        [[nodiscard]] auto LoadLockPackages(const fs::path &path) -> std::map<std::string, LockPackageEntry>
        {
            const auto loaded = LoadXml(path);
            const auto *rootElement = loaded.document.Root();
            if (rootElement == nullptr || rootElement->name != "LockFile")
            {
                throw std::runtime_error(path.string() + ": expected LockFile root element");
            }

            std::map<std::string, LockPackageEntry> packages{};
            const auto *packagesNode = FindChild(*rootElement, "Packages");
            if (packagesNode == nullptr)
            {
                return packages;
            }

            for (const auto *packageNode : ChildElements(*packagesNode, "Package"))
            {
                const auto name = RequireAttribute(*packageNode, "Name", path);
                LockPackageEntry entry{};
                entry.version = Attribute(*packageNode, "Version").value_or("");
                entry.scope = Attribute(*packageNode, "Scope").value_or("");
                entry.source = Attribute(*packageNode, "Source").value_or("");
                packages[name] = std::move(entry);
            }
            return packages;
        }

        auto PrintLockDiff(const std::map<std::string, LockPackageEntry> &from,
                           const std::map<std::string, LockPackageEntry> &to, bool &anyDiff) -> void
        {
            std::set<std::string> packageNames{};
            for (const auto &[name, _] : from)
            {
                packageNames.insert(name);
            }
            for (const auto &[name, _] : to)
            {
                packageNames.insert(name);
            }

            for (const auto &name : packageNames)
            {
                const auto fromIt = from.find(name);
                const auto toIt = to.find(name);
                if (fromIt == from.end())
                {
                    anyDiff = true;
                    std::cout << "Package added: " << name << " " << toIt->second.version << "\n";
                    continue;
                }
                if (toIt == to.end())
                {
                    anyDiff = true;
                    std::cout << "Package removed: " << name << " " << fromIt->second.version << "\n";
                    continue;
                }

                if (fromIt->second.version != toIt->second.version)
                {
                    anyDiff = true;
                    std::cout << "Package changed: " << name << " " << fromIt->second.version << " -> "
                              << toIt->second.version << "\n";
                }
                if (fromIt->second.scope != toIt->second.scope)
                {
                    anyDiff = true;
                    std::cout << "Package scope changed: " << name << " " << fromIt->second.scope << " -> "
                              << toIt->second.scope << "\n";
                }
                if (fromIt->second.source != toIt->second.source)
                {
                    anyDiff = true;
                    std::cout << "Package source changed: " << name << " " << fromIt->second.source << " -> "
                              << toIt->second.source << "\n";
                }
            }
        }

        [[nodiscard]] auto SplitObjectIdentity(const std::string &identity) -> std::pair<std::string, std::string>
        {
            const auto separator = identity.find(':');
            if (separator == std::string::npos || separator == 0 || separator + 1 >= identity.size())
            {
                throw std::runtime_error("explain object syntax is '<kind>:<identity>'");
            }
            return {identity.substr(0, separator), identity.substr(separator + 1)};
        }

        [[nodiscard]] auto DefineName(const std::string &value) -> std::string
        {
            if (const auto separator = value.find('='); separator != std::string::npos)
            {
                return value.substr(0, separator);
            }
            return value;
        }

        [[nodiscard]] auto SplitCommandLineArgs(const std::string &args) -> std::vector<std::string>
        {
            std::vector<std::string> result{};
            std::string current{};
            bool quoted = false;
            char quoteChar = '\0';
            bool escaping = false;
            for (const char ch : args)
            {
                if (escaping)
                {
                    current += ch;
                    escaping = false;
                    continue;
                }
                if (ch == '\\')
                {
                    escaping = true;
                    continue;
                }
                if (quoted)
                {
                    if (ch == quoteChar)
                    {
                        quoted = false;
                    }
                    else
                    {
                        current += ch;
                    }
                    continue;
                }
                if (ch == '"' || ch == '\'')
                {
                    quoted = true;
                    quoteChar = ch;
                    continue;
                }
                if (std::isspace(static_cast<unsigned char>(ch)))
                {
                    if (!current.empty())
                    {
                        result.push_back(std::move(current));
                        current.clear();
                    }
                    continue;
                }
                current += ch;
            }
            if (escaping)
            {
                current += '\\';
            }
            if (!current.empty())
            {
                result.push_back(std::move(current));
            }
            return result;
        }

        [[nodiscard]] auto RunBuiltProduct(const ProjectManifest &project, const ProfileDefinition &profile,
                                           const ParsedArgs &args, CliEventEmitter &events,
                                           std::chrono::steady_clock::time_point commandStarted,
                                           std::string_view diagnosticsTitle) -> int
        {
            std::vector<BackendStepResult> backendSteps{};
            auto buildOptions = BuildOptionsForArgs(args, backendSteps, &events);
            const auto built = BuildLaunch(
                project, profile,
                args.outputPath.has_value() ? std::optional<fs::path>{*args.outputPath} : std::nullopt, buildOptions);
            if (!built.value.has_value() || built.diagnostics.HasErrors())
            {
                EmitDiagnostics(events, built.diagnostics, "ngin " + std::string{Lower(std::string{diagnosticsTitle})});
                return EmitCommandCompleted(events, "failed", 1, commandStarted);
            }

            const auto summary = LoadLaunchManifestSummary(built.value->manifestPath);
            EmitBuildArtifactsAndSummary(events, project, profile, *built.value, summary);
            if (!summary.selectedExecutable.has_value() || summary.selectedExecutable->empty())
            {
                throw std::runtime_error("launch manifest does not declare a selected executable");
            }

            const auto executableName =
                *summary.selectedExecutable +
                (fs::exists(built.value->outputDir / "bin" / (*summary.selectedExecutable + ".exe")) ? ".exe" : "");
            const auto executablePath = built.value->outputDir / "bin" / executableName;
            if (!fs::exists(executablePath))
            {
                throw std::runtime_error("selected executable was not staged to '" + executablePath.string() + "'");
            }

            fs::path workingDirectory = summary.workingDirectory == "."
                                            ? built.value->outputDir
                                            : fs::absolute(built.value->outputDir / summary.workingDirectory);
            if (!fs::exists(workingDirectory))
            {
                workingDirectory = built.value->outputDir;
            }

            auto runArgs = SplitCommandLineArgs(profile.launch.args);
            runArgs.insert(runArgs.end(), args.runArgs.begin(), args.runArgs.end());
            const auto runStarted = std::chrono::steady_clock::now();
            events.Emit(
                CliEventType::PhaseStarted,
                EventData{}.AddString("phase", "run").AddString("label", std::string{diagnosticsTitle} + " process"));
            const auto result =
                RunProcessCapture(executablePath, runArgs, workingDirectory, [&](std::string_view text) {
                    events.Emit(CliEventType::BackendOutput, EventData{}
                                                                 .AddString("phase", "run")
                                                                 .AddString("stream", "combined")
                                                                 .AddString("text", std::string{text}));
                });
            const auto duration = static_cast<std::int64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - runStarted)
                    .count());
            EventData phaseData{};
            phaseData.AddString("phase", "run")
                .AddString("label", std::string{diagnosticsTitle} + " process")
                .AddNumber("durationMs", duration);
            if (result.exitCode == 0)
            {
                events.Emit(CliEventType::PhaseCompleted, std::move(phaseData));
            }
            else
            {
                phaseData.AddNumber("exitCode", result.exitCode);
                events.Emit(CliEventType::PhaseFailed, std::move(phaseData));
            }
            return EmitCommandCompleted(events, result.exitCode == 0 ? "success" : "failed", result.exitCode,
                                        commandStarted);
        }

        auto PrintConditionalFeatures(const ProjectManifest &project, const ProfileDefinition &profile,
                                      const std::optional<EnvironmentDefinition> &environment) -> void
        {
            if (!environment.has_value())
            {
                return;
            }
            bool printedHeader = false;
            for (const auto &feature : environment->features)
            {
                if (!HasSelection(feature.selectors))
                {
                    continue;
                }
                if (!printedHeader)
                {
                    std::cout << "    Features:\n";
                    printedHeader = true;
                }
                std::cout << "      - " << feature.name << " "
                          << (SelectionMatches(project, feature.selectors, profile) ? "included" : "excluded") << " (";
                PrintSelectorSummary(feature.selectors);
                std::cout << ")\n";
            }
        }

        [[nodiscard]] auto DirectSelectorMatches(const SelectorSet &selectors, const ProfileDefinition &profile) -> bool
        {
            if (selectors.impossible)
            {
                return false;
            }
            if (selectors.profile.has_value() && *selectors.profile != profile.name)
            {
                return false;
            }
            if (selectors.platform.has_value() && *selectors.platform != profile.platform)
            {
                return false;
            }
            if (selectors.operatingSystem.has_value() && *selectors.operatingSystem != profile.operatingSystem)
            {
                return false;
            }
            if (selectors.architecture.has_value() && *selectors.architecture != profile.architecture)
            {
                return false;
            }
            if (selectors.buildType.has_value() && *selectors.buildType != profile.buildType)
            {
                return false;
            }
            if (selectors.environment.has_value() && *selectors.environment != profile.environmentName)
            {
                return false;
            }
            return true;
        }

        [[nodiscard]] auto ConditionMap(const std::vector<ConditionDefinition> &conditions)
            -> std::unordered_map<std::string, const ConditionDefinition *>
        {
            std::unordered_map<std::string, const ConditionDefinition *> result{};
            for (const auto &condition : conditions)
            {
                result.emplace(condition.name, &condition);
            }
            return result;
        }

        [[nodiscard]] auto ConditionOrigin(const ConditionDefinition &condition) -> std::string
        {
            if (condition.builtin)
            {
                return "built-in";
            }
            std::ostringstream text{};
            text << (condition.sourceKind.empty() ? "manifest" : condition.sourceKind);
            if (!condition.sourceName.empty())
            {
                text << " '" << condition.sourceName << "'";
            }
            if (!condition.manifestPath.empty())
            {
                text << " (" << condition.manifestPath.string() << ")";
            }
            return text.str();
        }

        [[nodiscard]] auto EvalConditionNode(
            const ConditionNode &node, const std::unordered_map<std::string, const ConditionDefinition *> &conditions,
            const ProfileDefinition &profile) -> bool
        {
            switch (node.kind)
            {
            case ConditionNode::Kind::Match:
                return DirectSelectorMatches(node.match, profile);
            case ConditionNode::Kind::ConditionRef: {
                const auto it = conditions.find(node.conditionName);
                return it != conditions.end() && EvalConditionNode(it->second->body, conditions, profile);
            }
            case ConditionNode::Kind::All:
                return std::all_of(node.children.begin(), node.children.end(), [&](const ConditionNode &child) {
                    return EvalConditionNode(child, conditions, profile);
                });
            case ConditionNode::Kind::Any:
                return std::any_of(node.children.begin(), node.children.end(), [&](const ConditionNode &child) {
                    return EvalConditionNode(child, conditions, profile);
                });
            case ConditionNode::Kind::Not:
                return node.children.size() == 1 && !EvalConditionNode(node.children.front(), conditions, profile);
            }
            return false;
        }

        auto PrintConditionNode(const ConditionNode &node,
                                const std::unordered_map<std::string, const ConditionDefinition *> &conditions,
                                const ProfileDefinition &profile, const int indent) -> bool
        {
            const auto pad = std::string(static_cast<std::size_t>(indent), ' ');
            switch (node.kind)
            {
            case ConditionNode::Kind::Match: {
                const auto matched = DirectSelectorMatches(node.match, profile);
                std::cout << pad << "- Match: " << (matched ? "matched" : "not matched");
                if (HasSelection(node.match))
                {
                    std::cout << " (";
                    PrintSelectorSummary(node.match);
                    std::cout << ")";
                }
                std::cout << "\n";
                return matched;
            }
            case ConditionNode::Kind::ConditionRef: {
                const auto it = conditions.find(node.conditionName);
                if (it == conditions.end())
                {
                    std::cout << pad << "- ConditionRef " << node.conditionName << ": unknown\n";
                    return false;
                }
                const auto matched = EvalConditionNode(it->second->body, conditions, profile);
                std::cout << pad << "- ConditionRef " << node.conditionName << ": "
                          << (matched ? "matched" : "not matched") << "\n";
                PrintConditionNode(it->second->body, conditions, profile, indent + 2);
                return matched;
            }
            case ConditionNode::Kind::All: {
                std::cout << pad << "- All\n";
                bool matched = true;
                for (const auto &child : node.children)
                {
                    matched = PrintConditionNode(child, conditions, profile, indent + 2) && matched;
                }
                std::cout << pad << "  result: " << (matched ? "matched" : "not matched") << "\n";
                return matched;
            }
            case ConditionNode::Kind::Any: {
                std::cout << pad << "- Any\n";
                bool matched = false;
                for (const auto &child : node.children)
                {
                    matched = PrintConditionNode(child, conditions, profile, indent + 2) || matched;
                }
                std::cout << pad << "  result: " << (matched ? "matched" : "not matched") << "\n";
                return matched;
            }
            case ConditionNode::Kind::Not: {
                std::cout << pad << "- Not\n";
                const auto childMatched = !node.children.empty() &&
                                          PrintConditionNode(node.children.front(), conditions, profile, indent + 2);
                const auto matched = !childMatched;
                std::cout << pad << "  result: " << (matched ? "matched" : "not matched") << "\n";
                return matched;
            }
            }
            return false;
        }
    } // namespace

    auto ParseCommonArgs(int argc, char **argv, int startIndex) -> ParsedArgs
    {
        ParsedArgs args{};
        for (int argIndex = std::max(1, startIndex - 1); argIndex < argc; ++argIndex)
        {
            args.argv.push_back(argv[argIndex]);
        }
        for (int index = startIndex; index < argc; ++index)
        {
            const std::string current = argv[index];
            if (current == "--project" && index + 1 < argc)
            {
                args.projectPath = argv[++index];
            }
            else if (current == "--profile" && index + 1 < argc)
            {
                args.profileName = argv[++index];
            }
            else if (current == "--configuration" && index + 1 < argc)
            {
                args.configurationName = argv[++index];
            }
            else if (current == "--configuration")
            {
                throw std::runtime_error("--configuration expects Debug, Release, "
                                         "RelWithDebInfo, or MinSizeRel");
            }
            else if (current == "--from-profile" && index + 1 < argc)
            {
                args.fromProfileName = argv[++index];
            }
            else if (current == "--to-profile" && index + 1 < argc)
            {
                args.toProfileName = argv[++index];
            }
            else if (current == "--")
            {
                for (int argIndex = index + 1; argIndex < argc; ++argIndex)
                {
                    args.runArgs.push_back(argv[argIndex]);
                }
                break;
            }
            else if ((current == "--output" || current == "--output-dir") && index + 1 < argc)
            {
                args.outputPath = argv[++index];
            }
            else if (current == "--lock" && index + 1 < argc)
            {
                args.lockPath = argv[++index];
            }
            else if (current == "--from-lock" && index + 1 < argc)
            {
                args.fromLockPath = argv[++index];
            }
            else if (current == "--to-lock" && index + 1 < argc)
            {
                args.toLockPath = argv[++index];
            }
            else if (current == "--format" && index + 1 < argc)
            {
                args.format = argv[++index];
                if (args.format == "jsonl")
                {
                    args.eventOutputMode = EventOutputMode::JsonLines;
                }
            }
            else if (current == "--json")
            {
                args.format = "json";
            }
            else if (current == "--quiet" || current == "-q")
            {
                args.verbosity = OutputVerbosity::Quiet;
            }
            else if (current == "--verbose" || current == "-v")
            {
                args.verbosity = OutputVerbosity::Verbose;
                args.backendOutputMode = BackendOutputMode::Stream;
            }
            else if (current == "--trace")
            {
                args.verbosity = OutputVerbosity::Trace;
                args.backendOutputMode = BackendOutputMode::Stream;
            }
            else if (current == "--plain")
            {
                args.colorMode = OutputColorMode::Never;
            }
            else if (current == "--ui" && index + 1 < argc)
            {
                const std::string mode = argv[++index];
                if (mode == "auto")
                {
                    args.colorMode = OutputColorMode::Auto;
                    args.backendOutputMode = BackendOutputMode::Compact;
                }
                else if (mode == "pretty")
                {
                    args.colorMode = OutputColorMode::Auto;
                    args.backendOutputMode = BackendOutputMode::Compact;
                }
                else if (mode == "compact")
                {
                    args.backendOutputMode = BackendOutputMode::Compact;
                }
                else if (mode == "plain")
                {
                    args.colorMode = OutputColorMode::Never;
                    args.backendOutputMode = BackendOutputMode::Compact;
                }
                else if (mode == "json")
                {
                    args.format = "json";
                    args.colorMode = OutputColorMode::Never;
                    args.backendOutputMode = BackendOutputMode::Silent;
                }
                else if (mode == "jsonl")
                {
                    args.eventOutputMode = EventOutputMode::JsonLines;
                    args.colorMode = OutputColorMode::Never;
                }
                else
                {
                    throw std::runtime_error("--ui expects auto, pretty, compact, plain, json, or jsonl");
                }
            }
            else if (current == "--events" && index + 1 < argc)
            {
                const std::string mode = argv[++index];
                if (mode == "none")
                {
                    args.eventOutputMode = EventOutputMode::None;
                }
                else if (mode == "jsonl")
                {
                    args.eventOutputMode = EventOutputMode::JsonLines;
                    args.colorMode = OutputColorMode::Never;
                }
                else
                {
                    throw std::runtime_error("--events expects jsonl or none");
                }
            }
            else if (current == "--backend-output" && index + 1 < argc)
            {
                const std::string mode = argv[++index];
                if (mode == "stream")
                {
                    args.backendOutputMode = BackendOutputMode::Stream;
                }
                else if (mode == "compact")
                {
                    args.backendOutputMode = BackendOutputMode::Compact;
                }
                else if (mode == "silent")
                {
                    args.backendOutputMode = BackendOutputMode::Silent;
                }
                else
                {
                    throw std::runtime_error("--backend-output expects stream, compact, or silent");
                }
            }
            else if (current == "--color" && index + 1 < argc)
            {
                const std::string mode = argv[++index];
                if (mode == "auto")
                {
                    args.colorMode = OutputColorMode::Auto;
                }
                else if (mode == "always")
                {
                    args.colorMode = OutputColorMode::Always;
                }
                else if (mode == "never")
                {
                    args.colorMode = OutputColorMode::Never;
                }
                else
                {
                    throw std::runtime_error("--color expects auto, always, or never");
                }
            }
            else if (current == "--version" && index + 1 < argc)
            {
                args.versionRange = argv[++index];
            }
            else if (current == "--scope" && index + 1 < argc)
            {
                args.scope = argv[++index];
            }
            else if (current == "--launch" && index + 1 < argc)
            {
                args.launchName = argv[++index];
            }
            else if (current == "--build-plan")
            {
                args.graphPlan = "build";
            }
            else if (current == "--stage-plan")
            {
                args.graphPlan = "stage";
            }
            else if (current == "--package-plan")
            {
                args.graphPlan = "package";
            }
            else if (current == "--package-output-plan")
            {
                args.graphPlan = "package-output";
            }
            else if (current == "--launch-plan")
            {
                args.graphPlan = "launch";
            }
            else if (current == "--runtime-plan")
            {
                args.graphPlan = "runtime";
            }
            else if (current == "--publish-plan")
            {
                args.graphPlan = "publish";
            }
            else if (current == "--quality-plan")
            {
                args.graphPlan = "quality";
            }
            else if (current == "--environment-plan")
            {
                args.graphPlan = "environment";
            }
            else if (current == "--locked")
            {
                args.locked = true;
            }
            else if ((current == "--dependencies" || current == "--externals") && index + 1 < argc)
            {
                args.targetDir = argv[++index];
            }
            else if (current.rfind("--", 0) == 0)
            {
                throw std::runtime_error("unknown option: " + current);
            }
            else if (!args.packageName.has_value())
            {
                args.packageName = current;
            }
            else if (!args.featureName.has_value())
            {
                args.featureName = current;
            }
            else
            {
                throw std::runtime_error("unexpected argument: " + current);
            }
        }
        return args;
    }

    auto CmdList(const fs::path &root) -> int
    {
        const auto workspace = LoadWorkspaceManifest(root);
        std::cout << "Workspace: " << workspace.name << "\n";
        std::cout << "Projects:\n";
        for (const auto &projectPath : workspace.projects)
        {
            const auto project = LoadProjectManifest(projectPath);
            std::cout << "  - " << project.name << " [" << project.type << "] " << project.path.string() << "\n";
        }
        return 0;
    }

    auto CmdStatus(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)args;
        const auto workspace = LoadWorkspaceManifest(root);
        std::cout << "Workspace: " << workspace.name << "\n";
        std::cout << "  manifest: " << workspace.path.string() << "\n";
        std::cout << "  platform version: " << workspace.platformVersion << "\n";
        std::cout << "Package sources:\n";
        for (const auto &source : workspace.packageSources)
        {
            std::cout << "  - " << source.string() << (fs::exists(source) ? "" : " [missing]") << "\n";
        }
        std::cout << "Projects:\n";
        for (const auto &projectPath : workspace.projects)
        {
            std::cout << "  - " << projectPath.string() << (fs::exists(projectPath) ? "" : " [missing]") << "\n";
        }
        return 0;
    }

    auto CmdDoctor(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)args;
        int fail = 0;
        std::cout << "NGIN workspace doctor\n";
        std::cout << "  root: " << root << "\n";
        std::cout << "  workspace manifest: " << WorkspaceFilePath(root).value_or(root / "<missing>") << "\n";
        const auto reportTool = [&root, &fail](const std::string &tool, const bool required) {
            const auto resolved = ResolveToolPath(tool, root);
            if (!resolved.has_value())
            {
                std::cout << (required ? "[error] " : "[warn] ") << "missing tool: " << tool << "\n";
                if (required)
                {
                    fail = 1;
                }
                return;
            }
            std::cout << "[ok] tool: " << tool << " (" << resolved->source << ") " << resolved->path.string() << "\n";
        };

        if (!ToolExists("git"))
        {
            std::cout << "[error] missing tool: git\n";
            fail = 1;
        }
        else
        {
            std::cout << "[ok] tool: git\n";
        }
        reportTool("cmake", true);
        reportTool("ninja", false);
        std::optional<WorkspaceManifest> workspace{};
        try
        {
            workspace = LoadWorkspaceManifest(root);
            std::size_t projectsParsed = 0;
            for (const auto &projectPath : workspace->projects)
            {
                (void)LoadProjectManifest(projectPath);
                ++projectsParsed;
            }
            const auto catalog = LoadPackageCatalog(workspace, root);
            std::cout << "[ok] XML manifests parse\n";
            std::cout << "[ok] projects: " << projectsParsed << "\n";
            std::cout << "[ok] packages indexed: " << catalog.size() << "\n";
        }
        catch (const std::exception &ex)
        {
            std::cout << "[error] " << ex.what() << "\n";
            fail = 1;
        }

        if (!workspace.has_value())
        {
            std::cout << "\ndoctor result: FAIL\n";
            return 1;
        }
        for (const auto &source : workspace->packageSources)
        {
            if (!fs::exists(source))
            {
                std::cout << "[warn] package source missing: " << source.string() << "\n";
                fail = 1;
            }
        }
        std::cout << "\ndoctor result: " << (fail == 0 ? "PASS" : "FAIL") << "\n";
        return fail;
    }

    auto CmdPackageList(const fs::path &root) -> int
    {
        const auto workspace = LoadWorkspaceManifest(root);
        const auto catalog = LoadPackageCatalog(workspace, root);
        std::vector<std::string> names{};
        for (const auto &[name, _] : catalog)
        {
            names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        for (const auto &name : names)
        {
            const auto &entry = catalog.at(name);
            const auto manifest = LoadPackageManifest(entry.manifestPath);
            std::cout << manifest.name << " " << manifest.version << " " << entry.manifestPath.string();
            if (!entry.providerRoot.empty())
            {
                std::cout << " provider=" << entry.providerRoot.string();
            }
            std::cout << "\n";
        }
        return 0;
    }

    auto CmdPackageShow(const fs::path &root, const ParsedArgs &args) -> int
    {
        if (!args.packageName.has_value())
        {
            throw std::runtime_error("package show requires a package name");
        }
        const auto workspace = LoadWorkspaceManifest(root);
        const auto catalog = LoadPackageCatalog(workspace, root);
        const auto it = catalog.find(*args.packageName);
        if (it == catalog.end())
        {
            throw std::runtime_error("unknown package '" + *args.packageName + "'");
        }
        const auto manifest = LoadPackageManifest(it->second.manifestPath);
        std::cout << "Package: " << manifest.name << "\n";
        std::cout << "  version: " << manifest.version << "\n";
        std::cout << "  manifest: " << manifest.path << "\n";
        std::cout << "  provider root: ";
        if (it->second.providerRoot.empty())
        {
            std::cout << "(none)\n";
        }
        else
        {
            std::cout << it->second.providerRoot << "\n";
        }
        std::cout << "  build backend: " << (manifest.build.backend.empty() ? "(none)" : manifest.build.backend)
                  << "\n";
        std::cout << "  libraries: " << manifest.artifacts.libraries.size() << "\n";
        for (const auto &library : manifest.artifacts.libraries)
        {
            std::cout << "    - " << library.name;
            if (!library.target.empty())
            {
                std::cout << " target=" << library.target;
            }
            if (!library.linkage.empty())
            {
                std::cout << " linkage=" << library.linkage;
            }
            if (!library.origin.empty())
            {
                std::cout << " origin=" << library.origin;
            }
            if (!library.exported)
            {
                std::cout << " internal";
            }
            std::cout << "\n";
        }
        std::cout << "  executables: " << manifest.artifacts.executables.size() << "\n";
        for (const auto &executable : manifest.artifacts.executables)
        {
            std::cout << "    - " << executable.name;
            if (!executable.target.empty())
            {
                std::cout << " target=" << executable.target;
            }
            if (!executable.origin.empty())
            {
                std::cout << " origin=" << executable.origin;
            }
            if (!executable.exported)
            {
                std::cout << " internal";
            }
            std::cout << "\n";
        }
        std::cout << "  operating systems:";
        if (manifest.compatibility.operatingSystems.empty())
        {
            std::cout << " (none)";
        }
        for (const auto &operatingSystem : manifest.compatibility.operatingSystems)
        {
            std::cout << " " << operatingSystem;
        }
        std::cout << "\n";
        std::cout << "  architectures:";
        if (manifest.compatibility.architectures.empty())
        {
            std::cout << " (none)";
        }
        for (const auto &architecture : manifest.compatibility.architectures)
        {
            std::cout << " " << architecture;
        }
        std::cout << "\n";
        std::cout << "  dependencies: " << manifest.dependencies.size() << "\n";
        for (const auto &dependency : manifest.dependencies)
        {
            std::cout << "    - " << dependency.name;
            if (!dependency.versionRange.empty())
            {
                std::cout << " " << dependency.versionRange;
            }
            if (dependency.optional)
            {
                std::cout << " optional";
            }
            if (!dependency.scope.empty())
            {
                std::cout << " scope=" << dependency.scope;
            }
            std::cout << "\n";
        }
        std::cout << "  package policy: DefaultFeatures=" << manifest.defaultFeatures
                  << " LockFile=" << manifest.lockFile << "\n";
        std::cout << "  features: " << manifest.features.size() << "\n";
        for (const auto &feature : manifest.features)
        {
            std::cout << "    - " << feature.name;
            if (!feature.description.empty())
            {
                std::cout << " \"" << feature.description << "\"";
            }
            if (HasSelection(feature.selectors))
            {
                std::cout << " selectors=(";
                PrintSelectorSummary(feature.selectors);
                std::cout << ")";
            }
            std::cout << "\n";
            for (const auto &capability : feature.provides)
            {
                std::cout << "      provides: " << capability.name;
                if (capability.exclusive)
                {
                    std::cout << " exclusive";
                }
                std::cout << "\n";
            }
            for (const auto &capability : feature.requiredCapabilities)
            {
                std::cout << "      requires: " << capability.name << "\n";
            }
            for (const auto &dependency : feature.packageRefs)
            {
                std::cout << "      dependency: " << dependency.name;
                if (!dependency.versionRange.empty())
                {
                    std::cout << " " << dependency.versionRange;
                }
                if (!dependency.scope.empty())
                {
                    std::cout << " scope=" << dependency.scope;
                }
                std::cout << "\n";
            }
        }
        std::cout << "  inputs: " << manifest.inputs.size() << "\n";
        for (const auto &input : manifest.inputs)
        {
            std::cout << "    - " << (input.path.empty() ? input.pattern : input.path) << " [" << input.kind << "]";
            if (!input.role.empty())
            {
                std::cout << ":" << input.role;
            }
            if (!input.target.empty())
            {
                std::cout << " -> " << input.target;
            }
            else if (!input.targetRoot.empty())
            {
                std::cout << " -> " << input.targetRoot << "/";
            }
            std::cout << "\n";
        }
        std::cout << "  modules: " << manifest.modules.size() << "\n";
        for (const auto &module : manifest.modules)
        {
            std::cout << "    - " << module.name << " [" << module.type << "]";
            if (!module.required.empty())
            {
                std::cout << " requires:";
                for (const auto &dep : module.required)
                {
                    std::cout << " " << dep;
                }
            }
            if (!module.optional.empty())
            {
                std::cout << " optional:";
                for (const auto &dep : module.optional)
                {
                    std::cout << " " << dep;
                }
            }
            std::cout << "\n";
        }
        std::cout << "  plugins: " << manifest.plugins.size() << "\n";
        for (const auto &plugin : manifest.plugins)
        {
            std::cout << "    - " << plugin.name;
            if (plugin.optional)
            {
                std::cout << " optional";
            }
            if (!plugin.requiredModules.empty())
            {
                std::cout << " requires:";
                for (const auto &dep : plugin.requiredModules)
                {
                    std::cout << " " << dep;
                }
            }
            if (!plugin.optionalModules.empty())
            {
                std::cout << " optional-modules:";
                for (const auto &dep : plugin.optionalModules)
                {
                    std::cout << " " << dep;
                }
            }
            std::cout << "\n";
        }
        return 0;
    }

    auto CmdPackageSourcesList(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)args;
        const auto workspace = LoadWorkspaceManifest(root);
        std::cout << "Package sources for workspace: " << workspace.name << "\n";
        if (workspace.packageSources.empty() && workspace.packageSourceUrls.empty())
        {
            std::cout << "  (none)\n";
        }
        for (const auto &source : workspace.packageSources)
        {
            std::cout << "  - " << source.string();
            if (!fs::exists(source))
            {
                std::cout << " [missing]";
            }
            std::cout << "\n";
        }
        for (const auto &source : workspace.packageSourceUrls)
        {
            std::cout << "  - " << source << "\n";
        }
        if (!workspace.packageProviders.empty())
        {
            std::cout << "Package providers:\n";
            std::vector<std::string> names{};
            for (const auto &[name, _] : workspace.packageProviders)
            {
                names.push_back(name);
            }
            std::sort(names.begin(), names.end());
            for (const auto &name : names)
            {
                const auto &provider = workspace.packageProviders.at(name);
                std::cout << "  - " << name << " -> " << provider.string();
                if (!fs::exists(provider))
                {
                    std::cout << " [missing]";
                }
                std::cout << "\n";
            }
        }
        if (!workspace.externalPackageProviders.empty())
        {
            std::cout << "External package providers:\n";
            std::vector<std::string> names{};
            for (const auto &[name, _] : workspace.externalPackageProviders)
            {
                names.push_back(name);
            }
            std::sort(names.begin(), names.end());
            for (const auto &name : names)
            {
                const auto &provider = workspace.externalPackageProviders.at(name);
                std::cout << "  - " << provider.name << " (" << provider.kind << ")";
                if (!provider.root.empty())
                {
                    std::cout << " root=" << provider.root.string();
                    if (!fs::exists(provider.root))
                    {
                        std::cout << " [missing]";
                    }
                }
                if (!provider.triplet.empty())
                {
                    std::cout << " triplet=" << provider.triplet;
                }
                if (!provider.profile.empty())
                {
                    std::cout << " profile=" << provider.profile;
                }
                std::cout << "\n";
            }
        }
        return 0;
    }

    auto CmdPackageSourcesAdd(const fs::path &root, const ParsedArgs &args) -> int
    {
        if (!args.packageName.has_value() || !args.featureName.has_value())
        {
            throw std::runtime_error("package sources add requires a source name and path or URL");
        }
        const auto workspacePath = WorkspaceFilePath(root);
        if (!workspacePath.has_value())
        {
            throw std::runtime_error(root.string() + ": no .ngin workspace file found");
        }
        auto text = ReadTextIfExists(*workspacePath);
        text = InsertPackageSource(std::move(text), *args.packageName, *args.featureName);
        WriteTextFile(*workspacePath, text);
        std::cout << "Added package source\n";
        std::cout << "  workspace: " << *workspacePath << "\n";
        std::cout << "  name: " << *args.packageName << "\n";
        std::cout << "  location: " << *args.featureName << "\n";
        return 0;
    }

    auto CmdPackageSourcesRemove(const fs::path &root, const ParsedArgs &args) -> int
    {
        if (!args.packageName.has_value())
        {
            throw std::runtime_error("package sources remove requires a source name");
        }
        const auto workspacePath = WorkspaceFilePath(root);
        if (!workspacePath.has_value())
        {
            throw std::runtime_error(root.string() + ": no .ngin workspace file found");
        }
        auto text = ReadTextIfExists(*workspacePath);
        text = RemovePackageSource(std::move(text), *args.packageName);
        WriteTextFile(*workspacePath, text);
        std::cout << "Removed package source\n";
        std::cout << "  workspace: " << *workspacePath << "\n";
        std::cout << "  name: " << *args.packageName << "\n";
        return 0;
    }

    auto CmdPackageAdd(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        if (!args.packageName.has_value())
        {
            throw std::runtime_error("package add requires a package name");
        }
        if (!args.versionRange.has_value() || args.versionRange->empty())
        {
            throw std::runtime_error("package add requires --version <range>");
        }

        const auto projectPath = ResolveProjectPath(args.projectPath);
        const auto project = LoadProjectManifest(projectPath);
        if (project.productKind.empty())
        {
            throw std::runtime_error("package add requires a product-first project");
        }
        if (std::any_of(project.packageRefs.begin(), project.packageRefs.end(),
                        [&](const PackageReference &reference) { return reference.name == *args.packageName; }))
        {
            throw std::runtime_error("project already references package '" + *args.packageName + "'");
        }

        const auto scope = args.scope.value_or("Target");
        auto text = ReadTextIfExists(projectPath);
        if (text.empty())
        {
            throw std::runtime_error(projectPath.string() + ": failed to read project file");
        }
        text = InsertPackageUse(text, project.productKind, *args.packageName, *args.versionRange, scope);
        WriteTextFile(projectPath, text);

        std::cout << "Added package reference\n";
        std::cout << "  project: " << projectPath << "\n";
        std::cout << "  package: " << *args.packageName << "\n";
        std::cout << "  version: " << *args.versionRange << "\n";
        std::cout << "  scope: " << scope << "\n";
        return 0;
    }

    auto CmdProjectReferenceAdd(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        if (!args.packageName.has_value())
        {
            throw std::runtime_error("add project-reference requires a project path");
        }

        const auto projectPath = ResolveProjectPath(args.projectPath);
        const auto project = LoadProjectManifest(projectPath);
        if (project.productKind.empty())
        {
            throw std::runtime_error("add project-reference requires a product-first project");
        }

        const auto referencePathText = *args.packageName;
        const auto referencePath = fs::weakly_canonical(projectPath.parent_path() / referencePathText);
        const auto referencedProject = LoadProjectManifest(referencePath);
        if (std::any_of(project.projectRefs.begin(), project.projectRefs.end(), [&](const ProjectReference &reference) {
                return !reference.path.empty() && fs::weakly_canonical(reference.path) == referencePath;
            }))
        {
            throw std::runtime_error("project already references project '" + referencedProject.name + "'");
        }

        auto text = ReadTextIfExists(projectPath);
        if (text.empty())
        {
            throw std::runtime_error(projectPath.string() + ": failed to read project file");
        }
        text = InsertProjectReferenceUse(text, project.productKind, referencedProject.name, referencePathText);
        WriteTextFile(projectPath, text);

        std::cout << "Added project reference\n";
        std::cout << "  project: " << projectPath << "\n";
        std::cout << "  reference: " << referencedProject.name << "\n";
        std::cout << "  path: " << referencePathText << "\n";
        return 0;
    }

    auto CmdPackageRemove(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        if (!args.packageName.has_value())
        {
            throw std::runtime_error("package remove requires a package name");
        }

        const auto projectPath = ResolveProjectPath(args.projectPath);
        const auto project = LoadProjectManifest(projectPath);
        if (project.productKind.empty())
        {
            throw std::runtime_error("package remove requires a product-first project");
        }
        if (std::none_of(project.packageRefs.begin(), project.packageRefs.end(),
                         [&](const PackageReference &reference) { return reference.name == *args.packageName; }))
        {
            throw std::runtime_error("project does not reference package '" + *args.packageName + "'");
        }

        auto text = ReadTextIfExists(projectPath);
        if (text.empty())
        {
            throw std::runtime_error(projectPath.string() + ": failed to read project file");
        }
        text = RemovePackageUse(text, project.productKind, *args.packageName);
        WriteTextFile(projectPath, text);

        std::cout << "Removed package reference\n";
        std::cout << "  project: " << projectPath << "\n";
        std::cout << "  package: " << *args.packageName << "\n";
        return 0;
    }

    auto CmdPackageUpdate(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        if (!args.packageName.has_value())
        {
            throw std::runtime_error("package update requires a package name");
        }
        if (!args.versionRange.has_value() || args.versionRange->empty())
        {
            throw std::runtime_error("package update requires --version <range>");
        }

        const auto projectPath = ResolveProjectPath(args.projectPath);
        const auto project = LoadProjectManifest(projectPath);
        if (project.productKind.empty())
        {
            throw std::runtime_error("package update requires a product-first project");
        }
        const auto referenceIt =
            std::find_if(project.packageRefs.begin(), project.packageRefs.end(),
                         [&](const PackageReference &reference) { return reference.name == *args.packageName; });
        if (referenceIt == project.packageRefs.end())
        {
            throw std::runtime_error("project does not reference package '" + *args.packageName + "'");
        }

        const auto scope = args.scope.value_or(referenceIt->scope.empty() ? "Target" : referenceIt->scope);
        auto text = ReadTextIfExists(projectPath);
        if (text.empty())
        {
            throw std::runtime_error(projectPath.string() + ": failed to read project file");
        }
        text = UpdatePackageUse(text, project.productKind, *args.packageName, *args.versionRange, scope);
        WriteTextFile(projectPath, text);

        std::cout << "Updated package reference\n";
        std::cout << "  project: " << projectPath << "\n";
        std::cout << "  package: " << *args.packageName << "\n";
        std::cout << "  version: " << *args.versionRange << "\n";
        std::cout << "  scope: " << scope << "\n";
        return 0;
    }

    auto CmdPackagePack(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        CommandEventSession session{args, "package pack"};
        auto &events = session.Events();
        const auto commandStarted = std::chrono::steady_clock::now();
        EmitCommandStarted(events, args);
        const auto invocation = ResolveInvocation(args);
        EmitSelection(events, invocation);
        const auto packageOutputs = EffectivePackageOutputs(invocation.project, invocation.profile);
        if (packageOutputs.empty())
        {
            throw std::runtime_error("project '" + invocation.project.name + "' does not declare PackageOutput");
        }

        const PackageOutputDefinition *selected = nullptr;
        if (args.packageName.has_value())
        {
            const auto it =
                std::find_if(packageOutputs.begin(), packageOutputs.end(),
                             [&](const PackageOutputDefinition &output) { return output.name == *args.packageName; });
            if (it == packageOutputs.end())
            {
                throw std::runtime_error("project does not declare PackageOutput '" + *args.packageName + "'");
            }
            selected = &*it;
        }
        else if (packageOutputs.size() == 1)
        {
            selected = &packageOutputs.front();
        }
        else
        {
            throw std::runtime_error("package pack requires a PackageOutput name when "
                                     "the project declares multiple outputs");
        }

        fs::path manifestPath{};
        std::optional<fs::path> archivePath{};
        if (args.outputPath.has_value())
        {
            const auto outputPath = fs::path(*args.outputPath);
            if (outputPath.extension() == ".nginpkg")
            {
                manifestPath = outputPath;
            }
            else if (outputPath.extension() == ".nginpack")
            {
                archivePath = outputPath;
            }
            else
            {
                manifestPath = outputPath / (selected->name + ".nginpkg");
                archivePath = outputPath / (selected->name + ".nginpack");
            }
        }
        else
        {
            manifestPath = invocation.project.path.parent_path() / "dist" / (selected->name + ".nginpkg");
            archivePath = invocation.project.path.parent_path() / "dist" / (selected->name + ".nginpack");
        }

        const auto packageStarted = std::chrono::steady_clock::now();
        events.Emit(CliEventType::PhaseStarted,
                    EventData{}.AddString("phase", "package").AddString("label", "Package pack"));
        const auto manifest = GeneratePackageOutputManifest(*selected);
        if (!manifestPath.empty())
        {
            if (!manifestPath.parent_path().empty())
            {
                fs::create_directories(manifestPath.parent_path());
            }
            WriteTextFile(manifestPath, manifest);
        }
        if (archivePath.has_value())
        {
            if (!archivePath->parent_path().empty())
            {
                fs::create_directories(archivePath->parent_path());
            }
            WriteZipFile(*archivePath, std::vector<ZipFileEntry>{
                                           ZipFileEntry{
                                               .path = "package.nginpkg",
                                               .contents = manifest,
                                           },
                                       });
        }
        const auto packageDuration = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - packageStarted)
                .count());
        events.Emit(CliEventType::PhaseCompleted, EventData{}
                                                      .AddString("phase", "package")
                                                      .AddString("label", "Package pack")
                                                      .AddNumber("durationMs", packageDuration));
        if (!manifestPath.empty())
        {
            events.Emit(CliEventType::ArtifactProduced, EventData{}
                                                            .AddString("kind", "package-manifest")
                                                            .AddString("name", selected->name)
                                                            .AddString("path", manifestPath.string()));
        }
        if (archivePath.has_value())
        {
            events.Emit(CliEventType::ArtifactProduced, EventData{}
                                                            .AddString("kind", "package-archive")
                                                            .AddString("name", selected->name)
                                                            .AddString("path", archivePath->string()));
        }
        EventData summary{};
        summary.AddString("project", invocation.project.path.string())
            .AddString("package", selected->name)
            .AddString("version", selected->version);
        if (!manifestPath.empty())
        {
            summary.AddString("manifest", manifestPath.string());
        }
        if (archivePath.has_value())
        {
            summary.AddString("archive", archivePath->string());
        }
        events.Emit(CliEventType::Summary, std::move(summary));
        return EmitCommandCompleted(events, "success", 0, commandStarted);
    }

    auto CmdPackageLock(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        CommandEventSession session{args, "package lock"};
        auto &events = session.Events();
        const auto commandStarted = std::chrono::steady_clock::now();
        EmitCommandStarted(events, args);
        const auto invocation = ResolveInvocation(args);
        EmitSelection(events, invocation);
        const auto resolved = ResolveLaunch(invocation.project, invocation.profile);
        if (!resolved.value.has_value() || resolved.diagnostics.HasErrors())
        {
            EmitDiagnostics(events, resolved.diagnostics, "ngin package lock");
            return EmitCommandCompleted(events, "failed", 1, commandStarted);
        }
        const auto packageStarted = std::chrono::steady_clock::now();
        events.Emit(CliEventType::PhaseStarted,
                    EventData{}.AddString("phase", "package").AddString("label", "Package lock"));
        const auto lockPath =
            args.outputPath.has_value() ? fs::path(*args.outputPath) : DefaultLockPath(*resolved.value);
        if (!lockPath.parent_path().empty())
        {
            fs::create_directories(lockPath.parent_path());
        }
        std::ofstream out(lockPath);
        out << GenerateLockFile(*resolved.value);
        const auto packageDuration = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - packageStarted)
                .count());
        events.Emit(CliEventType::PhaseCompleted, EventData{}
                                                      .AddString("phase", "package")
                                                      .AddString("label", "Package lock")
                                                      .AddNumber("durationMs", packageDuration));
        events.Emit(CliEventType::ArtifactProduced,
                    EventData{}.AddString("kind", "lock-file").AddString("path", lockPath.string()));
        events.Emit(
            CliEventType::Summary,
            EventData{}
                .AddString("path", lockPath.string())
                .AddNumber("packages", static_cast<std::int64_t>(resolved.value->orderedPackages.size()))
                .AddNumber("features", static_cast<std::int64_t>(resolved.value->selectedPackageFeatures.size())));
        return EmitCommandCompleted(events, "success", 0, commandStarted);
    }

    auto CmdPackageVerifyLock(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        const auto invocation = ResolveInvocation(args);
        const auto resolved = ResolveLaunch(invocation.project, invocation.profile);
        if (!resolved.value.has_value() || resolved.diagnostics.HasErrors())
        {
            PrintDiagnostics(resolved.diagnostics, "Package lock", std::cout);
            return 1;
        }
        const auto lockPath = args.lockPath.has_value() ? fs::path(*args.lockPath) : DefaultLockPath(*resolved.value);
        const auto existing = ReadTextIfExists(lockPath);
        if (existing.empty())
        {
            std::cout << "Package lock verification failed\n";
            std::cout << "  missing: " << lockPath << "\n";
            return 1;
        }
        const auto expected = GenerateLockFile(*resolved.value);
        if (existing != expected)
        {
            std::cout << "Package lock verification failed\n";
            std::cout << "  path: " << lockPath << "\n";
            std::cout << "  reason: resolved package graph differs from lock file\n";
            return 1;
        }
        std::cout << "Package lock verified\n";
        std::cout << "  path: " << lockPath << "\n";
        return 0;
    }

    [[nodiscard]] auto ExtractNginPackManifestPayloadForRestore(const fs::path &archivePath) -> std::string
    {
        return ReadZipEntry(archivePath, "package.nginpkg");
    }

    auto CmdRestore(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        CommandEventSession session{args, "restore"};
        auto &events = session.Events();
        const auto commandStarted = std::chrono::steady_clock::now();
        EmitCommandStarted(events, args);
        const auto invocation = ResolveInvocation(args);
        EmitSelection(events, invocation);
        const auto resolved = ResolveLaunch(invocation.project, invocation.profile);
        if (!resolved.value.has_value() || resolved.diagnostics.HasErrors())
        {
            EmitDiagnostics(events, resolved.diagnostics, "ngin restore");
            return EmitCommandCompleted(events, "failed", 1, commandStarted);
        }

        const auto restoreStarted = std::chrono::steady_clock::now();
        events.Emit(CliEventType::PhaseStarted,
                    EventData{}.AddString("phase", "restore").AddString("label", "Package restore"));
        const auto lockPath = args.lockPath.has_value() ? fs::path(*args.lockPath) : DefaultLockPath(*resolved.value);
        const auto expectedLock = GenerateLockFile(*resolved.value);
        if (args.locked)
        {
            const auto existingLock = ReadTextIfExists(lockPath);
            if (existingLock.empty())
            {
                events.Emit(CliEventType::Diagnostic,
                            EventData{}
                                .AddString("severity", "error")
                                .AddString("source", "ngin restore")
                                .AddString("message", "locked restore failed: missing lock file")
                                .AddString("subject", lockPath.string()));
                events.Emit(CliEventType::PhaseFailed,
                            EventData{}
                                .AddString("phase", "restore")
                                .AddString("label", "Package restore")
                                .AddNumber("durationMs", CommandDurationMilliseconds(restoreStarted))
                                .AddNumber("exitCode", 1));
                return EmitCommandCompleted(events, "failed", 1, commandStarted);
            }
            if (existingLock != expectedLock)
            {
                events.Emit(CliEventType::Diagnostic,
                            EventData{}
                                .AddString("severity", "error")
                                .AddString("source", "ngin restore")
                                .AddString("message", "locked restore failed: resolved package "
                                                      "graph differs from lock file")
                                .AddString("subject", lockPath.string()));
                events.Emit(CliEventType::PhaseFailed,
                            EventData{}
                                .AddString("phase", "restore")
                                .AddString("label", "Package restore")
                                .AddNumber("durationMs", CommandDurationMilliseconds(restoreStarted))
                                .AddNumber("exitCode", 1));
                return EmitCommandCompleted(events, "failed", 1, commandStarted);
            }
        }

        std::string providerRestoreError{};
        if (!RestoreExternalProviderPackages(*resolved.value, args, events, providerRestoreError))
        {
            return EmitRestoreFailure(events, restoreStarted, commandStarted, providerRestoreError);
        }

        const auto storeRoot =
            args.outputPath.has_value() ? fs::path(*args.outputPath) : DefaultPackageStorePath(*resolved.value);
        fs::create_directories(storeRoot);
        for (const auto &package : resolved.value->orderedPackages)
        {
            const auto packageDir = storeRoot / package.manifest.name / package.manifest.version;
            fs::create_directories(packageDir);
            fs::copy_file(package.manifest.path, packageDir / package.manifest.path.filename(),
                          fs::copy_options::overwrite_existing);
            if (package.manifest.path.extension() == ".nginpack")
            {
                ExtractZipFile(package.manifest.path, packageDir);
                if (!fs::exists(packageDir / "package.nginpkg"))
                {
                    WriteTextFile(packageDir / "package.nginpkg",
                                  ExtractNginPackManifestPayloadForRestore(package.manifest.path));
                }
            }
        }

        if (!args.locked)
        {
            if (!lockPath.parent_path().empty())
            {
                fs::create_directories(lockPath.parent_path());
            }
            WriteTextFile(lockPath, expectedLock);
        }

        events.Emit(CliEventType::PhaseCompleted,
                    EventData{}
                        .AddString("phase", "restore")
                        .AddString("label", "Package restore")
                        .AddNumber("durationMs", CommandDurationMilliseconds(restoreStarted)));
        events.Emit(CliEventType::ArtifactProduced,
                    EventData{}.AddString("kind", "lock-file").AddString("path", lockPath.string()));
        events.Emit(CliEventType::Summary,
                    EventData{}
                        .AddString("store", storeRoot.string())
                        .AddString("lock", lockPath.string())
                        .AddString("locked", args.locked ? "true" : "false")
                        .AddNumber("packages", static_cast<std::int64_t>(resolved.value->orderedPackages.size())));
        if (IsVerbose(args) && !resolved.value->orderedPackages.empty())
        {
            PrintSection(args, "Resolved packages");
            for (const auto &package : resolved.value->orderedPackages)
            {
                PrintItem(args, package.manifest.name, package.manifest.version);
            }
        }
        return EmitCommandCompleted(events, "success", 0, commandStarted);
    }

    auto CmdSettingsInit(const fs::path &root, const ParsedArgs &args) -> int
    {
        const auto projectPath = ResolveProjectPath(args.projectPath);
        (void)root;
        const auto projectRoot = RootDirFrom(projectPath.parent_path()).value_or(projectPath.parent_path());
        const auto settingsPath = projectRoot / ".ngin/local/user.nginsettings";
        bool createdSettings = false;
        if (!fs::exists(settingsPath))
        {
            fs::create_directories(settingsPath.parent_path());
            std::ofstream out(settingsPath);
            out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                << "<LocalSettings SchemaVersion=\"1\">\n"
                << "  <Settings>\n"
                << "  </Settings>\n"
                << "</LocalSettings>\n";
            createdSettings = true;
        }

        const auto gitignorePath = projectRoot / ".gitignore";
        bool updatedGitignore = false;
        const auto gitignoreText = ReadTextIfExists(gitignorePath);
        if (!ContainsGitignoreEntry(gitignoreText, ".ngin/local/") && !ContainsGitignoreEntry(gitignoreText, ".ngin/*"))
        {
            std::ofstream out(gitignorePath, std::ios::app);
            if (!gitignoreText.empty() && gitignoreText.back() != '\n')
            {
                out << "\n";
            }
            out << ".ngin/local/\n";
            updatedGitignore = true;
        }

        std::cout << "Initialized local settings\n";
        std::cout << "  settings: " << settingsPath << (createdSettings ? " [created]" : " [exists]") << "\n";
        std::cout << "  gitignore: " << gitignorePath << (updatedGitignore ? " [updated]" : " [ok]") << "\n";
        std::cout << "  import from project manifests with a path relative to the "
                     "project file\n";
        return 0;
    }

    auto CmdVariablesExplain(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        const auto invocation = ResolveInvocation(args);
        const auto resolved = ResolveLaunch(invocation.project, invocation.profile);
        if (!resolved.value.has_value() || resolved.diagnostics.HasErrors())
        {
            PrintDiagnostics(resolved.diagnostics, "Variables", std::cout);
            return 1;
        }

        std::cout << "Variables for profile: " << resolved.value->profile.name << "\n";
        if (resolved.value->environmentVariables.empty())
        {
            std::cout << "  (none)\n";
        }
        for (const auto &variable : resolved.value->environmentVariables)
        {
            std::cout << "  " << variable.name << " = ";
            if (!variable.resolved)
            {
                std::cout << "<missing>";
            }
            else if (variable.secret)
            {
                std::cout << "<secret>";
            }
            else
            {
                std::cout << variable.value;
            }
            std::cout << "    source: " << variable.resolvedSource << "\n";
        }
        PrintDiagnostics(resolved.diagnostics, "Variables", std::cout);
        return 0;
    }

    auto CmdExplainCondition(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        if (!args.packageName.has_value() || args.packageName->empty())
        {
            throw std::runtime_error("explain condition requires a condition name");
        }
        const auto invocation = ResolveInvocation(args);
        const auto conditions = ConditionMap(invocation.project.conditions);
        const auto it = conditions.find(*args.packageName);
        if (it == conditions.end())
        {
            std::vector<std::string> names{};
            for (const auto &[name, _] : conditions)
            {
                names.push_back(name);
            }
            std::sort(names.begin(), names.end());
            std::ostringstream available{};
            for (std::size_t index = 0; index < names.size(); ++index)
            {
                if (index != 0)
                {
                    available << ", ";
                }
                available << names[index];
            }
            throw std::runtime_error("unknown condition '" + *args.packageName + "' (available: " + available.str() +
                                     ")");
        }

        const auto &condition = *it->second;
        const auto matched = EvalConditionNode(condition.body, conditions, invocation.profile);
        std::cout << "Condition: " << condition.name << "\n";
        std::cout << "  result: " << (matched ? "matched" : "not matched") << "\n";
        std::cout << "  origin: " << ConditionOrigin(condition) << "\n";
        std::cout << "  profile: " << invocation.profile.name << " BuildType=" << invocation.profile.buildType
                  << " Platform=" << invocation.profile.platform
                  << " OperatingSystem=" << invocation.profile.operatingSystem
                  << " Architecture=" << invocation.profile.architecture
                  << " Environment=" << invocation.profile.environmentName << "\n";
        std::cout << "  tree:\n";
        PrintConditionNode(condition.body, conditions, invocation.profile, 4);
        return 0;
    }

    auto CmdExplainPackageFeature(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        if (!args.packageName.has_value() || !args.featureName.has_value())
        {
            throw std::runtime_error("explain package-feature requires a package name and feature name");
        }
        const auto invocation = ResolveInvocation(args);
        const auto resolved = ResolveLaunch(invocation.project, invocation.profile);
        if (!resolved.value.has_value() || resolved.diagnostics.HasErrors())
        {
            PrintDiagnostics(resolved.diagnostics, "Package feature", std::cout);
            return 1;
        }

        const auto packageIt =
            std::find_if(resolved.value->orderedPackages.begin(), resolved.value->orderedPackages.end(),
                         [&](const ResolvedPackage &package) { return package.manifest.name == *args.packageName; });
        if (packageIt == resolved.value->orderedPackages.end())
        {
            throw std::runtime_error("package '" + *args.packageName + "' is not selected");
        }
        const auto featureIt =
            std::find_if(packageIt->manifest.features.begin(), packageIt->manifest.features.end(),
                         [&](const PackageManifest::Feature &feature) { return feature.name == *args.featureName; });
        if (featureIt == packageIt->manifest.features.end())
        {
            throw std::runtime_error("package '" + *args.packageName + "' does not declare feature '" +
                                     *args.featureName + "'");
        }
        const auto selectedIt = std::find_if(
            resolved.value->selectedPackageFeatures.begin(), resolved.value->selectedPackageFeatures.end(),
            [&](const SelectedPackageFeature &feature) {
                return feature.packageName == *args.packageName && feature.featureName == *args.featureName;
            });
        const auto matchedSelectors =
            SelectionMatches(packageIt->manifest.conditions, featureIt->selectors, invocation.profile);
        std::cout << "Package feature: " << *args.packageName << "::" << *args.featureName << "\n";
        std::cout << "  result: "
                  << (selectedIt != resolved.value->selectedPackageFeatures.end() ? "selected" : "not selected")
                  << "\n";
        std::cout << "  selector result: " << (matchedSelectors ? "matched" : "not matched") << "\n";
        std::cout << "  manifest: " << packageIt->manifest.path << "\n";
        std::cout << "  dependencies: " << featureIt->packageRefs.size() << "\n";
        for (const auto &dependency : featureIt->packageRefs)
        {
            std::cout << "    - " << dependency.name;
            if (!dependency.versionRange.empty())
            {
                std::cout << " " << dependency.versionRange;
            }
            std::cout << "\n";
        }
        std::cout << "  provides: " << featureIt->provides.size() << "\n";
        for (const auto &capability : featureIt->provides)
        {
            std::cout << "    - " << capability.name << (capability.exclusive ? " exclusive" : "") << "\n";
        }
        std::cout << "  requires: " << featureIt->requiredCapabilities.size() << "\n";
        for (const auto &capability : featureIt->requiredCapabilities)
        {
            std::cout << "    - " << capability.name << "\n";
        }
        std::cout << "  inputs: " << featureIt->inputs.size() << "\n";
        std::cout << "  variables: " << featureIt->variables.size() << "\n";
        std::cout << "  runtime modules: " << featureIt->runtime.modules.size() << "\n";
        return 0;
    }

    auto CmdExplainGenerator(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        if (!args.packageName.has_value())
        {
            throw std::runtime_error("explain generator requires a generator name");
        }
        const auto invocation = ResolveInvocation(args);
        const auto resolved = ResolveLaunch(invocation.project, invocation.profile);
        if (!resolved.value.has_value() || resolved.diagnostics.HasErrors())
        {
            PrintDiagnostics(resolved.diagnostics, "Generator", std::cout);
            return 1;
        }
        const auto generatorIt = std::find_if(
            resolved.value->generators.begin(), resolved.value->generators.end(),
            [&](const ResolvedGenerator &generator) { return generator.declaration.name == *args.packageName; });
        if (generatorIt == resolved.value->generators.end())
        {
            throw std::runtime_error("generator '" + *args.packageName + "' is not selected");
        }
        const auto &generator = *generatorIt;
        std::cout << "Generator: " << generator.declaration.name << "\n";
        std::cout << "  result: selected\n";
        std::cout << "  kind: " << generator.declaration.kind << "\n";
        std::cout << "  owner: " << generator.ownerKind << " " << generator.ownerName << "\n";
        std::cout << "  manifest: " << generator.manifestPath << "\n";
        if (!generator.declaration.packageName.empty())
        {
            std::cout << "  package: " << generator.declaration.packageName << "\n";
        }
        if (!generator.declaration.toolName.empty())
        {
            std::cout << "  tool: " << generator.declaration.toolName << "\n";
        }
        if (generator.declaration.hasInlineTool)
        {
            std::cout << "  inline tool:";
            if (!generator.declaration.inlineTool.executable.empty())
            {
                std::cout << " executable=" << generator.declaration.inlineTool.executable;
            }
            std::cout << "\n";
        }
        std::cout << "  inputs: " << generator.declaration.inputs.size() << "\n";
        for (const auto &input : generator.declaration.inputs)
        {
            std::cout << "    - " << input.kind << " " << input.path << "\n";
        }
        std::cout << "  outputs: " << generator.declaration.outputs.size() << "\n";
        for (const auto &output : generator.declaration.outputs)
        {
            std::cout << "    - " << output.kind << " Role=" << output.role << " Path=" << output.path << "\n";
        }
        std::cout << "  arguments: " << generator.declaration.arguments.size() << "\n";
        return 0;
    }

    [[nodiscard]] auto NamedConventions(const ProjectManifest &project, const ProfileDefinition &profile,
                                        const std::string &productKind)
        -> std::vector<std::pair<std::string, std::string>>;

    [[nodiscard]] auto BuildCompositionGraph(const LoadedInvocation &invocation,
                                             const DiagnosticResult<ResolvedLaunch> &resolvedResult)
        -> CompositionGraph;

    auto CmdExplainObject(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        if (!args.packageName.has_value())
        {
            throw std::runtime_error("explain requires an object identity such as property:Language");
        }
        const auto [kind, identity] = SplitObjectIdentity(*args.packageName);
        const auto invocation = ResolveInvocation(args);
        const auto resolved = ResolveLaunch(invocation.project, invocation.profile);
        if (!resolved.value.has_value() || resolved.diagnostics.HasErrors())
        {
            PrintDiagnostics(resolved.diagnostics, "Explain", std::cout);
            return 1;
        }
        const auto graph = BuildCompositionGraph(invocation, resolved);

        if (kind == "property")
        {
            std::cout << "Property: " << identity << "\n";
            if (identity == "Language")
            {
                std::cout << "  value: " << invocation.project.build.language << "\n";
                std::cout << "  standard: " << invocation.project.build.languageStandard << "\n";
                if (invocation.project.build.languageExplicit)
                {
                    std::cout << "  source: manifest override\n";
                }
                else
                {
                    const auto convention =
                        invocation.project.build.language == "CXX" && invocation.project.build.languageStandard == "23"
                            ? "NGIN.Cpp.Defaults"
                            : "NGIN.Workspace.LanguageDefaults";
                    std::cout << "  convention: " << convention << "\n";
                    std::cout << "  reason: project did not declare Language\n";
                    std::cout << "  override: <Language Standard=\"C++20\" "
                                 "Required=\"true\" Extensions=\"false\" />\n";
                }
                return 0;
            }
            if (identity == "BuildType")
            {
                std::cout << "  value: " << resolved.value->profile.buildType << "\n";
                std::cout << "  source: selected profile " << resolved.value->profile.name << "\n";
                return 0;
            }
            if (identity == "HostPlatform")
            {
                std::cout << "  value: " << resolved.value->profile.hostPlatform << "\n";
                std::cout << "  source: selected profile " << resolved.value->profile.name << "\n";
                return 0;
            }
            if (identity == "TargetPlatform" || identity == "Platform")
            {
                std::cout << "  value: " << resolved.value->profile.platform << "\n";
                std::cout << "  operatingSystem: " << resolved.value->profile.operatingSystem << "\n";
                std::cout << "  architecture: " << resolved.value->profile.architecture << "\n";
                std::cout << "  source: selected profile " << resolved.value->profile.name << "\n";
                return 0;
            }
            if (identity == "Toolchain")
            {
                std::cout << "  value: "
                          << (resolved.value->profile.toolchain.empty() ? "(default)"
                                                                        : resolved.value->profile.toolchain)
                          << "\n";
                std::cout << "  source: selected profile " << resolved.value->profile.name << "\n";
                return 0;
            }
            if (identity == "Environment")
            {
                std::cout << "  value: " << resolved.value->profile.environmentName << "\n";
                std::cout << "  source: selected profile " << resolved.value->profile.name << "\n";
                return 0;
            }
            throw std::runtime_error("unknown explain property '" + identity + "'");
        }

        if (kind == "convention")
        {
            std::cout << "Convention: " << identity << "\n";
            const auto conventionIt = std::find_if(graph.conventions.begin(), graph.conventions.end(),
                                                   [&](const auto &convention) { return convention.name == identity; });
            if (conventionIt == graph.conventions.end())
            {
                std::cout << "  result: not selected\n";
                return 0;
            }
            std::cout << "  result: selected\n";
            std::cout << "  reason: " << conventionIt->reason << "\n";
            std::cout << "  project: " << graph.identity.project << "\n";
            std::cout << "  product: " << graph.identity.product << "\n";
            std::cout << "  profile: " << graph.identity.profile << "\n";
            std::cout << "  provenance: " << conventionIt->provenance.sourceKind << " "
                      << conventionIt->provenance.sourceName << "\n";
            return 0;
        }

        if (kind == "define")
        {
            std::cout << "Define: " << identity << "\n";
            bool found = false;
            for (const auto &unit : resolved.value->projectUnits)
            {
                for (const auto &setting : EffectiveBuildSettings(unit.project, unit.profile,
                                                                  unit.project.build.compileDefinitions, "Define"))
                {
                    if (DefineName(setting.value) != identity)
                    {
                        continue;
                    }
                    found = true;
                    std::cout << "  value: " << setting.value << "\n";
                    if (!setting.provenance.sourceKind.empty())
                    {
                        std::cout << "  owner: " << setting.provenance.sourceKind << " "
                                  << setting.provenance.sourceName << "\n";
                        std::cout << "  manifest: " << setting.provenance.manifestPath << "\n";
                        std::cout << "  reason: " << setting.provenance.reason << "\n";
                    }
                    else if (setting.selectors.profile.has_value())
                    {
                        std::cout << "  owner: project-profile " << *setting.selectors.profile << "\n";
                        std::cout << "  manifest: " << unit.project.path << "\n";
                    }
                    else
                    {
                        std::cout << "  owner: project " << unit.project.name << "\n";
                        std::cout << "  manifest: " << unit.project.path << "\n";
                    }
                }
            }
            for (const auto &feature : resolved.value->selectedPackageFeatures)
            {
                for (const auto &setting : feature.build.compileDefinitions)
                {
                    if (DefineName(setting.value) != identity)
                    {
                        continue;
                    }
                    found = true;
                    std::cout << "  value: " << setting.value << "\n";
                    std::cout << "  owner: package feature " << feature.packageName << "/" << feature.featureName
                              << "\n";
                    std::cout << "  manifest: " << feature.manifestPath << "\n";
                }
            }
            if (!found)
            {
                std::cout << "  result: not selected\n";
            }
            return 0;
        }

        if (kind == "source")
        {
            std::cout << "Source: " << identity << "\n";
            const auto requested = fs::path(identity).lexically_normal();
            const auto inputIt = std::find_if(resolved.value->inputs.begin(), resolved.value->inputs.end(),
                                              [&](const ResolvedInput &input) {
                                                  if (input.kind != "Source" && input.kind != "Generated")
                                                  {
                                                      return false;
                                                  }
                                                  return fs::path(input.source).lexically_normal() == requested ||
                                                         input.absoluteSourcePath.lexically_normal() == requested;
                                              });
            if (inputIt == resolved.value->inputs.end())
            {
                std::cout << "  result: not selected\n";
                return 0;
            }
            std::cout << "  result: selected\n";
            std::cout << "  source: " << inputIt->source << "\n";
            std::cout << "  absoluteSourcePath: " << inputIt->absoluteSourcePath << "\n";
            std::cout << "  kind: " << inputIt->kind << "\n";
            std::cout << "  role: " << inputIt->role << "\n";
            std::cout << "  visibility: " << inputIt->visibility << "\n";
            std::cout << "  owner: " << inputIt->ownerKind << " " << inputIt->ownerName << "\n";
            std::cout << "  manifest: " << inputIt->manifestPath << "\n";
            return 0;
        }

        if (kind == "stage")
        {
            std::cout << "Stage: " << identity << "\n";
            const auto requested = fs::path(identity).lexically_normal();
            const auto inputIt =
                std::find_if(resolved.value->inputs.begin(), resolved.value->inputs.end(),
                             [&](const ResolvedInput &input) { return input.stagedRelativePath == requested; });
            if (inputIt == resolved.value->inputs.end())
            {
                std::cout << "  result: not selected\n";
                return 0;
            }
            std::cout << "  source: " << inputIt->source << "\n";
            std::cout << "  absoluteSourcePath: " << inputIt->absoluteSourcePath << "\n";
            std::cout << "  kind: " << inputIt->kind << "\n";
            std::cout << "  owner: " << inputIt->ownerKind << " " << inputIt->ownerName << "\n";
            std::cout << "  manifest: " << inputIt->manifestPath << "\n";
            return 0;
        }

        if (kind == "package")
        {
            std::cout << "Package: " << identity << "\n";
            const auto packageIt =
                std::find_if(resolved.value->orderedPackages.begin(), resolved.value->orderedPackages.end(),
                             [&](const ResolvedPackage &package) { return package.manifest.name == identity; });
            if (packageIt == resolved.value->orderedPackages.end())
            {
                std::cout << "  result: not selected\n";
                return 0;
            }
            std::cout << "  result: selected\n";
            std::cout << "  version: " << packageIt->manifest.version << "\n";
            if (const auto scopeIt = resolved.value->packageScopes.find(packageIt->manifest.name);
                scopeIt != resolved.value->packageScopes.end() && !scopeIt->second.empty())
            {
                std::cout << "  scope: " << scopeIt->second << "\n";
            }
            std::cout << "  source: " << packageIt->source << "\n";
            std::cout << "  manifest: " << packageIt->manifest.path << "\n";
            return 0;
        }

        if (kind == "feature")
        {
            std::cout << "Feature: " << identity << "\n";
            const auto slash = identity.find('/');
            if (slash == std::string::npos)
            {
                throw std::runtime_error("feature explain identity must be Package/Feature");
            }
            const auto packageName = identity.substr(0, slash);
            const auto featureName = identity.substr(slash + 1);
            const auto featureIt =
                std::find_if(resolved.value->selectedPackageFeatures.begin(),
                             resolved.value->selectedPackageFeatures.end(), [&](const SelectedPackageFeature &feature) {
                                 return feature.packageName == packageName && feature.featureName == featureName;
                             });
            if (featureIt == resolved.value->selectedPackageFeatures.end())
            {
                std::cout << "  result: not selected\n";
                return 0;
            }
            std::cout << "  result: selected\n";
            std::cout << "  packageVersion: " << featureIt->packageVersion << "\n";
            std::cout << "  manifest: " << featureIt->manifestPath << "\n";
            return 0;
        }

        if (kind == "generator")
        {
            std::cout << "Generator: " << identity << "\n";
            const auto generatorIt = std::find_if(
                resolved.value->generators.begin(), resolved.value->generators.end(),
                [&](const ResolvedGenerator &generator) { return generator.declaration.name == identity; });
            if (generatorIt == resolved.value->generators.end())
            {
                std::cout << "  result: not selected\n";
                return 0;
            }
            std::cout << "  result: selected\n";
            std::cout << "  owner: " << generatorIt->ownerKind << " " << generatorIt->ownerName << "\n";
            std::cout << "  tool: " << generatorIt->declaration.toolName << "\n";
            std::cout << "  manifest: " << generatorIt->manifestPath << "\n";
            std::cout << "  outputs: " << generatorIt->declaration.outputs.size() << "\n";
            return 0;
        }

        if (kind == "launch")
        {
            std::cout << "Launch: " << identity << "\n";
            const auto launches = EffectiveLaunches(resolved.value->project, resolved.value->profile);
            const auto launchIt = std::find_if(launches.begin(), launches.end(),
                                               [&](const LaunchDefinition &launch) { return launch.name == identity; });
            if (launchIt == launches.end())
            {
                std::cout << "  result: not found\n";
                return 0;
            }
            const auto selected = launchIt->name == resolved.value->profile.launch.name;
            std::cout << "  result: " << (selected ? "selected" : "available") << "\n";
            std::cout << "  name: " << launchIt->name << "\n";
            std::cout << "  executable: "
                      << (launchIt->executable.has_value()                 ? *launchIt->executable
                          : resolved.value->selectedExecutable.has_value() ? resolved.value->selectedExecutable->name
                                                                           : "(none)")
                      << "\n";
            std::cout << "  workingDirectory: " << launchIt->workingDirectory << "\n";
            std::cout << "  args: " << launchIt->args << "\n";
            return 0;
        }

        if (kind == "publish")
        {
            std::cout << "Publish: " << identity << "\n";
            const auto publishes = EffectivePublishes(resolved.value->project, resolved.value->profile);
            const auto publishIt =
                std::find_if(publishes.begin(), publishes.end(),
                             [&](const PublishDefinition &publish) { return publish.name == identity; });
            if (publishIt == publishes.end())
            {
                std::cout << "  result: not selected\n";
                return 0;
            }
            std::cout << "  result: selected\n";
            std::cout << "  kind: " << publishIt->kind << "\n";
            if (!publishIt->format.empty())
            {
                std::cout << "  format: " << publishIt->format << "\n";
            }
            std::cout << "  output: " << publishIt->output << "\n";
            std::cout << "  includeStage: " << (publishIt->includeStage ? "true" : "false") << "\n";
            std::cout << "  includeRuntimeDependencies: " << (publishIt->includeRuntimeDependencies ? "true" : "false")
                      << "\n";
            std::cout << "  includeSymbols: " << (publishIt->includeSymbols ? "true" : "false") << "\n";
            return 0;
        }

        if (kind == "package-output")
        {
            std::cout << "Package output: " << identity << "\n";
            const auto packageOutputs = EffectivePackageOutputs(invocation.project, invocation.profile);
            const auto outputIt =
                std::find_if(packageOutputs.begin(), packageOutputs.end(),
                             [&](const PackageOutputDefinition &output) { return output.name == identity; });
            if (outputIt == packageOutputs.end())
            {
                std::cout << "  result: not selected\n";
                return 0;
            }
            std::cout << "  result: selected\n";
            std::cout << "  version: " << outputIt->version << "\n";
            if (!outputIt->from.empty())
            {
                std::cout << "  from: " << outputIt->from << "\n";
            }
            if (!outputIt->description.empty())
            {
                std::cout << "  description: " << outputIt->description << "\n";
            }
            if (!outputIt->license.empty())
            {
                std::cout << "  license: " << outputIt->license << "\n";
            }
            std::cout << "  headers: " << outputIt->headers.size() << "\n";
            std::cout << "  libraries: " << outputIt->libraries.size() << "\n";
            std::cout << "  tools: " << outputIt->tools.size() << "\n";
            std::cout << "  capabilities: " << outputIt->capabilities.size() << "\n";
            if (!outputIt->abiTag.empty())
            {
                std::cout << "  abi: " << outputIt->abiTag << "\n";
            }
            return 0;
        }

        if (kind == "env")
        {
            std::cout << "Environment variable: " << identity << "\n";
            const auto variableIt =
                std::find_if(resolved.value->environmentVariables.begin(), resolved.value->environmentVariables.end(),
                             [&](const EnvironmentVariable &variable) { return variable.name == identity; });
            if (variableIt == resolved.value->environmentVariables.end())
            {
                std::cout << "  result: not selected\n";
                return 0;
            }
            std::cout << "  result: selected\n";
            std::cout << "  value: " << (variableIt->secret ? "<redacted>" : variableIt->value) << "\n";
            std::cout << "  secret: " << (variableIt->secret ? "true" : "false") << "\n";
            std::cout << "  resolved: " << (variableIt->resolved ? "true" : "false") << "\n";
            std::cout << "  source: " << variableIt->resolvedSource << "\n";
            return 0;
        }

        if (kind == "analyzer")
        {
            std::cout << "Analyzer: " << identity << "\n";
            const auto analyzers = EffectiveAnalyzers(resolved.value->project, resolved.value->profile,
                                                      resolved.value->selectedPackageFeatures);
            const auto analyzerIt = analyzers.find(identity);
            if (analyzerIt == analyzers.end() || !analyzerIt->second.enabled)
            {
                std::cout << "  result: not selected\n";
                return 0;
            }
            std::cout << "  result: selected\n";
            std::cout << "  tool: " << AnalyzerToolName(analyzerIt->second) << "\n";
            if (!analyzerIt->second.packageName.empty())
            {
                std::cout << "  package: " << analyzerIt->second.packageName << "\n";
            }
            std::cout << "  scope: " << analyzerIt->second.scope << "\n";
            std::cout << "  severity: " << analyzerIt->second.severity << "\n";
            if (!analyzerIt->second.configPath.empty())
            {
                std::cout << "  config: " << analyzerIt->second.configPath << "\n";
            }
            return 0;
        }

        if (kind == "runtime-module")
        {
            std::cout << "Runtime module: " << identity << "\n";
            const auto required =
                std::find(resolved.value->requiredModules.begin(), resolved.value->requiredModules.end(), identity);
            const auto optional =
                std::find(resolved.value->optionalModules.begin(), resolved.value->optionalModules.end(), identity);
            if (required == resolved.value->requiredModules.end() && optional == resolved.value->optionalModules.end())
            {
                std::cout << "  result: not selected\n";
                return 0;
            }
            std::cout << "  result: selected\n";
            std::cout << "  selection: "
                      << (required != resolved.value->requiredModules.end() ? "required" : "optional") << "\n";
            return 0;
        }

        throw std::runtime_error("unknown explain object kind '" + kind + "'");
    }

    [[nodiscard]] auto NamedConventions(const ProjectManifest &project, const ProfileDefinition &profile,
                                        const std::string &productKind)
        -> std::vector<std::pair<std::string, std::string>>
    {
        std::vector<std::pair<std::string, std::string>> conventions{};
        if (!productKind.empty())
        {
            conventions.emplace_back("NGIN." + productKind, "selected by product kind");
        }
        if (!project.build.languageExplicit)
        {
            conventions.emplace_back(project.build.language == "CXX" && project.build.languageStandard == "23"
                                         ? "NGIN.Cpp.Defaults"
                                         : "NGIN.Workspace.LanguageDefaults",
                                     "project did not declare a language override");
        }
        if (!project.build.backendExplicit)
        {
            conventions.emplace_back(project.build.backend == "CMake" && project.build.mode == "Generated"
                                         ? "NGIN.CMake.Generated"
                                         : "NGIN.Workspace.BackendDefaults",
                                     "project did not declare a backend override");
        }
        if (!profile.name.empty())
        {
            conventions.emplace_back("NGIN.Profile." + profile.name, "selected profile overlay");
        }
        if (profile.hostPlatform == "host")
        {
            conventions.emplace_back("NGIN.HostPlatform", "host platform alias was selected");
        }
        return conventions;
    }

    [[nodiscard]] auto BuildCompositionGraph(const LoadedInvocation &invocation,
                                             const DiagnosticResult<ResolvedLaunch> &resolvedResult) -> CompositionGraph
    {
        const auto *resolved = resolvedResult.value.has_value() ? &*resolvedResult.value : nullptr;
        const auto productKind =
            invocation.project.productKind.empty() ? invocation.project.type : invocation.project.productKind;
        const auto effectivePublishes = EffectivePublishes(invocation.project, invocation.profile);
        const auto effectivePackageOutputs = EffectivePackageOutputs(invocation.project, invocation.profile);
        const auto effectiveAnalyzers = EffectiveAnalyzers(invocation.project, invocation.profile,
                                                           resolved == nullptr ? std::vector<SelectedPackageFeature>{}
                                                                               : resolved->selectedPackageFeatures);
        const auto effectiveLaunches = EffectiveLaunches(invocation.project, invocation.profile);
        const auto packageFeatureIdentity = [](const std::string &packageName, const std::string &featureName) {
            return packageName + "::" + featureName;
        };

        auto projectProvenance = [&](std::string reason) -> CompositionGraph::Provenance {
            return CompositionGraph::Provenance{
                .sourceKind = "project",
                .sourceName = invocation.project.name,
                .manifestPath = invocation.project.path,
                .reason = std::move(reason),
            };
        };
        auto contributionProvenance = [&](const ContributionProvenance &provenance, std::string fallbackReason,
                                          const SelectorSet &selectors = {}) -> CompositionGraph::Provenance {
            if (!provenance.sourceKind.empty())
            {
                return CompositionGraph::Provenance{
                    .sourceKind = provenance.sourceKind,
                    .sourceName = provenance.sourceName,
                    .manifestPath = provenance.manifestPath,
                    .reason = provenance.reason.empty() ? std::move(fallbackReason) : provenance.reason,
                };
            }
            if (selectors.profile.has_value())
            {
                return CompositionGraph::Provenance{
                    .sourceKind = "project-profile",
                    .sourceName = *selectors.profile,
                    .manifestPath = invocation.project.path,
                    .reason = std::move(fallbackReason),
                };
            }
            return projectProvenance(std::move(fallbackReason));
        };
        auto profileProvenance = [&](std::string reason) -> CompositionGraph::Provenance {
            return CompositionGraph::Provenance{
                .sourceKind = "project-profile",
                .sourceName = invocation.profile.name,
                .manifestPath = invocation.project.path,
                .reason = std::move(reason),
            };
        };
        auto namedContributionProvenance = [&](const ContributionProvenance &provenance,
                                               const std::string &name,
                                               bool profileItem,
                                               std::string fallbackReason) -> CompositionGraph::Provenance {
            if (!provenance.sourceKind.empty())
            {
                return contributionProvenance(provenance, std::move(fallbackReason));
            }
            if (profileItem)
            {
                return profileProvenance(std::move(fallbackReason));
            }
            (void)name;
            return projectProvenance(std::move(fallbackReason));
        };
        auto ownerContributionProvenance = [&](const ContributionProvenance &provenance,
                                               const std::string &ownerKind,
                                               const std::string &ownerName,
                                               const fs::path &manifestPath,
                                               const SelectorSet &selectors,
                                               std::string fallbackReason) -> CompositionGraph::Provenance {
            if (!provenance.sourceKind.empty() || selectors.profile.has_value())
            {
                return contributionProvenance(provenance, std::move(fallbackReason), selectors);
            }
            if (ownerKind != "project")
            {
                return CompositionGraph::Provenance{
                    .sourceKind = ownerKind,
                    .sourceName = ownerName,
                    .manifestPath = manifestPath,
                    .reason = std::move(fallbackReason),
                };
            }
            return projectProvenance(std::move(fallbackReason));
        };
        const auto launchIsProfileItem = [&](const std::string &name) {
            return std::any_of(invocation.profile.launches.begin(), invocation.profile.launches.end(),
                               [&](const LaunchDefinition &candidate) { return candidate.name == name; }) ||
                   invocation.profile.launch.name == name;
        };
        const auto publishIsProfileItem = [&](const std::string &name) {
            return std::any_of(invocation.profile.publishes.begin(), invocation.profile.publishes.end(),
                               [&](const PublishDefinition &candidate) { return candidate.name == name; });
        };
        const auto packageOutputIsProfileItem = [&](const std::string &name) {
            return std::any_of(invocation.profile.packageOutputs.begin(), invocation.profile.packageOutputs.end(),
                               [&](const PackageOutputDefinition &candidate) { return candidate.name == name; });
        };

        CompositionGraph graph{};
        graph.state = resolved == nullptr || resolvedResult.diagnostics.HasErrors() ? "diagnostic" : "resolved";
        graph.facets = {"identity", "workspace", "project",  "product",     "profile",   "platform",
                        "package",  "build",     "generate", "stage",       "runtime",   "environment",
                        "launch",   "publish",   "quality",  "diagnostics", "provenance"};
        graph.identity = CompositionGraph::Identity{
            .project = invocation.project.name,
            .projectPath = invocation.project.path,
            .product = productKind,
            .profile = invocation.profile.name,
        };
        graph.product = CompositionGraph::Product{
            .kind = productKind,
            .outputType = invocation.project.output.kind,
            .outputName = invocation.project.output.name,
            .targetName = invocation.project.output.target,
        };
        graph.selection = CompositionGraph::Selection{
            .profile = invocation.profile.name,
            .hostPlatform = invocation.profile.hostPlatform,
            .targetPlatform = invocation.profile.platform,
            .operatingSystem = invocation.profile.operatingSystem,
            .architecture = invocation.profile.architecture,
            .toolchain = invocation.profile.toolchain,
            .environment = invocation.profile.environmentName,
            .abiTag = resolved == nullptr ? "" : resolved->targetAbiTag,
        };

        for (const auto &[name, reason] : NamedConventions(invocation.project, invocation.profile, productKind))
        {
            graph.conventions.push_back(CompositionGraph::Convention{
                .name = name,
                .reason = reason,
                .provenance =
                    CompositionGraph::Provenance{
                        .sourceKind = "convention",
                        .sourceName = name,
                        .manifestPath = invocation.project.path,
                        .reason = reason,
                    },
            });
        }

        graph.properties.push_back(CompositionGraph::Property{
            .name = "Language",
            .value = invocation.project.build.language + invocation.project.build.languageStandard,
            .provenance = invocation.project.build.languageExplicit
                              ? projectProvenance("manifest language override")
                              : projectProvenance("selected by named language convention"),
        });
        graph.properties.push_back(CompositionGraph::Property{
            .name = "BuildType",
            .value = invocation.profile.buildType,
            .provenance = profileProvenance("selected profile build type"),
        });
        graph.properties.push_back(CompositionGraph::Property{
            .name = "HostPlatform",
            .value = invocation.profile.hostPlatform,
            .provenance = profileProvenance("selected profile host platform"),
        });
        graph.properties.push_back(CompositionGraph::Property{
            .name = "TargetPlatform",
            .value = invocation.profile.platform,
            .provenance = profileProvenance("selected profile target platform"),
        });
        graph.properties.push_back(CompositionGraph::Property{
            .name = "Toolchain",
            .value = invocation.profile.toolchain,
            .provenance = profileProvenance("selected profile toolchain"),
        });
        graph.properties.push_back(CompositionGraph::Property{
            .name = "Environment",
            .value = invocation.profile.environmentName,
            .provenance = profileProvenance("selected profile environment"),
        });

        graph.summary.packages = resolved == nullptr ? 0 : resolved->orderedPackages.size();
        graph.summary.packageFeatures = resolved == nullptr ? 0 : resolved->selectedPackageFeatures.size();
        graph.summary.generators = resolved == nullptr ? 0 : resolved->generators.size();
        graph.summary.runtimeModules =
            resolved == nullptr ? 0 : resolved->requiredModules.size() + resolved->optionalModules.size();
        graph.summary.environmentVariables = resolved == nullptr ? 0 : resolved->environmentVariables.size();
        graph.summary.publishes = effectivePublishes.size();
        graph.summary.diagnostics = resolvedResult.diagnostics.entries.size();
        for (const auto &[_, analyzer] : effectiveAnalyzers)
        {
            if (analyzer.enabled)
            {
                ++graph.summary.analyzers;
            }
        }

        if (resolved != nullptr)
        {
            for (const auto &package : resolved->orderedPackages)
            {
                const auto scope = [&]() -> std::string {
                    if (const auto it = resolved->packageScopes.find(package.manifest.name);
                        it != resolved->packageScopes.end())
                    {
                        return it->second;
                    }
                    return {};
                }();
                std::vector<std::string> dependencies{};
                if (const auto edgeIt = resolved->packageEdges.find(package.manifest.name);
                    edgeIt != resolved->packageEdges.end())
                {
                    dependencies.assign(edgeIt->second.begin(), edgeIt->second.end());
                }
                graph.packages.push_back(CompositionGraph::Package{
                    .name = package.manifest.name,
                    .version = package.manifest.version,
                    .source = package.source,
                    .provider = package.manifest.build.provider,
                    .providerKind = [&]() -> std::string {
                        if (!resolved->workspace.has_value() || package.manifest.build.provider.empty())
                        {
                            return {};
                        }
                        if (const auto providerIt =
                                resolved->workspace->externalPackageProviders.find(package.manifest.build.provider);
                            providerIt != resolved->workspace->externalPackageProviders.end())
                        {
                            return providerIt->second.kind;
                        }
                        return {};
                    }(),
                    .providerPackage = ProviderPackageName(package.manifest),
                    .providerVersion = ProviderPackageVersion(package.manifest),
                    .providerRoot = package.sourceDirectory,
                    .scope = scope,
                    .closures = PackageClosuresForScope(scope),
                    .dependencies = std::move(dependencies),
                    .provenance =
                        CompositionGraph::Provenance{
                            .sourceKind = "package",
                            .sourceName = package.manifest.name,
                            .manifestPath = package.manifest.path,
                            .reason = "resolved package dependency",
                        },
                });
            }

            for (const auto &feature : resolved->selectedPackageFeatures)
            {
                graph.packageFeatures.push_back(CompositionGraph::PackageFeature{
                    .package = feature.packageName,
                    .feature = feature.featureName,
                    .packageVersion = feature.packageVersion,
                    .provenance =
                        CompositionGraph::Provenance{
                            .sourceKind = "package-feature",
                            .sourceName = packageFeatureIdentity(feature.packageName, feature.featureName),
                            .manifestPath = feature.manifestPath,
                            .reason = "selected package feature",
                        },
                });
            }

            for (const auto &unit : resolved->projectUnits)
            {
                for (const auto &definition : EffectiveBuildSettings(unit.project, unit.profile,
                                                                     unit.project.build.compileDefinitions, "Define"))
                {
                    graph.buildDefines.push_back(CompositionGraph::BuildDefine{
                        .value = definition.value,
                        .provenance = contributionProvenance(definition.provenance, "selected compile definition",
                                                             definition.selectors),
                    });
                }
            }
            for (const auto &feature : resolved->selectedPackageFeatures)
            {
                for (const auto &definition : feature.build.compileDefinitions)
                {
                    graph.buildDefines.push_back(CompositionGraph::BuildDefine{
                        .value = definition.value,
                        .provenance =
                            CompositionGraph::Provenance{
                                .sourceKind = "package-feature",
                                .sourceName = packageFeatureIdentity(feature.packageName, feature.featureName),
                                .manifestPath = feature.manifestPath,
                                .reason = "selected package feature compile definition",
                            },
                    });
                }
            }

            for (const auto &generator : resolved->generators)
            {
                graph.generators.push_back(CompositionGraph::Generator{
                    .name = generator.declaration.name,
                    .owner = generator.ownerKind + ":" + generator.ownerName,
                    .tool = generator.declaration.toolName,
                    .outputs = generator.declaration.outputs.size(),
                    .provenance = ownerContributionProvenance(generator.declaration.provenance,
                                                              generator.ownerKind, generator.ownerName,
                                                              generator.manifestPath,
                                                              generator.declaration.selectors,
                                                              "active generator declaration"),
                });
            }

            for (const auto &input : resolved->inputs)
            {
                if (input.kind == "Source")
                {
                    ++graph.summary.sources;
                }
                else if (input.kind == "Header")
                {
                    ++graph.summary.headers;
                }
                if (input.kind == "Source" || input.kind == "Header" || input.kind == "Generated")
                {
                    graph.buildInputs.push_back(CompositionGraph::BuildInput{
                        .kind = input.kind,
                        .role = input.role,
                        .source = input.source,
                        .owner = input.ownerKind + ":" + input.ownerName,
                        .provenance = contributionProvenance(input.provenance, "selected build input"),
                    });
                }
                if (!input.stagedRelativePath.empty())
                {
                    ++graph.summary.stagedFiles;
                    graph.stageFiles.push_back(CompositionGraph::StageFile{
                        .kind = input.contentKind.empty() ? input.kind : input.contentKind,
                        .source = input.source,
                        .target = input.stagedRelativePath,
                        .owner = input.ownerKind + ":" + input.ownerName,
                        .provenance = contributionProvenance(input.provenance, "staged file contribution"),
                    });
                }
            }

            for (const auto &variable : resolved->environmentVariables)
            {
                graph.environment.push_back(CompositionGraph::EnvironmentEntry{
                    .name = variable.name,
                    .value = variable.secret ? "<redacted>" : variable.value,
                    .secret = variable.secret,
                    .resolved = variable.resolved,
                    .source = variable.resolvedSource,
                    .provenance = contributionProvenance(variable.provenance,
                                                         variable.secret ? "secret environment contribution"
                                                                         : "environment contribution"),
                });
            }
        }

        if (resolved != nullptr)
        {
            for (const auto &module : resolved->requiredModules)
            {
                const auto provenanceIt = resolved->runtimeModuleProvenance.find(module);
                graph.runtimeModules.push_back(CompositionGraph::RuntimeModule{
                    .name = module,
                    .selection = "required",
                    .provenance = provenanceIt == resolved->runtimeModuleProvenance.end()
                                      ? profileProvenance("resolved required runtime module")
                                      : contributionProvenance(provenanceIt->second,
                                                               "resolved required runtime module"),
                });
            }
            for (const auto &module : resolved->optionalModules)
            {
                const auto provenanceIt = resolved->runtimeModuleProvenance.find(module);
                graph.runtimeModules.push_back(CompositionGraph::RuntimeModule{
                    .name = module,
                    .selection = "optional",
                    .provenance = provenanceIt == resolved->runtimeModuleProvenance.end()
                                      ? profileProvenance("resolved optional runtime module")
                                      : contributionProvenance(provenanceIt->second,
                                                               "resolved optional runtime module"),
                });
            }
            for (const auto &plugin : resolved->enabledPlugins)
            {
                graph.runtimePlugins.push_back(CompositionGraph::RuntimePlugin{
                    .name = plugin,
                    .provenance = profileProvenance("resolved runtime plugin"),
                });
            }
            graph.launch = CompositionGraph::Launch{
                .name = resolved->profile.launch.name,
                .executable = resolved->selectedExecutable.has_value() ? resolved->selectedExecutable->name : "",
                .workingDirectory = resolved->profile.launch.workingDirectory,
                .args = resolved->profile.launch.args,
                .selected = true,
                .provenance = namedContributionProvenance(
                    resolved->profile.launch.provenance, resolved->profile.launch.name,
                    launchIsProfileItem(resolved->profile.launch.name), "selected launch entry"),
            };
        }

        for (const auto &launch : effectiveLaunches)
        {
            graph.launches.push_back(CompositionGraph::Launch{
                .name = launch.name,
                .executable = launch.executable.value_or(
                    invocation.project.output.kind == "Executable" ? invocation.project.output.name : ""),
                .workingDirectory = launch.workingDirectory,
                .args = launch.args,
                .selected = launch.name == invocation.profile.launch.name,
                .provenance = namedContributionProvenance(
                    launch.provenance, launch.name, launchIsProfileItem(launch.name),
                    launch.name == invocation.profile.launch.name ? "selected launch entry" : "effective launch entry"),
            });
        }

        for (const auto &output : effectivePackageOutputs)
        {
            graph.packageOutputs.push_back(CompositionGraph::PackageOutput{
                .name = output.name,
                .version = output.version,
                .from = output.from,
                .headers = output.headers.size(),
                .libraries = output.libraries.size(),
                .tools = output.tools.size(),
                .capabilities = output.capabilities.size(),
                .abi = output.abiTag,
                .provenance = namedContributionProvenance(
                    output.provenance, output.name, packageOutputIsProfileItem(output.name),
                    "source product package output"),
            });
        }

        for (const auto &publish : effectivePublishes)
        {
            graph.publishes.push_back(CompositionGraph::Publish{
                .name = publish.name,
                .kind = publish.kind,
                .format = publish.format,
                .output = publish.output,
                .includeStage = publish.includeStage,
                .includeRuntimeDependencies = publish.includeRuntimeDependencies,
                .includeSymbols = publish.includeSymbols,
                .provenance = namedContributionProvenance(
                    publish.provenance, publish.name, publishIsProfileItem(publish.name),
                    "resolved publish entry"),
            });
        }

        for (const auto &[_, analyzer] : effectiveAnalyzers)
        {
            if (!analyzer.enabled)
            {
                continue;
            }
            graph.analyzers.push_back(CompositionGraph::QualityAnalyzer{
                .name = analyzer.name,
                .tool = AnalyzerToolName(analyzer),
                .packageName = analyzer.packageName,
                .scope = analyzer.scope,
                .severity = analyzer.severity,
                .configPath = analyzer.configPath,
                .configOptional = analyzer.configOptional,
                .provenance =
                    contributionProvenance(analyzer.provenance, "resolved quality analyzer", analyzer.selectors),
            });
        }

        return graph;
    }

    auto WriteGraphProvenance(std::ostream &out, const CompositionGraph::Provenance &provenance) -> void
    {
        out << "{"
            << "\"sourceKind\":" << Json(provenance.sourceKind) << ","
            << "\"sourceName\":" << Json(provenance.sourceName) << ","
            << "\"manifestPath\":" << JsonPath(provenance.manifestPath) << ","
            << "\"reason\":" << Json(provenance.reason) << "}";
    }

    auto WriteGraphConventions(std::ostream &out, const std::vector<CompositionGraph::Convention> &conventions) -> void
    {
        out << "[";
        for (std::size_t index = 0; index < conventions.size(); ++index)
        {
            if (index > 0)
            {
                out << ",";
            }
            out << "{"
                << "\"name\":" << Json(conventions[index].name) << ","
                << "\"reason\":" << Json(conventions[index].reason) << ","
                << "\"provenance\":";
            WriteGraphProvenance(out, conventions[index].provenance);
            out << "}";
        }
        out << "]";
    }

    auto WriteGraphProperties(std::ostream &out, const std::vector<CompositionGraph::Property> &properties) -> void
    {
        out << "[";
        for (std::size_t index = 0; index < properties.size(); ++index)
        {
            if (index > 0)
            {
                out << ",";
            }
            out << "{"
                << "\"name\":" << Json(properties[index].name) << ","
                << "\"value\":" << Json(properties[index].value) << ","
                << "\"provenance\":";
            WriteGraphProvenance(out, properties[index].provenance);
            out << "}";
        }
        out << "]";
    }

    auto WriteGraphStageFiles(std::ostream &out, const std::vector<CompositionGraph::StageFile> &files,
                              bool includeProvenance) -> void
    {
        out << "[";
        for (std::size_t index = 0; index < files.size(); ++index)
        {
            if (index > 0)
            {
                out << ",";
            }
            out << "{"
                << "\"kind\":" << Json(files[index].kind) << ","
                << "\"source\":" << JsonPath(files[index].source) << ","
                << "\"target\":" << JsonPath(files[index].target) << ","
                << "\"owner\":" << Json(files[index].owner);
            if (includeProvenance)
            {
                out << ",\"provenance\":";
                WriteGraphProvenance(out, files[index].provenance);
            }
            out << "}";
        }
        out << "]";
    }

    auto WriteGraphEnvironmentEntries(std::ostream &out, const std::vector<CompositionGraph::EnvironmentEntry> &entries,
                                      bool includeProvenance) -> void
    {
        out << "[";
        for (std::size_t index = 0; index < entries.size(); ++index)
        {
            if (index > 0)
            {
                out << ",";
            }
            out << "{"
                << "\"name\":" << Json(entries[index].name) << ","
                << "\"value\":" << Json(entries[index].value) << ","
                << "\"secret\":" << (entries[index].secret ? "true" : "false") << ","
                << "\"resolved\":" << (entries[index].resolved ? "true" : "false") << ","
                << "\"source\":" << Json(entries[index].source);
            if (includeProvenance)
            {
                out << ",\"provenance\":";
                WriteGraphProvenance(out, entries[index].provenance);
            }
            out << "}";
        }
        out << "]";
    }

    auto WriteGraphPackageOutputs(std::ostream &out, const std::vector<CompositionGraph::PackageOutput> &outputs,
                                  bool includeProvenance) -> void
    {
        out << "[";
        for (std::size_t index = 0; index < outputs.size(); ++index)
        {
            if (index > 0)
            {
                out << ",";
            }
            out << "{"
                << "\"name\":" << Json(outputs[index].name) << ","
                << "\"version\":" << Json(outputs[index].version) << ","
                << "\"from\":" << Json(outputs[index].from) << ","
                << "\"headers\":" << outputs[index].headers << ","
                << "\"libraries\":" << outputs[index].libraries << ","
                << "\"tools\":" << outputs[index].tools << ","
                << "\"capabilities\":" << outputs[index].capabilities << ","
                << "\"abi\":" << Json(outputs[index].abi);
            if (includeProvenance)
            {
                out << ",\"provenance\":";
                WriteGraphProvenance(out, outputs[index].provenance);
            }
            out << "}";
        }
        out << "]";
    }

    auto WriteGraphPackages(std::ostream &out, const std::vector<CompositionGraph::Package> &packages,
                            bool includeProvenance) -> void
    {
        out << "[";
        for (std::size_t index = 0; index < packages.size(); ++index)
        {
            if (index > 0)
            {
                out << ",";
            }
            out << "{"
                << "\"name\":" << Json(packages[index].name) << ","
                << "\"version\":" << Json(packages[index].version) << ","
                << "\"source\":" << Json(packages[index].source) << ","
                << "\"provider\":" << Json(packages[index].provider) << ","
                << "\"providerKind\":" << Json(packages[index].providerKind) << ","
                << "\"providerPackage\":" << Json(packages[index].providerPackage) << ","
                << "\"providerVersion\":" << Json(packages[index].providerVersion) << ","
                << "\"providerRoot\":" << JsonPath(packages[index].providerRoot) << ","
                << "\"scope\":" << Json(packages[index].scope) << ","
                << "\"closures\":[";
            for (std::size_t closureIndex = 0; closureIndex < packages[index].closures.size(); ++closureIndex)
            {
                if (closureIndex > 0)
                {
                    out << ",";
                }
                out << Json(packages[index].closures[closureIndex]);
            }
            out << "],\"dependencies\":[";
            for (std::size_t dependencyIndex = 0; dependencyIndex < packages[index].dependencies.size();
                 ++dependencyIndex)
            {
                if (dependencyIndex > 0)
                {
                    out << ",";
                }
                out << Json(packages[index].dependencies[dependencyIndex]);
            }
            out << "]";
            if (includeProvenance)
            {
                out << ",\"provenance\":";
                WriteGraphProvenance(out, packages[index].provenance);
            }
            out << "}";
        }
        out << "]";
    }

    auto WriteGraphPackageFeatures(std::ostream &out, const std::vector<CompositionGraph::PackageFeature> &features,
                                   bool includeProvenance) -> void
    {
        out << "[";
        for (std::size_t index = 0; index < features.size(); ++index)
        {
            if (index > 0)
            {
                out << ",";
            }
            out << "{"
                << "\"package\":" << Json(features[index].package) << ","
                << "\"feature\":" << Json(features[index].feature) << ","
                << "\"packageVersion\":" << Json(features[index].packageVersion);
            if (includeProvenance)
            {
                out << ",\"provenance\":";
                WriteGraphProvenance(out, features[index].provenance);
            }
            out << "}";
        }
        out << "]";
    }

    auto WriteGraphBuildPlan(std::ostream &out, const CompositionGraph &graph, bool includeProvenance) -> void
    {
        out << "{\"defines\":[";
        for (std::size_t index = 0; index < graph.buildDefines.size(); ++index)
        {
            if (index > 0)
            {
                out << ",";
            }
            if (includeProvenance)
            {
                out << "{\"value\":" << Json(graph.buildDefines[index].value) << ",\"provenance\":";
                WriteGraphProvenance(out, graph.buildDefines[index].provenance);
                out << "}";
            }
            else
            {
                out << Json(graph.buildDefines[index].value);
            }
        }
        out << "],\"inputs\":[";
        for (std::size_t index = 0; index < graph.buildInputs.size(); ++index)
        {
            if (index > 0)
            {
                out << ",";
            }
            out << "{"
                << "\"kind\":" << Json(graph.buildInputs[index].kind) << ","
                << "\"role\":" << Json(graph.buildInputs[index].role) << ","
                << "\"source\":" << Json(graph.buildInputs[index].source) << ","
                << "\"owner\":" << Json(graph.buildInputs[index].owner);
            if (includeProvenance)
            {
                out << ",\"provenance\":";
                WriteGraphProvenance(out, graph.buildInputs[index].provenance);
            }
            out << "}";
        }
        out << "]}";
    }

    auto WriteGraphGenerators(std::ostream &out, const std::vector<CompositionGraph::Generator> &generators,
                              bool includeProvenance) -> void
    {
        out << "[";
        for (std::size_t index = 0; index < generators.size(); ++index)
        {
            if (index > 0)
            {
                out << ",";
            }
            out << "{"
                << "\"name\":" << Json(generators[index].name) << ","
                << "\"owner\":" << Json(generators[index].owner) << ","
                << "\"tool\":" << Json(generators[index].tool) << ","
                << "\"outputs\":" << generators[index].outputs;
            if (includeProvenance)
            {
                out << ",\"provenance\":";
                WriteGraphProvenance(out, generators[index].provenance);
            }
            out << "}";
        }
        out << "]";
    }

    auto WriteGraphRuntime(std::ostream &out, const CompositionGraph &graph, bool includeProvenance) -> void
    {
        out << "{\"requiredModules\":[";
        bool firstRequired = true;
        for (const auto &module : graph.runtimeModules)
        {
            if (module.selection != "required")
            {
                continue;
            }
            if (!firstRequired)
            {
                out << ",";
            }
            firstRequired = false;
            if (includeProvenance)
            {
                out << "{\"name\":" << Json(module.name) << ",\"provenance\":";
                WriteGraphProvenance(out, module.provenance);
                out << "}";
            }
            else
            {
                out << Json(module.name);
            }
        }
        out << "],\"optionalModules\":[";
        bool firstOptional = true;
        for (const auto &module : graph.runtimeModules)
        {
            if (module.selection != "optional")
            {
                continue;
            }
            if (!firstOptional)
            {
                out << ",";
            }
            firstOptional = false;
            if (includeProvenance)
            {
                out << "{\"name\":" << Json(module.name) << ",\"provenance\":";
                WriteGraphProvenance(out, module.provenance);
                out << "}";
            }
            else
            {
                out << Json(module.name);
            }
        }
        out << "],\"plugins\":[";
        for (std::size_t index = 0; index < graph.runtimePlugins.size(); ++index)
        {
            if (index > 0)
            {
                out << ",";
            }
            if (includeProvenance)
            {
                out << "{\"name\":" << Json(graph.runtimePlugins[index].name) << ",\"provenance\":";
                WriteGraphProvenance(out, graph.runtimePlugins[index].provenance);
                out << "}";
            }
            else
            {
                out << Json(graph.runtimePlugins[index].name);
            }
        }
        out << "]}";
    }

    auto WriteGraphLaunch(std::ostream &out, const CompositionGraph::Launch &launch, bool includeProvenance) -> void
    {
        out << "{"
            << "\"name\":" << Json(launch.name) << ","
            << "\"executable\":" << Json(launch.executable) << ","
            << "\"workingDirectory\":" << Json(launch.workingDirectory) << ","
            << "\"args\":" << Json(launch.args) << ","
            << "\"selected\":" << (launch.selected ? "true" : "false");
        if (includeProvenance)
        {
            out << ",\"provenance\":";
            WriteGraphProvenance(out, launch.provenance);
        }
        out << "}";
    }

    auto WriteGraphLaunches(std::ostream &out, const std::vector<CompositionGraph::Launch> &launches,
                            bool includeProvenance) -> void
    {
        out << "[";
        for (std::size_t index = 0; index < launches.size(); ++index)
        {
            if (index > 0)
            {
                out << ",";
            }
            WriteGraphLaunch(out, launches[index], includeProvenance);
        }
        out << "]";
    }

    auto WriteGraphPublishes(std::ostream &out, const std::vector<CompositionGraph::Publish> &publishes,
                             bool includeProvenance) -> void
    {
        out << "[";
        for (std::size_t index = 0; index < publishes.size(); ++index)
        {
            if (index > 0)
            {
                out << ",";
            }
            out << "{"
                << "\"name\":" << Json(publishes[index].name) << ","
                << "\"kind\":" << Json(publishes[index].kind) << ","
                << "\"format\":" << Json(publishes[index].format) << ","
                << "\"output\":" << Json(publishes[index].output) << ","
                << "\"includeStage\":" << (publishes[index].includeStage ? "true" : "false") << ","
                << "\"includeRuntimeDependencies\":" << (publishes[index].includeRuntimeDependencies ? "true" : "false")
                << ","
                << "\"includeSymbols\":" << (publishes[index].includeSymbols ? "true" : "false");
            if (includeProvenance)
            {
                out << ",\"provenance\":";
                WriteGraphProvenance(out, publishes[index].provenance);
            }
            out << "}";
        }
        out << "]";
    }

    auto WriteGraphAnalyzers(std::ostream &out, const std::vector<CompositionGraph::QualityAnalyzer> &analyzers,
                             bool includeProvenance) -> void
    {
        out << "[";
        for (std::size_t index = 0; index < analyzers.size(); ++index)
        {
            if (index > 0)
            {
                out << ",";
            }
            out << "{"
                << "\"name\":" << Json(analyzers[index].name) << ","
                << "\"tool\":" << Json(analyzers[index].tool) << ","
                << "\"package\":" << Json(analyzers[index].packageName) << ","
                << "\"scope\":" << Json(analyzers[index].scope) << ","
                << "\"severity\":" << Json(analyzers[index].severity) << ","
                << "\"configPath\":" << Json(analyzers[index].configPath) << ","
                << "\"configOptional\":" << (analyzers[index].configOptional ? "true" : "false");
            if (includeProvenance)
            {
                out << ",\"provenance\":";
                WriteGraphProvenance(out, analyzers[index].provenance);
            }
            out << "}";
        }
        out << "]";
    }

    auto CmdNew(const fs::path &root, const std::string &kind, const std::string &name) -> int
    {
        const auto productKind = ProductKindFromNewKind(kind);
        const auto projectDir = root / name;
        if (fs::exists(projectDir))
        {
            throw std::runtime_error(projectDir.string() + ": directory already exists");
        }

        const auto projectPath = projectDir / (name + ".nginproj");
        WriteNewFile(projectPath, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n\n"
                                  "<Project SchemaVersion=\"4\" Name=\"" +
                                      name +
                                      "\">\n"
                                      "  <" +
                                      productKind +
                                      " />\n"
                                      "</Project>\n");

        if (productKind == "Library")
        {
            WriteNewFile(projectDir / "include" / (name + ".hpp"), "#pragma once\n");
            WriteNewFile(projectDir / "src" / (name + ".cpp"), "#include \"" + name + ".hpp\"\n");
        }
        else
        {
            WriteNewFile(projectDir / "src/main.cpp", "int main() { return 0; }\n");
        }

        std::cout << "Created " << productKind << " project\n";
        std::cout << "  project: " << projectPath << "\n";
        return 0;
    }

    auto WriteCompositionGraphJson(const LoadedInvocation &invocation,
                                   const DiagnosticResult<ResolvedLaunch> &resolvedResult) -> void;

    auto CmdInspect(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        if (!args.format.has_value())
        {
            throw std::runtime_error("inspect requires --format json");
        }
        if (*args.format != "json")
        {
            throw std::runtime_error("inspect supports only --format json");
        }

        const auto invocation = ResolveInvocation(args);
        const auto resolvedResult = ResolveLaunch(invocation.project, invocation.profile);
        WriteCompositionGraphJson(invocation, resolvedResult);
        return resolvedResult.diagnostics.HasErrors() ? 1 : 0;
    }

    auto WriteCompositionGraphJson(const LoadedInvocation &invocation,
                                   const DiagnosticResult<ResolvedLaunch> &resolvedResult) -> void
    {
        const auto graph = BuildCompositionGraph(invocation, resolvedResult);

        auto writeDiagnostics = [&](const DiagnosticReport &diagnostics) {
            std::cout << "[";
            for (std::size_t index = 0; index < diagnostics.entries.size(); ++index)
            {
                const auto &entry = diagnostics.entries[index];
                if (index > 0)
                {
                    std::cout << ",";
                }
                std::cout << "{"
                          << "\"severity\":" << Json(entry.severity == DiagnosticSeverity::Error ? "error" : "warning")
                          << ","
                          << "\"subject\":" << Json(entry.subject) << ","
                          << "\"message\":" << Json(entry.message) << "}";
            }
            std::cout << "]";
        };

        std::cout << "{\n";
        std::cout << "  \"schemaVersion\": " << Json(graph.schemaVersion) << ",\n";
        std::cout << "  \"kind\": " << Json(graph.kind) << ",\n";
        std::cout << "  \"state\": " << Json(graph.state) << ",\n";
        std::cout << "  \"facets\": [";
        for (std::size_t index = 0; index < graph.facets.size(); ++index)
        {
            if (index > 0)
            {
                std::cout << ",";
            }
            std::cout << Json(graph.facets[index]);
        }
        std::cout << "],\n";
        std::cout << "  \"identity\": {"
                  << "\"project\":" << Json(graph.identity.project) << ","
                  << "\"projectPath\":" << JsonPath(graph.identity.projectPath) << ","
                  << "\"product\":" << Json(graph.identity.product) << ","
                  << "\"profile\":" << Json(graph.identity.profile) << "},\n";
        std::cout << "  \"conventions\": ";
        WriteGraphConventions(std::cout, graph.conventions);
        std::cout << ",\n";
        std::cout << "  \"properties\": ";
        WriteGraphProperties(std::cout, graph.properties);
        std::cout << ",\n";
        std::cout << "  \"product\": {"
                  << "\"kind\":" << Json(graph.product.kind) << ","
                  << "\"outputType\":" << Json(graph.product.outputType) << ","
                  << "\"outputName\":" << Json(graph.product.outputName) << ","
                  << "\"targetName\":" << Json(graph.product.targetName) << "},\n";
        std::cout << "  \"selection\": {"
                  << "\"profile\":" << Json(graph.selection.profile) << ","
                  << "\"hostPlatform\":" << Json(graph.selection.hostPlatform) << ","
                  << "\"targetPlatform\":" << Json(graph.selection.targetPlatform) << ","
                  << "\"operatingSystem\":" << Json(graph.selection.operatingSystem) << ","
                  << "\"architecture\":" << Json(graph.selection.architecture) << ","
                  << "\"toolchain\":" << Json(graph.selection.toolchain) << ","
                  << "\"environment\":" << Json(graph.selection.environment) << ","
                  << "\"abiTag\":" << Json(graph.selection.abiTag) << "},\n";
        std::cout << "  \"facetsSummary\": {"
                  << "\"packages\":" << graph.summary.packages << ","
                  << "\"packageFeatures\":" << graph.summary.packageFeatures << ","
                  << "\"sources\":" << graph.summary.sources << ","
                  << "\"headers\":" << graph.summary.headers << ","
                  << "\"generators\":" << graph.summary.generators << ","
                  << "\"stagedFiles\":" << graph.summary.stagedFiles << ","
                  << "\"runtimeModules\":" << graph.summary.runtimeModules << ","
                  << "\"environmentVariables\":" << graph.summary.environmentVariables << ","
                  << "\"publishes\":" << graph.summary.publishes << ","
                  << "\"analyzers\":" << graph.summary.analyzers << ","
                  << "\"diagnostics\":" << graph.summary.diagnostics << "},\n";

        std::cout << "  \"plans\": {";
        std::cout << "\"packages\":";
        WriteGraphPackages(std::cout, graph.packages, false);
        std::cout << ",";

        std::cout << "\"packageFeatures\":";
        WriteGraphPackageFeatures(std::cout, graph.packageFeatures, false);
        std::cout << ",";

        std::cout << "\"build\":";
        WriteGraphBuildPlan(std::cout, graph, false);
        std::cout << ",";

        std::cout << "\"generators\":";
        WriteGraphGenerators(std::cout, graph.generators, false);
        std::cout << ",";

        std::cout << "\"stage\":{\"files\":";
        WriteGraphStageFiles(std::cout, graph.stageFiles, false);
        std::cout << "},";

        std::cout << "\"runtime\":";
        WriteGraphRuntime(std::cout, graph, false);
        std::cout << ",";

        std::cout << "\"environment\":{\"variables\":";
        WriteGraphEnvironmentEntries(std::cout, graph.environment, false);
        std::cout << "},";

        std::cout << "\"launch\":";
        WriteGraphLaunch(std::cout, graph.launch, false);
        std::cout << ",";

        std::cout << "\"launches\":";
        WriteGraphLaunches(std::cout, graph.launches, false);
        std::cout << ",";

        std::cout << "\"packageOutputs\":";
        WriteGraphPackageOutputs(std::cout, graph.packageOutputs, false);
        std::cout << ",";

        std::cout << "\"publish\":";
        WriteGraphPublishes(std::cout, graph.publishes, false);
        std::cout << ",";

        std::cout << "\"quality\":{\"analyzers\":";
        WriteGraphAnalyzers(std::cout, graph.analyzers, false);
        std::cout << "},";

        std::cout << "\"diagnostics\":";
        writeDiagnostics(resolvedResult.diagnostics);
        std::cout << "}\n";
        std::cout << "}\n";
    }

    auto WriteCompositionGraphPlanJson(const LoadedInvocation &invocation,
                                       const DiagnosticResult<ResolvedLaunch> &resolvedResult, const std::string &plan)
        -> void
    {
        const auto graph = BuildCompositionGraph(invocation, resolvedResult);
        const auto *resolved = resolvedResult.value.has_value() ? &*resolvedResult.value : nullptr;
        auto writeDiagnostics = [&](const DiagnosticReport &diagnostics) {
            std::cout << "[";
            for (std::size_t index = 0; index < diagnostics.entries.size(); ++index)
            {
                const auto &entry = diagnostics.entries[index];
                if (index > 0)
                {
                    std::cout << ",";
                }
                std::cout << "{"
                          << "\"severity\":" << Json(entry.severity == DiagnosticSeverity::Error ? "error" : "warning")
                          << ","
                          << "\"subject\":" << Json(entry.subject) << ","
                          << "\"message\":" << Json(entry.message) << "}";
            }
            std::cout << "]";
        };

        std::cout << "{\n";
        std::cout << "  \"schemaVersion\": \"4.0\",\n";
        std::cout << "  \"kind\": \"NGIN.CompositionGraphPlan\",\n";
        std::cout << "  \"plan\": " << Json(plan) << ",\n";
        std::cout << "  \"state\": " << Json(graph.state) << ",\n";
        std::cout << "  \"identity\": {"
                  << "\"project\":" << Json(graph.identity.project) << ","
                  << "\"product\":" << Json(graph.identity.product) << ","
                  << "\"profile\":" << Json(graph.identity.profile) << "},\n";
        std::cout << "  \"data\": ";

        if (resolved == nullptr)
        {
            std::cout << "null,\n";
            std::cout << "  \"diagnostics\": ";
            writeDiagnostics(resolvedResult.diagnostics);
            std::cout << "\n}\n";
            return;
        }

        if (plan == "stage")
        {
            std::cout << "{\"files\":";
            WriteGraphStageFiles(std::cout, graph.stageFiles, true);
            std::cout << "}";
        }
        else if (plan == "launch")
        {
            std::cout << "{\"selected\":";
            WriteGraphLaunch(std::cout, graph.launch, true);
            std::cout << ",\"launches\":";
            WriteGraphLaunches(std::cout, graph.launches, true);
            std::cout << "}";
        }
        else if (plan == "package")
        {
            std::cout << "{\"packages\":";
            WriteGraphPackages(std::cout, graph.packages, true);
            std::cout << ",\"features\":";
            WriteGraphPackageFeatures(std::cout, graph.packageFeatures, true);
            std::cout << "}";
        }
        else if (plan == "package-output")
        {
            std::cout << "{\"packageOutputs\":";
            WriteGraphPackageOutputs(std::cout, graph.packageOutputs, true);
            std::cout << "}";
        }
        else if (plan == "runtime")
        {
            WriteGraphRuntime(std::cout, graph, true);
        }
        else if (plan == "environment")
        {
            std::cout << "{\"variables\":";
            WriteGraphEnvironmentEntries(std::cout, graph.environment, true);
            std::cout << "}";
        }
        else if (plan == "publish")
        {
            std::cout << "{\"publishes\":";
            WriteGraphPublishes(std::cout, graph.publishes, true);
            std::cout << "}";
        }
        else if (plan == "quality")
        {
            std::cout << "{\"analyzers\":";
            WriteGraphAnalyzers(std::cout, graph.analyzers, true);
            std::cout << "}";
        }
        else
        {
            WriteGraphBuildPlan(std::cout, graph, true);
        }

        std::cout << ",\n  \"diagnostics\": ";
        writeDiagnostics(resolvedResult.diagnostics);
        std::cout << "\n}\n";
    }

    auto CmdValidate(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        const auto invocation = ResolveInvocation(args);
        const auto resolved = ResolveLaunch(invocation.project, invocation.profile);
        if (!resolved.value.has_value() || resolved.diagnostics.HasErrors())
        {
            PrintDiagnostics(resolved.diagnostics, "Validation", std::cout);
            return 1;
        }
        PrintTitle(args, "NGIN validate");
        PrintField(args, "product", resolved.value->project.name);
        PrintField(args, "profile", resolved.value->profile.name);
        PrintField(args, "packages", resolved.value->orderedPackages.size());
        PrintField(args, "executable",
                   resolved.value->selectedExecutable.has_value() ? resolved.value->selectedExecutable->name
                                                                  : "(none)");
        if (IsVerbose(args))
        {
            PrintField(args, "required modules", resolved.value->requiredModules.size());
            PrintField(args, "optional modules", resolved.value->optionalModules.size());
            PrintField(args, "libraries", resolved.value->libraries.size());
            PrintField(args, "executables", resolved.value->executables.size());
        }
        if (!IsQuiet(args))
        {
            PrintDiagnostics(resolved.diagnostics, "Validation", std::cout);
            PrintSuccess(args, "Validation passed");
        }
        return 0;
    }

    auto CmdGraph(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        if (args.format.has_value())
        {
            if (*args.format != "json")
            {
                throw std::runtime_error("graph supports only --format json");
            }
            const auto invocation = ResolveInvocation(args);
            const auto resolved = ResolveLaunch(invocation.project, invocation.profile);
            if (!args.graphPlan.has_value())
            {
                WriteCompositionGraphJson(invocation, resolved);
            }
            else
            {
                WriteCompositionGraphPlanJson(invocation, resolved, *args.graphPlan);
            }
            return resolved.diagnostics.HasErrors() ? 1 : 0;
        }
        const auto invocation = ResolveInvocation(args);
        const auto resolved = ResolveLaunch(invocation.project, invocation.profile);
        if (!resolved.value.has_value() || resolved.diagnostics.HasErrors())
        {
            PrintDiagnostics(resolved.diagnostics, "Graph", std::cout);
            return 1;
        }
        const auto graph = BuildCompositionGraph(invocation, resolved);
        if (args.graphPlan == "stage")
        {
            std::cout << "Stage plan for profile: " << graph.identity.profile << "\n";
            for (const auto &file : graph.stageFiles)
            {
                std::cout << "  - " << file.target.generic_string() << " <- " << file.source.generic_string() << " ["
                          << file.kind << "]"
                          << " owner=" << file.owner << "\n";
            }
            if (graph.stageFiles.empty())
            {
                std::cout << "  (none)\n";
            }
            return 0;
        }
        if (args.graphPlan == "launch")
        {
            std::cout << "Launch plan for profile: " << graph.identity.profile << "\n";
            const auto launches =
                graph.launches.empty() ? std::vector<CompositionGraph::Launch>{graph.launch} : graph.launches;
            for (const auto &launch : launches)
            {
                std::cout << "  launch " << (launch.name.empty() ? "(default)" : launch.name)
                          << (launch.selected ? " [selected]" : "") << "\n";
                std::cout << "    executable: " << (launch.executable.empty() ? "(none)" : launch.executable) << "\n";
                std::cout << "    workingDirectory: " << launch.workingDirectory << "\n";
                std::cout << "    args: " << launch.args << "\n";
            }
            if (launches.empty())
            {
                std::cout << "  (none)\n";
            }
            return 0;
        }
        if (args.graphPlan == "package")
        {
            std::cout << "Package plan for profile: " << graph.identity.profile << "\n";
            if (graph.packages.empty())
            {
                std::cout << "  packages: (none)\n";
            }
            for (const auto &package : graph.packages)
            {
                std::cout << "  package " << package.name << " " << package.version << " source=" << package.source
                          << "\n";
                if (!package.provider.empty())
                {
                    std::cout << "    provider " << package.provider << " kind=" << package.providerKind
                              << " package=" << package.providerPackage << " version=" << package.providerVersion
                              << "\n";
                }
                if (!package.scope.empty())
                {
                    std::cout << "    scope " << package.scope << "\n";
                }
                for (const auto &dep : package.dependencies)
                {
                    std::cout << "    depends-on " << dep << "\n";
                }
            }
            if (graph.packageFeatures.empty())
            {
                std::cout << "  features: (none)\n";
            }
            for (const auto &feature : graph.packageFeatures)
            {
                std::cout << "  feature " << feature.package << "/" << feature.feature << "\n";
            }
            return 0;
        }
        if (args.graphPlan == "package-output")
        {
            std::cout << "Package output plan for profile: " << graph.identity.profile << "\n";
            if (graph.packageOutputs.empty())
            {
                std::cout << "  package outputs: (none)\n";
            }
            for (const auto &output : graph.packageOutputs)
            {
                std::cout << "  package-output " << output.name << " version=" << output.version;
                if (!output.from.empty())
                {
                    std::cout << " from=" << output.from;
                }
                if (!output.abi.empty())
                {
                    std::cout << " abi=" << output.abi;
                }
                std::cout << "\n";
                std::cout << "    headers=" << output.headers << " libraries=" << output.libraries
                          << " tools=" << output.tools << " capabilities=" << output.capabilities << "\n";
            }
            return 0;
        }
        if (args.graphPlan == "runtime")
        {
            std::cout << "Runtime plan for profile: " << graph.identity.profile << "\n";
            if (graph.runtimeModules.empty())
            {
                std::cout << "  modules: (none)\n";
            }
            for (const auto &module : graph.runtimeModules)
            {
                std::cout << "  " << module.selection << " module " << module.name << "\n";
            }
            if (graph.runtimePlugins.empty())
            {
                std::cout << "  plugins: (none)\n";
            }
            for (const auto &plugin : graph.runtimePlugins)
            {
                std::cout << "  plugin " << plugin.name << "\n";
            }
            return 0;
        }
        if (args.graphPlan == "environment")
        {
            std::cout << "Environment plan for profile: " << graph.identity.profile << "\n";
            if (graph.environment.empty())
            {
                std::cout << "  variables: (none)\n";
            }
            for (const auto &variable : graph.environment)
            {
                std::cout << "  env " << variable.name << "=" << variable.value
                          << " secret=" << (variable.secret ? "true" : "false")
                          << " resolved=" << (variable.resolved ? "true" : "false");
                if (!variable.source.empty())
                {
                    std::cout << " source=" << variable.source;
                }
                std::cout << "\n";
            }
            return 0;
        }
        if (args.graphPlan == "publish")
        {
            std::cout << "Publish plan for profile: " << graph.identity.profile << "\n";
            if (graph.publishes.empty())
            {
                std::cout << "  publishes: (none)\n";
            }
            for (const auto &publish : graph.publishes)
            {
                std::cout << "  publish " << publish.name << " kind=" << publish.kind << " output=" << publish.output;
                if (!publish.format.empty())
                {
                    std::cout << " format=" << publish.format;
                }
                std::cout << "\n";
                std::cout << "    includeStage=" << (publish.includeStage ? "true" : "false")
                          << " includeRuntimeDependencies=" << (publish.includeRuntimeDependencies ? "true" : "false")
                          << " includeSymbols=" << (publish.includeSymbols ? "true" : "false") << "\n";
            }
            return 0;
        }
        if (args.graphPlan == "quality")
        {
            std::cout << "Quality plan for profile: " << graph.identity.profile << "\n";
            for (const auto &analyzer : graph.analyzers)
            {
                std::cout << "  analyzer " << analyzer.name << " scope=" << analyzer.scope
                          << " severity=" << analyzer.severity;
                if (!analyzer.tool.empty() && analyzer.tool != analyzer.name)
                {
                    std::cout << " tool=" << analyzer.tool;
                }
                if (!analyzer.configPath.empty())
                {
                    std::cout << " config=" << analyzer.configPath;
                }
                std::cout << "\n";
            }
            if (graph.analyzers.empty())
            {
                std::cout << "  analyzers: (none)\n";
            }
            return 0;
        }
        if (args.graphPlan == "build")
        {
            std::cout << "Build plan for profile: " << graph.identity.profile << "\n";
            std::cout << "  backend: " << resolved.value->project.build.backend << "\n";
            std::cout << "  mode: " << resolved.value->project.build.mode << "\n";
            std::cout << "  language: " << resolved.value->project.build.language
                      << resolved.value->project.build.languageStandard << "\n";
            std::cout << "  output: " << resolved.value->project.output.kind << " "
                      << resolved.value->project.output.name << " target=" << resolved.value->project.output.target
                      << "\n";
            std::cout << "  inputs:\n";
            for (const auto &input : graph.buildInputs)
            {
                if (input.kind != "Source" && input.kind != "Generated")
                {
                    continue;
                }
                std::cout << "    - " << input.kind;
                if (!input.role.empty())
                {
                    std::cout << ":" << input.role;
                }
                std::cout << " " << input.source << " owner=" << input.owner << "\n";
            }
            if (std::none_of(graph.buildInputs.begin(), graph.buildInputs.end(),
                             [](const CompositionGraph::BuildInput &input) {
                                 return input.kind == "Source" || input.kind == "Generated";
                             }))
            {
                std::cout << "    (none)\n";
            }
            std::cout << "  defines:\n";
            for (const auto &definition : graph.buildDefines)
            {
                std::cout << "    - " << definition.value << " owner=" << definition.provenance.sourceKind << ":"
                          << definition.provenance.sourceName << "\n";
            }
            if (graph.buildDefines.empty())
            {
                std::cout << "    (none)\n";
            }
            return 0;
        }
        std::cout << "Graph for profile: " << resolved.value->profile.name << "\n\nProjects:\n";
        for (const auto &unit : resolved.value->projectUnits)
        {
            std::cout << "  - " << unit.project.name << " [" << unit.profile.name << "]\n";
        }
        std::cout << "\nPackages:\n";
        for (const auto &package : resolved.value->orderedPackages)
        {
            const auto &edges = resolved.value->packageEdges.at(package.manifest.name);
            std::cout << "  - " << package.manifest.name << " -> ";
            if (edges.empty())
            {
                std::cout << "(none)";
            }
            else
            {
                bool first = true;
                for (const auto &dep : edges)
                {
                    if (!first)
                    {
                        std::cout << ", ";
                    }
                    std::cout << dep;
                    first = false;
                }
            }
            std::cout << "\n";
        }
        std::cout << "\nPackage features:\n";
        if (resolved.value->selectedPackageFeatures.empty())
        {
            std::cout << "  (none)\n";
        }
        for (const auto &feature : resolved.value->selectedPackageFeatures)
        {
            std::cout << "  - " << feature.packageName << "::" << feature.featureName << "\n";
        }
        std::cout << "\nGenerators:\n";
        if (resolved.value->generators.empty())
        {
            std::cout << "  (none)\n";
        }
        for (const auto &generator : resolved.value->generators)
        {
            std::cout << "  - " << generator.declaration.name << " kind=" << generator.declaration.kind
                      << " owner=" << generator.ownerKind << ":" << generator.ownerName;
            if (!generator.declaration.toolName.empty())
            {
                std::cout << " tool=" << generator.declaration.toolName;
            }
            std::cout << "\n";
        }
        std::cout << "\nCapabilities:\n";
        if (resolved.value->capabilityProviders.empty())
        {
            std::cout << "  (none)\n";
        }
        for (const auto &provider : resolved.value->capabilityProviders)
        {
            std::cout << "  - " << provider.capability << " <- " << provider.packageName
                      << "::" << provider.featureName;
            if (provider.exclusive)
            {
                std::cout << " exclusive";
            }
            std::cout << "\n";
        }
        std::cout << "\nModules:\n";
        for (const auto &[name, edges] : resolved.value->dependencyEdges)
        {
            std::cout << "  - " << name << " -> ";
            if (edges.empty())
            {
                std::cout << "(none)";
            }
            else
            {
                bool first = true;
                for (const auto &dep : edges)
                {
                    if (!first)
                    {
                        std::cout << ", ";
                    }
                    std::cout << dep;
                    first = false;
                }
            }
            std::cout << "\n";
        }
        std::cout << "\nArtifacts:\n";
        for (const auto &library : resolved.value->libraries)
        {
            std::cout << "  - library " << library.name;
            if (!library.target.empty())
            {
                std::cout << " target=" << library.target;
            }
            if (!library.linkage.empty())
            {
                std::cout << " linkage=" << library.linkage;
            }
            if (!library.origin.empty())
            {
                std::cout << " origin=" << library.origin;
            }
            std::cout << "\n";
        }
        for (const auto &executable : resolved.value->executables)
        {
            std::cout << "  - executable " << executable.name;
            if (!executable.target.empty())
            {
                std::cout << " target=" << executable.target;
            }
            if (!executable.origin.empty())
            {
                std::cout << " origin=" << executable.origin;
            }
            if (resolved.value->selectedExecutable.has_value() &&
                resolved.value->selectedExecutable->name == executable.name)
            {
                std::cout << " selected";
            }
            std::cout << "\n";
        }
        bool printedConditionsHeader = false;
        for (const auto &unit : resolved.value->projectUnits)
        {
            if (unit.project.conditions.empty())
            {
                continue;
            }
            if (!printedConditionsHeader)
            {
                std::cout << "\nConditions:\n";
                printedConditionsHeader = true;
            }
            std::cout << "  - " << unit.project.name << ":\n";
            for (const auto &condition : unit.project.conditions)
            {
                SelectorSet selector{};
                selector.conditionRefs.push_back(condition.name);
                std::cout << "    " << condition.name << ": "
                          << (SelectionMatches(unit.project, selector, unit.profile) ? "matched" : "not matched")
                          << "\n";
            }
        }

        bool printedSelectionHeader = false;
        for (const auto &unit : resolved.value->projectUnits)
        {
            const auto hasConditionalSelections =
                std::any_of(unit.project.projectRefs.begin(), unit.project.projectRefs.end(),
                            [](const ProjectReference &reference) { return HasSelection(reference.selectors); }) ||
                std::any_of(unit.project.packageRefs.begin(), unit.project.packageRefs.end(),
                            [](const PackageReference &reference) { return HasSelection(reference.selectors); }) ||
                (unit.environment.has_value() &&
                 (std::any_of(unit.environment->projectRefs.begin(), unit.environment->projectRefs.end(),
                              [](const ProjectReference &reference) { return HasSelection(reference.selectors); }) ||
                  std::any_of(unit.environment->packageRefs.begin(), unit.environment->packageRefs.end(),
                              [](const PackageReference &reference) { return HasSelection(reference.selectors); }) ||
                  std::any_of(unit.environment->features.begin(), unit.environment->features.end(),
                              [](const FeatureFlag &feature) { return HasSelection(feature.selectors); }))) ||
                std::any_of(unit.profile.projectRefs.begin(), unit.profile.projectRefs.end(),
                            [](const ProjectReference &reference) { return HasSelection(reference.selectors); }) ||
                std::any_of(unit.profile.packageRefs.begin(), unit.profile.packageRefs.end(),
                            [](const PackageReference &reference) { return HasSelection(reference.selectors); }) ||
                std::any_of(unit.project.runtime.enableModules.begin(), unit.project.runtime.enableModules.end(),
                            [](const RuntimeReference &reference) { return HasSelection(reference.selectors); }) ||
                std::any_of(unit.project.runtime.disableModules.begin(), unit.project.runtime.disableModules.end(),
                            [](const RuntimeReference &reference) { return HasSelection(reference.selectors); }) ||
                std::any_of(unit.profile.runtime.enableModules.begin(), unit.profile.runtime.enableModules.end(),
                            [](const RuntimeReference &reference) { return HasSelection(reference.selectors); }) ||
                std::any_of(unit.profile.runtime.disableModules.begin(), unit.profile.runtime.disableModules.end(),
                            [](const RuntimeReference &reference) { return HasSelection(reference.selectors); });
            if (!hasConditionalSelections)
            {
                continue;
            }
            if (!printedSelectionHeader)
            {
                std::cout << "\nConditional selections:\n";
                printedSelectionHeader = true;
            }
            std::cout << "  - " << unit.project.name << ":\n";
            PrintConditionalProjectReferences("Project references", unit.project, unit.profile,
                                              unit.project.projectRefs);
            PrintConditionalPackageReferences("Package references", unit.project, unit.profile,
                                              unit.project.packageRefs);
            if (unit.environment.has_value())
            {
                PrintConditionalProjectReferences("Environment project references", unit.project, unit.profile,
                                                  unit.environment->projectRefs);
                PrintConditionalPackageReferences("Environment package references", unit.project, unit.profile,
                                                  unit.environment->packageRefs);
                PrintConditionalFeatures(unit.project, unit.profile, unit.environment);
                PrintConditionalRuntimeRefs("Environment enabled modules", unit.project, unit.profile,
                                            unit.environment->runtime.enableModules);
                PrintConditionalRuntimeRefs("Environment disabled modules", unit.project, unit.profile,
                                            unit.environment->runtime.disableModules);
            }
            PrintConditionalProjectReferences("Profile project references", unit.project, unit.profile,
                                              unit.profile.projectRefs);
            PrintConditionalPackageReferences("Profile package references", unit.project, unit.profile,
                                              unit.profile.packageRefs);
            PrintConditionalRuntimeRefs("Enabled modules", unit.project, unit.profile,
                                        unit.project.runtime.enableModules);
            PrintConditionalRuntimeRefs("Disabled modules", unit.project, unit.profile,
                                        unit.project.runtime.disableModules);
            PrintConditionalRuntimeRefs("Profile enabled modules", unit.project, unit.profile,
                                        unit.profile.runtime.enableModules);
            PrintConditionalRuntimeRefs("Profile disabled modules", unit.project, unit.profile,
                                        unit.profile.runtime.disableModules);
        }

        bool printedBuildSettingsHeader = false;
        for (const auto &unit : resolved.value->projectUnits)
        {
            const auto hasConditionalBuildSettings =
                std::any_of(unit.project.build.includeDirectories.begin(), unit.project.build.includeDirectories.end(),
                            [](const BuildSetting &setting) { return HasSelection(setting.selectors); }) ||
                std::any_of(unit.project.build.compileDefinitions.begin(), unit.project.build.compileDefinitions.end(),
                            [](const BuildSetting &setting) { return HasSelection(setting.selectors); }) ||
                std::any_of(unit.project.build.compileOptions.begin(), unit.project.build.compileOptions.end(),
                            [](const BuildSetting &setting) { return HasSelection(setting.selectors); }) ||
                std::any_of(unit.project.build.linkOptions.begin(), unit.project.build.linkOptions.end(),
                            [](const BuildSetting &setting) { return HasSelection(setting.selectors); });
            if (!hasConditionalBuildSettings)
            {
                continue;
            }
            if (!printedBuildSettingsHeader)
            {
                std::cout << "\nConditional build settings:\n";
                printedBuildSettingsHeader = true;
            }
            std::cout << "  - " << unit.project.name << ":\n";
            PrintConditionalBuildSettings("IncludeDirectories", unit.project, unit.profile,
                                          unit.project.build.includeDirectories);
            PrintConditionalBuildSettings("CompileDefinitions", unit.project, unit.profile,
                                          unit.project.build.compileDefinitions);
            PrintConditionalBuildSettings("CompileOptions", unit.project, unit.profile,
                                          unit.project.build.compileOptions);
            PrintConditionalBuildSettings("LinkOptions", unit.project, unit.profile, unit.project.build.linkOptions);
        }
        PrintDiagnostics(resolved.diagnostics, "Graph", std::cout);
        return 0;
    }

    auto CmdDiff(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        if (args.fromLockPath.has_value() || args.toLockPath.has_value())
        {
            if (!args.fromLockPath.has_value() || !args.toLockPath.has_value())
            {
                throw std::runtime_error("diff lock mode requires --from-lock "
                                         "<ngin.lock> and --to-lock <ngin.lock>");
            }

            const auto from = LoadLockPackages(*args.fromLockPath);
            const auto to = LoadLockPackages(*args.toLockPath);
            bool anyDiff = false;

            std::cout << "Lock diff\n";
            std::cout << "  from lock: " << *args.fromLockPath << "\n";
            std::cout << "  to lock: " << *args.toLockPath << "\n\n";

            PrintLockDiff(from, to, anyDiff);
            if (!anyDiff)
            {
                std::cout << "No lock differences.\n";
            }
            return 0;
        }

        if (!args.fromProfileName.has_value() || !args.toProfileName.has_value())
        {
            throw std::runtime_error("diff requires --from-profile <name> and --to-profile <name>");
        }

        const auto project = LoadProjectManifest(ResolveProjectPath(args.projectPath));
        const auto &fromProfile = ProfileByName(project, args.fromProfileName);
        const auto &toProfile = ProfileByName(project, args.toProfileName);
        const auto fromResolved = ResolveLaunch(project, fromProfile);
        const auto toResolved = ResolveLaunch(project, toProfile);

        if (!fromResolved.value.has_value() || fromResolved.diagnostics.HasErrors())
        {
            PrintDiagnostics(fromResolved.diagnostics, "Diff from-profile", std::cout);
            return 1;
        }
        if (!toResolved.value.has_value() || toResolved.diagnostics.HasErrors())
        {
            PrintDiagnostics(toResolved.diagnostics, "Diff to-profile", std::cout);
            return 1;
        }

        const auto from = BuildDiffSnapshot(*fromResolved.value);
        const auto to = BuildDiffSnapshot(*toResolved.value);
        bool anyDiff = false;

        std::cout << "Diff for project: " << project.name << "\n";
        std::cout << "  from profile: " << fromProfile.name << "\n";
        std::cout << "  to profile: " << toProfile.name << "\n\n";

        PrintMapDiff("Selection", from.selection, to.selection, anyDiff);
        PrintSetDiff("Defines", from.defines, to.defines, anyDiff);
        PrintMapDiff("Packages", from.packages, to.packages, anyDiff);
        PrintSetDiff("Package features", from.packageFeatures, to.packageFeatures, anyDiff);
        PrintSetDiff("Generators", from.generators, to.generators, anyDiff);
        PrintSetDiff("Generated outputs", from.generatedOutputs, to.generatedOutputs, anyDiff);
        PrintMapDiff("Stage", from.stagedFiles, to.stagedFiles, anyDiff);
        PrintSetDiff("Runtime modules", from.runtimeModules, to.runtimeModules, anyDiff);
        PrintSetDiff("Plugins", from.plugins, to.plugins, anyDiff);
        PrintMapDiff("Environment", from.environment, to.environment, anyDiff);
        PrintMapDiff("Launch", from.launch, to.launch, anyDiff);
        PrintMapDiff("Launch entries", from.launches, to.launches, anyDiff);
        PrintSetDiff("Publishes", from.publishes, to.publishes, anyDiff);
        PrintSetDiff("Analyzers", from.analyzers, to.analyzers, anyDiff);
        PrintSetDiff("Artifacts", from.artifacts, to.artifacts, anyDiff);

        if (!anyDiff)
        {
            std::cout << "No graph differences.\n";
        }
        return 0;
    }

    auto CmdFormat(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        const auto manifestPath = args.projectPath.has_value() ? fs::weakly_canonical(*args.projectPath)
                                                               : ResolveProjectPath(args.projectPath);
        const auto formatted = FormatXmlManifest(manifestPath);
        WriteTextFile(manifestPath, formatted);
        std::cout << "Formatted manifest\n";
        std::cout << "  path: " << manifestPath << "\n";
        return 0;
    }

    auto CmdSchema(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        if (args.format.has_value() && *args.format != "json")
        {
            throw std::runtime_error("schema supports only --format json");
        }

        std::cout << "{\n";
        std::cout << "  \"schemaVersion\": \"4.0\",\n";
        std::cout << "  \"format\": \"xml\",\n";
        std::cout << "  \"fileTypes\": [\".nginproj\", \".nginpkg\", \".ngin\", "
                     "\".ngin.xml\"],\n";
        std::cout << "  \"productKinds\": [\"Application\", \"Library\", \"Tool\", "
                     "\"Test\", \"Benchmark\", \"Plugin\", \"Module\", \"External\"],\n";
        std::cout << "  \"dependencyKinds\": [\"Project\", \"Package\", \"Tool\", "
                     "\"Runtime\"],\n";
        std::cout << "  \"dependencyScopes\": [\"Build\", \"Target\", \"Runtime\", "
                     "\"Test\", \"Dev\", \"Publish\"],\n";
        std::cout << "  \"overlayOperations\": [\"Remove\"],\n";
        std::cout << "  \"commonProductSections\": [\"Uses\", \"Build\", "
                     "\"Generate\", \"Stage\", \"Environment\", \"Quality\"],\n";
        std::cout << "  \"productSections\": {\n";
        std::cout << "    \"Application\": [\"Runtime\", \"Launch\", \"Publish\"],\n";
        std::cout << "    \"Library\": [\"Exports\", \"PackageOutput\"],\n";
        std::cout << "    \"Tool\": [\"Run\", \"Stage\", \"PackageOutput\"],\n";
        std::cout << "    \"Test\": [\"Run\", \"Report\", \"TestSettings\"],\n";
        std::cout << "    \"Benchmark\": [\"Run\", \"Report\", \"BenchmarkSettings\"],\n";
        std::cout << "    \"Plugin\": [\"Runtime\", \"Stage\", \"Exports\", "
                     "\"PackageOutput\"],\n";
        std::cout << "    \"Module\": [\"Runtime\", \"Exports\", \"PackageOutput\"],\n";
        std::cout << "    \"External\": [\"Uses\", \"Exports\", \"Stage\", "
                     "\"PackageOutput\"]\n";
        std::cout << "  },\n";
        std::cout << "  \"buildItems\": [\"Language\", \"Sources\", \"Headers\", "
                     "\"IncludePath\", \"Define\", \"CompileOption\", \"LinkOption\", "
                     "\"LinkLibrary\", \"PrecompiledHeader\", \"UnityBuild\"],\n";
        std::cout << "  \"stageItems\": [\"Config\", \"Content\"],\n";
        std::cout << "  \"runtimeItems\": [\"Module\", \"Plugin\", \"Setting\"],\n";
        std::cout << "  \"environmentItems\": [\"Env\", \"LaunchEnv\", \"Secret\"],\n";
        std::cout << "  \"publishKinds\": [\"Folder\", \"Archive\"],\n";
        std::cout << "  \"archiveFormats\": [\"zip\"],\n";
        std::cout << "  \"explainKinds\": [\"property\", \"convention\", \"source\", "
                     "\"define\", \"package\", \"feature\", \"stage\", "
                     "\"generator\", \"launch\", \"publish\", \"package-output\", "
                     "\"env\", \"analyzer\", \"runtime-module\", \"toolchain\"],\n";
        std::cout << "  \"graphPlans\": [\"build\", \"stage\", \"package\", "
                     "\"package-output\", \"launch\", \"runtime\", \"environment\", "
                     "\"publish\", \"quality\"]\n";
        std::cout << "}\n";
        return 0;
    }

    auto CmdClean(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        const auto invocation = ResolveInvocation(args);
        const auto cleaned =
            CleanLaunch(invocation.project, invocation.profile,
                        args.outputPath.has_value() ? std::optional<fs::path>{*args.outputPath} : std::nullopt);
        if (!cleaned.value.has_value() || cleaned.diagnostics.HasErrors())
        {
            PrintDiagnostics(cleaned.diagnostics, "Clean", std::cout);
            return 1;
        }

        PrintTitle(args, "NGIN clean");
        PrintField(args, "product", invocation.project.name);
        PrintField(args, "profile", invocation.profile.name);
        PrintField(args, "output", *cleaned.value);
        if (!IsQuiet(args))
        {
            PrintDiagnostics(cleaned.diagnostics, "Clean", std::cout);
            PrintSuccess(args, "Clean complete");
        }
        return 0;
    }

    auto CmdConfigure(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        CommandEventSession session{args, "configure"};
        auto &events = session.Events();
        const auto commandStarted = std::chrono::steady_clock::now();
        EmitCommandStarted(events, args);
        const auto invocation = ResolveInvocation(args);
        EmitSelection(events, invocation);
        std::vector<BackendStepResult> backendSteps{};
        auto buildOptions = BuildOptionsForArgs(args, backendSteps, &events);
        const auto configured = ConfigureLaunch(
            invocation.project, invocation.profile,
            args.outputPath.has_value() ? std::optional<fs::path>{*args.outputPath} : std::nullopt, buildOptions);
        if (!configured.value.has_value() || configured.diagnostics.HasErrors())
        {
            EmitDiagnostics(events, configured.diagnostics, "ngin configure");
            return EmitCommandCompleted(events, "failed", 1, commandStarted);
        }

        EmitConfiguredArtifacts(events, *configured.value);
        PrintVerboseResolvedDetails(args, invocation.project, invocation.profile, configured.value->outputDir);
        EmitDiagnostics(events, configured.diagnostics, "ngin configure");
        return EmitCommandCompleted(events, "success", 0, commandStarted);
    }

    auto CmdBuild(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        CommandEventSession session{args, "build"};
        auto &events = session.Events();
        const auto commandStarted = std::chrono::steady_clock::now();
        EmitCommandStarted(events, args);
        const auto invocation = ResolveInvocation(args);
        EmitSelection(events, invocation);
        std::vector<BackendStepResult> backendSteps{};
        auto buildOptions = BuildOptionsForArgs(args, backendSteps, &events);
        auto built = BuildLaunch(invocation.project, invocation.profile,
                                 args.outputPath.has_value() ? std::optional<fs::path>{*args.outputPath} : std::nullopt,
                                 buildOptions);
        if (!built.value.has_value() || built.diagnostics.HasErrors())
        {
            EmitDiagnostics(events, built.diagnostics, "ngin build");
            return EmitCommandCompleted(events, "failed", 1, commandStarted);
        }

        const auto summary = LoadLaunchManifestSummary(built.value->manifestPath);
        EmitBuildArtifactsAndSummary(events, invocation.project, invocation.profile, *built.value, summary);
        PrintVerboseResolvedDetails(args, invocation.project, invocation.profile, built.value->outputDir);
        EmitDiagnostics(events, built.diagnostics, "ngin build");
        return EmitCommandCompleted(events, "success", 0, commandStarted);
    }

    auto CmdStage(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        CommandEventSession session{args, "stage"};
        auto &events = session.Events();
        const auto commandStarted = std::chrono::steady_clock::now();
        EmitCommandStarted(events, args);
        const auto invocation = ResolveInvocation(args);
        EmitSelection(events, invocation);
        std::vector<BackendStepResult> backendSteps{};
        auto buildOptions = BuildOptionsForArgs(args, backendSteps, &events);
        auto built = BuildLaunch(invocation.project, invocation.profile,
                                 args.outputPath.has_value() ? std::optional<fs::path>{*args.outputPath} : std::nullopt,
                                 buildOptions);
        if (!built.value.has_value() || built.diagnostics.HasErrors())
        {
            EmitDiagnostics(events, built.diagnostics, "ngin stage");
            return EmitCommandCompleted(events, "failed", 1, commandStarted);
        }

        const auto summary = LoadLaunchManifestSummary(built.value->manifestPath);
        EmitBuildArtifactsAndSummary(events, invocation.project, invocation.profile, *built.value, summary);
        PrintVerboseResolvedDetails(args, invocation.project, invocation.profile, built.value->outputDir);
        EmitDiagnostics(events, built.diagnostics, "ngin stage");
        return EmitCommandCompleted(events, "success", 0, commandStarted);
    }

    auto CmdRebuild(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        CommandEventSession session{args, "rebuild"};
        auto &events = session.Events();
        const auto commandStarted = std::chrono::steady_clock::now();
        EmitCommandStarted(events, args);
        const auto invocation = ResolveInvocation(args);
        EmitSelection(events, invocation);
        const auto cleanResult =
            CleanLaunch(invocation.project, invocation.profile,
                        args.outputPath.has_value() ? std::optional<fs::path>{*args.outputPath} : std::nullopt);
        if (!cleanResult.value.has_value() || cleanResult.diagnostics.HasErrors())
        {
            EmitDiagnostics(events, cleanResult.diagnostics, "ngin rebuild");
            return EmitCommandCompleted(events, "failed", 1, commandStarted);
        }

        std::vector<BackendStepResult> backendSteps{};
        auto buildOptions = BuildOptionsForArgs(args, backendSteps, &events);
        auto built = BuildLaunch(invocation.project, invocation.profile,
                                 args.outputPath.has_value() ? std::optional<fs::path>{*args.outputPath} : std::nullopt,
                                 buildOptions);
        AppendDiagnostics(built.diagnostics, cleanResult.diagnostics);
        if (!built.value.has_value() || built.diagnostics.HasErrors())
        {
            EmitDiagnostics(events, built.diagnostics, "ngin rebuild");
            return EmitCommandCompleted(events, "failed", 1, commandStarted);
        }

        const auto summary = LoadLaunchManifestSummary(built.value->manifestPath);
        EmitBuildArtifactsAndSummary(events, invocation.project, invocation.profile, *built.value, summary);
        PrintVerboseResolvedDetails(args, invocation.project, invocation.profile, built.value->outputDir);
        EmitDiagnostics(events, built.diagnostics, "ngin rebuild");
        return EmitCommandCompleted(events, "success", 0, commandStarted);
    }

    auto CmdRun(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        CommandEventSession session{args, "run"};
        auto &events = session.Events();
        const auto commandStarted = std::chrono::steady_clock::now();
        EmitCommandStarted(events, args);
        const auto invocation = ResolveInvocation(args);
        EmitSelection(events, invocation);
        return RunBuiltProduct(invocation.project, invocation.profile, args, events, commandStarted, "Run");
    }

    auto CmdTest(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        CommandEventSession session{args, "test"};
        auto &events = session.Events();
        const auto commandStarted = std::chrono::steady_clock::now();
        EmitCommandStarted(events, args);
        const auto invocation = ResolveInvocation(args);
        EmitSelection(events, invocation);
        if (invocation.project.productKind != "Test")
        {
            throw std::runtime_error("ngin test requires a Test product project");
        }
        return RunBuiltProduct(invocation.project, invocation.profile, args, events, commandStarted, "Test");
    }

    auto CmdBenchmark(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        CommandEventSession session{args, "benchmark"};
        auto &events = session.Events();
        const auto commandStarted = std::chrono::steady_clock::now();
        EmitCommandStarted(events, args);
        const auto invocation = ResolveInvocation(args);
        EmitSelection(events, invocation);
        if (invocation.project.productKind != "Benchmark")
        {
            throw std::runtime_error("ngin benchmark requires a Benchmark product project");
        }
        return RunBuiltProduct(invocation.project, invocation.profile, args, events, commandStarted, "Benchmark");
    }

    auto CmdAnalyze(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        CommandEventSession session{args, "analyze"};
        auto &events = session.Events();
        const auto commandStarted = std::chrono::steady_clock::now();
        EmitCommandStarted(events, args);
        const auto invocation = ResolveInvocation(args);
        EmitSelection(events, invocation);
        const auto resolvedResult = ResolveLaunch(invocation.project, invocation.profile);
        if (resolvedResult.diagnostics.HasErrors())
        {
            EmitDiagnostics(events, resolvedResult.diagnostics, "ngin analyze");
            if (!IsQuiet(args))
            {
                PrintDiagnostics(resolvedResult.diagnostics, "Analyze", std::cout);
            }
            return EmitCommandCompleted(events, "failed", 1, commandStarted);
        }
        if (!resolvedResult.value.has_value())
        {
            events.Emit(CliEventType::Diagnostic,
                        EventData{}
                            .AddString("severity", "error")
                            .AddString("source", "ngin analyze")
                            .AddString("message", "failed to resolve project launch context"));
            if (!IsQuiet(args))
            {
                std::cout << "\nAnalyze errors:\n  - failed to resolve project launch "
                             "context\n";
            }
            return EmitCommandCompleted(events, "failed", 1, commandStarted);
        }
        const auto &resolved = *resolvedResult.value;
        const auto analyzers =
            EffectiveAnalyzers(invocation.project, invocation.profile, resolved.selectedPackageFeatures);

        bool anyEnabled = false;
        bool needsConfigure = false;
        for (const auto &[_, analyzer] : analyzers)
        {
            if (!analyzer.enabled)
            {
                continue;
            }
            anyEnabled = true;
            needsConfigure = needsConfigure || IsClangTidyAnalyzer(analyzer);
            if (!IsQuiet(args))
            {
                std::ostringstream detail{};
                detail << "scope=" << analyzer.scope << " severity=" << analyzer.severity;
                if (!AnalyzerToolName(analyzer).empty() && AnalyzerToolName(analyzer) != analyzer.name)
                {
                    detail << " tool=" << AnalyzerToolName(analyzer);
                }
                if (!analyzer.configPath.empty())
                {
                    detail << " config=" << analyzer.configPath;
                }
                PrintItem(args, analyzer.name, detail.str());
            }
        }
        if (!anyEnabled)
        {
            PrintField(args, "analyzers", "(none)");
        }
        if (!needsConfigure)
        {
            events.Emit(CliEventType::Summary, EventData{}
                                                   .AddNumber("analyzers", static_cast<std::int64_t>(analyzers.size()))
                                                   .AddNumber("findings", 0));
            return EmitCommandCompleted(events, "success", 0, commandStarted);
        }

        std::vector<BackendStepResult> backendSteps{};
        auto buildOptions = BuildOptionsForArgs(args, backendSteps, &events);
        const auto configured = ConfigureLaunch(invocation.project, invocation.profile, args.outputPath, buildOptions);
        if (configured.diagnostics.HasErrors())
        {
            EmitDiagnostics(events, configured.diagnostics, "ngin analyze");
            if (!IsQuiet(args))
            {
                PrintDiagnostics(configured.diagnostics, "Analyze", std::cout);
            }
            return EmitCommandCompleted(events, "failed", 1, commandStarted);
        }
        if (!configured.value.has_value() || !configured.value->compileCommandsPath.has_value())
        {
            events.Emit(CliEventType::Diagnostic,
                        EventData{}
                            .AddString("severity", "error")
                            .AddString("source", "ngin analyze")
                            .AddString("message", "clang-tidy requires a configured compile_commands.json"));
            if (!IsQuiet(args))
            {
                std::cout << "\nAnalyze errors:\n  - clang-tidy requires a configured "
                             "compile_commands.json\n";
            }
            return EmitCommandCompleted(events, "failed", 1, commandStarted);
        }

        const auto sources = ResolveAnalyzerSources(resolved);
        if (sources.empty())
        {
            events.Emit(CliEventType::Summary, EventData{}.AddNumber("sources", 0).AddNumber("findings", 0));
            return EmitCommandCompleted(events, "success", 0, commandStarted);
        }

        bool hasErrors = false;
        int findingsCount = 0;
        const auto analyzeStarted = std::chrono::steady_clock::now();
        events.Emit(CliEventType::PhaseStarted,
                    EventData{}.AddString("phase", "analyze").AddString("label", "Analyzer execution"));
        for (const auto &[_, analyzer] : analyzers)
        {
            if (!analyzer.enabled || !IsClangTidyAnalyzer(analyzer))
            {
                continue;
            }
            const auto tool = ResolveAnalyzerTool(resolved, analyzer);
            if (!tool.has_value())
            {
                const auto message = "analyzer '" + analyzer.name +
                                     "' could not resolve clang-tidy. Install clang-tidy on PATH or set "
                                     "NGIN_CLANG_TIDY to the executable path.";
                events.Emit(CliEventType::Diagnostic, EventData{}
                                                          .AddString("severity", "error")
                                                          .AddString("source", "ngin analyze")
                                                          .AddString("message", message));
                if (!IsQuiet(args))
                {
                    std::cout << "\nAnalyze errors:\n"
                              << "  - " << message << "\n";
                }
                hasErrors = true;
                continue;
            }

            std::optional<fs::path> configPath{};
            if (!analyzer.configPath.empty())
            {
                const auto candidate = fs::path(analyzer.configPath).is_absolute()
                                           ? fs::path(analyzer.configPath)
                                           : invocation.project.path.parent_path() / analyzer.configPath;
                if (fs::exists(candidate))
                {
                    configPath = candidate;
                }
                else if (!analyzer.configOptional)
                {
                    const auto message =
                        "analyzer '" + analyzer.name + "' config '" + candidate.string() + "' does not exist";
                    events.Emit(CliEventType::Diagnostic, EventData{}
                                                              .AddString("severity", "error")
                                                              .AddString("source", "ngin analyze")
                                                              .AddString("message", message));
                    if (!IsQuiet(args))
                    {
                        std::cout << "\nAnalyze errors:\n"
                                  << "  - " << message << "\n";
                    }
                    hasErrors = true;
                    continue;
                }
            }

            for (const auto &source : sources)
            {
                std::vector<std::string> commandArgs{
                    "-p",
                    configured.value->buildDir.value_or(configured.value->outputDir).string(),
                };
                if (configPath.has_value())
                {
                    commandArgs.push_back("--config-file=" + configPath->string());
                }
                commandArgs.push_back(source.string());

                const auto result = RunProcessCapture(tool->path, commandArgs, invocation.project.path.parent_path());
                const auto findings = ParseClangTidyFindings(result.output, analyzer.severity);
                for (const auto &finding : findings)
                {
                    ++findingsCount;
                    events.Emit(CliEventType::Diagnostic,
                                EventData{}
                                    .AddString("severity", finding.severity)
                                    .AddString("source", "clang-tidy")
                                    .AddString("code", ClangTidyCodeFromMessage(finding.message))
                                    .AddString("message", finding.message)
                                    .AddString("file", finding.file.string())
                                    .AddNumber("line", std::stoll(finding.line))
                                    .AddNumber("column", std::stoll(finding.column)));
                    if (!IsQuiet(args))
                    {
                        PrintAnalyzerFinding(finding);
                    }
                    if (finding.severity == "error")
                    {
                        hasErrors = true;
                    }
                }
                if (result.exitCode != 0 && findings.empty())
                {
                    const auto message =
                        "analyzer '" + analyzer.name + "' failed with exit code " + std::to_string(result.exitCode);
                    events.Emit(CliEventType::Diagnostic, EventData{}
                                                              .AddString("severity", "error")
                                                              .AddString("source", "ngin analyze")
                                                              .AddString("message", message));
                    if (!IsQuiet(args))
                    {
                        std::cout << "\nAnalyze errors:\n"
                                  << "  - " << message << "\n";
                    }
                    if (!result.output.empty())
                    {
                        events.Emit(CliEventType::BackendOutput, EventData{}
                                                                     .AddString("phase", "analyze")
                                                                     .AddString("stream", "combined")
                                                                     .AddString("text", result.output));
                        std::istringstream output{result.output};
                        std::string line{};
                        while (!IsQuiet(args) && std::getline(output, line))
                        {
                            if (!line.empty())
                            {
                                std::cout << "    " << line << "\n";
                            }
                        }
                    }
                    hasErrors = true;
                }
            }
        }
        const auto analyzeDuration = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - analyzeStarted)
                .count());
        events.Emit(hasErrors ? CliEventType::PhaseFailed : CliEventType::PhaseCompleted,
                    EventData{}
                        .AddString("phase", "analyze")
                        .AddString("label", "Analyzer execution")
                        .AddNumber("durationMs", analyzeDuration)
                        .AddNumber("exitCode", hasErrors ? 1 : 0));
        events.Emit(CliEventType::Summary, EventData{}
                                               .AddNumber("sources", static_cast<std::int64_t>(sources.size()))
                                               .AddNumber("findings", findingsCount));
        return EmitCommandCompleted(events, hasErrors ? "failed" : "success", hasErrors ? 1 : 0, commandStarted);
    }

    auto CmdPublish(const fs::path &root, const ParsedArgs &args) -> int
    {
        (void)root;
        CommandEventSession session{args, "publish"};
        auto &events = session.Events();
        const auto commandStarted = std::chrono::steady_clock::now();
        EmitCommandStarted(events, args);
        const auto invocation = ResolveInvocation(args);
        EmitSelection(events, invocation);
        const auto publishes = EffectivePublishes(invocation.project, invocation.profile);
        const auto &publish = SelectPublish(publishes, args.packageName);
        if (publish.kind != "Folder" && publish.kind != "Archive")
        {
            throw std::runtime_error("publish kind '" + publish.kind + "' is not implemented yet");
        }
        if (publish.kind == "Archive" && !publish.format.empty() && Lower(publish.format) != "zip")
        {
            throw std::runtime_error("archive publish format '" + publish.format + "' is not implemented yet");
        }

        std::vector<BackendStepResult> backendSteps{};
        auto buildOptions = BuildOptionsForArgs(args, backendSteps, &events);
        auto built = BuildLaunch(invocation.project, invocation.profile,
                                 args.outputPath.has_value() ? std::optional<fs::path>{*args.outputPath} : std::nullopt,
                                 buildOptions);
        if (!built.value.has_value() || built.diagnostics.HasErrors())
        {
            EmitDiagnostics(events, built.diagnostics, "ngin publish");
            return EmitCommandCompleted(events, "failed", 1, commandStarted);
        }

        auto publishOutput = fs::path(publish.output);
        if (publishOutput.is_relative())
        {
            publishOutput = invocation.project.path.parent_path() / publishOutput;
        }
        if (publish.kind == "Folder")
        {
            if (fs::exists(publishOutput))
            {
                fs::remove_all(publishOutput);
            }
            CopyDirectoryContents(built.value->outputDir, publishOutput);
        }
        else
        {
            WriteZipArchive(built.value->outputDir, publishOutput);
        }
        events.Emit(CliEventType::ArtifactProduced,
                    EventData{}
                        .AddString("kind", publish.kind == "Folder" ? "publish-directory" : "publish-archive")
                        .AddString("name", publish.name)
                        .AddString("path", publishOutput.string()));
        events.Emit(CliEventType::Summary, EventData{}
                                               .AddString("publish", publish.name)
                                               .AddString("kind", publish.kind)
                                               .AddString("output", publishOutput.string()));

        PrintVerboseResolvedDetails(args, invocation.project, invocation.profile, built.value->outputDir);
        EmitDiagnostics(events, built.diagnostics, "ngin publish");
        return EmitCommandCompleted(events, "success", 0, commandStarted);
    }
} // namespace NGIN::CLI
