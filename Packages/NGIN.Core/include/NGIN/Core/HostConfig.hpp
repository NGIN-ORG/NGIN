#pragma once

/// @file HostConfig.hpp
/// @brief Boot-time host configuration for core host creation.

#include <NGIN/Core/Types.hpp>
#include <NGIN/Core/Versioning.hpp>
#include <NGIN/Log/LoggerRegistry.hpp>
#include <NGIN/Memory/SmartPointers.hpp>

#include <functional>
#include <string>
#include <vector>

namespace NGIN::IO
{
  class IFileSystem;
}

namespace NGIN::Core
{
  class IPluginCatalog;
  class IPluginBinaryLoader;
  class IModuleCatalog;
  class IServiceRegistry;
  class IEventBus;
  class ITaskRuntime;
  class IConfigStore;
} // namespace NGIN::Core

namespace NGIN::Core
{
  struct KernelBootstrapContext
  {
    NGIN::Memory::Shared<IServiceRegistry> services{};
    NGIN::Memory::Shared<IEventBus> events{};
    NGIN::Memory::Shared<ITaskRuntime> tasks{};
    NGIN::Memory::Shared<IConfigStore> config{};
    NGIN::Log::LoggerRegistry *loggerRegistry{nullptr};
  };

  /// @brief API entry synchronization policy for kernel host calls.
  enum class KernelApiThreadPolicy : NGIN::UInt8
  {
    SingleThreadOnly,
    Serialized
  };

  /// @brief Scheduler policy knobs supplied by host bootstrap.
  struct SchedulerPolicy
  {
    NGIN::UInt32 workerThreads{0};
    bool enableRenderLane{false};
  };

  /// @brief Logging sink bootstrap policy.
  struct LogSinkConfig
  {
    bool includeConsoleSink{true};
    bool includeSource{true};
    bool autoFlush{false};
  };

  /// @brief Immutable host configuration used during `CreateKernel`.
  struct KernelHostConfig
  {
    std::string hostName{};
    HostType hostType{HostType::GuiApp};
    std::string platformName{};
    std::string operatingSystemName{"linux"};
    std::string architectureName{"x64"};
    SemanticVersion platformVersion{0, 1, 0, {}};
    std::string targetName{};
    std::string workingDirectory{};
    std::vector<std::string> configInputs{};
    std::vector<std::string> pluginSearchPaths{};
    bool enableDynamicPlugins{false};
    bool enableReflection{false};

    std::vector<std::string> commandLineArgs{};
    std::string environmentName{};
    LogSinkConfig logSinkConfig{};
    SchedulerPolicy schedulerPolicy{};
    KernelFailurePolicy failurePolicy{KernelFailurePolicy::StopKernel};
    KernelApiThreadPolicy apiThreadPolicy{
        KernelApiThreadPolicy::SingleThreadOnly};

    std::vector<std::string> requestedModules{};

    std::function<CoreResult<void>(KernelBootstrapContext &)> configureServices{};

    NGIN::Memory::Shared<NGIN::IO::IFileSystem> fileSystem{};
    NGIN::Memory::Shared<IModuleCatalog> moduleCatalog{};
    NGIN::Memory::Shared<IPluginCatalog> pluginCatalog{};
    NGIN::Memory::Shared<IPluginBinaryLoader> pluginBinaryLoader{};
  };
} // namespace NGIN::Core
