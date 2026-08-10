#include "ManifestFormatter.hpp"

#include <NGIN/Serialization/Core/SourceBuffer.hpp>
#include <NGIN/Serialization/XML/XmlParser.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace NGIN::CLI
{
    namespace
    {
        using namespace NGIN::Serialization::XML;

        [[nodiscard]] auto EscapeText(const std::string_view value) -> std::string
        {
            std::string result{};
            for (const auto ch : value)
            {
                switch (ch)
                {
                case '&': result += "&amp;"; break;
                case '<': result += "&lt;"; break;
                case '>': result += "&gt;"; break;
                default: result += ch; break;
                }
            }
            return result;
        }

        [[nodiscard]] auto EscapeAttribute(const std::string_view value) -> std::string
        {
            auto result = EscapeText(value);
            std::size_t position = 0;
            while ((position = result.find('"', position)) != std::string::npos)
            {
                result.replace(position, 1, "&quot;");
                position += 6;
            }
            return result;
        }

        [[nodiscard]] auto Trim(const std::string_view value) -> std::string_view
        {
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos) return {};
            return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
        }

        auto WriteNode(std::ostringstream &out, const NodeView node, const std::size_t depth) -> void;

        auto WriteElement(std::ostringstream &out, const ElementView element, const std::size_t depth) -> void
        {
            const std::string indent(depth * 2, ' ');
            out << indent << '<' << element.Name();
            for (const auto attribute : element.Attributes())
                out << ' ' << attribute.Name() << "=\"" << EscapeAttribute(attribute.Value()) << '"';

            bool hasContent = false;
            bool hasElementLikeChild = false;
            for (const auto child : element.Children())
            {
                const auto text = Trim(child.TryText().value_or(std::string_view{}));
                if (child.Kind() == NodeKind::Element || child.Kind() == NodeKind::Comment ||
                    child.Kind() == NodeKind::ProcessingInstruction || child.Kind() == NodeKind::CData)
                    hasElementLikeChild = true;
                if (child.Kind() == NodeKind::Element || !text.empty()) hasContent = true;
            }
            if (!hasContent)
            {
                out << " />\n";
                return;
            }
            if (!hasElementLikeChild)
            {
                out << '>';
                for (const auto child : element.Children())
                    if (const auto text = Trim(child.TryText().value_or(std::string_view{})); !text.empty())
                        out << EscapeText(text);
                out << "</" << element.Name() << ">\n";
                return;
            }
            out << ">\n";
            for (const auto child : element.Children()) WriteNode(out, child, depth + 1);
            out << indent << "</" << element.Name() << ">\n";
        }

        auto WriteNode(std::ostringstream &out, const NodeView node, const std::size_t depth) -> void
        {
            if (const auto element = node.TryElement())
            {
                WriteElement(out, *element, depth);
                return;
            }
            const auto value = Trim(node.TryText().value_or(std::string_view{}));
            if (value.empty()) return;
            const std::string indent(depth * 2, ' ');
            switch (node.Kind())
            {
            case NodeKind::Comment: out << indent << "<!--" << value << "-->\n"; break;
            case NodeKind::CData: out << indent << "<![CDATA[" << value << "]]>\n"; break;
            case NodeKind::ProcessingInstruction: out << indent << "<?" << node.Name() << ' ' << value << "?>\n"; break;
            case NodeKind::Text: out << indent << EscapeText(value) << '\n'; break;
            case NodeKind::Element: break;
            }
        }
    }

    auto FormatManifestXml(const std::string_view source) -> std::string
    {
        ParseOptions options{};
        options.trivia = TriviaPolicy::Preserve;
        auto parsed = Parse(NGIN::Serialization::OwnedTextBuffer{source}, options);
        if (!parsed.HasValue()) throw std::runtime_error("invalid XML manifest");
        const auto root = parsed.Value().Root();
        if (!root.IsValid()) throw std::runtime_error("manifest has no XML root element");
        std::ostringstream result{};
        result << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n\n";
        WriteElement(result, root, 0);
        return result.str();
    }

    auto FormatManifestFile(const std::filesystem::path &path) -> std::string
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error(path.string() + ": cannot read manifest");
        std::ostringstream source{};
        source << input.rdbuf();
        return FormatManifestXml(source.str());
    }
}
