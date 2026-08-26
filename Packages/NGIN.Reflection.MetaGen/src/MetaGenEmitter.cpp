#include "MetaGenEmitter.hpp"

#include "MetaGenCommon.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

namespace NGIN::Reflection::MetaGen {
namespace {
[[nodiscard]] auto ReadText(const fs::path &path)
    -> std::optional<std::string> {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  std::ostringstream text{};
  text << input.rdbuf();
  return text.str();
}

[[nodiscard]] auto WriteIfChanged(const fs::path &path,
                                  const std::string_view content) -> bool {
  if (const std::optional<std::string> existing = ReadText(path);
      existing && *existing == content) {
    return true;
  }
  std::error_code error{};
  fs::create_directories(path.parent_path(), error);
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return false;
  }
  output << content;
  return static_cast<bool>(output);
}

[[nodiscard]] auto MetadataExpression(const MetadataValue &value)
    -> std::string {
  switch (value.kind) {
  case MetadataValueKind::Boolean:
  case MetadataValueKind::Integer:
  case MetadataValueKind::FloatingPoint:
    return value.text;
  case MetadataValueKind::String:
  case MetadataValueKind::Identifier:
    return "std::string{\"" + EscapeCppString(value.text) + "\"}";
  }
  return "std::string{}";
}

[[nodiscard]] auto ParameterList(const ReflectedMethod &method) -> std::string {
  std::ostringstream out{};
  for (std::size_t index = 0; index < method.parameters.size(); ++index) {
    if (index != 0) {
      out << ", ";
    }
    out << method.parameters[index].cppType;
  }
  return out.str();
}

[[nodiscard]] auto MethodPointerType(const ReflectedType &type,
                                     const ReflectedMethod &method)
    -> std::string {
  std::ostringstream out{};
  if (method.isStatic) {
    out << method.returnType << " (*) (" << ParameterList(method) << ')';
  } else {
    out << method.returnType << " (" << type.cppQualifiedName << "::*) ("
        << ParameterList(method) << ')';
    if (method.isConst)
      out << " const";
    if (method.isVolatile)
      out << " volatile";
    if (method.isLValueQualified)
      out << " &";
    if (method.isRValueQualified)
      out << " &&";
  }
  if (method.isNoexcept)
    out << " noexcept";
  return out.str();
}

[[nodiscard]] auto MethodPointer(const ReflectedType &type,
                                 const ReflectedMethod &method) -> std::string {
  return "static_cast<" + MethodPointerType(type, method) + ">(&" +
         type.cppQualifiedName + "::" + method.cppName + ')';
}

auto EmitAttributes(std::ostringstream &out, const AttributeSet &attributes,
                    const std::string_view receiver,
                    const std::string_view templateArgument = {}) -> void {
  for (const auto &[name, value] : attributes) {
    out << "      type." << receiver;
    if (!templateArgument.empty()) {
      out << '<' << templateArgument << '>';
    }
    out << "(\"" << EscapeCppString(name) << "\", " << MetadataExpression(value)
        << ");\n";
  }
}

[[nodiscard]] auto ConstructorBinding(const ReflectedParameter &parameter,
                                      const bool injectable) -> std::string {
  if (!injectable ||
      (parameter.dependencyName.empty() && !parameter.optional)) {
    return parameter.cppType;
  }
  if (!parameter.dependencyName.empty() && parameter.optional) {
    return "NGIN::Reflection::NamedOptionalConstructorDependency<" +
           parameter.cppType + ", \"" +
           EscapeCppString(parameter.dependencyName) + "\">";
  }
  if (!parameter.dependencyName.empty()) {
    return "NGIN::Reflection::NamedConstructorDependency<" + parameter.cppType +
           ", \"" + EscapeCppString(parameter.dependencyName) + "\">";
  }
  return "NGIN::Reflection::OptionalConstructorDependency<" +
         parameter.cppType + ">";
}

auto EmitType(std::ostringstream &out, const ReflectedType &type) -> void {
  out << "  template <>\n";
  out << "  struct GeneratedDescriptor<" << type.cppQualifiedName << ">\n";
  out << "  {\n";
  out << "    static void Build(NGIN::Reflection::TypeBuilder<"
      << type.cppQualifiedName << "> &type)\n";
  out << "    {\n";
  out << "      type.SetName(\"" << EscapeCppString(type.reflectionName)
      << "\");\n";
  EmitAttributes(out, type.attributes, "Attribute");
  if (type.kind == TypeKind::Enum) {
    for (const ReflectedEnumValue &value : type.enumValues) {
      out << "      type.EnumValue(\"" << EscapeCppString(value.reflectionName)
          << "\", " << type.cppQualifiedName << "::" << value.cppName << ");\n";
    }
  } else {
    for (const ReflectedBase &base : type.bases) {
      out << "      type.Base<" << base.cppType << ">();\n";
    }
    for (const ReflectedField &field : type.fields) {
      const std::string pointer =
          "&" + type.cppQualifiedName + "::" + field.cppName;
      out << "      type.Field<" << pointer << ">(\""
          << EscapeCppString(field.reflectionName) << "\");\n";
      EmitAttributes(out, field.attributes, "FieldAttribute", pointer);
    }
    for (const ReflectedProperty &property : type.properties) {
      if (!property.getter) {
        continue;
      }
      const std::string getter = MethodPointer(type, *property.getter);
      out << "      type.Property<" << getter;
      if (property.setter) {
        out << ", " << MethodPointer(type, *property.setter);
      }
      out << ">(\"" << EscapeCppString(property.reflectionName) << "\");\n";
      EmitAttributes(out, property.attributes, "PropertyAttribute", getter);
    }
    for (const ReflectedMethod &method : type.methods) {
      const std::string pointer = MethodPointer(type, method);
      out << "      type.Method<" << pointer << ">(\""
          << EscapeCppString(method.reflectionName) << "\");\n";
      EmitAttributes(out, method.attributes, "MethodAttribute", pointer);
    }
    for (const ReflectedConstructor &constructor : type.constructors) {
      out << "      type."
          << (constructor.injectable ? "InjectableConstructor<"
                                     : "Constructor<");
      for (std::size_t index = 0; index < constructor.parameters.size();
           ++index) {
        if (index != 0) {
          out << ", ";
        }
        out << ConstructorBinding(constructor.parameters[index],
                                  constructor.injectable);
      }
      out << ">();\n";
      EmitAttributes(out, constructor.attributes, "ConstructorAttribute");
    }
  }
  out << "    }\n";
  out << "  };\n\n";
}

[[nodiscard]] auto HeaderIdentifier(const ReflectedHeader &header)
    -> std::string {
  return "Header_" + StableHash(header.normalizedPath);
}

[[nodiscard]] auto EmitHeader(const ReflectedHeader &header,
                              const ReflectionModel &model) -> std::string {
  std::ostringstream out{};
  out << "// <auto-generated>\n";
  out << "// Generated by NGIN MetaGen scanner. Do not edit by hand.\n";
  out << "// Source: " << header.normalizedPath << "\n";
  out << "// Cache-Key: " << header.cacheKey << "\n";
  out << "// </auto-generated>\n\n";
  out << "namespace NGIN::Reflection::detail\n{\n";
  for (const ReflectedType &type : model.types) {
    if (type.declarationFile.empty() ||
        fs::weakly_canonical(type.declarationFile) !=
            fs::weakly_canonical(header.path)) {
      continue;
    }
    if (!type.manualDescriptor)
      EmitType(out, type);
  }
  out << "  inline void Register_" << HeaderIdentifier(header)
      << "(NGIN::Reflection::ModuleRegistration &module)\n  {\n";
  for (const ReflectedType &type : model.types) {
    if (!type.declarationFile.empty() &&
        fs::weakly_canonical(type.declarationFile) ==
            fs::weakly_canonical(header.path)) {
      out << "    module.RegisterType<" << type.cppQualifiedName << ">();\n";
    }
  }
  out << "  }\n";
  out << "} // namespace NGIN::Reflection::detail\n";
  return out.str();
}

[[nodiscard]] auto EmitAggregate(const MetaGenContext &context,
                                 const ReflectionModel &model,
                                 const std::vector<fs::path> &headerUnits)
    -> std::string {
  std::ostringstream out{};
  const std::string functionName =
      "Register_" + SanitizeIdentifier(context.projectName) + "_" +
      SanitizeIdentifier(context.profileName) + "_Reflection";
  out << "// <auto-generated>\n";
  out << "// Generated by NGIN MetaGen scanner. Do not edit by hand.\n";
  out << "// ReflectionModel-Version: " << model.version << "\n";
  out << "// Cache-Key: " << model.cacheKey << "\n";
  out << "// </auto-generated>\n\n";
  out << "#include <NGIN/Reflection/Reflection.hpp>\n\n";
  for (const ReflectedHeader &header : model.headers) {
    out << "#include \"" << EscapeCppString(header.path.generic_string())
        << "\"\n";
  }
  out << '\n';
  for (const fs::path &unit : headerUnits) {
    out << "#include \""
        << EscapeCppString(
               unit.lexically_relative(context.outputs.front().parent_path())
                   .generic_string())
        << "\"\n";
  }
  out << "\nnamespace\n{\n";
  out << "  bool RegisterGeneratedReflectionModule()\n  {\n";
  out << "    return NGIN::Reflection::EnsureModuleInitialized(\n";
  out << "      \"" << EscapeCppString(model.moduleName) << "\",\n";
  out << "      [](NGIN::Reflection::ModuleRegistration &module)\n      {\n";
  for (const ReflectedHeader &header : model.headers) {
    out << "        NGIN::Reflection::detail::Register_"
        << HeaderIdentifier(header) << "(module);\n";
  }
  out << "      });\n  }\n\n";
  out << "  [[maybe_unused]] const bool g_registeredGeneratedReflectionModule "
         "= "
         "RegisterGeneratedReflectionModule();\n";
  out << "} // namespace\n\n";
  out << "extern \"C\" bool " << functionName << "()\n{\n";
  out << "  return RegisterGeneratedReflectionModule();\n}\n";
  return out.str();
}
} // namespace

auto EmitReflection(const MetaGenContext &context, const ReflectionModel &model)
    -> EmitResult {
  EmitResult result{};
  if (context.outputs.empty()) {
    result.diagnostics.push_back(
        "generator context declares no generated source output");
    return result;
  }
  const fs::path aggregatePath = context.outputs.front();
  const fs::path unitDirectory =
      aggregatePath.parent_path() / "reflection.headers";
  std::error_code error{};
  fs::create_directories(unitDirectory, error);
  if (error) {
    result.diagnostics.push_back(
        "failed to create generated reflection directory '" +
        unitDirectory.string() + "'");
    return result;
  }

  std::vector<fs::path> headerUnits{};
  std::set<fs::path> ownedUnits{};
  for (const ReflectedHeader &header : model.headers) {
    const fs::path unit =
        unitDirectory / (HeaderIdentifier(header) + ".reflection.inc");
    if (!WriteIfChanged(unit, EmitHeader(header, model))) {
      result.diagnostics.push_back(
          "failed to write generated reflection unit '" + unit.string() + "'");
      return result;
    }
    headerUnits.push_back(unit);
    ownedUnits.insert(unit.lexically_normal());
    result.generatedFiles.push_back(unit);
  }
  for (const fs::directory_entry &entry :
       fs::directory_iterator(unitDirectory, error)) {
    if (error) {
      break;
    }
    if (entry.is_regular_file() && entry.path().extension() == ".inc" &&
        !ownedUnits.contains(entry.path().lexically_normal())) {
      fs::remove(entry.path(), error);
      if (error) {
        result.diagnostics.push_back(
            "failed to remove stale generated reflection unit '" +
            entry.path().string() + "'");
        return result;
      }
    }
  }

  if (!WriteIfChanged(aggregatePath,
                      EmitAggregate(context, model, headerUnits))) {
    result.diagnostics.push_back(
        "failed to write generated reflection aggregate '" +
        aggregatePath.string() + "'");
    return result;
  }
  result.generatedFiles.insert(result.generatedFiles.begin(), aggregatePath);

  fs::path modelPath = aggregatePath;
  modelPath += ".model.json";
  if (!WriteIfChanged(modelPath, SerializeReflectionModel(model))) {
    result.diagnostics.push_back("failed to write ReflectionModel JSON '" +
                                 modelPath.string() + "'");
    return result;
  }
  result.generatedFiles.push_back(modelPath);

  const fs::path manifestPath =
      aggregatePath.parent_path() / "reflection.generated.manifest.json";
  std::ostringstream manifest{};
  manifest << "{\"kind\":\"NGIN.ReflectionGeneratedManifest\",\"version\":1,"
              "\"cacheKey\":\""
           << EscapeJsonString(model.cacheKey) << "\",\"outputs\":[";
  for (std::size_t index = 0; index < result.generatedFiles.size(); ++index) {
    if (index != 0)
      manifest << ',';
    manifest << '\"'
             << EscapeJsonString(result.generatedFiles[index].generic_string())
             << '\"';
  }
  manifest << "]}\n";
  if (!WriteIfChanged(manifestPath, manifest.str())) {
    result.diagnostics.push_back(
        "failed to write generated reflection manifest '" +
        manifestPath.string() + "'");
    return result;
  }
  result.generatedFiles.push_back(manifestPath);
  return result;
}
} // namespace NGIN::Reflection::MetaGen
