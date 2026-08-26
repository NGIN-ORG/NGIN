#include "MetaGenContext.hpp"

#include "MetaGenEmitter.hpp"
#include "MetaGenInspection.hpp"
#include "MetaGenPreprocessor.hpp"
#include "MetaGenScanner.hpp"

#include <sstream>

namespace NGIN::Reflection::MetaGen {
namespace {
[[nodiscard]] auto MethodPointer(const ReflectedType &type,
                                 const ReflectedMethod &method) -> std::string {
  std::ostringstream out{};
  out << "static_cast<" << method.returnType << " (";
  if (method.isStatic)
    out << '*';
  else
    out << type.cppQualifiedName << "::*";
  out << ") (";
  for (std::size_t index = 0; index < method.parameters.size(); ++index) {
    if (index != 0)
      out << ", ";
    out << method.parameters[index].cppType;
  }
  out << ')';
  if (method.isConst)
    out << " const";
  if (method.isVolatile)
    out << " volatile";
  if (method.isLValueQualified)
    out << " &";
  if (method.isRValueQualified)
    out << " &&";
  if (method.isNoexcept)
    out << " noexcept";
  out << ">(&" << type.cppQualifiedName << "::" << method.cppName << ')';
  return out.str();
}
} // namespace

auto BuildReflectionModel(const MetaGenContext &metaGenContext)
    -> InspectionResult {
  InspectionResult result{};
  if (metaGenContext.sourceFiles.empty()) {
    result.diagnostics.push_back("project '" + metaGenContext.projectName +
                                 "' declares no reflection headers to scan");
    return result;
  }

  PreprocessResult preprocessed = PreprocessHeaders(metaGenContext);
  if (!preprocessed.diagnostics.empty()) {
    result.diagnostics = std::move(preprocessed.diagnostics);
    return result;
  }

  MetaGenContext scannerContext = metaGenContext;
  scannerContext.compiler = preprocessed.compiler;
  ScanResult scanned =
      ScanPreprocessedSource(preprocessed.source, scannerContext);
  if (!scanned.diagnostics.empty()) {
    result.diagnostics = std::move(scanned.diagnostics);
    return result;
  }
  result.model = std::move(scanned.model);
  return result;
}

auto ExplainReflectionModel(const ReflectionModel &model,
                            const std::string_view query) -> std::string {
  std::ostringstream out{};
  out << "ReflectionModel v" << model.version << "\n";
  out << "project: " << model.projectName << "\n";
  out << "compiler: " << model.compiler << "\n";
  out << "cache-key: " << model.cacheKey << "\n";
  std::size_t matches = 0;
  for (const ReflectedType &type : model.types) {
    const bool match =
        query.empty() || type.cppQualifiedName == query ||
        type.reflectionName == query ||
        type.declarationFile.generic_string().find(query) != std::string::npos;
    if (!match)
      continue;
    ++matches;
    out << "\n" << type.cppQualifiedName << "\n";
    out << "  included: NGIN_REFLECT at "
        << type.source.begin.file.generic_string() << ':'
        << type.source.begin.line << "\n";
    out << "  reflected-name: " << type.reflectionName << "\n";
    if (type.manualDescriptor)
      out << "  descriptor: manual NginReflect/Describe<T> escape hatch\n";
    out << "  cache-key: " << StableHash(type.cppQualifiedName + model.cacheKey)
        << "\n";
    for (const ReflectedBase &base : type.bases)
      out << "  base " << base.cppType << ": included public direct base\n";
    for (const ReflectedField &field : type.fields)
      out << "  field " << field.cppName << " -> " << field.reflectionName
          << ": "
          << (field.access == AccessKind::Public ? "included public field"
                                                 : "included NGIN_FIELD")
          << "; expression=&" << type.cppQualifiedName << "::" << field.cppName
          << "\n";
    for (const ReflectedProperty &property : type.properties) {
      out << "  property " << property.reflectionName
          << ": included matching NGIN_PROPERTY accessors";
      if (property.getter)
        out << "; getter=" << MethodPointer(type, *property.getter);
      if (property.setter)
        out << "; setter=" << MethodPointer(type, *property.setter);
      out << "\n";
    }
    for (const ReflectedMethod &method : type.methods)
      out << "  method " << method.cppName << " -> " << method.reflectionName
          << ": included NGIN_METHOD; expression="
          << MethodPointer(type, method) << "\n";
    for (const ReflectedConstructor &constructor : type.constructors)
      out << "  constructor(" << constructor.parameters.size()
          << " parameters): included "
          << (constructor.injectable ? "NGIN_INJECT" : "NGIN_CTOR") << "\n";
    for (const ReflectedEnumValue &value : type.enumValues)
      out << "  enum-value " << value.cppName << " -> " << value.reflectionName
          << ": included enum default policy\n";
  }
  if (matches == 0)
    out << "\nno reflected header or type matched '" << query << "'\n";
  return out.str();
}

auto GenerateMetaData(const MetaGenContext &metaGenContext) -> MetaGenResult {
  MetaGenResult result{};
  if (metaGenContext.outputs.empty()) {
    result.diagnostics.push_back(
        "generator context declares no generated source outputs");
    return result;
  }
  InspectionResult inspected = BuildReflectionModel(metaGenContext);
  if (!inspected.diagnostics.empty()) {
    result.diagnostics = std::move(inspected.diagnostics);
    return result;
  }

  EmitResult emitted = EmitReflection(metaGenContext, inspected.model);
  if (!emitted.diagnostics.empty()) {
    result.diagnostics = std::move(emitted.diagnostics);
    return result;
  }
  result.generatedFiles = std::move(emitted.generatedFiles);
  result.reflectedTypeCount = inspected.model.types.size();
  return result;
}
} // namespace NGIN::Reflection::MetaGen
