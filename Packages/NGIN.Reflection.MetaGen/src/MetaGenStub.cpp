#include "MetaGenContext.hpp"

namespace NGIN::Reflection::MetaGen
{
    auto GenerateMetaData(const MetaGenContext &) -> MetaGenResult
    {
        return MetaGenResult{
            .available = false,
            .diagnostics = {
                "NGIN.Reflection.MetaGen was built without Clang support. Install LLVM/Clang development packages and configure with NGIN_REFLECTION_METAGEN_ENABLE_CLANG=ON.",
            },
        };
    }
}
