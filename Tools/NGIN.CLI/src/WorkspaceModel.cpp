#include "WorkspaceModel.hpp"

#include "Canonical.hpp"
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

        [[nodiscard]] auto HasMagic(const std::string_view value) -> bool
        {
            return value.find_first_of("*?[") != std::string_view::npos;
        }

        [[nodiscard]] auto InferredProjectSystem(const std::filesystem::path &path,
                                                 const std::string_view authored,
                                                 const ManifestSourceRange &source,
                                                 std::vector<ManifestDiagnostic> &diagnostics)
            -> std::optional<ProjectSystem>
        {
            if (authored == "Ngin") return ProjectSystem::Ngin;
            if (authored == "CMake") return ProjectSystem::CMake;
            std::error_code error{};
            if (std::filesystem::is_regular_file(path, error) && path.extension() == ".nginproj")
                return ProjectSystem::Ngin;
            if (std::filesystem::is_directory(path, error) &&
                (std::filesystem::is_regular_file(path / "CMakeLists.txt", error) ||
                 std::filesystem::is_regular_file(path / "CMakePresets.json", error) ||
                 std::filesystem::is_regular_file(path / "CMakeUserPresets.json", error)))
                return ProjectSystem::CMake;
            AddError(diagnostics, "cannot infer a Project System for '" + path.generic_string() + "'", source);
            return std::nullopt;
        }

        [[nodiscard]] auto ProjectCapabilities(const ProjectSystem system, const SemanticProject *project = nullptr)
            -> std::set<std::string, std::less<>>
        {
            if (system == ProjectSystem::CMake)
                return {"Inspect", "Configure", "Build", "BuildTarget", "Test", "SourceOwnership",
                        "OpenDeclaration", "Artifacts"};
            std::set<std::string, std::less<>> result{
                "Inspect", "Build", "SourceOwnership", "OpenDeclaration", "CompositionGraph", "AuthoringPlan"};
            if (project != nullptr)
            {
                if (!project->stage.empty()) result.insert("Stage");
                if (!project->runs.empty())
                {
                    result.insert("Run");
                    result.insert("Debug");
                }
                if (!project->tests.empty()) result.insert("Test");
                if (!project->benchmarks.empty()) result.insert("Benchmark");
            }
            return result;
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

        auto AddProject(const std::filesystem::path &path, const std::string_view authoredSystem,
                        const ManifestSourceRange &discoveredBy,
                        SemanticWorkspace &model, std::map<std::string, ManifestSourceRange, std::less<>> &discovered,
                        std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            std::error_code error{};
            const auto canonical = std::filesystem::weakly_canonical(path, error);
            const auto identity = (error ? path.lexically_normal() : canonical).generic_string();
            if (const auto existing = discovered.find(identity); existing != discovered.end())
            {
                diagnostics.push_back(ManifestDiagnostic{.severity = ManifestDiagnosticSeverity::Warning,
                                                         .code = "NGIN7001",
                                                         .message = "project discovery deduplicated '" +
                                                                    path.generic_string() + "'",
                                                         .source = discoveredBy,
                                                         .relatedSources = {existing->second}});
                return;
            }
            discovered.emplace(identity, discoveredBy);
            const auto system = InferredProjectSystem(canonical, authoredSystem, discoveredBy, diagnostics);
            if (!system.has_value()) return;
            if (*system == ProjectSystem::CMake)
            {
                if (!std::filesystem::is_directory(canonical, error))
                {
                    AddError(diagnostics, "CMake project must be a directory: '" + path.generic_string() + "'",
                             discoveredBy);
                    return;
                }
                if (!std::filesystem::is_regular_file(canonical / "CMakeLists.txt", error))
                {
                    AddError(diagnostics, "CMake project has no root CMakeLists.txt: '" + path.generic_string() + "'",
                             discoveredBy);
                    return;
                }
                const auto name = canonical.filename().string();
                model.projects.push_back(WorkspaceProject{
                    .id = Sha256Fingerprint(model.manifest.canonicalPath + "|CMake|" + identity),
                    .name = name,
                    .system = ProjectSystem::CMake,
                    .path = canonical,
                    .root = canonical,
                    .project = std::nullopt,
                    .capabilities = ProjectCapabilities(ProjectSystem::CMake),
                    .discoveredBy = discoveredBy});
                return;
            }
            if (!std::filesystem::is_regular_file(canonical, error) || canonical.extension() != ".nginproj")
            {
                AddError(diagnostics, "NGIN project must be a .nginproj file: '" + path.generic_string() + "'",
                         discoveredBy);
                return;
            }
            const auto authored = ParseAuthoredManifest(path);
            diagnostics.insert(diagnostics.end(), authored.diagnostics.begin(), authored.diagnostics.end());
            if (!authored.Succeeded() || !std::holds_alternative<AuthoredProjectManifest>(*authored.value)) return;
            auto semantic = ParseSemanticProject(std::get<AuthoredProjectManifest>(*authored.value));
            diagnostics.insert(diagnostics.end(), semantic.diagnostics.begin(), semantic.diagnostics.end());
            if (semantic.value.has_value())
            {
                const auto capabilities = ProjectCapabilities(ProjectSystem::Ngin, &*semantic.value);
                model.projects.push_back(WorkspaceProject{
                    .id = Sha256Fingerprint(model.manifest.canonicalPath + "|Ngin|" + identity),
                    .name = semantic.value->name,
                    .system = ProjectSystem::Ngin,
                    .path = canonical,
                    .root = canonical.parent_path(),
                    .project = std::move(*semantic.value),
                    .capabilities = capabilities,
                    .discoveredBy = discoveredBy});
            }
        }

        auto ParseProjects(const AuthoredWorkspaceManifest &workspace, SemanticWorkspace &model,
                           std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            const auto *discover = Child(workspace.root, "workspace.discover");
            if (discover == nullptr) return;
            std::map<std::string, ManifestSourceRange, std::less<>> discovered{};
            for (const auto *declaration : Children(*discover, "workspace.discover.projects"))
            {
                const auto include = AttributeValue(*declaration, "Include");
                const auto exclude = AttributeValue(*declaration, "Exclude");
                const auto system = AttributeValue(*declaration, "System");
                if (!HasMagic(include))
                {
                    if (const auto resolved = ResolveWorkspacePath(model.root, include, declaration->source, diagnostics))
                        AddProject(*resolved, system, declaration->source, model, discovered, diagnostics);
                    continue;
                }
                if (system == "CMake")
                {
                    AddError(diagnostics, "CMake project discovery requires an exact directory path in version 1",
                             declaration->source);
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
                    AddProject(model.root / std::filesystem::path(match.value), system, declaration->source, model,
                               discovered, diagnostics);
                }
            }
            std::ranges::sort(model.projects, {},
                              [](const WorkspaceProject &item) { return item.path.generic_string(); });
        }

        auto ParsePackages(const AuthoredWorkspaceManifest &workspace, SemanticWorkspace &model,
                           std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            const auto *discover = Child(workspace.root, "workspace.discover");
            if (discover != nullptr)
            {
                for (const auto *node : Children(*discover, "workspace.discover.packages"))
                {
                    const auto include = AttributeValue(*node, "Include");
                    const auto exclude = AttributeValue(*node, "Exclude");
                    GlobResult matches{};
                    if (HasMagic(include))
                        matches = ExpandPortableGlob(model.root, include, false, node->source,
                                                     model.pathPolicy.allowSymlinks, true);
                    else
                        matches.matches.push_back(PortablePath{.value = include,
                                                               .base = PortablePathBase::Workspace});
                    diagnostics.insert(diagnostics.end(), matches.diagnostics.begin(), matches.diagnostics.end());
                    for (const auto &match : matches.matches)
                    {
                        if (!match.value.ends_with(".nginpkg") ||
                            (!exclude.empty() && GlobMatchesPortable(exclude, match.value)))
                            continue;
                        const auto manifestPath = model.root / std::filesystem::path(match.value);
                        const auto parsed = ParseAuthoredManifest(manifestPath);
                        diagnostics.insert(diagnostics.end(), parsed.diagnostics.begin(), parsed.diagnostics.end());
                        if (!parsed.Succeeded()) continue;
                        const auto *authored = std::get_if<AuthoredPackageManifest>(&*parsed.value);
                        if (authored == nullptr) continue;
                        const auto relativeRoot = std::filesystem::path(match.value).parent_path().generic_string();
                        WorkspaceLocalPackage local{
                            .name = authored->name,
                            .manifest = match,
                            .root = PortablePath{.value = relativeRoot.empty() ? "." : relativeRoot,
                                                 .base = PortablePathBase::Workspace},
                            .coordinate = PackageCoordinate{.name = authored->name,
                                                            .exactVersion = authored->version},
                            .source = node->source};
                        if (const auto *development = Child(authored->root, "package.development"))
                        {
                            const auto projectPath = NormalizePortablePath(AttributeValue(*development, "Project"),
                                                                           PortablePathBase::Manifest,
                                                                           development->source);
                            diagnostics.insert(diagnostics.end(), projectPath.diagnostics.begin(),
                                               projectPath.diagnostics.end());
                            if (projectPath.Succeeded()) local.developmentProject = *projectPath.value;
                        }
                        if (const auto [existing, inserted] =
                                model.localPackages.emplace(authored->name, std::move(local));
                            !inserted)
                            AddError(diagnostics, "duplicate discovered Package '" + authored->name + "'",
                                     node->source, {existing->second.source});
                    }
                }
            }
            if (const auto *versions = Child(workspace.root, "workspace.versions"))
            for (const auto *node : Children(*versions, "workspace.versions.package"))
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
        }

        auto ParsePolicies(const AuthoredWorkspaceManifest &workspace, SemanticWorkspace &model,
                           std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            model.actionTrustPolicy = ParseActionTrustPolicy(workspace, diagnostics);
        }

        auto ParseCapabilityPreferences(const AuthoredWorkspaceManifest &workspace, SemanticWorkspace &model,
                                        std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            const auto *capabilities = Child(workspace.root, "workspace.capabilities");
            if (capabilities == nullptr) return;
            const auto append = [&](const AuthoredElement &node, const AuthoredElement *condition) {
                WorkspaceCapabilityPreference preference{
                    .name = AttributeValue(node, "Name"),
                    .provider = AttributeValue(node, "Provider"),
                    .source = node.source,
                };
                if (condition != nullptr)
                {
                    if (const auto value = AttributeValue(*condition, "OS"); !value.empty())
                        preference.operatingSystem = value;
                    if (const auto value = AttributeValue(*condition, "Architecture"); !value.empty())
                        preference.architecture = value;
                    if (const auto value = AttributeValue(*condition, "Configuration"); !value.empty())
                        preference.configuration = value;
                }
                model.capabilityPreferences.push_back(std::move(preference));
            };
            for (const auto &child : capabilities->children)
            {
                if (child.specId == "workspace.capabilities.prefer")
                    append(child, nullptr);
                else if (child.specId == "workspace.capabilities.when")
                    for (const auto &preference : child.children) append(preference, &child);
            }
            for (std::size_t left = 0; left < model.capabilityPreferences.size(); ++left)
                for (std::size_t right = left + 1; right < model.capabilityPreferences.size(); ++right)
                {
                    const auto &a = model.capabilityPreferences[left];
                    const auto &b = model.capabilityPreferences[right];
                    if (a.name == b.name && a.operatingSystem == b.operatingSystem &&
                        a.architecture == b.architecture && a.configuration == b.configuration &&
                        a.provider != b.provider)
                        AddError(diagnostics, "conflicting capability preferences for '" + a.name + "'", b.source,
                                 {a.source});
                }
        }

        auto ValidateWorkspace(const AuthoredWorkspaceManifest &workspace, SemanticWorkspace &model,
                               std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            std::set<std::string, std::less<>> dependencies{};
            for (const auto &project : model.projects)
            {
                if (!project.project.has_value()) continue;
                for (const auto &dependency : project.project->dependencies)
                    if (const auto *package = std::get_if<PackageDependencyRequest>(&dependency))
                        dependencies.insert(package->name);
                    else if (const auto *capability = std::get_if<ProjectCapabilityRequest>(&dependency))
                    {
                        if (capability->provider.has_value()) dependencies.insert(*capability->provider);
                        for (const auto &preference : model.capabilityPreferences)
                            if (preference.name == capability->name) dependencies.insert(preference.provider);
                    }
            }
            for (const auto &[name, constraint] : model.centralVersions)
                if (!dependencies.contains(name))
                    diagnostics.push_back(ManifestDiagnostic{
                        .severity = ManifestDiagnosticSeverity::Warning,
                        .code = "NGIN7001",
                        .message = "central Version '" + name + "' is unused by discovered projects",
                        .source = constraint.source});

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

    auto SemanticWorkspaceResult::Succeeded() const -> bool
    {
        return value.has_value() && std::ranges::none_of(diagnostics, [](const ManifestDiagnostic &diagnostic) {
                   return diagnostic.severity == ManifestDiagnosticSeverity::Error;
               });
    }

    auto ProjectSystemName(const ProjectSystem system) -> std::string_view
    {
        switch (system)
        {
        case ProjectSystem::Ngin: return "Ngin";
        case ProjectSystem::CMake: return "CMake";
        }
        return "Ngin";
    }

    auto ParseSemanticWorkspace(const AuthoredWorkspaceManifest &workspace) -> SemanticWorkspaceResult
    {
        SemanticWorkspaceResult result{};
        SemanticWorkspace model{
            .manifest = workspace.manifest, .name = workspace.name, .root = workspace.manifest.path.parent_path()};
        auto selection = ParseWorkspaceSelection(workspace);
        result.diagnostics.insert(result.diagnostics.end(), selection.diagnostics.begin(), selection.diagnostics.end());
        if (selection.value.has_value()) model.selection = std::move(*selection.value);
        model.outputRoot = PortablePath{.value = ".ngin/build", .base = PortablePathBase::Workspace};
        ParsePolicies(workspace, model, result.diagnostics);
        ParseCapabilityPreferences(workspace, model, result.diagnostics);
        ParseProjects(workspace, model, result.diagnostics);
        ParsePackages(workspace, model, result.diagnostics);
        ValidateWorkspace(workspace, model, result.diagnostics);
        if (std::ranges::none_of(result.diagnostics, [](const ManifestDiagnostic &diagnostic) {
                return diagnostic.severity == ManifestDiagnosticSeverity::Error;
            }))
            result.value = std::move(model);
        return result;
    }
} // namespace NGIN::CLI
