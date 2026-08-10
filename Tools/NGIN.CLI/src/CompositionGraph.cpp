#include "CompositionGraph.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace NGIN::CLI
{
    namespace
    {
        [[nodiscard]] auto ContextName(const PackageInstanceContext context) -> std::string
        {
            return context == PackageInstanceContext::Host ? "Host" : "Target";
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

        [[nodiscard]] auto LinkageName(const LibraryLinkage linkage) -> std::string
        {
            switch (linkage)
            {
            case LibraryLinkage::None: return "None";
            case LibraryLinkage::Static: return "Static";
            case LibraryLinkage::Shared: return "Shared";
            case LibraryLinkage::Interface: return "Interface";
            }
            return "None";
        }

        [[nodiscard]] auto ProvenanceValue(const GraphProvenance &provenance) -> CanonicalValue
        {
            return CanonicalValue::Object{{"column", static_cast<std::int64_t>(provenance.column)},
                                          {"document", provenance.document},
                                          {"kind", provenance.kind},
                                          {"line", static_cast<std::int64_t>(provenance.line)},
                                          {"owner", provenance.owner},
                                          {"reason", provenance.reason}};
        }

        template <typename T, typename F>
        [[nodiscard]] auto Array(const std::vector<T> &values, F convert) -> CanonicalValue::Array
        {
            CanonicalValue::Array result{};
            result.reserve(values.size());
            for (const auto &value : values) result.push_back(convert(value));
            return result;
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

        [[nodiscard]] auto GraphValue(const CompositionGraphData &graph) -> CanonicalValue
        {
            CanonicalValue::Object product{{"identity", graph.product.identity},
                                           {"linkage", LinkageName(graph.product.linkage)},
                                           {"name", graph.product.name},
                                           {"provenance", ProvenanceValue(graph.product.provenance)},
                                           {"type", std::string(ProductTypeName(graph.product.type))}};
            if (graph.product.version.has_value()) product.emplace("version", *graph.product.version);
            const CanonicalValue::Object selection{{"compiler", graph.selection.compiler},
                                                   {"compilerVersion", graph.selection.compilerVersion},
                                                   {"configuration", graph.selection.configuration},
                                                   {"identity", graph.selection.identity},
                                                   {"provenance", ProvenanceValue(graph.selection.provenance)},
                                                   {"runtimeLibrary", graph.selection.runtimeLibrary},
                                                   {"targetArchitecture", graph.selection.targetArchitecture},
                                                   {"targetOperatingSystem", graph.selection.targetOperatingSystem}};
            return CanonicalValue::Object{
                {"actions", Array(graph.actions, [](const GraphAction &value) {
                     CanonicalValue::Array outputs{};
                     for (const auto &output : value.outputs) outputs.emplace_back(output);
                     return CanonicalValue::Object{{"actionExport", value.actionExport},
                                                   {"deterministic", value.deterministic},
                                                   {"identity", value.identity},
                                                   {"kind", std::string(ActionKindName(value.kind))},
                                                   {"outputs", outputs},
                                                   {"packageInstance", value.packageInstance},
                                                   {"provenance", ProvenanceValue(value.provenance)},
                                                   {"toolExport", value.toolExport}};
                 })},
                {"buildItems", Array(graph.buildItems, [](const GraphBuildItem &value) {
                     return CanonicalValue::Object{{"generated", value.generated},
                                                   {"identity", value.identity},
                                                   {"kind", value.kind},
                                                   {"path", value.path},
                                                   {"provenance", ProvenanceValue(value.provenance)},
                                                   {"visibility", value.visibility}};
                 })},
                {"capabilityBindings", Array(graph.capabilities, [](const GraphCapabilityBinding &value) {
                     return CanonicalValue::Object{{"capability", value.binding.capability},
                                                   {"domain", value.binding.domain},
                                                   {"export", value.binding.exportName},
                                                   {"identity", value.identity},
                                                   {"packageInstance", value.binding.packageInstance},
                                                   {"provenance", ProvenanceValue(value.provenance)},
                                                   {"requirement", value.binding.requirement},
                                                   {"version", value.binding.version}};
                 })},
                {"contributions", Array(graph.contributions, [](const GraphContribution &value) {
                     return CanonicalValue::Object{{"destination", value.destination},
                                                   {"identity", value.identity},
                                                   {"include", value.include},
                                                   {"kind", value.kind},
                                                   {"owner", value.owner},
                                                   {"provenance", ProvenanceValue(value.provenance)}};
                 })},
                {"edges", Array(graph.edges, [](const GraphEdge &value) {
                     return CanonicalValue::Object{{"context", value.context},
                                                   {"from", value.from},
                                                   {"identity", value.identity},
                                                   {"kind", value.kind},
                                                   {"provenance", ProvenanceValue(value.provenance)},
                                                   {"to", value.to},
                                                   {"visibility", value.visibility}};
                 })},
                {"exports", Array(graph.exports, [](const GraphExport &value) {
                     return CanonicalValue::Object{{"identity", value.identity},
                                                   {"kind", ExportKindName(value.kind)},
                                                   {"name", value.name},
                                                   {"packageInstance", value.packageInstance},
                                                   {"provenance", ProvenanceValue(value.provenance)}};
                 })},
                {"kind", "NGIN.CompositionGraph"},
                {"options", Array(graph.options, [](const GraphOption &value) {
                     return CanonicalValue::Object{{"artifact", value.artifact},
                                                   {"identity", value.identity},
                                                   {"name", value.name},
                                                   {"owner", value.owner},
                                                   {"provenance", ProvenanceValue(value.provenance)},
                                                   {"value", value.value}};
                 })},
                {"packages", Array(graph.packages, [](const GraphPackageInstance &value) {
                     CanonicalValue::Object options{};
                     for (const auto &[name, option] : value.artifactOptions) options.emplace(name, option);
                     return CanonicalValue::Object{{"compatibility", CompatibilityValue(value.compatibility)},
                                                   {"context", ContextName(value.context)},
                                                   {"hermetic", value.hermetic},
                                                   {"identity", value.identity},
                                                   {"integrity", value.integrity},
                                                   {"name", value.coordinate.name},
                                                   {"options", options},
                                                   {"provenance", ProvenanceValue(value.provenance)},
                                                   {"providerIdentity", value.providerIdentity},
                                                   {"providerKind", value.providerKind},
                                                   {"revision", value.revision},
                                                   {"source", value.coordinate.sourceBinding.value_or("")},
                                                   {"version", value.coordinate.exactVersion}};
                 })},
                {"plugins", Array(graph.plugins, [](const GraphPlugin &value) {
                     return CanonicalValue::Object{{"export", value.exportName},
                                                   {"identity", value.identity},
                                                   {"packageInstance", value.packageInstance},
                                                   {"provenance", ProvenanceValue(value.provenance)}};
                 })},
                {"product", product},
                {"selection", selection},
                {"state", "resolved"},
            };
        }

        template <typename T>
        auto SortByIdentity(std::vector<T> &values) -> void
        {
            std::ranges::sort(values, {}, &T::identity);
        }

        [[nodiscard]] auto Inventory(const CompositionGraphData &graph)
            -> std::map<std::string, std::map<std::string, std::string, std::less<>>, std::less<>>
        {
            std::map<std::string, std::map<std::string, std::string, std::less<>>, std::less<>> result{};
            const auto add = [&](const std::string &category, const auto &values, const auto &convert) {
                for (const auto &value : values)
                    result[category].emplace(value.identity, SerializeCanonical(convert(value)));
            };
            result["product"].emplace(
                graph.product.identity,
                SerializeCanonical(CanonicalValue::Object{{"linkage", LinkageName(graph.product.linkage)},
                                                           {"name", graph.product.name},
                                                           {"type", std::string(ProductTypeName(graph.product.type))},
                                                           {"version", graph.product.version.value_or("")}}));
            result["selection"].emplace(
                graph.selection.identity,
                SerializeCanonical(CanonicalValue::Object{{"architecture", graph.selection.targetArchitecture},
                                                           {"compiler", graph.selection.compiler},
                                                           {"compilerVersion", graph.selection.compilerVersion},
                                                           {"configuration", graph.selection.configuration},
                                                           {"operatingSystem", graph.selection.targetOperatingSystem},
                                                           {"runtimeLibrary", graph.selection.runtimeLibrary}}));
            add("option", graph.options, [](const auto &value) {
                return CanonicalValue::Object{{"artifact", value.artifact},
                                              {"owner", value.owner},
                                              {"value", value.value}};
            });
            add("package", graph.packages, [](const auto &value) {
                return CanonicalValue::Object{{"context", ContextName(value.context)},
                                              {"name", value.coordinate.name},
                                              {"version", value.coordinate.exactVersion}};
            });
            add("export", graph.exports, [](const auto &value) {
                return CanonicalValue::Object{{"kind", ExportKindName(value.kind)}, {"name", value.name}};
            });
            add("action", graph.actions, [](const auto &value) {
                return CanonicalValue::Object{{"kind", std::string(ActionKindName(value.kind))},
                                              {"tool", value.toolExport}};
            });
            add("plugin", graph.plugins, [](const auto &value) {
                return CanonicalValue::Object{{"export", value.exportName},
                                              {"packageInstance", value.packageInstance}};
            });
            add("capability", graph.capabilities, [](const auto &value) {
                return CanonicalValue::Object{{"capability", value.binding.capability},
                                              {"export", value.binding.exportName},
                                              {"version", value.binding.version}};
            });
            add("contribution", graph.contributions, [](const auto &value) {
                return CanonicalValue::Object{{"destination", value.destination}, {"owner", value.owner}};
            });
            add("buildItem", graph.buildItems, [](const auto &value) {
                return CanonicalValue::Object{{"kind", value.kind}, {"path", value.path}};
            });
            add("edge", graph.edges, [](const auto &value) {
                return CanonicalValue::Object{{"context", value.context},
                                              {"from", value.from},
                                              {"kind", value.kind},
                                              {"to", value.to},
                                              {"visibility", value.visibility}};
            });
            return result;
        }
    }

    ResolvedCompositionGraph::ResolvedCompositionGraph(CompositionGraphData data)
    {
        SortByIdentity(data.options);
        SortByIdentity(data.packages);
        SortByIdentity(data.exports);
        SortByIdentity(data.capabilities);
        SortByIdentity(data.actions);
        for (auto &action : data.actions) std::ranges::sort(action.outputs);
        SortByIdentity(data.plugins);
        SortByIdentity(data.contributions);
        SortByIdentity(data.buildItems);
        SortByIdentity(data.edges);
        data_ = std::make_shared<const CompositionGraphData>(std::move(data));
        canonical_ = SerializeCompositionGraph(*data_);
        identity_ = CanonicalDigestInput("CompositionIdentity", {{"graph", canonical_}});
    }

    auto ResolvedCompositionGraph::Data() const -> const CompositionGraphData & { return *data_; }
    auto ResolvedCompositionGraph::CanonicalSerialization() const -> std::string { return canonical_; }
    auto ResolvedCompositionGraph::CompositionIdentity() const -> std::string { return identity_; }

    auto SerializeCompositionGraph(const CompositionGraphData &graph) -> std::string
    {
        return SerializeCanonical(GraphValue(graph));
    }

    auto DiffCompositionGraphs(const ResolvedCompositionGraph &before, const ResolvedCompositionGraph &after)
        -> std::vector<GraphDifference>
    {
        std::vector<GraphDifference> result{};
        const auto left = Inventory(before.Data());
        const auto right = Inventory(after.Data());
        std::set<std::string, std::less<>> categories{};
        for (const auto &[category, _] : left) categories.insert(category);
        for (const auto &[category, _] : right) categories.insert(category);
        for (const auto &category : categories)
        {
            const auto leftCategory = left.find(category);
            const auto rightCategory = right.find(category);
            std::set<std::string, std::less<>> identities{};
            if (leftCategory != left.end())
                for (const auto &[identity, _] : leftCategory->second) identities.insert(identity);
            if (rightCategory != right.end())
                for (const auto &[identity, _] : rightCategory->second) identities.insert(identity);
            for (const auto &identity : identities)
            {
                const std::string *beforeValue = nullptr;
                const std::string *afterValue = nullptr;
                if (leftCategory != left.end())
                    if (const auto found = leftCategory->second.find(identity); found != leftCategory->second.end())
                        beforeValue = &found->second;
                if (rightCategory != right.end())
                    if (const auto found = rightCategory->second.find(identity); found != rightCategory->second.end())
                        afterValue = &found->second;
                const auto hasBefore = beforeValue != nullptr;
                const auto hasAfter = afterValue != nullptr;
                if (!hasBefore)
                    result.push_back(GraphDifference{.category = category, .identity = identity, .change = "Added",
                                                     .after = *afterValue});
                else if (!hasAfter)
                    result.push_back(GraphDifference{.category = category, .identity = identity, .change = "Removed",
                                                     .before = *beforeValue});
                else if (*beforeValue != *afterValue)
                    result.push_back(GraphDifference{.category = category, .identity = identity, .change = "Changed",
                                                     .before = *beforeValue, .after = *afterValue});
            }
        }
        return result;
    }

    auto ExplainCompositionIdentity(const ResolvedCompositionGraph &graph, const std::string_view identity)
        -> std::optional<GraphExplanation>
    {
        const auto &data = graph.Data();
        GraphExplanation result{.identity = std::string(identity)};
        const auto find = [&](const std::string &category, const auto &values, const auto &describe) -> bool {
            const auto found = std::ranges::find_if(values, [&](const auto &value) { return value.identity == identity; });
            if (found == values.end()) return false;
            result.category = category;
            result.value = describe(*found);
            result.provenance.push_back(found->provenance);
            return true;
        };
        const auto found = find("package", data.packages, [](const auto &value) {
                               return value.coordinate.name + "@" + value.coordinate.exactVersion;
                           }) ||
                           find("export", data.exports, [](const auto &value) { return value.name; }) ||
                           find("action", data.actions, [](const auto &value) { return value.actionExport; }) ||
                           find("capability", data.capabilities,
                                [](const auto &value) { return value.binding.capability + "@" + value.binding.version; }) ||
                           find("contribution", data.contributions,
                                [](const auto &value) { return value.destination; }) ||
                           find("buildItem", data.buildItems, [](const auto &value) { return value.path; }) ||
                           find("option", data.options,
                                [](const auto &value) { return value.name + "=" + value.value; }) ||
                           find("plugin", data.plugins, [](const auto &value) { return value.exportName; }) ||
                           find("edge", data.edges,
                                [](const auto &value) { return value.from + " -> " + value.to; });
        if (!found && data.product.identity == identity)
        {
            result.category = "product";
            result.value = data.product.name;
            result.provenance.push_back(data.product.provenance);
        }
        else if (!found && data.selection.identity == identity)
        {
            result.category = "selection";
            result.value = data.selection.configuration + ":" + data.selection.targetOperatingSystem + ":" +
                           data.selection.targetArchitecture;
            result.provenance.push_back(data.selection.provenance);
        }
        else if (!found) return std::nullopt;
        for (const auto &edge : data.edges)
            if (edge.from == identity || edge.to == identity) result.edges.push_back(edge);
        return result;
    }
}
