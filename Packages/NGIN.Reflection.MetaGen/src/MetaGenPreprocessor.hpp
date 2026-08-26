#pragma once

#include "MetaGenContext.hpp"

#include <string>

namespace NGIN::Reflection::MetaGen {
/// @brief Preprocessed scan input and supporting generation artifacts.
struct PreprocessResult {
  /// @brief Combined preprocessed translation unit consumed by the scanner.
  std::string source{};
  /// @brief Compiler executable used for preprocessing.
  std::string compiler{};
  /// @brief Intermediate files created for the preprocessing step.
  std::vector<fs::path> generatedFiles{};
  /// @brief Context, compiler, or preprocessing errors.
  std::vector<std::string> diagnostics{};
};

/// @brief Preprocess project headers with the target's real compile command.
[[nodiscard]] auto PreprocessHeaders(const MetaGenContext &context)
    -> PreprocessResult;
} // namespace NGIN::Reflection::MetaGen
