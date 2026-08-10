#include "WorkspaceModel.hpp"

#include "PackageModel.hpp"
#include "SemanticAuthoring.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace NGIN::CLI
{
    namespace
    {
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

        [[nodiscard]] auto Children(const AuthoredElement &element, const std::string_view specId)
            -> std::vector<const AuthoredElement *>
        {
            std::vector<const AuthoredElement *> result{};
            for (const auto &child : element.children)
                if (child.specId == specId) result.push_back(&child);
            return result;
        }

        auto AddError(std::vector<ManifestDiagnostic> &diagnostics, std::string message,
                      const ManifestSourceRange &source, std::vector<ManifestSourceRange> related = {}) -> void
        {
            diagnostics.push_back(ManifestDiagnostic{.severity = ManifestDiagnosticSeverity::Error,
                                                     .code = "NGIN7001",
                                                     .message = std::move(message),
                                                     .source = source,
                                                     .relatedSources = std::move(related)});
        }

        [[nodiscard]] auto IsContained(const std::filesystem::path &candidate, const std::filesystem::path &root)
            -> bool
        {
            std::error_code error{};
            const auto canonicalRoot = std::filesystem::weakly_canonical(root, error);
            if (error) return false;
            const auto canonicalCandidate = std::filesystem::weakly_canonical(candidate, error);
            if (error) return false;
            const auto relative = canonicalCandidate.lexically_relative(canonicalRoot);
            return !relative.empty() && relative != ".." && !relative.generic_string().starts_with("../");
        }

        [[nodiscard]] auto ResolveWorkspacePath(const std::filesystem::path &root, const std::string &authored,
                                                const ManifestSourceRange &source,
                                                std::vector<ManifestDiagnostic> &diagnostics)
            -> std::optional<std::filesystem::path>
        {
            const auto portable = NormalizePortablePath(authored, PortablePathBase::Workspace, source);
            if (!portable.Succeeded())
            {
                diagnostics.insert(diagnostics.end(), portable.diagnostics.begin(), portable.diagnostics.end());
                return std::nullopt;
            }
            const auto resolved = root / std::filesystem::path(portable.value->value);
            if (!IsContained(resolved, root) && portable.value->value != ".")
            {
                AddError(diagnostics, "workspace path escapes the workspace root: '" + authored + "'", source);
                return std::nullopt;
            }
            return resolved;
        }

        auto AddProject(const std::filesystem::path &path, const ManifestSourceRange &discoveredBy,
                        SemanticWorkspace &model, std::map<std::string, ManifestSourceRange, std::less<>> &discovered,
                        std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            std::error_code error{};
            const auto canonical = std::filesystem::weakly_canonical(path, error);
            const auto identity = (error ? path.lexically_normal() : canonical).generic_string();
            if (const auto existing = discovered.find(identity); existing != discovered.end())
            {
                AddError(diagnostics,
                         "project is discovered by more than one workspace declaration: '" + path.generic_string() +
                             "'",
                         discoveredBy, {existing->second});
                return;
            }
            discovered.emplace(identity, discoveredBy);
            if (!std::filesystem::is_regular_file(path, error))
            {
                AddError(diagnostics, "discovered project does not exist: '" + path.generic_string() + "'",
                         discoveredBy);
                return;
            }
            const auto authored = ParseAuthoredManifest(path);
            diagnostics.insert(diagnostics.end(), authored.diagnostics.begin(), authored.diagnostics.end());
            if (!authored.Succeeded() || !std::holds_alternative<AuthoredProjectManifest>(*authored.value)) return;
            auto semantic = ParseSemanticProject(std::get<AuthoredProjectManifest>(*authored.value));
            diagnostics.insert(diagnostics.end(), semantic.diagnostics.begin(), semantic.diagnostics.end());
            if (semantic.value.has_value())
                model.projects.push_back(WorkspaceProject{
                    .path = canonical, .project = std::move(*semantic.value), .discoveredBy = discoveredBy});
        }

        auto ParseProjects(const AuthoredWorkspaceManifest &workspace, SemanticWorkspace &model,
                           std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            const auto *projects = Child(workspace.root, "workspace.projects");
            if (projects == nullptr) return;
            std::map<std::string, ManifestSourceRange, std::less<>> discovered{};
            for (const auto *declaration : Children(*projects, "workspace.projects.project"))
            {
                const auto path = AttributeValue(*declaration, "Path");
                const auto include = AttributeValue(*declaration, "Include");
                const auto exclude = AttributeValue(*declaration, "Exclude");
                if ((path.empty() == include.empty()) || (!path.empty() && !exclude.empty()))
                {
                    AddError(diagnostics,
                             "Project discovery requires exactly one of Path or Include; "
                             "Exclude is valid only with Include",
                             declaration->source);
                    continue;
                }
                if (!path.empty())
                {
                    if (const auto resolved = ResolveWorkspacePath(model.root, path, declaration->source, diagnostics))
                        AddProject(*resolved, declaration->source, model, discovered, diagnostics);
                    continue;
                }
                const auto matches =
                    ExpandPortableGlob(model.root, include, false, declaration->source,
                                       model.pathPolicy.allowSymlinks, true);
                diagnostics.insert(diagnostics.end(), matches.diagnostics.begin(), matches.diagnostics.end());
                for (const auto &match : matches.matches)
                {
                    if (!match.value.ends_with(".nginproj")) continue;
                    if (!exclude.empty() && GlobMatchesPortable(exclude, match.value)) continue;
                    AddProject(model.root / std::filesystem::path(match.value), declaration->source, model, discovered,
                               diagnostics);
                }
            }
            std::ranges::sort(model.projects, {},
                              [](const WorkspaceProject &item) { return item.path.generic_string(); });
        }

        auto ParsePackages(const AuthoredWorkspaceManifest &workspace, SemanticWorkspace &model,
                           std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            const auto *packages = Child(workspace.root, "workspace.packages");
            if (packages == nullptr) return;
            for (const auto *node : Children(*packages, "workspace.packages.source"))
            {
                WorkspacePackageSource source{.name = AttributeValue(*node, "Name"),
                                              .kind = AttributeValue(*node, "Kind"),
                                              .source = node->source};
                if (const auto path = AttributeValue(*node, "Path"); !path.empty())
                {
                    const auto normalized = NormalizePortablePath(path, PortablePathBase::Workspace, node->source);
                    if (path == ".")
                        source.path = PortablePath{.value = ".", .base = PortablePathBase::Workspace};
                    else if (normalized.Succeeded())
                        source.path = *normalized.value;
                    else
                        diagnostics.insert(diagnostics.end(), normalized.diagnostics.begin(),
                                           normalized.diagnostics.end());
                }
                if (const auto url = AttributeValue(*node, "Url"); !url.empty()) source.url = url;
                if (source.path.has_value() == source.url.has_value())
                    AddError(diagnostics, "Package Source '" + source.name + "' requires exactly one of Path or Url",
                             node->source);
                if (const auto [existing, inserted] = model.packageSources.emplace(source.name, std::move(source));
                    !inserted)
                    AddError(diagnostics, "duplicate PackageProvider Source '" + existing->first + "'", node->source,
                             {existing->second.source});
            }
            for (const auto *node : Children(*packages, "workspace.packages.local-package"))
            {
                const auto manifestText = AttributeValue(*node, "Manifest");
                const auto rootText = AttributeValue(*node, "Root");
                const auto manifest = NormalizePortablePath(manifestText, PortablePathBase::Workspace, node->source);
                auto root = NormalizePortablePath(rootText, PortablePathBase::Workspace, node->source);
                if (rootText == ".")
                    root = PortablePathResult{.value = PortablePath{.value = ".", .base = PortablePathBase::Workspace}};
                if (!manifest.Succeeded() || !root.Succeeded())
                {
                    diagnostics.insert(diagnostics.end(), manifest.diagnostics.begin(), manifest.diagnostics.end());
                    diagnostics.insert(diagnostics.end(), root.diagnostics.begin(), root.diagnostics.end());
                    continue;
                }
                const auto manifestPath = model.root / std::filesystem::path(manifest.value->value);
                const auto package = ParseAuthoredManifest(manifestPath);
                diagnostics.insert(diagnostics.end(), package.diagnostics.begin(), package.diagnostics.end());
                if (!package.Succeeded() || !std::holds_alternative<AuthoredPackageManifest>(*package.value)) continue;
                const auto &authoredPackage = std::get<AuthoredPackageManifest>(*package.value);
                const auto name = AttributeValue(*node, "Name");
                if (authoredPackage.name != name)
                    AddError(diagnostics,
                             "LocalPackage name '" + name + "' does not match manifest package '" +
                                 authoredPackage.name + "'",
                             node->source, {authoredPackage.root.source});
                if (!IsContained(model.root / std::filesystem::path(root.value->value), model.root))
                    AddError(diagnostics, "LocalPackage root escapes the workspace", node->source);
                const auto version = ParseSemanticVersion(authoredPackage.version);
                if (!version.has_value()) continue;
                WorkspaceLocalPackage local{
                    .name = name,
                    .manifest = *manifest.value,
                    .root = *root.value,
                    .coordinate = PackageCoordinate{.name = name, .exactVersion = authoredPackage.version},
                    .source = node->source};
                if (const auto [existing, inserted] = model.localPackages.emplace(name, std::move(local)); !inserted)
                    AddError(diagnostics, "duplicate LocalPackage '" + name + "'", node->source,
                             {existing->second.source});
            }
            for (const auto *node : Children(*packages, "workspace.packages.version"))
            {
                const auto name = AttributeValue(*node, "Name");
                const auto constraint = ParseAuthoredVersionConstraint(*node, name, diagnostics);
                if (!constraint.has_value())
                {
                    AddError(diagnostics, "central Version '" + name + "' requires one constraint", node->source);
                    continue;
                }
                if (const auto [existing, inserted] = model.centralVersions.emplace(name, *constraint); !inserted)
                    AddError(diagnostics, "duplicate central Version '" + name + "'", node->source,
                             {existing->second.source});
            }
            for (const auto *node : Children(*packages, "workspace.packages.binding"))
            {
                WorkspacePackageBinding binding{.package = AttributeValue(*node, "Package"),
                                                .sourceName = AttributeValue(*node, "Source"),
                                                .coordinate = AttributeValue(*node, "Coordinate"),
                                                .source = node->source};
                if (!model.packageSources.contains(binding.sourceName))
                    AddError(diagnostics,
                             "Binding for '" + binding.package + "' references unknown Source '" + binding.sourceName +
                                 "'",
                             node->source);
                if (const auto [existing, inserted] = model.packageBindings.emplace(binding.package, binding);
                    !inserted)
                    AddError(diagnostics, "duplicate package Binding for '" + binding.package + "'", node->source,
                             {existing->second.source});
            }
        }

        auto ParsePolicies(const AuthoredWorkspaceManifest &workspace, SemanticWorkspace &model,
                           std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            const auto *policies = Child(workspace.root, "workspace.policies");
            if (policies == nullptr) return;
            if (const auto *providers = Child(*policies, "workspace.policies.providers"))
            {
                model.providerPolicy.integrityRequired = AttributeValue(*providers, "IntegrityRequired") == "true";
                model.providerPolicy.locked = AttributeValue(*providers, "Locked") == "true";
                model.providerPolicy.allowNonHermetic = AttributeValue(*providers, "AllowNonHermetic") == "true";
                for (const auto *allow : Children(*providers, "workspace.policies.providers.allow"))
                    model.providerPolicy.allowedKinds.insert(AttributeValue(*allow, "Kind"));
                if (model.providerPolicy.locked && model.providerPolicy.allowNonHermetic)
                    AddError(diagnostics, "Locked PackageProvider policy cannot allow non-hermetic results",
                             providers->source);
                if (model.providerPolicy.locked && !model.providerPolicy.integrityRequired)
                    AddError(diagnostics, "Locked PackageProvider policy requires IntegrityRequired=\"true\"",
                             providers->source);
            }
            if (const auto *paths = Child(*policies, "workspace.policies.paths"))
            {
                model.pathPolicy.allowSymlinks = AttributeValue(*paths, "AllowSymlinks") == "true";
                model.pathPolicy.requireContained = AttributeValue(*paths, "RequireContained", "true") == "true";
            }
            if (const auto *stage = Child(*policies, "workspace.policies.stage"))
                model.stageCollision = AttributeValue(*stage, "Collision", "Error") == "IdenticalBytes"
                                           ? WorkspaceStageCollisionPolicy::IdenticalBytes
                                           : WorkspaceStageCollisionPolicy::Error;
            if (const auto *compatibility = Child(*policies, "workspace.policies.compatibility"))
            {
                for (const auto *target : Children(*compatibility, "workspace.policies.compatibility.target"))
                    model.compatibilityPolicy.targets.insert(AttributeValue(*target, "Name"));
                for (const auto *toolchain : Children(*compatibility, "workspace.policies.compatibility.toolchain"))
                    model.compatibilityPolicy.toolchains.insert(AttributeValue(*toolchain, "Name"));
            }
            model.actionTrustPolicy = ParseActionTrustPolicy(workspace, diagnostics);
        }

        auto ValidateWorkspace(const AuthoredWorkspaceManifest &workspace, SemanticWorkspace &model,
                               std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            std::set<std::string, std::less<>> dependencies{};
            for (const auto &project : model.projects)
                for (const auto &dependency : project.project.dependencies)
                    if (const auto *package = std::get_if<PackageDependencyRequest>(&dependency))
                        dependencies.insert(package->name);
            for (const auto &[name, constraint] : model.centralVersions)
                if (!dependencies.contains(name))
                    AddError(diagnostics, "central Version '" + name + "' is unused by discovered projects",
                             constraint.source);

            for (const auto &[_, source] : model.packageSources)
                if (!model.providerPolicy.allowedKinds.empty() &&
                    !model.providerPolicy.allowedKinds.contains(source.kind))
                    AddError(diagnostics, "PackageProvider kind '" + source.kind + "' is not allowed by policy",
                             source.source);

            const auto targetExists = [&](const std::string &name) {
                return name == "host" || std::ranges::any_of(model.selection.targets, [&](const Target &target) {
                           return target.name == name || target.aliases.contains(name);
                       });
            };
            for (const auto &name : model.compatibilityPolicy.targets)
                if (!targetExists(name))
                    AddError(diagnostics, "Compatibility policy references unknown Target '" + name + "'",
                             workspace.root.source);
            for (const auto &name : model.compatibilityPolicy.toolchains)
                if (!std::ranges::any_of(model.selection.toolchains,
                                         [&](const Toolchain &toolchain) { return toolchain.name == name; }))
                    AddError(diagnostics, "Compatibility policy references unknown Toolchain '" + name + "'",
                             workspace.root.source);
        }
    } // namespace

    auto SemanticWorkspaceResult::Succeeded() const -> bool { return value.has_value() && diagnostics.empty(); }

    auto ParseSemanticWorkspace(const AuthoredWorkspaceManifest &workspace) -> SemanticWorkspaceResult
    {
        SemanticWorkspaceResult result{};
        SemanticWorkspace model{
            .manifest = workspace.manifest, .name = workspace.name, .root = workspace.manifest.path.parent_path()};
        auto selection = ParseWorkspaceSelection(workspace);
        result.diagnostics.insert(result.diagnostics.end(), selection.diagnostics.begin(), selection.diagnostics.end());
        if (selection.value.has_value()) model.selection = std::move(*selection.value);
        if (const auto *defaults = Child(workspace.root, "workspace.defaults"))
            if (const auto *output = Child(*defaults, "workspace.defaults.output-root"))
            {
                const auto normalized =
                    NormalizePortablePath(AttributeValue(*output, "Path"), PortablePathBase::Workspace, output->source);
                if (normalized.Succeeded())
                    model.outputRoot = *normalized.value;
                else
                    result.diagnostics.insert(result.diagnostics.end(), normalized.diagnostics.begin(),
                                              normalized.diagnostics.end());
            }
        ParsePolicies(workspace, model, result.diagnostics);
        ParseProjects(workspace, model, result.diagnostics);
        ParsePackages(workspace, model, result.diagnostics);
        ValidateWorkspace(workspace, model, result.diagnostics);
        if (result.diagnostics.empty()) result.value = std::move(model);
        return result;
    }
} // namespace NGIN::CLI
