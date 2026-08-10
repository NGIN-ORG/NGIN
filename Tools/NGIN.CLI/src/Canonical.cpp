#include "Canonical.hpp"

#include <sstream>
#include <type_traits>

namespace NGIN::CLI
{
    namespace
    {
        auto WriteEscaped(std::ostream &out, const std::string_view value) -> void
        {
            static constexpr char hex[] = "0123456789abcdef";
            out << '"';
            for (const auto ch : value)
            {
                const auto byte = static_cast<unsigned char>(ch);
                switch (ch)
                {
                case '"': out << "\\\""; break;
                case '\\': out << "\\\\"; break;
                case '\b': out << "\\b"; break;
                case '\f': out << "\\f"; break;
                case '\n': out << "\\n"; break;
                case '\r': out << "\\r"; break;
                case '\t': out << "\\t"; break;
                default:
                    if (byte < 0x20)
                    {
                        out << "\\u00" << hex[byte >> 4U] << hex[byte & 0x0fU];
                    }
                    else
                    {
                        out << ch;
                    }
                    break;
                }
            }
            out << '"';
        }

        auto WriteValue(std::ostream &out, const CanonicalValue &value) -> void
        {
            std::visit(
                [&](const auto &item) {
                    using T = std::decay_t<decltype(item)>;
                    if constexpr (std::is_same_v<T, std::nullptr_t>)
                    {
                        out << "null";
                    }
                    else if constexpr (std::is_same_v<T, bool>)
                    {
                        out << (item ? "true" : "false");
                    }
                    else if constexpr (std::is_same_v<T, std::int64_t>)
                    {
                        out << item;
                    }
                    else if constexpr (std::is_same_v<T, std::string>)
                    {
                        WriteEscaped(out, item);
                    }
                    else if constexpr (std::is_same_v<T, CanonicalValue::Array>)
                    {
                        out << '[';
                        for (std::size_t index = 0; index < item.size(); ++index)
                        {
                            if (index != 0) out << ',';
                            WriteValue(out, item[index]);
                        }
                        out << ']';
                    }
                    else
                    {
                        out << '{';
                        std::size_t index = 0;
                        for (const auto &[key, child] : item)
                        {
                            if (index++ != 0) out << ',';
                            WriteEscaped(out, key);
                            out << ':';
                            WriteValue(out, child);
                        }
                        out << '}';
                    }
                },
                value.value);
        }
    }

    auto SerializeCanonical(const CanonicalValue &value) -> std::string
    {
        std::ostringstream out;
        WriteValue(out, value);
        return out.str();
    }

    auto CanonicalDigestInput(const std::string_view kind, const CanonicalValue::Object &fields) -> std::string
    {
        CanonicalValue::Object root{{"kind", std::string(kind)}, {"value", fields}};
        return SerializeCanonical(root);
    }
}
