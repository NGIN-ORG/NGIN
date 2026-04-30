#include "MetaGenContext.hpp"

#include <NGIN/Serialization/XML/XmlParser.hpp>

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>

namespace NGIN::Reflection::MetaGen
{
    namespace
    {
        using XmlDocument = NGIN::Serialization::XmlDocument;
        using XmlElement = NGIN::Serialization::XmlElement;
        using XmlNode = NGIN::Serialization::XmlNode;
        using XmlParseOptions = NGIN::Serialization::XmlParseOptions;
        using XmlParser = NGIN::Serialization::XmlParser;

        struct LoadedXmlDocument
        {
            std::string text{};
            XmlDocument document{0};
        };

        [[nodiscard]] auto Attribute(const XmlElement &node, const std::string_view key) -> std::string
        {
            const auto *attribute = node.FindAttribute(key);
            return attribute == nullptr ? std::string{} : std::string(attribute->value);
        }

        [[nodiscard]] auto ChildElements(const XmlElement &node, const std::string_view name = {}) -> std::vector<const XmlElement *>
        {
            std::vector<const XmlElement *> out{};
            for (NGIN::UIntSize index = 0; index < node.children.Size(); ++index)
            {
                const auto &child = node.children[index];
                if (child.type == XmlNode::Type::Element && child.element != nullptr &&
                    (name.empty() || child.element->name == name))
                {
                    out.push_back(child.element);
                }
            }
            return out;
        }

        [[nodiscard]] auto FindChild(const XmlElement &node, const std::string_view name) -> const XmlElement *
        {
            for (const auto *child : ChildElements(node))
            {
                if (child->name == name)
                {
                    return child;
                }
            }
            return nullptr;
        }

        [[nodiscard]] auto LoadXml(const fs::path &path, std::vector<std::string> &diagnostics) -> std::optional<LoadedXmlDocument>
        {
            std::ifstream input(path);
            if (!input)
            {
                diagnostics.push_back("failed to open generator context '" + path.string() + "'");
                return std::nullopt;
            }
            LoadedXmlDocument loaded{};
            std::ostringstream text{};
            text << input.rdbuf();
            loaded.text = text.str();

            XmlParseOptions options{};
            options.decodeEntities = true;
            options.arenaBytes = std::max<NGIN::UIntSize>(
                16384, static_cast<NGIN::UIntSize>(loaded.text.size() * 8 + 4096));
            auto parsed = XmlParser::Parse(loaded.text, options);
            if (!parsed)
            {
                diagnostics.push_back("failed to parse generator context '" + path.string() + "'");
                return std::nullopt;
            }
            loaded.document = std::move(parsed.Value());
            return loaded;
        }
    }

    auto ReadContext(const fs::path &path, std::vector<std::string> &diagnostics) -> MetaGenContext
    {
        MetaGenContext context{};
        auto loaded = LoadXml(path, diagnostics);
        if (!loaded.has_value())
        {
            return context;
        }

        const auto *root = loaded->document.Root();
        if (root == nullptr || root->name != "GeneratorContext")
        {
            diagnostics.push_back("generator context root element must be <GeneratorContext>");
            return context;
        }
        if (Attribute(*root, "SchemaVersion") != "1")
        {
            diagnostics.push_back("unsupported generator context schema version '" + Attribute(*root, "SchemaVersion") + "'");
            return context;
        }

        context.generator = Attribute(*root, "Generator");
        context.projectName = Attribute(*root, "Project");
        context.profileName = Attribute(*root, "Profile");
        context.platform = Attribute(*root, "Platform");
        context.buildType = Attribute(*root, "BuildType");
        context.operatingSystem = Attribute(*root, "OperatingSystem");
        context.architecture = Attribute(*root, "Architecture");
        context.environment = Attribute(*root, "Environment");
        context.projectDir = Attribute(*root, "ProjectDir");
        context.outputDir = Attribute(*root, "OutputDir");
        context.generatedDir = Attribute(*root, "GeneratedDir");
        context.languageStandard = Attribute(*root, "LanguageStandard").empty() ? "23" : Attribute(*root, "LanguageStandard");

        if (const auto *sources = FindChild(*root, "Sources"))
        {
            for (const auto *file : ChildElements(*sources, "File"))
            {
                const auto filePath = Attribute(*file, "Path");
                if (filePath.empty())
                {
                    continue;
                }
                context.sourceFiles.emplace_back(filePath);
                if (Attribute(*file, "Role") == "Header")
                {
                    context.sourceRoots.push_back(fs::path(filePath).parent_path().lexically_normal());
                }
            }
        }
        if (const auto *includes = FindChild(*root, "IncludeDirectories"))
        {
            for (const auto *include : ChildElements(*includes, "IncludeDirectory"))
            {
                const auto includePath = Attribute(*include, "Path");
                if (!includePath.empty())
                {
                    context.includeDirectories.emplace_back(includePath);
                    context.sourceRoots.emplace_back(includePath);
                }
            }
        }
        if (const auto *definitions = FindChild(*root, "CompileDefinitions"))
        {
            for (const auto *definition : ChildElements(*definitions, "Definition"))
            {
                const auto value = Attribute(*definition, "Value");
                if (!value.empty())
                {
                    context.compileDefinitions.push_back(value);
                }
            }
        }
        if (const auto *options = FindChild(*root, "CompileOptions"))
        {
            for (const auto *option : ChildElements(*options, "Option"))
            {
                const auto value = Attribute(*option, "Value");
                if (!value.empty())
                {
                    context.compileOptions.push_back(value);
                }
            }
        }
        if (const auto *outputs = FindChild(*root, "Outputs"))
        {
            for (const auto *output : ChildElements(*outputs, "Generated"))
            {
                if (Attribute(*output, "Role") != "Source")
                {
                    continue;
                }
                const auto outputPath = Attribute(*output, "Path");
                if (!outputPath.empty())
                {
                    context.outputs.emplace_back(outputPath);
                }
            }
        }
        if (context.outputs.empty())
        {
            diagnostics.push_back("generator context declares no generated source outputs");
        }
        return context;
    }
}
