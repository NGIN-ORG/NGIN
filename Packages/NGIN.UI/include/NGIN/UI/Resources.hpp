#pragma once

#include <NGIN/UI/TextDirection.hpp>
#include <NGIN/UI/Theme.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>

namespace NGIN::UI {
/// @brief Typed identity used to store and resolve a scoped resource.
template <typename T> class ResourceKey final {
public:
  explicit ResourceKey(std::string_view name) : m_name(name) {}

  [[nodiscard]] auto Name() const noexcept -> const std::string & {
    return m_name;
  }

private:
  std::string m_name;
};

/// @brief Immutable hierarchical map of theme or application resources.
class ResourceScope final {
public:
  explicit ResourceScope(std::shared_ptr<const ResourceScope> parent = {})
      : m_parent(std::move(parent)) {}

  template <typename T> void Provide(const ResourceKey<T> &key, T value) {
    m_values[Key{std::type_index{typeid(T)}, key.Name()}] =
        std::make_shared<const T>(std::move(value));
  }

  template <typename T>
  [[nodiscard]] auto Resolve(const ResourceKey<T> &key) const noexcept
      -> const T * {
    const auto found =
        m_values.find(Key{std::type_index{typeid(T)}, key.Name()});
    if (found != m_values.end()) {
      return static_cast<const T *>(found->second.get());
    }
    return m_parent ? m_parent->Resolve(key) : nullptr;
  }

  [[nodiscard]] auto Parent() const noexcept
      -> const std::shared_ptr<const ResourceScope> & {
    return m_parent;
  }

private:
  struct Key final {
    std::type_index type{typeid(void)};
    std::string name{};

    [[nodiscard]] auto operator==(const Key &) const noexcept -> bool = default;
  };

  struct KeyHash final {
    [[nodiscard]] auto operator()(const Key &key) const noexcept
        -> std::size_t {
      const auto typeHash = key.type.hash_code();
      const auto nameHash = std::hash<std::string>{}(key.name);
      return typeHash ^
             (nameHash + 0x9e3779b9U + (typeHash << 6U) + (typeHash >> 2U));
    }
  };

  std::shared_ptr<const ResourceScope> m_parent{};
  std::unordered_map<Key, std::shared_ptr<const void>, KeyHash> m_values{};
};

inline const ResourceKey<Theme> ThemeResource{"NGIN.UI.Theme"};
inline const ResourceKey<std::string> LocaleResource{"NGIN.UI.Locale"};
inline const ResourceKey<TextDirection> TextDirectionResource{
    "NGIN.UI.TextDirection"};
inline const ResourceKey<bool> ReducedMotionResource{"NGIN.UI.ReducedMotion"};
inline const ResourceKey<bool> HighContrastResource{"NGIN.UI.HighContrast"};
} // namespace NGIN::UI
