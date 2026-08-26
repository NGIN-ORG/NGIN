#include "MetaGenPreprocessor.hpp"

#include "MetaGenCommon.hpp"

#include <NGIN/Serialization/Core/SourceBuffer.hpp>
#include <NGIN/Serialization/JSON/JsonParser.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>

namespace NGIN::Reflection::MetaGen {
namespace {
using JsonArray = NGIN::Serialization::JSON::ArrayView;
using JsonDocument = NGIN::Serialization::JSON::Document;
using JsonObject = NGIN::Serialization::JSON::ObjectView;
using JsonParseResult =
    NGIN::Utilities::Expected<JsonDocument,
                              NGIN::Serialization::ParseDiagnostic>;
using JsonValue = NGIN::Serialization::JSON::ValueView;

struct CompileCommand {
  fs::path directory{};
  fs::path file{};
  std::vector<std::string> arguments{};
};

[[nodiscard]] auto ReadText(const fs::path &path)
    -> std::optional<std::string> {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  std::ostringstream text{};
  text << input.rdbuf();
  return text.str();
}

[[nodiscard]] auto WriteIfChanged(const fs::path &path,
                                  const std::string_view content) -> bool {
  if (const std::optional<std::string> existing = ReadText(path);
      existing && *existing == content) {
    return true;
  }
  std::error_code error{};
  fs::create_directories(path.parent_path(), error);
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return false;
  }
  output << content;
  return static_cast<bool>(output);
}

[[nodiscard]] auto Lower(std::string value) -> std::string {
  std::ranges::transform(value, value.begin(), [](const unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

[[nodiscard]] auto NormalizedPathKey(const fs::path &path,
                                     const fs::path &base = {}) -> std::string {
  fs::path value = path;
  if (value.is_relative() && !base.empty()) {
    value = base / value;
  }
  std::error_code error{};
  const fs::path canonical = fs::weakly_canonical(value, error);
  std::string result =
      (error ? value.lexically_normal() : canonical).generic_string();
#ifdef _WIN32
  result = Lower(std::move(result));
#endif
  return result;
}

[[nodiscard]] auto SplitCommand(const std::string_view command)
    -> std::vector<std::string> {
  std::vector<std::string> result{};
  std::string current{};
  bool singleQuoted = false;
  bool doubleQuoted = false;
  for (std::size_t index = 0; index < command.size(); ++index) {
    const char ch = command[index];
    if (ch == '\'' && !doubleQuoted) {
      singleQuoted = !singleQuoted;
      continue;
    }
    if (ch == '"' && !singleQuoted) {
      doubleQuoted = !doubleQuoted;
      continue;
    }
    if (ch == '\\' && !singleQuoted && index + 1 < command.size() &&
        (command[index + 1] == '"' || command[index + 1] == '\'' ||
         std::isspace(static_cast<unsigned char>(command[index + 1])) != 0)) {
      current.push_back(command[++index]);
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(ch)) != 0 && !singleQuoted &&
        !doubleQuoted) {
      if (!current.empty()) {
        result.push_back(std::move(current));
        current.clear();
      }
      continue;
    }
    current.push_back(ch);
  }
  if (!current.empty()) {
    result.push_back(std::move(current));
  }
  return result;
}

[[nodiscard]] auto ParseCompileCommands(const fs::path &path,
                                        std::vector<std::string> &diagnostics)
    -> std::vector<CompileCommand> {
  const std::optional<std::string> text = ReadText(path);
  if (!text) {
    diagnostics.push_back("failed to open compilation database '" +
                          path.string() + "'");
    return {};
  }
  JsonParseResult parsed = NGIN::Serialization::JSON::Parser::Parse(
      NGIN::Serialization::OwnedTextBuffer{*text});
  if (!parsed) {
    diagnostics.push_back("failed to parse compilation database '" +
                          path.string() + "'");
    return {};
  }
  const std::optional<JsonArray> array = parsed.Value().Root().TryArray();
  if (!array) {
    diagnostics.push_back("compilation database root must be an array");
    return {};
  }
  std::vector<CompileCommand> result{};
  for (const JsonValue value : *array) {
    const std::optional<JsonObject> object = value.TryObject();
    if (!object) {
      continue;
    }
    const std::optional<JsonValue> directory = object->Find("directory");
    const std::optional<JsonValue> file = object->Find("file");
    if (!directory || !file || !directory->TryString() || !file->TryString()) {
      continue;
    }
    CompileCommand entry{};
    entry.directory = std::string(*directory->TryString());
    entry.file = std::string(*file->TryString());
    if (const std::optional<JsonValue> arguments = object->Find("arguments");
        arguments) {
      const std::optional<JsonArray> argumentArray = arguments->TryArray();
      if (argumentArray) {
        for (const JsonValue argument : *argumentArray) {
          if (const std::optional<std::string_view> string =
                  argument.TryString()) {
            entry.arguments.emplace_back(*string);
          }
        }
      }
    }
    if (entry.arguments.empty()) {
      if (const std::optional<JsonValue> command = object->Find("command");
          command && command->TryString()) {
        entry.arguments = SplitCommand(*command->TryString());
      }
    }
    if (!entry.arguments.empty()) {
      result.push_back(std::move(entry));
    }
  }
  return result;
}

[[nodiscard]] auto SelectCommand(const MetaGenContext &context,
                                 const std::vector<CompileCommand> &commands)
    -> const CompileCommand * {
  for (const fs::path &output : context.outputs) {
    const std::string outputKey = NormalizedPathKey(output);
    const auto found =
        std::ranges::find_if(commands, [&](const CompileCommand &command) {
          return NormalizedPathKey(command.file, command.directory) ==
                 outputKey;
        });
    if (found != commands.end()) {
      return &*found;
    }
  }
  return nullptr;
}

[[nodiscard]] auto IsMsvcStyle(const std::string_view compiler,
                               const std::vector<std::string> &arguments)
    -> bool {
  const std::string name = Lower(fs::path(compiler).filename().string());
  return name == "cl" || name == "cl.exe" || name == "clang-cl" ||
         name == "clang-cl.exe" ||
         std::ranges::find(arguments, "/c") != arguments.end();
}

[[nodiscard]] auto ResponseQuote(const std::string_view argument)
    -> std::string {
  std::string result{"\""};
  std::size_t backslashes = 0;
  for (const char ch : argument) {
    if (ch == '\\') {
      ++backslashes;
      continue;
    }
    if (ch == '"') {
      result.append(backslashes * 2 + 1, '\\');
      result.push_back('"');
      backslashes = 0;
      continue;
    }
    result.append(backslashes, '\\');
    backslashes = 0;
    result.push_back(ch);
  }
  result.append(backslashes * 2, '\\');
  result.push_back('"');
  return result;
}

[[nodiscard]] auto ShellQuote(const std::string_view argument) -> std::string {
#ifdef _WIN32
  return ResponseQuote(argument);
#else
  std::string result{"'"};
  for (const char ch : argument) {
    if (ch == '\'') {
      result += "'\\''";
    } else {
      result.push_back(ch);
    }
  }
  result.push_back('\'');
  return result;
#endif
}

[[nodiscard]] auto FilterArguments(const CompileCommand &command,
                                   const bool msvcStyle)
    -> std::vector<std::string> {
  std::vector<std::string> result{};
  const std::string sourceKey =
      NormalizedPathKey(command.file, command.directory);
  for (std::size_t index = 1; index < command.arguments.size(); ++index) {
    const std::string &argument = command.arguments[index];
    const std::string lower = Lower(argument);
    if (NormalizedPathKey(argument, command.directory) == sourceKey ||
        argument == "-c" || lower == "/c" || argument == "-MD" ||
        argument == "-MMD" || argument == "-MP" || argument == "-MG" ||
        argument == "-Winvalid-pch" || lower == "/showincludes") {
      continue;
    }
    if (argument == "-o" || argument == "-MF" || argument == "-MT" ||
        argument == "-MQ" || argument == "-MJ" ||
        argument == "--serialize-diagnostics") {
      ++index;
      continue;
    }
    if ((msvcStyle && (lower.starts_with("/fo") || lower.starts_with("/fp") ||
                       lower.starts_with("/yu") || lower.starts_with("/yc"))) ||
        (!msvcStyle && (argument.starts_with("-o") && argument.size() > 2))) {
      continue;
    }
    result.push_back(argument);
  }
  return result;
}

[[nodiscard]] auto BuildScanTranslationUnit(const MetaGenContext &context)
    -> std::string {
  std::vector<fs::path> headers{};
  for (const fs::path &source : context.sourceFiles) {
    const std::string extension = Lower(source.extension().string());
    if (extension == ".h" || extension == ".hh" || extension == ".hpp" ||
        extension == ".hxx") {
      headers.push_back(source.lexically_normal());
    }
  }
  std::ranges::sort(headers, {},
                    [](const fs::path &path) { return path.generic_string(); });
  headers.erase(std::unique(headers.begin(), headers.end()), headers.end());
  std::ostringstream out{};
  out << "#define NGIN_METAGEN_SCAN 1\n";
  for (const fs::path &header : headers) {
    out << "#include \"" << EscapeCppString(header.generic_string()) << "\"\n";
  }
  return out.str();
}
} // namespace

auto PreprocessHeaders(const MetaGenContext &context) -> PreprocessResult {
  PreprocessResult result{};
  if (context.compilationDatabaseDir.empty()) {
    result.diagnostics.push_back(
        "generator context does not declare CompilationDatabaseDir; "
        "MetaGen requires the target compiler command");
    return result;
  }
  const fs::path databasePath =
      context.compilationDatabaseDir / "compile_commands.json";
  const std::vector<CompileCommand> commands =
      ParseCompileCommands(databasePath, result.diagnostics);
  if (!result.diagnostics.empty()) {
    return result;
  }
  const CompileCommand *selected = SelectCommand(context, commands);
  if (selected == nullptr) {
    result.diagnostics.push_back(
        "compilation database has no command for generated reflection output; "
        "reconfigure the target with CMAKE_EXPORT_COMPILE_COMMANDS enabled");
    return result;
  }

  std::size_t compilerIndex = 0;
  const std::string launcher =
      Lower(fs::path(selected->arguments.front()).filename().string());
  if ((launcher == "ccache" || launcher == "sccache") &&
      selected->arguments.size() > 1) {
    compilerIndex = 1;
  }
  result.compiler = selected->arguments[compilerIndex];
  std::vector<std::string> compilerArguments = selected->arguments;
  if (compilerIndex != 0) {
    compilerArguments.erase(compilerArguments.begin());
  }
  CompileCommand direct = *selected;
  direct.arguments = std::move(compilerArguments);
  const bool msvcStyle = IsMsvcStyle(result.compiler, direct.arguments);
  std::vector<std::string> arguments = FilterArguments(direct, msvcStyle);

  const fs::path outputRoot =
      context.outputs.front().parent_path() / ".metagen";
  const fs::path scanPath = outputRoot / "scan.cpp";
  const fs::path preprocessedPath = outputRoot / "scan.ii";
  const fs::path responsePath = outputRoot / "preprocess.rsp";
  const std::string scanUnit = BuildScanTranslationUnit(context);
  if (!WriteIfChanged(scanPath, scanUnit)) {
    result.diagnostics.push_back(
        "failed to write MetaGen scan translation unit '" + scanPath.string() +
        "'");
    return result;
  }

  if (msvcStyle) {
    arguments.push_back("/DNGIN_METAGEN_SCAN=1");
    arguments.push_back("/P");
    arguments.push_back("/Fi" + preprocessedPath.string());
    arguments.push_back(scanPath.string());
  } else {
    arguments.push_back("-DNGIN_METAGEN_SCAN=1");
    arguments.push_back("-E");
    arguments.push_back(scanPath.string());
    arguments.push_back("-o");
    arguments.push_back(preprocessedPath.string());
  }

  std::ostringstream response{};
  for (const std::string &argument : arguments) {
    response << ResponseQuote(argument) << '\n';
  }
  if (!WriteIfChanged(responsePath, response.str())) {
    result.diagnostics.push_back(
        "failed to write MetaGen compiler response file '" +
        responsePath.string() + "'");
    return result;
  }

  const std::string command =
      ShellQuote(result.compiler) + " @" + ShellQuote(responsePath.string());
  const fs::path previousDirectory = fs::current_path();
  std::error_code directoryError{};
  fs::current_path(selected->directory, directoryError);
  if (directoryError) {
    result.diagnostics.push_back(
        "failed to enter compiler working directory '" +
        selected->directory.string() + "'");
    return result;
  }
  const int status = std::system(command.c_str());
  fs::current_path(previousDirectory, directoryError);
  if (status != 0) {
    result.diagnostics.push_back(
        "target compiler preprocessing failed with exit status " +
        std::to_string(status) + " while scanning '" + scanPath.string() + "'");
    return result;
  }
  const std::optional<std::string> preprocessed = ReadText(preprocessedPath);
  if (!preprocessed) {
    result.diagnostics.push_back(
        "target compiler did not produce preprocessed output '" +
        preprocessedPath.string() + "'");
    return result;
  }
  result.source = *preprocessed;
  result.generatedFiles = {scanPath, preprocessedPath, responsePath};
  return result;
}
} // namespace NGIN::Reflection::MetaGen
