#include <NGIN/Core/Loader.hpp>

#include <NGIN/IO/DynamicLibrary.hpp>
#include <NGIN/IO/FileSystemUtilities.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>
#include <NGIN/Serialization/XML/XmlParser.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>

namespace NGIN::Core {
namespace {
using XmlDocument = NGIN::Serialization::XmlDocument;
using XmlElement = NGIN::Serialization::XmlElement;
using XmlNode = NGIN::Serialization::XmlNode;
using XmlParseOptions = NGIN::Serialization::XmlParseOptions;
using XmlParser = NGIN::Serialization::XmlParser;

[[nodiscard]] auto ToString(const NGIN::IO::Path &path) -> std::string {
  return std::string(path.View());
}

[[nodiscard]] auto ResolveDescriptorRelativePath(const std::string &filePath,
                                                 const std::string &rawPath)
    -> std::string {
  NGIN::IO::Path path(rawPath);
  if (!path.IsRelative()) {
    return ToString(path.LexicallyNormal());
  }

  return ToString(NGIN::IO::Path(filePath).Parent().Join(path.View()).LexicallyNormal());
}

[[nodiscard]] auto ParseModuleType(const std::string_view text) -> ModuleType {
  if (text == "Runtime")
    return ModuleType::Runtime;
  if (text == "Editor")
    return ModuleType::Editor;
  if (text == "Program")
    return ModuleType::Program;
  if (text == "Developer")
    return ModuleType::Developer;
  if (text == "ThirdParty")
    return ModuleType::ThirdParty;
  return ModuleType::Runtime;
}

[[nodiscard]] auto ParseModuleFamily(const std::string_view text)
    -> ModuleFamily {
  if (text == "Base")
    return ModuleFamily::Base;
  if (text == "Reflection")
    return ModuleFamily::Reflection;
  if (text == "Core")
    return ModuleFamily::Core;
  if (text == "Platform")
    return ModuleFamily::Platform;
  if (text == "Editor")
    return ModuleFamily::Editor;
  if (text == "Domain")
    return ModuleFamily::Domain;
  if (text == "App")
    return ModuleFamily::App;
  return ModuleFamily::Core;
}

[[nodiscard]] auto ParseStartupStage(const std::string_view text)
    -> std::optional<StartupStage> {
  if (text == "Foundation")
    return StartupStage::Foundation;
  if (text == "Platform")
    return StartupStage::Platform;
  if (text == "Services")
    return StartupStage::Services;
  if (text == "Features")
    return StartupStage::Features;
  if (text == "Presentation")
    return StartupStage::Presentation;
  return std::nullopt;
}

[[nodiscard]] auto Attribute(const XmlElement &element,
                             const std::string_view key)
    -> std::optional<std::string> {
  const auto *attribute = element.FindAttribute(key);
  if (attribute == nullptr) {
    return std::nullopt;
  }
  return std::string(attribute->value);
}

[[nodiscard]] auto FindChild(const XmlElement &element,
                             const std::string_view name)
    -> const XmlElement * {
  for (NGIN::UIntSize index = 0; index < element.children.Size(); ++index) {
    const auto &child = element.children[index];
    if (child.type == XmlNode::Type::Element && child.element != nullptr &&
        child.element->name == name) {
      return child.element;
    }
  }
  return nullptr;
}

[[nodiscard]] auto ChildElements(const XmlElement &element,
                                 const std::string_view name = {})
    -> std::vector<const XmlElement *> {
  std::vector<const XmlElement *> out{};
  out.reserve(static_cast<std::size_t>(element.children.Size()));
  for (NGIN::UIntSize index = 0; index < element.children.Size(); ++index) {
    const auto &child = element.children[index];
    if (child.type != XmlNode::Type::Element || child.element == nullptr) {
      continue;
    }
    if (name.empty() || child.element->name == name) {
      out.push_back(child.element);
    }
  }
  return out;
}

[[nodiscard]] auto ParseBoolAttribute(const XmlElement &element,
                                      const std::string_view key,
                                      const bool defaultValue = false) -> bool {
  const auto value = Attribute(element, key);
  if (!value.has_value()) {
    return defaultValue;
  }
  return value.value() == "true" || value.value() == "1" ||
         value.value() == "yes";
}

template <typename T>
void ReadStringRefs(const XmlElement *element, const std::string_view childName,
                    T &out) {
  if (element == nullptr) {
    return;
  }
  for (const auto *child : ChildElements(*element, childName)) {
    const auto value = Attribute(*child, "Name");
    if (value.has_value()) {
      out.emplace_back(value.value());
    }
  }
}

[[nodiscard]] auto ParseDescriptorFromXml(const std::string &filePath,
                                          const std::string_view xmlText,
                                          ModuleDescriptor &out) noexcept
    -> CoreResult<void> {
  XmlParseOptions options{};
  options.decodeEntities = true;
  options.arenaBytes = std::max<NGIN::UIntSize>(
      8192, static_cast<NGIN::UIntSize>(xmlText.size() * 8 + 4096));
  auto parsed = XmlParser::Parse(xmlText, options);
  if (!parsed) {
    return NGIN::Utilities::Unexpected<KernelError>(
        MakeKernelError(KernelErrorCode::ConfigFailure, "Loader", {},
                        "failed to parse plugin descriptor: " + filePath));
  }

  const XmlDocument &doc = parsed.Value();
  const auto *root = doc.Root();
  if (root == nullptr || root->name != "Module") {
    return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
        KernelErrorCode::InvalidArgument, "Loader", {},
        "plugin descriptor root must be <Module>: " + filePath));
  }

  const auto nameValue = Attribute(*root, "Name");
  if (!nameValue.has_value()) {
    return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
        KernelErrorCode::InvalidArgument, "Loader", {},
        "plugin descriptor missing attribute 'Name': " + filePath));
  }

  out.name = nameValue.value();
  out.entryKind = ModuleEntryKind::Dynamic;
  out.pluginRegistrar = "NGIN_RegisterPlugin";

  if (const auto library = Attribute(*root, "Library"); library.has_value()) {
    out.pluginLibrary = ResolveDescriptorRelativePath(filePath, library.value());
  }
  if (const auto registrar = Attribute(*root, "Registrar");
      registrar.has_value() && !registrar->empty()) {
    out.pluginRegistrar = registrar.value();
  }

  if (const auto family = Attribute(*root, "Family"); family.has_value()) {
    out.family = ParseModuleFamily(family.value());
  }

  if (const auto type = Attribute(*root, "Type"); type.has_value()) {
    out.type = ParseModuleType(type.value());
  }

  if (const auto stage = Attribute(*root, "StartupStage"); stage.has_value()) {
    const auto parsedStage = ParseStartupStage(stage.value());
    if (!parsedStage.has_value()) {
      return NGIN::Utilities::Unexpected<KernelError>(
          MakeKernelError(KernelErrorCode::InvalidArgument, "Loader", {},
                          "plugin descriptor contains unknown startup stage '" +
                              stage.value() + "': " + filePath));
    }
    out.startupStage = parsedStage.value();
  }

  if (const auto version = Attribute(*root, "Version"); version.has_value()) {
    auto parsedVersion = ParseSemanticVersion(version.value());
    if (!parsedVersion) {
      return NGIN::Utilities::Unexpected<KernelError>(parsedVersion.Error());
    }
    out.version = parsedVersion.Value();
  }

  if (const auto range = Attribute(*root, "CompatiblePlatformRange");
      range.has_value()) {
    auto parsedRange = ParseVersionRange(range.value());
    if (!parsedRange) {
      return NGIN::Utilities::Unexpected<KernelError>(parsedRange.Error());
    }
    out.compatiblePlatformRange = parsedRange.Value();
  }

  if (FindChild(*root, "Platforms") != nullptr) {
    return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
        KernelErrorCode::InvalidArgument, "Loader", {},
        "plugin descriptor <Platforms> is no longer supported: " +
            filePath));
  }
  if (FindChild(*root, "SupportedHosts") != nullptr) {
    return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
        KernelErrorCode::InvalidArgument, "Loader", {},
        "plugin descriptor <SupportedHosts> is no longer supported: " +
            filePath));
  }
  if (const auto *compatibility = FindChild(*root, "Compatibility")) {
    ReadStringRefs(FindChild(*compatibility, "OperatingSystems"),
                   "OperatingSystem", out.operatingSystems);
    ReadStringRefs(FindChild(*compatibility, "Architectures"), "Architecture",
                   out.architectures);
  }
  ReadStringRefs(FindChild(*root, "ProvidesServices"), "Service",
                 out.providesServices);
  ReadStringRefs(FindChild(*root, "RequiresServices"), "Service",
                 out.requiresServices);
  ReadStringRefs(FindChild(*root, "Capabilities"), "Capability",
                 out.capabilities);

  out.reflectionRequired =
      ParseBoolAttribute(*root, "ReflectionRequired", false);

  if (const auto *dependencies = FindChild(*root, "Dependencies")) {
    for (const auto *dependencyElement :
         ChildElements(*dependencies, "Dependency")) {
      const auto dependencyName = Attribute(*dependencyElement, "Name");
      if (!dependencyName.has_value()) {
        continue;
      }

      DependencyDescriptor dep{};
      dep.name = dependencyName.value();

      dep.optional = ParseBoolAttribute(*dependencyElement, "Optional", false);

      if (const auto depRange =
              Attribute(*dependencyElement, "RequiredVersion");
          depRange.has_value()) {
        auto parsedDepRange = ParseVersionRange(depRange.value());
        if (!parsedDepRange) {
          return NGIN::Utilities::Unexpected<KernelError>(
              parsedDepRange.Error());
        }
        dep.requiredVersion = parsedDepRange.Value();
      }

      out.dependencies.push_back(std::move(dep));
    }
  }

  return CoreResult<void>{};
}
} // namespace

auto StaticModuleCatalog::Register(
    StaticModuleRegistration registration) noexcept -> CoreResult<void> {
  if (registration.descriptor.name.empty()) {
    return NGIN::Utilities::Unexpected<KernelError>(
        MakeKernelError(KernelErrorCode::InvalidArgument, "Loader", {},
                        "static module name cannot be empty"));
  }
  if (!registration.factory) {
    return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
        KernelErrorCode::InvalidArgument, "Loader",
        registration.descriptor.name, "static module factory is empty"));
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  for (const auto &existing : m_entries) {
    if (existing.descriptor.name == registration.descriptor.name) {
      return NGIN::Utilities::Unexpected<KernelError>(
          MakeKernelError(KernelErrorCode::AlreadyExists, "Loader",
                          registration.descriptor.name,
                          "duplicate static module registration"));
    }
  }

  m_entries.emplace_back(std::move(registration));
  return CoreResult<void>{};
}

void StaticModuleCatalog::Clear() noexcept {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_entries.clear();
}

auto StaticModuleCatalog::Snapshot() const
    -> std::vector<StaticModuleRegistration> {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_entries;
}

class PluginModuleRegistry final : public IPluginModuleRegistry {
public:
  auto Register(std::string moduleName,
                ModuleFactory factory) noexcept -> CoreResult<void> override {
    if (moduleName.empty()) {
      return NGIN::Utilities::Unexpected<KernelError>(
          MakeKernelError(KernelErrorCode::InvalidArgument, "Plugin", {},
                          "plugin module registration name cannot be empty"));
    }
    if (!factory) {
      return NGIN::Utilities::Unexpected<KernelError>(
          MakeKernelError(KernelErrorCode::InvalidArgument, "Plugin",
                          moduleName,
                          "plugin module registration factory cannot be empty"));
    }
    if (factories.contains(moduleName)) {
      return NGIN::Utilities::Unexpected<KernelError>(
          MakeKernelError(KernelErrorCode::AlreadyExists, "Plugin",
                          moduleName,
                          "duplicate plugin module registration"));
    }

    factories.emplace(std::move(moduleName), std::move(factory));
    return CoreResult<void>{};
  }

  std::unordered_map<std::string, ModuleFactory> factories{};
};

struct DynamicPluginBinaryLoader::Impl {
  struct LoadedPlugin {
    std::unique_ptr<NGIN::IO::DynamicLibrary> library{};
    std::unordered_map<std::string, ModuleFactory> factories{};
  };

  std::unordered_map<std::string, LoadedPlugin> loadedByLibrary{};
};

DynamicPluginBinaryLoader::DynamicPluginBinaryLoader()
    : m_impl(std::make_unique<Impl>()) {}

DynamicPluginBinaryLoader::~DynamicPluginBinaryLoader() = default;

DynamicPluginBinaryLoader::DynamicPluginBinaryLoader(
    DynamicPluginBinaryLoader &&) noexcept = default;

auto DynamicPluginBinaryLoader::operator=(
    DynamicPluginBinaryLoader &&) noexcept -> DynamicPluginBinaryLoader & =
    default;

auto DynamicPluginBinaryLoader::LoadModuleFactory(
    const ModuleDescriptor &descriptor) noexcept -> CoreResult<ModuleFactory> {
  if (descriptor.pluginLibrary.empty()) {
    return NGIN::Utilities::Unexpected<KernelError>(
        MakeKernelError(KernelErrorCode::InvalidArgument, "Plugin",
                        descriptor.name,
                        "dynamic module descriptor missing Library"));
  }
  if (descriptor.pluginRegistrar.empty()) {
    return NGIN::Utilities::Unexpected<KernelError>(
        MakeKernelError(KernelErrorCode::InvalidArgument, "Plugin",
                        descriptor.name,
                        "dynamic module descriptor missing Registrar"));
  }

  try {
    auto it = m_impl->loadedByLibrary.find(descriptor.pluginLibrary);
    if (it == m_impl->loadedByLibrary.end()) {
      auto library = std::make_unique<NGIN::IO::DynamicLibrary>(
          descriptor.pluginLibrary, NGIN::IO::DynamicLibrary::LoadMode::Now);
      const auto registrar =
          library->Resolve<PluginRegistrarFn>(descriptor.pluginRegistrar);

      PluginModuleRegistry registry{};
      auto registered = registrar(registry);
      if (!registered) {
        return NGIN::Utilities::Unexpected<KernelError>(registered.Error());
      }

      Impl::LoadedPlugin loaded{};
      loaded.library = std::move(library);
      loaded.factories = std::move(registry.factories);
      it = m_impl->loadedByLibrary.emplace(descriptor.pluginLibrary,
                                           std::move(loaded))
               .first;
    }

    const auto factoryIt = it->second.factories.find(descriptor.name);
    if (factoryIt == it->second.factories.end()) {
      return NGIN::Utilities::Unexpected<KernelError>(
          MakeKernelError(KernelErrorCode::ModuleFactoryFailure, "Plugin",
                          descriptor.name,
                          "plugin registrar did not provide module factory"));
    }
    return factoryIt->second;
  } catch (const NGIN::IO::DynamicLibraryError &error) {
    return NGIN::Utilities::Unexpected<KernelError>(
        MakeKernelError(KernelErrorCode::DynamicPluginUnsupported, "Plugin",
                        descriptor.name, error.what()));
  } catch (const std::exception &error) {
    return NGIN::Utilities::Unexpected<KernelError>(
        MakeKernelError(KernelErrorCode::InternalError, "Plugin",
                        descriptor.name, error.what()));
  } catch (...) {
    return NGIN::Utilities::Unexpected<KernelError>(
        MakeKernelError(KernelErrorCode::InternalError, "Plugin",
                        descriptor.name, "dynamic plugin load failed"));
  }
}

FilesystemPluginCatalog::FilesystemPluginCatalog(
    std::vector<std::string> searchPaths)
    : FilesystemPluginCatalog(
          std::move(searchPaths),
          NGIN::Memory::MakeSharedAs<NGIN::IO::IFileSystem,
                                     NGIN::IO::LocalFileSystem>()) {}

FilesystemPluginCatalog::FilesystemPluginCatalog(
    std::vector<std::string> searchPaths,
    NGIN::Memory::Shared<NGIN::IO::IFileSystem> fileSystem)
    : m_searchPaths(std::move(searchPaths)),
      m_fileSystem(std::move(fileSystem)) {}

auto FilesystemPluginCatalog::CollectDescriptors(
    std::vector<ModuleDescriptor> &out) noexcept -> CoreResult<void> {
  try {
    if (!m_fileSystem) {
      return NGIN::Utilities::Unexpected<KernelError>(
          MakeKernelError(KernelErrorCode::InternalError, "Loader", {},
                          "filesystem plugin scan failed"));
    }

    for (const auto &rawPath : m_searchPaths) {
      if (rawPath.empty()) {
        continue;
      }

      const NGIN::IO::Path searchPath(rawPath);
      auto searchInfo = m_fileSystem->GetInfo(searchPath);
      if (!searchInfo || !searchInfo.Value().exists ||
          searchInfo.Value().type != NGIN::IO::EntryType::Directory) {
        continue;
      }

      NGIN::IO::EnumerateOptions options{};
      options.recursive = true;
      options.includeDirectories = false;
      options.populateInfo = true;

      auto entries = m_fileSystem->Enumerate(searchPath, options);
      if (!entries) {
        return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
            KernelErrorCode::InternalError, "Loader", ToString(searchPath),
            "filesystem plugin scan failed"));
      }

      while (true) {
        auto next = entries->Next();
        if (!next) {
          return NGIN::Utilities::Unexpected<KernelError>(MakeKernelError(
              KernelErrorCode::InternalError, "Loader", ToString(searchPath),
              "filesystem plugin scan failed"));
        }
        if (!next->HasEntry()) {
          break;
        }

        const auto &entry = next->Entry();
        if (entry.type != NGIN::IO::EntryType::File) {
          continue;
        }

        const auto &path = entry.path;
        if (path.Extension() != "xml") {
          continue;
        }

        const auto filename = path.Filename();
        if (!(filename.ends_with(".module.xml") ||
              filename.ends_with(".plugin-module.xml"))) {
          continue;
        }

        auto input = NGIN::IO::ReadAllText(*m_fileSystem, path);
        if (!input) {
          continue;
        }

        ModuleDescriptor descriptor{};
        auto parse = ParseDescriptorFromXml(ToString(path),
                                            input.Value().View(), descriptor);
        if (!parse) {
          return NGIN::Utilities::Unexpected<KernelError>(parse.Error());
        }

        if (descriptor.pluginName.empty()) {
          descriptor.pluginName = std::string(path.Parent().Filename());
        }
        descriptor.entryKind = ModuleEntryKind::Dynamic;
        out.push_back(std::move(descriptor));
      }
    }

    std::sort(out.begin(), out.end(),
              [](const ModuleDescriptor &lhs, const ModuleDescriptor &rhs) {
                return lhs.name < rhs.name;
              });
  } catch (...) {
    return NGIN::Utilities::Unexpected<KernelError>(
        MakeKernelError(KernelErrorCode::InternalError, "Loader", {},
                        "filesystem plugin scan failed"));
  }

  return CoreResult<void>{};
}

auto CreateStaticModuleCatalog() noexcept
    -> NGIN::Memory::Shared<IModuleCatalog> {
  return NGIN::Memory::MakeSharedAs<IModuleCatalog, StaticModuleCatalog>();
}

auto CreateDynamicPluginBinaryLoader() noexcept
    -> NGIN::Memory::Shared<IPluginBinaryLoader> {
  return NGIN::Memory::MakeSharedAs<IPluginBinaryLoader,
                                    DynamicPluginBinaryLoader>();
}
} // namespace NGIN::Core
