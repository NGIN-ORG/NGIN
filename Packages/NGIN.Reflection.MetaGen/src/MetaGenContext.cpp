#include "MetaGenContext.hpp"

#include <NGIN/Serialization/XML/XmlParser.hpp>

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>

namespace NGIN::Reflection::MetaGen {
namespace {
using XmlDocument = NGIN::Serialization::XML::Document;
using XmlElement = NGIN::Serialization::XML::ElementView;
using XmlElementRange = NGIN::Serialization::XML::FilteredChildRange;
using XmlParseResult =
    NGIN::Utilities::Expected<XmlDocument,
                              NGIN::Serialization::ParseDiagnostic>;
using XmlParser = NGIN::Serialization::XML::Parser;

struct LoadedXmlDocument {
  std::string text{};
  XmlDocument document{};
};

[[nodiscard]] auto Attribute(const XmlElement &node, const std::string_view key)
    -> std::string {
  const std::optional<NGIN::Serialization::XML::AttributeView> attribute =
      node.Attribute(key);
  return !attribute.has_value() ? std::string{}
                                : std::string(attribute->Value());
}

[[nodiscard]] auto ChildElements(const XmlElement &node,
                                 const std::string_view name = {})
    -> XmlElementRange {
  return node.Children(name);
}

[[nodiscard]] auto FindChild(const XmlElement &node,
                             const std::string_view name)
    -> std::optional<XmlElement> {
  return node.FirstChild(name);
}

[[nodiscard]] auto LoadXml(const fs::path &path,
                           std::vector<std::string> &diagnostics)
    -> std::optional<LoadedXmlDocument> {
  std::ifstream input(path);
  if (!input) {
    diagnostics.push_back("failed to open generator context '" + path.string() +
                          "'");
    return std::nullopt;
  }
  LoadedXmlDocument loaded{};
  std::ostringstream text{};
  text << input.rdbuf();
  loaded.text = text.str();

  XmlParseResult parsed =
      XmlParser::Parse(NGIN::Serialization::OwnedTextBuffer{loaded.text});
  if (!parsed) {
    diagnostics.push_back("failed to parse generator context '" +
                          path.string() + "'");
    return std::nullopt;
  }
  loaded.document = std::move(parsed.Value());
  return loaded;
}
} // namespace

// namespace

auto ReadContext(const fs::path &path, std::vector<std::string> &diagnostics)
    -> MetaGenContext {
  MetaGenContext context{};
  std::optional<LoadedXmlDocument> loaded = LoadXml(path, diagnostics);
  if (!loaded.has_value()) {
    return context;
  }

  const XmlElement root = loaded->document.Root();
  if (!root.IsValid() || root.Name() != "GeneratorContext") {
    diagnostics.push_back(
        "generator context root element must be <GeneratorContext>");
    return context;
  }
  if (Attribute(root, "Version") != "1") {
    diagnostics.push_back("unsupported generator context version '" +
                          Attribute(root, "Version") + "'");
    return context;
  }

  context.generator = Attribute(root, "Generator");
  context.projectName = Attribute(root, "Project");
  context.profileName = Attribute(root, "Configuration");
  if (context.profileName.empty()) {
    context.profileName = Attribute(root, "Profile");
  }
  context.platform = Attribute(root, "Platform");
  context.optimization = Attribute(root, "Optimization");
  context.debugSymbols = Attribute(root, "DebugSymbols") == "true";
  context.linkTimeOptimization =
      Attribute(root, "LinkTimeOptimization") == "true";
  context.backendConfiguration = Attribute(root, "BackendConfiguration");
  context.operatingSystem = Attribute(root, "OperatingSystem");
  context.architecture = Attribute(root, "Architecture");
  context.environment = Attribute(root, "Environment");
  context.projectDir = Attribute(root, "ProjectDir");
  context.outputDir = Attribute(root, "OutputDir");
  context.generatedDir = Attribute(root, "GeneratedDir");
  context.compilationDatabaseDir = Attribute(root, "CompilationDatabaseDir");
  context.languageStandard = Attribute(root, "LanguageStandard").empty()
                                 ? "23"
                                 : Attribute(root, "LanguageStandard");
  if (context.languageStandard.starts_with("C++")) {
    context.languageStandard.erase(0, 3);
  }
  if (!context.projectDir.empty()) {
    context.sourceRoots.push_back(context.projectDir.lexically_normal());
  }

  if (const std::optional<XmlElement> sources = FindChild(root, "Sources")) {
    for (const XmlElement file : ChildElements(*sources, "File")) {
      const std::string filePath = Attribute(file, "Path");
      if (filePath.empty()) {
        continue;
      }
      context.sourceFiles.emplace_back(filePath);
      if (Attribute(file, "Role") == "Header") {
        context.sourceRoots.push_back(
            fs::path(filePath).parent_path().lexically_normal());
      }
    }
  }
  if (const std::optional<XmlElement> includes =
          FindChild(root, "IncludeDirectories")) {
    for (const XmlElement include :
         ChildElements(*includes, "IncludeDirectory")) {
      const std::string includePath = Attribute(include, "Path");
      if (!includePath.empty()) {
        context.includeDirectories.emplace_back(includePath);
        // Package includes are compiler inputs, not reflection ownership
        // roots.
        if (Attribute(include, "Source") != "package" &&
            Attribute(include, "Package").empty()) {
          context.sourceRoots.emplace_back(includePath);
        }
      }
    }
  }
  if (const std::optional<XmlElement> definitions =
          FindChild(root, "CompileDefinitions")) {
    for (const XmlElement definition :
         ChildElements(*definitions, "Definition")) {
      const std::string value = Attribute(definition, "Value");
      if (!value.empty()) {
        context.compileDefinitions.push_back(value);
      }
    }
  }
  if (const std::optional<XmlElement> options =
          FindChild(root, "CompileOptions")) {
    for (const XmlElement option : ChildElements(*options, "Option")) {
      const std::string value = Attribute(option, "Value");
      if (!value.empty()) {
        context.compileOptions.push_back(value);
      }
    }
  }
  if (const std::optional<XmlElement> arguments =
          FindChild(root, "Arguments")) {
    for (const XmlElement argument : ChildElements(*arguments, "Argument")) {
      const std::string value = Attribute(argument, "Value");
      if (!value.empty()) {
        context.arguments.push_back(value);
      }
    }
  }
  if (const std::optional<XmlElement> options = FindChild(root, "Options")) {
    for (const XmlElement option : ChildElements(*options, "Option")) {
      const std::string name = Attribute(option, "Name");
      if (!name.empty()) {
        context.options[name] = Attribute(option, "Value");
      }
    }
  }
  if (const std::map<std::string, std::string, std::less<>>::const_iterator
          backend = context.options.find("Backend");
      backend != context.options.end() && !backend->second.empty()) {
    context.backend = backend->second;
  }
  if (const std::optional<XmlElement> outputs = FindChild(root, "Outputs")) {
    for (const XmlElement output : ChildElements(*outputs, "Generated")) {
      if (Attribute(output, "Role") != "Source") {
        continue;
      }
      const std::string outputPath = Attribute(output, "Path");
      if (!outputPath.empty()) {
        context.outputs.emplace_back(outputPath);
      }
    }
  }
  if (context.outputs.empty()) {

    diagnostics.push_back(
        "generator context declares no generated source outputs");
  }
  return context;
}
} // namespace NGIN::Reflection::MetaGen
// namespace NGIN::Reflection::MetaGen
