#include "ActionModel.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace NGIN::CLI
{
    namespace
    {
        auto AddError(std::vector<ManifestDiagnostic> &diagnostics, std::string code, std::string message,
                      const ManifestSourceRange &source = {}, std::vector<ManifestSourceRange> related = {}) -> void
        {
            diagnostics.push_back(ManifestDiagnostic{.severity = ManifestDiagnosticSeverity::Error,
                                                       .code = std::move(code),
                                                       .message = std::move(message),
                                                       .source = source,
                                                       .relatedSources = std::move(related)});
        }

        [[nodiscard]] auto QualifiedParts(const std::string_view value)
            -> std::optional<std::pair<std::string, std::string>>
        {
            const auto separator = value.rfind("::");
            if (separator == std::string_view::npos || separator == 0 || separator + 2 == value.size())
                return std::nullopt;
            return std::pair{std::string(value.substr(0, separator)), std::string(value.substr(separator + 2))};
        }

        [[nodiscard]] auto RuleSpecificity(const ActionTrustRule &rule) -> std::size_t
        {
            return static_cast<std::size_t>(rule.package.has_value()) + static_cast<std::size_t>(rule.kind.has_value()) +
                   static_cast<std::size_t>(rule.providerKind.has_value()) +
                   static_cast<std::size_t>(rule.sourceBinding.has_value()) +
                   static_cast<std::size_t>(rule.trust.has_value()) +
                   static_cast<std::size_t>(rule.signature.has_value()) +
                   static_cast<std::size_t>(rule.executableOrigin.has_value());
        }

        [[nodiscard]] auto OriginMatches(const PortablePath &rule, const PortablePath &actual) -> bool
        {
            return actual.value == rule.value || actual.value.starts_with(rule.value + "/");
        }

        [[nodiscard]] auto RuleMatches(const ActionTrustRule &rule, const ResolvedAction &action,
                                       const ActionExecutionContext &context) -> bool
        {
            const auto &provider = action.hostInstance.providerResult;
            if (rule.package.has_value() && *rule.package != provider.coordinate.name) return false;
            if (rule.kind.has_value() && *rule.kind != action.kind) return false;
            if (rule.providerKind.has_value() && *rule.providerKind != provider.providerKind) return false;
            if (rule.sourceBinding.has_value() && provider.coordinate.sourceBinding != rule.sourceBinding) return false;
            if (rule.trust.has_value() && *rule.trust != provider.trust) return false;
            if (rule.signature.has_value() && *rule.signature != provider.signature) return false;
            if (rule.executableOrigin.has_value() &&
                (!context.executableOrigin.has_value() ||
                 !OriginMatches(*rule.executableOrigin, *context.executableOrigin)))
                return false;
            return true;
        }

        [[nodiscard]] auto IsWithinDirectory(const std::string_view path, const std::string_view directory) -> bool
        {
            return path.starts_with(std::string(directory) + "/");
        }

        [[nodiscard]] auto AttributeValue(const AuthoredElement &element, const std::string_view name,
                                          std::string fallback = {}) -> std::string
        {
            const auto *attribute = element.Attribute(name);
            return attribute == nullptr ? std::move(fallback) : attribute->value;
        }

        [[nodiscard]] auto Child(const AuthoredElement &element, const std::string_view specId)
            -> const AuthoredElement *
        {
            const auto found = std::ranges::find(element.children, specId, &AuthoredElement::specId);
            return found == element.children.end() ? nullptr : &*found;
        }

        [[nodiscard]] auto ParseActionKind(const std::string_view value) -> std::optional<ActionKind>
        {
            if (value.empty()) return std::nullopt;
            if (value == "Generate") return ActionKind::Generate;
            if (value == "Analyze") return ActionKind::Analyze;
            if (value == "Format") return ActionKind::Format;
            if (value == "Validate") return ActionKind::Validate;
            return ActionKind::Custom;
        }

        [[nodiscard]] auto ParseTrustDecision(const std::string_view value) -> ActionTrustDecision
        {
            if (value == "Allow") return ActionTrustDecision::Allow;
            if (value == "Confirm") return ActionTrustDecision::Confirm;
            return ActionTrustDecision::Deny;
        }
    }

    auto ActionKindName(const ActionKind kind) -> std::string_view
    {
        switch (kind)
        {
        case ActionKind::Generate: return "Generate";
        case ActionKind::Analyze: return "Analyze";
        case ActionKind::Format: return "Format";
        case ActionKind::Validate: return "Validate";
        case ActionKind::Custom: return "Custom";
        }
        return "Custom";
    }

    auto ActionTrustExplanation::Allowed() const -> bool
    {
        return decision == ActionTrustDecision::Allow && diagnostics.empty();
    }

    auto ResolvedActionResult::Succeeded() const -> bool { return value.has_value() && diagnostics.empty(); }

    auto ResolveActionSelection(const ProjectActionSelection &selection, const SemanticPackage &package,
                                const PackageProviderResult &provider,
                                const BinaryCompatibility &hostCompatibility,
                                const ResolvedPackageOptions &options) -> ResolvedActionResult
    {
        ResolvedActionResult result{};
        const auto qualified = QualifiedParts(selection.qualifiedAction);
        if (!qualified.has_value())
        {
            AddError(result.diagnostics, "NGIN5005", "invalid qualified Action '" + selection.qualifiedAction + "'",
                     selection.source);
            return result;
        }
        if (qualified->first != package.coordinate.name)
        {
            AddError(result.diagnostics, "NGIN5006",
                     "Action selection names package '" + qualified->first + "' but resolver supplied '" +
                         package.coordinate.name + "'",
                     selection.source);
            return result;
        }
        if (provider.coordinate.name != package.coordinate.name ||
            provider.coordinate.exactVersion != package.coordinate.exactVersion)
        {
            AddError(result.diagnostics, "NGIN5006", "Action PackageProviderResult does not match semantic package",
                     selection.source);
            return result;
        }
        const auto actionExport = package.exports.find(qualified->second);
        if (actionExport == package.exports.end() || actionExport->second.kind != ExportUseKind::Action ||
            !actionExport->second.action.has_value())
        {
            AddError(result.diagnostics, "NGIN5006", "unknown Action export '" + selection.qualifiedAction + "'",
                     selection.source);
            return result;
        }
        if (actionExport->second.action->kind != selection.kind)
        {
            AddError(result.diagnostics, "NGIN5007",
                     std::string(ActionKindName(selection.kind)) + " project verb cannot select " +
                         std::string(ActionKindName(actionExport->second.action->kind)) + " Action '" +
                         selection.qualifiedAction + "'",
                     selection.source, {actionExport->second.source});
            return result;
        }
        const auto toolExport = package.exports.find(actionExport->second.action->toolExport);
        if (toolExport == package.exports.end() || toolExport->second.kind != ExportUseKind::Tool)
        {
            AddError(result.diagnostics, "NGIN5004", "Action Tool export is missing", selection.source);
            return result;
        }
        if (!options.Succeeded())
        {
            result.diagnostics = options.diagnostics;
            return result;
        }
        auto hostProvider = provider;
        hostProvider.context = PackageInstanceContext::Host;
        auto hostInstance = ConstructPackageInstance(hostProvider, hostCompatibility, options.artifactValues);
        const SelectionFacts hostSelection{
            .configuration = Configuration{.name = hostCompatibility.configuration},
            .target = Target{.operatingSystem = hostCompatibility.operatingSystem,
                             .architecture = hostCompatibility.architecture},
            .toolchain = Toolchain{.compiler = hostCompatibility.compiler,
                                   .compilerVersion = hostCompatibility.compilerVersion,
                                   .runtimeLibrary = hostCompatibility.runtimeLibrary},
            .options = options.values,
        };
        const auto activation = ActivatePackageExports(
            package, hostInstance,
            PackageActivationRequest{
                .exports = {{.kind = ExportUseKind::Action, .name = actionExport->second.name,
                             .source = selection.source},
                            {.kind = ExportUseKind::Tool, .name = toolExport->second.name,
                             .source = actionExport->second.action->source}},
                .selection = hostSelection,
                .options = options,
            });
        if (!activation.Succeeded())
        {
            result.diagnostics = activation.diagnostics;
            return result;
        }
        ResolvedAction resolved{
            .qualifiedAction = selection.qualifiedAction,
            .kind = selection.kind,
            .hostInstance = std::move(hostInstance),
            .actionExport = actionExport->second.name,
            .toolExport = toolExport->second.name,
            .contract = *actionExport->second.action,
            .inputs = selection.inputs,
            .options = selection.options,
            .arguments = actionExport->second.action->arguments,
            .activatedExports = activation.exports,
            .requirements = activation.requirements,
            .contributions = activation.contributions,
            .capabilities = activation.capabilities,
            .source = selection.source,
        };
        resolved.arguments.insert(resolved.arguments.end(), selection.arguments.begin(), selection.arguments.end());
        for (const auto &output : resolved.contract.outputs)
        {
            if (output.kind != ActionOutputKind::Source && output.kind != ActionOutputKind::Header) continue;
            resolved.generatedItems.push_back(BuildItemDeclaration{
                .kind = output.kind == ActionOutputKind::Source ? BuildItemKind::Source : BuildItemKind::Header,
                .operation = BuildItemOperation::Include,
                .pattern = output.path.value,
                .generated = true,
                .source = output.source,
            });
        }
        result.value = std::move(resolved);
        return result;
    }

    auto EvaluateActionTrust(const ResolvedAction &action, const ActionTrustPolicy &policy,
                             const ActionExecutionContext &context) -> ActionTrustExplanation
    {
        const auto &provider = action.hostInstance.providerResult;
        ActionTrustExplanation result{
            .decision = policy.defaultDecision,
            .qualifiedAction = action.qualifiedAction,
            .toolExport = action.toolExport,
            .packageInstance = action.hostInstance.identity,
            .providerKind = provider.providerKind,
            .providerIdentity = provider.nativeIdentity,
            .executableOrigin = context.executableOrigin.has_value()
                                    ? std::optional<std::string>{context.executableOrigin->value}
                                    : std::nullopt,
            .reason = "workspace default Action trust decision",
        };
        std::vector<const ActionTrustRule *> best{};
        std::size_t bestSpecificity = 0;
        for (const auto &rule : policy.rules)
        {
            if (!RuleMatches(rule, action, context)) continue;
            const auto specificity = RuleSpecificity(rule);
            if (best.empty() || specificity > bestSpecificity)
            {
                best = {&rule};
                bestSpecificity = specificity;
            }
            else if (specificity == bestSpecificity)
                best.push_back(&rule);
        }
        if (!best.empty())
        {
            result.decision = best.front()->decision;
            result.reason = best.front()->reason.empty() ? "matched workspace Action trust rule" : best.front()->reason;
            result.matchedRule = best.front()->source;
            for (std::size_t index = 1; index < best.size(); ++index)
                if (best[index]->decision != best.front()->decision)
                {
                    AddError(result.diagnostics, "NGIN5008", "conflicting equally specific Action trust rules",
                             best[index]->source, {best.front()->source});
                    result.decision = ActionTrustDecision::Deny;
                }
        }
        if (policy.requireLocked && !context.locked)
        {
            AddError(result.diagnostics, "NGIN5009", "Action execution requires a locked PackageInstance",
                     action.source);
            result.decision = ActionTrustDecision::Deny;
        }
        if (policy.requireIntegrity && provider.integrity.empty())
        {
            AddError(result.diagnostics, "NGIN5009", "Action execution requires verified package integrity",
                     action.source);
            result.decision = ActionTrustDecision::Deny;
        }
        if (policy.requireSignature && provider.signature.empty())
        {
            AddError(result.diagnostics, "NGIN5009", "Action execution requires a signed package result",
                     action.source);
            result.decision = ActionTrustDecision::Deny;
        }
        if (context.nonInteractive && result.decision == ActionTrustDecision::Confirm)
        {
            AddError(result.diagnostics, "NGIN5009", "non-interactive Action execution cannot request confirmation",
                     action.source);
            result.decision = ActionTrustDecision::Deny;
        }
        return result;
    }

    auto ParseActionTrustPolicy(const AuthoredWorkspaceManifest &workspace,
                                std::vector<ManifestDiagnostic> &diagnostics) -> ActionTrustPolicy
    {
        ActionTrustPolicy result{};
        const auto *policies = Child(workspace.root, "workspace.policies");
        if (policies == nullptr) return result;
        const auto *actions = Child(*policies, "workspace.policies.actions");
        if (actions == nullptr) return result;
        result.defaultDecision = ParseTrustDecision(AttributeValue(*actions, "Default", "Deny"));
        result.requireLocked = AttributeValue(*actions, "RequireLocked") == "true";
        result.requireIntegrity = AttributeValue(*actions, "IntegrityRequired") == "true";
        result.requireSignature = AttributeValue(*actions, "SignatureRequired") == "true";
        for (const auto &node : actions->children)
        {
            ActionTrustRule rule{
                .kind = ParseActionKind(AttributeValue(node, "Kind")),
                .decision = node.name == "Allow"   ? ActionTrustDecision::Allow
                            : node.name == "Confirm" ? ActionTrustDecision::Confirm
                                                     : ActionTrustDecision::Deny,
                .reason = AttributeValue(node, "Reason"),
                .source = node.source,
            };
            const auto optional = [&](const std::string_view name) -> std::optional<std::string> {
                const auto value = AttributeValue(node, name);
                return value.empty() ? std::nullopt : std::optional<std::string>{value};
            };
            rule.package = optional("Package");
            rule.providerKind = optional("Provider");
            rule.sourceBinding = optional("Source");
            rule.trust = optional("Trust");
            rule.signature = optional("Signature");
            if (const auto origin = optional("ExecutableOrigin"); origin.has_value())
            {
                const auto parsed = NormalizePortablePath(*origin, PortablePathBase::Workspace, node.source);
                if (parsed.Succeeded()) rule.executableOrigin = *parsed.value;
                else diagnostics.insert(diagnostics.end(), parsed.diagnostics.begin(), parsed.diagnostics.end());
            }
            result.rules.push_back(std::move(rule));
        }
        return result;
    }

    auto ValidateActionOutputCollisions(const std::span<const ResolvedAction> actions)
        -> std::vector<ManifestDiagnostic>
    {
        std::vector<ManifestDiagnostic> diagnostics{};
        struct Owner
        {
            std::string action{};
            ActionOutputDeclaration output{};
        };
        std::vector<Owner> owners{};
        for (const auto &action : actions)
            for (const auto &output : action.contract.outputs)
            {
                for (const auto &existing : owners)
                {
                    if (existing.action == action.qualifiedAction) continue;
                    const auto collision = existing.output.path.value == output.path.value ||
                                           (existing.output.kind == ActionOutputKind::Directory &&
                                            IsWithinDirectory(output.path.value, existing.output.path.value)) ||
                                           (output.kind == ActionOutputKind::Directory &&
                                            IsWithinDirectory(existing.output.path.value, output.path.value));
                    if (collision)
                        AddError(diagnostics, "NGIN5010",
                                 "Action output collision at '" + output.path.value + "' between '" +
                                     existing.action + "' and '" + action.qualifiedAction + "'",
                                 output.source, {existing.output.source});
                }
                owners.push_back(Owner{.action = action.qualifiedAction, .output = output});
            }
        return diagnostics;
    }

    auto ValidateObservedActionOutputs(const ResolvedAction &action,
                                       const std::span<const PortablePath> observedOutputs)
        -> std::vector<ManifestDiagnostic>
    {
        std::vector<ManifestDiagnostic> diagnostics{};
        std::set<std::string, std::less<>> observed{};
        for (const auto &output : observedOutputs)
        {
            observed.insert(output.value);
            const auto declared = std::ranges::any_of(action.contract.outputs, [&](const ActionOutputDeclaration &item) {
                return item.path.value == output.value ||
                       (item.kind == ActionOutputKind::Directory && IsWithinDirectory(output.value, item.path.value));
            });
            if (!declared)
                AddError(diagnostics, "NGIN5011",
                         "Action '" + action.qualifiedAction + "' produced undeclared output '" + output.value + "'",
                         action.source);
        }
        for (const auto &declared : action.contract.outputs)
            if (declared.kind != ActionOutputKind::Directory && !observed.contains(declared.path.value))
                AddError(diagnostics, "NGIN5011",
                         "Action '" + action.qualifiedAction + "' did not produce declared output '" +
                             declared.path.value + "'",
                         declared.source);
        return diagnostics;
    }
}
