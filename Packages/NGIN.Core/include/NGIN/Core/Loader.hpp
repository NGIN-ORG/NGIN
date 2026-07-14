#pragma once

/// @file Loader.hpp
/// @brief Static module catalog and dynamic plugin seam contracts.

#include <NGIN/Core/Descriptors.hpp>
#include <NGIN/Core/Errors.hpp>
#include <NGIN/Core/Export.hpp>
#include <NGIN/Memory/SmartPointers.hpp>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace NGIN::IO {
class IFileSystem;
}

namespace NGIN::Core {
class IModule;

/// @brief Factory callable for creating a module instance.
using ModuleFactory =
    std::function<CoreResult<NGIN::Memory::Shared<IModule>>()>;

/// @brief Static module registration record.
struct StaticModuleRegistration {
  ModuleDescriptor descriptor{};
  ModuleFactory factory{};
};

/// @brief Registry exposed to dynamic plugin registrar functions.
class NGIN_CORE_API IPluginModuleRegistry {
public:
  virtual ~IPluginModuleRegistry() = default;

  virtual auto Register(std::string moduleName,
                        ModuleFactory factory) noexcept -> CoreResult<void> = 0;
};

/// @brief Dynamic plugin registrar function exported by plugin binaries.
using PluginRegistrarFn = CoreResult<void> (*)(IPluginModuleRegistry &);

/// @brief Module catalog abstraction used by kernel resolution.
class NGIN_CORE_API IModuleCatalog {
public:
  virtual ~IModuleCatalog() = default;
  virtual auto Register(StaticModuleRegistration registration) noexcept
      -> CoreResult<void> = 0;
  virtual void Clear() noexcept = 0;
  [[nodiscard]] virtual auto Snapshot() const
      -> std::vector<StaticModuleRegistration> = 0;
};

/// @brief Thread-safe static module catalog implementation.
class NGIN_CORE_API StaticModuleCatalog final : public IModuleCatalog {
public:
  auto Register(StaticModuleRegistration registration) noexcept
      -> CoreResult<void> override;
  void Clear() noexcept override;
  [[nodiscard]] auto Snapshot() const
      -> std::vector<StaticModuleRegistration> override;

private:
  mutable std::mutex m_mutex;
  std::vector<StaticModuleRegistration> m_entries;
};

/// @brief Dynamic plugin catalog abstraction (Spec 003 seam).
class NGIN_CORE_API IPluginCatalog {
public:
  virtual ~IPluginCatalog() = default;

  /// @brief Enumerate dynamic module descriptors from `.module.xml` /
  /// `.plugin-module.xml` files without loading binaries.
  virtual auto CollectDescriptors(std::vector<ModuleDescriptor> &out) noexcept
      -> CoreResult<void> = 0;
};

/// @brief Filesystem-backed dynamic descriptor catalog scanning XML module
/// descriptors from plugin search paths.
class NGIN_CORE_API FilesystemPluginCatalog final : public IPluginCatalog {
public:
  explicit FilesystemPluginCatalog(std::vector<std::string> searchPaths);
  FilesystemPluginCatalog(
      std::vector<std::string> searchPaths,
      NGIN::Memory::Shared<NGIN::IO::IFileSystem> fileSystem);

  auto CollectDescriptors(std::vector<ModuleDescriptor> &out) noexcept
      -> CoreResult<void> override;

private:
  std::vector<std::string> m_searchPaths;
  NGIN::Memory::Shared<NGIN::IO::IFileSystem> m_fileSystem{};
};

/// @brief Dynamic plugin binary-loader abstraction (Spec 003 seam).
class NGIN_CORE_API IPluginBinaryLoader {
public:
  virtual ~IPluginBinaryLoader() = default;

  /// @brief Load the binary behind a dynamic descriptor and return its factory.
  virtual auto LoadModuleFactory(const ModuleDescriptor &descriptor) noexcept
      -> CoreResult<ModuleFactory> = 0;
};

/// @brief Default dynamic plugin loader backed by NGIN::IO::DynamicLibrary.
class NGIN_CORE_API DynamicPluginBinaryLoader final : public IPluginBinaryLoader {
public:
  DynamicPluginBinaryLoader();
  ~DynamicPluginBinaryLoader() override;

  DynamicPluginBinaryLoader(DynamicPluginBinaryLoader &&) noexcept;
  auto operator=(DynamicPluginBinaryLoader &&) noexcept
      -> DynamicPluginBinaryLoader &;

  auto LoadModuleFactory(const ModuleDescriptor &descriptor) noexcept
      -> CoreResult<ModuleFactory> override;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

/// @brief Create a default per-kernel static module catalog.
NGIN_CORE_API auto CreateStaticModuleCatalog() noexcept
    -> NGIN::Memory::Shared<IModuleCatalog>;

/// @brief Create a default dynamic plugin binary loader.
NGIN_CORE_API auto CreateDynamicPluginBinaryLoader() noexcept
    -> NGIN::Memory::Shared<IPluginBinaryLoader>;
} // namespace NGIN::Core
