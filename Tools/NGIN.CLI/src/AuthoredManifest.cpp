#include "AuthoredManifest.hpp"

#include "Support.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <map>
#include <regex>
#include <sstream>
#include <system_error>

namespace NGIN::CLI
{
    namespace
    {
        inline constexpr std::string_view XmlNamespace = "http://www.w3.org/XML/1998/namespace";
        inline constexpr std::string_view XmlnsNamespace = "http://www.w3.org/2000/xmlns/";

        struct QualifiedName
        {
            std::string prefix{};
            std::string local{};
        };

        class SourceMap
        {
        public:
            SourceMap(std::string_view text, fs::path path) : m_text(text), m_path(std::move(path))
            {
                m_lines.push_back(0);
                for (std::size_t index = 0; index < text.size(); ++index)
                {
                    if (text[index] == '\n')
                    {
                        m_lines.push_back(index + 1);
                    }
                }
            }

            [[nodiscard]] auto Position(const std::size_t offset) const -> ManifestSourcePosition
            {
                const auto bounded = std::min(offset, m_text.size());
                const auto next = std::upper_bound(m_lines.begin(), m_lines.end(), bounded);
                const auto lineIndex = static_cast<std::size_t>(std::distance(m_lines.begin(), next) - 1);
                return ManifestSourcePosition{
                    .offset = bounded,
                    .line = lineIndex + 1,
                    .column = bounded - m_lines[lineIndex] + 1,
                };
            }

            [[nodiscard]] auto Range(const NGIN::Serialization::SourceSpan span) const -> ManifestSourceRange
            {
                return ManifestSourceRange{.path = m_path, .begin = Position(span.begin), .end = Position(span.end)};
            }

            [[nodiscard]] auto Point(const std::size_t offset) const -> ManifestSourceRange
            {
                const auto position = Position(offset);
                return ManifestSourceRange{.path = m_path, .begin = position, .end = position};
            }

        private:
            std::string_view m_text{};
            fs::path m_path{};
            std::vector<std::size_t> m_lines{};
        };

        [[nodiscard]] auto SplitName(const std::string_view name) -> QualifiedName
        {
            const auto separator = name.find(':');
            if (separator == std::string_view::npos)
            {
                return QualifiedName{.local = std::string(name)};
            }
            return QualifiedName{.prefix = std::string(name.substr(0, separator)),
                                 .local = std::string(name.substr(separator + 1))};
        }

        [[nodiscard]] auto Trim(const std::string_view value) -> std::string
        {
            const auto first = std::find_if_not(value.begin(), value.end(), [](const unsigned char ch) {
                return std::isspace(ch) != 0;
            });
            const auto last = std::find_if_not(value.rbegin(), value.rend(), [](const unsigned char ch) {
                                  return std::isspace(ch) != 0;
                              }).base();
            return first < last ? std::string(first, last) : std::string{};
        }

        auto AddDiagnostic(std::vector<ManifestDiagnostic> &diagnostics, std::string code, std::string message,
                           ManifestSourceRange source) -> void
        {
            diagnostics.push_back(ManifestDiagnostic{
                .severity = ManifestDiagnosticSeverity::Error,
                .code = std::move(code),
                .message = std::move(message),
                .source = std::move(source),
            });
        }

        [[nodiscard]] auto ValidateScalar(const ManifestAttributeSpec &attribute, const std::string_view value)
            -> bool
        {
            if (value.empty())
            {
                return false;
            }
            switch (attribute.valueKind)
            {
            case ManifestValueKind::String:
            case ManifestValueKind::Path: return true;
            case ManifestValueKind::Identifier:
            {
                static const std::regex pattern(R"([A-Za-z0-9][A-Za-z0-9._-]*)");
                return std::regex_match(value.begin(), value.end(), pattern);
            }
            case ManifestValueKind::Boolean: return value == "true" || value == "false";
            case ManifestValueKind::Integer:
            {
                std::int64_t parsed{};
                const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
                return result.ec == std::errc{} && result.ptr == value.data() + value.size();
            }
            case ManifestValueKind::SemanticVersion:
            {
                static const std::regex pattern(
                    R"((0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?)");
                return std::regex_match(value.begin(), value.end(), pattern);
            }
            case ManifestValueKind::VersionCompatibility:
            {
                static const std::regex pattern(
                    R"((0|[1-9][0-9]*)(\.(0|[1-9][0-9]*))?(\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?(\+[0-9A-Za-z.-]+)?)?)");
                return std::regex_match(value.begin(), value.end(), pattern);
            }
            case ManifestValueKind::Enumeration:
                return std::ranges::find(attribute.allowedValues, value) != attribute.allowedValues.end();
            }
            return false;
        }

        [[nodiscard]] auto ExpectedValue(const ManifestAttributeSpec &attribute) -> std::string
        {
            if (attribute.valueKind != ManifestValueKind::Enumeration)
            {
                return std::string(ManifestValueKindName(attribute.valueKind));
            }
            std::ostringstream out;
            for (std::size_t index = 0; index < attribute.allowedValues.size(); ++index)
            {
                if (index != 0)
                {
                    out << ", ";
                }
                out << attribute.allowedValues[index];
            }
            return out.str();
        }

        struct ParseContext
        {
            const ManifestSpec &spec;
            const SourceMap &sourceMap;
            std::vector<ManifestDiagnostic> &diagnostics;
        };

        using NamespaceMap = std::map<std::string, std::string, std::less<>>;

        [[nodiscard]] auto NamespaceDeclarations(const XmlElement &element, NamespaceMap inherited,
                                                 ParseContext &context) -> NamespaceMap
        {
            for (const auto attribute : element.Attributes())
            {
                const auto name = std::string(attribute.Name());
                std::optional<std::string> prefix{};
                if (name == "xmlns")
                {
                    prefix = "";
                }
                else if (name.starts_with("xmlns:"))
                {
                    prefix = name.substr(6);
                }
                if (!prefix.has_value())
                {
                    continue;
                }

                const auto uri = std::string(attribute.Value());
                if (!uri.empty() && uri != XmlNamespace && uri != XmlnsNamespace && context.spec.FindNamespace(uri) == nullptr)
                {
                    AddDiagnostic(context.diagnostics, "NGIN1002",
                                  "unregistered manifest extension namespace '" + uri + "'",
                                  context.sourceMap.Range(attribute.ValueSpan()));
                }
                inherited[*prefix] = uri;
            }
            return inherited;
        }

        [[nodiscard]] auto ResolveNamespace(const QualifiedName &name, const NamespaceMap &namespaces,
                                            const ManifestSourceRange &source, ParseContext &context,
                                            const bool attribute) -> std::optional<std::string>
        {
            if (name.prefix.empty())
            {
                if (attribute)
                {
                    return std::string{};
                }
                const auto found = namespaces.find("");
                return found == namespaces.end() ? std::optional<std::string>{std::string{}}
                                                  : std::optional<std::string>{found->second};
            }
            if (name.prefix == "xml")
            {
                return std::string(XmlNamespace);
            }
            const auto found = namespaces.find(name.prefix);
            if (found == namespaces.end())
            {
                AddDiagnostic(context.diagnostics, "NGIN1002",
                              "XML prefix '" + name.prefix + "' is not bound to a registered namespace", source);
                return std::nullopt;
            }
            return found->second;
        }

        [[nodiscard]] auto ParseElement(const XmlElement &xml, const ManifestElementSpec &elementSpec,
                                        NamespaceMap namespaces, ParseContext &context,
                                        const bool declarationsApplied = false) -> AuthoredElement
        {
            if (!declarationsApplied)
            {
                namespaces = NamespaceDeclarations(xml, std::move(namespaces), context);
            }
            AuthoredElement authored{
                .specId = elementSpec.id,
                .name = elementSpec.name,
                .namespaceUri = elementSpec.namespaceUri,
                .source = context.sourceMap.Range(xml.Span()),
            };

            for (const auto xmlAttribute : xml.Attributes())
            {
                const auto rawName = std::string(xmlAttribute.Name());
                if (rawName == "xmlns" || rawName.starts_with("xmlns:"))
                {
                    continue;
                }
                const auto qualified = SplitName(rawName);
                const auto source = context.sourceMap.Range(xmlAttribute.Span());
                const auto namespaceUri = ResolveNamespace(qualified, namespaces, source, context, true);
                if (!namespaceUri.has_value())
                {
                    continue;
                }
                if (!namespaceUri->empty())
                {
                    AddDiagnostic(context.diagnostics, "NGIN1004",
                                  "namespaced attribute '" + rawName + "' is not allowed on <" + elementSpec.name + ">",
                                  source);
                    continue;
                }

                const auto found = std::ranges::find(elementSpec.attributes, qualified.local,
                                                     &ManifestAttributeSpec::name);
                if (found == elementSpec.attributes.end())
                {
                    AddDiagnostic(context.diagnostics, "NGIN1004",
                                  "unknown attribute '" + qualified.local + "' on <" + elementSpec.name + ">",
                                  context.sourceMap.Range(xmlAttribute.NameSpan()));
                    continue;
                }
                const auto value = std::string(xmlAttribute.Value());
                if (!ValidateScalar(*found, value))
                {
                    AddDiagnostic(context.diagnostics, "NGIN1007",
                                  "invalid value '" + value + "' for attribute '" + found->name + "'; expected " +
                                      ExpectedValue(*found),
                                  context.sourceMap.Range(xmlAttribute.ValueSpan()));
                }
                authored.attributes.push_back(AuthoredAttribute{
                    .name = qualified.local,
                    .namespaceUri = *namespaceUri,
                    .value = value,
                    .source = source,
                });
            }

            for (const auto &attribute : elementSpec.attributes)
            {
                if (attribute.required && authored.Attribute(attribute.name) == nullptr)
                {
                    AddDiagnostic(context.diagnostics, "NGIN1005",
                                  "missing required attribute '" + attribute.name + "' on <" + elementSpec.name + ">",
                                  authored.source);
                }
            }

            std::map<std::string, std::size_t, std::less<>> childCounts{};
            std::string text{};
            for (const auto childNode : xml.Children())
            {
                if (const auto childElement = childNode.TryElement(); childElement.has_value())
                {
                    auto childNamespaces = NamespaceDeclarations(*childElement, namespaces, context);
                    const auto qualified = SplitName(childElement->Name());
                    const auto childSource = context.sourceMap.Range(childElement->Span());
                    const auto namespaceUri = ResolveNamespace(qualified, childNamespaces, childSource, context, false);
                    if (!namespaceUri.has_value())
                    {
                        continue;
                    }
                    if (!namespaceUri->empty() && context.spec.FindNamespace(*namespaceUri) == nullptr)
                    {
                        continue;
                    }
                    const auto *childSpec = context.spec.FindChild(elementSpec, *namespaceUri, qualified.local);
                    if (childSpec == nullptr)
                    {
                        const auto code = namespaceUri->empty() ? "NGIN1003" : "NGIN1009";
                        const auto description = namespaceUri->empty()
                                                     ? "unknown or misplaced core element <" + qualified.local + ">"
                                                     : "extension element <" + qualified.local + "> is not allowed under <" +
                                                           elementSpec.name + ">";
                        AddDiagnostic(context.diagnostics, code, description, childSource);
                        continue;
                    }
                    authored.children.push_back(ParseElement(*childElement, *childSpec, childNamespaces, context, true));
                    ++childCounts[childSpec->id];
                    continue;
                }
                if (const auto childText = childNode.TryText(); childText.has_value())
                {
                    text += std::string(*childText);
                }
            }
            authored.text = Trim(text);
            if (!elementSpec.allowsText && !authored.text.empty())
            {
                AddDiagnostic(context.diagnostics, "NGIN1008",
                              "text content is not allowed in <" + elementSpec.name + ">", authored.source);
            }

            for (const auto &child : elementSpec.children)
            {
                const auto count = childCounts[child.elementId];
                const auto &childSpec = context.spec.Element(child.elementId);
                if (count < child.minimum)
                {
                    AddDiagnostic(context.diagnostics, "NGIN1006",
                                  "<" + elementSpec.name + "> requires " + std::to_string(child.minimum) +
                                      " <" + childSpec.name + "> element(s)",
                                  authored.source);
                }
                if (child.maximum.has_value() && count > *child.maximum)
                {
                    AddDiagnostic(context.diagnostics, "NGIN1006",
                                  "<" + elementSpec.name + "> allows at most " +
                                      std::to_string(*child.maximum) + " <" + childSpec.name + "> element(s)",
                                  authored.source);
                }
            }
            return authored;
        }

        [[nodiscard]] auto KindFromRoot(const std::string_view localName) -> std::optional<ManifestDocumentKind>
        {
            if (localName == "Project") return ManifestDocumentKind::Project;
            if (localName == "Package") return ManifestDocumentKind::Package;
            if (localName == "Workspace") return ManifestDocumentKind::Workspace;
            return std::nullopt;
        }

        [[nodiscard]] auto ManifestIdentity(const fs::path &path) -> AuthoredManifestIdentity
        {
            std::error_code error{};
            auto canonical = fs::weakly_canonical(path, error);
            if (error)
            {
                canonical = fs::absolute(path, error).lexically_normal();
            }
            return AuthoredManifestIdentity{.path = path, .canonicalPath = canonical.generic_string()};
        }

        [[nodiscard]] auto MakeTypedManifest(const ManifestDocumentKind kind, const fs::path &path,
                                             AuthoredElement root) -> AuthoredManifest
        {
            const auto identity = ManifestIdentity(path);
            const auto value = [&](const std::string_view name) -> std::string {
                const auto *attribute = root.Attribute(name);
                return attribute == nullptr ? std::string{} : attribute->value;
            };
            switch (kind)
            {
            case ManifestDocumentKind::Project:
            {
                const auto version = value("Version");
                return AuthoredProjectManifest{.manifest = identity,
                                               .name = value("Name"),
                                               .type = value("Type"),
                                               .version = version.empty() ? std::nullopt
                                                                          : std::optional<std::string>{version},
                                               .root = std::move(root)};
            }
            case ManifestDocumentKind::Package:
                return AuthoredPackageManifest{.manifest = identity,
                                               .name = value("Name"),
                                               .version = value("Version"),
                                               .root = std::move(root)};
            case ManifestDocumentKind::Workspace:
                return AuthoredWorkspaceManifest{.manifest = identity,
                                                 .name = value("Name"),
                                                 .root = std::move(root)};
            }
            throw std::logic_error("unsupported authored manifest kind");
        }
    }

    auto AuthoredElement::Attribute(const std::string_view attributeName) const -> const AuthoredAttribute *
    {
        const auto found = std::ranges::find(attributes, attributeName, &AuthoredAttribute::name);
        return found == attributes.end() ? nullptr : &*found;
    }

    auto AuthoredManifestResult::Succeeded() const -> bool
    {
        return value.has_value() && std::ranges::none_of(diagnostics, [](const ManifestDiagnostic &diagnostic) {
                   return diagnostic.severity == ManifestDiagnosticSeverity::Error;
               });
    }

    auto ParseAuthoredManifest(const fs::path &path, const AuthoredManifestParseOptions &options)
        -> AuthoredManifestResult
    {
        try
        {
            return ParseAuthoredManifestText(ReadText(path), path, options);
        }
        catch (const std::exception &error)
        {
            AuthoredManifestResult result{};
            AddDiagnostic(result.diagnostics, "NGIN1000", error.what(), ManifestSourceRange{.path = path});
            return result;
        }
    }

    auto ParseAuthoredManifestText(std::string text, fs::path origin, const AuthoredManifestParseOptions &options)
        -> AuthoredManifestResult
    {
        AuthoredManifestResult result{};
        const auto &spec = options.spec == nullptr ? CurrentManifestSpec() : *options.spec;
        const SourceMap sourceMap(text, origin);
        auto parsed = XmlParser::Parse(NGIN::Serialization::OwnedTextBuffer{text});
        if (!parsed.HasValue())
        {
            const auto &error = parsed.Error();
            AddDiagnostic(result.diagnostics, "NGIN1000", "invalid XML: " + ToString(error),
                          sourceMap.Point(error.location.offset));
            return result;
        }

        const auto &document = parsed.Value();
        const auto root = document.Root();
        NamespaceMap namespaces{{"xml", std::string(XmlNamespace)}, {"xmlns", std::string(XmlnsNamespace)}};
        ParseContext context{.spec = spec, .sourceMap = sourceMap, .diagnostics = result.diagnostics};
        namespaces = NamespaceDeclarations(root, std::move(namespaces), context);
        const auto qualified = SplitName(root.Name());
        const auto rootSource = sourceMap.Range(root.Span());
        const auto namespaceUri = ResolveNamespace(qualified, namespaces, rootSource, context, false);
        if (!namespaceUri.has_value())
        {
            return result;
        }
        if (!namespaceUri->empty())
        {
            AddDiagnostic(result.diagnostics, "NGIN1001", "manifest root must use the core empty namespace", rootSource);
            return result;
        }
        const auto kind = KindFromRoot(qualified.local);
        if (!kind.has_value())
        {
            AddDiagnostic(result.diagnostics, "NGIN1001",
                          "unknown manifest root <" + qualified.local + ">; expected Project, Package, or Workspace",
                          rootSource);
            return result;
        }
        const auto &documentSpec = spec.Document(*kind);
        const auto expectedExtension = documentSpec.extension;
        if (!origin.extension().empty() && origin.extension().generic_string() != expectedExtension)
        {
            AddDiagnostic(result.diagnostics, "NGIN1001",
                          std::string(ManifestDocumentKindName(*kind)) + " manifests use the '" + expectedExtension +
                              "' extension",
                          rootSource);
        }

        auto authoredRoot = ParseElement(root, spec.Element(documentSpec.rootElementId), namespaces, context, true);
        if (std::ranges::any_of(result.diagnostics, [](const ManifestDiagnostic &diagnostic) {
                return diagnostic.severity == ManifestDiagnosticSeverity::Error;
            }))
        {
            return result;
        }

        result.value = MakeTypedManifest(*kind, origin, std::move(authoredRoot));
        for (const auto &validator : options.semanticValidators)
        {
            validator(*result.value, result.diagnostics);
        }
        if (std::ranges::any_of(result.diagnostics, [](const ManifestDiagnostic &diagnostic) {
                return diagnostic.severity == ManifestDiagnosticSeverity::Error;
            }))
        {
            result.value.reset();
        }
        return result;
    }
}
