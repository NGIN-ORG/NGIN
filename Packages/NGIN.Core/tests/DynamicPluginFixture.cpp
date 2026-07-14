#include <NGIN/Core/Core.hpp>

#if defined(_WIN32)
#define NGIN_CORE_TEST_PLUGIN_EXPORT __declspec(dllexport)
#else
#define NGIN_CORE_TEST_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

namespace {
class DynamicFixtureModule final : public NGIN::Core::IModule {
public:
  auto OnStart(NGIN::Core::ModuleContext &context) noexcept
      -> NGIN::Core::CoreResult<void> override {
    auto result = context.RegisterSingletonValue<bool>(
        "Core.DynamicFixture.Ready", true);
    if (!result) {
      return result;
    }

    result = context.RegisterSingletonValue<std::string>(
        "Core.DynamicFixture.ModuleRoot", std::string(context.ModuleRoot()));
    if (!result) {
      return result;
    }

    result = context.RegisterSingletonValue<std::string>(
        "Core.DynamicFixture.DescriptorPath",
        std::string(context.DescriptorPath()));
    if (!result) {
      return result;
    }

    result = context.RegisterSingletonValue<std::string>(
        "Core.DynamicFixture.LibraryPath", std::string(context.LibraryPath()));
    if (!result) {
      return result;
    }

    result = context.RegisterSingletonValue<std::string>(
        "Core.DynamicFixture.PluginName", std::string(context.PluginName()));
    if (!result) {
      return result;
    }

    return context.RegisterSingletonValue<bool>(
        "Core.DynamicFixture.IsDynamic", context.IsDynamicModule());
  }
};
} // namespace

extern "C" NGIN_CORE_TEST_PLUGIN_EXPORT auto
NGIN_RegisterPlugin(NGIN::Core::IPluginModuleRegistry &registry)
    -> NGIN::Core::CoreResult<void> {
  return registry.Register(
      "Core.DynamicFixture",
      []() -> NGIN::Core::CoreResult<NGIN::Memory::Shared<NGIN::Core::IModule>> {
        return NGIN::Memory::MakeSharedAs<NGIN::Core::IModule,
                                          DynamicFixtureModule>();
      });
}

extern "C" NGIN_CORE_TEST_PLUGIN_EXPORT auto
NGIN_RegisterPluginFailing(NGIN::Core::IPluginModuleRegistry &)
    -> NGIN::Core::CoreResult<void> {
  return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
      NGIN::Core::MakeKernelError(
          NGIN::Core::KernelErrorCode::ModuleFactoryFailure, "Plugin",
          "Core.DynamicFixture", "forced registrar failure"));
}
