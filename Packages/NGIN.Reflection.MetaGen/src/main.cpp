#include "MetaGenContext.hpp"

#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
    auto PrintUsage() -> void
    {
        std::cerr << "usage: ngin-metagen --context <file>\n";
    }
}

auto main(int argc, char **argv) -> int
{
    std::filesystem::path contextPath{};
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument{argv[index]};
        if (argument == "--context" && index + 1 < argc)
        {
            contextPath = argv[++index];
            continue;
        }
        PrintUsage();
        return 2;
    }

    if (contextPath.empty())
    {
        PrintUsage();
        return 2;
    }

    std::vector<std::string> diagnostics{};
    auto context = NGIN::Reflection::MetaGen::ReadContext(contextPath, diagnostics);
    if (!diagnostics.empty())
    {
        for (const auto &diagnostic : diagnostics)
        {
            std::cerr << "error: " << diagnostic << "\n";
        }
        return 1;
    }

    auto result = NGIN::Reflection::MetaGen::GenerateMetaData(context);
    if (!result.available || !result.diagnostics.empty())
    {
        for (const auto &diagnostic : result.diagnostics)
        {
            std::cerr << "error: " << diagnostic << "\n";
        }
        return 1;
    }

    for (const auto &file : result.generatedFiles)
    {
        std::cout << "generated: " << file.string() << "\n";
    }
    std::cout << "reflected types: " << result.reflectedTypeCount << "\n";
    return 0;
}
