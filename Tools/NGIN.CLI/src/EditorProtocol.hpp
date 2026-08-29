#pragma once

#include "AuthoredManifest.hpp"
#include "Canonical.hpp"
#include "CompositionGraph.hpp"
#include "ProjectModel.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace NGIN::CLI {
struct EditorItemRequest {
  std::string path{};
  BuildItemKind kind{BuildItemKind::Source};
  std::optional<BuildVisibility> visibility{};
};

struct EditorPlanRequest {
  std::string intent{};
  std::vector<EditorItemRequest> items{};
  std::optional<std::string> from{};
  std::optional<std::string> to{};
  std::optional<std::string> precondition{};
  std::optional<std::string> packageName{};
  std::optional<std::string> version{};
  bool exactVersion{false};
};

[[nodiscard]] auto SerializeEditorProductSnapshot(
    const std::filesystem::path &projectPath,
    const ResolvedCompositionGraph &graph, std::string_view configuration,
    std::string_view target, std::string_view toolchain,
    std::string_view manifestText) -> std::string;

[[nodiscard]] auto CreateEditorAuthoringPlan(
    const std::filesystem::path &projectPath,
    const AuthoredProjectManifest &authored, const SemanticProject &effective,
    const ResolvedCompositionGraph &graph, const EditorPlanRequest &request,
    std::string_view manifestText) -> CanonicalValue;
} // namespace NGIN::CLI
