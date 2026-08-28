#pragma once

#include "ManifestSpec.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace NGIN::CLI
{
    namespace fs = std::filesystem;

    struct ManifestSourcePosition
    {
        std::size_t offset{0};
        std::size_t line{1};
        std::size_t column{1};

        [[nodiscard]] friend auto operator==(const ManifestSourcePosition &, const ManifestSourcePosition &)
            -> bool = default;
    };

    struct ManifestSourceRange
    {
        fs::path path{};
        ManifestSourcePosition begin{};
        ManifestSourcePosition end{};

        [[nodiscard]] friend auto operator==(const ManifestSourceRange &, const ManifestSourceRange &)
            -> bool = default;
    };

    struct AuthoredAttribute
    {
        std::string name{};
        std::string namespaceUri{};
        std::string value{};
        ManifestSourceRange source{};
    };

    struct AuthoredElement
    {
        std::string specId{};
        std::string name{};
        std::string namespaceUri{};
        std::string text{};
        ManifestSourceRange source{};
        std::vector<AuthoredAttribute> attributes{};
        std::vector<AuthoredElement> children{};

        [[nodiscard]] auto Attribute(std::string_view attributeName) const -> const AuthoredAttribute *;
    };

    struct AuthoredManifestIdentity
    {
        fs::path path{};
        std::string canonicalPath{};
    };

    struct AuthoredProjectManifest
    {
        AuthoredManifestIdentity manifest{};
        std::string name{};
        std::string artifactKind{};
        std::optional<std::string> libraryKind{};
        std::optional<std::string> version{};
        AuthoredElement root{};
    };

    struct AuthoredPackageManifest
    {
        AuthoredManifestIdentity manifest{};
        std::string name{};
        std::string version{};
        AuthoredElement root{};
    };

    struct AuthoredWorkspaceManifest
    {
        AuthoredManifestIdentity manifest{};
        std::string name{};
        AuthoredElement root{};
    };

    using AuthoredManifest =
        std::variant<AuthoredProjectManifest, AuthoredPackageManifest, AuthoredWorkspaceManifest>;

    enum class ManifestDiagnosticSeverity
    {
        Error,
        Warning,
    };

    struct ManifestDiagnostic
    {
        ManifestDiagnosticSeverity severity{ManifestDiagnosticSeverity::Error};
        std::string code{};
        std::string message{};
        ManifestSourceRange source{};
        std::vector<ManifestSourceRange> relatedSources{};
        std::optional<std::string> fixHint{};
    };

    struct AuthoredManifestResult
    {
        std::optional<AuthoredManifest> value{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool;
    };

    using ManifestSemanticValidator =
        std::function<void(const AuthoredManifest &, std::vector<ManifestDiagnostic> &)>;

    struct AuthoredManifestParseOptions
    {
        const ManifestSpec *spec{nullptr};
        std::vector<ManifestSemanticValidator> semanticValidators{};
    };

    [[nodiscard]] auto ParseAuthoredManifest(const fs::path &path,
                                             const AuthoredManifestParseOptions &options = {})
        -> AuthoredManifestResult;
    [[nodiscard]] auto ParseAuthoredManifestText(std::string text, fs::path origin,
                                                 const AuthoredManifestParseOptions &options = {})
        -> AuthoredManifestResult;
}
