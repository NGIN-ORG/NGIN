#pragma once

#include "MetaGenContext.hpp"
#include "ReflectionModel.hpp"

#include <string_view>

namespace NGIN::Reflection::MetaGen {
/// @brief Reflection model and diagnostics produced by the token scanner.
struct ScanResult {
  /// @brief Normalized reflection model, valid when diagnostics are empty.
  ReflectionModel model{};
  /// @brief Unsupported-declaration and annotation errors.
  std::vector<std::string> diagnostics{};
};

/// @brief Scan preprocessed C++ without loading a compiler AST library.
/// @param source Preprocessed translation unit text.
/// @param context Product context used for ownership and module identity.
/// @return Deterministic model plus any actionable diagnostics.
[[nodiscard]] auto ScanPreprocessedSource(std::string_view source,
                                          const MetaGenContext &context)
    -> ScanResult;
} // namespace NGIN::Reflection::MetaGen
