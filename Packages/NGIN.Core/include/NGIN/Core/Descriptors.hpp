#pragma once

/// @file Descriptors.hpp
/// @brief Module and dependency descriptors consumed by the runtime resolver.

#include <NGIN/Core/Types.hpp>
#include <NGIN/Core/Versioning.hpp>

#include <string>
#include <vector>

namespace NGIN::Core
{
    /// @brief Descriptor module family used for layer enforcement.
    enum class ModuleFamily : NGIN::UInt8
    {
        Base,
        Reflection,
        Core,
        Platform,
        Editor,
        Domain,
        App
    };

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
        ModuleFamily                      family {ModuleFamily::Core};
        ModuleType                        type {ModuleType::Runtime};
        SemanticVersion                   version {};
        VersionRange                      compatiblePlatformRange {};
        std::vector<std::string>          platforms {};
        std::vector<DependencyDescriptor> dependencies {};
        StartupStage                      startupStage {StartupStage::Features};
        std::vector<HostType>             supportedHosts {};
        ModuleEntryKind                   entryKind {ModuleEntryKind::Static};
        std::string                       pluginName {};
        std::vector<std::string>          providesServices {};
        std::vector<std::string>          requiresServices {};
        bool                              reflectionRequired {false};
        std::vector<std::string>          capabilities {};
        NGIN::Int32                       priority {0};
    };

    [[nodiscard]] constexpr auto ToString(const ModuleFamily value) noexcept -> std::string_view
    {
        switch (value)
        {
            case ModuleFamily::Base: return "Base";
            case ModuleFamily::Reflection: return "Reflection";
            case ModuleFamily::Core: return "Core";
            case ModuleFamily::Platform: return "Platform";
            case ModuleFamily::Editor: return "Editor";
            case ModuleFamily::Domain: return "Domain";
            case ModuleFamily::App: return "App";
        }
        return "Unknown";
    }

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
