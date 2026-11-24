#include "ManifestCli.hpp"
#include "Canonical.hpp"
#include "WorkspaceModel.hpp"
#include "SemanticAuthoring.hpp"

#include <cctype>

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace NGIN::CLI
{
    namespace
    {
        using Object = CanonicalValue::Object;
        using Array = CanonicalValue::Array;
        using Symbols = std::map<std::string, ManifestSourceRange>;

        auto Location(const ManifestSourceRange &range) -> CanonicalValue
        {
            return Object{{"path", range.path.generic_string()},
                          {"start", static_cast<std::int64_t>(range.begin.offset)},
                          {"end", static_cast<std::int64_t>(range.end.offset)}};
        }

        auto ReadManifest(const fs::path &path) -> AuthoredManifest
        {
            auto result = ParseAuthoredManifest(path);
            if (!result.Succeeded())
                throw std::runtime_error(path.string() + ": " + result.diagnostics.front().message);
            return std::move(*result.value);
        }

        auto Fold(std::string value) -> std::string
        {
            std::ranges::transform(value, value.begin(), [](unsigned char c) { return std::tolower(c); });
            return value;
        }

        auto AddReference(Array &links, const Symbols &symbols, const std::string &key, const AuthoredAttribute *use)
            -> void
        {
            const auto definition = symbols.find(key);
            if (use && definition != symbols.end())
                links.emplace_back(
                    Object{{"source", Location(use->source)}, {"definition", Location(definition->second)}});
        }

        auto IsAvailable(const SemanticWorkspace &model, const AuthoredElement &node, const std::string &name) -> bool
        {
            const auto found = model.localPackages.find(name);
            if (found == model.localPackages.end())
                return false;
            const auto version = ParseSemanticVersion(found->second.coordinate.exactVersion);
            std::vector<ManifestDiagnostic> errors{};
            const auto constraint = ParseAuthoredVersionConstraint(node, name, errors);
            const auto central = model.centralVersions.find(name);
            if (!version || !errors.empty() || (constraint && !VersionConstraintContains(*constraint, *version)) ||
                (central != model.centralVersions.end() && !VersionConstraintContains(central->second, *version)))
                return false;
            const auto binding = model.packageBindings.find(name);
            if (binding == model.packageBindings.end())
                return true;
            const auto packageRoot = fs::weakly_canonical(model.root / found->second.root.value);
            for (const auto &[sourceName, source] : model.packageSources)
            {
                if (source.kind != "Directory" || !source.path)
                    continue;
                const auto relative =
                    packageRoot.lexically_relative(fs::weakly_canonical(model.root / source.path->value));
                if (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..")
                    return binding->second.sourceName == sourceName;
            }
            return binding->second.sourceName == "workspace";
        }

        auto References(const AuthoredElement &node, const SemanticWorkspace &model, const Symbols &symbols,
                        Array &links, const std::string &owner) -> void
        {
            const auto *name = node.Attribute("Name");
            if (node.specId == "project.uses.package" || node.specId == "package.requires.package" ||
                node.specId == "workspace.versions.package")
            {
                if (!name || !IsAvailable(model, node, name->value))
                    return;
                const auto key = "package:" + name->value;
                AddReference(links, symbols, key, name);
                for (const auto &child : node.children)
                    if (const auto *exportName = child.Attribute("Name"))
                        AddReference(links, symbols, key + ":" + child.name + ":" + exportName->value, exportName);
                return;
            }
            if (node.specId == "project.uses.project" || node.specId == "package.requires.project")
            {
                if (const auto *path = node.Attribute("Path"))
                    AddReference(links, symbols,
                                 fs::weakly_canonical(node.source.path.parent_path() / path->value).generic_string(),
                                 path);
            }
            if (const auto *use = node.Attribute("Using"))
            {
                const auto slash = use->value.rfind('/');
                const auto separator = slash == std::string::npos ? use->value.rfind("::") : slash;
                if (separator != std::string::npos)
                {
                    const auto package = use->value.substr(0, separator);
                    const auto label = use->value.substr(separator + (slash == std::string::npos ? 2 : 1));
                    if (IsAvailable(model, node, package))
                        AddReference(links, symbols, "export:" + package + ":" + label, use);
                }
            }
            if (!owner.empty())
            {
                if (const auto *target = node.Attribute("Export"); target && node.specId == "cmake.target")
                    AddReference(links, symbols, "export:" + owner + ":" + target->value, target);
                if (const auto *tool = node.Attribute("Tool"); tool && node.specId.starts_with("package.export."))
                    AddReference(links, symbols, "package:" + owner + ":Tool:" + tool->value, tool);
                if (name && node.specId.starts_with("package.requires."))
                    AddReference(links, symbols, "package:" + owner + ":" + node.name + ":" + name->value, name);
            }
            for (const auto &child : node.children)
                References(child, model, symbols, links, owner);
        }
    } // namespace

    auto WorkspaceCatalog(const fs::path &root, const CliArguments &args, bool navigation) -> int
    {
        const auto workspacePath = FindWorkspace(root, args);
        if (!workspacePath)
            throw std::runtime_error("select an authored workspace with --workspace FILE");
        const auto authored = ReadManifest(*workspacePath);
        const auto *workspace = std::get_if<AuthoredWorkspaceManifest>(&authored);
        if (!workspace)
            throw std::runtime_error("expected a workspace manifest");
        const auto parsed = ParseSemanticWorkspace(*workspace);
        if (!parsed.Succeeded())
            throw std::runtime_error(parsed.diagnostics.front().message);
        const auto &model = *parsed.value;
        const auto query = Fold(args.positional.empty() ? "" : args.positional.front());
        Array packages{}, links{};
        Symbols symbols{};
        std::vector<std::pair<std::string, AuthoredElement>> documents{};
        for (const auto &[name, local] : model.localPackages)
        {
            const auto path = fs::weakly_canonical(model.root / local.manifest.value);
            auto package = std::get<AuthoredPackageManifest>(ReadManifest(path));
            const auto semantic = ParseSemanticPackage(package);
            if (!semantic.Succeeded())
                throw std::runtime_error(path.string() + ": " + semantic.diagnostics.front().message);
            auto matches = Fold(name).contains(query);
            const auto key = "package:" + name;
            symbols.emplace(key, package.root.Attribute("Name")->source);
            Array exports{};
            std::string summary;
            for (const auto &child : package.root.children)
            {
                const auto *label = child.Attribute("Name");
                if (!label || !child.specId.starts_with("package.export."))
                    continue;
                const auto &item = semantic.value->exports.at(label->value);
                summary += "\n  " + child.name + ":" + item.name + (item.defaultExport ? " (default)" : "");
                Array capabilities{};
                for (const auto &provided : item.capabilities)
                {
                    matches |= Fold(provided.name).contains(query);
                    capabilities.emplace_back(provided.name);
                    summary += " provides " + provided.name;
                }
                matches |= Fold(item.name).contains(query);
                exports.emplace_back(Object{{"name", item.name},
                                            {"kind", child.name},
                                            {"default", item.defaultExport},
                                            {"capabilities", std::move(capabilities)}});
                symbols.emplace("export:" + name + ":" + item.name, label->source);
                symbols.emplace(key + ":" + child.name + ":" + item.name, label->source);
            }
            if (navigation)
                documents.emplace_back(name, std::move(package.root));
            else if (matches)
            {
                if (args.format != "json")
                    std::cout << name << " " << local.coordinate.exactVersion << summary << '\n';
                packages.emplace_back(Object{{"name", name},
                                             {"version", local.coordinate.exactVersion},
                                             {"manifest", path.generic_string()},
                                             {"exports", std::move(exports)}});
            }
        }
        if (navigation)
        {
            for (const auto &entry : model.projects)
            {
                if (!entry.project)
                    continue;
                auto product = std::get<AuthoredProjectManifest>(ReadManifest(entry.path));
                const auto &declaration = product.root.Attribute("Name")->source;
                symbols.emplace(fs::weakly_canonical(entry.path).generic_string(), declaration);
                documents.emplace_back("", std::move(product.root));
            }
            References(workspace->root, model, symbols, links, "");
            for (const auto &[owner, document] : documents)
                References(document, model, symbols, links, owner);
            std::cout << SerializeCanonical(Object{
                             {"kind", "NGIN.EditorSymbols"}, {"version", std::int64_t{1}}, {"links", std::move(links)}})
                      << '\n';
        }
        else if (args.format == "json")
            std::cout << SerializeCanonical(packages) << '\n';
        return 0;
    }
} // namespace NGIN::CLI
