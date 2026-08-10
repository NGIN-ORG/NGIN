#pragma once

#include "CompositionGraph.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace NGIN::CLI
{
    struct DependencyLockEntry
    {
        std::string packageInstance{};
        PackageCoordinate coordinate{};
        PackageInstanceContext context{PackageInstanceContext::Target};
        std::string providerKind{};
        std::string providerCoordinate{};
        std::string providerVersion{};
        std::string revision{};
        std::string integrity{};
        std::string trust{};
        std::string signature{};
        std::string artifactIdentity{};
        bool hermetic{false};
        BinaryCompatibility compatibility{};
        std::map<std::string, std::string, std::less<>> artifactOptions{};

        [[nodiscard]] friend auto operator==(const DependencyLockEntry &, const DependencyLockEntry &) -> bool = default;
    };

    struct DependencyLockData
    {
        std::vector<DependencyLockEntry> packages{};
    };

    class ResolvedDependencyLock
    {
    public:
        explicit ResolvedDependencyLock(DependencyLockData data);

        [[nodiscard]] auto Data() const -> const DependencyLockData &;
        [[nodiscard]] auto CanonicalSerialization() const -> std::string;
        [[nodiscard]] auto Fingerprint() const -> std::string;

    private:
        std::shared_ptr<const DependencyLockData> data_{};
        std::string canonical_{};
        std::string fingerprint_{};
    };

    struct DependencyLockParseResult
    {
        std::optional<ResolvedDependencyLock> lock{};
        std::vector<ManifestDiagnostic> diagnostics{};

        [[nodiscard]] auto Succeeded() const -> bool;
    };

    struct LockInvalidation
    {
        std::string package{};
        std::string field{};
        std::string expected{};
        std::string actual{};
        std::string reason{};
    };

    struct LockVerificationPolicy
    {
        bool requireHermetic{false};
        bool requireIntegrity{true};
    };

    struct LockVerificationResult
    {
        bool reusable{false};
        std::vector<LockInvalidation> invalidations{};
    };

    struct FingerprintVerificationResult
    {
        bool matches{false};
        std::string expected{};
        std::string actual{};
        std::string reason{};
    };

    [[nodiscard]] auto CreateDependencyLock(const ResolvedCompositionGraph &graph) -> ResolvedDependencyLock;
    [[nodiscard]] auto SerializeDependencyLock(const DependencyLockData &lock) -> std::string;
    [[nodiscard]] auto ParseDependencyLock(std::string_view text) -> DependencyLockParseResult;
    [[nodiscard]] auto VerifyDependencyLock(const ResolvedDependencyLock &expected,
                                            const ResolvedDependencyLock &actual,
                                            const LockVerificationPolicy &policy = {})
        -> LockVerificationResult;
    [[nodiscard]] auto VerifyCompositionFingerprint(std::string_view expected,
                                                    const ResolvedCompositionGraph &actual)
        -> FingerprintVerificationResult;
}
