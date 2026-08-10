#pragma once

#include "PackageModel.hpp"

#include <memory>

namespace NGIN::CLI
{
    struct CMakeBindingResolution
    {
        std::optional<CMakeIntegrationBindings> value{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool;
    };

    class ResolvedCMakeIntegrationBindings
    {
    public:
        explicit ResolvedCMakeIntegrationBindings(std::vector<CMakeIntegrationBindings> bindings = {});

        [[nodiscard]] auto Data() const -> const std::vector<CMakeIntegrationBindings> &;

    private:
        std::shared_ptr<const std::vector<CMakeIntegrationBindings>> bindings_{};
    };

    [[nodiscard]] auto ResolveCMakeIntegration(
        const AuthoredPackageManifest &authored, const SemanticPackage &package,
        const PackageProviderResult &provider, const PackageInstance &instance,
        const ActivePackageExports &activation, const SelectionFacts &selection,
        const ResolvedPackageOptions &options) -> CMakeBindingResolution;

    [[nodiscard]] auto CMakeIntegrationKindName(CMakeIntegrationKind kind) -> std::string_view;
}
