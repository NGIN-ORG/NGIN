#pragma once

#include <NGIN/Serialization/Core/ParseError.hpp>
#include <NGIN/Serialization/XML/XmlParser.hpp>
#include <NGIN/Serialization/XML/XmlTypes.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
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

[[nodiscard]] inline auto ReadBinary(const fs::path &path) -> std::string {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("failed to open file: " + path.string());
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

inline auto WriteBinary(const fs::path &path, std::string_view contents)
    -> void {
  if (!path.parent_path().empty()) {
    fs::create_directories(path.parent_path());
  }
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    throw std::runtime_error("failed to write file: " + path.string());
  }
  output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

struct ZipFileEntry {
  std::string path{};
  std::string contents{};
};

[[nodiscard]] inline auto ZipReadU16(std::string_view bytes,
                                     std::size_t offset) -> std::uint16_t {
  if (offset + 2 > bytes.size()) {
    throw std::runtime_error("invalid zip archive; truncated record");
  }
  return static_cast<std::uint16_t>(
      static_cast<unsigned char>(bytes[offset]) |
      (static_cast<unsigned char>(bytes[offset + 1]) << 8U));
}

[[nodiscard]] inline auto ZipReadU32(std::string_view bytes,
                                     std::size_t offset) -> std::uint32_t {
  if (offset + 4 > bytes.size()) {
    throw std::runtime_error("invalid zip archive; truncated record");
  }
  return static_cast<std::uint32_t>(
      static_cast<unsigned char>(bytes[offset]) |
      (static_cast<unsigned char>(bytes[offset + 1]) << 8U) |
      (static_cast<unsigned char>(bytes[offset + 2]) << 16U) |
      (static_cast<unsigned char>(bytes[offset + 3]) << 24U));
}

inline auto ZipWriteU16(std::ostream &out, std::uint16_t value) -> void {
  out.put(static_cast<char>(value & 0xffU));
  out.put(static_cast<char>((value >> 8U) & 0xffU));
}

inline auto ZipWriteU32(std::ostream &out, std::uint32_t value) -> void {
  ZipWriteU16(out, static_cast<std::uint16_t>(value & 0xffffU));
  ZipWriteU16(out, static_cast<std::uint16_t>((value >> 16U) & 0xffffU));
}

[[nodiscard]] inline auto ZipCrc32(std::string_view data) -> std::uint32_t {
  std::uint32_t crc = 0xffffffffU;
  for (const auto ch : data) {
    crc ^= static_cast<unsigned char>(ch);
    for (int bit = 0; bit < 8; ++bit) {
      const auto mask = 0U - (crc & 1U);
      crc = (crc >> 1U) ^ (0xedb88320U & mask);
    }
  }
  return ~crc;
}

struct ZipCentralEntry {
  std::string path{};
  std::uint16_t method{};
  std::uint32_t compressedSize{};
  std::uint32_t uncompressedSize{};
  std::uint32_t localHeaderOffset{};
};

[[nodiscard]] inline auto ZipCentralEntries(const fs::path &archivePath)
    -> std::vector<ZipCentralEntry> {
  const auto bytes = ReadBinary(archivePath);
  if (bytes.size() < 22) {
    throw std::runtime_error(archivePath.string() +
                             ": invalid zip archive; missing end record");
  }

  std::optional<std::size_t> endOffset{};
  const auto minOffset = bytes.size() > 65557 ? bytes.size() - 65557 : 0;
  for (std::size_t offset = bytes.size() - 22; offset + 1 > minOffset;
       --offset) {
    if (ZipReadU32(bytes, offset) == 0x06054b50U) {
      endOffset = offset;
      break;
    }
    if (offset == 0) {
      break;
    }
  }
  if (!endOffset.has_value()) {
    throw std::runtime_error(archivePath.string() +
                             ": invalid zip archive; missing end record");
  }

  const auto entryCount = ZipReadU16(bytes, *endOffset + 10);
  const auto centralOffset = ZipReadU32(bytes, *endOffset + 16);
  std::size_t offset = centralOffset;
  std::vector<ZipCentralEntry> entries{};
  entries.reserve(entryCount);
  for (std::uint16_t index = 0; index < entryCount; ++index) {
    if (ZipReadU32(bytes, offset) != 0x02014b50U) {
      throw std::runtime_error(archivePath.string() +
                               ": invalid zip archive; bad central record");
    }
    const auto method = ZipReadU16(bytes, offset + 10);
    const auto compressedSize = ZipReadU32(bytes, offset + 20);
    const auto uncompressedSize = ZipReadU32(bytes, offset + 24);
    const auto fileNameSize = ZipReadU16(bytes, offset + 28);
    const auto extraSize = ZipReadU16(bytes, offset + 30);
    const auto commentSize = ZipReadU16(bytes, offset + 32);
    const auto localHeaderOffset = ZipReadU32(bytes, offset + 42);
    if (offset + 46 + fileNameSize > bytes.size()) {
      throw std::runtime_error(archivePath.string() +
                               ": invalid zip archive; truncated file name");
    }
    entries.push_back(ZipCentralEntry{
        .path = bytes.substr(offset + 46, fileNameSize),
        .method = method,
        .compressedSize = compressedSize,
        .uncompressedSize = uncompressedSize,
        .localHeaderOffset = localHeaderOffset,
    });
    offset += 46 + fileNameSize + extraSize + commentSize;
  }
  return entries;
}

[[nodiscard]] inline auto ReadZipEntry(const fs::path &archivePath,
                                       std::string_view entryPath)
    -> std::string {
  const auto bytes = ReadBinary(archivePath);
  for (const auto &entry : ZipCentralEntries(archivePath)) {
    if (entry.path != entryPath) {
      continue;
    }
    if (entry.method != 0) {
      throw std::runtime_error(archivePath.string() + ": zip entry '" +
                               entry.path +
                               "' uses unsupported compression method");
    }
    if (entry.compressedSize != entry.uncompressedSize) {
      throw std::runtime_error(archivePath.string() + ": zip entry '" +
                               entry.path + "' has inconsistent sizes");
    }
    const auto headerOffset =
        static_cast<std::size_t>(entry.localHeaderOffset);
    if (ZipReadU32(bytes, headerOffset) != 0x04034b50U) {
      throw std::runtime_error(archivePath.string() +
                               ": invalid zip archive; bad local record");
    }
    const auto fileNameSize = ZipReadU16(bytes, headerOffset + 26);
    const auto extraSize = ZipReadU16(bytes, headerOffset + 28);
    const auto dataOffset = headerOffset + 30 + fileNameSize + extraSize;
    if (dataOffset + entry.compressedSize > bytes.size()) {
      throw std::runtime_error(archivePath.string() +
                               ": invalid zip archive; truncated entry data");
    }
    return bytes.substr(dataOffset, entry.compressedSize);
  }
  throw std::runtime_error(archivePath.string() + ": missing zip entry '" +
                           std::string(entryPath) + "'");
}

inline auto WriteZipFile(const fs::path &archivePath,
                         std::vector<ZipFileEntry> entries) -> void {
  if (!archivePath.parent_path().empty()) {
    fs::create_directories(archivePath.parent_path());
  }
  std::sort(entries.begin(), entries.end(),
            [](const ZipFileEntry &left, const ZipFileEntry &right) {
              return left.path < right.path;
            });

  struct WrittenEntry {
    ZipFileEntry entry{};
    std::uint32_t crc{};
    std::uint32_t offset{};
  };

  std::vector<WrittenEntry> written{};
  written.reserve(entries.size());
  std::ofstream out(archivePath, std::ios::binary);
  if (!out) {
    throw std::runtime_error("failed to write zip archive: " +
                             archivePath.string());
  }

  for (auto &entry : entries) {
    WrittenEntry writtenEntry{};
    writtenEntry.entry = std::move(entry);
    writtenEntry.crc = ZipCrc32(writtenEntry.entry.contents);
    writtenEntry.offset = static_cast<std::uint32_t>(out.tellp());

    ZipWriteU32(out, 0x04034b50U);
    ZipWriteU16(out, 20);
    ZipWriteU16(out, 0);
    ZipWriteU16(out, 0);
    ZipWriteU16(out, 0);
    ZipWriteU16(out, 0);
    ZipWriteU32(out, writtenEntry.crc);
    ZipWriteU32(out,
                static_cast<std::uint32_t>(writtenEntry.entry.contents.size()));
    ZipWriteU32(out,
                static_cast<std::uint32_t>(writtenEntry.entry.contents.size()));
    ZipWriteU16(out,
                static_cast<std::uint16_t>(writtenEntry.entry.path.size()));
    ZipWriteU16(out, 0);
    out.write(writtenEntry.entry.path.data(),
              static_cast<std::streamsize>(writtenEntry.entry.path.size()));
    out.write(writtenEntry.entry.contents.data(),
              static_cast<std::streamsize>(writtenEntry.entry.contents.size()));
    written.push_back(std::move(writtenEntry));
  }

  const auto centralDirectoryOffset = static_cast<std::uint32_t>(out.tellp());
  for (const auto &entry : written) {
    ZipWriteU32(out, 0x02014b50U);
    ZipWriteU16(out, 20);
    ZipWriteU16(out, 20);
    ZipWriteU16(out, 0);
    ZipWriteU16(out, 0);
    ZipWriteU16(out, 0);
    ZipWriteU16(out, 0);
    ZipWriteU32(out, entry.crc);
    ZipWriteU32(out,
                static_cast<std::uint32_t>(entry.entry.contents.size()));
    ZipWriteU32(out,
                static_cast<std::uint32_t>(entry.entry.contents.size()));
    ZipWriteU16(out, static_cast<std::uint16_t>(entry.entry.path.size()));
    ZipWriteU16(out, 0);
    ZipWriteU16(out, 0);
    ZipWriteU16(out, 0);
    ZipWriteU16(out, 0);
    ZipWriteU32(out, 0);
    ZipWriteU32(out, entry.offset);
    out.write(entry.entry.path.data(),
              static_cast<std::streamsize>(entry.entry.path.size()));
  }
  const auto centralDirectorySize =
      static_cast<std::uint32_t>(out.tellp()) - centralDirectoryOffset;

  ZipWriteU32(out, 0x06054b50U);
  ZipWriteU16(out, 0);
  ZipWriteU16(out, 0);
  ZipWriteU16(out, static_cast<std::uint16_t>(written.size()));
  ZipWriteU16(out, static_cast<std::uint16_t>(written.size()));
  ZipWriteU32(out, centralDirectorySize);
  ZipWriteU32(out, centralDirectoryOffset);
  ZipWriteU16(out, 0);
}

inline auto ExtractZipFile(const fs::path &archivePath,
                           const fs::path &destination) -> void {
  const auto normalizedDestination = fs::absolute(destination).lexically_normal();
  for (const auto &entry : ZipCentralEntries(archivePath)) {
    if (entry.path.empty() || entry.path.back() == '/') {
      continue;
    }
    const auto target =
        (normalizedDestination / fs::path(entry.path)).lexically_normal();
    const auto relative = target.lexically_relative(normalizedDestination);
    if (relative.empty() || *relative.begin() == ".." ||
        relative.begin()->is_absolute()) {
      throw std::runtime_error(archivePath.string() +
                               ": unsafe zip entry path '" + entry.path + "'");
    }
    WriteBinary(target, ReadZipEntry(archivePath, entry.path));
  }
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

[[nodiscard]] inline auto LoadXmlText(const std::string &text,
                                      const std::string &origin) -> LoadedXml {
  LoadedXml loaded{};
  loaded.text = text;
  XmlParseOptions options{};
  options.decodeEntities = true;
  options.arenaBytes = std::max<NGIN::UIntSize>(
      16384, static_cast<NGIN::UIntSize>(loaded.text.size() * 8 + 4096));
  auto parsed = XmlParser::Parse(loaded.text, options);
  if (!parsed.HasValue()) {
    throw std::runtime_error(
        origin + ": failed to parse XML: " + ToString(parsed.Error()));
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

[[nodiscard]] inline auto NormalizeGlobPattern(std::string pattern)
    -> std::string {
  std::replace(pattern.begin(), pattern.end(), '\\', '/');
  return pattern;
}

[[nodiscard]] inline auto GlobToRegex(std::string_view pattern) -> std::string {
  std::string regex;
  regex.reserve(pattern.size() * 2 + 2);
  regex += '^';
  for (std::size_t index = 0; index < pattern.size(); ++index) {
    const char ch = pattern[index];
    if (ch == '*') {
      if (index + 1 < pattern.size() && pattern[index + 1] == '*') {
        index += 1;
        if (index + 1 < pattern.size() && pattern[index + 1] == '/') {
          regex += "(?:.*/)?";
          index += 1;
        } else {
          regex += ".*";
        }
      } else {
        regex += "[^/]*";
      }
      continue;
    }
    if (ch == '?') {
      regex += "[^/]";
      continue;
    }
    if (std::string_view(".^$+{}()[]|").find(ch) != std::string_view::npos) {
      regex.push_back('\\');
    }
    regex.push_back(ch);
  }
  regex += '$';
  return regex;
}

[[nodiscard]] inline auto GlobMatches(std::string_view pattern,
                                      std::string_view path) -> bool {
  const auto normalizedPattern = NormalizeGlobPattern(std::string(pattern));
  auto normalizedPath = std::string(path);
  std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
  try {
    return std::regex_match(normalizedPath, std::regex(GlobToRegex(normalizedPattern)));
  } catch (const std::regex_error &) {
    return false;
  }
}

[[nodiscard]] inline auto AnyGlobMatches(const std::vector<std::string> &patterns,
                                         const fs::path &relativePath)
    -> bool {
  const auto value = relativePath.generic_string();
  return std::any_of(patterns.begin(), patterns.end(),
                     [&](const std::string &pattern) {
                       return GlobMatches(pattern, value);
                     });
}
} // namespace NGIN::CLI
