#include <NGIN/Runtime/Loader.hpp>

#include <mutex>
#include <unordered_map>

namespace NGIN::Runtime
{
    namespace
    {
        std::mutex                              g_staticRegistryMutex;
        std::vector<StaticModuleRegistration> g_staticRegistry;
    }

    auto RegisterStaticModule(StaticModuleRegistration registration) noexcept -> RuntimeResult<void>
    {
        if (registration.descriptor.name.empty())
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Loader", {}, "static module name cannot be empty"));
        }
        if (!registration.factory)
        {
            return NGIN::Utilities::Unexpected<KernelError>(
                MakeKernelError(KernelErrorCode::InvalidArgument, "Loader", registration.descriptor.name, "static module factory is empty"));
        }

        std::lock_guard<std::mutex> lock(g_staticRegistryMutex);
        for (const auto& existing : g_staticRegistry)
        {
            if (existing.descriptor.name == registration.descriptor.name)
            {
                return NGIN::Utilities::Unexpected<KernelError>(
                    MakeKernelError(KernelErrorCode::AlreadyExists, "Loader", registration.descriptor.name, "duplicate static module registration"));
            }
        }

        g_staticRegistry.emplace_back(std::move(registration));
        return RuntimeResult<void> {};
    }

    void ClearStaticModules() noexcept
    {
        std::lock_guard<std::mutex> lock(g_staticRegistryMutex);
        g_staticRegistry.clear();
    }

    auto GetStaticModules() noexcept -> std::vector<StaticModuleRegistration>
    {
        std::lock_guard<std::mutex> lock(g_staticRegistryMutex);
        return g_staticRegistry;
    }
}

