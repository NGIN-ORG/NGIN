#pragma once

/// @file HostConfig.hpp
/// @brief Boot-time host configuration for runtime kernel creation.

#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/Runtime/Types.hpp>
#include <NGIN/Runtime/Versioning.hpp>

#include <functional>
#include <string>
#include <vector>

namespace NGIN::Runtime
{
    class IPluginCatalog;
    class IPluginBinaryLoader;
    class IModuleCatalog;
    class IServiceRegistry;

    /// @brief API entry synchronization policy for kernel host calls.
    enum class KernelApiThreadPolicy : NGIN::UInt8
    {
        SingleThreadOnly,
        Serialized
    };

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
        SemanticVersion          platformVersion {0, 1, 0, {}};
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
        KernelApiThreadPolicy    apiThreadPolicy {KernelApiThreadPolicy::SingleThreadOnly};

        std::vector<std::string> requestedModules {};

        std::function<RuntimeResult<void>(IServiceRegistry&)> configureServices {};

        NGIN::Memory::Shared<IModuleCatalog>      moduleCatalog {};
        NGIN::Memory::Shared<IPluginCatalog>      pluginCatalog {};
        NGIN::Memory::Shared<IPluginBinaryLoader> pluginBinaryLoader {};
    };
}
