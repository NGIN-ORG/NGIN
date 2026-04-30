#pragma once

#include <map>
#include <string>
#include <string_view>

namespace NGIN::Reflection::MetaGen
{
    struct Annotation
    {
        std::string kind{};
        std::map<std::string, std::string> options{};
    };

    [[nodiscard]] auto ParseAnnotation(std::string_view payload) -> Annotation;
    [[nodiscard]] auto EscapeCppString(std::string_view input) -> std::string;
    [[nodiscard]] auto SanitizeIdentifier(std::string_view input) -> std::string;
}
