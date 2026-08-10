#include "SelectionAuthoring.hpp"

#include <algorithm>
#include <map>

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
                                                       .code = "NGIN2011",
                                                       .message = std::move(message),
                                                       .source = source,
                                                       .relatedSources = std::move(related)});
        }

        template <typename TValue, typename TName>
        auto AddUnique(std::vector<TValue> &target, TValue value, TName name, const ManifestSourceRange &source,
                       std::map<std::string, ManifestSourceRange, std::less<>> &declared,
                       std::vector<ManifestDiagnostic> &diagnostics) -> void
        {
            const auto identity = name(value);
            if (const auto existing = declared.find(identity); existing != declared.end())
            {
                AddError(diagnostics, "duplicate selection declaration '" + identity + "'", source,
                         {existing->second});
                return;
            }
            declared.emplace(identity, source);
            target.push_back(std::move(value));
        }

        [[nodiscard]] auto ParseRequest(const AuthoredElement &owner, const std::string_view prefix)
            -> SelectionRequest
        {
            SelectionRequest request{};
            if (const auto *node = Child(owner, std::string(prefix) + ".configuration"))
                request.configuration = AttributeValue(*node, "Name");
            if (const auto *node = Child(owner, std::string(prefix) + ".target"))
                request.target = AttributeValue(*node, "Name");
            if (const auto *node = Child(owner, std::string(prefix) + ".toolchain"))
                request.toolchain = AttributeValue(*node, "Name");
            if (const auto *node = Child(owner, std::string(prefix) + ".launch"))
                request.launch = AttributeValue(*node, "Name");
            for (const auto *option : Children(owner, std::string(prefix) + ".option"))
                request.options.emplace(AttributeValue(*option, "Name"), AttributeValue(*option, "Value"));
            return request;
        }
    }

    auto WorkspaceSelectionResult::Succeeded() const -> bool
    {
        return value.has_value() && diagnostics.empty();
    }

    auto ParseWorkspaceSelection(const AuthoredWorkspaceManifest &workspace) -> WorkspaceSelectionResult
    {
        WorkspaceSelectionResult result{};
        WorkspaceSelectionModel model{};
        std::map<std::string, ManifestSourceRange, std::less<>> configurationNames{};
        std::map<std::string, ManifestSourceRange, std::less<>> targetNames{};
        std::map<std::string, ManifestSourceRange, std::less<>> toolchainNames{};
        std::map<std::string, ManifestSourceRange, std::less<>> presetNames{};

        if (const auto *configurations = Child(workspace.root, "workspace.configurations"))
        {
            for (const auto *node : Children(*configurations, "workspace.configuration"))
            {
                Configuration configuration{.name = AttributeValue(*node, "Name")};
                if (const auto *optimization = Child(*node, "workspace.configuration.optimization"))
                    configuration.optimization = AttributeValue(*optimization, "Mode");
                if (const auto *symbols = Child(*node, "workspace.configuration.debug-symbols"))
                    configuration.debugSymbols = AttributeValue(*symbols, "Enabled") == "true";
                if (const auto *lto = Child(*node, "workspace.configuration.lto"))
                    configuration.linkTimeOptimization = AttributeValue(*lto, "Enabled") == "true";
                AddUnique(model.configurations, std::move(configuration), [](const Configuration &item) { return item.name; },
                          node->source, configurationNames, result.diagnostics);
            }
        }

        if (const auto *targets = Child(workspace.root, "workspace.targets"))
        {
            std::map<std::string, ManifestSourceRange, std::less<>> allAliases{};
            for (const auto *node : Children(*targets, "workspace.target"))
            {
                Target target{.name = AttributeValue(*node, "Name"),
                              .operatingSystem = AttributeValue(*node, "OS"),
                              .architecture = AttributeValue(*node, "Architecture")};
                for (const auto *alias : Children(*node, "workspace.target.alias"))
                    target.aliases.insert(AttributeValue(*alias, "Name"));
                const auto primary = target.name;
                AddUnique(model.targets, std::move(target), [](const Target &item) { return item.name; }, node->source,
                          targetNames, result.diagnostics);
                if (const auto existing = allAliases.find(primary); existing != allAliases.end())
                    AddError(result.diagnostics, "Target name or alias '" + primary + "' is ambiguous", node->source,
                             {existing->second});
                else
                    allAliases.emplace(primary, node->source);
                if (!model.targets.empty() && model.targets.back().name == primary)
                {
                    for (const auto &alias : model.targets.back().aliases)
                    {
                        if (const auto existing = allAliases.find(alias); existing != allAliases.end())
                            AddError(result.diagnostics, "Target name or alias '" + alias + "' is ambiguous", node->source,
                                     {existing->second});
                        else
                            allAliases.emplace(alias, node->source);
                    }
                }
            }
        }

        if (const auto *toolchains = Child(workspace.root, "workspace.toolchains"))
        {
            for (const auto *node : Children(*toolchains, "workspace.toolchain"))
            {
                Toolchain toolchain{.name = AttributeValue(*node, "Name"),
                                    .compiler = AttributeValue(*node, "Compiler"),
                                    .compilerVersion = AttributeValue(*node, "CompilerVersion"),
                                    .runtimeLibrary = AttributeValue(*node, "RuntimeLibrary"),
                                    .linker = AttributeValue(*node, "Linker")};
                if (const auto path = AttributeValue(*node, "ToolchainFile"); !path.empty())
                {
                    const auto normalized = NormalizePortablePath(path, PortablePathBase::Workspace, node->source);
                    if (normalized.Succeeded()) toolchain.toolchainFile = *normalized.value;
                    else result.diagnostics.insert(result.diagnostics.end(), normalized.diagnostics.begin(),
                                                   normalized.diagnostics.end());
                }
                AddUnique(model.toolchains, std::move(toolchain), [](const Toolchain &item) { return item.name; },
                          node->source, toolchainNames, result.diagnostics);
            }
        }

        if (const auto *defaults = Child(workspace.root, "workspace.defaults"))
            model.defaults = ParseRequest(*defaults, "workspace.defaults");

        if (const auto *presets = Child(workspace.root, "workspace.presets"))
        {
            for (const auto *node : Children(*presets, "workspace.preset"))
            {
                Preset preset{.name = AttributeValue(*node, "Name"),
                              .command = AttributeValue(*node, "Command"),
                              .selection = ParseRequest(*node, "workspace.preset"),
                              .source = node->source};
                AddUnique(model.presets, std::move(preset), [](const Preset &item) { return item.name; }, node->source,
                          presetNames, result.diagnostics);
            }
        }

        const auto checkReference = [&](const std::optional<std::string> &name, const auto &declared,
                                        const std::string_view kind, const ManifestSourceRange &source) {
            if (name.has_value() && !declared.contains(*name) && !(*name == "host" && kind == "Target"))
                AddError(result.diagnostics, "unknown " + std::string(kind) + " '" + *name + "'", source);
        };
        const auto targetExists = [&](const std::string &name) {
            return name == "host" || std::ranges::any_of(model.targets, [&](const Target &candidate) {
                       return candidate.name == name || candidate.aliases.contains(name);
                   });
        };
        checkReference(model.defaults.configuration, configurationNames, "Configuration", workspace.root.source);
        if (model.defaults.target.has_value() && !targetExists(*model.defaults.target))
            AddError(result.diagnostics, "unknown Target '" + *model.defaults.target + "'", workspace.root.source);
        checkReference(model.defaults.toolchain, toolchainNames, "Toolchain", workspace.root.source);
        for (const auto &preset : model.presets)
        {
            checkReference(preset.selection.configuration, configurationNames, "Configuration", preset.source);
            if (preset.selection.target.has_value())
            {
                if (!targetExists(*preset.selection.target))
                    AddError(result.diagnostics, "unknown Target '" + *preset.selection.target + "'", preset.source);
            }
            checkReference(preset.selection.toolchain, toolchainNames, "Toolchain", preset.source);
        }
        if (result.diagnostics.empty()) result.value = std::move(model);
        return result;
    }
}
