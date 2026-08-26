#pragma once

#include "MetaGenContext.hpp"
#include "ReflectionModel.hpp"

namespace NGIN::Reflection::MetaGen {
/// @brief Reflection model and diagnostics produced by source inspection.
struct InspectionResult {
  /// @brief Normalized reflection model, valid when diagnostics are empty.
  ReflectionModel model{};
  /// @brief Preprocessing or scanning errors.
  std::vector<std::string> diagnostics{};
};

/// @brief Build a reflection model from the headers in a generator context.
[[nodiscard]] auto BuildReflectionModel(const MetaGenContext &context)
    -> InspectionResult;

/// @brief Render a human-readable explanation of the model or one type.
/// @param model Model to inspect.
/// @param query Empty for the whole model, otherwise a C++ or reflection name.
/// @return Stable textual explanation suitable for CLI output.
[[nodiscard]] auto ExplainReflectionModel(const ReflectionModel &model,
                                          std::string_view query)
    -> std::string;
} // namespace NGIN::Reflection::MetaGen
