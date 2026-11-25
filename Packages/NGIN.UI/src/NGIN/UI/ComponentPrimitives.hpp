#pragma once

#include <NGIN/UI/Components.hpp>
#include "Composer.hpp"

namespace NGIN::UI::Views {
/// @brief A container invokes its child emitter once for each indexed child.
using ChildEmitter = Utilities::Callable<void(UIntSize)>;
/// @brief Layout-aware bridge for containers implemented with the composer API.
using ContainerBuilder =
    Utilities::Callable<void(Composer &, const ViewEnvironment &,
                             std::string_view, const ChildEmitter &)>;

namespace Detail {
[[nodiscard]] auto ContainerWithType(ContainerBuilder build,
                                     std::vector<View> children,
                                     std::type_index type) -> View;
} // namespace Detail

template <typename Factory>
[[nodiscard]] auto Container(Factory build, std::vector<View> children)
    -> View {
  return Detail::ContainerWithType(std::move(build), std::move(children),
                                   typeid(Factory));
}

template <typename Factory> [[nodiscard]] auto Element(Factory build) -> View {
  return Detail::ContainerWithType(
      [build = std::move(build)](
          Composer &composer, const ViewEnvironment &env, std::string_view key,
          const ChildEmitter &) mutable { build(composer, env, key); },
      {}, typeid(Factory));
}

/// @brief Creates one independently owned composition callback and subscription
/// lifetime.
[[nodiscard]] auto Mount(View view, ViewEnvironment environment,
                         InvalidationScheduler invalidate)
    -> Utilities::Callable<void(Composer &)>;
} // namespace NGIN::UI::Views
