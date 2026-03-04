#pragma once

/// @file Types.hpp
/// @brief Foundational enums and IDs for the runtime kernel contract.

#include <NGIN/Primitives.hpp>

#include <string>
#include <string_view>

namespace NGIN::Runtime
{
    /// @brief Host process mode.
    enum class HostType : NGIN::UInt8
    {
        RuntimeApp,
        EditorHost,
        Server,
        Program
    };

    /// @brief Kernel lifecycle state.
    enum class KernelState : NGIN::UInt8
    {
        Created,
        ConfigLoaded,
        ModulesResolved,
        ServicesBuilt,
        ModulesLoaded,
        Running,
        Stopping,
        Stopped,
        Shutdown
    };

    /// @brief Runtime module lifecycle state.
    enum class ModuleState : NGIN::UInt8
    {
        Discovered,
        Resolved,
        Loaded,
        Constructed,
        Initialized,
        Running,
        Stopping,
        Uninitialized,
        Unloaded
    };

    /// @brief Coarse module startup phase.
    enum class LoadPhase : NGIN::UInt8
    {
        Bootstrap,
        Platform,
        CoreServices,
        Data,
        Domain,
        Application,
        Editor
    };

    /// @brief Module entry source.
    enum class ModuleEntryKind : NGIN::UInt8
    {
        Static,
        Dynamic
    };

    /// @brief Running-state failure handling policy.
    enum class KernelFailurePolicy : NGIN::UInt8
    {
        FailFast,
        StopKernel,
        IsolateModule
    };

    using ModuleId = NGIN::UInt64;
    using TaskId   = NGIN::UInt64;

    [[nodiscard]] constexpr auto ToString(const HostType value) noexcept -> std::string_view
    {
        switch (value)
        {
            case HostType::RuntimeApp: return "RuntimeApp";
            case HostType::EditorHost: return "EditorHost";
            case HostType::Server: return "Server";
            case HostType::Program: return "Program";
        }
        return "Unknown";
    }

    [[nodiscard]] constexpr auto ToString(const KernelState value) noexcept -> std::string_view
    {
        switch (value)
        {
            case KernelState::Created: return "Created";
            case KernelState::ConfigLoaded: return "ConfigLoaded";
            case KernelState::ModulesResolved: return "ModulesResolved";
            case KernelState::ServicesBuilt: return "ServicesBuilt";
            case KernelState::ModulesLoaded: return "ModulesLoaded";
            case KernelState::Running: return "Running";
            case KernelState::Stopping: return "Stopping";
            case KernelState::Stopped: return "Stopped";
            case KernelState::Shutdown: return "Shutdown";
        }
        return "Unknown";
    }

    [[nodiscard]] constexpr auto ToString(const ModuleState value) noexcept -> std::string_view
    {
        switch (value)
        {
            case ModuleState::Discovered: return "Discovered";
            case ModuleState::Resolved: return "Resolved";
            case ModuleState::Loaded: return "Loaded";
            case ModuleState::Constructed: return "Constructed";
            case ModuleState::Initialized: return "Initialized";
            case ModuleState::Running: return "Running";
            case ModuleState::Stopping: return "Stopping";
            case ModuleState::Uninitialized: return "Uninitialized";
            case ModuleState::Unloaded: return "Unloaded";
        }
        return "Unknown";
    }

    [[nodiscard]] constexpr auto ToString(const LoadPhase value) noexcept -> std::string_view
    {
        switch (value)
        {
            case LoadPhase::Bootstrap: return "Bootstrap";
            case LoadPhase::Platform: return "Platform";
            case LoadPhase::CoreServices: return "CoreServices";
            case LoadPhase::Data: return "Data";
            case LoadPhase::Domain: return "Domain";
            case LoadPhase::Application: return "Application";
            case LoadPhase::Editor: return "Editor";
        }
        return "Unknown";
    }

    [[nodiscard]] constexpr auto ToString(const ModuleEntryKind value) noexcept -> std::string_view
    {
        switch (value)
        {
            case ModuleEntryKind::Static: return "Static";
            case ModuleEntryKind::Dynamic: return "Dynamic";
        }
        return "Unknown";
    }

    [[nodiscard]] constexpr auto ToString(const KernelFailurePolicy value) noexcept -> std::string_view
    {
        switch (value)
        {
            case KernelFailurePolicy::FailFast: return "FailFast";
            case KernelFailurePolicy::StopKernel: return "StopKernel";
            case KernelFailurePolicy::IsolateModule: return "IsolateModule";
        }
        return "Unknown";
    }
}

