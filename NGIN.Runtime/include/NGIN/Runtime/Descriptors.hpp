#pragma once

/// @file Descriptors.hpp
/// @brief Module and dependency descriptors consumed by the runtime resolver.

#include <NGIN/Runtime/Types.hpp>
#include <NGIN/Runtime/Versioning.hpp>

#include <string>
#include <vector>

namespace NGIN::Runtime
{
    /// @brief Descriptor module type.
    enum class ModuleType : NGIN::UInt8
    {
        Runtime,
        Editor,
        Program,
        Developer,
        ThirdParty
    };

    /// @brief Directed dependency edge between modules.
    struct DependencyDescriptor
    {
        std::string  name {};
        bool         optional {false};
        VersionRange requiredVersion {};
    };

    /// @brief Resolved module descriptor used by kernel orchestration.
    struct ModuleDescriptor
    {
        std::string                       name {};
        ModuleType                        type {ModuleType::Runtime};
        SemanticVersion                   version {};
        VersionRange                      compatiblePlatformRange {};
        std::vector<std::string>          platforms {};
        std::vector<DependencyDescriptor> dependencies {};
        LoadPhase                         loadPhase {LoadPhase::CoreServices};
        ModuleEntryKind                   entryKind {ModuleEntryKind::Static};
        std::string                       pluginName {};
        std::vector<std::string>          providesServices {};
        std::vector<std::string>          requiresServices {};
        bool                              reflectionRequired {false};
        std::vector<std::string>          capabilities {};
        NGIN::Int32                       priority {0};
    };

    [[nodiscard]] constexpr auto ToString(const ModuleType value) noexcept -> std::string_view
    {
        switch (value)
        {
            case ModuleType::Runtime: return "Runtime";
            case ModuleType::Editor: return "Editor";
            case ModuleType::Program: return "Program";
            case ModuleType::Developer: return "Developer";
            case ModuleType::ThirdParty: return "ThirdParty";
        }
        return "Unknown";
    }
}

