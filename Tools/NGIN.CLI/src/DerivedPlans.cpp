#include "DerivedPlans.hpp"

#include "Canonical.hpp"
#include "CMakeIntegration.hpp"

namespace NGIN::CLI
{
    namespace
    {
        [[nodiscard]] auto ProvenanceValue(const GraphProvenance &value) -> CanonicalValue
        {
            return CanonicalValue::Object{{"column", static_cast<std::int64_t>(value.column)},
                                          {"document", value.document},
                                          {"kind", value.kind},
                                          {"line", static_cast<std::int64_t>(value.line)},
                                          {"owner", value.owner},
                                          {"reason", value.reason}};
        }

        [[nodiscard]] auto IdentityValue(const PlanIdentity &value) -> CanonicalValue
        {
            return CanonicalValue::Object{{"adapter", value.adapter},
                                          {"adapterVersion", value.adapterVersion},
                                          {"compositionIdentity", value.compositionIdentity},
                                          {"identity", value.identity},
                                          {"kind", value.kind}};
        }

        [[nodiscard]] auto CacheValue(const std::vector<CMakeCacheBinding> &values) -> CanonicalValue
        {
            CanonicalValue::Array result{};
            for (const auto &value : values)
                result.push_back(CanonicalValue::Object{{"artifact", value.artifact},
                                                        {"name", value.name},
                                                        {"type", value.type},
                                                        {"value", value.value}});
            return result;
        }
    }

    auto SerializeBuildPlan(const BuildPlan &plan) -> std::string
    {
        CanonicalValue::Array items{};
        for (const auto &item : plan.items)
            items.push_back(CanonicalValue::Object{{"generated", item.generated},
                                                   {"graphIdentity", item.graphIdentity},
                                                   {"identity", item.identity},
                                                   {"operation", item.operation},
                                                   {"provenance", ProvenanceValue(item.provenance)},
                                                   {"value", item.value},
                                                   {"visibility", item.visibility}});
        CanonicalValue::Array links{};
        for (const auto &link : plan.links)
            links.push_back(CanonicalValue::Object{{"graphIdentity", link.graphIdentity},
                                                   {"identity", link.identity},
                                                   {"provenance", ProvenanceValue(link.provenance)},
                                                   {"target", link.targetName},
                                                   {"visibility", link.visibility}});
        CanonicalValue::Array packages{};
        for (const auto &package : plan.packages)
        {
            CanonicalValue::Object value{{"binaryDirectory", package.binaryDirectory},
                                         {"cache", CacheValue(package.cache)},
                                         {"identity", package.identity},
                                         {"installedPrefix", package.installedPrefix},
                                         {"installBeforeUse", package.installBeforeUse},
                                         {"kind", std::string(CMakeIntegrationKindName(package.kind))},
                                         {"packageInstance", package.packageInstance},
                                         {"source", package.source}};
            if (package.findPackage.has_value())
                value.emplace("findPackage",
                              CanonicalValue::Object{{"config", package.findPackage->config},
                                                     {"name", package.findPackage->name},
                                                     {"required", package.findPackage->required},
                                                     {"version", package.findPackage->version.value_or("")}});
            packages.push_back(std::move(value));
        }
        CanonicalValue::Array actions{};
        for (const auto &action : plan.actionDependencies) actions.emplace_back(action);
        CanonicalValue::Object root{{"actionDependencies", actions},
                                    {"crossCompiling", plan.crossCompiling},
                                    {"generator", plan.generator},
                                                         {"items", items},
                                                         {"kind", "NGIN.BuildPlan"},
                                                         {"links", links},
                                    {"multiConfiguration", plan.multiConfiguration},
                                                         {"packages", packages},
                                                         {"plan", IdentityValue(plan.plan)},
                                                         {"product", plan.productGraphIdentity},
                                                         {"targetKind", plan.targetKind},
                                    {"targetName", plan.targetName}};
        if (plan.toolchainFile.has_value()) root.emplace("toolchainFile", *plan.toolchainFile);
        return SerializeCanonical(root);
    }

    auto SerializeActionPlan(const ActionPlan &plan) -> std::string
    {
        CanonicalValue::Array steps{};
        for (const auto &step : plan.steps)
        {
            CanonicalValue::Array outputs{};
            for (const auto &output : step.outputs) outputs.emplace_back(output);
            steps.push_back(CanonicalValue::Object{{"deterministic", step.deterministic},
                                                   {"graphIdentity", step.graphIdentity},
                                                   {"identity", step.identity},
                                                   {"kind", std::string(ActionKindName(step.kind))},
                                                   {"outputs", outputs},
                                                   {"provenance", ProvenanceValue(step.provenance)},
                                                   {"toolGraphIdentity", step.toolGraphIdentity},
                                                   {"toolTarget", step.toolTarget}});
        }
        return SerializeCanonical(CanonicalValue::Object{{"kind", "NGIN.ActionPlan"},
                                                         {"plan", IdentityValue(plan.plan)},
                                                         {"steps", steps}});
    }

    auto FingerprintBuildPlan(const BuildPlan &plan) -> std::string
    {
        auto canonical = plan;
        canonical.plan.identity.clear();
        return CanonicalFingerprint("BuildPlanFingerprint", {{"plan", SerializeBuildPlan(canonical)}});
    }

    auto FingerprintActionPlan(const ActionPlan &plan) -> std::string
    {
        auto canonical = plan;
        canonical.plan.identity.clear();
        return CanonicalFingerprint("ActionPlanFingerprint", {{"plan", SerializeActionPlan(canonical)}});
    }
}
