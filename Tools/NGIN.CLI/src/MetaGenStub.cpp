#include "MetaGen.hpp"

#include <iostream>

namespace NGIN::CLI
{
    auto GenerateMetaData(
        const fs::path &,
        const ProjectManifest &,
        const ConfigurationDefinition &,
        const fs::path &) -> MetaGenResult
    {
        return MetaGenResult{
            .available = false,
            .diagnostics = {
                "ngin metagen was built without Clang support. Install LLVM/Clang development packages and configure with NGIN_CLI_ENABLE_METAGEN=ON.",
            },
        };
    }

    auto RunMetaGen(
        const fs::path &,
        const ProjectManifest &,
        const ConfigurationDefinition &,
        const std::optional<std::string> &) -> int
    {
        std::cerr
            << "error: ngin metagen was built without Clang support\n"
            << "hint: install LLVM/Clang development packages and configure with NGIN_CLI_ENABLE_METAGEN=ON\n";
        return 1;
    }
}
