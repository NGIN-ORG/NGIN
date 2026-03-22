#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace NGIN::CLI
{
    enum class DiagnosticSeverity
    {
        Error,
        Warning,
    };

    struct Diagnostic
    {
        DiagnosticSeverity severity{DiagnosticSeverity::Error};
        std::string subject{};
        std::string message{};
    };

    struct DiagnosticReport
    {
        std::vector<Diagnostic> entries{};

        [[nodiscard]] auto HasErrors() const -> bool
        {
            for (const auto &entry : entries)
            {
                if (entry.severity == DiagnosticSeverity::Error)
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] auto Empty() const -> bool
        {
            return entries.empty();
        }
    };

    template <typename TValue>
    struct DiagnosticResult
    {
        std::optional<TValue> value{};
        DiagnosticReport diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool
        {
            return value.has_value() && !diagnostics.HasErrors();
        }
    };

    auto AddError(DiagnosticReport &report, std::string message, std::string subject = {}) -> void;
    auto AddWarning(DiagnosticReport &report, std::string message, std::string subject = {}) -> void;
    auto AppendDiagnostics(DiagnosticReport &target, const DiagnosticReport &source) -> void;
    auto PrintDiagnostics(const DiagnosticReport &report, std::string_view title, std::ostream &output) -> void;
}
