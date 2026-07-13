#include "Tooling.hpp"

#include <NGIN/Serialization/JSON/JsonParser.hpp>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <future>
#include <cctype>
#include <iomanip>
#include <iterator>
#include <regex>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace NGIN::CLI
{
    auto BuildToolSchedule(const std::vector<ToolScheduleNode> &nodes,
                           std::size_t workerBudget) -> ToolSchedulePlan
    {
        if (workerBudget == 0) throw std::runtime_error("tool scheduler worker budget must be positive");
        std::map<std::string, std::size_t> indices{};
        for (std::size_t index = 0; index < nodes.size(); ++index)
        {
            if (nodes[index].identity.empty()) throw std::runtime_error("tool scheduler node identity is required");
            if (!indices.emplace(nodes[index].identity, index).second)
                throw std::runtime_error("duplicate tool scheduler node '" + nodes[index].identity + "'");
            if (nodes[index].weight == 0) throw std::runtime_error("tool scheduler node weight must be positive");
            if (nodes[index].maxParallelism == 0)
                throw std::runtime_error("tool scheduler node maximum parallelism must be positive");
        }

        std::vector<std::set<std::size_t>> prerequisites(nodes.size());
        std::vector<std::vector<std::size_t>> dependents(nodes.size());
        for (std::size_t index = 0; index < nodes.size(); ++index)
            for (const auto &dependency : nodes[index].dependencies)
            {
                const auto found = indices.find(dependency);
                if (found == indices.end())
                    throw std::runtime_error("tool scheduler node '" + nodes[index].identity +
                                             "' depends on missing node '" + dependency + "'");
                if (!prerequisites[index].insert(found->second).second) continue;
                dependents[found->second].push_back(index);
            }

        std::vector<std::size_t> remaining{};
        remaining.reserve(nodes.size());
        std::set<std::pair<std::string, std::size_t>> ready{};
        for (std::size_t index = 0; index < nodes.size(); ++index)
        {
            remaining.push_back(prerequisites[index].size());
            if (prerequisites[index].empty()) ready.emplace(nodes[index].identity, index);
        }

        ToolSchedulePlan plan{};
        std::size_t scheduled = 0;
        while (!ready.empty())
        {
            ToolScheduleBatch batch{};
            std::set<std::string> exclusiveResources{};
            for (auto candidate = ready.begin(); candidate != ready.end();)
            {
                const auto index = candidate->second;
                const auto effectiveWeight = std::min(nodes[index].weight, workerBudget);
                const auto resourceAvailable = nodes[index].exclusiveResource.empty() ||
                                               !exclusiveResources.contains(nodes[index].exclusiveResource);
                const auto budgetAvailable = batch.nodeIndices.empty() ||
                                             batch.weight + effectiveWeight <= workerBudget;
                if (!resourceAvailable || !budgetAvailable)
                {
                    ++candidate;
                    continue;
                }
                batch.nodeIndices.push_back(index);
                batch.weight += effectiveWeight;
                if (!nodes[index].exclusiveResource.empty())
                    exclusiveResources.insert(nodes[index].exclusiveResource);
                candidate = ready.erase(candidate);
            }
            if (batch.nodeIndices.empty())
                throw std::runtime_error("tool scheduler could not allocate a ready node");
            std::sort(batch.nodeIndices.begin(), batch.nodeIndices.end(), [&](const auto left, const auto right) {
                return nodes[left].identity < nodes[right].identity;
            });
            scheduled += batch.nodeIndices.size();
            for (const auto index : batch.nodeIndices)
                for (const auto dependent : dependents[index])
                    if (--remaining[dependent] == 0)
                        ready.emplace(nodes[dependent].identity, dependent);
            plan.batches.push_back(std::move(batch));
        }
        if (scheduled != nodes.size()) throw std::runtime_error("tool execution plan contains a dependency cycle");
        return plan;
    }

    auto ExecuteToolSchedule(const std::vector<ToolScheduleNode> &nodes,
                             std::size_t workerBudget,
                             const std::function<ToolScheduleOutcome(std::size_t)> &execute)
        -> std::vector<ToolScheduleOutcome>
    {
        const auto plan = BuildToolSchedule(nodes, workerBudget);
        std::map<std::string, std::size_t> indices{};
        for (std::size_t index = 0; index < nodes.size(); ++index) indices.emplace(nodes[index].identity, index);
        std::vector<ToolScheduleOutcome> outcomes(nodes.size(), ToolScheduleOutcome{.status = "pending"});
        bool failFast = false;
        for (const auto &batch : plan.batches)
        {
            std::vector<std::pair<std::size_t, std::future<ToolScheduleOutcome>>> active{};
            for (const auto index : batch.nodeIndices)
            {
                std::vector<std::string> failedDependencies{};
                for (const auto &dependency : nodes[index].dependencies)
                {
                    const auto &outcome = outcomes[indices.at(dependency)];
                    if (outcome.status != "succeeded")
                        failedDependencies.push_back(dependency + " (" + outcome.status + ")");
                }
                if (failFast)
                {
                    outcomes[index] = ToolScheduleOutcome{
                        .status = "skipped", .skipReason = "a previous run triggered FailFast"};
                }
                else if (nodes[index].failureStrategy == "DependencyAware" && !failedDependencies.empty())
                {
                    std::ostringstream reason{};
                    reason << "prerequisite failure: ";
                    for (std::size_t item = 0; item < failedDependencies.size(); ++item)
                    {
                        if (item != 0) reason << ", ";
                        reason << failedDependencies[item];
                    }
                    outcomes[index] = ToolScheduleOutcome{.status = "skipped", .skipReason = reason.str()};
                }
                else
                {
                    active.emplace_back(index, std::async(std::launch::async, execute, index));
                }
            }
            for (auto &[index, future] : active)
            {
                try
                {
                    outcomes[index] = future.get();
                }
                catch (const std::exception &error)
                {
                    outcomes[index] = ToolScheduleOutcome{.status = "failed", .skipReason = error.what()};
                }
            }
            for (const auto index : batch.nodeIndices)
                if (nodes[index].failureStrategy == "FailFast" && outcomes[index].status != "succeeded")
                    failFast = true;
        }
        return outcomes;
    }

    namespace
    {
        using NGIN::Serialization::JsonObject;
        using NGIN::Serialization::JsonValue;

        [[nodiscard]] auto EscapeJson(std::string_view value) -> std::string
        {
            std::string escaped{};
            escaped.reserve(value.size() + 8);
            for (const char ch : value)
            {
                switch (ch)
                {
                case '"': escaped += "\\\""; break;
                case '\\': escaped += "\\\\"; break;
                case '\b': escaped += "\\b"; break;
                case '\f': escaped += "\\f"; break;
                case '\n': escaped += "\\n"; break;
                case '\r': escaped += "\\r"; break;
                case '\t': escaped += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(ch) < 0x20U)
                    {
                        static constexpr char Hex[] = "0123456789abcdef";
                        escaped += "\\u00";
                        escaped += Hex[(static_cast<unsigned char>(ch) >> 4U) & 0xfU];
                        escaped += Hex[static_cast<unsigned char>(ch) & 0xfU];
                    }
                    else
                    {
                        escaped += ch;
                    }
                }
            }
            return escaped;
        }

        auto WriteString(std::ostream &out, std::string_view value) -> void
        {
            out << '"' << EscapeJson(value) << '"';
        }

        [[nodiscard]] auto Required(const JsonObject &object, std::string_view key,
                                    JsonValue::Type type) -> const JsonValue &
        {
            const auto *value = object.Find(key);
            if (value == nullptr || value->GetType() != type)
            {
                throw std::runtime_error("tool driver event requires '" + std::string(key) + "'");
            }
            return *value;
        }

        [[nodiscard]] auto OptionalString(const JsonObject &object, std::string_view key) -> std::string
        {
            const auto *value = object.Find(key);
            return value != nullptr && value->IsString() ? std::string(value->AsString()) : std::string{};
        }

        [[nodiscard]] auto ParsePosition(const JsonObject &object) -> ToolProtocolPosition
        {
            const auto &line = Required(object, "line", JsonValue::Type::Number);
            const auto &column = Required(object, "column", JsonValue::Type::Number);
            if (line.AsNumber() < 1.0 || column.AsNumber() < 1.0)
            {
                throw std::runtime_error("tool driver locations use one-based positive line and column values");
            }
            return ToolProtocolPosition{
                .line = static_cast<std::int64_t>(line.AsNumber()),
                .column = static_cast<std::int64_t>(column.AsNumber()),
            };
        }

        [[nodiscard]] auto ParseLocation(const JsonObject &object) -> ToolProtocolLocation
        {
            const auto &file = Required(object, "file", JsonValue::Type::Object).AsObject();
            const auto absolute = Required(file, "absolute", JsonValue::Type::String).AsString();
            const auto &range = Required(object, "range", JsonValue::Type::Object).AsObject();
            ToolProtocolLocation location{
                .file = fs::path(absolute),
                .start = ParsePosition(Required(range, "start", JsonValue::Type::Object).AsObject()),
                .message = OptionalString(object, "message"),
            };
            if (const auto *end = range.Find("end"); end != nullptr && end->IsObject())
            {
                location.end = ParsePosition(end->AsObject());
            }
            return location;
        }

        [[nodiscard]] auto ParseDiagnostic(const JsonObject &data) -> ToolProtocolDiagnostic
        {
            ToolProtocolDiagnostic diagnostic{
                .severity = std::string(Required(data, "severity", JsonValue::Type::String).AsString()),
                .code = OptionalString(data, "code"),
                .originalSeverity = OptionalString(data, "originalSeverity"),
                .originalCode = OptionalString(data, "originalCode"),
                .message = std::string(Required(data, "message", JsonValue::Type::String).AsString()),
                .documentationUrl = OptionalString(data, "documentationUrl"),
                .fingerprint = OptionalString(data, "fingerprint"),
            };
            if (diagnostic.severity != "note" && diagnostic.severity != "info" &&
                diagnostic.severity != "warning" && diagnostic.severity != "error" &&
                diagnostic.severity != "fatal")
            {
                throw std::runtime_error("tool driver diagnostic has unsupported severity '" +
                                         diagnostic.severity + "'");
            }
            diagnostic.intrinsicSeverity = diagnostic.severity;
            diagnostic.effectiveSeverity = diagnostic.severity;
            if (const auto *suppressed = data.Find("suppressed"); suppressed != nullptr && suppressed->IsBool())
                diagnostic.suppressed = suppressed->AsBool();
            diagnostic.suppressionSource = OptionalString(data, "suppressionSource");
            diagnostic.suppressionReason = OptionalString(data, "suppressionReason");
            if (diagnostic.suppressed && diagnostic.suppressionSource.empty())
                diagnostic.suppressionSource = "tool";
            if (const auto *location = data.Find("primaryLocation"); location != nullptr && location->IsObject())
            {
                diagnostic.primaryLocation = ParseLocation(location->AsObject());
            }
            if (const auto *related = data.Find("relatedLocations"); related != nullptr && related->IsArray())
            {
                for (const auto &entry : related->AsArray().values)
                {
                    if (!entry.IsObject())
                    {
                        throw std::runtime_error("tool driver related location must be an object");
                    }
                    diagnostic.relatedLocations.push_back(ParseLocation(entry.AsObject()));
                }
            }
            const auto appendStrings = [&](std::string_view key, std::vector<std::string> &target) {
                if (const auto *values = data.Find(key); values != nullptr && values->IsArray())
                {
                    for (const auto &entry : values->AsArray().values)
                    {
                        if (entry.IsString()) target.emplace_back(entry.AsString());
                    }
                }
            };
            appendStrings("tags", diagnostic.tags);
            appendStrings("editSetIds", diagnostic.editSetIds);
            return diagnostic;
        }

        [[nodiscard]] auto ParseEditSet(const JsonObject &data) -> ToolProtocolEditSet
        {
            ToolProtocolEditSet editSet{
                .id = std::string(Required(data, "id", JsonValue::Type::String).AsString()),
                .label = OptionalString(data, "label"),
                .applicability = OptionalString(data, "applicability"),
            };
            if (editSet.applicability.empty()) editSet.applicability = "suggested";
            if (editSet.applicability != "automatic" && editSet.applicability != "suggested" &&
                editSet.applicability != "unsafe")
                throw std::runtime_error("tool edit applicability must be automatic, suggested, or unsafe");
            const auto &files = Required(data, "files", JsonValue::Type::Array).AsArray();
            for (const auto &fileValue : files.values)
            {
                const auto &file = Required(fileValue.AsObject(), "path", JsonValue::Type::Object).AsObject();
                ToolProtocolFileEdits parsed{
                    .file = fs::path(Required(file, "absolute", JsonValue::Type::String).AsString()),
                    .expectedDigest = OptionalString(fileValue.AsObject(), "expectedDigest"),
                };
                const auto &edits = Required(fileValue.AsObject(), "edits", JsonValue::Type::Array).AsArray();
                for (const auto &editValue : edits.values)
                {
                    const auto &edit = editValue.AsObject();
                    const auto &range = Required(edit, "range", JsonValue::Type::Object).AsObject();
                    parsed.edits.push_back(ToolProtocolTextEdit{
                        .start = ParsePosition(Required(range, "start", JsonValue::Type::Object).AsObject()),
                        .end = ParsePosition(Required(range, "end", JsonValue::Type::Object).AsObject()),
                        .newText = std::string(Required(edit, "newText", JsonValue::Type::String).AsString()),
                    });
                }
                editSet.files.push_back(std::move(parsed));
            }
            return editSet;
        }

        [[nodiscard]] auto SplitCommand(std::string_view command) -> std::vector<std::string>
        {
            std::vector<std::string> result{};
            std::string current{};
            char quote = '\0';
            bool escaped = false;
            for (std::size_t index = 0; index < command.size(); ++index)
            {
                const auto ch = command[index];
                if (escaped) { current += ch; escaped = false; continue; }
                if (ch == '\\' && quote != '\'')
                {
                    const auto next = index + 1 < command.size() ? command[index + 1] : '\0';
                    const auto escapesNext = next == '\\' || next == '"' ||
                        (quote == '\0' && (std::isspace(static_cast<unsigned char>(next)) || next == '\''));
                    if (escapesNext) { escaped = true; continue; }
                    current += ch;
                    continue;
                }
                if (quote != '\0')
                {
                    if (ch == quote) quote = '\0'; else current += ch;
                    continue;
                }
                if (ch == '\'' || ch == '"') { quote = ch; continue; }
                if (std::isspace(static_cast<unsigned char>(ch)))
                {
                    if (!current.empty()) { result.push_back(std::move(current)); current.clear(); }
                    continue;
                }
                current += ch;
            }
            if (escaped) current += '\\';
            if (quote != '\0') throw std::runtime_error("compile command contains an unterminated quote");
            if (!current.empty()) result.push_back(std::move(current));
            return result;
        }

        [[nodiscard]] auto IsCompilerExecutable(std::string_view candidate) -> bool
        {
            const auto separator = candidate.find_last_of("/\\");
            auto name = std::string(separator == std::string_view::npos
                                        ? candidate
                                        : candidate.substr(separator + 1));
            std::ranges::transform(name, name.begin(), [](const unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            static const std::unordered_set<std::string> Names{
                "cl", "cl.exe", "clang", "clang.exe", "clang++", "clang++.exe",
                "clang-cl", "clang-cl.exe", "gcc", "gcc.exe", "g++", "g++.exe",
                "c++", "c++.exe", "icl", "icl.exe", "icx", "icx.exe", "icpx", "icpx.exe",
            };
            return Names.contains(name);
        }

        [[nodiscard]] auto RepairUnquotedWindowsCompilerPath(std::vector<std::string> arguments)
            -> std::vector<std::string>
        {
            if (arguments.size() < 2 || arguments[0].size() < 3 || arguments[0][1] != ':' ||
                (arguments[0][2] != '\\' && arguments[0][2] != '/') ||
                IsCompilerExecutable(arguments[0]))
                return arguments;

            auto candidate = arguments[0];
            for (std::size_t index = 1; index < arguments.size(); ++index)
            {
                if (!arguments[index].empty() && (arguments[index].front() == '-' || arguments[index].front() == '/'))
                    break;
                candidate += ' ';
                candidate += arguments[index];
                if (!IsCompilerExecutable(candidate)) continue;

                std::vector<std::string> repaired{};
                repaired.reserve(arguments.size() - index);
                repaired.push_back(std::move(candidate));
                repaired.insert(repaired.end(),
                                std::make_move_iterator(arguments.begin() + static_cast<std::ptrdiff_t>(index + 1)),
                                std::make_move_iterator(arguments.end()));
                return repaired;
            }
            return arguments;
        }

        [[nodiscard]] auto StableCommandDigest(const std::vector<std::string> &arguments) -> std::string
        {
            std::uint64_t hash = 14695981039346656037ULL;
            for (const auto &argument : arguments)
            {
                for (const unsigned char ch : argument) { hash ^= ch; hash *= 1099511628211ULL; }
                hash ^= 0xffU;
                hash *= 1099511628211ULL;
            }
            std::ostringstream out{};
            out << std::hex << std::setfill('0') << std::setw(16) << hash;
            return out.str();
        }

        [[nodiscard]] auto NormalizeBootstrapMessage(std::string message) -> std::string
        {
            static const std::regex suffix{R"(\s*\[([^\]]+)\]\s*$)"};
            std::smatch match{};
            if (std::regex_search(message, match, suffix))
                message.replace(static_cast<std::size_t>(match.position()),
                                static_cast<std::size_t>(match.length()),
                                " [clang-tidy:" + match[1].str() + "]");
            return message;
        }

        [[nodiscard]] auto BootstrapDiagnosticCode(const std::string &message) -> std::string
        {
            static const std::regex suffix{R"(\[clang-tidy:([^\]]+)\]\s*$)"};
            std::smatch match{};
            return std::regex_search(message, match, suffix) ? match[1].str() : std::string{};
        }

        [[nodiscard]] auto Trim(std::string value) -> std::string
        {
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return {};
            const auto last = value.find_last_not_of(" \t\r\n");
            return value.substr(first, last - first + 1);
        }

        [[nodiscard]] auto ParseYamlScalar(std::string value) -> std::string
        {
            value = Trim(std::move(value));
            if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'')
            {
                value = value.substr(1, value.size() - 2);
                std::size_t position = 0;
                while ((position = value.find("''", position)) != std::string::npos)
                    value.replace(position, 2, "'"), ++position;
                return value;
            }
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            {
                value = value.substr(1, value.size() - 2);
                std::string decoded{};
                bool escaped = false;
                for (const auto character : value)
                {
                    if (!escaped && character == '\\') { escaped = true; continue; }
                    if (escaped)
                    {
                        decoded += character == 'n' ? '\n' : character == 'r' ? '\r'
                                   : character == 't' ? '\t' : character;
                        escaped = false;
                    }
                    else decoded += character;
                }
                if (escaped) decoded += '\\';
                return decoded;
            }
            return value;
        }

        struct BootstrapReplacement
        {
            fs::path file{};
            std::size_t offset{};
            std::size_t length{};
            std::string text{};
        };

        struct BootstrapFix
        {
            std::string diagnosticName{};
            fs::path diagnosticFile{};
            std::vector<BootstrapReplacement> replacements{};
        };

        [[nodiscard]] auto ParseBootstrapFixYaml(const fs::path &path) -> std::vector<BootstrapFix>
        {
            std::ifstream input(path, std::ios::binary);
            if (!input) return {};
            std::vector<BootstrapFix> fixes{};
            BootstrapFix *currentFix = nullptr;
            BootstrapReplacement *currentReplacement = nullptr;
            bool inReplacements = false;
            std::string line{};
            while (std::getline(input, line))
            {
                const auto trimmed = Trim(line);
                const auto valueAfter = [&](std::string_view key) {
                    return ParseYamlScalar(trimmed.substr(key.size()));
                };
                if (trimmed.starts_with("- DiagnosticName:"))
                {
                    fixes.push_back(BootstrapFix{.diagnosticName = valueAfter("- DiagnosticName:")});
                    currentFix = &fixes.back();
                    currentReplacement = nullptr;
                    inReplacements = false;
                }
                else if (currentFix != nullptr && trimmed.starts_with("FilePath:") && !inReplacements)
                {
                    currentFix->diagnosticFile = valueAfter("FilePath:");
                }
                else if (currentFix != nullptr && trimmed.starts_with("Replacements:"))
                {
                    inReplacements = trimmed != "Replacements: []";
                }
                else if (currentFix != nullptr && inReplacements && trimmed.starts_with("- FilePath:"))
                {
                    currentFix->replacements.push_back(BootstrapReplacement{
                        .file = valueAfter("- FilePath:")});
                    currentReplacement = &currentFix->replacements.back();
                }
                else if (currentReplacement != nullptr && trimmed.starts_with("Offset:"))
                {
                    currentReplacement->offset = static_cast<std::size_t>(
                        std::stoull(valueAfter("Offset:")));
                }
                else if (currentReplacement != nullptr && trimmed.starts_with("Length:"))
                {
                    currentReplacement->length = static_cast<std::size_t>(
                        std::stoull(valueAfter("Length:")));
                }
                else if (currentReplacement != nullptr && trimmed.starts_with("ReplacementText:"))
                {
                    currentReplacement->text = valueAfter("ReplacementText:");
                }
            }
            std::erase_if(fixes, [](const auto &fix) { return fix.replacements.empty(); });
            return fixes;
        }

        [[nodiscard]] auto PositionAtByteOffset(std::string_view text, std::size_t offset)
            -> ToolProtocolPosition
        {
            if (offset > text.size()) throw std::runtime_error("clang-tidy replacement offset is outside the file");
            ToolProtocolPosition position{};
            for (std::size_t index = 0; index < offset; ++index)
            {
                if (text[index] == '\n') { ++position.line; position.column = 1; }
                else ++position.column;
            }
            return position;
        }

        struct LimitedProcessResult
        {
            int exitCode{};
            std::string output{};
            std::string errorOutput{};
            bool cancelled{false};
            bool timedOut{false};
            bool outputLimitExceeded{false};
        };

#if defined(_WIN32)
        std::array<std::atomic<std::uintptr_t>, 64> ActiveWindowsToolJobs{};
        std::atomic_bool WindowsToolCancellationRequested{false};
        std::mutex WindowsToolJobMutex{};
        std::size_t ActiveWindowsToolJobCount{};

        auto WINAPI ForwardWindowsToolTermination(DWORD controlType) -> BOOL
        {
            if (controlType != CTRL_C_EVENT && controlType != CTRL_BREAK_EVENT &&
                controlType != CTRL_CLOSE_EVENT && controlType != CTRL_SHUTDOWN_EVENT)
                return FALSE;
            WindowsToolCancellationRequested.store(true, std::memory_order_relaxed);
            for (const auto &slot : ActiveWindowsToolJobs)
            {
                const auto value = slot.load(std::memory_order_relaxed);
                if (value != 0) ::TerminateJobObject(reinterpret_cast<HANDLE>(value), 4);
            }
            return TRUE;
        }

        [[nodiscard]] auto RegisterWindowsToolJob(HANDLE job) -> std::size_t
        {
            std::scoped_lock lock{WindowsToolJobMutex};
            if (ActiveWindowsToolJobCount == 0)
            {
                WindowsToolCancellationRequested.store(false, std::memory_order_relaxed);
                ::SetConsoleCtrlHandler(ForwardWindowsToolTermination, TRUE);
            }
            for (std::size_t index = 0; index < ActiveWindowsToolJobs.size(); ++index)
            {
                if (ActiveWindowsToolJobs[index].load(std::memory_order_relaxed) == 0)
                {
                    ActiveWindowsToolJobs[index].store(reinterpret_cast<std::uintptr_t>(job),
                                                       std::memory_order_relaxed);
                    ++ActiveWindowsToolJobCount;
                    return index;
                }
            }
            throw std::runtime_error("too many concurrent tool driver processes");
        }

        auto UnregisterWindowsToolJob(std::size_t slot) -> void
        {
            std::scoped_lock lock{WindowsToolJobMutex};
            ActiveWindowsToolJobs[slot].store(0, std::memory_order_relaxed);
            if (ActiveWindowsToolJobCount > 0) --ActiveWindowsToolJobCount;
            if (ActiveWindowsToolJobCount == 0)
            {
                ::SetConsoleCtrlHandler(ForwardWindowsToolTermination, FALSE);
                WindowsToolCancellationRequested.store(false, std::memory_order_relaxed);
            }
        }

        [[nodiscard]] auto QuoteWindowsArgument(std::wstring_view value) -> std::wstring
        {
            if (value.find_first_of(L" \t\"") == std::wstring_view::npos) return std::wstring(value);
            std::wstring result{L"\""};
            std::size_t slashes = 0;
            for (const auto character : value)
            {
                if (character == L'\\') { ++slashes; continue; }
                if (character == L'\"') result.append(slashes * 2 + 1, L'\\');
                else result.append(slashes, L'\\');
                slashes = 0;
                result.push_back(character);
            }
            result.append(slashes * 2, L'\\');
            result.push_back(L'\"');
            return result;
        }
#else
        static_assert(std::atomic<pid_t>::is_always_lock_free);
        std::array<std::atomic<pid_t>, 64> ActiveToolProcessGroups{};
        std::atomic<int> ToolCancellationRequested{0};
        std::mutex ToolSignalMutex{};
        std::size_t ActiveToolProcessCount{};
        struct sigaction PreviousToolTermination{};
        struct sigaction PreviousToolInterrupt{};

        auto ForwardToolTermination(const int signal) -> void
        {
            ToolCancellationRequested.store(signal, std::memory_order_relaxed);
            for (const auto &slot : ActiveToolProcessGroups)
            {
                const auto processGroup = slot.load(std::memory_order_relaxed);
                if (processGroup > 0) ::kill(-processGroup, SIGTERM);
            }
        }

        [[nodiscard]] auto RegisterToolProcessGroup(pid_t processGroup) -> std::size_t
        {
            std::scoped_lock lock{ToolSignalMutex};
            if (ActiveToolProcessCount == 0)
            {
                ToolCancellationRequested.store(0, std::memory_order_relaxed);
                struct sigaction action{};
                action.sa_handler = ForwardToolTermination;
                ::sigemptyset(&action.sa_mask);
                action.sa_flags = 0;
                ::sigaction(SIGTERM, &action, &PreviousToolTermination);
                ::sigaction(SIGINT, &action, &PreviousToolInterrupt);
            }
            for (std::size_t index = 0; index < ActiveToolProcessGroups.size(); ++index)
            {
                if (ActiveToolProcessGroups[index].load(std::memory_order_relaxed) == 0)
                {
                    ActiveToolProcessGroups[index].store(processGroup, std::memory_order_relaxed);
                    ++ActiveToolProcessCount;
                    return index;
                }
            }
            throw std::runtime_error("too many concurrent tool driver processes");
        }

        auto UnregisterToolProcessGroup(std::size_t slot) -> void
        {
            std::scoped_lock lock{ToolSignalMutex};
            ActiveToolProcessGroups[slot].store(0, std::memory_order_relaxed);
            if (ActiveToolProcessCount > 0) --ActiveToolProcessCount;
            if (ActiveToolProcessCount == 0)
            {
                ::sigaction(SIGTERM, &PreviousToolTermination, nullptr);
                ::sigaction(SIGINT, &PreviousToolInterrupt, nullptr);
                ToolCancellationRequested.store(0, std::memory_order_relaxed);
            }
        }
#endif

        [[nodiscard]] auto RunLimitedProcess(
            const fs::path &executable, const std::vector<std::string> &arguments,
            const fs::path &workingDirectory, std::optional<std::uint64_t> timeoutMilliseconds,
            std::size_t maximumOutputBytes,
            const std::function<void(std::string_view)> &stdoutLineObserver = {}) -> LimitedProcessResult
        {
#if defined(_WIN32)
            SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
            HANDLE stdoutRead{}, stdoutWrite{}, stderrRead{}, stderrWrite{};
            if (!::CreatePipe(&stdoutRead, &stdoutWrite, &security, 0) ||
                !::CreatePipe(&stderrRead, &stderrWrite, &security, 0))
                throw std::runtime_error("failed to create tool process pipes");
            ::SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);
            ::SetHandleInformation(stderrRead, HANDLE_FLAG_INHERIT, 0);

            std::wstring command = QuoteWindowsArgument(executable.wstring());
            for (const auto &argument : arguments)
            {
                command.push_back(L' ');
                command += QuoteWindowsArgument(fs::path(argument).wstring());
            }
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdOutput = stdoutWrite;
            startup.hStdError = stderrWrite;
            startup.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
            PROCESS_INFORMATION process{};
            auto mutableCommand = std::vector<wchar_t>(command.begin(), command.end());
            mutableCommand.push_back(L'\0');
            std::vector<wchar_t> environmentBlock{};
            for (const auto *name : {L"PATH", L"SystemRoot", L"TEMP", L"TMP", L"USERPROFILE"})
            {
                const auto size = ::GetEnvironmentVariableW(name, nullptr, 0);
                if (size == 0) continue;
                std::wstring value(size, L'\0');
                ::GetEnvironmentVariableW(name, value.data(), size);
                value.resize(size - 1);
                const std::wstring entry = std::wstring(name) + L"=" + value;
                environmentBlock.insert(environmentBlock.end(), entry.begin(), entry.end());
                environmentBlock.push_back(L'\0');
            }
            environmentBlock.push_back(L'\0');
            const auto created = ::CreateProcessW(
                executable.wstring().c_str(), mutableCommand.data(), nullptr, nullptr, TRUE,
                CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                environmentBlock.data(), workingDirectory.wstring().c_str(),
                &startup, &process);
            ::CloseHandle(stdoutWrite);
            ::CloseHandle(stderrWrite);
            if (!created)
            {
                ::CloseHandle(stdoutRead); ::CloseHandle(stderrRead);
                throw std::runtime_error("failed to create tool process");
            }
            const auto job = ::CreateJobObjectW(nullptr, nullptr);
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (job == nullptr ||
                !::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
                !::AssignProcessToJobObject(job, process.hProcess))
            {
                ::TerminateProcess(process.hProcess, 3);
                ::CloseHandle(process.hThread); ::CloseHandle(process.hProcess);
                if (job != nullptr) ::CloseHandle(job);
                ::CloseHandle(stdoutRead); ::CloseHandle(stderrRead);
                throw std::runtime_error("failed to create isolated tool process job");
            }
            const auto jobSlot = RegisterWindowsToolJob(job);
            ::ResumeThread(process.hThread);
            LimitedProcessResult result{};
            std::string pendingStdout{};
            const auto append = [&](std::string &target, const char *bytes, std::size_t count,
                                    bool stdoutStream) {
                const auto consumed = result.output.size() + result.errorOutput.size();
                const auto available = maximumOutputBytes > consumed ? maximumOutputBytes - consumed : 0;
                const auto accepted = std::min(available, count);
                target.append(bytes, accepted);
                if (stdoutStream && stdoutLineObserver)
                {
                    pendingStdout.append(bytes, accepted);
                    while (true)
                    {
                        const auto newline = pendingStdout.find('\n');
                        if (newline == std::string::npos) break;
                        auto line = pendingStdout.substr(0, newline);
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        stdoutLineObserver(line);
                        pendingStdout.erase(0, newline + 1);
                    }
                }
                if (accepted != count)
                {
                    result.outputLimitExceeded = true;
                    ::TerminateJobObject(job, 3);
                }
            };
            const auto drain = [&](HANDLE pipe, std::string &target, bool stdoutStream) {
                std::array<char, 4096> buffer{};
                DWORD available{};
                while (::PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) && available > 0)
                {
                    DWORD read{};
                    if (!::ReadFile(pipe, buffer.data(),
                                    static_cast<DWORD>(std::min<std::size_t>(buffer.size(), available)),
                                    &read, nullptr) || read == 0) break;
                    append(target, buffer.data(), read, stdoutStream);
                }
            };
            const auto started = std::chrono::steady_clock::now();
            while (::WaitForSingleObject(process.hProcess, 10) == WAIT_TIMEOUT)
            {
                drain(stdoutRead, result.output, true);
                drain(stderrRead, result.errorOutput, false);
                if (WindowsToolCancellationRequested.load(std::memory_order_relaxed))
                {
                    result.cancelled = true;
                    ::TerminateJobObject(job, 4);
                }
                if (timeoutMilliseconds.has_value() &&
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started).count() >=
                        static_cast<std::int64_t>(*timeoutMilliseconds))
                {
                    result.timedOut = true;
                    ::TerminateJobObject(job, 5);
                }
            }
            drain(stdoutRead, result.output, true);
            drain(stderrRead, result.errorOutput, false);
            if (stdoutLineObserver && !pendingStdout.empty()) stdoutLineObserver(pendingStdout);
            DWORD exitCode{};
            ::GetExitCodeProcess(process.hProcess, &exitCode);
            result.exitCode = static_cast<int>(exitCode);
            UnregisterWindowsToolJob(jobSlot);
            ::CloseHandle(stdoutRead); ::CloseHandle(stderrRead);
            ::CloseHandle(process.hThread); ::CloseHandle(process.hProcess); ::CloseHandle(job);
            return result;
#else
            int stdoutPipe[2]{};
            int stderrPipe[2]{};
            if (::pipe(stdoutPipe) != 0 || ::pipe(stderrPipe) != 0)
                throw std::runtime_error("failed to create tool process pipes");
            std::vector<std::string> storage{executable.string()};
            storage.insert(storage.end(), arguments.begin(), arguments.end());
            std::vector<char *> argv{};
            for (auto &entry : storage) argv.push_back(entry.data());
            argv.push_back(nullptr);
            const auto inheritedValue = [](const char *name) {
                const auto *value = std::getenv(name);
                return value == nullptr ? std::string{} : std::string(value);
            };
            const std::array<std::pair<const char *, std::string>, 4> safeEnvironment{{
                {"PATH", inheritedValue("PATH")},
                {"HOME", inheritedValue("HOME")},
                {"TMPDIR", inheritedValue("TMPDIR")},
                {"LANG", inheritedValue("LANG")},
            }};
            const auto processId = ::fork();
            if (processId < 0)
            {
                ::close(stdoutPipe[0]); ::close(stdoutPipe[1]);
                ::close(stderrPipe[0]); ::close(stderrPipe[1]);
                throw std::runtime_error("failed to fork tool process");
            }
            if (processId == 0)
            {
                ::setpgid(0, 0);
                ::close(stdoutPipe[0]);
                ::close(stderrPipe[0]);
                ::dup2(stdoutPipe[1], STDOUT_FILENO);
                ::dup2(stderrPipe[1], STDERR_FILENO);
                ::close(stdoutPipe[1]);
                ::close(stderrPipe[1]);
                if (::chdir(workingDirectory.c_str()) != 0) std::_Exit(127);
                ::clearenv();
                for (const auto &[name, value] : safeEnvironment)
                    if (!value.empty()) ::setenv(name, value.c_str(), 1);
                ::execvp(executable.c_str(), argv.data());
                std::_Exit(errno == ENOENT ? 127 : 126);
            }
            const auto processSlot = RegisterToolProcessGroup(processId);
            ::close(stdoutPipe[1]);
            ::close(stderrPipe[1]);
            ::fcntl(stdoutPipe[0], F_SETFL, ::fcntl(stdoutPipe[0], F_GETFL) | O_NONBLOCK);
            ::fcntl(stderrPipe[0], F_SETFL, ::fcntl(stderrPipe[0], F_GETFL) | O_NONBLOCK);
            LimitedProcessResult result{};
            const auto started = std::chrono::steady_clock::now();
            int status = 0;
            bool completed = false;
            std::optional<std::chrono::steady_clock::time_point> cancellationStarted{};
            std::array<char, 4096> buffer{};
            std::string pendingStdout{};
            const auto drain = [&](const int descriptor, std::string &target) {
                while (true)
                {
                    const auto count = ::read(descriptor, buffer.data(), buffer.size());
                    if (count > 0)
                    {
                        const auto consumed = result.output.size() + result.errorOutput.size();
                        const auto available = maximumOutputBytes > consumed
                                                   ? maximumOutputBytes - consumed : 0;
                        target.append(buffer.data(), std::min<std::size_t>(available, count));
                        if (descriptor == stdoutPipe[0] && stdoutLineObserver)
                        {
                            pendingStdout.append(buffer.data(), std::min<std::size_t>(available, count));
                            while (true)
                            {
                                const auto newline = pendingStdout.find('\n');
                                if (newline == std::string::npos) break;
                                auto line = pendingStdout.substr(0, newline);
                                if (!line.empty() && line.back() == '\r') line.pop_back();
                                stdoutLineObserver(line);
                                pendingStdout.erase(0, newline + 1);
                            }
                        }
                        if (static_cast<std::size_t>(count) > available)
                        {
                            result.outputLimitExceeded = true;
                            ::kill(-processId, SIGKILL);
                        }
                        continue;
                    }
                    if (count < 0 && errno == EINTR) continue;
                    break;
                }
            };
            while (!completed)
            {
                drain(stdoutPipe[0], result.output);
                drain(stderrPipe[0], result.errorOutput);
                const auto waited = ::waitpid(processId, &status, WNOHANG);
                completed = waited == processId;
                if (!completed && ToolCancellationRequested.load(std::memory_order_relaxed) != 0)
                {
                    result.cancelled = true;
                    if (!cancellationStarted.has_value())
                    {
                        cancellationStarted = std::chrono::steady_clock::now();
                        ::kill(-processId, SIGTERM);
                    }
                    else if (std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - *cancellationStarted).count() >= 250)
                    {
                        ::kill(-processId, SIGKILL);
                    }
                }
                if (!completed && timeoutMilliseconds.has_value() &&
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started).count() >=
                        static_cast<std::int64_t>(*timeoutMilliseconds))
                {
                    result.timedOut = true;
                    ::kill(-processId, SIGKILL);
                }
                if (!completed)
                {
                    pollfd descriptors[2]{
                        {.fd = stdoutPipe[0], .events = POLLIN, .revents = 0},
                        {.fd = stderrPipe[0], .events = POLLIN, .revents = 0},
                    };
                    (void)::poll(descriptors, 2, 10);
                }
            }
            drain(stdoutPipe[0], result.output);
            drain(stderrPipe[0], result.errorOutput);
            if (ToolCancellationRequested.load(std::memory_order_relaxed) != 0)
                result.cancelled = true;
            if (stdoutLineObserver && !pendingStdout.empty()) stdoutLineObserver(pendingStdout);
            ::close(stdoutPipe[0]);
            ::close(stderrPipe[0]);
            UnregisterToolProcessGroup(processSlot);
            result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status)
                              : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1;
            return result;
#endif
        }
    }

    auto LoadToolTranslationUnits(const fs::path &compileCommandsPath,
                                  const std::vector<fs::path> &selectedSources,
                                  std::string_view targetPlatform,
                                  std::string_view owner)
        -> std::vector<ToolDriverRequest::TranslationUnit>
    {
        std::ifstream input(compileCommandsPath, std::ios::binary);
        if (!input) throw std::runtime_error(compileCommandsPath.string() + ": failed to read compilation-unit plan");
        std::ostringstream contents{};
        contents << input.rdbuf();
        const auto text = contents.str();
        auto parsed = NGIN::Serialization::JsonParser::Parse(text);
        if (!parsed.HasValue() || !parsed.Value().Root().IsArray())
            throw std::runtime_error(compileCommandsPath.string() + ": compilation-unit plan must be a JSON array");

        std::unordered_set<std::string> selected{};
        for (const auto &source : selectedSources)
            selected.insert(fs::weakly_canonical(source).generic_string());
        std::vector<ToolDriverRequest::TranslationUnit> result{};
        for (const auto &entry : parsed.Value().Root().AsArray().values)
        {
            if (!entry.IsObject()) continue;
            const auto &object = entry.AsObject();
            const auto directory = fs::path(Required(object, "directory", JsonValue::Type::String).AsString());
            auto source = fs::path(Required(object, "file", JsonValue::Type::String).AsString());
            if (source.is_relative()) source = directory / source;
            source = fs::weakly_canonical(source);
            if (!selected.empty() && !selected.contains(source.generic_string())) continue;
            std::vector<std::string> arguments{};
            if (const auto *array = object.Find("arguments"); array != nullptr && array->IsArray())
            {
                for (const auto &argument : array->AsArray().values)
                    if (argument.IsString()) arguments.emplace_back(argument.AsString());
            }
            else
            {
                arguments = SplitCommand(Required(object, "command", JsonValue::Type::String).AsString());
            }
            arguments = RepairUnquotedWindowsCompilerPath(std::move(arguments));
            if (arguments.empty()) throw std::runtime_error("compilation unit has no compiler command");
            const auto extension = source.extension().string();
            result.push_back(ToolDriverRequest::TranslationUnit{
                .source = source,
                .workingDirectory = directory,
                .compiler = arguments.front(),
                .arguments = arguments,
                .targetPlatform = std::string(targetPlatform),
                .language = extension == ".c" ? "c" : "c++",
                .owner = std::string(owner),
                .generated = false,
                .commandDigest = StableCommandDigest(arguments),
            });
        }
        return result;
    }

    auto ParseToolTimeoutMilliseconds(std::string_view value) -> std::optional<std::uint64_t>
    {
        if (value.empty()) return std::nullopt;
        std::uint64_t multiplier = 1;
        std::size_t suffix = 1;
        if (value.ends_with("ms")) { multiplier = 1; suffix = 2; }
        else if (value.ends_with('s')) multiplier = 1000;
        else if (value.ends_with('m')) multiplier = 60 * 1000;
        else if (value.ends_with('h')) multiplier = 60 * 60 * 1000;
        else throw std::runtime_error("invalid tool timeout");
        return std::stoull(std::string(value.substr(0, value.size() - suffix))) * multiplier;
    }

    auto WriteToolDriverRequest(const ToolDriverRequest &request, const fs::path &path) -> void
    {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            throw std::runtime_error(path.string() + ": failed to write tool driver request");
        }
        const auto writePath = [&](const fs::path &value) {
            out << "{\"absolute\":";
            WriteString(out, fs::absolute(value).lexically_normal().string());
            out << ",\"workspaceRelative\":";
            WriteString(out, value.lexically_relative(request.workspaceRoot).generic_string());
            out << "}";
        };
        out << "{\"schemaVersion\":\"1.0\",\"kind\":\"NGIN.ToolDriver.Request\",\"runId\":";
        WriteString(out, request.runId);
        out << ",\"workspace\":{\"root\":";
        WriteString(out, request.workspaceRoot.string());
        out << ",\"name\":";
        WriteString(out, request.workspaceName);
        out << "},\"project\":{\"path\":";
        WriteString(out, request.projectPath.string());
        out << ",\"name\":";
        WriteString(out, request.projectName);
        out << "},\"profile\":";
        WriteString(out, request.profile);
        out << ",\"action\":{\"name\":";
        WriteString(out, request.actionName);
        out << ",\"kind\":";
        WriteString(out, request.actionKind);
        out << "},\"tool\":{\"path\":";
        WriteString(out, request.tool.path.string());
        out << ",\"source\":";
        WriteString(out, request.tool.source);
        out << ",\"version\":";
        WriteString(out, request.tool.version);
        out << ",\"digest\":";
        WriteString(out, request.tool.digest);
        out << "},\"driver\":{\"name\":";
        WriteString(out, request.driverName);
        out << ",\"protocol\":";
        WriteString(out, request.driverProtocol);
        out << ",\"path\":";
        WriteString(out, request.driver.path.string());
        out << ",\"source\":";
        WriteString(out, request.driver.source);
        out << ",\"version\":";
        WriteString(out, request.driver.version);
        out << ",\"digest\":";
        WriteString(out, request.driver.digest);
        out << "},\"host\":{\"platform\":";
        WriteString(out, request.hostPlatform);
        out << "},\"target\":{\"platform\":";
        WriteString(out, request.targetPlatform);
        out << ",\"abi\":";
        WriteString(out, request.targetAbi);
        out << "},\"workingDirectory\":";
        WriteString(out, request.workingDirectory.string());
        out << ",\"outputDirectory\":";
        WriteString(out, request.outputDirectory.string());
        out << ",\"configs\":[";
        for (std::size_t index = 0; index < request.configs.size(); ++index)
        {
            if (index > 0) out << ',';
            writePath(request.configs[index]);
        }
        out << "],\"arguments\":[";
        for (std::size_t index = 0; index < request.arguments.size(); ++index)
        {
            if (index > 0) out << ',';
            WriteString(out, request.arguments[index]);
        }
        out << "],\"inputSets\":[{\"contract\":";
        WriteString(out, request.inputContract);
        out << ",\"identity\":";
        WriteString(out, request.runId + ":inputs");
        out << ",\"files\":[";
        for (std::size_t index = 0; index < request.files.size(); ++index)
        {
            if (index > 0) out << ',';
            out << "{\"path\":";
            writePath(request.files[index]);
            out << ",\"role\":";
            WriteString(out, request.inputContract == "artifacts/v1" ||
                                  request.inputContract.starts_with("build.") ||
                                  request.inputContract.starts_with("stage.")
                              ? "Artifact" : "Source");
            out << ",\"ownerKind\":\"project\",\"ownerName\":";
            WriteString(out, request.projectName);
            out << ",\"generated\":false";
            if (request.inputContentPath.has_value() && request.files.size() == 1)
            {
                out << ",\"contentPath\":";
                writePath(*request.inputContentPath);
            }
            out << '}';
        }
        out << "],\"translationUnits\":[";
        for (std::size_t index = 0; index < request.translationUnits.size(); ++index)
        {
            if (index > 0) out << ',';
            const auto &unit = request.translationUnits[index];
            out << "{\"source\":";
            writePath(unit.source);
            out << ",\"workingDirectory\":"; WriteString(out, unit.workingDirectory.string());
            out << ",\"compiler\":"; WriteString(out, unit.compiler);
            out << ",\"arguments\":[";
            for (std::size_t argumentIndex = 0; argumentIndex < unit.arguments.size(); ++argumentIndex)
            {
                if (argumentIndex > 0) out << ',';
                WriteString(out, unit.arguments[argumentIndex]);
            }
            out << "],\"targetPlatform\":"; WriteString(out, unit.targetPlatform);
            out << ",\"language\":"; WriteString(out, unit.language);
            out << ",\"owner\":"; WriteString(out, unit.owner);
            out << ",\"generated\":" << (unit.generated ? "true" : "false") << ",\"commandDigest\":";
            WriteString(out, unit.commandDigest);
            out << '}';
        }
        out << "],\"priorResultPaths\":[";
        for (std::size_t index = 0; index < request.priorResultPaths.size(); ++index)
        {
            if (index > 0) out << ',';
            writePath(request.priorResultPaths[index]);
        }
        out << "]}],\"options\":{\"maximumOutputBytes\":" << request.maximumOutputBytes
            << ",\"jobs\":" << request.jobs << ",\"editMode\":";
        WriteString(out, request.editMode);
        if (request.timeoutMilliseconds.has_value())
            out << ",\"timeoutMilliseconds\":" << *request.timeoutMilliseconds;
        out << "},\"environment\":{";
        for (std::size_t index = 0; index < request.environment.size(); ++index)
        {
            if (index > 0) out << ',';
            WriteString(out, request.environment[index].name);
            out << ':';
            WriteString(out, request.environment[index].value);
        }
        out << "},\"capabilitiesRequested\":[";
        for (std::size_t index = 0; index < request.capabilitiesRequested.size(); ++index)
        {
            if (index > 0) out << ',';
            WriteString(out, request.capabilitiesRequested[index]);
        }
        out << "]}\n";
        out.close();
        fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write,
                        fs::perm_options::replace);
    }

    auto ParseToolDriverEvents(std::string_view output, std::string_view expectedRunId) -> ToolDriverResult
    {
        ToolDriverResult result{};
        result.rawOutput = std::string(output);
        std::istringstream lines{std::string(output)};
        std::string line{};
        std::int64_t expectedSequence = 1;
        try
        {
            while (std::getline(lines, line))
            {
                if (line.empty()) continue;
                auto parsed = NGIN::Serialization::JsonParser::Parse(std::string_view{line});
                if (!parsed.HasValue() || !parsed.Value().Root().IsObject())
                    throw std::runtime_error("tool driver emitted malformed JSONL");
                const auto &event = parsed.Value().Root().AsObject();
                if (Required(event, "schemaVersion", JsonValue::Type::String).AsString() != "1.0" ||
                    Required(event, "kind", JsonValue::Type::String).AsString() != "NGIN.ToolDriver.Event")
                    throw std::runtime_error("tool driver emitted an incompatible protocol event");
                if (Required(event, "runId", JsonValue::Type::String).AsString() != expectedRunId)
                    throw std::runtime_error("tool driver event runId does not match the request");
                const auto sequence = static_cast<std::int64_t>(Required(event, "sequence", JsonValue::Type::Number).AsNumber());
                if (sequence != expectedSequence++)
                    throw std::runtime_error("tool driver event sequence is not monotonic");
                const auto type = Required(event, "type", JsonValue::Type::String).AsString();
                const auto &data = Required(event, "data", JsonValue::Type::Object).AsObject();
                if (result.completed)
                    throw std::runtime_error("tool driver emitted an event after run.completed");
                if (type == "diagnostic")
                    result.diagnostics.push_back(ParseDiagnostic(data));
                else if (type == "edit.proposed")
                    result.edits.push_back(ParseEditSet(data));
                else if (type == "artifact.produced")
                {
                    const auto artifactPath = OptionalString(data, "path");
                    if (!artifactPath.empty()) result.artifacts.emplace_back(artifactPath);
                }
                else if (type == "metric")
                {
                    result.metrics.push_back(ToolProtocolMetric{
                        .name = std::string(Required(data, "name", JsonValue::Type::String).AsString()),
                        .value = Required(data, "value", JsonValue::Type::Number).AsNumber(),
                        .unit = OptionalString(data, "unit"),
                    });
                }
                else if (type == "run.completed")
                {
                    if (result.completed)
                        throw std::runtime_error("tool driver emitted multiple run.completed events");
                    result.completed = true;
                    result.executionStatus = std::string(Required(data, "status", JsonValue::Type::String).AsString());
                    if (result.executionStatus != "succeeded" && result.executionStatus != "failed" &&
                        result.executionStatus != "cancelled" && result.executionStatus != "timed-out")
                        throw std::runtime_error("tool driver run.completed has unsupported status '" +
                                                 result.executionStatus + "'");
                }
                else if (type != "run.started" && type != "progress" && type != "log")
                    throw std::runtime_error("tool driver emitted unknown event type '" + std::string(type) + "'");
            }
            if (!result.completed)
                throw std::runtime_error("tool driver reached EOF without run.completed");
        }
        catch (const std::exception &error)
        {
            result.executionStatus = "failed";
            result.protocolError = error.what();
        }
        return result;
    }

    auto ParseToolDriverProbeEvents(std::string_view output, std::string_view expectedRunId)
        -> ToolDriverProbeResult
    {
        ToolDriverProbeResult result{};
        result.rawOutput = std::string(output);
        std::istringstream lines{std::string(output)};
        std::string line{};
        std::int64_t expectedSequence = 1;
        try
        {
            while (std::getline(lines, line))
            {
                if (line.empty()) continue;
                auto parsed = NGIN::Serialization::JsonParser::Parse(std::string_view{line});
                if (!parsed.HasValue() || !parsed.Value().Root().IsObject())
                    throw std::runtime_error("tool driver probe emitted malformed JSONL");
                const auto &event = parsed.Value().Root().AsObject();
                if (Required(event, "schemaVersion", JsonValue::Type::String).AsString() != "1.0" ||
                    Required(event, "kind", JsonValue::Type::String).AsString() != "NGIN.ToolDriver.Event")
                    throw std::runtime_error("tool driver probe emitted an incompatible protocol event");
                if (Required(event, "runId", JsonValue::Type::String).AsString() != expectedRunId)
                    throw std::runtime_error("tool driver probe event runId does not match the request");
                const auto sequence = static_cast<std::int64_t>(
                    Required(event, "sequence", JsonValue::Type::Number).AsNumber());
                if (sequence != expectedSequence++)
                    throw std::runtime_error("tool driver probe event sequence is not monotonic");
                if (result.completed)
                    throw std::runtime_error("tool driver probe emitted an event after probe.completed");
                const auto type = Required(event, "type", JsonValue::Type::String).AsString();
                const auto &data = Required(event, "data", JsonValue::Type::Object).AsObject();
                if (type == "probe.completed")
                {
                    result.completed = true;
                    result.available = Required(data, "available", JsonValue::Type::Bool).AsBool();
                    const auto *compatible = data.Find("hostCompatible");
                    result.hostCompatible = compatible == nullptr || !compatible->IsBool()
                                                ? result.available : compatible->AsBool();
                    result.driverVersion = OptionalString(data, "driverVersion");
                    result.toolVersion = OptionalString(data, "toolVersion");
                    result.reason = OptionalString(data, "reason");
                    const auto appendStrings = [&](std::string_view name, std::vector<std::string> &target) {
                        if (const auto *values = data.Find(name); values != nullptr && values->IsArray())
                            for (const auto &value : values->AsArray().values)
                                if (value.IsString()) target.emplace_back(value.AsString());
                    };
                    appendStrings("protocols", result.protocols);
                    appendStrings("capabilities", result.capabilities);
                }
                else if (type != "log" && type != "progress")
                    throw std::runtime_error("tool driver probe emitted unexpected event type '" +
                                             std::string(type) + "'");
            }
            if (!result.completed)
                throw std::runtime_error("tool driver probe reached EOF without probe.completed");
            if (std::find(result.protocols.begin(), result.protocols.end(), "NGIN.ToolDriver/1") ==
                result.protocols.end())
                throw std::runtime_error("tool driver probe does not support NGIN.ToolDriver/1");
        }
        catch (const std::exception &error)
        {
            result.available = false;
            result.protocolError = error.what();
        }
        return result;
    }

    auto DeduplicateToolDiagnostics(const std::vector<ToolProtocolDiagnostic> &diagnostics)
        -> std::vector<ToolProtocolDiagnostic>
    {
        std::vector<ToolProtocolDiagnostic> result{};
        std::unordered_map<std::string, std::size_t> indices{};
        for (const auto &diagnostic : diagnostics)
        {
            std::vector<std::string> fingerprintParts{
                diagnostic.severity, diagnostic.code, diagnostic.message};
            if (diagnostic.primaryLocation.has_value())
            {
                fingerprintParts.push_back(diagnostic.primaryLocation->file.generic_string());
                fingerprintParts.push_back(std::to_string(diagnostic.primaryLocation->start.line));
                fingerprintParts.push_back(std::to_string(diagnostic.primaryLocation->start.column));
            }
            const auto key = diagnostic.fingerprint.empty()
                                 ? StableCommandDigest(fingerprintParts)
                                 : diagnostic.fingerprint;
            const auto [entry, inserted] = indices.emplace(key, result.size());
            if (inserted)
            {
                result.push_back(diagnostic);
                if (result.back().fingerprint.empty()) result.back().fingerprint = key;
                continue;
            }
            auto &existing = result[entry->second];
            for (const auto &related : diagnostic.relatedLocations)
            {
                const auto duplicate = std::any_of(
                    existing.relatedLocations.begin(), existing.relatedLocations.end(),
                    [&](const ToolProtocolLocation &candidate) {
                        return candidate.file == related.file && candidate.start.line == related.start.line &&
                               candidate.start.column == related.start.column && candidate.message == related.message;
                    });
                if (!duplicate) existing.relatedLocations.push_back(related);
            }
            for (const auto &editId : diagnostic.editSetIds)
                if (std::find(existing.editSetIds.begin(), existing.editSetIds.end(), editId) == existing.editSetIds.end())
                    existing.editSetIds.push_back(editId);
        }
        return result;
    }

    auto ToolRequestCacheKey(const ToolDriverRequest &request) -> std::string
    {
        std::vector<std::string> parts{
            request.driverProtocol, request.driverName, request.driver.path.generic_string(),
            request.driver.source, request.driver.version, request.driver.digest,
            request.actionName, request.actionKind,
            request.tool.path.generic_string(), request.tool.source,
            request.tool.version, request.tool.digest,
            request.hostPlatform, request.targetPlatform, request.targetAbi,
            request.inputContract, std::to_string(request.jobs), request.editMode,
            std::to_string(request.maximumOutputBytes),
            request.timeoutMilliseconds.has_value() ? std::to_string(*request.timeoutMilliseconds) : ""};
        const auto addFile = [&](const fs::path &path) {
            parts.push_back(fs::weakly_canonical(path).generic_string());
            std::ifstream input(path, std::ios::binary);
            std::ostringstream contents{};
            if (input) contents << input.rdbuf();
            parts.push_back(contents.str());
        };
        for (const auto &config : request.configs) addFile(config);
        parts.insert(parts.end(), request.arguments.begin(), request.arguments.end());
        for (const auto &file : request.files) addFile(file);
        if (request.inputContentPath.has_value()) addFile(*request.inputContentPath);
        for (const auto &result : request.priorResultPaths) addFile(result);
        for (const auto &unit : request.translationUnits)
        {
            parts.push_back(unit.source.generic_string());
            parts.push_back(unit.commandDigest);
        }
        parts.insert(parts.end(), request.capabilitiesRequested.begin(),
                     request.capabilitiesRequested.end());
        for (const auto &variable : request.environment)
        {
            if (variable.secret)
            {
                if (variable.cacheKey)
                    parts.push_back("secret-environment:" + variable.name + ":" +
                                    StableCommandDigest({variable.value}));
            }
            else
            {
                parts.push_back("environment:" + variable.name + "=" + variable.value);
            }
        }
        return StableCommandDigest(parts);
    }

    auto ToolFileDigest(const fs::path &path) -> std::string
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error(path.string() + ": failed to read file for edit validation");
        std::ostringstream contents{};
        contents << input.rdbuf();
        return StableCommandDigest({contents.str()});
    }

    auto ApplyToolEdits(const std::vector<ToolProtocolEditSet> &editSets,
                        const std::optional<fs::path> &allowedRoot) -> void
    {
        const auto canonicalRoot = allowedRoot.has_value()
                                       ? std::optional<fs::path>{fs::weakly_canonical(*allowedRoot)}
                                       : std::nullopt;
        for (const auto &editSet : editSets)
        {
            for (const auto &fileEdits : editSet.files)
            {
                const auto canonicalFile = fs::weakly_canonical(fileEdits.file);
                if (canonicalRoot.has_value())
                {
                    const auto relative = canonicalFile.lexically_relative(*canonicalRoot);
                    if (relative.empty() || relative.is_absolute() || relative.string().starts_with(".."))
                        throw std::runtime_error(fileEdits.file.string() +
                                                 ": refused tool edit outside the workspace");
                }
                if (!fileEdits.expectedDigest.empty() && ToolFileDigest(fileEdits.file) != fileEdits.expectedDigest)
                    throw std::runtime_error(fileEdits.file.string() +
                                             ": refused stale tool edit set '" + editSet.id + "'");
                std::ifstream input(fileEdits.file, std::ios::binary);
                std::ostringstream contents{};
                contents << input.rdbuf();
                auto text = contents.str();
                const auto offset = [&](const ToolProtocolPosition &position) {
                    std::size_t currentLine = 1;
                    std::size_t index = 0;
                    while (currentLine < static_cast<std::size_t>(position.line) && index < text.size())
                        if (text[index++] == '\n') ++currentLine;
                    if (currentLine != static_cast<std::size_t>(position.line))
                        throw std::runtime_error(fileEdits.file.string() + ": tool edit line is outside the file");
                    const auto value = index + static_cast<std::size_t>(position.column - 1);
                    const auto lineEnd = text.find('\n', index);
                    const auto maximum = lineEnd == std::string::npos ? text.size() : lineEnd;
                    if (value > maximum)
                        throw std::runtime_error(fileEdits.file.string() + ": tool edit column is outside the file");
                    return value;
                };
                struct ResolvedEdit { std::size_t start; std::size_t end; std::string text; };
                std::vector<ResolvedEdit> resolved{};
                for (const auto &edit : fileEdits.edits)
                {
                    const auto start = offset(edit.start);
                    const auto end = offset(edit.end);
                    if (end < start) throw std::runtime_error("tool edit range ends before it starts");
                    resolved.push_back({start, end, edit.newText});
                }
                std::sort(resolved.begin(), resolved.end(), [](const auto &left, const auto &right) {
                    return left.start > right.start;
                });
                std::size_t previousStart = text.size() + 1;
                for (const auto &edit : resolved)
                {
                    if (edit.end > previousStart)
                        throw std::runtime_error(fileEdits.file.string() + ": overlapping tool edits are not allowed");
                    text.replace(edit.start, edit.end - edit.start, edit.text);
                    previousStart = edit.start;
                }
                std::ofstream output(fileEdits.file, std::ios::binary | std::ios::trunc);
                if (!output) throw std::runtime_error(fileEdits.file.string() + ": failed to apply tool edits");
                output << text;
            }
        }
    }

    auto LoadToolBaseline(const fs::path &path) -> std::vector<std::string>
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error(path.string() + ": tool baseline does not exist");
        std::ostringstream contents{};
        contents << input.rdbuf();
        const auto text = contents.str();
        static const std::regex fingerprintPattern{R"re("fingerprint":"([^"]+)")re"};
        std::vector<std::string> fingerprints{};
        for (std::sregex_iterator it(text.begin(), text.end(), fingerprintPattern), end; it != end; ++it)
            fingerprints.push_back((*it)[1].str());
        return fingerprints;
    }

    auto WriteToolBaseline(std::string_view runName,
                           const std::vector<ToolProtocolDiagnostic> &diagnostics,
                           const fs::path &path) -> void
    {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error(path.string() + ": failed to write tool baseline");
        out << "{\"schemaVersion\":\"1.0\",\"kind\":\"NGIN.ToolBaseline\",\"run\":";
        WriteString(out, runName);
        out << ",\"findings\":[";
        for (std::size_t index = 0; index < diagnostics.size(); ++index)
        {
            if (index > 0) out << ',';
            out << "{\"fingerprint\":"; WriteString(out, diagnostics[index].fingerprint);
            out << ",\"code\":"; WriteString(out, diagnostics[index].code);
            out << ",\"message\":"; WriteString(out, diagnostics[index].message);
            out << ",\"reason\":\"accepted by explicit NGIN baseline update\"";
            out << '}';
        }
        out << "]}\n";
    }

    auto WriteToolResultReplay(const ToolDriverResult &result, std::string_view runId,
                               const fs::path &path) -> void
    {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error(path.string() + ": failed to write tool cache entry");
        std::int64_t sequence = 1;
        const auto prefix = [&](std::string_view type) {
            out << "{\"schemaVersion\":\"1.0\",\"kind\":\"NGIN.ToolDriver.Event\",\"runId\":";
            WriteString(out, runId);
            out << ",\"sequence\":" << sequence++ << ",\"type\":";
            WriteString(out, type);
            out << ",\"data\":";
        };
        const auto writePosition = [&](const ToolProtocolPosition &position) {
            out << "{\"line\":" << position.line << ",\"column\":" << position.column << '}';
        };
        const auto writeLocation = [&](const ToolProtocolLocation &location) {
            out << "{\"file\":{\"absolute\":"; WriteString(out, location.file.string());
            out << ",\"workspaceRelative\":\"\"},\"range\":{\"start\":";
            writePosition(location.start);
            if (location.end.has_value()) { out << ",\"end\":"; writePosition(*location.end); }
            out << '}';
            if (!location.message.empty()) { out << ",\"message\":"; WriteString(out, location.message); }
            out << '}';
        };
        prefix("run.started"); out << "{}}\n";
        for (const auto &diagnostic : result.diagnostics)
        {
            prefix("diagnostic");
            out << "{\"severity\":"; WriteString(out, diagnostic.severity);
            out << ",\"code\":"; WriteString(out, diagnostic.code);
            out << ",\"originalSeverity\":"; WriteString(out, diagnostic.originalSeverity);
            out << ",\"originalCode\":"; WriteString(out, diagnostic.originalCode);
            out << ",\"message\":"; WriteString(out, diagnostic.message);
            out << ",\"fingerprint\":"; WriteString(out, diagnostic.fingerprint);
            out << ",\"suppressed\":" << (diagnostic.suppressed ? "true" : "false");
            out << ",\"suppressionSource\":"; WriteString(out, diagnostic.suppressionSource);
            out << ",\"suppressionReason\":"; WriteString(out, diagnostic.suppressionReason);
            if (diagnostic.primaryLocation.has_value()) { out << ",\"primaryLocation\":"; writeLocation(*diagnostic.primaryLocation); }
            out << ",\"relatedLocations\":[";
            for (std::size_t index = 0; index < diagnostic.relatedLocations.size(); ++index)
            { if (index > 0) out << ','; writeLocation(diagnostic.relatedLocations[index]); }
            out << "],\"tags\":[";
            for (std::size_t index = 0; index < diagnostic.tags.size(); ++index)
            { if (index > 0) out << ','; WriteString(out, diagnostic.tags[index]); }
            out << "],\"editSetIds\":[";
            for (std::size_t index = 0; index < diagnostic.editSetIds.size(); ++index)
            { if (index > 0) out << ','; WriteString(out, diagnostic.editSetIds[index]); }
            out << "]}}\n";
        }
        for (const auto &editSet : result.edits)
        {
            prefix("edit.proposed");
            out << "{\"id\":"; WriteString(out, editSet.id);
            out << ",\"label\":"; WriteString(out, editSet.label);
            out << ",\"applicability\":"; WriteString(out, editSet.applicability);
            out << ",\"files\":[";
            for (std::size_t fileIndex = 0; fileIndex < editSet.files.size(); ++fileIndex)
            {
                if (fileIndex > 0) out << ',';
                const auto &file = editSet.files[fileIndex];
                out << "{\"path\":{\"absolute\":"; WriteString(out, file.file.string());
                out << ",\"workspaceRelative\":\"\"},\"expectedDigest\":";
                WriteString(out, file.expectedDigest);
                out << ",\"edits\":[";
                for (std::size_t editIndex = 0; editIndex < file.edits.size(); ++editIndex)
                {
                    if (editIndex > 0) out << ',';
                    const auto &edit = file.edits[editIndex];
                    out << "{\"range\":{\"start\":"; writePosition(edit.start);
                    out << ",\"end\":"; writePosition(edit.end);
                    out << "},\"newText\":"; WriteString(out, edit.newText); out << '}';
                }
                out << "]}";
            }
            out << "]}}\n";
        }
        for (const auto &artifact : result.artifacts)
        { prefix("artifact.produced"); out << "{\"path\":"; WriteString(out, artifact.string()); out << "}}\n"; }
        for (const auto &metric : result.metrics)
        {
            prefix("metric"); out << "{\"name\":"; WriteString(out, metric.name);
            out << ",\"value\":" << metric.value << ",\"unit\":";
            WriteString(out, metric.unit); out << "}}\n";
        }
        prefix("run.completed"); out << "{\"status\":"; WriteString(out, result.executionStatus); out << "}}\n";
    }

    auto ReadToolResultReplay(const fs::path &path) -> ToolDriverResult
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error(path.string() + ": failed to read tool cache entry");
        std::ostringstream contents{};
        contents << input.rdbuf();
        const auto text = contents.str();
        static const std::regex runIdPattern{R"re("runId":"([^"]+)")re"};
        std::smatch match{};
        if (!std::regex_search(text, match, runIdPattern))
            throw std::runtime_error(path.string() + ": cached tool result has no runId");
        auto result = ParseToolDriverEvents(text, match[1].str());
        result.cacheStatus = "hit";
        return result;
    }

    auto ExecuteToolDriver(const fs::path &driverExecutable,
                           const ToolDriverRequest &request,
                           const fs::path &requestPath,
                           const std::function<void(const ToolDriverStreamEvent &)> &observer) -> ToolDriverResult
    {
        WriteToolDriverRequest(request, requestPath);
        const auto observeLine = [&](std::string_view line)
        {
            if (!observer || line.empty()) return;
            auto parsed = NGIN::Serialization::JsonParser::Parse(line);
            if (!parsed.HasValue() || !parsed.Value().Root().IsObject()) return;
            const auto &event = parsed.Value().Root().AsObject();
            const auto *type = event.Find("type");
            const auto *runId = event.Find("runId");
            const auto *data = event.Find("data");
            if (type == nullptr || !type->IsString() || runId == nullptr || !runId->IsString() ||
                runId->AsString() != request.runId || data == nullptr || !data->IsObject()) return;
            if (type->AsString() != "progress" && type->AsString() != "log") return;
            const auto &payload = data->AsObject();
            ToolDriverStreamEvent streamed{
                .type = std::string(type->AsString()),
                .stage = OptionalString(payload, "stage"),
                .message = OptionalString(payload, "message"),
            };
            if (const auto *current = payload.Find("current"); current != nullptr && current->IsNumber())
                streamed.current = static_cast<std::int64_t>(current->AsNumber());
            if (const auto *total = payload.Find("total"); total != nullptr && total->IsNumber())
                streamed.total = static_cast<std::int64_t>(total->AsNumber());
            observer(streamed);
        };
        const auto process = RunLimitedProcess(
            driverExecutable, {"--ngin-request", requestPath.string()},
            request.workingDirectory, request.timeoutMilliseconds, request.maximumOutputBytes,
            observeLine);
        auto result = ParseToolDriverEvents(process.output, request.runId);
        result.processExitCode = process.exitCode;
        result.driverLog = process.errorOutput;
        if (process.cancelled)
        {
            result.executionStatus = "cancelled";
            result.protocolError = "tool driver execution was cancelled";
        }
        else if (process.timedOut)
        {
            result.executionStatus = "timed-out";
            result.protocolError = "tool driver exceeded its timeout";
        }
        else if (process.outputLimitExceeded)
        {
            result.executionStatus = "failed";
            result.protocolError = "tool driver exceeded its output limit";
        }
        else if (process.exitCode != 0)
        {
            result.executionStatus = "failed";
            if (!result.protocolError.empty()) result.protocolError += "; ";
            result.protocolError += "tool driver exited with code " + std::to_string(process.exitCode);
        }
        return result;
    }

    auto ExecuteToolDriverProbe(const fs::path &driverExecutable,
                                const ToolDriverRequest &request,
                                const fs::path &requestPath) -> ToolDriverProbeResult
    {
        WriteToolDriverRequest(request, requestPath);
        const auto process = RunLimitedProcess(
            driverExecutable, {"--ngin-probe", requestPath.string()},
            request.workingDirectory,
            request.timeoutMilliseconds.value_or(5000),
            std::min<std::size_t>(request.maximumOutputBytes, 1024U * 1024U));
        auto result = ParseToolDriverProbeEvents(process.output, request.runId);
        result.driverLog = process.errorOutput;
        if (process.cancelled)
            result.protocolError = "tool driver probe was cancelled";
        else if (process.timedOut)
            result.protocolError = "tool driver probe exceeded its timeout";
        else if (process.outputLimitExceeded)
            result.protocolError = "tool driver probe exceeded its output limit";
        else if (process.exitCode != 0)
            result.protocolError = "tool driver probe exited with code " + std::to_string(process.exitCode);
        if (!result.protocolError.empty()) result.available = false;
        return result;
    }

    auto ProbeBuiltinToolAdapter(std::string_view adapter,
                                 const ToolResolution &tool,
                                 const std::vector<std::string> &probeArguments)
        -> ToolDriverProbeResult
    {
        ToolDriverProbeResult result{};
        if (adapter != "builtin.clang-tidy.v1" &&
            adapter != "builtin.stdout-transform.v1")
        {
            result.protocolError = "no registered tool adapter named '" + std::string(adapter) + "'";
            return result;
        }
        result.driverVersion = "1.0.0";
        result.protocols = {"NGIN.ToolDriver/1"};
        result.capabilities = adapter == "builtin.clang-tidy.v1"
            ? std::vector<std::string>{"diagnostics", "related-locations", "fixes",
                                       "active-file", "changed-files"}
            : std::vector<std::string>{"edits", "active-file", "changed-files",
                                       "document-formatting"};
        const auto effectiveProbeArguments = adapter == "builtin.clang-tidy.v1"
                                                 ? std::vector<std::string>{"--version"}
                                                 : probeArguments;
        if (effectiveProbeArguments.empty())
        {
            result.completed = true;
            result.available = true;
            result.hostCompatible = true;
            result.toolVersion = tool.version;
            return result;
        }
        const auto process = RunLimitedProcess(
            tool.path, effectiveProbeArguments, tool.path.parent_path(), 5000, 1024U * 1024U);
        result.driverLog = process.errorOutput;
        result.rawOutput = process.output;
        if (process.exitCode != 0 || process.timedOut || process.outputLimitExceeded)
        {
            result.available = false;
            result.protocolError = process.timedOut ? "adapter tool version probe timed out"
                                 : process.outputLimitExceeded ? "adapter tool version probe exceeded its output limit"
                                 : "adapter tool version probe exited with code " + std::to_string(process.exitCode);
            return result;
        }
        static const std::regex versionPattern{R"re(([0-9]+(?:\.[0-9]+)+))re"};
        std::smatch match{};
        if (std::regex_search(process.output, match, versionPattern)) result.toolVersion = match[1].str();
        result.completed = true;
        result.available = true;
        result.hostCompatible = true;
        return result;
    }

    auto ExecuteBuiltinToolAdapter(std::string_view adapter,
                                   const ToolDriverRequest &request,
                                   const fs::path &compilationDatabaseDirectory)
        -> ToolDriverResult
    {
        ToolDriverResult aggregate{};
        aggregate.executionStatus = "succeeded";
        aggregate.completed = true;
        if (adapter == "builtin.stdout-transform.v1")
        {
            if (request.actionKind != "Format" && request.actionKind != "Transform")
            {
                aggregate.executionStatus = "failed";
                aggregate.protocolError = "registered adapter does not support action kind '" +
                                          request.actionKind + "'";
                return aggregate;
            }

            const auto expandArgument = [&](std::string value, const fs::path &input) {
                const auto replaceAll = [&](std::string_view token, std::string_view replacement) {
                    std::size_t offset = 0;
                    while ((offset = value.find(token, offset)) != std::string::npos)
                    {
                        value.replace(offset, token.size(), replacement);
                        offset += replacement.size();
                    }
                };
                replaceAll("$(InputFile)", input.string());
                replaceAll("$(InputContentFile)",
                           request.inputContentPath.has_value() && request.files.size() == 1
                               ? request.inputContentPath->string()
                               : input.string());
                replaceAll("$(Config)", request.configs.empty() ? "" : request.configs.front().string());
                replaceAll("$(WorkspaceRoot)", request.workspaceRoot.string());
                replaceAll("$(ProjectPath)", request.projectPath.string());
                replaceAll("$(WorkingDirectory)", request.workingDirectory.string());
                replaceAll("$(OutputDirectory)", request.outputDirectory.string());
                return value;
            };

            for (const auto &input : request.files)
            {
                const auto contentInput = request.inputContentPath.has_value() && request.files.size() == 1
                                              ? *request.inputContentPath
                                              : input;
                std::ifstream source(contentInput, std::ios::binary);
                if (!source)
                {
                    aggregate.executionStatus = "failed";
                    aggregate.protocolError = input.string() + ": failed to read transform input";
                    break;
                }
                std::ostringstream contents{};
                contents << source.rdbuf();
                const auto original = contents.str();

                std::vector<std::string> arguments{};
                bool hasInputArgument = false;
                for (const auto &argumentTemplate : request.arguments)
                {
                    if (argumentTemplate.contains("$(Config)") && request.configs.empty()) continue;
                    hasInputArgument = hasInputArgument || argumentTemplate.contains("$(InputFile)") ||
                                       argumentTemplate.contains("$(InputContentFile)");
                    arguments.push_back(expandArgument(argumentTemplate, input));
                }
                if (!hasInputArgument) arguments.push_back(input.string());

                const auto process = RunLimitedProcess(
                    request.tool.path, arguments, request.workingDirectory,
                    request.timeoutMilliseconds, request.maximumOutputBytes);
                aggregate.processExitCode = std::max(aggregate.processExitCode, process.exitCode);
                aggregate.driverLog += process.errorOutput;
                if (process.cancelled)
                {
                    aggregate.executionStatus = "cancelled";
                    aggregate.protocolError = "adapter tool execution was cancelled";
                    break;
                }
                if (process.timedOut)
                {
                    aggregate.executionStatus = "timed-out";
                    aggregate.protocolError = "adapter tool exceeded its timeout";
                    break;
                }
                if (process.outputLimitExceeded)
                {
                    aggregate.executionStatus = "failed";
                    aggregate.protocolError = "adapter tool exceeded its output limit";
                    break;
                }
                if (process.exitCode != 0)
                {
                    aggregate.executionStatus = "failed";
                    aggregate.protocolError = "registered adapter tool process exited with code " +
                                              std::to_string(process.exitCode);
                    break;
                }
                if (process.output == original) continue;

                aggregate.edits.push_back(ToolProtocolEditSet{
                    .id = "stdout-transform-" + StableCommandDigest(
                        {input.generic_string(), original, process.output}),
                    .label = (request.actionKind == "Format" ? "Format " : "Transform ") +
                             input.filename().string(),
                    .applicability = "automatic",
                    .files = {ToolProtocolFileEdits{
                        .file = input,
                        .expectedDigest = StableCommandDigest({original}),
                        .edits = {ToolProtocolTextEdit{
                            .start = ToolProtocolPosition{.line = 1, .column = 1},
                            .end = PositionAtByteOffset(original, original.size()),
                            .newText = process.output,
                        }},
                    }},
                });
            }
            return aggregate;
        }
        if (adapter != "builtin.clang-tidy.v1")
        {
            aggregate.executionStatus = "failed";
            aggregate.protocolError = "unknown registered built-in adapter '" + std::string(adapter) + "'";
            return aggregate;
        }
        if (request.actionKind != "Analyze")
        {
            aggregate.executionStatus = "failed";
            aggregate.protocolError = "registered adapter does not support action kind '" + request.actionKind + "'";
            return aggregate;
        }

        static const std::regex diagnosticPattern{
            R"(^(.+):(\d+):(\d+):\s+(warning|error|fatal error|note):\s+(.+)$)"};
        fs::create_directories(request.outputDirectory / "native");
        for (const auto &source : request.files)
        {
            std::vector<std::string> arguments{"-p", compilationDatabaseDirectory.string()};
            if (!request.configs.empty()) arguments.push_back("--config-file=" + request.configs.front().string());
            const auto fixesPath = request.outputDirectory / "native" /
                                   (StableCommandDigest({source.generic_string()}) + ".fixes.yaml");
            arguments.push_back("--export-fixes=" + fixesPath.string());
            arguments.push_back(source.string());
            const auto process = RunLimitedProcess(
                request.tool.path, arguments, request.workingDirectory,
                request.timeoutMilliseconds, request.maximumOutputBytes);
            aggregate.processExitCode = std::max(aggregate.processExitCode, process.exitCode);
            aggregate.rawOutput += process.output;
            std::istringstream lines{process.output};
            std::string line{};
            while (std::getline(lines, line))
            {
                std::smatch match{};
                if (!std::regex_match(line, match, diagnosticPattern)) continue;
                auto severity = match[4].str();
                if (severity == "fatal error") severity = "fatal";
                auto message = NormalizeBootstrapMessage(match[5].str());
                const auto file = fs::path(match[1].str());
                const auto lineNumber = std::stoll(match[2].str());
                const auto column = std::stoll(match[3].str());
                const auto code = BootstrapDiagnosticCode(message);
                aggregate.diagnostics.push_back(ToolProtocolDiagnostic{
                    .severity = severity,
                    .code = code,
                    .message = message,
                    .primaryLocation = ToolProtocolLocation{
                        .file = file,
                        .start = ToolProtocolPosition{.line = lineNumber, .column = column},
                    },
                    .fingerprint = StableCommandDigest({file.generic_string(), std::to_string(lineNumber),
                                                        std::to_string(column), code, message}),
                });
            }
            for (const auto &fix : ParseBootstrapFixYaml(fixesPath))
            {
                ToolProtocolEditSet editSet{
                    .id = "clang-tidy-" + StableCommandDigest({
                        source.generic_string(), fix.diagnosticName,
                        std::to_string(fix.replacements.front().offset)}),
                    .label = "Apply clang-tidy fix for " + fix.diagnosticName,
                    .applicability = "suggested",
                };
                std::map<fs::path, std::vector<BootstrapReplacement>> replacementsByFile{};
                for (const auto &replacement : fix.replacements)
                    replacementsByFile[fs::weakly_canonical(replacement.file)].push_back(replacement);
                for (auto &[file, replacements] : replacementsByFile)
                {
                    std::ifstream fileInput(file, std::ios::binary);
                    if (!fileInput) throw std::runtime_error(file.string() +
                                                             ": failed to read clang-tidy replacement file");
                    std::ostringstream contents{};
                    contents << fileInput.rdbuf();
                    const auto text = contents.str();
                    ToolProtocolFileEdits fileEdits{
                        .file = file,
                        .expectedDigest = ToolFileDigest(file),
                    };
                    std::sort(replacements.begin(), replacements.end(),
                              [](const auto &left, const auto &right) { return left.offset < right.offset; });
                    std::size_t previousEnd = 0;
                    for (const auto &replacement : replacements)
                    {
                        if (replacement.offset < previousEnd ||
                            replacement.offset + replacement.length > text.size())
                            throw std::runtime_error(file.string() +
                                                     ": clang-tidy produced invalid overlapping replacements");
                        previousEnd = replacement.offset + replacement.length;
                        fileEdits.edits.push_back(ToolProtocolTextEdit{
                            .start = PositionAtByteOffset(text, replacement.offset),
                            .end = PositionAtByteOffset(text, replacement.offset + replacement.length),
                            .newText = replacement.text,
                        });
                    }
                    editSet.files.push_back(std::move(fileEdits));
                }
                for (auto diagnostic = aggregate.diagnostics.rbegin();
                     diagnostic != aggregate.diagnostics.rend(); ++diagnostic)
                {
                    if (diagnostic->code != fix.diagnosticName) continue;
                    if (diagnostic->primaryLocation.has_value() && !fix.diagnosticFile.empty() &&
                        fs::weakly_canonical(diagnostic->primaryLocation->file) !=
                            fs::weakly_canonical(fix.diagnosticFile))
                        continue;
                    diagnostic->editSetIds.push_back(editSet.id);
                    break;
                }
                aggregate.edits.push_back(std::move(editSet));
            }
            if (process.cancelled) { aggregate.executionStatus = "cancelled"; aggregate.protocolError = "adapter tool execution was cancelled"; }
            else if (process.timedOut) { aggregate.executionStatus = "timed-out"; aggregate.protocolError = "adapter tool exceeded its timeout"; }
            else if (process.outputLimitExceeded) { aggregate.executionStatus = "failed"; aggregate.protocolError = "adapter tool exceeded its output limit"; }
            else if (process.exitCode != 0) aggregate.executionStatus = "failed";
        }
        if (aggregate.executionStatus == "failed" && aggregate.protocolError.empty())
            aggregate.protocolError = "registered adapter tool process exited with code " +
                                      std::to_string(aggregate.processExitCode);
        return aggregate;
    }

    auto WriteNormalizedToolResult(const ToolDriverRequest &request,
                                   std::string_view runName,
                                   std::string_view driverName,
                                   const ToolDriverResult &result,
                                   std::optional<bool> gateFailed,
                                   const fs::path &path,
                                   std::int64_t durationMilliseconds) -> void
    {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error(path.string() + ": failed to write normalized tool result");
        const auto writePosition = [&](const ToolProtocolPosition &position) {
            out << "{\"line\":" << position.line << ",\"column\":" << position.column << '}';
        };
        const auto writeLocation = [&](const ToolProtocolLocation &location) {
            out << "{\"file\":{\"absolute\":";
            WriteString(out, location.file.string());
            out << ",\"workspaceRelative\":";
            WriteString(out, location.file.lexically_relative(request.workspaceRoot).generic_string());
            out << "},\"range\":{\"start\":";
            writePosition(location.start);
            if (location.end.has_value()) { out << ",\"end\":"; writePosition(*location.end); }
            out << '}';
            if (!location.message.empty()) { out << ",\"message\":"; WriteString(out, location.message); }
            out << '}';
        };
        out << "{\"schemaVersion\":\"1.0\",\"kind\":\"NGIN.ToolResult\",\"runId\":";
        WriteString(out, request.runId);
        out << ",\"run\":"; WriteString(out, runName);
        out << ",\"action\":"; WriteString(out, request.actionName);
        out << ",\"tool\":"; WriteString(out, request.tool.path.string());
        out << ",\"driver\":"; WriteString(out, driverName);
        out << ",\"executionStatus\":"; WriteString(out, result.executionStatus);
        out << ",\"gateStatus\":";
        WriteString(out, !gateFailed.has_value() ? "not-evaluated" : *gateFailed ? "failed" : "passed");
        out << ",\"cacheStatus\":"; WriteString(out, result.cacheStatus);
        out << ",\"changeStatus\":"; WriteString(out, result.changeStatus);
        out << ",\"durationMs\":" << durationMilliseconds;
        out << ",\"diagnostics\":[";
        for (std::size_t index = 0; index < result.diagnostics.size(); ++index)
        {
            if (index > 0) out << ',';
            const auto &diagnostic = result.diagnostics[index];
            out << "{\"severity\":"; WriteString(out, diagnostic.effectiveSeverity.empty()
                                                           ? diagnostic.severity : diagnostic.effectiveSeverity);
            out << ",\"intrinsicSeverity\":"; WriteString(out, diagnostic.intrinsicSeverity.empty()
                                                                    ? diagnostic.severity : diagnostic.intrinsicSeverity);
            out << ",\"effectiveSeverity\":"; WriteString(out, diagnostic.effectiveSeverity.empty()
                                                                    ? diagnostic.severity : diagnostic.effectiveSeverity);
            out << ",\"code\":"; WriteString(out, diagnostic.code);
            out << ",\"originalSeverity\":"; WriteString(out, diagnostic.originalSeverity);
            out << ",\"originalCode\":"; WriteString(out, diagnostic.originalCode);
            out << ",\"message\":"; WriteString(out, diagnostic.message);
            out << ",\"documentationUrl\":"; WriteString(out, diagnostic.documentationUrl);
            out << ",\"fingerprint\":"; WriteString(out, diagnostic.fingerprint);
            out << ",\"suppressed\":" << (diagnostic.suppressed ? "true" : "false");
            out << ",\"baselined\":" << (diagnostic.baselined ? "true" : "false");
            out << ",\"suppressionSource\":"; WriteString(out, diagnostic.suppressionSource);
            out << ",\"suppressionReason\":"; WriteString(out, diagnostic.suppressionReason);
            out << ",\"tags\":[";
            for (std::size_t tag = 0; tag < diagnostic.tags.size(); ++tag)
            {
                if (tag > 0) out << ',';
                WriteString(out, diagnostic.tags[tag]);
            }
            out << "],\"editSetIds\":[";
            for (std::size_t edit = 0; edit < diagnostic.editSetIds.size(); ++edit)
            {
                if (edit > 0) out << ',';
                WriteString(out, diagnostic.editSetIds[edit]);
            }
            out << ']';
            if (diagnostic.primaryLocation.has_value()) { out << ",\"primaryLocation\":"; writeLocation(*diagnostic.primaryLocation); }
            out << ",\"relatedLocations\":[";
            for (std::size_t related = 0; related < diagnostic.relatedLocations.size(); ++related)
            {
                if (related > 0) out << ',';
                writeLocation(diagnostic.relatedLocations[related]);
            }
            out << "]}";
        }
        out << "],\"edits\":[";
        for (std::size_t index = 0; index < result.edits.size(); ++index)
        {
            if (index > 0) out << ',';
            const auto &editSet = result.edits[index];
            out << "{\"id\":"; WriteString(out, editSet.id);
            out << ",\"label\":"; WriteString(out, editSet.label);
            out << ",\"applicability\":"; WriteString(out, editSet.applicability);
            out << ",\"files\":[";
            for (std::size_t fileIndex = 0; fileIndex < editSet.files.size(); ++fileIndex)
            {
                if (fileIndex > 0) out << ',';
                const auto &file = editSet.files[fileIndex];
                out << "{\"path\":{\"absolute\":"; WriteString(out, file.file.string());
                out << ",\"workspaceRelative\":";
                WriteString(out, file.file.lexically_relative(request.workspaceRoot).generic_string());
                out << "},\"expectedDigest\":"; WriteString(out, file.expectedDigest);
                out << ",\"edits\":[";
                for (std::size_t editIndex = 0; editIndex < file.edits.size(); ++editIndex)
                {
                    if (editIndex > 0) out << ',';
                    out << "{\"range\":{\"start\":";
                    writePosition(file.edits[editIndex].start);
                    out << ",\"end\":";
                    writePosition(file.edits[editIndex].end);
                    out << "},\"newText\":";
                    WriteString(out, file.edits[editIndex].newText);
                    out << '}';
                }
                out << "]}";
            }
            out << "]}";
        }
        out << "],\"artifacts\":[";
        for (std::size_t index = 0; index < result.artifacts.size(); ++index)
        {
            if (index > 0) out << ',';
            out << "{\"kind\":\"tool-report\",\"path\":";
            WriteString(out, result.artifacts[index].string());
            out << '}';
        }
        out << "],\"metrics\":[";
        for (std::size_t index = 0; index < result.metrics.size(); ++index)
        {
            if (index > 0) out << ',';
            out << "{\"name\":"; WriteString(out, result.metrics[index].name);
            out << ",\"value\":" << result.metrics[index].value << ",\"unit\":";
            WriteString(out, result.metrics[index].unit);
            out << '}';
        }
        out << "]}\n";
    }

    auto WriteToolReport(const ToolDriverRequest &request,
                         std::string_view runName,
                         std::string_view driverName,
                         const ToolDriverResult &result,
                         std::optional<bool> gateFailed,
                         std::string_view format,
                         const fs::path &path) -> void
    {
        if (format == "json")
        {
            WriteNormalizedToolResult(request, runName, driverName, result, gateFailed, path);
            return;
        }
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error(path.string() + ": failed to write tool report");
        if (format == "text")
        {
            for (const auto &diagnostic : result.diagnostics)
            {
                out << '[' << (diagnostic.effectiveSeverity.empty() ? diagnostic.severity
                                                                    : diagnostic.effectiveSeverity) << "] ";
                if (diagnostic.primaryLocation.has_value())
                    out << diagnostic.primaryLocation->file.string() << ':'
                        << diagnostic.primaryLocation->start.line << ':'
                        << diagnostic.primaryLocation->start.column << ": ";
                out << diagnostic.message << '\n';
            }
            return;
        }
        if (format == "jsonl")
        {
            std::int64_t sequence = 1;
            for (const auto &diagnostic : result.diagnostics)
            {
                out << "{\"schemaVersion\":\"1.0\",\"kind\":\"NGIN.ToolResult.Event\",\"runId\":";
                WriteString(out, request.runId);
                out << ",\"sequence\":" << sequence++ << ",\"type\":\"diagnostic\",\"data\":{\"severity\":";
                WriteString(out, diagnostic.effectiveSeverity.empty() ? diagnostic.severity
                                                                      : diagnostic.effectiveSeverity);
                out << ",\"intrinsicSeverity\":";
                WriteString(out, diagnostic.intrinsicSeverity.empty() ? diagnostic.severity
                                                                      : diagnostic.intrinsicSeverity);
                out << ",\"code\":"; WriteString(out, diagnostic.code);
                out << ",\"message\":"; WriteString(out, diagnostic.message);
                out << ",\"fingerprint\":"; WriteString(out, diagnostic.fingerprint);
                out << "}}\n";
            }
            out << "{\"schemaVersion\":\"1.0\",\"kind\":\"NGIN.ToolResult.Event\",\"runId\":";
            WriteString(out, request.runId);
            out << ",\"sequence\":" << sequence
                << ",\"type\":\"run.completed\",\"data\":{\"executionStatus\":";
            WriteString(out, result.executionStatus);
            out << ",\"gateStatus\":";
            WriteString(out, !gateFailed.has_value() ? "not-evaluated" : *gateFailed ? "failed" : "passed");
            out << ",\"changeStatus\":";
            WriteString(out, result.changeStatus);
            out << "}}\n";
            return;
        }
        if (format == "sarif")
        {
            out << "{\"$schema\":\"https://json.schemastore.org/sarif-2.1.0.json\","
                   "\"version\":\"2.1.0\",\"runs\":[{\"tool\":{\"driver\":{\"name\":";
            WriteString(out, driverName);
            out << ",\"version\":";
            WriteString(out, request.driver.version);
            out << ",\"rules\":[";
            std::set<std::string> writtenRules{};
            bool firstRule = true;
            for (const auto &diagnostic : result.diagnostics)
            {
                if (diagnostic.code.empty() || !writtenRules.insert(diagnostic.code).second) continue;
                if (!firstRule) out << ',';
                firstRule = false;
                out << "{\"id\":"; WriteString(out, diagnostic.code);
                out << ",\"shortDescription\":{\"text\":"; WriteString(out, diagnostic.code); out << '}';
                if (!diagnostic.documentationUrl.empty())
                {
                    out << ",\"helpUri\":";
                    WriteString(out, diagnostic.documentationUrl);
                }
                if (!diagnostic.tags.empty())
                {
                    out << ",\"properties\":{\"tags\":[";
                    for (std::size_t tag = 0; tag < diagnostic.tags.size(); ++tag)
                    {
                        if (tag > 0) out << ',';
                        WriteString(out, diagnostic.tags[tag]);
                    }
                    out << "]}";
                }
                out << '}';
            }
            out << "]}},\"artifacts\":[";
            std::set<std::string> writtenArtifacts{};
            bool firstArtifact = true;
            const auto writeArtifact = [&](const fs::path &artifact) {
                const auto identity = artifact.generic_string();
                if (identity.empty() || !writtenArtifacts.insert(identity).second) return;
                if (!firstArtifact) out << ',';
                firstArtifact = false;
                out << "{\"location\":{\"uri\":";
                WriteString(out, identity);
                out << "}}";
            };
            for (const auto &diagnostic : result.diagnostics)
            {
                if (diagnostic.primaryLocation.has_value()) writeArtifact(diagnostic.primaryLocation->file);
                for (const auto &related : diagnostic.relatedLocations) writeArtifact(related.file);
            }
            for (const auto &editSet : result.edits)
                for (const auto &file : editSet.files) writeArtifact(file.file);
            for (const auto &artifact : result.artifacts) writeArtifact(artifact);
            out << "],\"invocations\":[{\"executionSuccessful\":"
                << (result.executionStatus == "succeeded" ? "true" : "false")
                << ",\"exitCode\":" << result.processExitCode << "}],\"results\":[";
            for (std::size_t index = 0; index < result.diagnostics.size(); ++index)
            {
                if (index > 0) out << ',';
                const auto &diagnostic = result.diagnostics[index];
                const auto severity = diagnostic.effectiveSeverity.empty() ? diagnostic.severity
                                                                           : diagnostic.effectiveSeverity;
                out << "{\"ruleId\":"; WriteString(out, diagnostic.code);
                out << ",\"level\":";
                WriteString(out, severity == "warning" ? "warning" :
                                 (severity == "note" || severity == "info") ? "note" : "error");
                out << ",\"message\":{\"text\":"; WriteString(out, diagnostic.message); out << '}';
                out << ",\"properties\":{\"intrinsicSeverity\":";
                WriteString(out, diagnostic.intrinsicSeverity.empty() ? diagnostic.severity
                                                                      : diagnostic.intrinsicSeverity);
                out << ",\"effectiveSeverity\":"; WriteString(out, severity);
                out << ",\"run\":"; WriteString(out, runName);
                if (!diagnostic.originalCode.empty())
                {
                    out << ",\"originalCode\":";
                    WriteString(out, diagnostic.originalCode);
                }
                out << '}';
                if (!diagnostic.fingerprint.empty())
                {
                    out << ",\"partialFingerprints\":{\"nginFingerprint\":";
                    WriteString(out, diagnostic.fingerprint);
                    out << '}';
                }
                if (diagnostic.suppressed)
                {
                    out << ",\"suppressions\":[{\"kind\":\"external\",\"status\":\"accepted\",\"justification\":";
                    WriteString(out, diagnostic.suppressionReason);
                    out << "}]";
                }
                if (diagnostic.primaryLocation.has_value())
                {
                    out << ",\"locations\":[{\"physicalLocation\":{\"artifactLocation\":{\"uri\":";
                    WriteString(out, diagnostic.primaryLocation->file.generic_string());
                    out << "},\"region\":{\"startLine\":" << diagnostic.primaryLocation->start.line
                        << ",\"startColumn\":" << diagnostic.primaryLocation->start.column;
                    if (diagnostic.primaryLocation->end.has_value())
                        out << ",\"endLine\":" << diagnostic.primaryLocation->end->line
                            << ",\"endColumn\":" << diagnostic.primaryLocation->end->column;
                    out << "}}}]";
                }
                if (!diagnostic.relatedLocations.empty())
                {
                    out << ",\"relatedLocations\":[";
                    for (std::size_t related = 0; related < diagnostic.relatedLocations.size(); ++related)
                    {
                        if (related > 0) out << ',';
                        const auto &location = diagnostic.relatedLocations[related];
                        out << "{\"id\":" << related + 1
                            << ",\"physicalLocation\":{\"artifactLocation\":{\"uri\":";
                        WriteString(out, location.file.generic_string());
                        out << "},\"region\":{\"startLine\":" << location.start.line
                            << ",\"startColumn\":" << location.start.column << "}}";
                        if (!location.message.empty())
                        {
                            out << ",\"message\":{\"text\":";
                            WriteString(out, location.message);
                            out << '}';
                        }
                        out << '}';
                    }
                    out << ']';
                }
                bool wroteFix = false;
                for (const auto &editId : diagnostic.editSetIds)
                    for (const auto &editSet : result.edits)
                        if (editSet.id == editId)
                        {
                            out << (wroteFix ? "," : ",\"fixes\":[")
                                << "{\"description\":{\"text\":";
                            WriteString(out, editSet.label.empty() ? "Apply " + editSet.id : editSet.label);
                            out << "},\"artifactChanges\":[";
                            for (std::size_t file = 0; file < editSet.files.size(); ++file)
                            {
                                if (file > 0) out << ',';
                                out << "{\"artifactLocation\":{\"uri\":";
                                WriteString(out, editSet.files[file].file.generic_string());
                                out << "},\"replacements\":[";
                                for (std::size_t edit = 0; edit < editSet.files[file].edits.size(); ++edit)
                                {
                                    if (edit > 0) out << ',';
                                    const auto &replacement = editSet.files[file].edits[edit];
                                    out << "{\"deletedRegion\":{\"startLine\":" << replacement.start.line
                                        << ",\"startColumn\":" << replacement.start.column
                                        << ",\"endLine\":" << replacement.end.line
                                        << ",\"endColumn\":" << replacement.end.column
                                        << "},\"insertedContent\":{\"text\":";
                                    WriteString(out, replacement.newText);
                                    out << "}}";
                                }
                                out << "]}";
                            }
                            out << "]}";
                            wroteFix = true;
                        }
                if (wroteFix) out << ']';
                out << '}';
            }
            out << "]}]}\n";
            return;
        }
        throw std::runtime_error("unsupported normalized tool report format '" + std::string(format) + "'");
    }
}
