#include "DependencyLock.hpp"

#include "Canonical.hpp"

#include <NGIN/Serialization/Core/SourceBuffer.hpp>
#include <NGIN/Serialization/JSON/JsonParser.hpp>

#include <algorithm>
#include <map>
#include <tuple>

namespace NGIN::CLI
{
    namespace
    {
        using JsonObject = NGIN::Serialization::JSON::ObjectView;
        using JsonValue = NGIN::Serialization::JSON::ValueView;

        [[nodiscard]] auto ContextName(const PackageInstanceContext context) -> std::string
        {
            return context == PackageInstanceContext::Host ? "Host" : "Target";
        }

        template <typename Comparator>
        [[nodiscard]] auto OptionsValue(const std::map<std::string, std::string, Comparator> &options)
            -> CanonicalValue
        {
            CanonicalValue::Object value{};
            for (const auto &[name, option] : options) value.emplace(name, option);
            return value;
        }

        [[nodiscard]] auto CompatibilityValue(const BinaryCompatibility &compatibility) -> CanonicalValue
        {
            CanonicalValue::Object options{};
            for (const auto &[name, value] : compatibility.artifactOptions) options.emplace(name, value);
            return CanonicalValue::Object{{"architecture", compatibility.architecture},
                                          {"compiler", compatibility.compiler},
                                          {"compilerVersion", compatibility.compilerVersion},
                                          {"configuration", compatibility.configuration},
                                          {"linkage", compatibility.linkage},
                                          {"operatingSystem", compatibility.operatingSystem},
                                          {"options", options},
                                          {"runtimeLibrary", compatibility.runtimeLibrary}};
        }

        [[nodiscard]] auto EntryValue(const DependencyLockEntry &entry) -> CanonicalValue
        {
            return CanonicalValue::Object{{"artifactIdentity", entry.artifactIdentity},
                                          {"artifactOptions", OptionsValue(entry.artifactOptions)},
                                          {"compatibility", CompatibilityValue(entry.compatibility)},
                                          {"context", ContextName(entry.context)},
                                          {"hermetic", entry.hermetic},
                                          {"integrity", entry.integrity},
                                          {"name", entry.coordinate.name},
                                          {"packageInstance", entry.packageInstance},
                                          {"providerCoordinate", entry.providerCoordinate},
                                          {"providerKind", entry.providerKind},
                                          {"providerVersion", entry.providerVersion},
                                          {"revision", entry.revision},
                                          {"source", entry.coordinate.sourceBinding.value_or("")},
                                          {"version", entry.coordinate.exactVersion}};
        }

        auto AddParseError(std::vector<ManifestDiagnostic> &diagnostics, std::string message,
                           const std::size_t line = 0, const std::size_t column = 0) -> void
        {
            diagnostics.push_back(ManifestDiagnostic{.severity = ManifestDiagnosticSeverity::Error,
                                                       .code = "NGIN8001",
                                                       .message = std::move(message),
                                                       .source = ManifestSourceRange{
                                                           .begin = ManifestSourcePosition{.line = line, .column = column}}});
        }

        [[nodiscard]] auto Required(const JsonObject &object, const std::string_view key,
                                    std::vector<ManifestDiagnostic> &diagnostics) -> std::optional<JsonValue>
        {
            const auto value = object.Find(key);
            if (!value.has_value()) AddParseError(diagnostics, "dependency lock is missing '" + std::string(key) + "'");
            return value;
        }

        [[nodiscard]] auto StringMember(const JsonObject &object, const std::string_view key,
                                        std::vector<ManifestDiagnostic> &diagnostics) -> std::optional<std::string>
        {
            const auto value = Required(object, key, diagnostics);
            if (!value.has_value()) return std::nullopt;
            const auto text = value->TryString();
            if (!text.has_value())
            {
                AddParseError(diagnostics, "dependency lock field '" + std::string(key) + "' must be a string");
                return std::nullopt;
            }
            return std::string(*text);
        }

        [[nodiscard]] auto BoolMember(const JsonObject &object, const std::string_view key,
                                      std::vector<ManifestDiagnostic> &diagnostics) -> std::optional<bool>
        {
            const auto value = Required(object, key, diagnostics);
            if (!value.has_value()) return std::nullopt;
            const auto boolean = value->TryBool();
            if (!boolean.has_value()) AddParseError(diagnostics, "dependency lock field '" + std::string(key) + "' must be Boolean");
            return boolean;
        }

        [[nodiscard]] auto StringMap(const JsonObject &object, const std::string_view key,
                                     std::vector<ManifestDiagnostic> &diagnostics)
            -> std::optional<std::map<std::string, std::string, std::less<>>>
        {
            const auto value = Required(object, key, diagnostics);
            if (!value.has_value()) return std::nullopt;
            const auto map = value->TryObject();
            if (!map.has_value())
            {
                AddParseError(diagnostics, "dependency lock field '" + std::string(key) + "' must be an object");
                return std::nullopt;
            }
            std::map<std::string, std::string, std::less<>> result{};
            for (const auto member : *map)
            {
                const auto text = member.Value().TryString();
                if (!text.has_value())
                {
                    AddParseError(diagnostics, "dependency lock option '" + std::string(member.Key()) + "' must be a string");
                    return std::nullopt;
                }
                result.emplace(member.Key(), *text);
            }
            return result;
        }

        [[nodiscard]] auto ParseCompatibility(const JsonObject &object,
                                              std::vector<ManifestDiagnostic> &diagnostics)
            -> std::optional<BinaryCompatibility>
        {
            BinaryCompatibility value{};
            const auto operatingSystem = StringMember(object, "operatingSystem", diagnostics);
            const auto architecture = StringMember(object, "architecture", diagnostics);
            const auto compiler = StringMember(object, "compiler", diagnostics);
            const auto compilerVersion = StringMember(object, "compilerVersion", diagnostics);
            const auto runtimeLibrary = StringMember(object, "runtimeLibrary", diagnostics);
            const auto configuration = StringMember(object, "configuration", diagnostics);
            const auto linkage = StringMember(object, "linkage", diagnostics);
            const auto options = StringMap(object, "options", diagnostics);
            if (!operatingSystem || !architecture || !compiler || !compilerVersion || !runtimeLibrary ||
                !configuration || !linkage || !options) return std::nullopt;
            value.operatingSystem = *operatingSystem;
            value.architecture = *architecture;
            value.compiler = *compiler;
            value.compilerVersion = *compilerVersion;
            value.runtimeLibrary = *runtimeLibrary;
            value.configuration = *configuration;
            value.linkage = *linkage;
            value.artifactOptions.insert(options->begin(), options->end());
            return value;
        }

        [[nodiscard]] auto LogicalKey(const DependencyLockEntry &entry) -> std::string
        {
            return entry.coordinate.name + "|" + ContextName(entry.context) + "|" +
                   entry.coordinate.sourceBinding.value_or("");
        }

        auto Difference(LockVerificationResult &result, const DependencyLockEntry &expected,
                        const std::string_view field, const std::string &left, const std::string &right) -> void
        {
            if (left == right) return;
            result.invalidations.push_back(LockInvalidation{.package = LogicalKey(expected),
                                                             .field = std::string(field),
                                                             .expected = left,
                                                             .actual = right,
                                                             .reason = std::string(field) + " changed"});
        }
    }

    ResolvedDependencyLock::ResolvedDependencyLock(DependencyLockData data)
    {
        std::ranges::sort(data.packages, [](const auto &left, const auto &right) {
            return left.packageInstance < right.packageInstance;
        });
        data_ = std::make_shared<const DependencyLockData>(std::move(data));
        canonical_ = SerializeDependencyLock(*data_);
        fingerprint_ = CanonicalFingerprint("DependencyLockFingerprint", {{"lock", canonical_}});
    }

    auto ResolvedDependencyLock::Data() const -> const DependencyLockData & { return *data_; }
    auto ResolvedDependencyLock::CanonicalSerialization() const -> std::string { return canonical_; }
    auto ResolvedDependencyLock::Fingerprint() const -> std::string { return fingerprint_; }

    auto DependencyLockParseResult::Succeeded() const -> bool { return lock.has_value() && diagnostics.empty(); }

    auto CreateDependencyLock(const ResolvedCompositionGraph &graph) -> ResolvedDependencyLock
    {
        DependencyLockData lock{};
        for (const auto &package : graph.Data().packages)
            lock.packages.push_back(DependencyLockEntry{.packageInstance = package.identity,
                                                        .coordinate = package.coordinate,
                                                        .context = package.context,
                                                        .providerKind = package.providerKind,
                                                        .providerCoordinate = package.providerIdentity,
                                                        .providerVersion = package.providerVersion,
                                                        .revision = package.revision,
                                                        .integrity = package.integrity,
                                                        .artifactIdentity = package.artifactIdentity,
                                                        .hermetic = package.hermetic,
                                                        .compatibility = package.compatibility,
                                                        .artifactOptions = package.artifactOptions});
        return ResolvedDependencyLock{std::move(lock)};
    }

    auto SerializeDependencyLock(const DependencyLockData &lock) -> std::string
    {
        CanonicalValue::Array packages{};
        for (const auto &entry : lock.packages) packages.push_back(EntryValue(entry));
        return SerializeCanonical(CanonicalValue::Object{{"kind", "NGIN.DependencyLock"}, {"packages", packages}});
    }

    auto ParseDependencyLock(const std::string_view text) -> DependencyLockParseResult
    {
        DependencyLockParseResult result{};
        auto parsed = NGIN::Serialization::JSON::Parse(NGIN::Serialization::OwnedTextBuffer{text});
        if (!parsed.HasValue())
        {
            const auto &error = parsed.Error();
            AddParseError(result.diagnostics, "invalid dependency lock JSON: " + std::string(error.message.View()),
                          error.location.line, error.location.column);
            return result;
        }
        const auto root = parsed.Value().Root().TryObject();
        if (!root.has_value())
        {
            AddParseError(result.diagnostics, "dependency lock root must be an object");
            return result;
        }
        const auto kind = StringMember(*root, "kind", result.diagnostics);
        if (!kind || *kind != "NGIN.DependencyLock")
        {
            AddParseError(result.diagnostics, "dependency lock kind must be 'NGIN.DependencyLock'");
            return result;
        }
        const auto packageValue = Required(*root, "packages", result.diagnostics);
        const auto packages = packageValue ? packageValue->TryArray() : std::nullopt;
        if (!packages.has_value())
        {
            AddParseError(result.diagnostics, "dependency lock 'packages' must be an array");
            return result;
        }
        DependencyLockData data{};
        for (const auto value : *packages)
        {
            const auto object = value.TryObject();
            if (!object.has_value())
            {
                AddParseError(result.diagnostics, "dependency lock package entry must be an object");
                continue;
            }
            DependencyLockEntry entry{};
            const auto packageInstance = StringMember(*object, "packageInstance", result.diagnostics);
            const auto name = StringMember(*object, "name", result.diagnostics);
            const auto version = StringMember(*object, "version", result.diagnostics);
            const auto source = StringMember(*object, "source", result.diagnostics);
            const auto context = StringMember(*object, "context", result.diagnostics);
            const auto providerKind = StringMember(*object, "providerKind", result.diagnostics);
            const auto providerCoordinate = StringMember(*object, "providerCoordinate", result.diagnostics);
            const auto providerVersion = StringMember(*object, "providerVersion", result.diagnostics);
            const auto revision = StringMember(*object, "revision", result.diagnostics);
            const auto integrity = StringMember(*object, "integrity", result.diagnostics);
            const auto artifactIdentity = StringMember(*object, "artifactIdentity", result.diagnostics);
            const auto hermetic = BoolMember(*object, "hermetic", result.diagnostics);
            const auto artifactOptions = StringMap(*object, "artifactOptions", result.diagnostics);
            const auto compatibilityValue = Required(*object, "compatibility", result.diagnostics);
            const auto compatibilityObject = compatibilityValue ? compatibilityValue->TryObject() : std::nullopt;
            const auto compatibility = compatibilityObject ? ParseCompatibility(*compatibilityObject, result.diagnostics)
                                                           : std::nullopt;
            if (!compatibilityObject) AddParseError(result.diagnostics, "dependency lock 'compatibility' must be an object");
            if (!packageInstance || !name || !version || !source || !context || !providerKind ||
                !providerCoordinate || !providerVersion || !revision || !integrity || !artifactIdentity ||
                !hermetic || !artifactOptions || !compatibility) continue;
            if (*context != "Host" && *context != "Target")
            {
                AddParseError(result.diagnostics, "dependency lock context must be 'Host' or 'Target'");
                continue;
            }
            entry.packageInstance = *packageInstance;
            entry.coordinate = PackageCoordinate{.name = *name, .exactVersion = *version,
                                                  .sourceBinding = source->empty() ? std::nullopt
                                                                                  : std::optional<std::string>{*source}};
            entry.context = *context == "Host" ? PackageInstanceContext::Host : PackageInstanceContext::Target;
            entry.providerKind = *providerKind;
            entry.providerCoordinate = *providerCoordinate;
            entry.providerVersion = *providerVersion;
            entry.revision = *revision;
            entry.integrity = *integrity;
            entry.artifactIdentity = *artifactIdentity;
            entry.hermetic = *hermetic;
            entry.artifactOptions = *artifactOptions;
            entry.compatibility = *compatibility;
            data.packages.push_back(std::move(entry));
        }
        if (result.diagnostics.empty()) result.lock.emplace(std::move(data));
        return result;
    }

    auto VerifyDependencyLock(const ResolvedDependencyLock &expected, const ResolvedDependencyLock &actual,
                              const LockVerificationPolicy &policy) -> LockVerificationResult
    {
        LockVerificationResult result{};
        std::map<std::string, std::vector<const DependencyLockEntry *>, std::less<>> left{};
        std::map<std::string, std::vector<const DependencyLockEntry *>, std::less<>> right{};
        for (const auto &entry : expected.Data().packages) left[LogicalKey(entry)].push_back(&entry);
        for (const auto &entry : actual.Data().packages) right[LogicalKey(entry)].push_back(&entry);
        const auto sortEntries = [](auto &buckets) {
            for (auto &[_, entries] : buckets)
                std::ranges::sort(entries, [](const auto *a, const auto *b) {
                    return std::tie(a->coordinate.exactVersion, a->providerKind, a->providerCoordinate) <
                           std::tie(b->coordinate.exactVersion, b->providerKind, b->providerCoordinate);
                });
        };
        sortEntries(left);
        sortEntries(right);
        for (const auto &[key, entries] : left)
        {
            const auto found = right.find(key);
            if (found == right.end())
            {
                result.invalidations.push_back({.package = key, .field = "package", .expected = "present",
                                                .actual = "missing", .reason = "locked package is no longer resolved"});
                continue;
            }
            const auto count = std::min(entries.size(), found->second.size());
            for (std::size_t index = 0; index < count; ++index)
            {
                const auto &entry = *entries[index];
                const auto &other = *found->second[index];
                Difference(result, entry, "packageInstance", entry.packageInstance, other.packageInstance);
                Difference(result, entry, "version", entry.coordinate.exactVersion, other.coordinate.exactVersion);
                Difference(result, entry, "providerKind", entry.providerKind, other.providerKind);
                Difference(result, entry, "providerCoordinate", entry.providerCoordinate, other.providerCoordinate);
                Difference(result, entry, "providerVersion", entry.providerVersion, other.providerVersion);
                Difference(result, entry, "revision", entry.revision, other.revision);
                Difference(result, entry, "integrity", entry.integrity, other.integrity);
                Difference(result, entry, "artifactIdentity", entry.artifactIdentity, other.artifactIdentity);
                Difference(result, entry, "hermetic", entry.hermetic ? "true" : "false",
                           other.hermetic ? "true" : "false");
                Difference(result, entry, "compatibility", SerializeCanonical(CompatibilityValue(entry.compatibility)),
                           SerializeCanonical(CompatibilityValue(other.compatibility)));
                Difference(result, entry, "artifactOptions", SerializeCanonical(OptionsValue(entry.artifactOptions)),
                           SerializeCanonical(OptionsValue(other.artifactOptions)));
            }
            if (entries.size() != found->second.size())
                result.invalidations.push_back({.package = key, .field = "instances",
                                                .expected = std::to_string(entries.size()),
                                                .actual = std::to_string(found->second.size()),
                                                .reason = "coexisting PackageInstance count changed"});
        }
        for (const auto &[key, _] : right)
            if (!left.contains(key))
                result.invalidations.push_back({.package = key, .field = "package", .expected = "absent",
                                                .actual = "present", .reason = "resolution acquired an additional package"});
        for (const auto &entry : actual.Data().packages)
        {
            if (policy.requireHermetic && !entry.hermetic)
                result.invalidations.push_back({.package = LogicalKey(entry), .field = "hermetic",
                                                .expected = "true", .actual = "false",
                                                .reason = "locked CI requires a hermetic PackageProvider result"});
            if (policy.requireIntegrity && entry.integrity.empty())
                result.invalidations.push_back({.package = LogicalKey(entry), .field = "integrity",
                                                .expected = "verified identity", .actual = "missing",
                                                .reason = "locked resolution requires provider integrity"});
        }
        result.reusable = result.invalidations.empty();
        return result;
    }

    auto VerifyCompositionFingerprint(const std::string_view expected, const ResolvedCompositionGraph &actual)
        -> FingerprintVerificationResult
    {
        const auto value = actual.CompositionIdentity();
        return FingerprintVerificationResult{.matches = expected == value,
                                             .expected = std::string(expected),
                                             .actual = value,
                                             .reason = expected == value ? "composition fingerprint matches"
                                                                         : "resolved semantic composition changed"};
    }
}
