#include "CMakeProjectSystem.hpp"

#include <NGIN/Serialization/Core/SourceBuffer.hpp>
#include <NGIN/Serialization/JSON/JsonParser.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace NGIN::CLI
{
    namespace
    {
        namespace fs = std::filesystem;
        using JsonObject = NGIN::Serialization::JSON::ObjectView;
        using JsonValue = NGIN::Serialization::JSON::ValueView;

        struct ConfigurePresetDetails
        {
            CMakePreset preset{};
            bool hidden{false};
            std::optional<std::string> binaryDirectory{};
            std::vector<std::string> inherits{};
        };

        struct PresetData
        {
            std::map<std::string, ConfigurePresetDetails, std::less<>> configure{};
            std::map<std::string, CMakePreset, std::less<>> build{};
            std::map<std::string, CMakePreset, std::less<>> test{};
            std::vector<std::string> diagnostics{};
        };

        struct CapturedProcess
        {
            int exitCode{};
            std::string output{};
        };

        [[nodiscard]] auto ReadText(const fs::path &path) -> std::string
        {
            std::ifstream input(path, std::ios::binary);
            if (!input) throw std::runtime_error("cannot read '" + path.generic_string() + "'");
            std::ostringstream text{};
            text << input.rdbuf();
            return text.str();
        }

        auto WriteText(const fs::path &path, const std::string_view text) -> void
        {
            fs::create_directories(path.parent_path());
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("cannot write '" + path.generic_string() + "'");
            output << text;
        }

        [[nodiscard]] auto String(const JsonObject &object, const std::string_view key)
            -> std::optional<std::string>
        {
            const auto value = object.Find(key);
            if (!value.has_value()) return std::nullopt;
            const auto text = value->TryString();
            return text.has_value() ? std::optional<std::string>{*text} : std::nullopt;
        }

        [[nodiscard]] auto Boolean(const JsonObject &object, const std::string_view key, const bool fallback = false)
            -> bool
        {
            const auto value = object.Find(key);
            if (!value.has_value()) return fallback;
            return value->TryBool().value_or(fallback);
        }

        [[nodiscard]] auto Integer(const JsonObject &object, const std::string_view key)
            -> std::optional<std::int64_t>
        {
            const auto value = object.Find(key);
            if (!value.has_value()) return std::nullopt;
            return value->TryInt64();
        }

        [[nodiscard]] auto Object(const JsonObject &object, const std::string_view key)
            -> std::optional<JsonObject>
        {
            const auto value = object.Find(key);
            return value.has_value() ? value->TryObject() : std::nullopt;
        }

        [[nodiscard]] auto Array(const JsonObject &object, const std::string_view key)
            -> std::optional<NGIN::Serialization::JSON::ArrayView>
        {
            const auto value = object.Find(key);
            return value.has_value() ? value->TryArray() : std::nullopt;
        }

        [[nodiscard]] auto Strings(const JsonObject &object, const std::string_view key) -> std::vector<std::string>
        {
            std::vector<std::string> result{};
            const auto value = object.Find(key);
            if (!value.has_value()) return result;
            if (const auto text = value->TryString(); text.has_value())
            {
                result.emplace_back(*text);
                return result;
            }
            const auto array = value->TryArray();
            if (!array.has_value()) return result;
            for (const auto item : *array)
                if (const auto text = item.TryString(); text.has_value()) result.emplace_back(*text);
            return result;
        }

        template <typename Callback>
        auto ForObjects(const JsonObject &object, const std::string_view key, Callback callback) -> void
        {
            const auto values = Array(object, key);
            if (!values.has_value()) return;
            for (const auto value : *values)
                if (const auto entry = value.TryObject(); entry.has_value()) callback(*entry);
        }

        [[nodiscard]] auto ParseJson(const fs::path &path) -> NGIN::Serialization::JSON::Document
        {
            auto parsed = NGIN::Serialization::JSON::Parse(NGIN::Serialization::OwnedTextBuffer{ReadText(path)});
            if (!parsed.HasValue())
            {
                const auto &error = parsed.Error();
                throw std::runtime_error(path.generic_string() + ": invalid JSON at " +
                                         std::to_string(error.location.line) + ':' +
                                         std::to_string(error.location.column) + ": " +
                                         std::string(error.message.View()));
            }
            return std::move(parsed.Value());
        }

        [[nodiscard]] auto Quote(const std::string &value) -> std::string
        {
#if defined(_WIN32)
            std::string result{"\""};
            std::size_t slashes = 0;
            for (const auto character : value)
            {
                if (character == '\\')
                {
                    ++slashes;
                    continue;
                }
                if (character == '"')
                {
                    result.append(slashes * 2 + 1, '\\');
                    result += '"';
                    slashes = 0;
                    continue;
                }
                result.append(slashes, '\\');
                slashes = 0;
                result += character;
            }
            result.append(slashes * 2, '\\');
            return result + '"';
#else
            std::string result{"'"};
            for (const auto character : value)
                if (character == '\'')
                    result += "'\\''";
                else
                    result += character;
            return result + '\'';
#endif
        }

        [[nodiscard]] auto RunCaptured(const std::string &executable, const std::vector<std::string> &arguments,
                                       const fs::path &workingDirectory) -> CapturedProcess
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto log = fs::temp_directory_path() / ("ngin-cmake-" + std::to_string(stamp) + ".log");
#if defined(_WIN32)
            std::string command = "cd /d " + Quote(workingDirectory.string()) + " && " + Quote(executable);
#else
            std::string command = "cd " + Quote(workingDirectory.string()) + " && " + Quote(executable);
#endif
            for (const auto &argument : arguments) command += ' ' + Quote(argument);
            command += " > " + Quote(log.string()) + " 2>&1";
            const auto exitCode = std::system(command.c_str());
            std::string output{};
            try
            {
                output = ReadText(log);
            }
            catch (...)
            {
            }
            std::error_code error{};
            fs::remove(log, error);
            return {.exitCode = exitCode, .output = std::move(output)};
        }

        [[nodiscard]] auto RedactSensitiveOutput(std::string output) -> std::string
        {
            static const std::regex assignment{
                R"((^|[\r\n][ \t]*)([A-Za-z_][A-Za-z0-9_]*(?:TOKEN|SECRET|PASSWORD|PASSWD|CREDENTIAL|PRIVATE_KEY)[A-Za-z0-9_]*)[ \t]*=[ \t]*[^\r\n]*)",
                std::regex::icase};
            return std::regex_replace(output, assignment, "$1$2=<redacted>");
        }

        auto Run(const std::string &executable, const std::vector<std::string> &arguments,
                 const fs::path &workingDirectory) -> int
        {
            const auto result = RunCaptured(executable, arguments, workingDirectory);
            if (!result.output.empty()) std::cout << RedactSensitiveOutput(result.output);
            return result.exitCode == 0 ? 0 : 1;
        }

        auto LoadPresetFile(const fs::path &path, PresetData &result, std::set<std::string, std::less<>> &visited)
            -> void
        {
            std::error_code error{};
            const auto canonical = fs::weakly_canonical(path, error).generic_string();
            if (error || !visited.insert(canonical).second || !fs::is_regular_file(path, error)) return;
            auto document = ParseJson(path);
            const auto root = document.Root().TryObject();
            if (!root.has_value()) throw std::runtime_error(path.generic_string() + ": preset root must be an object");

            for (const auto &include : Strings(*root, "include"))
            {
                if (include.contains('$'))
                {
                    result.diagnostics.push_back("preset include with macros is delegated to CMake: " + include);
                    continue;
                }
                LoadPresetFile(path.parent_path() / include, result, visited);
            }

            ForObjects(*root, "configurePresets", [&](const JsonObject &object) {
                const auto name = String(object, "name");
                if (!name.has_value()) return;
                ConfigurePresetDetails details{};
                details.preset.name = *name;
                details.preset.displayName = String(object, "displayName").value_or(*name);
                details.preset.description = String(object, "description").value_or("");
                details.hidden = Boolean(object, "hidden");
                details.binaryDirectory = String(object, "binaryDir");
                details.inherits = Strings(object, "inherits");
                result.configure[*name] = std::move(details);
            });
            const auto readOperationPresets = [&](const std::string_view key,
                                                  std::map<std::string, CMakePreset, std::less<>> &target) {
                ForObjects(*root, key, [&](const JsonObject &object) {
                    const auto name = String(object, "name");
                    if (!name.has_value() || Boolean(object, "hidden")) return;
                    target[*name] = CMakePreset{.name = *name,
                                                .displayName = String(object, "displayName").value_or(*name),
                                                .description = String(object, "description").value_or(""),
                                                .configurePreset = String(object, "configurePreset")};
                });
            };
            readOperationPresets("buildPresets", result.build);
            readOperationPresets("testPresets", result.test);
        }

        [[nodiscard]] auto LoadPresets(const fs::path &root) -> PresetData
        {
            PresetData result{};
            std::set<std::string, std::less<>> visited{};
            LoadPresetFile(root / "CMakePresets.json", result, visited);
            LoadPresetFile(root / "CMakeUserPresets.json", result, visited);
            const auto available = [&](const std::string &executable, const std::vector<std::string> &arguments,
                                       const std::string_view kind) -> std::optional<std::set<std::string, std::less<>>> {
                const auto listed = RunCaptured(executable, arguments, root);
                if (listed.exitCode != 0)
                {
                    result.diagnostics.push_back("CMake could not enumerate " + std::string{kind} + " presets");
                    return std::nullopt;
                }
                std::set<std::string, std::less<>> names{};
                const std::regex quoted{"\"([^\"]+)\""};
                for (auto match = std::sregex_iterator(listed.output.begin(), listed.output.end(), quoted);
                     match != std::sregex_iterator{}; ++match)
                    names.insert((*match)[1].str());
                return names;
            };
            const auto retain = [](auto &values, const std::optional<std::set<std::string, std::less<>>> &names) {
                if (!names.has_value()) return;
                std::erase_if(values, [&](const auto &entry) { return !names->contains(entry.first); });
            };
            const auto configureNames = available("cmake", {"--list-presets"}, "configure");
            if (configureNames.has_value())
                std::erase_if(result.configure, [&](const auto &entry) {
                    return !entry.second.hidden && !configureNames->contains(entry.first);
                });
            retain(result.build, available("cmake", {"--build", "--list-presets"}, "build"));
            retain(result.test, available("ctest", {"--list-presets"}, "test"));
            return result;
        }

        [[nodiscard]] auto PublicPresets(const std::map<std::string, ConfigurePresetDetails, std::less<>> &values)
            -> std::vector<CMakePreset>
        {
            std::vector<CMakePreset> result{};
            for (const auto &[_, value] : values)
                if (!value.hidden) result.push_back(value.preset);
            return result;
        }

        [[nodiscard]] auto PublicPresets(const std::map<std::string, CMakePreset, std::less<>> &values)
            -> std::vector<CMakePreset>
        {
            std::vector<CMakePreset> result{};
            for (const auto &[_, value] : values) result.push_back(value);
            return result;
        }

        [[nodiscard]] auto ResolveBinaryDirectory(const PresetData &presets, const fs::path &root,
                                                  const std::string &preset)
            -> std::optional<fs::path>
        {
            const auto resolveAuthored = [&](const auto &self, const std::string &name,
                                             std::set<std::string, std::less<>> active)
                -> std::optional<std::string> {
                if (!active.insert(name).second)
                    throw std::runtime_error("CMake configure preset inheritance cycle");
                const auto found = presets.configure.find(name);
                if (found == presets.configure.end()) return std::nullopt;
                std::optional<std::string> value{};
                for (const auto &parent : found->second.inherits)
                    if (const auto inherited = self(self, parent, active); inherited.has_value()) value = inherited;
                if (found->second.binaryDirectory.has_value()) value = found->second.binaryDirectory;
                return value;
            };
            auto authored = resolveAuthored(resolveAuthored, preset, {});
            if (!authored.has_value()) return std::nullopt;
            const auto replace = [&](const std::string_view macro, const std::string &value) {
                std::size_t offset = 0;
                while ((offset = authored->find(macro, offset)) != std::string::npos)
                {
                    authored->replace(offset, macro.size(), value);
                    offset += value.size();
                }
            };
            replace("${sourceDir}", root.generic_string());
            replace("${sourceParentDir}", root.parent_path().generic_string());
            replace("${sourceDirName}", root.filename().generic_string());
            replace("${presetName}", preset);
            if (authored->contains("${") || authored->contains("$env{") || authored->contains("$penv{"))
                throw std::runtime_error("CMake preset '" + preset +
                                         "' uses a binaryDir macro that NGIN cannot resolve safely");
            const fs::path path{*authored};
            return path.is_absolute() ? path.lexically_normal() : (root / path).lexically_normal();
        }

        auto WriteFileApiQuery(const fs::path &buildDirectory) -> void
        {
            WriteText(buildDirectory / ".cmake/api/v1/query/client-ngin/query.json",
                      R"json({"requests":[{"kind":"codemodel","version":{"major":2}},{"kind":"cmakeFiles","version":{"major":1}},{"kind":"toolchains","version":{"major":1}}],"client":{"name":"NGIN CLI"}})json");
        }

        [[nodiscard]] auto LatestIndex(const fs::path &buildDirectory) -> std::optional<fs::path>
        {
            const auto reply = buildDirectory / ".cmake/api/v1/reply";
            std::error_code error{};
            if (!fs::is_directory(reply, error)) return std::nullopt;
            std::vector<fs::path> values{};
            for (const auto &entry : fs::directory_iterator(reply, error))
                if (entry.is_regular_file() && entry.path().filename().string().starts_with("index-") &&
                    entry.path().extension() == ".json")
                    values.push_back(entry.path());
            if (values.empty()) return std::nullopt;
            std::ranges::sort(values);
            return values.back();
        }

        [[nodiscard]] auto ReferenceFile(const JsonObject &index, const std::string_view kind)
            -> std::optional<std::string>
        {
            const auto objects = Array(index, "objects");
            if (!objects.has_value()) return std::nullopt;
            for (const auto value : *objects)
            {
                const auto object = value.TryObject();
                if (!object.has_value() || String(*object, "kind") != kind) continue;
                if (const auto file = String(*object, "jsonFile"); file.has_value()) return file;
            }
            return std::nullopt;
        }

        struct BacktraceGraph
        {
            std::vector<std::string> files{};
            struct Node
            {
                std::optional<std::int64_t> file{};
                std::optional<std::int64_t> line{};
                std::optional<std::int64_t> parent{};
            };
            std::vector<Node> nodes{};
        };

        [[nodiscard]] auto ReadBacktrace(const JsonObject &target) -> BacktraceGraph
        {
            BacktraceGraph result{};
            const auto graph = Object(target, "backtraceGraph");
            if (!graph.has_value()) return result;
            if (const auto files = Array(*graph, "files"); files.has_value())
                for (const auto value : *files)
                    if (const auto text = value.TryString(); text.has_value()) result.files.emplace_back(*text);
            ForObjects(*graph, "nodes", [&](const JsonObject &node) {
                result.nodes.push_back({.file = Integer(node, "file"),
                                        .line = Integer(node, "line"),
                                        .parent = Integer(node, "parent")});
            });
            return result;
        }

        auto ApplyBacktrace(const BacktraceGraph &graph, const std::optional<std::int64_t> index,
                            const fs::path &sourceRoot, std::optional<std::string> &path,
                            std::optional<std::int64_t> &line) -> void
        {
            if (!index.has_value() || *index < 0 || static_cast<std::size_t>(*index) >= graph.nodes.size()) return;
            auto current = *index;
            while (current >= 0 && static_cast<std::size_t>(current) < graph.nodes.size())
            {
                const auto &node = graph.nodes[static_cast<std::size_t>(current)];
                if (node.file.has_value() && *node.file >= 0 &&
                    static_cast<std::size_t>(*node.file) < graph.files.size())
                {
                    const fs::path authored{graph.files[static_cast<std::size_t>(*node.file)]};
                    path = (authored.is_absolute() ? authored : sourceRoot / authored).lexically_normal().generic_string();
                    line = node.line;
                    return;
                }
                if (!node.parent.has_value()) return;
                current = *node.parent;
            }
        }

        [[nodiscard]] auto IsStale(const fs::path &indexPath, const fs::path &replyDirectory,
                                   const JsonObject &index) -> bool
        {
            const auto file = ReferenceFile(index, "cmakeFiles");
            if (!file.has_value()) return false;
            auto document = ParseJson(replyDirectory / *file);
            const auto root = document.Root().TryObject();
            if (!root.has_value()) return false;
            const auto inputs = Array(*root, "inputs");
            if (!inputs.has_value()) return false;
            std::error_code error{};
            const auto replyTime = fs::last_write_time(indexPath, error);
            if (error) return false;
            const auto paths = Object(*root, "paths");
            const fs::path source = paths.has_value() ? String(*paths, "source").value_or("") : "";
            for (const auto value : *inputs)
            {
                const auto input = value.TryObject();
                if (!input.has_value()) continue;
                const auto authored = String(*input, "path");
                if (!authored.has_value()) continue;
                const fs::path path{*authored};
                const auto resolved = path.is_absolute() ? path : source / path;
                const auto changed = fs::last_write_time(resolved, error);
                if (!error && changed > replyTime) return true;
                error.clear();
            }
            return false;
        }

        auto ReadCTest(CMakeProjectSnapshot &snapshot) -> void
        {
            if (!snapshot.configured) return;
            std::vector<std::string> arguments{"--test-dir", snapshot.buildDirectory.string(),
                                               "--show-only=json-v1"};
            if (!snapshot.configuration.empty())
            {
                arguments.push_back("-C");
                arguments.push_back(snapshot.configuration);
            }
            const auto process = RunCaptured("ctest", arguments, snapshot.root);
            if (process.exitCode != 0) return;
            auto parsed = NGIN::Serialization::JSON::Parse(NGIN::Serialization::OwnedTextBuffer{process.output});
            if (!parsed.HasValue()) return;
            const auto root = parsed.Value().Root().TryObject();
            if (!root.has_value()) return;
            ForObjects(*root, "tests", [&](const JsonObject &test) {
                if (const auto name = String(test, "name"); name.has_value()) snapshot.tests.push_back({.name = *name});
            });
            std::ranges::sort(snapshot.tests, {}, &CMakeTest::name);
        }

        auto ReadFileApi(CMakeProjectSnapshot &snapshot) -> void
        {
            const auto indexPath = LatestIndex(snapshot.buildDirectory);
            if (!indexPath.has_value()) return;
            auto indexDocument = ParseJson(*indexPath);
            const auto index = indexDocument.Root().TryObject();
            if (!index.has_value()) return;
            const auto cmake = Object(*index, "cmake");
            const auto generator = cmake.has_value() ? Object(*cmake, "generator") : std::nullopt;
            snapshot.multiConfig = generator.has_value() && Boolean(*generator, "multiConfig");
            if (const auto toolchainsFile = ReferenceFile(*index, "toolchains"); toolchainsFile.has_value())
            {
                auto toolchainsDocument = ParseJson(indexPath->parent_path() / *toolchainsFile);
                if (const auto toolchains = toolchainsDocument.Root().TryObject(); toolchains.has_value())
                    ForObjects(*toolchains, "toolchains", [&](const JsonObject &toolchain) {
                        const auto compiler = Object(toolchain, "compiler");
                        snapshot.toolchains.push_back({
                            .language = String(toolchain, "language").value_or(""),
                            .compilerPath = compiler.has_value() ? String(*compiler, "path").value_or("") : "",
                            .compilerId = compiler.has_value() ? String(*compiler, "id").value_or("") : "",
                            .compilerVersion = compiler.has_value() ? String(*compiler, "version").value_or("") : "",
                            .target = compiler.has_value() ? String(*compiler, "target").value_or("") : ""});
                    });
            }
            const auto codemodelFile = ReferenceFile(*index, "codemodel");
            if (!codemodelFile.has_value()) return;
            const auto reply = indexPath->parent_path();
            auto codemodelDocument = ParseJson(reply / *codemodelFile);
            const auto codemodel = codemodelDocument.Root().TryObject();
            if (!codemodel.has_value()) return;
            const auto paths = Object(*codemodel, "paths");
            const fs::path sourceRoot = paths.has_value()
                                            ? fs::path{String(*paths, "source").value_or(snapshot.root.string())}
                                            : snapshot.root;
            const auto configurations = Array(*codemodel, "configurations");
            if (!configurations.has_value() || configurations->Empty()) return;
            std::optional<JsonObject> selected{};
            for (const auto value : *configurations)
            {
                const auto configuration = value.TryObject();
                if (!configuration.has_value()) continue;
                snapshot.configurations.push_back(String(*configuration, "name").value_or(""));
                if (!selected.has_value()) selected = *configuration;
                if (String(*configuration, "name").value_or("") == snapshot.configuration)
                {
                    selected = *configuration;
                    break;
                }
            }
            if (!selected.has_value()) return;
            if (snapshot.configuration.empty()) snapshot.configuration = String(*selected, "name").value_or("");
            ForObjects(*selected, "directories", [&](const JsonObject &directory) {
                if (const auto source = String(directory, "source"); source.has_value())
                {
                    const fs::path authored{*source};
                    snapshot.directories.push_back((authored.is_absolute() ? authored : sourceRoot / authored)
                                                       .lexically_normal().generic_string());
                }
            });
            ForObjects(*selected, "targets", [&](const JsonObject &reference) {
                const auto file = String(reference, "jsonFile");
                if (!file.has_value()) return;
                auto targetDocument = ParseJson(reply / *file);
                const auto target = targetDocument.Root().TryObject();
                if (!target.has_value()) return;
                CMakeTarget item{.id = String(*target, "id").value_or(String(reference, "id").value_or("")),
                                 .name = String(*target, "name").value_or(String(reference, "name").value_or("")),
                                 .type = String(*target, "type").value_or("UNKNOWN")};
                const auto backtrace = ReadBacktrace(*target);
                ApplyBacktrace(backtrace, Integer(*target, "backtrace"), sourceRoot,
                               item.declaration, item.declarationLine);
                if (const auto artifacts = Array(*target, "artifacts"); artifacts.has_value())
                    for (const auto value : *artifacts)
                    {
                        const auto artifact = value.TryObject();
                        if (!artifact.has_value()) continue;
                        const auto path = String(*artifact, "path");
                        if (!path.has_value()) continue;
                        const fs::path authored{*path};
                        item.artifacts.push_back((authored.is_absolute() ? authored : snapshot.buildDirectory / authored)
                                                     .lexically_normal().generic_string());
                    }
                ForObjects(*target, "dependencies", [&](const JsonObject &dependency) {
                    if (const auto id = String(dependency, "id"); id.has_value()) item.dependencies.push_back(*id);
                });
                ForObjects(*target, "compileGroups", [&](const JsonObject &group) {
                    CMakeCompileGroup compileGroup{
                        .id = std::to_string(item.compileGroups.size()),
                        .language = String(group, "language").value_or("")};
                    ForObjects(group, "compileCommandFragments", [&](const JsonObject &fragment) {
                        if (const auto value = String(fragment, "fragment"); value.has_value())
                            compileGroup.compileCommandFragments.push_back(*value);
                    });
                    ForObjects(group, "includes", [&](const JsonObject &include) {
                        if (const auto value = String(include, "path"); value.has_value())
                            compileGroup.includes.push_back(*value);
                    });
                    ForObjects(group, "defines", [&](const JsonObject &define) {
                        if (const auto value = String(define, "define"); value.has_value())
                            compileGroup.defines.push_back(*value);
                    });
                    item.compileGroups.push_back(std::move(compileGroup));
                });
                ForObjects(*target, "sources", [&](const JsonObject &source) {
                    const auto path = String(source, "path");
                    if (!path.has_value()) return;
                    const fs::path authored{*path};
                    CMakeSource sourceItem{
                        .path = (authored.is_absolute() ? authored : sourceRoot / authored).lexically_normal().generic_string()};
                    if (const auto group = Integer(source, "compileGroupIndex"); group.has_value())
                        sourceItem.compileGroup = std::to_string(*group);
                    ApplyBacktrace(backtrace, Integer(source, "backtrace"), sourceRoot,
                                   sourceItem.declaration, sourceItem.declarationLine);
                    item.sources.push_back(std::move(sourceItem));
                });
                std::ranges::sort(item.sources, {}, &CMakeSource::path);
                snapshot.targets.push_back(std::move(item));
            });
            std::ranges::sort(snapshot.targets, {}, &CMakeTarget::name);
            std::ranges::sort(snapshot.configurations);
            snapshot.configurations.erase(std::ranges::unique(snapshot.configurations).begin(),
                                          snapshot.configurations.end());
            std::ranges::sort(snapshot.directories);
            snapshot.directories.erase(std::ranges::unique(snapshot.directories).begin(), snapshot.directories.end());
            std::ranges::sort(snapshot.toolchains, {}, &CMakeToolchain::language);
            snapshot.configured = true;
            snapshot.stale = IsStale(*indexPath, reply, *index);
            ReadCTest(snapshot);
        }

        [[nodiscard]] auto PresetValue(const CMakePreset &preset) -> CanonicalValue
        {
            return CanonicalValue::Object{{"configurePreset", preset.configurePreset.value_or("")},
                                          {"description", preset.description},
                                          {"displayName", preset.displayName},
                                          {"name", preset.name}};
        }

        [[nodiscard]] auto PresetArray(const std::vector<CMakePreset> &presets) -> CanonicalValue::Array
        {
            CanonicalValue::Array result{};
            for (const auto &preset : presets) result.push_back(PresetValue(preset));
            return result;
        }

        [[nodiscard]] auto StringArray(const std::vector<std::string> &values) -> CanonicalValue::Array
        {
            CanonicalValue::Array result{};
            for (const auto &value : values) result.emplace_back(value);
            return result;
        }
    }

    auto IsCMakeProject(const fs::path &path) -> bool
    {
        std::error_code error{};
        return fs::is_directory(path, error) && fs::is_regular_file(path / "CMakeLists.txt", error);
    }

    auto InspectCMakeProject(const CMakeOperationRequest &request) -> CMakeProjectSnapshot
    {
        const auto root = fs::weakly_canonical(request.projectRoot);
        if (!IsCMakeProject(root)) throw std::runtime_error("not a CMake project: '" + root.generic_string() + "'");
        const auto presets = LoadPresets(root);
        const auto projectId = request.workspaceManifest.has_value()
                                   ? Sha256Fingerprint(fs::weakly_canonical(*request.workspaceManifest).generic_string() +
                                                       "|CMake|" + root.generic_string())
                                   : Sha256Fingerprint("CMake|" + root.generic_string());
        CMakeProjectSnapshot result{.id = projectId,
                                    .root = root,
                                    .name = root.filename().string(),
                                    .configurePreset = request.configurePreset.value_or(""),
                                    .configuration = request.configuration.value_or(""),
                                    .configurePresets = PublicPresets(presets.configure),
                                    .buildPresets = PublicPresets(presets.build),
                                    .testPresets = PublicPresets(presets.test),
                                    .diagnostics = presets.diagnostics};
        if (request.configurePreset.has_value())
        {
            const auto binary = ResolveBinaryDirectory(presets, root, *request.configurePreset);
            if (!binary.has_value())
            {
                result.diagnostics.push_back("configure preset '" + *request.configurePreset +
                                             "' does not define a resolvable binaryDir");
                return result;
            }
            result.buildDirectory = *binary;
            ReadFileApi(result);
        }
        return result;
    }

    auto SerializeCMakeProjectSnapshot(const CMakeProjectSnapshot &snapshot) -> std::string
    {
        CanonicalValue::Array targets{};
        for (const auto &target : snapshot.targets)
        {
            CanonicalValue::Array compileGroups{};
            for (const auto &group : target.compileGroups)
                compileGroups.emplace_back(CanonicalValue::Object{
                    {"compileCommandFragments", StringArray(group.compileCommandFragments)},
                    {"defines", StringArray(group.defines)},
                    {"id", group.id},
                    {"includes", StringArray(group.includes)},
                    {"language", group.language}});
            CanonicalValue::Array sources{};
            for (const auto &source : target.sources)
                sources.emplace_back(CanonicalValue::Object{{"compileGroup", source.compileGroup.value_or("")},
                                                            {"declaration", source.declaration.value_or("")},
                                                            {"declarationLine", source.declarationLine.value_or(0)},
                                                            {"path", source.path}});
            targets.emplace_back(CanonicalValue::Object{{"artifacts", StringArray(target.artifacts)},
                                                        {"compileGroups", std::move(compileGroups)},
                                                        {"declaration", target.declaration.value_or("")},
                                                        {"declarationLine", target.declarationLine.value_or(0)},
                                                        {"dependencies", StringArray(target.dependencies)},
                                                        {"id", target.id},
                                                        {"name", target.name},
                                                        {"sources", std::move(sources)},
                                                        {"type", target.type}});
        }
        CanonicalValue::Array tests{};
        for (const auto &test : snapshot.tests)
            tests.emplace_back(CanonicalValue::Object{{"name", test.name}});
        CanonicalValue::Array toolchains{};
        for (const auto &toolchain : snapshot.toolchains)
            toolchains.emplace_back(CanonicalValue::Object{{"compilerId", toolchain.compilerId},
                                                           {"compilerPath", toolchain.compilerPath},
                                                           {"compilerVersion", toolchain.compilerVersion},
                                                           {"language", toolchain.language},
                                                           {"target", toolchain.target}});
        return SerializeCanonical(CanonicalValue::Object{
            {"capabilities", StringArray({"Inspect", "Configure", "Build", "BuildTarget", "Test",
                                           "SourceOwnership", "OpenDeclaration", "Artifacts"})},
            {"cmake", CanonicalValue::Object{{"buildDirectory", snapshot.buildDirectory.generic_string()},
                                             {"buildPresets", PresetArray(snapshot.buildPresets)},
                                             {"configurations", StringArray(snapshot.configurations)},
                                             {"configuration", snapshot.configuration},
                                             {"configurePreset", snapshot.configurePreset},
                                             {"configurePresets", PresetArray(snapshot.configurePresets)},
                                             {"configured", snapshot.configured},
                                             {"directories", StringArray(snapshot.directories)},
                                             {"multiConfig", snapshot.multiConfig},
                                             {"stale", snapshot.stale},
                                             {"testPresets", PresetArray(snapshot.testPresets)},
                                             {"toolchains", std::move(toolchains)}}},
            {"diagnostics", StringArray(snapshot.diagnostics)},
            {"kind", "NGIN.EditorCMakeProjectSnapshot"},
            {"project", CanonicalValue::Object{{"id", snapshot.id},
                                                {"name", snapshot.name},
                                                {"projectSystem", "CMake"},
                                                {"root", snapshot.root.generic_string()}}},
            {"state", snapshot.configured ? "ready" : "degraded"},
            {"targets", std::move(targets)},
            {"tests", std::move(tests)},
            {"version", std::int64_t{2}}});
    }

    auto ConfigureCMakeProject(const CMakeOperationRequest &request) -> int
    {
        if (!request.configurePreset.has_value())
            throw std::runtime_error("CMake configure requires --configure-preset");
        const auto root = fs::weakly_canonical(request.projectRoot);
        const auto presets = LoadPresets(root);
        const auto binary = ResolveBinaryDirectory(presets, root, *request.configurePreset);
        if (!binary.has_value())
            throw std::runtime_error("CMake configure preset '" + *request.configurePreset +
                                     "' has no resolvable binaryDir");
        WriteFileApiQuery(*binary);
        const auto result = Run("cmake", {"--preset", *request.configurePreset}, root);
        if (result == 0) std::cout << "Configured " << root.filename().string() << " in " << *binary << '\n';
        return result;
    }

    auto BuildCMakeProject(const CMakeOperationRequest &request) -> int
    {
        const auto root = fs::weakly_canonical(request.projectRoot);
        if (request.operationPreset.has_value())
            return Run("cmake", {"--build", "--preset", *request.operationPreset}, root);
        if (!request.configurePreset.has_value())
            throw std::runtime_error("CMake target build requires --configure-preset or a build --preset");
        const auto snapshot = InspectCMakeProject(request);
        if (!snapshot.configured) throw std::runtime_error("CMake project is not configured");
        std::vector<std::string> arguments{"--build", snapshot.buildDirectory.string()};
        if (request.target.has_value())
        {
            const auto found = std::ranges::find_if(snapshot.targets, [&](const CMakeTarget &candidate) {
                return candidate.id == *request.target || candidate.name == *request.target;
            });
            if (found == snapshot.targets.end())
                throw std::runtime_error("unknown CMake target id or name '" + *request.target + "'");
            arguments.push_back("--target");
            arguments.push_back(found->name);
        }
        if (request.configuration.has_value())
        {
            arguments.push_back("--config");
            arguments.push_back(*request.configuration);
        }
        return Run("cmake", arguments, root);
    }

    auto TestCMakeProject(const CMakeOperationRequest &request) -> int
    {
        const auto root = fs::weakly_canonical(request.projectRoot);
        if (request.operationPreset.has_value())
            return Run("ctest", {"--preset", *request.operationPreset, "--output-on-failure"}, root);
        if (!request.configurePreset.has_value())
            throw std::runtime_error("CTest requires --configure-preset or a test --preset");
        const auto snapshot = InspectCMakeProject(request);
        if (!snapshot.configured) throw std::runtime_error("CMake project is not configured");
        std::vector<std::string> arguments{"--test-dir", snapshot.buildDirectory.string(), "--output-on-failure"};
        if (request.configuration.has_value())
        {
            arguments.push_back("-C");
            arguments.push_back(*request.configuration);
        }
        if (!request.tests.empty())
        {
            std::string expression{"^("};
            for (std::size_t index = 0; index < request.tests.size(); ++index)
            {
                if (index != 0) expression += '|';
                expression += std::regex_replace(request.tests[index], std::regex{R"([.^$|()\[\]{}*+?\\])"}, R"(\$&)");
            }
            expression += ")$";
            arguments.push_back("-R");
            arguments.push_back(expression);
        }
        return Run("ctest", arguments, root);
    }
}
