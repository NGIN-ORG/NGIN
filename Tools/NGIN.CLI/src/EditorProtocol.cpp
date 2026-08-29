#include "EditorProtocol.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace NGIN::CLI {
namespace {
[[nodiscard]] auto ArtifactKindName(const ProductArtifactKind kind)
    -> std::string {
  return kind == ProductArtifactKind::Executable ? "Executable" : "Library";
}

[[nodiscard]] auto LibraryKindText(const LibraryKind kind) -> std::string {
  switch (kind) {
  case LibraryKind::None:
    return "None";
  case LibraryKind::Static:
    return "Static";
  case LibraryKind::Shared:
    return "Shared";
  case LibraryKind::Interface:
    return "Interface";
  case LibraryKind::Plugin:
    return "Plugin";
  }
  return "None";
}

[[nodiscard]] auto BuildKindText(const BuildItemKind kind) -> std::string {
  switch (kind) {
  case BuildItemKind::Source:
    return "Source";
  case BuildItemKind::Header:
    return "Header";
  case BuildItemKind::CxxModule:
    return "CxxModule";
  case BuildItemKind::Resource:
    return "Resource";
  case BuildItemKind::IncludeDirectory:
    return "IncludeDirectory";
  case BuildItemKind::Define:
    return "Define";
  case BuildItemKind::CompileOption:
    return "CompileOption";
  case BuildItemKind::LinkOption:
    return "LinkOption";
  case BuildItemKind::PrecompiledHeader:
    return "PrecompiledHeader";
  }
  return "Source";
}

[[nodiscard]] auto
VisibilityText(const std::optional<BuildVisibility> visibility) -> std::string {
  if (!visibility.has_value())
    return {};
  switch (*visibility) {
  case BuildVisibility::Private:
    return "Private";
  case BuildVisibility::Public:
    return "Public";
  case BuildVisibility::Interface:
    return "Interface";
  }
  return {};
}

[[nodiscard]] auto XmlEscape(const std::string_view value) -> std::string {
  std::string result{};
  result.reserve(value.size());
  for (const char character : value) {
    switch (character) {
    case '&':
      result += "&amp;";
      break;
    case '<':
      result += "&lt;";
      break;
    case '>':
      result += "&gt;";
      break;
    case '"':
      result += "&quot;";
      break;
    case '\'':
      result += "&apos;";
      break;
    default:
      result += character;
      break;
    }
  }
  return result;
}

[[nodiscard]] auto NormalizedEditorPath(const std::string_view input)
    -> std::string {
  const std::filesystem::path path{input};
  if (path.empty() || path.is_absolute())
    throw std::runtime_error("editor item paths must be project-relative");
  const auto normalized = path.lexically_normal().generic_string();
  if (normalized == ".." || normalized.starts_with("../"))
    throw std::runtime_error(
        "editor item paths must remain inside the product boundary");
  return normalized.starts_with("./") ? normalized.substr(2) : normalized;
}

[[nodiscard]] auto GlobMatches(const std::string_view pattern,
                               const std::string_view value) -> bool {
  const auto match = [&](const auto &self, const std::size_t patternAt,
                         const std::size_t valueAt) -> bool {
    if (patternAt == pattern.size())
      return valueAt == value.size();
    if (pattern[patternAt] == '*') {
      const bool recursive =
          patternAt + 1 < pattern.size() && pattern[patternAt + 1] == '*';
      const auto next = patternAt + (recursive ? 2U : 1U);
      if (recursive && next < pattern.size() && pattern[next] == '/') {
        if (self(self, next + 1, valueAt))
          return true;
        for (auto cursor = valueAt; cursor < value.size(); ++cursor)
          if (value[cursor] == '/' && self(self, next + 1, cursor + 1))
            return true;
        return false;
      }
      for (auto cursor = valueAt;; ++cursor) {
        if (self(self, next, cursor))
          return true;
        if (cursor == value.size() || (!recursive && value[cursor] == '/'))
          break;
      }
      return false;
    }
    if (valueAt == value.size())
      return false;
    if (pattern[patternAt] == '?')
      return value[valueAt] != '/' && self(self, patternAt + 1, valueAt + 1);
    return pattern[patternAt] == value[valueAt] &&
           self(self, patternAt + 1, valueAt + 1);
  };
  return match(match, 0, 0);
}

struct Membership {
  bool selected{false};
  const BuildItemDeclaration *rule{};
};

[[nodiscard]] auto MembershipFor(const SemanticProject &effective,
                                 const EditorItemRequest &item) -> Membership {
  Membership result{};
  for (const auto &rule : effective.build.declarations) {
    if (rule.kind != item.kind)
      continue;
    if (!GlobMatches(rule.pattern, item.path))
      continue;
    if (rule.operation == BuildItemOperation::Include) {
      if (rule.exclude.has_value() && GlobMatches(*rule.exclude, item.path))
        continue;
      result = {.selected = true, .rule = &rule};
    } else if (rule.operation == BuildItemOperation::Remove) {
      result = {.selected = false, .rule = &rule};
    }
  }
  return result;
}

[[nodiscard]] auto LineStart(const std::string_view source,
                             const std::size_t offset) -> std::size_t {
  const auto previous = source.rfind('\n', offset == 0 ? 0 : offset - 1);
  return previous == std::string_view::npos ? 0 : previous + 1;
}

[[nodiscard]] auto IndentAt(const std::string_view source,
                            const std::size_t offset) -> std::string {
  const auto begin = LineStart(source, offset);
  auto end = begin;
  while (end < offset && (source[end] == ' ' || source[end] == '\t'))
    ++end;
  return std::string{source.substr(begin, end - begin)};
}

[[nodiscard]] auto Newline(const std::string_view source) -> std::string {
  return source.contains("\r\n") ? "\r\n" : "\n";
}

[[nodiscard]] auto Declaration(const EditorItemRequest &item,
                               const std::string_view operation)
    -> std::string {
  auto result = "<" + BuildKindText(item.kind) + " " + std::string{operation} +
                "=\"" + XmlEscape(item.path) + "\"";
  if (item.visibility.has_value())
    result += " Visibility=\"" + VisibilityText(item.visibility) + "\"";
  return result + " />";
}

struct PlannedTextEdit {
  std::size_t start{};
  std::size_t end{};
  std::string text{};
};

auto CollectPathEdits(const AuthoredElement &element,
                      const std::string_view source,
                      const std::string_view from, const std::string_view to,
                      std::vector<PlannedTextEdit> &edits) -> void {
  static const std::vector<std::string> pathAttributes{
      "Include", "Exclude", "Remove",          "Update",
      "From",    "Path",    "WorkingDirectory"};
  for (const auto &attribute : element.attributes) {
    if (std::ranges::find(pathAttributes, attribute.name) ==
        pathAttributes.end())
      continue;
    if (attribute.value != from &&
        !attribute.value.starts_with(std::string{from} + "/"))
      continue;
    const auto begin =
        source.find(attribute.value, attribute.source.begin.offset);
    if (begin == std::string_view::npos ||
        begin + attribute.value.size() > attribute.source.end.offset)
      throw std::runtime_error("cannot locate authored path attribute text");
    edits.push_back(PlannedTextEdit{
        .start = begin,
        .end = begin + attribute.value.size(),
        .text =
            XmlEscape(std::string{to} + attribute.value.substr(from.size()))});
  }
  for (const auto &child : element.children)
    CollectPathEdits(child, source, from, to, edits);
}

[[nodiscard]] auto PathRole(const std::filesystem::path &projectDirectory,
                            const std::string_view authored) -> std::string {
  const std::filesystem::path path{authored};
  const auto absolute = path.is_absolute()
                            ? path.lexically_normal()
                            : (projectDirectory / path).lexically_normal();
  const auto relative =
      absolute.lexically_relative(projectDirectory).generic_string();
  return relative.empty() ? std::string{authored} : relative;
}

[[nodiscard]] auto
CrossesNestedBoundary(const std::filesystem::path &projectPath,
                      const std::string_view destination) -> bool {
  const auto root = projectPath.parent_path();
  auto current = (root / std::filesystem::path{destination}).parent_path();
  while (current != root && current.string().size() >= root.string().size()) {
    std::error_code error{};
    for (const auto &entry :
         std::filesystem::directory_iterator(current, error))
      if (!error && entry.is_regular_file() &&
          entry.path().extension() == ".nginproj" &&
          std::filesystem::weakly_canonical(entry.path()) !=
              std::filesystem::weakly_canonical(projectPath))
        return true;
    const auto parent = current.parent_path();
    if (parent == current)
      break;
    current = parent;
  }
  return false;
}

[[nodiscard]] auto PlanBuildInsertion(
    const AuthoredProjectManifest &authored, const std::string_view source,
    const std::vector<std::pair<EditorItemRequest, std::string>> &items)
    -> std::optional<PlannedTextEdit> {
  if (items.empty())
    return std::nullopt;
  const auto newline = Newline(source);
  const auto build =
      std::ranges::find(authored.root.children, std::string{"project.build"},
                        &AuthoredElement::specId);
  if (build != authored.root.children.end()) {
    const auto close = source.rfind("</Build>", build->source.end.offset);
    if (close != std::string_view::npos &&
        close >= build->source.begin.offset) {
      const auto insertion = LineStart(source, close);
      const auto indent = IndentAt(source, close) + "  ";
      std::string text{};
      for (const auto &[item, operation] : items)
        text += indent + Declaration(item, operation) + newline;
      return PlannedTextEdit{
          .start = insertion, .end = insertion, .text = std::move(text)};
    }
    const auto tagEnd = source.find('>', build->source.begin.offset);
    if (tagEnd == std::string_view::npos)
      throw std::runtime_error("cannot locate authored Build element");
    const auto slash = source.find_last_not_of(" \t\r\n", tagEnd - 1);
    if (slash == std::string_view::npos || source[slash] != '/')
      throw std::runtime_error("cannot locate authored Build closing element");
    const auto indent = IndentAt(source, build->source.begin.offset);
    std::string replacement =
        std::string{source.substr(build->source.begin.offset,
                                  slash - build->source.begin.offset)} +
        ">" + newline;
    for (const auto &[item, operation] : items)
      replacement += indent + "  " + Declaration(item, operation) + newline;
    replacement += indent + "</Build>";
    return PlannedTextEdit{.start = build->source.begin.offset,
                           .end = tagEnd + 1,
                           .text = std::move(replacement)};
  }
  const auto rootClose = source.rfind("</" + authored.root.name + ">");
  if (rootClose == std::string_view::npos) {
    const auto tagEnd = source.find('>', authored.root.source.begin.offset);
    const auto slash = tagEnd == std::string_view::npos
                           ? std::string_view::npos
                           : source.find_last_not_of(" \t\r\n", tagEnd - 1);
    if (slash == std::string_view::npos || source[slash] != '/')
      throw std::runtime_error("cannot locate product closing element");
    auto replacement =
        std::string{source.substr(authored.root.source.begin.offset,
                                  slash - authored.root.source.begin.offset)} +
        ">" + newline + "  <Build>" + newline;
    for (const auto &[item, operation] : items)
      replacement += "    " + Declaration(item, operation) + newline;
    replacement += "  </Build>" + newline + "</" + authored.root.name + ">";
    return PlannedTextEdit{.start = authored.root.source.begin.offset,
                           .end = tagEnd + 1,
                           .text = std::move(replacement)};
  }
  const auto insertion = LineStart(source, rootClose);
  const auto indent = IndentAt(source, rootClose);
  std::string text = indent + "  <Build>" + newline;
  for (const auto &[item, operation] : items)
    text += indent + "    " + Declaration(item, operation) + newline;
  text += indent + "  </Build>" + newline;
  return PlannedTextEdit{
      .start = insertion, .end = insertion, .text = std::move(text)};
}

[[nodiscard]] auto ItemValue(const EditorItemRequest &item,
                             const Membership before, const bool after)
    -> CanonicalValue {
  CanonicalValue::Object value{
      {"after", CanonicalValue::Object{{"selected", after}}},
      {"before", CanonicalValue::Object{{"selected", before.selected}}},
      {"kind", BuildKindText(item.kind)},
      {"path", item.path},
      {"visibility", VisibilityText(item.visibility)}};
  if (before.rule != nullptr)
    value.emplace(
        "matchedRule",
        CanonicalValue::Object{
            {"column",
             static_cast<std::int64_t>(before.rule->source.begin.column)},
            {"line", static_cast<std::int64_t>(before.rule->source.begin.line)},
            {"pattern", before.rule->pattern}});
  return value;
}

[[nodiscard]] auto FindPackageElement(const AuthoredProjectManifest &authored,
                                      const std::string_view name)
    -> const AuthoredElement * {
  const auto uses =
      std::ranges::find(authored.root.children, std::string{"project.uses"},
                        &AuthoredElement::specId);
  if (uses == authored.root.children.end())
    return nullptr;
  const auto package =
      std::ranges::find_if(uses->children, [&](const auto &child) {
        const auto *attribute = child.Attribute("Name");
        return child.specId == "project.uses.package" && attribute != nullptr &&
               attribute->value == name;
      });
  return package == uses->children.end() ? nullptr : &*package;
}

[[nodiscard]] auto PackageDeclaration(const EditorPlanRequest &request)
    -> std::string {
  auto declaration =
      "<Package Name=\"" + XmlEscape(*request.packageName) + "\"";
  if (request.version.has_value())
    declaration +=
        std::string{request.exactVersion ? " Exact=\"" : " Version=\""} +
        XmlEscape(*request.version) + "\"";
  return declaration + " />";
}

[[nodiscard]] auto PlanPackageInsertion(const AuthoredProjectManifest &authored,
                                        const std::string_view source,
                                        const EditorPlanRequest &request)
    -> PlannedTextEdit {
  const auto newline = Newline(source);
  const auto declaration = PackageDeclaration(request);
  const auto uses =
      std::ranges::find(authored.root.children, std::string{"project.uses"},
                        &AuthoredElement::specId);
  if (uses != authored.root.children.end()) {
    const auto close = source.rfind("</Uses>", uses->source.end.offset);
    if (close != std::string_view::npos && close >= uses->source.begin.offset) {
      const auto insertion = LineStart(source, close);
      return {.start = insertion,
              .end = insertion,
              .text = IndentAt(source, close) + "  " + declaration + newline};
    }
    const auto tagEnd = source.find('>', uses->source.begin.offset);
    const auto slash = tagEnd == std::string_view::npos
                           ? std::string_view::npos
                           : source.find_last_not_of(" \t\r\n", tagEnd - 1);
    if (slash == std::string_view::npos || source[slash] != '/')
      throw std::runtime_error("cannot locate authored Uses element");
    const auto indent = IndentAt(source, uses->source.begin.offset);
    return {.start = uses->source.begin.offset,
            .end = tagEnd + 1,
            .text = indent + "<Uses>" + newline + indent + "  " + declaration +
                    newline + indent + "</Uses>"};
  }
  const auto rootClose = source.rfind("</" + authored.root.name + ">");
  if (rootClose == std::string_view::npos) {
    const auto tagEnd = source.find('>', authored.root.source.begin.offset);
    const auto slash = tagEnd == std::string_view::npos
                           ? std::string_view::npos
                           : source.find_last_not_of(" \t\r\n", tagEnd - 1);
    if (slash == std::string_view::npos || source[slash] != '/')
      throw std::runtime_error("cannot locate product closing element");
    auto opening =
        std::string{source.substr(authored.root.source.begin.offset,
                                  slash - authored.root.source.begin.offset)};
    return {.start = authored.root.source.begin.offset,
            .end = tagEnd + 1,
            .text = opening + ">" + newline + "  <Uses>" + newline + "    " +
                    declaration + newline + "  </Uses>" + newline + "</" +
                    authored.root.name + ">"};
  }
  const auto insertion = LineStart(source, rootClose);
  const auto indent = IndentAt(source, rootClose);
  return {.start = insertion,
          .end = insertion,
          .text = indent + "  <Uses>" + newline + indent + "    " +
                  declaration + newline + indent + "  </Uses>" + newline};
}

[[nodiscard]] auto PlanPackageRequirement(const AuthoredElement &package,
                                          const std::string_view source,
                                          const EditorPlanRequest &request)
    -> PlannedTextEdit {
  const auto tagEnd = source.find('>', package.source.begin.offset);
  if (tagEnd == std::string_view::npos || tagEnd >= package.source.end.offset)
    throw std::runtime_error("cannot locate authored Package start tag");
  auto opening = std::string{source.substr(
      package.source.begin.offset, tagEnd - package.source.begin.offset + 1)};
  std::vector<std::pair<std::size_t, std::size_t>> obsolete{};
  for (const auto &attribute : package.attributes) {
    if (attribute.name != "Version" && attribute.name != "Exact")
      continue;
    auto begin = attribute.source.begin.offset - package.source.begin.offset;
    const auto end = attribute.source.end.offset - package.source.begin.offset;
    while (begin > 0 &&
           std::isspace(static_cast<unsigned char>(opening[begin - 1])))
      --begin;
    obsolete.emplace_back(begin, end);
  }
  std::ranges::sort(obsolete, [](const auto &left, const auto &right) {
    return left.first > right.first;
  });
  for (const auto &[begin, end] : obsolete)
    opening.erase(begin, end - begin);
  if (request.version.has_value()) {
    const bool selfClosing = opening.rfind("/>") != std::string::npos;
    auto insert = selfClosing ? opening.rfind("/>") : opening.rfind('>');
    while (insert > 0 &&
           std::isspace(static_cast<unsigned char>(opening[insert - 1])))
      --insert;
    const auto close = selfClosing ? opening.rfind("/>") : opening.rfind('>');
    opening.erase(insert, close - insert);
    opening.insert(
        insert,
        std::string{request.exactVersion ? " Exact=\"" : " Version=\""} +
            XmlEscape(*request.version) + (selfClosing ? "\" " : "\""));
  }
  return {.start = package.source.begin.offset,
          .end = tagEnd + 1,
          .text = std::move(opening)};
}

[[nodiscard]] auto ProvenanceValue(const GraphProvenance &value)
    -> CanonicalValue {
  return CanonicalValue::Object{
      {"column", static_cast<std::int64_t>(value.column)},
      {"document", value.document},
      {"kind", value.kind},
      {"line", static_cast<std::int64_t>(value.line)},
      {"owner", value.owner},
      {"reason", value.reason}};
}
} // namespace

auto SerializeEditorProductSnapshot(const std::filesystem::path &projectPath,
                                    const ResolvedCompositionGraph &graph,
                                    const std::string_view configuration,
                                    const std::string_view target,
                                    const std::string_view toolchain,
                                    const std::string_view manifestText)
    -> std::string {
  const auto &data = graph.Data();
  CanonicalValue::Array roles{};
  for (const auto &item : data.buildItems) {
    if (item.kind != "Source" && item.kind != "Header" &&
        item.kind != "CxxModule" && item.kind != "Resource")
      continue;
    roles.emplace_back(
        CanonicalValue::Object{{"generated", item.generated},
                               {"kind", item.kind},
                               {"path", item.path},
                               {"provenance", ProvenanceValue(item.provenance)},
                               {"role", "Build"},
                               {"visibility", item.visibility}});
  }
  for (const auto &action : data.actions) {
    for (const auto &input : action.inputs)
      roles.emplace_back(CanonicalValue::Object{
          {"generated", false},
          {"kind", "File"},
          {"path", input},
          {"provenance", ProvenanceValue(action.provenance)},
          {"role", "ActionInput"},
          {"visibility", ""}});
    for (const auto &output : action.outputs)
      roles.emplace_back(CanonicalValue::Object{
          {"generated", true},
          {"kind", "File"},
          {"path", output},
          {"provenance", ProvenanceValue(action.provenance)},
          {"role", "ActionOutput"},
          {"visibility", ""}});
  }
  for (const auto &contribution : data.contributions)
    roles.emplace_back(CanonicalValue::Object{
        {"generated", false},
        {"kind", contribution.kind},
        {"path", contribution.include},
        {"provenance", ProvenanceValue(contribution.provenance)},
        {"role", contribution.provenance.kind == "ProjectStage"
                     ? "StageInput"
                     : "ExternalInput"},
        {"visibility", ""}});
  std::ranges::sort(roles, {}, [](const CanonicalValue &value) {
    return SerializeCanonical(value);
  });

  const auto canonicalProject =
      std::filesystem::weakly_canonical(projectPath).generic_string();
  return SerializeCanonical(CanonicalValue::Object{
      {"capabilities",
       CanonicalValue::Object{{"authoringPlan", true}, {"fileRoles", true}}},
      {"context",
       CanonicalValue::Object{{"configuration", std::string{configuration}},
                              {"target", std::string{target}},
                              {"toolchain", std::string{toolchain}}}},
      {"fileRoles", std::move(roles)},
      {"kind", "NGIN.EditorProductSnapshot"},
      {"manifestHash", Sha256Fingerprint(manifestText)},
      {"product",
       CanonicalValue::Object{
           {"artifactKind", ArtifactKindName(data.product.artifactKind)},
           {"boundary", projectPath.parent_path().generic_string()},
           {"id", Sha256Fingerprint(canonicalProject)},
           {"libraryKind", LibraryKindText(data.product.libraryKind)},
           {"manifest", canonicalProject},
           {"name", data.product.name}}},
      {"state", "ready"},
      {"version", std::int64_t{1}},
  });
}

auto CreateEditorAuthoringPlan(const std::filesystem::path &projectPath,
                               const AuthoredProjectManifest &authored,
                               const SemanticProject &effective,
                               const ResolvedCompositionGraph &graph,
                               const EditorPlanRequest &request,
                               const std::string_view manifestText)
    -> CanonicalValue {
  static const std::vector<std::string> supported{
      "CreateItems",  "IncludeItems",
      "ExcludeItems", "RenameItems",
      "MoveItems",    "DeleteItems",
      "AddPackage",   "ChangePackageRequirement",
      "RemovePackage"};
  if (std::ranges::find(supported, request.intent) == supported.end())
    throw std::runtime_error("editor planner does not yet support intent '" +
                             request.intent + "'");
  const bool packageIntent = request.intent == "AddPackage" ||
                             request.intent == "ChangePackageRequirement" ||
                             request.intent == "RemovePackage";
  if (!packageIntent && request.items.empty())
    throw std::runtime_error(
        "editor authoring plan requires at least one item");
  if (packageIntent &&
      (!request.packageName.has_value() || request.packageName->empty()))
    throw std::runtime_error("package authoring plans require --package");

  const auto manifestHash = Sha256Fingerprint(manifestText);
  CanonicalValue::Array diagnostics{};
  if (request.precondition.has_value() && *request.precondition != manifestHash)
    diagnostics.emplace_back(CanonicalValue::Object{
        {"code", "NGIN9001"},
        {"message",
         "the project manifest changed after the request was prepared"},
        {"severity", "error"}});

  std::vector<std::pair<EditorItemRequest, std::string>> declarations{};
  std::vector<PlannedTextEdit> plannedEdits{};
  CanonicalValue::Array items{};
  CanonicalValue::Array filesystem{};
  const bool relocating =
      request.intent == "RenameItems" || request.intent == "MoveItems";
  const bool deleting = request.intent == "DeleteItems";
  std::optional<std::string> from{};
  std::optional<std::string> to{};
  if (relocating) {
    if (!request.from.has_value() || !request.to.has_value())
      throw std::runtime_error("rename and move plans require --from and --to");
    from = NormalizedEditorPath(*request.from);
    to = NormalizedEditorPath(*request.to);
    if (CrossesNestedBoundary(projectPath, *to))
      diagnostics.emplace_back(CanonicalValue::Object{
          {"code", "NGIN9004"},
          {"message", "the destination crosses a nested product boundary"},
          {"path", *to},
          {"severity", "error"}});
  }

  const auto &data = graph.Data();
  const auto projectDirectory = projectPath.parent_path();

  if (packageIntent && diagnostics.empty()) {
    const auto *package = FindPackageElement(authored, *request.packageName);
    if (request.intent == "AddPackage") {
      if (package != nullptr)
        diagnostics.emplace_back(CanonicalValue::Object{
            {"code", "NGIN9010"},
            {"message", "package '" + *request.packageName +
                            "' is already a direct requirement"},
            {"severity", "error"}});
      else
        plannedEdits.push_back(
            PlanPackageInsertion(authored, manifestText, request));
    } else if (package == nullptr) {
      diagnostics.emplace_back(CanonicalValue::Object{
          {"code", "NGIN9011"},
          {"message", "package '" + *request.packageName +
                          "' is not a direct requirement"},
          {"severity", "error"}});
    } else if (request.intent == "ChangePackageRequirement") {
      if (!request.version.has_value() || request.version->empty())
        diagnostics.emplace_back(CanonicalValue::Object{
            {"code", "NGIN9012"},
            {"message",
             "changing a package requirement requires --version or --exact"},
            {"severity", "error"}});
      else
        plannedEdits.push_back(
            PlanPackageRequirement(*package, manifestText, request));
    } else {
      auto end = package->source.end.offset;
      if (end < manifestText.size() && manifestText[end] == '\r')
        ++end;
      if (end < manifestText.size() && manifestText[end] == '\n')
        ++end;
      plannedEdits.push_back(
          {.start = LineStart(manifestText, package->source.begin.offset),
           .end = end,
           .text = {}});
    }
  }
  const auto unsafeRole =
      [&](const std::string_view path) -> std::optional<std::string> {
    for (const auto &item : data.buildItems)
      if (PathRole(projectDirectory, item.path) == path && item.generated)
        return "generated Build output";
    for (const auto &action : data.actions) {
      if (std::ranges::any_of(action.outputs, [&](const auto &value) {
            return PathRole(projectDirectory, value) == path;
          }))
        return "generated Action output";
      if (deleting &&
          std::ranges::any_of(action.inputs, [&](const auto &value) {
            return PathRole(projectDirectory, value) == path;
          }))
        return "Action input";
    }
    if (deleting)
      for (const auto &contribution : data.contributions)
        if (contribution.provenance.kind == "ProjectStage" &&
            PathRole(projectDirectory, contribution.include) == path)
          return "Stage input";
    return std::nullopt;
  };

  for (auto item : request.items) {
    item.path = NormalizedEditorPath(item.path);
    if (effective.libraryKind == LibraryKind::Interface &&
        item.kind == BuildItemKind::Source)
      diagnostics.emplace_back(CanonicalValue::Object{
          {"code", "NGIN9002"},
          {"message",
           "Interface Libraries cannot contain compiled Source items"},
          {"path", item.path},
          {"severity", "error"}});
    const auto before = MembershipFor(effective, item);
    const bool include =
        request.intent == "CreateItems" || request.intent == "IncludeItems";
    bool after = include;
    if (relocating) {
      item.path = *from;
      if (const auto role = unsafeRole(*from))
        diagnostics.emplace_back(
            CanonicalValue::Object{{"code", "NGIN9003"},
                                   {"message", "cannot move " + *role},
                                   {"path", *from},
                                   {"severity", "error"}});
      auto destination = item;
      destination.path = *to;
      const auto destinationMembership = MembershipFor(effective, destination);
      const bool exactRule =
          before.rule != nullptr && before.rule->pattern == *from;
      if (before.selected && !destinationMembership.selected && !exactRule)
        declarations.emplace_back(destination, "Include");
      CollectPathEdits(authored.root, manifestText, *from, *to, plannedEdits);
      filesystem.emplace_back(CanonicalValue::Object{
          {"operation", request.intent == "RenameItems" ? "rename" : "move"},
          {"path", *from},
          {"to", *to}});
      auto value =
          std::get<CanonicalValue::Object>(ItemValue(item, before, true).value);
      value.emplace("to", *to);
      items.emplace_back(std::move(value));
      continue;
    }
    if (deleting) {
      after = false;
      if (const auto role = unsafeRole(item.path))
        diagnostics.emplace_back(CanonicalValue::Object{
            {"code", "NGIN9005"},
            {"message", "cannot delete a file used as an " + *role},
            {"path", item.path},
            {"severity", "error"}});
      if (before.selected)
        declarations.emplace_back(item, "Remove");
      filesystem.emplace_back(
          CanonicalValue::Object{{"operation", "delete"}, {"path", item.path}});
    } else if ((include && !before.selected) || (!include && before.selected))
      declarations.emplace_back(item, include ? "Include" : "Remove");
    items.emplace_back(ItemValue(item, before, after));
    if (request.intent == "CreateItems")
      filesystem.emplace_back(
          CanonicalValue::Object{{"operation", "create"}, {"path", item.path}});
  }

  CanonicalValue::Array textEdits{};
  if (diagnostics.empty()) {
    if (const auto edit =
            PlanBuildInsertion(authored, manifestText, declarations))
      plannedEdits.push_back(*edit);
    std::ranges::sort(plannedEdits, {}, &PlannedTextEdit::start);
    for (const auto &edit : plannedEdits)
      textEdits.emplace_back(CanonicalValue::Object{
          {"end", static_cast<std::int64_t>(edit.end)},
          {"path",
           std::filesystem::weakly_canonical(projectPath).generic_string()},
          {"start", static_cast<std::int64_t>(edit.start)},
          {"text", edit.text}});
  }

  const bool rejected =
      !diagnostics.empty() || (!declarations.empty() && textEdits.empty());
  CanonicalValue::Array refresh{};
  refresh.emplace_back(
      "product:" +
      Sha256Fingerprint(
          std::filesystem::weakly_canonical(projectPath).generic_string()));
  return CanonicalValue::Object{
      {"affectedProducts", CanonicalValue::Array{graph.Data().product.name}},
      {"diagnostics", std::move(diagnostics)},
      {"filesystem", std::move(filesystem)},
      {"intent", request.intent},
      {"items", std::move(items)},
      {"kind", "NGIN.EditorAuthoringPlan"},
      {"preconditions",
       CanonicalValue::Array{CanonicalValue::Object{
           {"path",
            std::filesystem::weakly_canonical(projectPath).generic_string()},
           {"sha256", manifestHash}}}},
      {"refresh", std::move(refresh)},
      {"state", rejected ? "rejected" : "ready"},
      {"textEdits", std::move(textEdits)},
      {"version", std::int64_t{1}},
  };
}
} // namespace NGIN::CLI
