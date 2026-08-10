#include "SemanticResolver.hpp"

#include "ActionModel.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace NGIN::CLI
{
    namespace
    {
        struct PendingOrigin
        {
            std::string from{};
            std::string kind{};
            RequirementVisibility visibility{RequirementVisibility::Private};
            ManifestSourceRange source{};
            std::string reason{};
        };

        struct PendingPackage
        {
            std::string name{};
            PackageInstanceContext context{PackageInstanceContext::Target};
            std::optional<std::string> sourceBinding{};
            std::vector<SourcedVersionConstraint> constraints{};
            std::vector<ExportUse> exports{};
            std::map<std::string, std::vector<std::string>, std::less<>> optionValues{};
            bool activateDefaults{false};
            std::vector<PendingOrigin> origins{};
        };

        struct ResolvedPackageState
        {
            PendingPackage request{};
            PackageProviderResult provider{};
            AuthoredPackageManifest authored{};
            SemanticPackage package{};
            ResolvedPackageOptions options{};
            PackageInstance instance{};
            ActivePackageExports activation{};
        };

        struct ParsedProviderPackage
        {
            AuthoredPackageManifest authored{};
            SemanticPackage semantic{};
        };

        auto AddError(std::vector<ManifestDiagnostic> &diagnostics, std::string code, std::string message,
                      const ManifestSourceRange &source = {}, std::vector<ManifestSourceRange> related = {}) -> void
        {
            diagnostics.push_back(ManifestDiagnostic{.severity = ManifestDiagnosticSeverity::Error,
                                                       .code = std::move(code),
                                                       .message = std::move(message),
                                                       .source = source,
                                                       .relatedSources = std::move(related)});
        }

        [[nodiscard]] auto ContextName(const PackageInstanceContext context) -> std::string
        {
            return context == PackageInstanceContext::Host ? "Host" : "Target";
        }

        [[nodiscard]] auto DependencyContextName(const DependencyContext context) -> std::string
        {
            switch (context)
            {
            case DependencyContext::Target: return "Target";
            case DependencyContext::Test: return "Test";
            case DependencyContext::Benchmark: return "Benchmark";
            case DependencyContext::Publish: return "Publish";
            }
            return "Target";
        }

        [[nodiscard]] auto ExportKindName(const ExportUseKind kind) -> std::string
        {
            switch (kind)
            {
            case ExportUseKind::Library: return "Library";
            case ExportUseKind::Tool: return "Tool";
            case ExportUseKind::Plugin: return "Plugin";
            case ExportUseKind::Action: return "Action";
            case ExportUseKind::Asset: return "Asset";
            }
            return "Library";
        }

        [[nodiscard]] auto BuildKindName(const BuildItemKind kind) -> std::string
        {
            switch (kind)
            {
            case BuildItemKind::Source: return "Source";
            case BuildItemKind::Header: return "Header";
            case BuildItemKind::CxxModule: return "CxxModule";
            case BuildItemKind::Resource: return "Resource";
            case BuildItemKind::IncludeDirectory: return "IncludeDirectory";
            case BuildItemKind::Define: return "Define";
            case BuildItemKind::CompileOption: return "CompileOption";
            case BuildItemKind::LinkOption: return "LinkOption";
            case BuildItemKind::PrecompiledHeader: return "PrecompiledHeader";
            }
            return "Source";
        }

        [[nodiscard]] auto VisibilityName(const BuildVisibility visibility) -> std::string
        {
            if (visibility == BuildVisibility::Public) return "Public";
            if (visibility == BuildVisibility::Interface) return "Interface";
            return "Private";
        }

        [[nodiscard]] auto RequirementVisibilityName(const RequirementVisibility visibility) -> std::string
        {
            return visibility == RequirementVisibility::Public ? "Public" : "Private";
        }

        [[nodiscard]] auto ContributionKindName(const ContributionKind kind) -> std::string
        {
            switch (kind)
            {
            case ContributionKind::Notice: return "Notice";
            case ContributionKind::RuntimeFile: return "RuntimeFile";
            case ContributionKind::RuntimeDirectory: return "RuntimeDirectory";
            case ContributionKind::AssetFile: return "AssetFile";
            case ContributionKind::AssetDirectory: return "AssetDirectory";
            }
            return "RuntimeFile";
        }

        [[nodiscard]] auto VersionText(const SemanticVersion &version) -> std::string
        {
            std::ostringstream out;
            out << version.major << '.' << version.minor << '.' << version.patch;
            if (!version.prerelease.empty())
            {
                out << '-';
                for (std::size_t index = 0; index < version.prerelease.size(); ++index)
                {
                    if (index != 0) out << '.';
                    out << version.prerelease[index];
                }
            }
            return out.str();
        }

        [[nodiscard]] auto RequestKey(const std::string_view name, const PackageInstanceContext context) -> std::string
        {
            return std::string(name) + "\n" + ContextName(context);
        }

        [[nodiscard]] auto UseContext(const ExportUseKind kind, const PackageInstanceContext fallback)
            -> PackageInstanceContext
        {
            return kind == ExportUseKind::Tool || kind == ExportUseKind::Action ? PackageInstanceContext::Host
                                                                                : fallback;
        }

        [[nodiscard]] auto SameUse(const ExportUse &left, const ExportUse &right) -> bool
        {
            return left.kind == right.kind && left.name == right.name;
        }

        [[nodiscard]] auto SameConstraint(const SourcedVersionConstraint &left,
                                          const SourcedVersionConstraint &right) -> bool
        {
            const auto sameBoundary = [](const std::optional<VersionBoundary> &a,
                                         const std::optional<VersionBoundary> &b) {
                return (!a.has_value() && !b.has_value()) ||
                       (a.has_value() && b.has_value() && a->version == b->version && a->inclusive == b->inclusive);
            };
            return sameBoundary(left.lower, right.lower) && sameBoundary(left.upper, right.upper) &&
                   left.description == right.description;
        }

        auto AddPending(std::map<std::string, PendingPackage, std::less<>> &pending, const std::string &name,
                        const PackageInstanceContext context, const std::optional<SourcedVersionConstraint> &constraint,
                        const std::span<const ExportUse> exports, const bool activateDefaults,
                        const std::map<std::string, std::string, std::less<>> &options,
                        const std::optional<std::string> &sourceBinding, PendingOrigin origin,
                        std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            const auto key = RequestKey(name, context);
            auto &[_, value] = *pending.try_emplace(key, PendingPackage{.name = name, .context = context}).first;
            if (sourceBinding.has_value())
            {
                if (value.sourceBinding.has_value() && value.sourceBinding != sourceBinding)
                    AddError(diagnostics, "NGIN6001", "conflicting PackageProvider source bindings for '" + name + "'",
                             origin.source);
                else
                    value.sourceBinding = sourceBinding;
            }
            if (constraint.has_value() &&
                std::ranges::none_of(value.constraints, [&](const auto &existing) {
                    return SameConstraint(existing, *constraint);
                }))
                value.constraints.push_back(*constraint);
            for (const auto &use : exports)
                if (std::ranges::none_of(value.exports, [&](const auto &existing) { return SameUse(existing, use); }))
                    value.exports.push_back(use);
            for (const auto &[option, authored] : options)
                if (std::ranges::find(value.optionValues[option], authored) == value.optionValues[option].end())
                    value.optionValues[option].push_back(authored);
            value.activateDefaults = value.activateDefaults || activateDefaults;
            const auto originIdentity = origin.from + "\n" + origin.kind + "\n" + origin.reason + "\n" +
                                        origin.source.path.generic_string() + ":" +
                                        std::to_string(origin.source.begin.line);
            if (std::ranges::none_of(value.origins, [&](const PendingOrigin &existing) {
                    return existing.from + "\n" + existing.kind + "\n" + existing.reason + "\n" +
                               existing.source.path.generic_string() + ":" +
                               std::to_string(existing.source.begin.line) == originIdentity;
                }))
                value.origins.push_back(std::move(origin));
        }

        [[nodiscard]] auto ConstraintValue(const SourcedVersionConstraint &constraint) -> CanonicalValue
        {
            CanonicalValue::Object result{{"description", constraint.description}};
            if (constraint.lower.has_value())
                result["lower"] = CanonicalValue::Object{{"inclusive", constraint.lower->inclusive},
                                                         {"version", VersionText(constraint.lower->version)}};
            if (constraint.upper.has_value())
                result["upper"] = CanonicalValue::Object{{"inclusive", constraint.upper->inclusive},
                                                         {"version", VersionText(constraint.upper->version)}};
            return result;
        }

        [[nodiscard]] auto PendingSignature(const std::map<std::string, PendingPackage, std::less<>> &pending)
            -> std::string
        {
            CanonicalValue::Array values{};
            for (const auto &[key, request] : pending)
            {
                CanonicalValue::Array constraints{};
                for (const auto &constraint : request.constraints) constraints.push_back(ConstraintValue(constraint));
                std::ranges::sort(constraints, [](const auto &left, const auto &right) {
                    return SerializeCanonical(left) < SerializeCanonical(right);
                });
                CanonicalValue::Array exports{};
                for (const auto &use : request.exports)
                    exports.push_back(ExportKindName(use.kind) + ":" + use.name);
                std::ranges::sort(exports, [](const auto &left, const auto &right) {
                    return SerializeCanonical(left) < SerializeCanonical(right);
                });
                CanonicalValue::Object options{};
                for (const auto &[name, authored] : request.optionValues)
                {
                    auto sorted = authored;
                    std::ranges::sort(sorted);
                    CanonicalValue::Array optionValues{};
                    for (const auto &value : sorted) optionValues.emplace_back(value);
                    options.emplace(name, optionValues);
                }
                values.push_back(CanonicalValue::Object{{"constraints", constraints},
                                                       {"defaults", request.activateDefaults},
                                                       {"exports", exports},
                                                       {"key", key},
                                                       {"options", options},
                                                       {"source", request.sourceBinding.value_or("")}});
            }
            return SerializeCanonical(values);
        }

        [[nodiscard]] auto EffectiveContexts(const SemanticResolutionRequest &request) -> std::set<DependencyContext>
        {
            if (!request.dependencyContexts.empty()) return request.dependencyContexts;
            if (request.project.type == ProductType::Test) return {DependencyContext::Test};
            if (request.project.type == ProductType::Benchmark) return {DependencyContext::Benchmark};
            return {DependencyContext::Target};
        }

        [[nodiscard]] auto LogicalDocument(const fs::path &path, const fs::path &workspaceRoot) -> std::string
        {
            if (path.empty()) return {};
            if (!path.is_absolute()) return path.generic_string();
            const auto relative = path.lexically_relative(workspaceRoot);
            if (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..")
                return relative.generic_string();
            return path.filename().generic_string();
        }

        [[nodiscard]] auto Provenance(const ManifestSourceRange &source, const fs::path &workspaceRoot,
                                      std::string kind, std::string owner, std::string reason) -> GraphProvenance
        {
            return GraphProvenance{.kind = std::move(kind),
                                   .owner = std::move(owner),
                                   .document = LogicalDocument(source.path, workspaceRoot),
                                   .line = source.begin.line,
                                   .column = source.begin.column,
                                   .reason = std::move(reason)};
        }

        auto AddProjectRoots(const SemanticResolutionRequest &request,
                             std::map<std::string, PendingPackage, std::less<>> &pending,
                             std::vector<GraphEdge> &projectEdges,
                             std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            const auto contexts = EffectiveContexts(request);
            for (const auto &dependency : request.project.dependencies)
            {
                if (const auto *package = std::get_if<PackageDependencyRequest>(&dependency))
                {
                    if (!contexts.contains(package->context)) continue;
                    if (package->exports.empty())
                        AddPending(pending, package->name, PackageInstanceContext::Target, package->constraint, {}, true,
                                   package->optionAssignments, package->sourceBinding,
                                   PendingOrigin{.from = request.project.name,
                                                 .kind = "ProjectDependency",
                                                 .source = package->source,
                                                 .reason = DependencyContextName(package->context)},
                                   diagnostics);
                    else
                        for (const auto &use : package->exports)
                            AddPending(pending, package->name,
                                       UseContext(use.kind, PackageInstanceContext::Target), package->constraint,
                                       std::span<const ExportUse>{&use, 1}, false, package->optionAssignments,
                                       package->sourceBinding,
                                       PendingOrigin{.from = request.project.name,
                                                     .kind = "ProjectDependency",
                                                     .source = package->source,
                                                     .reason = DependencyContextName(package->context)},
                                       diagnostics);
                }
                else if (const auto *project = std::get_if<ProjectDependencyRequest>(&dependency))
                {
                    if (!contexts.contains(project->context)) continue;
                    projectEdges.push_back(GraphEdge{.identity = request.project.name + "->Project:" + project->name,
                                                     .from = request.project.name,
                                                     .to = "Project:" + project->name,
                                                     .kind = "ProjectDependency",
                                                     .context = DependencyContextName(project->context),
                                                     .provenance = Provenance(project->source, request.workspaceRoot,
                                                                              "ProjectDependency", project->name,
                                                                              DependencyContextName(project->context))});
                }
            }
            for (const auto &action : request.project.actions)
            {
                if (!request.actionKinds.contains(action.kind)) continue;
                const auto separator = action.qualifiedAction.rfind("::");
                if (separator == std::string::npos) continue;
                const auto package = action.qualifiedAction.substr(0, separator);
                const ExportUse use{.kind = ExportUseKind::Action,
                                    .name = action.qualifiedAction.substr(separator + 2),
                                    .source = action.source};
                AddPending(pending, package, PackageInstanceContext::Host, std::nullopt,
                           std::span<const ExportUse>{&use, 1}, false, {}, std::nullopt,
                           PendingOrigin{.from = request.project.name,
                                         .kind = "ActionSelection",
                                         .source = action.source,
                                         .reason = std::string(ActionKindName(action.kind))},
                           diagnostics);
            }
        }

        [[nodiscard]] auto ResolveProvider(const PendingPackage &pending,
                                           const std::optional<SourcedVersionConstraint> &constraint,
                                           const std::vector<const PackageProvider *> &providers,
                                           std::vector<ManifestDiagnostic> &diagnostics)
            -> std::optional<PackageProviderResult>
        {
            std::vector<PackageProviderResult> successes{};
            std::vector<ManifestDiagnostic> failures{};
            for (const auto *provider : providers)
            {
                const auto resolved = provider->Resolve(PackageProviderRequest{.name = pending.name,
                                                                                .constraint = constraint,
                                                                                .sourceBinding = pending.sourceBinding,
                                                                                .context = pending.context});
                if (resolved.Succeeded()) successes.push_back(*resolved.value);
                else failures.insert(failures.end(), resolved.diagnostics.begin(), resolved.diagnostics.end());
            }
            if (successes.empty())
            {
                diagnostics.insert(diagnostics.end(), failures.begin(), failures.end());
                if (providers.empty()) AddError(diagnostics, "NGIN6002", "no PackageProvider is configured");
                return std::nullopt;
            }
            if (successes.size() > 1)
            {
                AddError(diagnostics, "NGIN6002", "multiple PackageProviders can resolve '" + pending.name + "'",
                         pending.origins.empty() ? ManifestSourceRange{} : pending.origins.front().source);
                return std::nullopt;
            }
            const auto &selected = successes.front();
            if (selected.providerKind.empty() || selected.nativeIdentity.empty() || selected.nativeVersion.empty() ||
                selected.artifactIdentity.empty())
            {
                AddError(diagnostics, "NGIN6002",
                         "PackageProviderResult must include provider kind, native coordinate/version, and artifact identity",
                         pending.origins.empty() ? ManifestSourceRange{} : pending.origins.front().source);
                return std::nullopt;
            }
            if (selected.hermetic && selected.integrity.empty())
            {
                AddError(diagnostics, "NGIN6002", "hermetic PackageProviderResult must include integrity",
                         pending.origins.empty() ? ManifestSourceRange{} : pending.origins.front().source);
                return std::nullopt;
            }
            return successes.front();
        }

        [[nodiscard]] auto ParseProviderPackage(const PackageProviderResult &provider,
                                                std::vector<ManifestDiagnostic> &diagnostics)
            -> std::optional<ParsedProviderPackage>
        {
            const auto authored = ParseAuthoredManifest(provider.manifest);
            if (!authored.Succeeded())
            {
                diagnostics.insert(diagnostics.end(), authored.diagnostics.begin(), authored.diagnostics.end());
                return std::nullopt;
            }
            const auto *package = std::get_if<AuthoredPackageManifest>(&*authored.value);
            if (package == nullptr)
            {
                AddError(diagnostics, "NGIN6003", "PackageProvider manifest is not a Package document");
                return std::nullopt;
            }
            const auto semantic = ParseSemanticPackage(*package);
            if (!semantic.Succeeded())
            {
                diagnostics.insert(diagnostics.end(), semantic.diagnostics.begin(), semantic.diagnostics.end());
                return std::nullopt;
            }
            return ParsedProviderPackage{.authored = *package, .semantic = *semantic.value};
        }

        [[nodiscard]] auto ResolveState(const PendingPackage &pending, const SemanticResolutionRequest &request,
                                        std::vector<ManifestDiagnostic> &diagnostics)
            -> std::optional<ResolvedPackageState>
        {
            auto constraints = pending.constraints;
            if (const auto central = request.centralVersions.find(pending.name); central != request.centralVersions.end())
                constraints.insert(constraints.end(), central->second.begin(), central->second.end());
            std::optional<SourcedVersionConstraint> constraint{};
            if (!constraints.empty())
            {
                const auto intersection = IntersectVersionConstraints(pending.name, constraints);
                if (!intersection.Succeeded())
                {
                    diagnostics.insert(diagnostics.end(), intersection.diagnostics.begin(), intersection.diagnostics.end());
                    return std::nullopt;
                }
                constraint = intersection.value;
            }
            const auto provider = ResolveProvider(pending, constraint, request.packageProviders, diagnostics);
            if (!provider.has_value()) return std::nullopt;
            const auto package = ParseProviderPackage(*provider, diagnostics);
            if (!package.has_value()) return std::nullopt;
            if (package->semantic.coordinate.name != provider->coordinate.name ||
                package->semantic.coordinate.exactVersion != provider->coordinate.exactVersion)
            {
                AddError(diagnostics, "NGIN6003", "PackageProviderResult coordinate does not match its manifest");
                return std::nullopt;
            }
            std::vector<PackageOptionAssignment> assignments{};
            if (const auto configured = request.packageOptions.find(pending.name); configured != request.packageOptions.end())
                assignments.insert(assignments.end(), configured->second.begin(), configured->second.end());
            for (const auto &[name, values] : pending.optionValues)
                for (const auto &value : values)
                    assignments.push_back(PackageOptionAssignment{.name = name,
                                                                  .value = value,
                                                                  .authority = AssignmentAuthority::Project});
            auto options = ResolvePackageOptions(package->semantic, assignments);
            if (!options.Succeeded())
            {
                diagnostics.insert(diagnostics.end(), options.diagnostics.begin(), options.diagnostics.end());
                return std::nullopt;
            }
            auto selection = pending.context == PackageInstanceContext::Host ? request.hostSelection
                                                                             : request.targetSelection;
            selection.options = options.values;
            auto compatibility = DeriveBinaryCompatibility(selection, "Default", package->semantic.options);
            auto providerForContext = *provider;
            providerForContext.context = pending.context;
            auto instance = ConstructPackageInstance(providerForContext, compatibility, options.artifactValues);
            auto exports = pending.exports;
            if (pending.activateDefaults)
                for (const auto &[_, exportModel] : package->semantic.exports)
                    if (exportModel.defaultExport &&
                        std::ranges::none_of(exports, [&](const auto &existing) {
                            return existing.kind == exportModel.kind && existing.name == exportModel.name;
                        }))
                        exports.push_back(ExportUse{.kind = exportModel.kind,
                                                    .name = exportModel.name,
                                                    .source = exportModel.source});
            const auto activation = ActivatePackageExports(
                package->semantic, instance,
                PackageActivationRequest{.exports = exports, .selection = selection, .options = options});
            if (!activation.Succeeded())
            {
                diagnostics.insert(diagnostics.end(), activation.diagnostics.begin(), activation.diagnostics.end());
                return std::nullopt;
            }
            return ResolvedPackageState{.request = pending,
                                        .provider = providerForContext,
                                        .authored = package->authored,
                                        .package = package->semantic,
                                        .options = std::move(options),
                                        .instance = std::move(instance),
                                        .activation = activation};
        }

    }

    auto SemanticResolutionResult::Succeeded() const -> bool
    {
        return graph.has_value() && diagnostics.empty();
    }

    auto ResolveComposition(const SemanticResolutionRequest &request) -> SemanticResolutionResult
    {
        SemanticResolutionResult result{};
        std::map<std::string, PendingPackage, std::less<>> roots{};
        std::vector<GraphEdge> projectEdges{};
        AddProjectRoots(request, roots, projectEdges, result.diagnostics);
        if (!result.diagnostics.empty()) return result;

        auto pending = roots;
        std::map<std::string, ResolvedPackageState, std::less<>> states{};
        CapabilityResolution capabilityResolution{};
        std::vector<SemanticCapabilityRequirement> resolvedCapabilityRequirements{};
        std::vector<CapabilityImplementation> resolvedCapabilityImplementations{};
        bool converged = false;
        for (std::size_t iteration = 0; iteration < 64; ++iteration)
        {
            states.clear();
            std::vector<ManifestDiagnostic> iterationDiagnostics{};
            for (const auto &[key, packageRequest] : pending)
            {
                const auto state = ResolveState(packageRequest, request, iterationDiagnostics);
                if (state.has_value()) states.emplace(key, *state);
            }
            if (!iterationDiagnostics.empty())
            {
                result.diagnostics = std::move(iterationDiagnostics);
                return result;
            }

            auto next = roots;
            std::vector<SemanticCapabilityRequirement> capabilityRequirements{};
            std::vector<CapabilityImplementation> implementations{};
            std::map<std::string, std::pair<std::string, ExportUseKind>, std::less<>> implementationOwners{};
            for (const auto &[key, state] : states)
            {
                for (const auto &exportName : state.activation.exports)
                {
                    const auto &exportModel = state.package.exports.at(exportName);
                    if (exportModel.kind != ExportUseKind::Action || !exportModel.action.has_value()) continue;
                    const auto tool = state.package.exports.find(exportModel.action->toolExport);
                    if (tool == state.package.exports.end()) continue;
                    const ExportUse use{.kind = ExportUseKind::Tool,
                                        .name = tool->second.name,
                                        .source = exportModel.action->source};
                    AddPending(next, state.request.name, PackageInstanceContext::Host, std::nullopt,
                               std::span<const ExportUse>{&use, 1}, false, {}, state.request.sourceBinding,
                               PendingOrigin{.from = state.instance.identity + "::" + exportName,
                                             .kind = "ActionToolRequirement",
                                             .source = exportModel.action->source,
                                             .reason = tool->second.name},
                               result.diagnostics);
                }
                for (const auto &requirement : state.activation.requirements)
                {
                    if (const auto *package = std::get_if<SemanticPackageRequirement>(&requirement))
                    {
                        if (package->exports.empty())
                            AddPending(next, package->name, state.request.context, package->constraint, {}, true,
                                       package->optionAssignments, std::nullopt,
                                       PendingOrigin{.from = state.instance.identity,
                                                     .kind = "PackageRequirement",
                                                     .visibility = package->visibility,
                                                     .source = package->source,
                                                     .reason = state.package.coordinate.name},
                                       result.diagnostics);
                        else
                            for (const auto &use : package->exports)
                                AddPending(next, package->name, UseContext(use.kind, state.request.context),
                                           package->constraint, std::span<const ExportUse>{&use, 1}, false,
                                           package->optionAssignments, std::nullopt,
                                           PendingOrigin{.from = state.instance.identity,
                                                         .kind = "PackageRequirement",
                                                         .visibility = package->visibility,
                                                         .source = package->source,
                                                         .reason = state.package.coordinate.name},
                                           result.diagnostics);
                    }
                    else if (const auto *capability = std::get_if<SemanticCapabilityRequirement>(&requirement))
                    {
                        auto contextual = *capability;
                        contextual.context = state.request.context;
                        contextual.requester = state.instance.identity + "::" + capability->requester;
                        capabilityRequirements.push_back(std::move(contextual));
                    }
                }
                for (const auto &[exportName, exportModel] : state.package.exports)
                    for (auto implementation : exportModel.capabilities)
                    {
                        implementation.context = state.request.context;
                        implementation.packageInstance = state.instance.identity;
                        implementation.exportName = exportName;
                        implementationOwners[state.instance.identity + "::" + exportName] = {key, exportModel.kind};
                        implementations.push_back(std::move(implementation));
                    }
            }
            capabilityResolution = ResolveCapabilityBindings(capabilityRequirements, implementations);
            for (const auto &binding : capabilityResolution.bindings)
            {
                const auto owner = implementationOwners.find(binding.packageInstance + "::" + binding.exportName);
                if (owner == implementationOwners.end()) continue;
                const auto &state = states.at(owner->second.first);
                const ExportUse use{.kind = owner->second.second, .name = binding.exportName};
                AddPending(next, state.request.name, state.request.context, std::nullopt,
                           std::span<const ExportUse>{&use, 1}, false, {}, state.request.sourceBinding,
                           PendingOrigin{.from = binding.requirement,
                                         .kind = "CapabilityBinding",
                                         .source = state.package.exports.at(binding.exportName).source,
                                         .reason = binding.capability},
                           result.diagnostics);
            }
            if (!result.diagnostics.empty()) return result;
            if (PendingSignature(next) == PendingSignature(pending))
            {
                pending = std::move(next);
                converged = true;
                resolvedCapabilityRequirements = capabilityRequirements;
                resolvedCapabilityImplementations = implementations;
                if (!capabilityResolution.Succeeded())
                    result.diagnostics = capabilityResolution.diagnostics;
                break;
            }
            pending = std::move(next);
        }
        if (!converged)
        {
            AddError(result.diagnostics, "NGIN6004", "semantic package resolution did not converge");
            return result;
        }
        if (!result.diagnostics.empty()) return result;

        std::map<std::string, std::vector<PackageInstanceUse>, std::less<>> coexistenceUses{};
        std::map<std::string, PackageCoexistence, std::less<>> coexistencePolicies{};
        for (const auto &[key, state] : states)
        {
            std::vector<std::string> activationPath{};
            for (const auto &origin : state.request.origins) activationPath.push_back(origin.from);
            const auto source = state.request.origins.empty() ? ManifestSourceRange{} : state.request.origins.front().source;
            coexistenceUses[state.package.coordinate.name].push_back(PackageInstanceUse{
                .instance = state.instance,
                .linkageClosure = request.project.name,
                .activationPath = std::move(activationPath),
                .source = source});
            coexistencePolicies.emplace(state.package.coordinate.name, state.package.coexistence);
        }
        for (const auto &[name, uses] : coexistenceUses)
        {
            const auto diagnostics = ValidatePackageInstanceCoexistence(
                uses, coexistencePolicies.at(name), request.platformAllowsSideBySidePackages);
            result.diagnostics.insert(result.diagnostics.end(), diagnostics.begin(), diagnostics.end());
        }
        if (!result.diagnostics.empty()) return result;

        std::vector<CMakeIntegrationBindings> cmakeIntegrations{};
        for (const auto &[_, state] : states)
        {
            auto selection = state.request.context == PackageInstanceContext::Host ? request.hostSelection
                                                                                   : request.targetSelection;
            selection.options = state.options.values;
            const auto integration = ResolveCMakeIntegration(state.authored, state.package, state.provider,
                                                             state.instance, state.activation, selection, state.options);
            result.diagnostics.insert(result.diagnostics.end(), integration.diagnostics.begin(),
                                      integration.diagnostics.end());
            if (integration.value.has_value()) cmakeIntegrations.push_back(*integration.value);
        }
        if (!result.diagnostics.empty()) return result;
        result.cmakeIntegrations = ResolvedCMakeIntegrationBindings{std::move(cmakeIntegrations)};

        CompositionGraphData graph{};
        const auto projectSource = request.project.manifest.path.empty()
                                       ? ManifestSourceRange{}
                                       : ManifestSourceRange{.path = request.project.manifest.path};
        graph.product = GraphProduct{.identity = request.project.name,
                                     .name = request.project.name,
                                     .type = request.project.type,
                                     .linkage = request.project.linkage,
                                     .version = request.project.version.has_value()
                                                    ? std::optional<std::string>{VersionText(*request.project.version)}
                                                    : std::nullopt,
                                     .provenance = Provenance(projectSource, request.workspaceRoot, "Project",
                                                              request.project.name, "primary product")};
        graph.selection = GraphSelection{.configuration = request.targetSelection.configuration.name,
                                         .targetOperatingSystem = request.targetSelection.target.operatingSystem,
                                         .targetArchitecture = request.targetSelection.target.architecture,
                                         .compiler = request.targetSelection.toolchain.compiler,
                                         .compilerVersion = request.targetSelection.toolchain.compilerVersion,
                                         .runtimeLibrary = request.targetSelection.toolchain.runtimeLibrary,
                                         .provenance = Provenance(projectSource, request.workspaceRoot,
                                                                  "Selection", request.project.name,
                                                                  "resolved target selection")};
        for (const auto &[name, definition] : request.project.options)
        {
            const auto selected = request.targetSelection.options.find(name);
            const auto value = selected == request.targetSelection.options.end() ? definition.defaultValue
                                                                                 : selected->second;
            graph.options.push_back(GraphOption{.identity = request.project.name + ":Option:" + name,
                                                .owner = request.project.name,
                                                .name = name,
                                                .value = CanonicalOptionValue(value),
                                                .artifact = definition.artifact,
                                                .provenance = Provenance(definition.source, request.workspaceRoot,
                                                                         "ProjectOption", request.project.name,
                                                                         selected == request.targetSelection.options.end()
                                                                             ? "declared default"
                                                                             : "resolved selection")});
        }

        std::map<std::string, const ResolvedPackageState *, std::less<>> statesByInstance{};
        for (const auto &[key, state] : states)
        {
            statesByInstance.emplace(state.instance.identity, &state);
            const auto source = state.request.origins.empty() ? ManifestSourceRange{} : state.request.origins.front().source;
            graph.packages.push_back(GraphPackageInstance{
                .identity = state.instance.identity,
                .coordinate = state.provider.coordinate,
                .context = state.request.context,
                .providerKind = state.provider.providerKind,
                .providerIdentity = state.provider.nativeIdentity,
                .providerVersion = state.provider.nativeVersion,
                .revision = state.provider.revision,
                .integrity = state.provider.integrity,
                .artifactIdentity = state.provider.artifactIdentity,
                .hermetic = state.provider.hermetic,
                .compatibility = state.instance.compatibility,
                .artifactOptions = state.instance.artifactOptions,
                .provenance = Provenance(source, request.workspaceRoot, "PackageProviderResult",
                                         state.provider.coordinate.name, "resolved exact package instance")});
            for (const auto &[name, value] : state.options.values)
                graph.options.push_back(GraphOption{
                    .identity = state.instance.identity + ":Option:" + name,
                    .owner = state.instance.identity,
                    .name = name,
                    .value = CanonicalOptionValue(value),
                    .artifact = state.package.options.at(name).artifact,
                    .provenance = Provenance(state.package.options.at(name).source, request.workspaceRoot,
                                             "PackageOption", state.package.coordinate.name, "resolved package option")});
            for (const auto &name : state.activation.exports)
            {
                const auto &exportModel = state.package.exports.at(name);
                const auto identity = state.instance.identity + "::" + name;
                graph.exports.push_back(GraphExport{.identity = identity,
                                                    .packageInstance = state.instance.identity,
                                                    .name = name,
                                                    .kind = exportModel.kind,
                                                    .provenance = Provenance(exportModel.source, request.workspaceRoot,
                                                                             "ExportActivation",
                                                                             state.package.coordinate.name,
                                                                             "activation closure")});
                graph.edges.push_back(GraphEdge{.identity = state.instance.identity + "->" + identity,
                                                .from = state.instance.identity,
                                                .to = identity,
                                                .kind = "ActivatesExport",
                                                .context = ContextName(state.request.context),
                                                .provenance = Provenance(exportModel.source, request.workspaceRoot,
                                                                         "ExportActivation", name,
                                                                         "selected or required export")});
                if (exportModel.kind == ExportUseKind::Plugin)
                    graph.plugins.push_back(GraphPlugin{.identity = identity,
                                                        .packageInstance = state.instance.identity,
                                                        .exportName = name,
                                                        .provenance = Provenance(exportModel.source,
                                                                                 request.workspaceRoot, "Plugin", name,
                                                                                 "deployable plugin artifact")});
            }
            for (const auto &contribution : state.activation.contributions)
            {
                const auto identity = state.instance.identity + ":Contribution:" +
                                      ContributionKindName(contribution.kind) + ":" + contribution.destination.value +
                                      ":" + contribution.include.value;
                graph.contributions.push_back(GraphContribution{
                    .identity = identity,
                    .owner = state.instance.identity + "::" + contribution.owner,
                    .kind = ContributionKindName(contribution.kind),
                    .include = contribution.include.value,
                    .destination = contribution.destination.value,
                    .provenance = Provenance(contribution.source, request.workspaceRoot, "Contribution",
                                             contribution.owner, "active package/export contribution")});
            }
            for (const auto &origin : pending.at(key).origins)
                graph.edges.push_back(GraphEdge{
                    .identity = origin.from + "->" + state.instance.identity + ":" + origin.kind + ":" +
                                std::to_string(origin.source.begin.line),
                    .from = origin.from,
                    .to = state.instance.identity,
                    .kind = origin.kind,
                    .visibility = RequirementVisibilityName(origin.visibility),
                    .context = ContextName(state.request.context),
                    .provenance = Provenance(origin.source, request.workspaceRoot, origin.kind, state.request.name,
                                             origin.reason)});
        }
        graph.edges.insert(graph.edges.end(), projectEdges.begin(), projectEdges.end());

        for (const auto &binding : capabilityResolution.bindings)
        {
            const auto identity = binding.requirement + "->Capability:" + binding.capability + "->" +
                                  binding.packageInstance + "::" + binding.exportName;
            const auto requirement = std::ranges::find_if(
                resolvedCapabilityRequirements,
                [&](const SemanticCapabilityRequirement &candidate) { return candidate.requester == binding.requirement; });
            const auto implementation = std::ranges::find_if(
                resolvedCapabilityImplementations,
                [&](const CapabilityImplementation &candidate) {
                    return candidate.packageInstance == binding.packageInstance &&
                           candidate.exportName == binding.exportName && candidate.name == binding.capability;
                });
            const auto requirementSource = requirement == resolvedCapabilityRequirements.end()
                                               ? ManifestSourceRange{}
                                               : requirement->source;
            const auto implementationSource = implementation == resolvedCapabilityImplementations.end()
                                                  ? ManifestSourceRange{}
                                                  : implementation->source;
            graph.capabilities.push_back(GraphCapabilityBinding{
                .identity = identity,
                .binding = binding,
                .provenance = Provenance(requirementSource, request.workspaceRoot, "CapabilityRequirement",
                                         binding.requirement, "unique compatible implementation")});
            graph.edges.push_back(GraphEdge{.identity = identity,
                                            .from = binding.requirement,
                                            .to = binding.packageInstance + "::" + binding.exportName,
                                            .kind = "CapabilityBinding",
                                            .context = statesByInstance.at(binding.packageInstance)->request.context ==
                                                               PackageInstanceContext::Host
                                                           ? "Host"
                                                           : "Target",
                                            .provenance = Provenance(implementationSource, request.workspaceRoot,
                                                                     "CapabilityImplementation",
                                                                     binding.packageInstance + "::" +
                                                                         binding.exportName,
                                                                     binding.capability + "@" + binding.version)});
        }

        std::vector<ResolvedAction> resolvedActions{};
        for (const auto &selection : request.project.actions)
        {
            if (!request.actionKinds.contains(selection.kind)) continue;
            const auto separator = selection.qualifiedAction.rfind("::");
            if (separator == std::string::npos) continue;
            const auto packageName = selection.qualifiedAction.substr(0, separator);
            const auto state = states.find(RequestKey(packageName, PackageInstanceContext::Host));
            if (state == states.end())
            {
                AddError(result.diagnostics, "NGIN6005", "selected Action package did not resolve", selection.source);
                continue;
            }
            const auto resolved = ResolveActionSelection(selection, state->second.package, state->second.provider,
                                                         state->second.instance.compatibility, state->second.options);
            if (!resolved.Succeeded())
            {
                result.diagnostics.insert(result.diagnostics.end(), resolved.diagnostics.begin(),
                                          resolved.diagnostics.end());
                continue;
            }
            resolvedActions.push_back(*resolved.value);
            std::vector<std::string> outputs{};
            for (const auto &output : resolved.value->contract.outputs) outputs.push_back(output.path.value);
            graph.actions.push_back(GraphAction{
                .identity = selection.qualifiedAction,
                .kind = selection.kind,
                .packageInstance = resolved.value->hostInstance.identity,
                .actionExport = resolved.value->actionExport,
                .toolExport = resolved.value->toolExport,
                .deterministic = resolved.value->contract.deterministic,
                .outputs = std::move(outputs),
                .provenance = Provenance(selection.source, request.workspaceRoot, "ActionSelection",
                                         request.project.name, std::string(ActionKindName(selection.kind)))});
            graph.edges.push_back(GraphEdge{.identity = selection.qualifiedAction + "->Tool:" +
                                                        resolved.value->toolExport,
                                            .from = selection.qualifiedAction,
                                            .to = resolved.value->hostInstance.identity + "::" +
                                                  resolved.value->toolExport,
                                            .kind = "UsesHostTool",
                                            .context = "Host",
                                            .provenance = Provenance(selection.source, request.workspaceRoot,
                                                                     "ActionSelection", selection.qualifiedAction,
                                                                     "Action Tool export")});
        }
        const auto actionCollisions = ValidateActionOutputCollisions(resolvedActions);
        result.diagnostics.insert(result.diagnostics.end(), actionCollisions.begin(), actionCollisions.end());

        const auto build = ResolveProjectBuild(request.project, request.projectDirectory);
        result.diagnostics.insert(result.diagnostics.end(), build.diagnostics.begin(), build.diagnostics.end());
        for (const auto &item : build.items)
            graph.buildItems.push_back(GraphBuildItem{
                .identity = item.identity,
                .kind = BuildKindName(item.kind),
                .path = item.path.value,
                .visibility = VisibilityName(item.visibility),
                .generated = item.generated,
                .provenance = Provenance(item.source, request.workspaceRoot,
                                         item.origin == BuildItemOriginKind::Convention ? "Convention" : "BuildItem",
                                         request.project.name,
                                         item.origin == BuildItemOriginKind::Convention ? "project convention"
                                                                                       : "authored build item")});
        for (const auto &action : resolvedActions)
            for (const auto &item : action.generatedItems)
            {
                const auto kind = BuildKindName(item.kind);
                const auto existing = std::ranges::find_if(graph.buildItems, [&](const GraphBuildItem &candidate) {
                    return candidate.kind == kind && candidate.path == item.pattern;
                });
                if (existing != graph.buildItems.end())
                {
                    if (!existing->generated)
                        AddError(result.diagnostics, "NGIN6006",
                                 "Action output collides with non-generated build item '" + item.pattern + "'",
                                 item.source);
                    continue;
                }
                graph.buildItems.push_back(GraphBuildItem{
                    .identity = "ActionGenerated:" + kind + ":" + item.pattern,
                    .kind = kind,
                    .path = item.pattern,
                    .visibility = "Private",
                    .generated = true,
                    .provenance = Provenance(item.source, request.workspaceRoot, "ActionOutput",
                                             action.qualifiedAction, "declared generated build item")});
            }
        if (!result.diagnostics.empty()) return result;
        result.graph.emplace(std::move(graph));
        return result;
    }
}
