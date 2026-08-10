#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace NGIN::CLI
{
    struct ActionDiagnostic
    {
        std::string file{};
        std::size_t startLine{1};
        std::size_t startColumn{1};
        std::size_t endLine{1};
        std::size_t endColumn{1};
        std::string severity{};
        std::string source{};
        std::string code{};
        std::string message{};
    };

    [[nodiscard]] auto ParseActionDiagnostics(std::string_view output, std::string_view source)
        -> std::vector<ActionDiagnostic>;
    [[nodiscard]] auto SerializeActionDiagnostics(const std::vector<ActionDiagnostic> &diagnostics) -> std::string;
}
