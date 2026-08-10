#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace NGIN::CLI
{
    [[nodiscard]] auto FormatManifestXml(std::string_view source) -> std::string;
    [[nodiscard]] auto FormatManifestFile(const std::filesystem::path &path) -> std::string;
}
