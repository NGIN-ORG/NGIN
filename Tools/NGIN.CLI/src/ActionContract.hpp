#pragma once

#include "ManifestPaths.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace NGIN::CLI
{
    enum class ActionKind
    {
        Generate,
        Analyze,
        Format,
        Validate,
        Custom,
    };

    enum class ActionInputKind
    {
        Header,
        Source,
        File,
    };

    enum class ActionOutputKind
    {
        Source,
        Header,
        File,
        Directory,
    };

    struct ActionInputDeclaration
    {
        ActionInputKind kind{ActionInputKind::File};
        std::string include{};
        std::optional<std::string> exclude{};
        ManifestSourceRange source{};
    };

    struct ActionOutputDeclaration
    {
        ActionOutputKind kind{ActionOutputKind::File};
        PortablePath path{};
        ManifestSourceRange source{};
    };

    struct SemanticActionContract
    {
        ActionKind kind{ActionKind::Custom};
        std::string toolExport{};
        std::vector<ActionInputDeclaration> inputs{};
        std::vector<ActionOutputDeclaration> outputs{};
        std::vector<std::string> arguments{};
        std::optional<PortablePath> workingDirectory{};
        std::map<std::string, std::string, std::less<>> environment{};
        bool deterministic{false};
        ManifestSourceRange source{};
    };

    [[nodiscard]] auto ActionKindName(ActionKind kind) -> std::string_view;
}
