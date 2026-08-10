#include "ActionDiagnostics.hpp"

#include "Canonical.hpp"

#include <regex>
#include <sstream>

namespace NGIN::CLI
{
    auto ParseActionDiagnostics(const std::string_view output, const std::string_view source)
        -> std::vector<ActionDiagnostic>
    {
        static const std::regex pattern{
            R"(^(.*):([0-9]+):([0-9]+):\s*(warning|error|note):\s*(.*?)(?:\s+\[([^\]]+)\])?\r?$)"};
        std::vector<ActionDiagnostic> result{};
        std::istringstream lines{std::string{output}};
        for (std::string line{}; std::getline(lines, line);)
        {
            std::smatch match{};
            if (!std::regex_match(line, match, pattern)) continue;
            const auto lineNumber = static_cast<std::size_t>(std::stoull(match[2].str()));
            const auto column = static_cast<std::size_t>(std::stoull(match[3].str()));
            result.push_back(ActionDiagnostic{
                .file = match[1].str(),
                .startLine = lineNumber,
                .startColumn = column,
                .endLine = lineNumber,
                .endColumn = column + 1,
                .severity = match[4].str() == "note" ? "information" : match[4].str(),
                .source = std::string{source},
                .code = match[6].matched ? match[6].str() : std::string{},
                .message = match[5].str(),
            });
        }
        return result;
    }

    auto SerializeActionDiagnostics(const std::vector<ActionDiagnostic> &diagnostics) -> std::string
    {
        CanonicalValue::Array values{};
        values.reserve(diagnostics.size());
        for (const auto &diagnostic : diagnostics)
        {
            values.push_back(CanonicalValue::Object{
                {"code", diagnostic.code},
                {"file", diagnostic.file},
                {"fixes", CanonicalValue::Array{}},
                {"message", diagnostic.message},
                {"range",
                 CanonicalValue::Object{
                     {"end", CanonicalValue::Object{{"column", static_cast<std::int64_t>(diagnostic.endColumn)},
                                                     {"line", static_cast<std::int64_t>(diagnostic.endLine)}}},
                     {"start",
                      CanonicalValue::Object{{"column", static_cast<std::int64_t>(diagnostic.startColumn)},
                                             {"line", static_cast<std::int64_t>(diagnostic.startLine)}}}}},
                {"severity", diagnostic.severity},
                {"source", diagnostic.source},
            });
        }
        return SerializeCanonical(CanonicalValue::Object{{"diagnostics", std::move(values)},
                                                         {"kind", "NGIN.ActionDiagnostics"},
                                                         {"state", "complete"}});
    }
}
