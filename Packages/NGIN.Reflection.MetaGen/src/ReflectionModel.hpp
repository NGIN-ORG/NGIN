#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace NGIN::Reflection::MetaGen {
namespace fs = std::filesystem;

/// @brief Schema version written into serialized reflection models.
inline constexpr std::uint32_t ReflectionModelVersion = 1;

/// @brief C++ declaration category represented by a reflected type.
enum class TypeKind {
  Record, ///< A class or struct declaration.
  Enum,   ///< An enumeration declaration.
};

/// @brief Effective C++ access level of a reflected declaration.
enum class AccessKind {
  Public,    ///< Publicly accessible declaration.
  Protected, ///< Declaration accessible to derived types.
  Private,   ///< Declaration accessible only to its owning type.
};

/// @brief Literal category preserved for an authored metadata value.
enum class MetadataValueKind {
  Boolean,       ///< Boolean literal.
  Integer,       ///< Integral literal.
  FloatingPoint, ///< Floating-point literal.
  String,        ///< Quoted string literal.
  Identifier,    ///< Unquoted identifier or expression spelling.
};

/// @brief Physical source position recovered from preprocessor line markers.
struct SourceLocation {
  fs::path file{};       ///< Source file path.
  std::size_t line{1};   ///< One-based line number.
  std::size_t column{1}; ///< One-based byte column.
};

/// @brief Half-open source range covering one reflected declaration.
struct SourceSpan {
  SourceLocation begin{}; ///< First position in the declaration.
  SourceLocation end{};   ///< Position immediately after the declaration.
};

/// @brief Typed metadata literal retained in the reflection model.
struct MetadataValue {
  MetadataValueKind kind{MetadataValueKind::Identifier}; ///< Literal category.
  std::string text{}; ///< Normalized spelling.
};

/// @brief Deterministically ordered attributes indexed by authored key.
using AttributeSet = std::map<std::string, MetadataValue, std::less<>>;

/// @brief Parameter of a reflected method or constructor.
struct ReflectedParameter {
  std::string cppType{};        ///< Canonical C++ type spelling.
  std::string cppName{};        ///< Authored parameter name.
  std::string dependencyName{}; ///< Optional dependency-injection name.
  bool optional{false};         ///< Whether injection may omit the dependency.
  SourceSpan source{};          ///< Declaration range.
  AttributeSet attributes{};    ///< User-authored metadata.
};

/// @brief Reflected non-static data member.
struct ReflectedField {
  std::string cppName{};                 ///< Authored member name.
  std::string cppType{};                 ///< Canonical C++ type spelling.
  std::string reflectionName{};          ///< Name exposed through reflection.
  AccessKind access{AccessKind::Public}; ///< Effective C++ access.
  SourceSpan source{};                   ///< Declaration range.
  AttributeSet attributes{};             ///< User-authored metadata.
};

/// @brief Reflected member or free/static function.
struct ReflectedMethod {
  std::string cppName{};                 ///< Authored function name.
  std::string returnType{};              ///< Canonical return-type spelling.
  std::string reflectionName{};          ///< Name exposed through reflection.
  std::string selector{};                ///< Optional overload selector.
  AccessKind access{AccessKind::Public}; ///< Effective C++ access.
  bool isStatic{false};                  ///< Whether no instance is required.
  bool isConst{false};           ///< Whether the member is const-qualified.
  bool isVolatile{false};        ///< Whether the member is volatile-qualified.
  bool isLValueQualified{false}; ///< Whether the member requires an lvalue.
  bool isRValueQualified{false}; ///< Whether the member requires an rvalue.
  bool isNoexcept{false};        ///< Whether the declaration is noexcept.
  SourceSpan source{};           ///< Declaration range.
  std::vector<ReflectedParameter> parameters{}; ///< Ordered parameters.
  AttributeSet attributes{};                    ///< User-authored metadata.
};

/// @brief Reflected property assembled from getter and setter methods.
struct ReflectedProperty {
  std::string reflectionName{};            ///< Name exposed through reflection.
  std::optional<ReflectedMethod> getter{}; ///< Optional read accessor.
  std::optional<ReflectedMethod> setter{}; ///< Optional write accessor.
  SourceSpan source{};                     ///< Combined accessor range.
  AttributeSet attributes{};               ///< Merged accessor metadata.
};

/// @brief Reflected constructor and its injection contract.
struct ReflectedConstructor {
  bool injectable{false};                ///< Whether the runtime may inject it.
  AccessKind access{AccessKind::Public}; ///< Effective C++ access.
  SourceSpan source{};                   ///< Declaration range.
  std::vector<ReflectedParameter> parameters{}; ///< Ordered parameters.
  AttributeSet attributes{};                    ///< User-authored metadata.
};

/// @brief Reflected enumerator.
struct ReflectedEnumValue {
  std::string cppName{};        ///< Authored enumerator name.
  std::string reflectionName{}; ///< Name exposed through reflection.
  SourceSpan source{};          ///< Declaration range.
  AttributeSet attributes{};    ///< User-authored metadata.
};

/// @brief Reflected direct base-class relationship.
struct ReflectedBase {
  std::string cppType{};                 ///< Canonical base-type spelling.
  AccessKind access{AccessKind::Public}; ///< Effective inheritance access.
  SourceSpan source{};                   ///< Declaration range.
  AttributeSet attributes{};             ///< User-authored metadata.
};

/// @brief Complete metadata for one reflected class, struct, or enum.
struct ReflectedType {
  TypeKind kind{TypeKind::Record}; ///< Declaration category.
  std::string cppQualifiedName{};  ///< Fully qualified C++ name.
  std::string reflectionName{};    ///< Name exposed through reflection.
  fs::path declarationFile{};      ///< Owning authored header.
  SourceSpan source{};             ///< Declaration range.
  AccessKind defaultAccess{
      AccessKind::Public};      ///< Class or struct default access.
  bool hasGeneratedBody{false}; ///< Whether `NGIN_GENERATED_BODY` is present.
  bool manualDescriptor{
      false}; ///< Whether generated registration is suppressed.
  std::vector<ReflectedBase> bases{};   ///< Direct bases in authored order.
  std::vector<ReflectedField> fields{}; ///< Reflected fields in authored order.
  std::vector<ReflectedProperty> properties{}; ///< Assembled properties.
  std::vector<ReflectedMethod>
      methods{}; ///< Reflected methods in authored order.
  std::vector<ReflectedConstructor> constructors{}; ///< Reflected constructors.
  std::vector<ReflectedEnumValue> enumValues{};     ///< Reflected enumerators.
  AttributeSet attributes{}; ///< User-authored type metadata.
};

/// @brief Authored header participating in reflection generation.
struct ReflectedHeader {
  fs::path path{};              ///< Header path emitted in includes.
  std::string normalizedPath{}; ///< Stable normalized path spelling.
  std::string cacheKey{};       ///< Content-addressed generation key.
};

/// @brief Versioned, deterministic intermediate representation for MetaGen.
struct ReflectionModel {
  std::uint32_t version{ReflectionModelVersion}; ///< Serialized schema version.
  std::string projectName{};                     ///< NGIN product name.
  std::string moduleName{};                      ///< Runtime module identity.
  std::string configuration{};            ///< Active build configuration.
  std::string compiler{};                 ///< Preprocessor compiler identity.
  std::string cacheKey{};                 ///< Whole-model content key.
  std::vector<ReflectedHeader> headers{}; ///< Participating authored headers.
  std::vector<ReflectedType> types{};     ///< Reflected types in stable order.
};

/// @brief Serialize a model to the stable JSON interchange format.
[[nodiscard]] auto SerializeReflectionModel(const ReflectionModel &model)
    -> std::string;

/// @brief Compute the portable hexadecimal hash used for generation keys.
[[nodiscard]] auto StableHash(std::string_view text) -> std::string;
} // namespace NGIN::Reflection::MetaGen
