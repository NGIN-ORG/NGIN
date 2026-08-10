#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace NGIN::CLI
{
    inline constexpr std::string_view CMakeIntegrationNamespace = "urn:ngin:integration:cmake";

    enum class ManifestDocumentKind
    {
        Project,
        Package,
        Workspace,
    };

    enum class ManifestValueKind
    {
        String,
        Identifier,
        Boolean,
        Integer,
        Path,
        SemanticVersion,
        VersionCompatibility,
        Enumeration,
    };

    struct ManifestAttributeSpec
    {
        std::string name{};
        ManifestValueKind valueKind{ManifestValueKind::String};
        bool required{false};
        std::vector<std::string> allowedValues{};
        std::string documentation{};
    };

    struct ManifestChildSpec
    {
        std::string elementId{};
        std::size_t minimum{0};
        std::optional<std::size_t> maximum{};
    };

    struct ManifestElementSpec
    {
        std::string id{};
        std::string name{};
        std::string namespaceUri{};
        std::vector<ManifestAttributeSpec> attributes{};
        std::vector<ManifestChildSpec> children{};
        bool allowsText{false};
        std::string documentation{};
        std::string semanticValidatorHook{};
        std::string graphProjection{};
    };

    struct ManifestDocumentSpec
    {
        ManifestDocumentKind kind{ManifestDocumentKind::Project};
        std::string extension{};
        std::string rootElementId{};
        std::string schemaFile{};
    };

    struct ManifestNamespaceSpec
    {
        std::string uri{};
        std::string preferredPrefix{};
        std::string schemaFile{};
        std::vector<std::string> rootElementIds{};
    };

    class ManifestSpec
    {
    public:
        [[nodiscard]] auto Documents() const -> std::span<const ManifestDocumentSpec>;
        [[nodiscard]] auto Elements() const -> std::span<const ManifestElementSpec>;
        [[nodiscard]] auto Namespaces() const -> std::span<const ManifestNamespaceSpec>;
        [[nodiscard]] auto Document(ManifestDocumentKind kind) const -> const ManifestDocumentSpec &;
        [[nodiscard]] auto Element(std::string_view id) const -> const ManifestElementSpec &;
        [[nodiscard]] auto FindElement(std::string_view id) const -> const ManifestElementSpec *;
        [[nodiscard]] auto FindChild(const ManifestElementSpec &parent, std::string_view namespaceUri,
                                     std::string_view localName) const -> const ManifestElementSpec *;
        [[nodiscard]] auto FindNamespace(std::string_view uri) const -> const ManifestNamespaceSpec *;

        std::vector<ManifestDocumentSpec> documents{};
        std::vector<ManifestElementSpec> elements{};
        std::vector<ManifestNamespaceSpec> namespaces{};
    };

    [[nodiscard]] auto CurrentManifestSpec() -> const ManifestSpec &;
    [[nodiscard]] auto ManifestDocumentKindName(ManifestDocumentKind kind) -> std::string_view;
    [[nodiscard]] auto ManifestValueKindName(ManifestValueKind kind) -> std::string_view;
}
