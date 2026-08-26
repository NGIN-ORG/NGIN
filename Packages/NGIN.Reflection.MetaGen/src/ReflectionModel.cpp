#include "ReflectionModel.hpp"

#include "MetaGenCommon.hpp"

#include <iomanip>
#include <sstream>

namespace NGIN::Reflection::MetaGen {
namespace {
auto Indent(std::ostringstream &out, const int depth) -> void {
  for (int index = 0; index < depth; ++index) {
    out << "  ";
  }
}

auto String(std::ostringstream &out, const std::string_view value) -> void {
  out << '\"' << EscapeJsonString(value) << '\"';
}

[[nodiscard]] auto AccessName(const AccessKind access) -> std::string_view {
  switch (access) {
  case AccessKind::Public:
    return "public";
  case AccessKind::Protected:
    return "protected";
  case AccessKind::Private:
    return "private";
  }
  return "public";
}

[[nodiscard]] auto ValueKindName(const MetadataValueKind kind)
    -> std::string_view {
  switch (kind) {
  case MetadataValueKind::Boolean:
    return "boolean";
  case MetadataValueKind::Integer:
    return "integer";
  case MetadataValueKind::FloatingPoint:
    return "floating-point";
  case MetadataValueKind::String:
    return "string";
  case MetadataValueKind::Identifier:
    return "identifier";
  }
  return "identifier";
}

auto Location(std::ostringstream &out, const SourceLocation &location) -> void {
  out << "{\"file\":";
  String(out, location.file.generic_string());
  out << ",\"line\":" << location.line << ",\"column\":" << location.column
      << '}';
}

auto Span(std::ostringstream &out, const SourceSpan &span) -> void {
  out << "{\"begin\":";
  Location(out, span.begin);
  out << ",\"end\":";
  Location(out, span.end);
  out << '}';
}

auto Attributes(std::ostringstream &out, const AttributeSet &attributes,
                const int depth) -> void {
  out << '{';
  bool first = true;
  for (const auto &[name, value] : attributes) {
    if (!first) {
      out << ',';
    }
    out << '\n';
    Indent(out, depth + 1);
    String(out, name);
    out << ":{\"kind\":";
    String(out, ValueKindName(value.kind));
    out << ",\"value\":";
    String(out, value.text);
    out << '}';
    first = false;
  }
  if (!attributes.empty()) {
    out << '\n';
    Indent(out, depth);
  }
  out << '}';
}

auto Parameter(std::ostringstream &out, const ReflectedParameter &parameter,
               const int depth) -> void {
  out << "{\"cppType\":";
  String(out, parameter.cppType);
  out << ",\"cppName\":";
  String(out, parameter.cppName);
  out << ",\"dependencyName\":";
  String(out, parameter.dependencyName);
  out << ",\"optional\":" << (parameter.optional ? "true" : "false")
      << ",\"source\":";
  Span(out, parameter.source);
  out << ",\"attributes\":";
  Attributes(out, parameter.attributes, depth + 1);
  out << '}';
}

auto Parameters(std::ostringstream &out,
                const std::vector<ReflectedParameter> &parameters,
                const int depth) -> void {
  out << '[';
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    if (index != 0) {
      out << ',';
    }
    out << '\n';
    Indent(out, depth + 1);
    Parameter(out, parameters[index], depth + 1);
  }
  if (!parameters.empty()) {
    out << '\n';
    Indent(out, depth);
  }
  out << ']';
}

auto Method(std::ostringstream &out, const ReflectedMethod &method,
            const int depth) -> void {
  out << "{\"cppName\":";
  String(out, method.cppName);
  out << ",\"returnType\":";
  String(out, method.returnType);
  out << ",\"reflectionName\":";
  String(out, method.reflectionName);
  out << ",\"selector\":";
  String(out, method.selector);
  out << ",\"access\":";
  String(out, AccessName(method.access));
  out << ",\"static\":" << (method.isStatic ? "true" : "false")
      << ",\"const\":" << (method.isConst ? "true" : "false")
      << ",\"volatile\":" << (method.isVolatile ? "true" : "false")
      << ",\"lvalueQualified\":"
      << (method.isLValueQualified ? "true" : "false")
      << ",\"rvalueQualified\":"
      << (method.isRValueQualified ? "true" : "false")
      << ",\"noexcept\":" << (method.isNoexcept ? "true" : "false")
      << ",\"source\":";
  Span(out, method.source);
  out << ",\"parameters\":";
  Parameters(out, method.parameters, depth + 1);
  out << ",\"attributes\":";
  Attributes(out, method.attributes, depth + 1);
  out << '}';
}

template <class T, class Emit>
auto Array(std::ostringstream &out, const std::vector<T> &values,
           const int depth, Emit emit) -> void {
  out << '[';
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0) {
      out << ',';
    }
    out << '\n';
    Indent(out, depth + 1);
    emit(out, values[index], depth + 1);
  }
  if (!values.empty()) {
    out << '\n';
    Indent(out, depth);
  }
  out << ']';
}

auto Type(std::ostringstream &out, const ReflectedType &type, const int depth)
    -> void {
  out << "{\"kind\":";
  String(out, type.kind == TypeKind::Enum ? "enum" : "record");
  out << ",\"cppQualifiedName\":";
  String(out, type.cppQualifiedName);
  out << ",\"reflectionName\":";
  String(out, type.reflectionName);
  out << ",\"declarationFile\":";
  String(out, type.declarationFile.generic_string());
  out << ",\"source\":";
  Span(out, type.source);
  out << ",\"defaultAccess\":";
  String(out, AccessName(type.defaultAccess));
  out << ",\"hasGeneratedBody\":" << (type.hasGeneratedBody ? "true" : "false")
      << ",\"manualDescriptor\":" << (type.manualDescriptor ? "true" : "false")
      << ",\"attributes\":";
  Attributes(out, type.attributes, depth + 1);
  out << ",\"bases\":";
  Array(out, type.bases, depth + 1,
        [](std::ostringstream &target, const ReflectedBase &base, const int d) {
          target << "{\"cppType\":";
          String(target, base.cppType);
          target << ",\"access\":";
          String(target, AccessName(base.access));
          target << ",\"source\":";
          Span(target, base.source);
          target << ",\"attributes\":";
          Attributes(target, base.attributes, d + 1);
          target << '}';
        });
  out << ",\"fields\":";
  Array(
      out, type.fields, depth + 1,
      [](std::ostringstream &target, const ReflectedField &field, const int d) {
        target << "{\"cppName\":";
        String(target, field.cppName);
        target << ",\"cppType\":";
        String(target, field.cppType);
        target << ",\"reflectionName\":";
        String(target, field.reflectionName);
        target << ",\"access\":";
        String(target, AccessName(field.access));
        target << ",\"source\":";
        Span(target, field.source);
        target << ",\"attributes\":";
        Attributes(target, field.attributes, d + 1);
        target << '}';
      });
  out << ",\"properties\":";
  Array(out, type.properties, depth + 1,
        [](std::ostringstream &target, const ReflectedProperty &property,
           const int d) {
          target << "{\"reflectionName\":";
          String(target, property.reflectionName);
          target << ",\"source\":";
          Span(target, property.source);
          target << ",\"getter\":";
          if (property.getter) {
            Method(target, *property.getter, d + 1);
          } else {
            target << "null";
          }
          target << ",\"setter\":";
          if (property.setter) {
            Method(target, *property.setter, d + 1);
          } else {
            target << "null";
          }
          target << ",\"attributes\":";
          Attributes(target, property.attributes, d + 1);
          target << '}';
        });
  out << ",\"methods\":";
  Array(out, type.methods, depth + 1, Method);
  out << ",\"constructors\":";
  Array(out, type.constructors, depth + 1,
        [](std::ostringstream &target, const ReflectedConstructor &constructor,
           const int d) {
          target << "{\"injectable\":"
                 << (constructor.injectable ? "true" : "false")
                 << ",\"access\":";
          String(target, AccessName(constructor.access));
          target << ",\"source\":";
          Span(target, constructor.source);
          target << ",\"parameters\":";
          Parameters(target, constructor.parameters, d + 1);
          target << ",\"attributes\":";
          Attributes(target, constructor.attributes, d + 1);
          target << '}';
        });
  out << ",\"enumValues\":";
  Array(out, type.enumValues, depth + 1,
        [](std::ostringstream &target, const ReflectedEnumValue &value,
           const int d) {
          target << "{\"cppName\":";
          String(target, value.cppName);
          target << ",\"reflectionName\":";
          String(target, value.reflectionName);
          target << ",\"source\":";
          Span(target, value.source);
          target << ",\"attributes\":";
          Attributes(target, value.attributes, d + 1);
          target << '}';
        });
  out << '}';
}
} // namespace

auto StableHash(const std::string_view text) -> std::string {
  std::uint64_t hash = 14695981039346656037ull;
  for (const unsigned char byte : text) {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  std::ostringstream out{};
  out << std::hex << std::setfill('0') << std::setw(16) << hash;
  return out.str();
}

auto SerializeReflectionModel(const ReflectionModel &model) -> std::string {
  std::ostringstream out{};
  out << "{\n  \"kind\":\"NGIN.ReflectionModel\",\n  \"version\":"
      << model.version << ",\n  \"projectName\":";
  String(out, model.projectName);
  out << ",\n  \"moduleName\":";
  String(out, model.moduleName);
  out << ",\n  \"configuration\":";
  String(out, model.configuration);
  out << ",\n  \"compiler\":";
  String(out, model.compiler);
  out << ",\n  \"cacheKey\":";
  String(out, model.cacheKey);
  out << ",\n  \"headers\":";
  Array(out, model.headers, 1,
        [](std::ostringstream &target, const ReflectedHeader &header, int) {
          target << "{\"path\":";
          String(target, header.path.generic_string());
          target << ",\"normalizedPath\":";
          String(target, header.normalizedPath);
          target << ",\"cacheKey\":";
          String(target, header.cacheKey);
          target << '}';
        });
  out << ",\n  \"types\":";
  Array(out, model.types, 1, Type);
  out << "\n}\n";
  return out.str();
}
} // namespace NGIN::Reflection::MetaGen
