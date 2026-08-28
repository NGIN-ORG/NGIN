#pragma once

#include "Selection.hpp"
#include "SemanticMerge.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace NGIN::CLI
{
    [[nodiscard]] auto ParseOptionDefinitions(const AuthoredElement &options,
                                              std::vector<ManifestDiagnostic> &diagnostics,
                                              bool artifactByDefault = false)
        -> std::map<std::string, OptionDefinition, std::less<>>;

    [[nodiscard]] auto ParseAuthoredVersionConstraint(const AuthoredElement &element,
                                                      std::string_view identity,
                                                      std::vector<ManifestDiagnostic> &diagnostics)
        -> std::optional<SourcedVersionConstraint>;

    [[nodiscard]] auto VersionConstraintContains(const SourcedVersionConstraint &constraint,
                                                 const SemanticVersion &version) -> bool;
}
