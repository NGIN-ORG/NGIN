#pragma once

#include <NGIN/Serialization/Core/ParseError.hpp>
#include <NGIN/Serialization/XML/XmlParser.hpp>
#include <NGIN/Serialization/XML/XmlTypes.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace NGIN::CLI {
namespace fs = std::filesystem;

using NGIN::Serialization::ParseError;
using NGIN::Serialization::XmlDocument;
using NGIN::Serialization::XmlElement;
using NGIN::Serialization::XmlNode;
using NGIN::Serialization::XmlParseOptions;
using NGIN::Serialization::XmlParser;

struct LoadedXml {
  std::string text{};
  XmlDocument document{0};
};

[[nodiscard]] inline auto ReadText(const fs::path &path) -> std::string {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("failed to open file: " + path.string());
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

[[nodiscard]] inline auto ToString(const ParseError &error) -> std::string {
  return std::string(error.message.Data(), error.message.Size());
}

[[nodiscard]] inline auto LoadXml(const fs::path &path) -> LoadedXml {
  LoadedXml loaded{};
  loaded.text = ReadText(path);
  XmlParseOptions options{};
  options.decodeEntities = true;
  options.arenaBytes = std::max<NGIN::UIntSize>(
      16384, static_cast<NGIN::UIntSize>(loaded.text.size() * 8 + 4096));
  auto parsed = XmlParser::Parse(loaded.text, options);
  if (!parsed.HasValue()) {
    throw std::runtime_error(
        path.string() + ": failed to parse XML: " + ToString(parsed.Error()));
  }
  loaded.document = std::move(parsed.Value());
  return loaded;
}

[[nodiscard]] inline auto ChildElements(const XmlElement &node,
                                        std::string_view name = {})
    -> std::vector<const XmlElement *> {
  std::vector<const XmlElement *> out;
  out.reserve(static_cast<std::size_t>(node.children.Size()));
  for (NGIN::UIntSize index = 0; index < node.children.Size(); ++index) {
    const auto &child = node.children[index];
    if (child.type != XmlNode::Type::Element || child.element == nullptr) {
      continue;
    }
    if (name.empty() || child.element->name == name) {
      out.push_back(child.element);
    }
  }
  return out;
}

[[nodiscard]] inline auto FindChild(const XmlElement &node,
                                    std::string_view name)
    -> const XmlElement * {
  for (NGIN::UIntSize index = 0; index < node.children.Size(); ++index) {
    const auto &child = node.children[index];
    if (child.type == XmlNode::Type::Element && child.element != nullptr &&
        child.element->name == name) {
      return child.element;
    }
  }
  return nullptr;
}

[[nodiscard]] inline auto Attribute(const XmlElement &node,
                                    std::string_view key)
    -> std::optional<std::string> {
  const auto *attr = node.FindAttribute(key);
  if (attr == nullptr) {
    return std::nullopt;
  }
  return std::string(attr->value);
}

[[nodiscard]] inline auto RequireAttribute(const XmlElement &node,
                                           std::string_view key,
                                           const fs::path &path)
    -> std::string {
  const auto value = Attribute(node, key);
  if (!value.has_value()) {
    throw std::runtime_error(path.string() + ": missing required attribute '" +
                             std::string(key) + "'");
  }
  return *value;
}

[[nodiscard]] inline auto BoolAttribute(const XmlElement &node,
                                        std::string_view key,
                                        bool defaultValue = false) -> bool {
  const auto value = Attribute(node, key);
  if (!value.has_value()) {
    return defaultValue;
  }
  return *value == "true" || *value == "1" || *value == "yes";
}

[[nodiscard]] inline auto Lower(std::string value) -> std::string {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

[[nodiscard]] inline auto EscapeXml(std::string_view input) -> std::string {
  std::string out;
  out.reserve(input.size());
  for (const char ch : input) {
    switch (ch) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    case '"':
      out += "&quot;";
      break;
    case '\'':
      out += "&apos;";
      break;
    default:
      out.push_back(ch);
      break;
    }
  }
  return out;
}

[[nodiscard]] inline auto EscapeCMake(std::string_view input) -> std::string {
  std::string out;
  out.reserve(input.size());
  for (const char ch : input) {
    if (ch == '\\' || ch == '"' || ch == '$') {
      out.push_back('\\');
    }
    out.push_back(ch);
  }
  return out;
}

[[nodiscard]] inline auto ToCMakePath(const fs::path &path) -> std::string {
  return EscapeCMake(path.generic_string());
}

[[nodiscard]] inline auto NormalizePath(const fs::path &path) -> fs::path {
  std::error_code error;
  const auto normalized = fs::weakly_canonical(path, error);
  if (!error) {
    return normalized.lexically_normal();
  }
  return fs::absolute(path).lexically_normal();
}

[[nodiscard]] inline auto IsPathWithinDirectory(const fs::path &candidate,
                                                const fs::path &directory)
    -> bool {
  const auto normalizedCandidate = NormalizePath(candidate);
  const auto normalizedDirectory = NormalizePath(directory);

  auto candidateIt = normalizedCandidate.begin();
  auto directoryIt = normalizedDirectory.begin();
  for (; directoryIt != normalizedDirectory.end() &&
         candidateIt != normalizedCandidate.end();
       ++directoryIt, ++candidateIt) {
    if (*candidateIt != *directoryIt) {
      return false;
    }
  }
  return directoryIt == normalizedDirectory.end();
}
} // namespace NGIN::CLI
