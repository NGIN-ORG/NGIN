#pragma once

#include <NGIN/Primitives.hpp>
#include <NGIN/UI/Error.hpp>
#include <NGIN/UI/Geometry.hpp>
#include <NGIN/UI/Handles.hpp>
#include <NGIN/UI/Invalidation.hpp>
#include <NGIN/UI/RoutedEvent.hpp>
#include <NGIN/UI/Semantics.hpp>
#include <NGIN/UI/Style.hpp>

#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace NGIN::UI {
/// @brief Records backend-neutral drawing commands for a custom element.
class DisplayListBuilder;

/// @brief Persistent, type-safe state storage keyed by a custom element instance.
class CustomStateStore final {
public:
  CustomStateStore() = default;
  CustomStateStore(const CustomStateStore &) = delete;
  CustomStateStore(CustomStateStore &&) = delete;
  auto operator=(const CustomStateStore &) -> CustomStateStore & = delete;
  auto operator=(CustomStateStore &&) -> CustomStateStore & = delete;
  ~CustomStateStore() = default;

  template <typename T, typename... Args>
  [[nodiscard]] auto GetOrCreate(std::string_view key, Args &&...args) noexcept
      -> UIResult<T *> {
    try {
      const auto ownedKey = std::string{key};
      const auto found = m_entries.find(ownedKey);
      if (found != m_entries.end()) {
        if (found->second->Type() != std::type_index{typeid(T)}) {
          return MakeUIError(
              UIErrorCode::InvalidState,
              "Custom state key was reused with a different type", "NGIN.UI",
              "CustomStateStore::GetOrCreate", ownedKey.c_str());
        }
        return &static_cast<Entry<T> &>(*found->second).value;
      }

      auto entry = std::make_unique<Entry<T>>(std::forward<Args>(args)...);
      auto *value = &entry->value;
      m_entries.emplace(ownedKey, std::move(entry));
      return value;
    } catch (const std::bad_alloc &) {
      return MakeUIError(UIErrorCode::OutOfMemory,
                         "Custom state allocation failed", "NGIN.UI",
                         "CustomStateStore::GetOrCreate");
    } catch (...) {
      return MakeUIError(UIErrorCode::InvalidState,
                         "Custom state construction threw an exception",
                         "NGIN.UI", "CustomStateStore::GetOrCreate");
    }
  }

  template <typename T>
  [[nodiscard]] auto Find(std::string_view key) noexcept -> T * {
    try {
      const auto found = m_entries.find(std::string{key});
      if (found == m_entries.end() ||
          found->second->Type() != std::type_index{typeid(T)}) {
        return nullptr;
      }
      return &static_cast<Entry<T> &>(*found->second).value;
    } catch (...) {
      return nullptr;
    }
  }

  template <typename T>
  [[nodiscard]] auto Find(std::string_view key) const noexcept -> const T * {
    try {
      const auto found = m_entries.find(std::string{key});
      if (found == m_entries.end() ||
          found->second->Type() != std::type_index{typeid(T)}) {
        return nullptr;
      }
      return &static_cast<const Entry<T> &>(*found->second).value;
    } catch (...) {
      return nullptr;
    }
  }

  [[nodiscard]] auto Size() const noexcept -> UIntSize {
    return m_entries.size();
  }

private:
  struct EntryBase {
    virtual ~EntryBase() = default;
    [[nodiscard]] virtual auto Type() const noexcept -> std::type_index = 0;
  };

  template <typename T> struct Entry final : EntryBase {
    template <typename... Args>
    explicit Entry(Args &&...args) : value(std::forward<Args>(args)...) {}

    [[nodiscard]] auto Type() const noexcept -> std::type_index override {
      return std::type_index{typeid(T)};
    }

    T value;
  };

  std::unordered_map<std::string, std::unique_ptr<EntryBase>> m_entries{};
};

/// @brief Hover, press, focus, enablement, and pointer position for custom paint.
struct CustomInteractionState final {
  bool hovered{false};
  bool pressed{false};
  bool focused{false};
  bool enabled{true};
};

/// @brief Runtime services and retained state supplied to a custom element.
class CustomElementContext final {
public:
  CustomElementContext(CustomStateStore &state, ElementId identity,
                       Rect arrangedBounds, CustomInteractionState interaction,
                       F32 scaleFactor) noexcept;

  template <typename T, typename... Args>
  [[nodiscard]] auto State(std::string_view key, Args &&...args) noexcept
      -> UIResult<T *> {
    return m_state->GetOrCreate<T>(key, std::forward<Args>(args)...);
  }

  template <typename T>
  [[nodiscard]] auto FindState(std::string_view key) noexcept -> T * {
    return m_state->Find<T>(key);
  }

  [[nodiscard]] auto Identity() const noexcept -> ElementId;
  [[nodiscard]] auto ArrangedSize() const noexcept -> Size;
  [[nodiscard]] auto ToLocal(Point windowPoint) const noexcept -> Point;
  [[nodiscard]] auto Interaction() const noexcept -> CustomInteractionState;
  [[nodiscard]] auto ScaleFactor() const noexcept -> F32;

private:
  CustomStateStore *m_state{nullptr};
  ElementId m_identity{};
  Rect m_arrangedBounds{};
  CustomInteractionState m_interaction{};
  F32 m_scaleFactor{1.0F};
};

/// @brief Drawing surface, bounds, scale, and interaction data for custom paint.
class PaintContext final {
public:
  PaintContext(DisplayListBuilder &builder, Size extent) noexcept;

  [[nodiscard]] auto Extent() const noexcept -> Size;
  [[nodiscard]] auto Bounds() const noexcept -> Rect;
  void Fill(Rect rect, Color color);
  void FillRounded(Rect rect, CornerRadius radius, Color color);
  void Stroke(Rect rect, F32 thickness, Color color);
  void StrokeRounded(Rect rect, CornerRadius radius, F32 thickness,
                     Color color);
  void Image(TextureHandle texture, Rect destination,
             Color tint = Color{1.0F, 1.0F, 1.0F, 1.0F});

private:
  DisplayListBuilder *m_builder{nullptr};
  Size m_extent{};
};

/// @brief Extension interface for custom measurement, painting, and semantics.
class ICustomElement {
public:
  virtual ~ICustomElement() = default;

  [[nodiscard]] virtual auto Measure(CustomElementContext &context,
                                     SizeConstraints constraints)
      -> UIResult<Size> = 0;
  virtual auto Arrange(CustomElementContext &context, Size finalSize)
      -> UIResult<void>;
  virtual auto Paint(CustomElementContext &context, PaintContext &paint)
      -> UIResult<void> = 0;
  [[nodiscard]] virtual auto Semantics(CustomElementContext &context)
      -> UIResult<SemanticProperties>;
  virtual auto PointerEvent(CustomElementContext &context,
                            RoutedPointerEvent &event)
      -> UIResult<InvalidationKind>;
  virtual auto KeyEvent(CustomElementContext &context, RoutedKeyEvent &event)
      -> UIResult<InvalidationKind>;
  virtual auto TextEvent(CustomElementContext &context, RoutedTextEvent &event)
      -> UIResult<InvalidationKind>;
  virtual void Unmounted(CustomElementContext &context) noexcept;
};
} // namespace NGIN::UI
