#pragma once

/// @file HostConfig.hpp
/// @brief Boot-time host configuration for runtime kernel creation.

#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/Runtime/Types.hpp>

#include <string>
#include <vector>

namespace NGIN::Runtime
{
    class IPluginCatalog;
    class IPluginBinaryLoader;

    /// @brief Scheduler policy knobs supplied by host bootstrap.
    struct SchedulerPolicy
    {
        NGIN::UInt32 workerThreads {0};
        bool         enableRenderLane {false};
    };

    /// @brief Logging sink bootstrap policy.
    struct LogSinkConfig
    {
        bool includeConsoleSink {true};
        bool includeSource {true};
        bool autoFlush {false};
    };

    /// @brief Immutable host configuration used during `CreateKernel`.
    struct KernelHostConfig
    {
        std::string              hostName {};
        HostType                 hostType {HostType::RuntimeApp};
        std::string              platformName {};
        std::string              targetName {};
        std::string              workingDirectory {};
        std::vector<std::string> configSources {};
        std::vector<std::string> pluginSearchPaths {};
        bool                     enableDynamicPlugins {false};
        bool                     enableReflection {false};

        std::vector<std::string> commandLineArgs {};
        std::string              environmentName {};
        LogSinkConfig            logSinkConfig {};
        SchedulerPolicy          schedulerPolicy {};
        KernelFailurePolicy      failurePolicy {KernelFailurePolicy::StopKernel};

        std::vector<std::string> requestedModules {};

        NGIN::Memory::Shared<IPluginCatalog>      pluginCatalog {};
        NGIN::Memory::Shared<IPluginBinaryLoader> pluginBinaryLoader {};
    };
}

