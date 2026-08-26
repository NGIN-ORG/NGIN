#include "MetaGenCommon.hpp"

#include <algorithm>
#include <cctype>

namespace NGIN::Reflection::MetaGen {
namespace {
[[nodiscard]] auto Trim(std::string_view input) -> std::string_view {
  while (!input.empty() &&
         std::isspace(static_cast<unsigned char>(input.front())) != 0) {
    input.remove_prefix(1);
  }
  while (!input.empty() &&
         std::isspace(static_cast<unsigned char>(input.back())) != 0) {
    input.remove_suffix(1);
  }
  return input;
}

[[nodiscard]] auto Unquote(std::string_view input) -> std::string {
  input = Trim(input);
  if (input.size() >= 2 && ((input.front() == '"' && input.back() == '"') ||
                            (input.front() == '\'' && input.back() == '\''))) {
    input.remove_prefix(1);
    input.remove_suffix(1);
  }
  return std::string(input);
}
} // namespace

auto ParseAnnotation(std::string_view payload) -> Annotation {
  Annotation annotation{};
  constexpr std::string_view prefix = "ngin.";
  payload = Trim(payload);
  if (payload.starts_with(prefix)) {
    payload.remove_prefix(prefix.size());
  }

  const std::size_t separator = payload.find(':');
  annotation.kind = std::string(Trim(separator == std::string_view::npos
                                         ? payload
                                         : payload.substr(0, separator)));
  if (separator == std::string_view::npos) {
    return annotation;
  }

  std::string_view options = payload.substr(separator + 1);
  while (!options.empty()) {
    const std::size_t semicolon = options.find(';');
    const std::size_t comma = options.find(',');
    const std::size_t split = semicolon == std::string_view::npos ? comma
                              : comma == std::string_view::npos
                                  ? semicolon
                                  : std::min(semicolon, comma);
    std::string_view item = Trim(
        split == std::string_view::npos ? options : options.substr(0, split));
    if (!item.empty()) {
      const std::size_t equals = item.find('=');
      if (equals == std::string_view::npos) {
        annotation.options.emplace(std::string(Trim(item)), "true");
      } else {
        annotation.options.emplace(std::string(Trim(item.substr(0, equals))),
                                   Unquote(item.substr(equals + 1)));
      }
    }
    if (split == std::string_view::npos) {
      break;
    }
    options.remove_prefix(split + 1);
  }
  return annotation;
}

auto EscapeCppString(std::string_view input) -> std::string {
  std::string output{};
  output.reserve(input.size());
  for (const char ch : input) {
    switch (ch) {
    case '\\':
      output += "\\\\";
      break;
    case '"':
      output += "\\\"";
      break;
    case '\n':
      output += "\\n";
      break;
    case '\r':
      output += "\\r";
      break;
    case '\t':
      output += "\\t";
      break;
    default:
      output.push_back(ch);
      break;
    }
  }
  return output;
}

auto EscapeJsonString(const std::string_view input) -> std::string {
  std::string output{};
  output.reserve(input.size());
  constexpr char digits[] = "0123456789abcdef";
  for (const unsigned char ch : input) {
    switch (ch) {
    case '\\':
      output += "\\\\";
      break;
    case '"':
      output += "\\\"";
      break;
    case '\b':
      output += "\\b";
      break;
    case '\f':
      output += "\\f";
      break;
    case '\n':
      output += "\\n";
      break;
    case '\r':
      output += "\\r";
      break;
    case '\t':
      output += "\\t";
      break;
    default:
      if (ch < 0x20) {
        output += "\\u00";
        output.push_back(digits[(ch >> 4u) & 0x0fu]);
        output.push_back(digits[ch & 0x0fu]);
      } else {
        output.push_back(static_cast<char>(ch));
      }
      break;
    }
  }
  return output;
}

auto SanitizeIdentifier(std::string_view input) -> std::string {
  std::string output{};
  output.reserve(input.size());
  for (const char ch : input) {
    if (std::isalnum(static_cast<unsigned char>(ch)) != 0) {
      output.push_back(ch);
    } else {
      output.push_back('_');
    }
  }
  if (output.empty() ||
      std::isdigit(static_cast<unsigned char>(output.front())) != 0) {
    output.insert(output.begin(), '_');
  }
  return output;
}
} // namespace NGIN::Reflection::MetaGen
