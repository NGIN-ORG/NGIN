#pragma once

#include "AuthoredManifest.hpp"
#include "Canonical.hpp"
#include "CompositionBoundary.hpp"
#include "ManifestPaths.hpp"
#include "SemanticMerge.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace NGIN::CLI
{
    enum class OptionType
    {
        Boolean,
        Enumeration,
        String,
        Integer,
        Path,
    };

    using OptionStorage = std::variant<bool, std::string, std::int64_t, PortablePath>;

    struct TypedOptionValue
    {
        OptionType type{OptionType::String};
        OptionStorage value{std::string{}};

        [[nodiscard]] friend auto operator==(const TypedOptionValue &, const TypedOptionValue &) -> bool = default;
    };

    struct OptionDefinition
    {
        std::string name{};
        OptionType type{OptionType::String};
        TypedOptionValue defaultValue{};
        bool artifact{false};
        std::set<std::string, std::less<>> allowedValues{};
        std::optional<std::int64_t> minimum{};
        std::optional<std::int64_t> maximum{};
        ManifestSourceRange source{};
    };

    struct OptionValueResult
    {
        std::optional<TypedOptionValue> value{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool;
    };

    [[nodiscard]] auto ParseOptionValue(const OptionDefinition &definition, std::string_view authored,
                                        const ManifestSourceRange &source = {}) -> OptionValueResult;
    [[nodiscard]] auto CanonicalOptionValue(const TypedOptionValue &value) -> std::string;

    struct Configuration
    {
        std::string name{};
        std::string optimization{"Off"};
        bool debugSymbols{false};
        bool linkTimeOptimization{false};
        std::map<std::string, TypedOptionValue, std::less<>> options{};

        [[nodiscard]] friend auto operator==(const Configuration &, const Configuration &) -> bool = default;
    };

    struct Target
    {
        std::string name{};
        std::set<std::string, std::less<>> aliases{};
        std::string operatingSystem{};
        std::string architecture{};

        [[nodiscard]] friend auto operator==(const Target &, const Target &) -> bool = default;
    };

    struct Toolchain
    {
        std::string name{};
        std::string compiler{};
        std::string compilerVersion{};
        std::string runtimeLibrary{};
        std::string linker{};
        std::optional<PortablePath> toolchainFile{};

        [[nodiscard]] friend auto operator==(const Toolchain &, const Toolchain &) -> bool = default;
    };

    struct SelectionFacts
    {
        Configuration configuration{};
        Target target{};
        Toolchain toolchain{};
        std::map<std::string, TypedOptionValue, std::less<>> options{};
    };

    [[nodiscard]] auto CanonicalTargetIdentity(const Target &target) -> std::string;
    [[nodiscard]] auto CanonicalToolchainIdentity(const Toolchain &toolchain) -> std::string;
    [[nodiscard]] auto DeriveBinaryCompatibility(const SelectionFacts &selection, std::string linkage,
                                                 const std::map<std::string, OptionDefinition, std::less<>> &definitions)
        -> BinaryCompatibility;
    [[nodiscard]] auto CanonicalSelection(const SelectionFacts &selection) -> CanonicalValue;

    struct SelectionRequest
    {
        std::optional<std::string> configuration{};
        std::optional<std::string> target{};
        std::optional<std::string> toolchain{};
        std::map<std::string, std::string, std::less<>> options{};
        std::optional<std::string> run{};

        [[nodiscard]] friend auto operator==(const SelectionRequest &, const SelectionRequest &) -> bool = default;
    };

    struct Profile
    {
        std::string name{};
        SelectionRequest selection{};
        ManifestSourceRange source{};
    };

    struct ProfileExpansionResult
    {
        std::optional<SelectionRequest> value{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool;
    };

    [[nodiscard]] auto ExpandProfile(const Profile &profile, const SelectionRequest &explicitRequest)
        -> ProfileExpansionResult;
    [[nodiscard]] auto ResolveTargetAlias(std::string_view name, const std::vector<Target> &targets,
                                          const Target &host, const ManifestSourceRange &source = {})
        -> std::pair<std::optional<Target>, std::vector<ManifestDiagnostic>>;

    struct RefinementSelector
    {
        std::optional<std::string> configuration{};
        std::optional<std::string> targetName{};
        std::optional<std::string> targetOperatingSystem{};
        std::optional<std::string> targetArchitecture{};
        std::optional<std::string> toolchainName{};
        std::optional<std::string> compiler{};
        std::map<std::string, TypedOptionValue, std::less<>> options{};
    };

    struct RefinementAssignment
    {
        std::string category{};
        std::string identity{};
        CanonicalValue value{};
        ManifestSourceRange source{};
    };

    struct SemanticRefinement
    {
        RefinementSelector selector{};
        std::vector<RefinementAssignment> assignments{};
        ManifestSourceRange source{};
    };

    struct RefinementResult
    {
        std::map<std::string, RefinementAssignment, std::less<>> assignments{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool;
    };

    [[nodiscard]] auto RefinementMatches(const RefinementSelector &selector, const SelectionFacts &selection) -> bool;
    [[nodiscard]] auto RefinementSpecificity(const RefinementSelector &selector) -> std::size_t;
    [[nodiscard]] auto ResolveRefinements(const SelectionFacts &selection,
                                          const std::vector<SemanticRefinement> &refinements) -> RefinementResult;
}
