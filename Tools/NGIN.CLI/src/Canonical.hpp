#pragma once

#include <map>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <utility>

namespace NGIN::CLI
{
    struct CanonicalValue
    {
        using Object = std::map<std::string, CanonicalValue, std::less<>>;
        using Array = std::vector<CanonicalValue>;
        using Storage = std::variant<std::nullptr_t, bool, std::int64_t, std::string, Array, Object>;

        Storage value{nullptr};

        CanonicalValue() = default;
        CanonicalValue(std::nullptr_t) : value(nullptr) {}
        CanonicalValue(bool input) : value(input) {}
        CanonicalValue(std::int64_t input) : value(input) {}
        CanonicalValue(std::string input) : value(std::move(input)) {}
        CanonicalValue(const char *input) : value(std::string(input)) {}
        CanonicalValue(Array input) : value(std::move(input)) {}
        CanonicalValue(Object input) : value(std::move(input)) {}

        [[nodiscard]] friend auto operator==(const CanonicalValue &, const CanonicalValue &) -> bool = default;
    };

    [[nodiscard]] auto SerializeCanonical(const CanonicalValue &value) -> std::string;
    [[nodiscard]] auto CanonicalDigestInput(std::string_view kind, const CanonicalValue::Object &fields)
        -> std::string;
    [[nodiscard]] auto Sha256Fingerprint(std::string_view value) -> std::string;
    [[nodiscard]] auto CanonicalFingerprint(std::string_view kind, const CanonicalValue::Object &fields)
        -> std::string;
}
