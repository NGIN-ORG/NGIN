#include "Diagnostics.hpp"

namespace NGIN::CLI
{
    auto AddError(DiagnosticReport &report, std::string message, std::string subject) -> void
    {
        report.entries.push_back(Diagnostic{
            .severity = DiagnosticSeverity::Error,
            .subject = std::move(subject),
            .message = std::move(message),
        });
    }

    auto AddWarning(DiagnosticReport &report, std::string message, std::string subject) -> void
    {
        report.entries.push_back(Diagnostic{
            .severity = DiagnosticSeverity::Warning,
            .subject = std::move(subject),
            .message = std::move(message),
        });
    }

    auto AppendDiagnostics(DiagnosticReport &target, const DiagnosticReport &source) -> void
    {
        target.entries.insert(target.entries.end(), source.entries.begin(), source.entries.end());
    }

    auto PrintDiagnostics(const DiagnosticReport &report, std::string_view title, std::ostream &output) -> void
    {
        bool printedErrors = false;
        for (const auto &entry : report.entries)
        {
            if (entry.severity != DiagnosticSeverity::Error)
            {
                continue;
            }
            if (!printedErrors)
            {
                output << "\n" << title << " errors:\n";
                printedErrors = true;
            }
            output << "  - " << entry.message << "\n";
        }

        bool printedWarnings = false;
        for (const auto &entry : report.entries)
        {
            if (entry.severity != DiagnosticSeverity::Warning)
            {
                continue;
            }
            if (!printedWarnings)
            {
                output << "\nWarnings:\n";
                printedWarnings = true;
            }
            output << "  - " << entry.message << "\n";
        }
    }
}
