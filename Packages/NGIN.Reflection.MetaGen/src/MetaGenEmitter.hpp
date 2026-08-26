#pragma once

#include "MetaGenContext.hpp"
#include "ReflectionModel.hpp"

namespace NGIN::Reflection::MetaGen {
/// @brief Files and diagnostics produced by the reflection emitter.
struct EmitResult {
  /// @brief Generated files written or confirmed unchanged.
  std::vector<fs::path> generatedFiles{};
  /// @brief Emission errors that prevent usable output.
  std::vector<std::string> diagnostics{};
};

/// @brief Emit deterministic registration sources and model artifacts.
[[nodiscard]] auto EmitReflection(const MetaGenContext &context,
                                  const ReflectionModel &model) -> EmitResult;
} // namespace NGIN::Reflection::MetaGen
