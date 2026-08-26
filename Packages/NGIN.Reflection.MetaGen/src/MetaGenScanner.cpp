#include "MetaGenScanner.hpp"

#include "MetaGenCommon.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <system_error>

namespace NGIN::Reflection::MetaGen {
namespace {
struct Token {
  std::string text{};
  std::string decoded{};
  SourceLocation source{};
  bool identifier{false};
  bool stringLiteral{false};
  bool number{false};
};

struct ParsedAnnotation {
  std::string kind{};
  std::map<std::string, std::string, std::less<>> options{};
  AttributeSet attributes{};
  SourceSpan source{};
};

struct AnnotationParse {
  ParsedAnnotation annotation{};
  std::size_t next{};
};

struct Scanner {
  const MetaGenContext &context;
  std::vector<Token> tokens{};
  ReflectionModel model{};
  std::vector<std::string> diagnostics{};
  std::set<std::string, std::less<>> ownedFiles{};
  std::set<std::string, std::less<>> reflectedNames{};
};

[[nodiscard]] auto Trim(std::string_view value) -> std::string_view {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.remove_prefix(1);
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.remove_suffix(1);
  }
  return value;
}

[[nodiscard]] auto Lower(std::string value) -> std::string {
  std::ranges::transform(value, value.begin(), [](const unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

[[nodiscard]] auto NormalizedPathKey(const fs::path &path) -> std::string {
  std::error_code error{};
  const fs::path canonical = fs::weakly_canonical(path, error);
  std::string result =
      (error ? path.lexically_normal() : canonical).generic_string();
#ifdef _WIN32
  result = Lower(std::move(result));
#endif
  return result;
}

[[nodiscard]] auto IsIdentifierStart(const unsigned char ch) -> bool {
  return std::isalpha(ch) != 0 || ch == '_' || ch >= 0x80;
}

[[nodiscard]] auto IsIdentifierContinue(const unsigned char ch) -> bool {
  return std::isalnum(ch) != 0 || ch == '_' || ch >= 0x80;
}

[[nodiscard]] auto DecodeStringLiteral(const std::string_view spelling)
    -> std::string {
  if (spelling.size() < 2) {
    return std::string(spelling);
  }
  std::string result{};
  result.reserve(spelling.size() - 2);
  for (std::size_t index = 1; index + 1 < spelling.size(); ++index) {
    char ch = spelling[index];
    if (ch != '\\' || index + 2 >= spelling.size()) {
      result.push_back(ch);
      continue;
    }
    ch = spelling[++index];
    switch (ch) {
    case 'n':
      result.push_back('\n');
      break;
    case 'r':
      result.push_back('\r');
      break;
    case 't':
      result.push_back('\t');
      break;
    case '\\':
      result.push_back('\\');
      break;
    case '"':
      result.push_back('"');
      break;
    default:
      result.push_back(ch);
      break;
    }
  }
  return result;
}

[[nodiscard]] auto ParseLineMarker(const std::string_view line,
                                   SourceLocation &source) -> bool {
  std::string_view value = Trim(line);
  if (value.empty() || value.front() != '#') {
    return false;
  }
  value.remove_prefix(1);
  value = Trim(value);
  if (value.starts_with("line")) {
    value.remove_prefix(4);
    value = Trim(value);
  }
  std::size_t lineNumber{};
  const char *begin = value.data();
  const char *end = value.data() + value.size();
  const std::from_chars_result parsed = std::from_chars(begin, end, lineNumber);
  if (parsed.ec != std::errc{}) {
    return false;
  }
  value.remove_prefix(static_cast<std::size_t>(parsed.ptr - begin));
  value = Trim(value);
  if (value.size() < 2 || value.front() != '"') {
    return false;
  }
  const std::size_t closing = value.find('"', 1);
  if (closing == std::string_view::npos) {
    return false;
  }
  const std::string fileSpelling{value.substr(0, closing + 1)};
  source.file = fs::path(DecodeStringLiteral(fileSpelling)).lexically_normal();
  source.line = lineNumber;
  source.column = 1;
  return true;
}

auto TokenizeLine(const std::string_view line, const SourceLocation &lineSource,
                  std::vector<Token> &tokens) -> void {
  static constexpr std::string_view compoundPunctuation[] = {
      "<=>", "...", "[[", "]]", "::", "&&", "||", "->", "++",
      "--",  "==",  "!=", "<=", ">=", "<<", ">>", "+=", "-=",
      "*=",  "/=",  "%=", "&=", "|=", "^=", "##"};
  std::size_t index = 0;
  while (index < line.size()) {
    const unsigned char ch = static_cast<unsigned char>(line[index]);
    if (std::isspace(ch) != 0) {
      ++index;
      continue;
    }

    Token token{};
    token.source = lineSource;
    token.source.column = index + 1;
    const std::size_t start = index;
    if (IsIdentifierStart(ch)) {
      ++index;
      while (index < line.size() &&
             IsIdentifierContinue(static_cast<unsigned char>(line[index]))) {
        ++index;
      }
      token.identifier = true;
    } else if (std::isdigit(ch) != 0) {
      ++index;
      while (index < line.size()) {
        const unsigned char current = static_cast<unsigned char>(line[index]);
        if (std::isalnum(current) == 0 && line[index] != '.' &&
            line[index] != '_' && line[index] != '\'') {
          break;
        }
        ++index;
      }
      token.number = true;
    } else if (line[index] == '"' || line[index] == '\'') {
      const char quote = line[index++];
      while (index < line.size()) {
        if (line[index] == '\\' && index + 1 < line.size()) {
          index += 2;
          continue;
        }
        if (line[index++] == quote) {
          break;
        }
      }
      token.stringLiteral = quote == '"';
    } else {
      bool compound = false;
      for (const std::string_view punctuation : compoundPunctuation) {
        if (line.substr(index).starts_with(punctuation)) {
          index += punctuation.size();
          compound = true;
          break;
        }
      }
      if (!compound) {
        ++index;
      }
    }
    token.text = std::string(line.substr(start, index - start));
    if (token.stringLiteral) {
      token.decoded = DecodeStringLiteral(token.text);
    }
    tokens.push_back(std::move(token));
  }
}

[[nodiscard]] auto Tokenize(const std::string_view source)
    -> std::vector<Token> {
  std::vector<Token> tokens{};
  SourceLocation current{};
  current.file = "<preprocessed>";
  std::size_t offset = 0;
  while (offset <= source.size()) {
    const std::size_t newline = source.find('\n', offset);
    const std::size_t end =
        newline == std::string_view::npos ? source.size() : newline;
    const std::string_view line = source.substr(offset, end - offset);
    if (!ParseLineMarker(line, current)) {
      TokenizeLine(line, current, tokens);
      ++current.line;
    }
    if (newline == std::string_view::npos) {
      break;
    }
    offset = newline + 1;
  }
  return tokens;
}

[[nodiscard]] auto FindMatching(const std::vector<Token> &tokens,
                                const std::size_t open,
                                const std::string_view openToken,
                                const std::string_view closeToken,
                                const std::size_t limit) -> std::size_t {
  std::size_t depth = 0;
  for (std::size_t index = open; index < limit; ++index) {
    if (tokens[index].text == openToken) {
      ++depth;
    } else if (tokens[index].text == closeToken && --depth == 0) {
      return index;
    }
  }
  return limit;
}

[[nodiscard]] auto SplitTopLevel(const std::string_view input,
                                 const char delimiter)
    -> std::vector<std::string_view> {
  std::vector<std::string_view> result{};
  std::size_t start = 0;
  int parentheses = 0;
  int brackets = 0;
  bool inString = false;
  char quote = 0;
  bool escaped = false;
  for (std::size_t index = 0; index < input.size(); ++index) {
    const char ch = input[index];
    if (inString) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == quote) {
        inString = false;
      }
      continue;
    }
    if (ch == '"' || ch == '\'') {
      inString = true;
      quote = ch;
    } else if (ch == '(') {
      ++parentheses;
    } else if (ch == ')') {
      --parentheses;
    } else if (ch == '[') {
      ++brackets;
    } else if (ch == ']') {
      --brackets;
    } else if ((ch == delimiter || (delimiter == ',' && ch == ';')) &&
               parentheses == 0 && brackets == 0) {
      result.push_back(input.substr(start, index - start));
      start = index + 1;
    }
  }
  result.push_back(input.substr(start));
  return result;
}

[[nodiscard]] auto FindTopLevelEquals(const std::string_view input)
    -> std::size_t {
  int depth = 0;
  bool inString = false;
  char quote = 0;
  bool escaped = false;
  for (std::size_t index = 0; index < input.size(); ++index) {
    const char ch = input[index];
    if (inString) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == quote) {
        inString = false;
      }
      continue;
    }
    if (ch == '"' || ch == '\'') {
      inString = true;
      quote = ch;
    } else if (ch == '(' || ch == '[' || ch == '{') {
      ++depth;
    } else if (ch == ')' || ch == ']' || ch == '}') {
      --depth;
    } else if (ch == '=' && depth == 0) {
      return index;
    }
  }
  return std::string_view::npos;
}

[[nodiscard]] auto Unquote(const std::string_view value) -> std::string {
  const std::string_view trimmed = Trim(value);
  if (trimmed.size() >= 2 &&
      ((trimmed.front() == '"' && trimmed.back() == '"') ||
       (trimmed.front() == '\'' && trimmed.back() == '\''))) {
    return DecodeStringLiteral(trimmed);
  }
  return std::string(trimmed);
}

[[nodiscard]] auto MetadataValueFrom(const std::string_view spelling)
    -> MetadataValue {
  const std::string_view value = Trim(spelling);
  if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                            (value.front() == '\'' && value.back() == '\''))) {
    return MetadataValue{.kind = MetadataValueKind::String,
                         .text = Unquote(value)};
  }
  const std::string lower = Lower(std::string(value));
  if (lower == "true" || lower == "false") {
    return MetadataValue{.kind = MetadataValueKind::Boolean, .text = lower};
  }
  if (!value.empty() &&
      (std::isdigit(static_cast<unsigned char>(value.front())) != 0 ||
       ((value.front() == '-' || value.front() == '+') && value.size() > 1))) {
    const bool floating = value.find_first_of(".eE") != std::string_view::npos;
    return MetadataValue{.kind = floating ? MetadataValueKind::FloatingPoint
                                          : MetadataValueKind::Integer,
                         .text = std::string(value)};
  }
  return MetadataValue{.kind = MetadataValueKind::Identifier,
                       .text = std::string(value)};
}

auto ParsePayload(const std::string_view payload, ParsedAnnotation &annotation,
                  const bool attributesOnly = false) -> void {
  for (std::string_view item : SplitTopLevel(payload, ',')) {
    item = Trim(item);
    if (item.empty()) {
      continue;
    }
    const std::size_t equals = FindTopLevelEquals(item);
    const std::string_view keyView =
        Trim(equals == std::string_view::npos ? item : item.substr(0, equals));
    const std::string_view valueView = equals == std::string_view::npos
                                           ? std::string_view{"true"}
                                           : Trim(item.substr(equals + 1));
    const std::string key{keyView};
    const std::string lowerKey = Lower(key);
    if (!attributesOnly && lowerKey == "attributes" && valueView.size() >= 2 &&
        valueView.front() == '(' && valueView.back() == ')') {
      ParsePayload(valueView.substr(1, valueView.size() - 2), annotation, true);
      continue;
    }
    if (!attributesOnly &&
        (lowerKey == "name" || lowerKey == "selector" ||
         lowerKey == "injectable" || lowerKey == "optional" ||
         lowerKey == "enabled" || lowerKey == "manual")) {
      annotation.options[lowerKey] = Unquote(valueView);
      continue;
    }
    annotation.attributes[key] = MetadataValueFrom(valueView);
  }
}

[[nodiscard]] auto ParseAnnotationAt(const std::vector<Token> &tokens,
                                     const std::size_t index,
                                     const std::size_t limit)
    -> std::optional<AnnotationParse> {
  if (index + 4 >= limit || tokens[index].text != "[[" ||
      tokens[index + 1].text != "ngin" || tokens[index + 2].text != "::" ||
      !tokens[index + 3].identifier) {
    return std::nullopt;
  }
  ParsedAnnotation annotation{};
  annotation.kind = Lower(tokens[index + 3].text);
  annotation.source.begin = tokens[index].source;
  std::size_t cursor = index + 4;
  if (cursor < limit && tokens[cursor].text == "(") {
    const std::size_t close = FindMatching(tokens, cursor, "(", ")", limit);
    if (close == limit) {
      return std::nullopt;
    }
    if (cursor + 1 < close && tokens[cursor + 1].stringLiteral) {
      ParsePayload(tokens[cursor + 1].decoded, annotation);
    }
    cursor = close + 1;
  }
  if (cursor >= limit || tokens[cursor].text != "]]") {
    return std::nullopt;
  }
  annotation.source.end = tokens[cursor].source;
  return AnnotationParse{.annotation = std::move(annotation),
                         .next = cursor + 1};
}

[[nodiscard]] auto CollectAnnotations(const std::vector<Token> &tokens,
                                      const std::size_t begin,
                                      const std::size_t end)
    -> std::vector<ParsedAnnotation> {
  std::vector<ParsedAnnotation> result{};
  for (std::size_t index = begin; index < end;) {
    if (const std::optional<AnnotationParse> annotation =
            ParseAnnotationAt(tokens, index, end)) {
      result.push_back(annotation->annotation);
      index = annotation->next;
    } else {
      ++index;
    }
  }
  return result;
}

[[nodiscard]] auto WithoutAnnotations(const std::vector<Token> &tokens,
                                      const std::size_t begin,
                                      const std::size_t end,
                                      const bool preserveDependencies = false)
    -> std::vector<Token> {
  std::vector<Token> result{};
  for (std::size_t index = begin; index < end;) {
    if (const std::optional<AnnotationParse> annotation =
            ParseAnnotationAt(tokens, index, end)) {
      if (preserveDependencies && annotation->annotation.kind == "dependency") {
        result.insert(
            result.end(), tokens.begin() + static_cast<std::ptrdiff_t>(index),
            tokens.begin() + static_cast<std::ptrdiff_t>(annotation->next));
      }
      index = annotation->next;
    } else {
      result.push_back(tokens[index++]);
    }
  }
  return result;
}

[[nodiscard]] auto
FindAnnotation(const std::vector<ParsedAnnotation> &annotations,
               const std::string_view kind) -> const ParsedAnnotation * {
  const auto found =
      std::ranges::find(annotations, kind, &ParsedAnnotation::kind);
  return found == annotations.end() ? nullptr : &*found;
}

[[nodiscard]] auto OptionOr(const ParsedAnnotation *annotation,
                            const std::string_view key,
                            std::string fallback = {}) -> std::string {
  if (annotation == nullptr) {
    return fallback;
  }
  const auto found = annotation->options.find(std::string(key));
  return found == annotation->options.end() || found->second.empty()
             ? fallback
             : found->second;
}

[[nodiscard]] auto OptionEnabled(const ParsedAnnotation *annotation,
                                 const std::string_view key) -> bool {
  if (annotation == nullptr) {
    return false;
  }
  const auto found = annotation->options.find(std::string(key));
  if (found == annotation->options.end()) {
    return false;
  }
  const std::string value = Lower(found->second);
  return value != "false" && value != "0" && value != "off";
}

[[nodiscard]] auto IsWord(const Token &token) -> bool {
  return token.identifier || token.number || token.stringLiteral ||
         token.text.starts_with('\'');
}

[[nodiscard]] auto JoinTokens(const std::vector<Token> &tokens,
                              const std::size_t begin, const std::size_t end)
    -> std::string {
  std::string result{};
  for (std::size_t index = begin; index < end; ++index) {
    const Token &token = tokens[index];
    const std::string_view previous =
        index == begin ? std::string_view{}
                       : std::string_view(tokens[index - 1].text);
    const bool noSpaceBefore = token.text == "::" || token.text == "," ||
                               token.text == ";" || token.text == ")" ||
                               token.text == "]" || token.text == ">" ||
                               token.text == "(" || token.text == "[";
    const bool noSpaceAfterPrevious = previous == "::" || previous == "(" ||
                                      previous == "[" || previous == "<" ||
                                      previous == "~";
    if (!result.empty() && !noSpaceBefore && !noSpaceAfterPrevious &&
        (IsWord(token) || IsWord(tokens[index - 1]) || token.text == "*" ||
         token.text == "&" || token.text == "&&" || previous == "*" ||
         previous == "&" || previous == "&&" || previous == ">")) {
      result.push_back(' ');
    }
    result += token.text;
    if (token.text == ",") {
      result.push_back(' ');
    }
  }
  return std::string(Trim(result));
}

[[nodiscard]] auto SpanOf(const std::vector<Token> &tokens,
                          const std::size_t begin, const std::size_t end)
    -> SourceSpan {
  if (begin >= end || begin >= tokens.size()) {
    return {};
  }
  return SourceSpan{.begin = tokens[begin].source,
                    .end = tokens[end - 1].source};
}

auto AddDiagnostic(Scanner &scanner, const SourceLocation &source,
                   const std::string_view declaration,
                   const std::string_view reason, const std::string_view remedy)
    -> void {
  std::ostringstream out{};
  out << source.file.generic_string() << ':' << source.line << ':'
      << source.column;
  if (!declaration.empty()) {
    out << ": '" << declaration << "'";
  }
  out << ": " << reason;
  if (!remedy.empty()) {
    out << ". " << remedy;
  }
  scanner.diagnostics.push_back(out.str());
}

[[nodiscard]] auto IsOwned(const Scanner &scanner, const SourceLocation &source)
    -> bool {
  return scanner.ownedFiles.contains(NormalizedPathKey(source.file));
}

[[nodiscard]] auto
SplitTokenRanges(const std::vector<Token> &tokens, const std::size_t begin,
                 const std::size_t end, const std::string_view delimiter)
    -> std::vector<std::pair<std::size_t, std::size_t>> {
  std::vector<std::pair<std::size_t, std::size_t>> result{};
  std::size_t start = begin;
  int parentheses = 0;
  int brackets = 0;
  int braces = 0;
  int angles = 0;
  for (std::size_t index = begin; index < end; ++index) {
    const std::string &text = tokens[index].text;
    if (text == "(")
      ++parentheses;
    else if (text == ")")
      --parentheses;
    else if (text == "[")
      ++brackets;
    else if (text == "]")
      --brackets;
    else if (text == "{")
      ++braces;
    else if (text == "}")
      --braces;
    else if (text == "<")
      ++angles;
    else if ((text == ">" || text == ">>") && angles > 0)
      angles = std::max(0, angles - (text == ">>" ? 2 : 1));
    else if (text == delimiter && parentheses == 0 && brackets == 0 &&
             braces == 0 && angles == 0) {
      result.emplace_back(start, index);
      start = index + 1;
    }
  }
  result.emplace_back(start, end);
  return result;
}

[[nodiscard]] auto
ParameterFrom(Scanner &scanner, const std::vector<Token> &tokens,
              std::size_t begin, std::size_t end, const std::string_view owner)
    -> ReflectedParameter {
  ReflectedParameter parameter{};
  parameter.source = SpanOf(tokens, begin, end);
  const std::vector<ParsedAnnotation> annotations =
      CollectAnnotations(tokens, begin, end);
  const ParsedAnnotation *dependency =
      FindAnnotation(annotations, "dependency");
  parameter.dependencyName = OptionOr(dependency, "name");
  parameter.optional = OptionEnabled(dependency, "optional");
  if (dependency != nullptr) {
    parameter.attributes = dependency->attributes;
  }
  std::vector<Token> clean = WithoutAnnotations(tokens, begin, end);
  if (clean.size() == 1 && clean.front().text == "void") {
    return parameter;
  }
  int depth = 0;
  for (std::size_t index = 0; index < clean.size(); ++index) {
    if (clean[index].text == "(" || clean[index].text == "[" ||
        clean[index].text == "<")
      ++depth;
    else if (clean[index].text == ")" || clean[index].text == "]" ||
             clean[index].text == ">")
      --depth;
    else if (clean[index].text == "=" && depth == 0) {
      clean.resize(index);
      break;
    }
  }
  if (clean.empty()) {
    AddDiagnostic(scanner, parameter.source.begin, owner,
                  "empty parameter declaration",
                  "Use an ordinary named or unnamed C++ parameter");
    return parameter;
  }
  std::optional<std::size_t> nameIndex{};
  for (std::size_t index = clean.size(); index-- > 0;) {
    if (!clean[index].identifier) {
      continue;
    }
    const bool hasTypeBefore =
        index > 0 &&
        (clean[index - 1].identifier || clean[index - 1].text == ">" ||
         clean[index - 1].text == "]" || clean[index - 1].text == "*" ||
         clean[index - 1].text == "&" || clean[index - 1].text == "&&");
    if (hasTypeBefore) {
      nameIndex = index;
    }
    break;
  }
  if (nameIndex) {
    parameter.cppName = clean[*nameIndex].text;
    clean.erase(clean.begin() + static_cast<std::ptrdiff_t>(*nameIndex));
  }
  parameter.cppType = JoinTokens(clean, 0, clean.size());
  return parameter;
}

[[nodiscard]] auto StripTypeQualifiers(std::string value) -> std::string {
  for (const std::string_view prefix :
       {std::string_view{"const "}, std::string_view{"volatile "}}) {
    if (value.starts_with(prefix)) {
      value.erase(0, prefix.size());
    }
  }
  while (!value.empty() &&
         (value.back() == '&' ||
          std::isspace(static_cast<unsigned char>(value.back())) != 0)) {
    value.pop_back();
  }
  return std::string(Trim(value));
}

[[nodiscard]] auto MethodFrom(Scanner &scanner, const std::vector<Token> &clean,
                              const std::vector<ParsedAnnotation> &annotations,
                              const AccessKind access,
                              const std::string_view owner,
                              const std::string_view unqualifiedType)
    -> std::optional<ReflectedMethod> {
  int angleDepth = 0;
  std::size_t open = clean.size();
  for (std::size_t index = 0; index < clean.size(); ++index) {
    if (clean[index].text == "<")
      ++angleDepth;
    else if ((clean[index].text == ">" || clean[index].text == ">>") &&
             angleDepth > 0)
      angleDepth =
          std::max(0, angleDepth - (clean[index].text == ">>" ? 2 : 1));
    else if (clean[index].text == "(" && angleDepth == 0) {
      open = index;
      break;
    }
  }
  if (open == clean.size() || open == 0) {
    return std::nullopt;
  }
  const std::size_t close = FindMatching(clean, open, "(", ")", clean.size());
  if (close == clean.size()) {
    AddDiagnostic(scanner, clean.front().source, owner,
                  "unbalanced method parameter list",
                  "Use the manual descriptor escape hatch for macro-generated "
                  "declarations");
    return std::nullopt;
  }
  std::size_t nameIndex = open;
  while (nameIndex > 0 && !clean[nameIndex - 1].identifier) {
    --nameIndex;
  }
  if (nameIndex == 0) {
    AddDiagnostic(
        scanner, clean.front().source, owner, "unsupported callable name",
        "Use a normal identifier or a handwritten Describe<T> descriptor");
    return std::nullopt;
  }
  --nameIndex;
  ReflectedMethod method{};
  method.cppName = clean[nameIndex].text;
  method.access = access;
  method.source = SpanOf(clean, 0, clean.size());
  const ParsedAnnotation *methodAnnotation =
      FindAnnotation(annotations, "method");
  const ParsedAnnotation *propertyAnnotation =
      FindAnnotation(annotations, "property");
  const ParsedAnnotation *annotation =
      methodAnnotation != nullptr ? methodAnnotation : propertyAnnotation;
  method.reflectionName = OptionOr(annotation, "name", method.cppName);
  method.selector = OptionOr(annotation, "selector");
  if (annotation != nullptr) {
    method.attributes = annotation->attributes;
  }
  method.isStatic = std::ranges::any_of(
      clean.begin(), clean.begin() + static_cast<std::ptrdiff_t>(nameIndex),
      [](const Token &token) { return token.text == "static"; });
  std::vector<Token> returnTokens{};
  static const std::set<std::string, std::less<>> declarationSpecifiers = {
      "constexpr", "consteval", "constinit", "explicit",
      "friend",    "inline",    "static",    "virtual"};
  for (std::size_t index = 0; index < nameIndex; ++index) {
    if (!declarationSpecifiers.contains(clean[index].text)) {
      returnTokens.push_back(clean[index]);
    }
  }
  method.returnType = JoinTokens(returnTokens, 0, returnTokens.size());
  if (method.cppName == unqualifiedType) {
    method.returnType.clear();
  }
  for (const auto [parameterBegin, parameterEnd] :
       SplitTokenRanges(clean, open + 1, close, ",")) {
    if (parameterBegin == parameterEnd) {
      continue;
    }
    ReflectedParameter parameter =
        ParameterFrom(scanner, clean, parameterBegin, parameterEnd, owner);
    if (parameter.cppType == "void" && parameter.cppName.empty() &&
        close == open + 2) {
      continue;
    }
    method.parameters.push_back(std::move(parameter));
  }
  for (std::size_t index = close + 1; index < clean.size(); ++index) {
    const std::string &text = clean[index].text;
    method.isConst = method.isConst || text == "const";
    method.isVolatile = method.isVolatile || text == "volatile";
    method.isLValueQualified = method.isLValueQualified || text == "&";
    method.isRValueQualified = method.isRValueQualified || text == "&&";
    method.isNoexcept = method.isNoexcept || text == "noexcept";
  }
  return method;
}

auto MergeAttributes(AttributeSet &target, const AttributeSet &source) -> void {
  target.insert(source.begin(), source.end());
}

auto AddPropertyMethod(Scanner &scanner, ReflectedType &type,
                       ReflectedMethod method) -> void {
  if (method.isStatic || method.isVolatile || method.isRValueQualified) {
    AddDiagnostic(
        scanner, method.source.begin,
        type.cppQualifiedName + "::" + method.cppName,
        "static, volatile, and rvalue-qualified property accessors are not "
        "supported",
        "Use an ordinary const/non-const accessor or handwritten Describe<T>");
    return;
  }
  const bool getter = method.parameters.empty() &&
                      method.returnType != "void" && !method.returnType.empty();
  const bool setter =
      method.parameters.size() == 1 && method.returnType == "void";
  if (!getter && !setter) {
    AddDiagnostic(scanner, method.source.begin,
                  type.cppQualifiedName + "::" + method.cppName,
                  "NGIN_PROPERTY requires a zero-parameter non-void getter or "
                  "one-parameter void setter",
                  "Adjust the signature or use NGIN_METHOD");
    return;
  }
  auto found = std::ranges::find(type.properties, method.reflectionName,
                                 &ReflectedProperty::reflectionName);
  if (found == type.properties.end()) {
    type.properties.push_back(
        ReflectedProperty{.reflectionName = method.reflectionName,
                          .source = method.source,
                          .attributes = method.attributes});
    found = std::prev(type.properties.end());
  }
  std::optional<ReflectedMethod> &slot = getter ? found->getter : found->setter;
  if (slot) {
    AddDiagnostic(scanner, method.source.begin,
                  type.cppQualifiedName + "::" + method.cppName,
                  std::string("duplicate property ") +
                      (getter ? "getter" : "setter") + " for '" +
                      method.reflectionName + "'",
                  "Give each accessor pair a unique Name");
    return;
  }
  MergeAttributes(found->attributes, method.attributes);
  slot = std::move(method);
}

auto ParseMember(Scanner &scanner, ReflectedType &type, const std::size_t begin,
                 const std::size_t end, const AccessKind access) -> void {
  if (begin >= end) {
    return;
  }
  const std::vector<ParsedAnnotation> annotations =
      CollectAnnotations(scanner.tokens, begin, end);
  if (FindAnnotation(annotations, "generated_body") != nullptr) {
    type.hasGeneratedBody = true;
    return;
  }
  if (FindAnnotation(annotations, "ignore") != nullptr) {
    return;
  }
  std::vector<Token> clean =
      WithoutAnnotations(scanner.tokens, begin, end, true);
  while (!clean.empty() &&
         (clean.back().text == ";" || clean.back().text == "=" ||
          clean.back().text == "default" || clean.back().text == "delete")) {
    clean.pop_back();
  }
  if (clean.empty()) {
    return;
  }
  if (clean.front().text == "using" || clean.front().text == "typedef" ||
      clean.front().text == "friend" || clean.front().text == "static_assert" ||
      clean.front().text == "template" || clean.front().text == "class" ||
      clean.front().text == "struct" || clean.front().text == "enum") {
    if (FindAnnotation(annotations, "field") != nullptr ||
        FindAnnotation(annotations, "method") != nullptr ||
        FindAnnotation(annotations, "property") != nullptr ||
        FindAnnotation(annotations, "ctor") != nullptr) {
      AddDiagnostic(scanner, clean.front().source, type.cppQualifiedName,
                    "annotation is attached to an unsupported declaration form",
                    "Use a non-template field, method, property accessor, "
                    "constructor, or handwritten Describe<T>");
    }
    return;
  }

  const std::optional<ReflectedMethod> parsedMethod =
      MethodFrom(scanner, clean, annotations, access, type.cppQualifiedName,
                 type.cppQualifiedName.substr(
                     type.cppQualifiedName.rfind("::") == std::string::npos
                         ? 0
                         : type.cppQualifiedName.rfind("::") + 2));
  if (parsedMethod) {
    const ParsedAnnotation *constructor = FindAnnotation(annotations, "ctor");
    const bool isConstructor =
        parsedMethod->returnType.empty() &&
        parsedMethod->cppName ==
            type.cppQualifiedName.substr(
                type.cppQualifiedName.rfind("::") == std::string::npos
                    ? 0
                    : type.cppQualifiedName.rfind("::") + 2);
    if (isConstructor) {
      if (constructor == nullptr) {
        return;
      }
      ReflectedConstructor reflected{};
      reflected.injectable = OptionEnabled(constructor, "injectable");
      reflected.access = access;
      reflected.source = parsedMethod->source;
      reflected.parameters = parsedMethod->parameters;
      reflected.attributes = constructor->attributes;
      type.constructors.push_back(std::move(reflected));
      return;
    }
    if (FindAnnotation(annotations, "property") != nullptr) {
      AddPropertyMethod(scanner, type, *parsedMethod);
    }
    if (FindAnnotation(annotations, "method") != nullptr) {
      if (parsedMethod->isRValueQualified) {
        AddDiagnostic(
            scanner, parsedMethod->source.begin,
            type.cppQualifiedName + "::" + parsedMethod->cppName,
            "rvalue-qualified reflected methods are not supported by the "
            "runtime invoker",
            "Use an lvalue-qualified overload or handwritten Describe<T>");
      } else {
        type.methods.push_back(*parsedMethod);
      }
    }
    return;
  }

  const ParsedAnnotation *fieldAnnotation =
      FindAnnotation(annotations, "field");
  const ParsedAnnotation *propertyAnnotation =
      FindAnnotation(annotations, "property");
  if (access != AccessKind::Public && fieldAnnotation == nullptr &&
      propertyAnnotation == nullptr) {
    return;
  }
  int depth = 0;
  std::size_t cutoff = clean.size();
  for (std::size_t index = 0; index < clean.size(); ++index) {
    const std::string &text = clean[index].text;
    if (text == "(" || text == "[" || text == "<")
      ++depth;
    else if (text == ")" || text == "]" || text == ">")
      --depth;
    else if ((text == "=" || text == "{" || text == ":") && depth == 0) {
      cutoff = index;
      break;
    } else if (text == "," && depth == 0) {
      AddDiagnostic(scanner, clean[index].source, type.cppQualifiedName,
                    "multiple field declarators in one reflected declaration "
                    "are unsupported",
                    "Declare each reflected field in its own statement");
      return;
    }
  }
  std::size_t nameIndex = cutoff;
  while (nameIndex > 0 && !clean[nameIndex - 1].identifier) {
    --nameIndex;
  }
  if (nameIndex == 0) {
    if (fieldAnnotation != nullptr) {
      AddDiagnostic(
          scanner, clean.front().source, type.cppQualifiedName,
          "could not determine the annotated field name",
          "Use a simple named data member or handwritten Describe<T>");
    }
    return;
  }
  --nameIndex;
  static const std::set<std::string, std::less<>> nonFields = {
      "operator", "return", "public", "private", "protected"};
  if (nonFields.contains(clean[nameIndex].text) || nameIndex == 0) {
    return;
  }
  std::vector<Token> typeTokens(
      clean.begin(), clean.begin() + static_cast<std::ptrdiff_t>(nameIndex));
  std::erase_if(typeTokens, [](const Token &token) {
    return token.text == "constexpr" || token.text == "constinit" ||
           token.text == "inline" || token.text == "mutable" ||
           token.text == "static";
  });
  ReflectedField field{};
  field.cppName = clean[nameIndex].text;
  field.cppType = JoinTokens(typeTokens, 0, typeTokens.size());
  field.reflectionName = OptionOr(
      fieldAnnotation != nullptr ? fieldAnnotation : propertyAnnotation, "name",
      field.cppName);
  field.access = access;
  field.source = SpanOf(clean, 0, clean.size());
  if (fieldAnnotation != nullptr) {
    field.attributes = fieldAnnotation->attributes;
  } else if (propertyAnnotation != nullptr) {
    field.attributes = propertyAnnotation->attributes;
  }
  type.fields.push_back(std::move(field));
}

auto ValidateType(Scanner &scanner, ReflectedType &type) -> void {
  for (const ReflectedField &field : type.fields) {
    if (field.access != AccessKind::Public && !type.hasGeneratedBody) {
      AddDiagnostic(
          scanner, field.source.begin,
          type.cppQualifiedName + "::" + field.cppName,
          "a non-public reflected field requires NGIN_GENERATED_BODY()",
          "Add NGIN_GENERATED_BODY() inside the type or use a handwritten "
          "friend descriptor");
    }
  }
  for (const ReflectedMethod &method : type.methods) {
    if (method.access != AccessKind::Public && !type.hasGeneratedBody) {
      AddDiagnostic(
          scanner, method.source.begin,
          type.cppQualifiedName + "::" + method.cppName,
          "a non-public reflected method requires NGIN_GENERATED_BODY()",
          "Add NGIN_GENERATED_BODY() inside the type");
    }
  }
  for (ReflectedProperty &property : type.properties) {
    if (!property.getter) {
      AddDiagnostic(scanner, property.source.begin, type.cppQualifiedName,
                    "property '" + property.reflectionName +
                        "' has a setter but no getter",
                    "Add a matching zero-parameter getter with the same Name");
      continue;
    }
    if (property.setter) {
      const std::string getterType =
          StripTypeQualifiers(property.getter->returnType);
      const std::string setterType =
          StripTypeQualifiers(property.setter->parameters.front().cppType);
      if (getterType != setterType ||
          property.getter->isStatic != property.setter->isStatic) {
        AddDiagnostic(
            scanner, property.source.begin, type.cppQualifiedName,
            "property '" + property.reflectionName +
                "' getter and setter disagree on value type or staticness",
            "Use matching accessor signatures");
      }
    }
    if (((property.getter && property.getter->access != AccessKind::Public) ||
         (property.setter && property.setter->access != AccessKind::Public)) &&
        !type.hasGeneratedBody) {
      AddDiagnostic(
          scanner, property.source.begin, type.cppQualifiedName,
          "non-public property accessors require NGIN_GENERATED_BODY()",
          "Add NGIN_GENERATED_BODY() inside the type");
    }
  }
  std::set<std::string, std::less<>> memberNames{};
  for (const ReflectedField &field : type.fields) {
    if (!memberNames.insert("field:" + field.reflectionName).second) {
      AddDiagnostic(scanner, field.source.begin, type.cppQualifiedName,
                    "duplicate reflected field name '" + field.reflectionName +
                        "'",
                    "Give reflected fields unique Name values");
    }
  }
  for (const ReflectedProperty &property : type.properties) {
    if (!memberNames.insert("property:" + property.reflectionName).second) {
      AddDiagnostic(scanner, property.source.begin, type.cppQualifiedName,
                    "duplicate reflected property name '" +
                        property.reflectionName + "'",
                    "Give reflected properties unique Name values");
    }
  }
}

auto ParseRecordMembers(Scanner &scanner, ReflectedType &type,
                        const std::size_t begin, const std::size_t end)
    -> void {
  AccessKind access = type.defaultAccess;
  std::size_t cursor = begin;
  while (cursor < end) {
    if (cursor + 1 < end &&
        (scanner.tokens[cursor].text == "public" ||
         scanner.tokens[cursor].text == "protected" ||
         scanner.tokens[cursor].text == "private") &&
        scanner.tokens[cursor + 1].text == ":") {
      access = scanner.tokens[cursor].text == "public" ? AccessKind::Public
               : scanner.tokens[cursor].text == "protected"
                   ? AccessKind::Protected
                   : AccessKind::Private;
      cursor += 2;
      continue;
    }
    const std::size_t memberBegin = cursor;
    int parentheses = 0;
    int brackets = 0;
    int angles = 0;
    bool sawClosingParenthesis = false;
    bool completed = false;
    while (cursor < end) {
      const std::string &text = scanner.tokens[cursor].text;
      if (text == "(")
        ++parentheses;
      else if (text == ")") {
        --parentheses;
        sawClosingParenthesis = true;
      } else if (text == "[")
        ++brackets;
      else if (text == "]")
        --brackets;
      else if (text == "<")
        ++angles;
      else if ((text == ">" || text == ">>") && angles > 0)
        angles = std::max(0, angles - (text == ">>" ? 2 : 1));
      else if (text == "{" && parentheses == 0 && brackets == 0 &&
               angles == 0) {
        const std::size_t close =
            FindMatching(scanner.tokens, cursor, "{", "}", end);
        if (close == end) {
          AddDiagnostic(scanner, scanner.tokens[cursor].source,
                        type.cppQualifiedName, "unbalanced member body",
                        "Fix the declaration before running MetaGen");
          return;
        }
        if (sawClosingParenthesis) {
          ParseMember(scanner, type, memberBegin, cursor, access);
          cursor = close + 1;
          if (cursor < end && scanner.tokens[cursor].text == ";")
            ++cursor;
          completed = true;
          break;
        }
        cursor = close;
      } else if (text == ";" && parentheses == 0 && brackets == 0 &&
                 angles == 0) {
        ParseMember(scanner, type, memberBegin, cursor, access);
        ++cursor;
        completed = true;
        break;
      }
      ++cursor;
    }
    if (!completed && cursor >= end) {
      break;
    }
  }
  ValidateType(scanner, type);
}

auto ParseEnumValues(Scanner &scanner, ReflectedType &type,
                     const std::size_t begin, const std::size_t end) -> void {
  for (const auto [valueBegin, valueEnd] :
       SplitTokenRanges(scanner.tokens, begin, end, ",")) {
    if (valueBegin == valueEnd) {
      continue;
    }
    const std::vector<ParsedAnnotation> annotations =
        CollectAnnotations(scanner.tokens, valueBegin, valueEnd);
    if (FindAnnotation(annotations, "ignore") != nullptr) {
      continue;
    }
    const ParsedAnnotation *valueAnnotation =
        FindAnnotation(annotations, "enum_value");
    std::vector<Token> clean =
        WithoutAnnotations(scanner.tokens, valueBegin, valueEnd);
    const auto name = std::ranges::find_if(
        clean, [](const Token &token) { return token.identifier; });
    if (name == clean.end()) {
      continue;
    }
    ReflectedEnumValue value{};
    value.cppName = name->text;
    value.reflectionName = OptionOr(valueAnnotation, "name", value.cppName);
    value.source = SpanOf(clean, 0, clean.size());
    if (valueAnnotation != nullptr) {
      value.attributes = valueAnnotation->attributes;
    }
    type.enumValues.push_back(std::move(value));
  }
}

[[nodiscard]] auto AnnotationSequenceAt(const std::vector<Token> &tokens,
                                        std::size_t cursor,
                                        const std::size_t limit)
    -> std::pair<std::vector<ParsedAnnotation>, std::size_t> {
  std::vector<ParsedAnnotation> annotations{};
  while (cursor < limit) {
    const std::optional<AnnotationParse> parsed =
        ParseAnnotationAt(tokens, cursor, limit);
    if (!parsed) {
      break;
    }
    annotations.push_back(parsed->annotation);
    cursor = parsed->next;
  }
  return {std::move(annotations), cursor};
}

auto ParseBases(ReflectedType &type, const std::vector<Token> &tokens,
                const std::size_t begin, const std::size_t end,
                const bool structDefault) -> void {
  for (const auto [baseBegin, baseEnd] :
       SplitTokenRanges(tokens, begin, end, ",")) {
    std::vector<Token> clean = WithoutAnnotations(tokens, baseBegin, baseEnd);
    AccessKind access =
        structDefault ? AccessKind::Public : AccessKind::Private;
    std::erase_if(clean, [&](const Token &token) {
      if (token.text == "public") {
        access = AccessKind::Public;
        return true;
      }
      if (token.text == "protected") {
        access = AccessKind::Protected;
        return true;
      }
      if (token.text == "private") {
        access = AccessKind::Private;
        return true;
      }
      return token.text == "virtual";
    });
    if (clean.empty() || access != AccessKind::Public) {
      continue;
    }
    type.bases.push_back(
        ReflectedBase{.cppType = JoinTokens(clean, 0, clean.size()),
                      .access = access,
                      .source = SpanOf(clean, 0, clean.size())});
  }
}

auto WalkScope(Scanner &scanner, const std::size_t begin, const std::size_t end,
               const std::string &scope) -> void;

auto ParseTypeAt(Scanner &scanner, const std::size_t keywordIndex,
                 std::vector<ParsedAnnotation> annotations, std::size_t cursor,
                 const std::size_t end, const std::string &scope,
                 const bool templateDeclaration) -> std::size_t {
  const std::string &keyword = scanner.tokens[keywordIndex].text;
  const bool enumType = keyword == "enum";
  const bool structType = keyword == "struct";
  if (enumType && cursor < end &&
      (scanner.tokens[cursor].text == "class" ||
       scanner.tokens[cursor].text == "struct")) {
    ++cursor;
  }
  auto [afterKeywordAnnotations, afterAnnotations] =
      AnnotationSequenceAt(scanner.tokens, cursor, end);
  annotations.insert(annotations.end(), afterKeywordAnnotations.begin(),
                     afterKeywordAnnotations.end());
  cursor = afterAnnotations;
  if (cursor >= end || !scanner.tokens[cursor].identifier) {
    return keywordIndex + 1;
  }
  const std::size_t nameIndex = cursor;
  const std::string name = scanner.tokens[cursor++].text;
  while (cursor < end && scanner.tokens[cursor].text != "{" &&
         scanner.tokens[cursor].text != ";") {
    ++cursor;
  }
  if (cursor >= end || scanner.tokens[cursor].text != "{") {
    return cursor;
  }
  const std::size_t bodyBegin = cursor + 1;
  const std::size_t bodyEnd =
      FindMatching(scanner.tokens, cursor, "{", "}", end);
  if (bodyEnd == end) {
    return end;
  }

  const std::string qualifiedName = scope.empty() ? name : scope + "::" + name;
  const ParsedAnnotation *reflect = FindAnnotation(annotations, "reflect");
  bool openTemplate = templateDeclaration;
  for (std::size_t lookBehind = keywordIndex;
       lookBehind > 0 && keywordIndex - lookBehind < 256;) {
    --lookBehind;
    const std::string &text = scanner.tokens[lookBehind].text;
    if (text == ";" || text == "{" || text == "}") {
      break;
    }
    if (text == "template") {
      openTemplate = true;
      break;
    }
  }
  if (reflect != nullptr && openTemplate &&
      IsOwned(scanner, reflect->source.begin)) {
    AddDiagnostic(
        scanner, reflect->source.begin, qualifiedName,
        "open class templates are outside the supported reflection surface",
        "Reflect a concrete non-template type or use a handwritten Describe<T> "
        "descriptor");
  }
  if (reflect != nullptr && FindAnnotation(annotations, "ignore") == nullptr &&
      IsOwned(scanner, reflect->source.begin) && !openTemplate) {
    ReflectedType type{};
    type.kind = enumType ? TypeKind::Enum : TypeKind::Record;
    type.cppQualifiedName = qualifiedName;
    type.reflectionName = OptionOr(reflect, "name", qualifiedName);
    type.declarationFile = reflect->source.begin.file;
    type.source = SourceSpan{.begin = reflect->source.begin,
                             .end = scanner.tokens[bodyEnd].source};
    type.defaultAccess =
        structType || enumType ? AccessKind::Public : AccessKind::Private;
    type.manualDescriptor = OptionEnabled(reflect, "manual");
    type.attributes = reflect->attributes;
    if (!scanner.reflectedNames.insert(type.reflectionName).second) {
      AddDiagnostic(scanner, type.source.begin, qualifiedName,
                    "duplicate reflected type name '" + type.reflectionName +
                        "'",
                    "Choose a unique NGIN_REFLECT Name");
    }
    const auto colon = std::ranges::find_if(
        scanner.tokens.begin() + static_cast<std::ptrdiff_t>(nameIndex + 1),
        scanner.tokens.begin() + static_cast<std::ptrdiff_t>(cursor),
        [](const Token &token) { return token.text == ":"; });
    if (colon != scanner.tokens.begin() + static_cast<std::ptrdiff_t>(cursor)) {
      const std::size_t colonIndex = static_cast<std::size_t>(
          std::distance(scanner.tokens.begin(), colon));
      ParseBases(type, scanner.tokens, colonIndex + 1, cursor, structType);
      if (!scope.empty()) {
        for (ReflectedBase &base : type.bases) {
          if (!base.cppType.starts_with("::") &&
              base.cppType.find("::") == std::string::npos) {
            base.cppType = scope + "::" + base.cppType;
          }
        }
      }
    }
    if (type.manualDescriptor) {
      // The authored NginReflect/Describe<T> descriptor owns the member model.
    } else if (enumType) {
      ParseEnumValues(scanner, type, bodyBegin, bodyEnd);
    } else {
      ParseRecordMembers(scanner, type, bodyBegin, bodyEnd);
    }
    scanner.model.types.push_back(std::move(type));
  }
  WalkScope(scanner, bodyBegin, bodyEnd, qualifiedName);
  return bodyEnd + 1;
}

auto WalkScope(Scanner &scanner, const std::size_t begin, const std::size_t end,
               const std::string &scope) -> void {
  std::vector<ParsedAnnotation> pending{};
  bool templateDeclaration = false;
  for (std::size_t cursor = begin; cursor < end;) {
    if (scanner.tokens[cursor].text == "template" && cursor + 1 < end &&
        scanner.tokens[cursor + 1].text == "<") {
      std::size_t depth = 0;
      std::size_t close = cursor + 1;
      for (; close < end; ++close) {
        if (scanner.tokens[close].text == "<")
          ++depth;
        else if (scanner.tokens[close].text == ">" && --depth == 0)
          break;
        else if (scanner.tokens[close].text == ">>" && depth > 0) {
          depth = depth > 2 ? depth - 2 : 0;
          if (depth == 0)
            break;
        }
      }
      templateDeclaration = true;
      cursor = close == end ? end : close + 1;
      continue;
    }
    if (const std::optional<AnnotationParse> annotation =
            ParseAnnotationAt(scanner.tokens, cursor, end)) {
      pending.push_back(annotation->annotation);
      cursor = annotation->next;
      continue;
    }
    if (scanner.tokens[cursor].text == "namespace") {
      std::size_t nameCursor = cursor + 1;
      std::string namespaceName{};
      while (nameCursor < end && scanner.tokens[nameCursor].text != "{" &&
             scanner.tokens[nameCursor].text != ";" &&
             scanner.tokens[nameCursor].text != "=") {
        if (scanner.tokens[nameCursor].identifier ||
            scanner.tokens[nameCursor].text == "::")
          namespaceName += scanner.tokens[nameCursor].text;
        ++nameCursor;
      }
      if (nameCursor < end && scanner.tokens[nameCursor].text == "{") {
        const std::size_t close =
            FindMatching(scanner.tokens, nameCursor, "{", "}", end);
        const std::string nestedScope = namespaceName.empty() ? scope
                                        : scope.empty()
                                            ? namespaceName
                                            : scope + "::" + namespaceName;
        WalkScope(scanner, nameCursor + 1, close, nestedScope);
        cursor = close + 1;
        pending.clear();
        continue;
      }
    }
    if (scanner.tokens[cursor].text == "class" ||
        scanner.tokens[cursor].text == "struct" ||
        scanner.tokens[cursor].text == "enum") {
      cursor = ParseTypeAt(scanner, cursor, std::move(pending), cursor + 1, end,
                           scope, templateDeclaration);
      pending.clear();
      templateDeclaration = false;
      continue;
    }
    if (scanner.tokens[cursor].text == ";" ||
        scanner.tokens[cursor].text == "}") {
      pending.clear();
      templateDeclaration = false;
    }
    ++cursor;
  }
}

auto AddHeaders(Scanner &scanner, const std::string_view preprocessed) -> void {
  std::vector<fs::path> headers{};
  for (const fs::path &source : scanner.context.sourceFiles) {
    const std::string extension = Lower(source.extension().string());
    if (extension == ".h" || extension == ".hh" || extension == ".hpp" ||
        extension == ".hxx") {
      headers.push_back(source);
    }
  }
  std::ranges::sort(headers, {}, [](const fs::path &path) {
    return NormalizedPathKey(path);
  });
  headers.erase(std::unique(headers.begin(), headers.end(),
                            [](const fs::path &left, const fs::path &right) {
                              return NormalizedPathKey(left) ==
                                     NormalizedPathKey(right);
                            }),
                headers.end());
  for (const fs::path &header : headers) {
    const std::string normalized = NormalizedPathKey(header);
    scanner.ownedFiles.insert(normalized);
    scanner.model.headers.push_back(ReflectedHeader{
        .path = header.lexically_normal(),
        .normalizedPath = normalized,
        .cacheKey = StableHash(normalized + std::string(preprocessed))});
  }
}
} // namespace

auto ScanPreprocessedSource(const std::string_view source,
                            const MetaGenContext &context) -> ScanResult {
  Scanner scanner{.context = context};
  scanner.model.projectName = context.projectName;
  scanner.model.moduleName = context.projectName + ".Reflection";
  scanner.model.configuration = context.profileName;
  scanner.model.compiler = context.compiler;
  AddHeaders(scanner, source);
  scanner.tokens = Tokenize(source);
  WalkScope(scanner, 0, scanner.tokens.size(), {});
  std::ranges::sort(scanner.model.types, [](const ReflectedType &left,
                                            const ReflectedType &right) {
    const std::string leftFile = NormalizedPathKey(left.declarationFile);
    const std::string rightFile = NormalizedPathKey(right.declarationFile);
    return leftFile == rightFile
               ? left.source.begin.line < right.source.begin.line
               : leftFile < rightFile;
  });
  scanner.model.cacheKey = StableHash(
      std::string(source) + context.projectName + context.profileName +
      context.compiler + std::to_string(ReflectionModelVersion));
  return ScanResult{.model = std::move(scanner.model),
                    .diagnostics = std::move(scanner.diagnostics)};
}
} // namespace NGIN::Reflection::MetaGen
