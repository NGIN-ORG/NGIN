#pragma once

#include <map>
#include <string>
#include <string_view>

namespace NGIN::Reflection::MetaGen {
/// @brief Parsed `NGIN_META(...)` annotation and its key-value options.
struct Annotation {
  /// @brief Annotation kind, such as `type`, `field`, or `method`.
  std::string kind{};
  /// @brief Annotation options indexed by their authored key.
  std::map<std::string, std::string> options{};
};

/// @brief Parse an annotation payload into its kind and options.
/// @param payload Text inside an `NGIN_META(...)` marker.
/// @return Parsed annotation data.
[[nodiscard]] auto ParseAnnotation(std::string_view payload) -> Annotation;

/// @brief Escape text for use inside a C++ string literal.
[[nodiscard]] auto EscapeCppString(std::string_view input) -> std::string;

/// @brief Escape text for use inside a JSON string literal.
[[nodiscard]] auto EscapeJsonString(std::string_view input) -> std::string;

/// @brief Convert arbitrary text into a valid C++ identifier fragment.
[[nodiscard]] auto SanitizeIdentifier(std::string_view input) -> std::string;
} // namespace NGIN::Reflection::MetaGen
