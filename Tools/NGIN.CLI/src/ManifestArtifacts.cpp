#include "ManifestArtifacts.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace NGIN::CLI
{
    namespace
    {
        [[nodiscard]] auto XmlEscape(const std::string_view value) -> std::string
        {
            std::string result{};
            result.reserve(value.size());
            for (const auto ch : value)
            {
                switch (ch)
                {
                case '&': result += "&amp;"; break;
                case '<': result += "&lt;"; break;
                case '>': result += "&gt;"; break;
                case '\"': result += "&quot;"; break;
                default: result += ch; break;
                }
            }
            return result;
        }

        [[nodiscard]] auto JsonEscape(const std::string_view value) -> std::string
        {
            std::string result{};
            result.reserve(value.size() + 2);
            result.push_back('\"');
            for (const auto ch : value)
            {
                switch (ch)
                {
                case '\\': result += "\\\\"; break;
                case '\"': result += "\\\""; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += ch; break;
                }
            }
            result.push_back('\"');
            return result;
        }

        [[nodiscard]] auto TypeName(const std::string_view id) -> std::string
        {
            std::string result{"t_"};
            for (const auto ch : id)
            {
                result.push_back(std::isalnum(static_cast<unsigned char>(ch)) != 0 ? ch : '_');
            }
            return result;
        }

        [[nodiscard]] auto Occurs(const std::optional<std::size_t> maximum) -> std::string
        {
            return maximum.has_value() ? std::to_string(*maximum) : "unbounded";
        }

        [[nodiscard]] auto XsdBuiltinType(const ManifestValueKind kind) -> std::string_view
        {
            switch (kind)
            {
            case ManifestValueKind::Identifier: return "identifier";
            case ManifestValueKind::Boolean: return "boolean";
            case ManifestValueKind::Integer: return "xs:long";
            case ManifestValueKind::SemanticVersion: return "semanticVersion";
            case ManifestValueKind::VersionCompatibility: return "versionCompatibility";
            case ManifestValueKind::String:
            case ManifestValueKind::Path:
            case ManifestValueKind::Enumeration: return "nonEmptyString";
            }
            return "nonEmptyString";
        }

        auto WriteSimpleTypes(std::ostream &out) -> void
        {
            out << "  <xs:simpleType name=\"nonEmptyString\">\n"
                   "    <xs:restriction base=\"xs:string\"><xs:minLength value=\"1\" /></xs:restriction>\n"
                   "  </xs:simpleType>\n"
                   "  <xs:simpleType name=\"identifier\">\n"
                   "    <xs:restriction base=\"xs:string\"><xs:pattern value=\"[A-Za-z0-9][A-Za-z0-9._-]*\" /></xs:restriction>\n"
                   "  </xs:simpleType>\n"
                   "  <xs:simpleType name=\"boolean\">\n"
                   "    <xs:restriction base=\"xs:string\"><xs:enumeration value=\"true\" /><xs:enumeration value=\"false\" /></xs:restriction>\n"
                   "  </xs:simpleType>\n"
                   "  <xs:simpleType name=\"semanticVersion\">\n"
                   "    <xs:restriction base=\"xs:string\"><xs:pattern value=\"(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?(\\+[0-9A-Za-z.-]+)?\" /></xs:restriction>\n"
                   "  </xs:simpleType>\n"
                   "  <xs:simpleType name=\"versionCompatibility\">\n"
                   "    <xs:restriction base=\"xs:string\"><xs:pattern value=\"(0|[1-9][0-9]*)(\\.(0|[1-9][0-9]*))?(\\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?(\\+[0-9A-Za-z.-]+)?)?\" /></xs:restriction>\n"
                   "  </xs:simpleType>\n";
        }

        auto CollectReachable(const ManifestSpec &spec, const std::string &rootId, std::set<std::string> &ids) -> void
        {
            if (!ids.insert(rootId).second)
            {
                return;
            }
            for (const auto &child : spec.Element(rootId).children)
            {
                CollectReachable(spec, child.elementId, ids);
            }
        }

        auto WriteAttribute(std::ostream &out, const ManifestAttributeSpec &attribute, std::string_view typePrefix)
            -> void
        {
            out << "    <xs:attribute name=\"" << XmlEscape(attribute.name) << "\"";
            if (attribute.valueKind != ManifestValueKind::Enumeration)
            {
                const auto type = XsdBuiltinType(attribute.valueKind);
                out << " type=\"";
                if (!type.starts_with("xs:")) out << typePrefix;
                out << type << "\"";
            }
            if (attribute.required)
            {
                out << " use=\"required\"";
            }
            if (attribute.valueKind != ManifestValueKind::Enumeration)
            {
                out << " />\n";
                return;
            }
            out << ">\n      <xs:simpleType><xs:restriction base=\"xs:string\">\n";
            for (const auto &value : attribute.allowedValues)
            {
                out << "        <xs:enumeration value=\"" << XmlEscape(value) << "\" />\n";
            }
            out << "      </xs:restriction></xs:simpleType>\n    </xs:attribute>\n";
        }

        auto WriteElementType(std::ostream &out, const ManifestSpec &spec, const ManifestElementSpec &element,
                              const bool extensionSchema) -> void
        {
            out << "  <xs:complexType name=\"" << TypeName(element.id) << "\"";
            if (element.allowsText && element.children.empty())
            {
                out << " mixed=\"true\"";
            }
            out << ">\n";
            if (!element.children.empty())
            {
                out << "    <xs:choice minOccurs=\"0\" maxOccurs=\"unbounded\">\n";
                for (const auto &child : element.children)
                {
                    const auto &childElement = spec.Element(child.elementId);
                    out << "      <xs:element ";
                    if (!childElement.namespaceUri.empty())
                    {
                        out << "ref=\"cmake:" << XmlEscape(childElement.name) << "\"";
                    }
                    else
                    {
                        out << "name=\"" << XmlEscape(childElement.name) << "\" type=\""
                            << (extensionSchema ? "ngin:" : "")
                            << TypeName(childElement.id) << "\"";
                        if (extensionSchema)
                        {
                            out << " form=\"unqualified\"";
                        }
                    }
                    out << " minOccurs=\"0\" maxOccurs=\"unbounded\" />\n";
                }
                out << "    </xs:choice>\n";
            }
            for (const auto &attribute : element.attributes)
            {
                WriteAttribute(out, attribute, extensionSchema ? "ngin:" : "");
            }
            for (const auto &child : element.children)
            {
                const auto &childElement = spec.Element(child.elementId);
                const auto qualifiedName = childElement.namespaceUri.empty()
                                               ? childElement.name
                                               : "cmake:" + childElement.name;
                if (child.minimum > 0)
                {
                    out << "    <xs:assert test=\"count(" << qualifiedName << ") ge " << child.minimum
                        << "\" />\n";
                }
                if (child.maximum.has_value())
                {
                    out << "    <xs:assert test=\"count(" << qualifiedName << ") le " << *child.maximum
                        << "\" />\n";
                }
            }
            out << "  </xs:complexType>\n";
        }

        [[nodiscard]] auto GenerateCoreXsd(const ManifestSpec &spec, const ManifestDocumentSpec &document) -> std::string
        {
            std::set<std::string> reachable{};
            CollectReachable(spec, document.rootElementId, reachable);
            const auto importsCMake = std::ranges::any_of(reachable, [&](const std::string &id) {
                return spec.Element(id).namespaceUri == CMakeIntegrationNamespace;
            });
            std::ostringstream out;
            out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                   "<!-- Generated from ManifestSpec. Do not edit. -->\n"
                   "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\"\n"
                   "           xmlns:vc=\"http://www.w3.org/2007/XMLSchema-versioning\"";
            if (importsCMake)
            {
                out << "\n           xmlns:cmake=\"" << CMakeIntegrationNamespace << "\"";
            }
            out << "\n           vc:minVersion=\"1.1\" elementFormDefault=\"unqualified\" attributeFormDefault=\"unqualified\">\n";
            if (importsCMake)
            {
                out << "  <xs:import namespace=\"" << CMakeIntegrationNamespace
                    << "\" schemaLocation=\"cmake-integration.xsd\" />\n";
            }
            WriteSimpleTypes(out);
            for (const auto &element : spec.Elements())
            {
                if (reachable.contains(element.id) && element.namespaceUri.empty())
                {
                    WriteElementType(out, spec, element, false);
                }
            }
            const auto &root = spec.Element(document.rootElementId);
            out << "  <xs:element name=\"" << root.name << "\" type=\"" << TypeName(root.id) << "\" />\n"
                   "</xs:schema>\n";
            return out.str();
        }

        [[nodiscard]] auto GenerateIntegrationXsd(const ManifestSpec &spec, const ManifestNamespaceSpec &namespaceSpec)
            -> std::string
        {
            std::set<std::string> reachable{};
            for (const auto &root : namespaceSpec.rootElementIds)
            {
                CollectReachable(spec, root, reachable);
            }
            std::ostringstream out;
            out << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                   "<!-- Generated from ManifestSpec. Do not edit. -->\n"
                   "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\"\n"
                   "           xmlns:vc=\"http://www.w3.org/2007/XMLSchema-versioning\"\n"
                   "           xmlns:ngin=\""
                << namespaceSpec.uri << "\"\n           xmlns:cmake=\"" << namespaceSpec.uri
                << "\"\n           targetNamespace=\"" << namespaceSpec.uri
                << "\" vc:minVersion=\"1.1\" elementFormDefault=\"qualified\" attributeFormDefault=\"unqualified\">\n";
            WriteSimpleTypes(out);
            for (const auto &element : spec.Elements())
            {
                if (reachable.contains(element.id))
                {
                    WriteElementType(out, spec, element, true);
                }
            }
            for (const auto &rootId : namespaceSpec.rootElementIds)
            {
                const auto &root = spec.Element(rootId);
                out << "  <xs:element name=\"" << root.name << "\" type=\"ngin:" << TypeName(root.id) << "\" />\n";
            }
            for (const auto &element : spec.Elements())
            {
                if (reachable.contains(element.id) && !element.namespaceUri.empty() &&
                    std::ranges::find(namespaceSpec.rootElementIds, element.id) == namespaceSpec.rootElementIds.end())
                {
                    out << "  <xs:element name=\"" << element.name << "\" type=\"ngin:" << TypeName(element.id)
                        << "\" />\n";
                }
            }
            out << "</xs:schema>\n";
            return out.str();
        }

        [[nodiscard]] auto GenerateEditorMetadata(const ManifestSpec &spec) -> std::string
        {
            std::ostringstream out;
            out << "{\n  \"generatedFrom\": \"ManifestSpec\",\n  \"documents\": [\n";
            for (std::size_t index = 0; index < spec.Documents().size(); ++index)
            {
                const auto &document = spec.Documents()[index];
                out << "    {\"kind\": " << JsonEscape(ManifestDocumentKindName(document.kind))
                    << ", \"extension\": " << JsonEscape(document.extension) << ", \"root\": "
                    << JsonEscape(spec.Element(document.rootElementId).name) << ", \"schema\": "
                    << JsonEscape(document.schemaFile) << "}" << (index + 1 == spec.Documents().size() ? "\n" : ",\n");
            }
            out << "  ],\n  \"namespaces\": [\n";
            for (std::size_t index = 0; index < spec.Namespaces().size(); ++index)
            {
                const auto &namespaceSpec = spec.Namespaces()[index];
                out << "    {\"uri\": " << JsonEscape(namespaceSpec.uri) << ", \"prefix\": "
                    << JsonEscape(namespaceSpec.preferredPrefix) << ", \"schema\": "
                    << JsonEscape(namespaceSpec.schemaFile) << "}"
                    << (index + 1 == spec.Namespaces().size() ? "\n" : ",\n");
            }
            out << "  ],\n  \"elements\": [\n";
            for (std::size_t index = 0; index < spec.Elements().size(); ++index)
            {
                const auto &element = spec.Elements()[index];
                out << "    {\"id\": " << JsonEscape(element.id) << ", \"name\": " << JsonEscape(element.name)
                    << ", \"namespace\": " << JsonEscape(element.namespaceUri) << ", \"documentation\": "
                    << JsonEscape(element.documentation) << ", \"semanticValidator\": "
                    << JsonEscape(element.semanticValidatorHook) << ", \"graphProjection\": "
                    << JsonEscape(element.graphProjection) << ", \"attributes\": [";
                for (std::size_t attributeIndex = 0; attributeIndex < element.attributes.size(); ++attributeIndex)
                {
                    const auto &attribute = element.attributes[attributeIndex];
                    if (attributeIndex != 0) out << ", ";
                    out << "{\"name\": " << JsonEscape(attribute.name) << ", \"type\": "
                        << JsonEscape(ManifestValueKindName(attribute.valueKind)) << ", \"required\": "
                        << (attribute.required ? "true" : "false") << "}";
                }
                out << "], \"children\": [";
                for (std::size_t childIndex = 0; childIndex < element.children.size(); ++childIndex)
                {
                    const auto &child = element.children[childIndex];
                    if (childIndex != 0) out << ", ";
                    out << "{\"id\": " << JsonEscape(child.elementId) << ", \"min\": " << child.minimum
                        << ", \"max\": ";
                    if (child.maximum.has_value()) out << *child.maximum;
                    else out << "null";
                    out << "}";
                }
                out << "]}" << (index + 1 == spec.Elements().size() ? "\n" : ",\n");
            }
            out << "  ]\n}\n";
            return out.str();
        }
    }

    auto GenerateManifestArtifacts(const ManifestSpec &spec) -> std::map<std::string, std::string>
    {
        std::map<std::string, std::string> artifacts{};
        for (const auto &document : spec.Documents())
        {
            artifacts.emplace(document.schemaFile, GenerateCoreXsd(spec, document));
        }
        for (const auto &namespaceSpec : spec.Namespaces())
        {
            artifacts.emplace(namespaceSpec.schemaFile, GenerateIntegrationXsd(spec, namespaceSpec));
        }
        artifacts.emplace("manifest-editor-metadata.json", GenerateEditorMetadata(spec));
        return artifacts;
    }

    auto WriteManifestArtifacts(const std::filesystem::path &directory, const ManifestSpec &spec) -> void
    {
        std::filesystem::create_directories(directory);
        for (const auto &[name, contents] : GenerateManifestArtifacts(spec))
        {
            std::ofstream output(directory / name, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                throw std::runtime_error("failed to write generated manifest artifact: " + (directory / name).string());
            }
            output << contents;
        }
    }
}
