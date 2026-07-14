#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace NGIN::Reflection::MetaGen
{
    namespace fs = std::filesystem;

    struct MetaGenContext
    {
        std::string generator{};
        std::string projectName{};
        std::string profileName{};
        std::string platform{};
        std::string optimization{};
        bool debugSymbols{true};
        bool linkTimeOptimization{false};
        std::string backendConfiguration{};
        std::string operatingSystem{};
        std::string architecture{};
        std::string environment{};
        fs::path projectDir{};
        fs::path outputDir{};
        fs::path generatedDir{};
        std::string languageStandard{"23"};
        std::vector<fs::path> sourceFiles{};
        std::vector<fs::path> sourceRoots{};
        std::vector<fs::path> includeDirectories{};
        std::vector<std::string> compileDefinitions{};
        std::vector<std::string> compileOptions{};
        std::vector<fs::path> outputs{};
    };

    struct MetaGenResult
    {
        bool available{true};
        std::vector<fs::path> generatedFiles{};
        std::size_t reflectedTypeCount{0};
        std::vector<std::string> diagnostics{};
    };

    [[nodiscard]] auto ReadContext(const fs::path &path, std::vector<std::string> &diagnostics) -> MetaGenContext;
    [[nodiscard]] auto GenerateMetaData(const MetaGenContext &context) -> MetaGenResult;
}
