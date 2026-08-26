#include "MetaGenContext.hpp"
#include "MetaGenInspection.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {
auto PrintUsage() -> void {
  std::cerr << "usage: ngin-metagen --context <file> [--explain "
               "<header-or-type> | --dump-model]\n";
}
} // namespace

auto main(int argc, char **argv) -> int {
  std::filesystem::path contextPath{};
  std::string explainQuery{};
  bool dumpModel = false;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (argument == "--context" && index + 1 < argc) {
      contextPath = argv[++index];
      continue;
    }
    if (argument == "--explain" && index + 1 < argc) {
      explainQuery = argv[++index];
      continue;
    }
    if (argument == "--dump-model") {
      dumpModel = true;
      continue;
    }
    PrintUsage();
    return 2;
  }

  if (contextPath.empty()) {
    PrintUsage();
    return 2;
  }

  std::vector<std::string> diagnostics{};
  NGIN::Reflection::MetaGen::MetaGenContext context =
      NGIN::Reflection::MetaGen::ReadContext(contextPath, diagnostics);
  if (!diagnostics.empty()) {
    for (const std::string &diagnostic : diagnostics) {
      std::cerr << "error: " << diagnostic << "\n";
    }
    return 1;
  }

  if (!explainQuery.empty() || dumpModel) {
    NGIN::Reflection::MetaGen::InspectionResult inspected =
        NGIN::Reflection::MetaGen::BuildReflectionModel(context);
    if (!inspected.diagnostics.empty()) {
      for (const std::string &diagnostic : inspected.diagnostics) {
        std::cerr << "error: " << diagnostic << "\n";
      }
      return 1;
    }
    if (dumpModel) {
      std::cout << NGIN::Reflection::MetaGen::SerializeReflectionModel(
          inspected.model);
    } else {
      std::cout << NGIN::Reflection::MetaGen::ExplainReflectionModel(
          inspected.model, explainQuery);
    }
    return 0;
  }

  NGIN::Reflection::MetaGen::MetaGenResult result =
      NGIN::Reflection::MetaGen::GenerateMetaData(context);
  if (!result.available || !result.diagnostics.empty()) {
    for (const std::string &diagnostic : result.diagnostics) {
      std::cerr << "error: " << diagnostic << "\n";
    }
    return 1;
  }

  for (const std::filesystem::path &file : result.generatedFiles) {
    std::cout << "generated: " << file.string() << "\n";
  }
  std::cout << "reflected types: " << result.reflectedTypeCount << "\n";
  return 0;
}
