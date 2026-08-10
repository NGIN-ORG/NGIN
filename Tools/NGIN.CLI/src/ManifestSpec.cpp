#include "ManifestSpec.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace NGIN::CLI
{
    namespace
    {
        using Attribute = ManifestAttributeSpec;
        using Child = ManifestChildSpec;
        using Element = ManifestElementSpec;

        [[nodiscard]] auto A(std::string name, ManifestValueKind kind = ManifestValueKind::String,
                             bool required = false, std::vector<std::string> values = {},
                             std::string documentation = {}) -> Attribute
        {
            return Attribute{std::move(name), kind, required, std::move(values), std::move(documentation)};
        }

        [[nodiscard]] auto Required(std::string name, ManifestValueKind kind = ManifestValueKind::String,
                                    std::vector<std::string> values = {}) -> Attribute
        {
            return A(std::move(name), kind, true, std::move(values));
        }

        [[nodiscard]] auto C(std::string id, std::size_t minimum = 0,
                             std::optional<std::size_t> maximum = std::nullopt) -> Child
        {
            return Child{std::move(id), minimum, maximum};
        }

        auto Add(ManifestSpec &spec, std::string id, std::string name, std::vector<Attribute> attributes = {},
                 std::vector<Child> children = {}, bool text = false, std::string documentation = {},
                 std::string semanticHook = {}, std::string graphProjection = {}, std::string namespaceUri = {})
            -> void
        {
            spec.elements.push_back(Element{
                .id = std::move(id),
                .name = std::move(name),
                .namespaceUri = std::move(namespaceUri),
                .attributes = std::move(attributes),
                .children = std::move(children),
                .allowsText = text,
                .documentation = std::move(documentation),
                .semanticValidatorHook = std::move(semanticHook),
                .graphProjection = std::move(graphProjection),
            });
        }

        [[nodiscard]] auto ItemAttributes(std::string primary = "Include") -> std::vector<Attribute>
        {
            return {
                A(std::move(primary), ManifestValueKind::Path),
                A("Remove", ManifestValueKind::Path),
                A("Update", ManifestValueKind::Path),
                A("Exclude", ManifestValueKind::Path),
                A("AllowEmpty", ManifestValueKind::Boolean),
                A("Generated", ManifestValueKind::Boolean),
            };
        }

        auto AddMetadata(ManifestSpec &spec, std::string_view prefix, bool package) -> std::string
        {
            const auto base = std::string(prefix);
            const auto metadata = base + ".metadata";
            std::vector<Child> children{
                C(base + ".metadata.description", 0, 1),
                C(base + ".metadata.license", 0, 1),
                C(base + ".metadata.homepage", 0, 1),
                C(base + ".metadata.vendor", 0, 1),
            };
            if (package)
            {
                children.push_back(C(base + ".metadata.repository", 0, 1));
            }
            Add(spec, metadata, "Metadata", {}, std::move(children), false, "Human-facing release metadata.");
            Add(spec, base + ".metadata.description", "Description", {}, {}, true);
            Add(spec, base + ".metadata.license", "License", {}, {}, true);
            Add(spec, base + ".metadata.homepage", "Homepage", {}, {}, true);
            Add(spec, base + ".metadata.vendor", "Vendor", {}, {}, true);
            if (package)
            {
                Add(spec, base + ".metadata.repository", "Repository", {}, {}, true);
            }
            return metadata;
        }

        auto AddOptions(ManifestSpec &spec, std::string_view prefix) -> std::string
        {
            const auto base = std::string(prefix) + ".options";
            Add(spec, base, "Options", {}, {C(base + ".boolean"), C(base + ".enum"), C(base + ".string"),
                                             C(base + ".integer"), C(base + ".path")},
                false, "Typed option declarations.", "validate-option-declarations");
            const auto common = std::vector<Attribute>{Required("Name", ManifestValueKind::Identifier),
                                                       Required("Default"), A("Artifact", ManifestValueKind::Boolean)};
            Add(spec, base + ".boolean", "Boolean",
                {Required("Name", ManifestValueKind::Identifier), Required("Default", ManifestValueKind::Boolean),
                 A("Artifact", ManifestValueKind::Boolean)});
            Add(spec, base + ".enum", "Enum", common, {C(base + ".enum.value", 1)});
            Add(spec, base + ".enum.value", "Value", {Required("Name", ManifestValueKind::Identifier)});
            Add(spec, base + ".string", "String", common, {C(base + ".string.value")});
            Add(spec, base + ".string.value", "Value", {Required("Name")});
            Add(spec, base + ".integer", "Integer",
                {Required("Name", ManifestValueKind::Identifier), Required("Default", ManifestValueKind::Integer),
                 A("Min", ManifestValueKind::Integer), A("Max", ManifestValueKind::Integer),
                 A("Artifact", ManifestValueKind::Boolean)});
            Add(spec, base + ".path", "Path",
                {Required("Name", ManifestValueKind::Identifier), Required("Default", ManifestValueKind::Path),
                 A("Artifact", ManifestValueKind::Boolean)});
            return base;
        }

        auto AddVersionAndUse(ManifestSpec &spec, std::string_view prefix) -> std::pair<std::string, std::string>
        {
            const auto base = std::string(prefix);
            const auto version = base + ".version-range";
            const auto use = base + ".use";
            Add(spec, version, "Version",
                {A("AtLeast", ManifestValueKind::SemanticVersion), A("After", ManifestValueKind::SemanticVersion),
                 A("AtMost", ManifestValueKind::SemanticVersion), A("Before", ManifestValueKind::SemanticVersion)},
                {}, false, "Structured version interval.", "validate-version-range");
            Add(spec, use, "Use",
                {A("Library", ManifestValueKind::Identifier), A("Tool", ManifestValueKind::Identifier),
                 A("Plugin", ManifestValueKind::Identifier), A("Action", ManifestValueKind::Identifier),
                 A("Asset", ManifestValueKind::Identifier)},
                {}, false, "Activates one named package export.", "validate-one-export-kind");
            return {version, use};
        }

        auto AddPackageReference(ManifestSpec &spec, std::string_view prefix) -> std::string
        {
            const auto base = std::string(prefix);
            const auto package = base + ".package";
            const auto [version, use] = AddVersionAndUse(spec, base);
            Add(spec, package, "Package",
                {Required("Name", ManifestValueKind::Identifier), A("Exact", ManifestValueKind::SemanticVersion),
                 A("Compatible", ManifestValueKind::VersionCompatibility)},
                {C(version, 0, 1), C(use), C(base + ".option")}, false, "Package coordinate and export activation.",
                "validate-package-coordinate", "dependency");
            Add(spec, base + ".option", "Option",
                {Required("Name", ManifestValueKind::Identifier), Required("Value")});
            return package;
        }

        auto AddBuild(ManifestSpec &spec, std::string_view prefix) -> std::string
        {
            const auto base = std::string(prefix) + ".build";
            Add(spec, base, "Build", {A("Conventions", ManifestValueKind::Boolean)},
                {C(base + ".language", 0, 1), C(base + ".source"), C(base + ".header"),
                 C(base + ".cxx-module"), C(base + ".resource"), C(base + ".include-directory"),
                 C(base + ".define"), C(base + ".compile-option"), C(base + ".link-option"),
                 C(base + ".precompiled-header"), C(base + ".unity-build", 0, 1), C(base + ".convention")},
                false, "Backend-neutral build inputs and usage requirements.", "validate-build-items", "build");
            Add(spec, base + ".language", "Language",
                {Required("Standard"), A("Extensions", ManifestValueKind::Boolean),
                 A("Required", ManifestValueKind::Boolean)});
            auto source = ItemAttributes();
            Add(spec, base + ".source", "Source", source);
            auto header = ItemAttributes();
            header.push_back(A("Visibility", ManifestValueKind::Enumeration, false, {"Private", "Public", "Interface"}));
            Add(spec, base + ".header", "Header", header);
            auto module = ItemAttributes();
            module.push_back(A("Kind", ManifestValueKind::Enumeration, false, {"Interface", "Implementation"}));
            module.push_back(A("Visibility", ManifestValueKind::Enumeration, false, {"Private", "Public", "Interface"}));
            Add(spec, base + ".cxx-module", "CxxModule", module);
            auto resource = ItemAttributes();
            resource.push_back(A("Into", ManifestValueKind::Path));
            Add(spec, base + ".resource", "Resource", resource);
            Add(spec, base + ".include-directory", "IncludeDirectory",
                {Required("Path", ManifestValueKind::Path),
                 A("Visibility", ManifestValueKind::Enumeration, false, {"Private", "Public", "Interface"}),
                 A("System", ManifestValueKind::Boolean)});
            Add(spec, base + ".define", "Define",
                {Required("Name", ManifestValueKind::Identifier), A("Value"),
                 A("Visibility", ManifestValueKind::Enumeration, false, {"Private", "Public", "Interface"})});
            Add(spec, base + ".compile-option", "CompileOption",
                {Required("Value"),
                 A("Visibility", ManifestValueKind::Enumeration, false, {"Private", "Public", "Interface"})});
            Add(spec, base + ".link-option", "LinkOption",
                {Required("Value"),
                 A("Visibility", ManifestValueKind::Enumeration, false, {"Private", "Public", "Interface"})});
            Add(spec, base + ".precompiled-header", "PrecompiledHeader",
                {Required("Path", ManifestValueKind::Path),
                 A("Visibility", ManifestValueKind::Enumeration, false, {"Private", "Public", "Interface"})});
            Add(spec, base + ".unity-build", "UnityBuild",
                {Required("Enabled", ManifestValueKind::Boolean), A("BatchSize", ManifestValueKind::Integer)});
            Add(spec, base + ".convention", "Convention",
                {Required("Name", ManifestValueKind::Identifier), Required("Enabled", ManifestValueKind::Boolean)});
            return base;
        }

        auto AddProjectSpec(ManifestSpec &spec) -> void
        {
            const auto metadata = AddMetadata(spec, "project", false);
            const auto options = AddOptions(spec, "project");
            const auto dependencyPackage = AddPackageReference(spec, "project.dependencies");
            const auto build = AddBuild(spec, "project");

            Add(spec, "project.dependencies", "Dependencies", {},
                {C(dependencyPackage), C("project.dependencies.project")}, false, "Direct product dependencies.");
            Add(spec, "project.dependencies.project", "Project",
                {Required("Name", ManifestValueKind::Identifier), A("Path", ManifestValueKind::Path)});

            Add(spec, "project.generate", "Generate", {Required("Action")},
                {C("project.generate.input"), C("project.generate.option"), C("project.generate.argument")}, false,
                "Selects a Generate Action.", "validate-action-selection", "action");
            Add(spec, "project.generate.input", "Input", ItemAttributes());
            Add(spec, "project.generate.option", "Option",
                {Required("Name", ManifestValueKind::Identifier), Required("Value")});
            Add(spec, "project.generate.argument", "Argument", {}, {}, true);

            Add(spec, "project.tooling", "Tooling", {},
                {C("project.tooling.analyze"), C("project.tooling.format"), C("project.tooling.validate"),
                 C("project.tooling.custom")});
            for (const auto &entry : {std::pair{"analyze", "Analyze"}, std::pair{"format", "Format"},
                                      std::pair{"validate", "Validate"}, std::pair{"custom", "Custom"}})
            {
                Add(spec, "project.tooling." + std::string(entry.first), entry.second, {Required("Action")});
            }

            Add(spec, "project.stage", "Stage", {}, {C("project.stage.file"), C("project.stage.directory")}, false,
                "Project-owned deployment inputs.", "validate-stage-destinations", "stage");
            Add(spec, "project.stage.file", "File",
                {Required("Include", ManifestValueKind::Path), Required("Into", ManifestValueKind::Path)});
            Add(spec, "project.stage.directory", "Directory",
                {Required("Include", ManifestValueKind::Path), Required("Into", ManifestValueKind::Path)});

            Add(spec, "project.launch", "Launch",
                {Required("Name", ManifestValueKind::Identifier), A("Default", ManifestValueKind::Boolean)},
                {C("project.launch.executable", 1, 1), C("project.launch.working-directory", 0, 1),
                 C("project.launch.argument"), C("project.launch.environment"), C("project.launch.secret")},
                false, "Process launch intent.", "validate-launch", "launch");
            Add(spec, "project.launch.executable", "Executable",
                {A("Product", ManifestValueKind::Identifier), A("Tool")}, {}, false, {}, "validate-executable-selector");
            Add(spec, "project.launch.working-directory", "WorkingDirectory",
                {Required("Path", ManifestValueKind::Path)});
            Add(spec, "project.launch.argument", "Argument", {}, {}, true);
            Add(spec, "project.launch.environment", "Environment", {Required("Name"), Required("Value")});
            Add(spec, "project.launch.secret", "Secret", {Required("Name"), Required("From")});

            const auto testingPackage = AddPackageReference(spec, "project.testing.dependencies");
            Add(spec, "project.testing.dependencies", "Dependencies", {},
                {C(testingPackage), C("project.testing.dependencies.project")});
            Add(spec, "project.testing.dependencies.project", "Project",
                {Required("Name", ManifestValueKind::Identifier), A("Path", ManifestValueKind::Path)});
            Add(spec, "project.testing", "Testing", {},
                {C("project.testing.dependencies", 0, 1), C("project.testing.argument"),
                 C("project.testing.timeout", 0, 1)}, false, {}, "validate-testing", "test");
            Add(spec, "project.testing.argument", "Argument", {}, {}, true);
            Add(spec, "project.testing.timeout", "Timeout", {Required("Seconds", ManifestValueKind::Integer)});

            const auto publishPackage = AddPackageReference(spec, "project.publish.dependencies");
            Add(spec, "project.publish.dependencies", "Dependencies", {},
                {C(publishPackage), C("project.publish.dependencies.project")});
            Add(spec, "project.publish.dependencies.project", "Project",
                {Required("Name", ManifestValueKind::Identifier), A("Path", ManifestValueKind::Path)});
            Add(spec, "project.publish", "Publish", {Required("Name", ManifestValueKind::Identifier)},
                {C("project.publish.folder", 0, 1), C("project.publish.archive", 0, 1),
                 C("project.publish.installer", 0, 1), C("project.publish.dependencies", 0, 1)}, false, {},
                "validate-one-publish-output", "publish");
            Add(spec, "project.publish.folder", "Folder", {Required("Output", ManifestValueKind::Path)});
            Add(spec, "project.publish.archive", "Archive",
                {Required("Format", ManifestValueKind::Enumeration, {"zip", "tgz"}),
                 Required("Output", ManifestValueKind::Path)});
            Add(spec, "project.publish.installer", "Installer",
                {Required("Format", ManifestValueKind::Enumeration, {"msi", "deb"}),
                 Required("Output", ManifestValueKind::Path)});

            Add(spec, "project.refinements", "Refinements", {}, {C("project.refinement")});
            Add(spec, "project.refinement", "Refinement", {},
                {C("project.refinement.select", 1, 1), C("project.refinement.build", 0, 1),
                 C("project.refinement.dependencies", 0, 1), C("project.refinement.stage", 0, 1)},
                false, {}, "validate-refinement");
            Add(spec, "project.refinement.select", "Select", {},
                {C("project.refinement.select.configuration", 0, 1), C("project.refinement.select.target", 0, 1),
                 C("project.refinement.select.toolchain", 0, 1), C("project.refinement.select.option")});
            Add(spec, "project.refinement.select.configuration", "Configuration",
                {Required("Name", ManifestValueKind::Identifier)});
            Add(spec, "project.refinement.select.target", "Target",
                {A("Name", ManifestValueKind::Identifier), A("OS"), A("Architecture")});
            Add(spec, "project.refinement.select.toolchain", "Toolchain",
                {A("Name", ManifestValueKind::Identifier), A("Compiler")});
            Add(spec, "project.refinement.select.option", "Option",
                {Required("Name", ManifestValueKind::Identifier), Required("Value")});
            AddBuild(spec, "project.refinement");
            const auto refinementPackage = AddPackageReference(spec, "project.refinement.dependencies");
            Add(spec, "project.refinement.dependencies", "Dependencies", {}, {C(refinementPackage)});
            Add(spec, "project.refinement.stage", "Stage", {},
                {C("project.refinement.stage.file"), C("project.refinement.stage.directory")});
            Add(spec, "project.refinement.stage.file", "File",
                {Required("Include", ManifestValueKind::Path), Required("Into", ManifestValueKind::Path)});
            Add(spec, "project.refinement.stage.directory", "Directory",
                {Required("Include", ManifestValueKind::Path), Required("Into", ManifestValueKind::Path)});

            Add(spec, "project.root", "Project",
                {Required("Name", ManifestValueKind::Identifier),
                 Required("Type", ManifestValueKind::Enumeration,
                          {"Application", "Library", "Tool", "Test", "Benchmark", "Plugin", "External"}),
                 A("Version", ManifestValueKind::SemanticVersion),
                 A("Linkage", ManifestValueKind::Enumeration, false, {"Static", "Shared", "Interface"})},
                {C(metadata, 0, 1), C(options, 0, 1), C("project.dependencies", 0, 1), C(build, 0, 1),
                 C("project.generate"), C("project.tooling", 0, 1), C("project.stage", 0, 1),
                 C("project.launch"), C("project.testing", 0, 1), C("project.publish"),
                 C("project.refinements", 0, 1)},
                false, "One primary NGIN product.", "validate-project-product", "product");
        }

        auto AddPackageSpec(ManifestSpec &spec) -> void
        {
            const auto metadata = AddMetadata(spec, "package", true);
            const auto options = AddOptions(spec, "package");
            const auto requirementPackage = AddPackageReference(spec, "package.requires");

            Add(spec, "package.requires", "Requires", {},
                {C(requirementPackage), C("package.requires.project"), C("package.requires.capability"),
                 C("package.requires.option-predicate"), C("package.requires.export"), C("package.requires.when")},
                false, "Requirements activated with their owning scope.", "validate-requirements");
            Add(spec, "package.requires.project", "Project",
                {Required("Name", ManifestValueKind::Identifier), A("Path", ManifestValueKind::Path)});
            Add(spec, "package.requires.capability", "Capability",
                {Required("Name", ManifestValueKind::Identifier), A("Exact", ManifestValueKind::SemanticVersion),
                 A("Compatible", ManifestValueKind::VersionCompatibility)},
                {C("package.requires.capability.version", 0, 1)}, false, {}, "validate-capability-requirement");
            Add(spec, "package.requires.capability.version", "Version",
                {A("AtLeast", ManifestValueKind::SemanticVersion), A("After", ManifestValueKind::SemanticVersion),
                 A("AtMost", ManifestValueKind::SemanticVersion), A("Before", ManifestValueKind::SemanticVersion)});
            Add(spec, "package.requires.option-predicate", "Option",
                {Required("Name", ManifestValueKind::Identifier), Required("Value")});
            Add(spec, "package.requires.export", "Export",
                {A("Library", ManifestValueKind::Identifier), A("Tool", ManifestValueKind::Identifier),
                 A("Plugin", ManifestValueKind::Identifier), A("Action", ManifestValueKind::Identifier),
                 A("Asset", ManifestValueKind::Identifier)}, {}, false, {}, "validate-one-export-kind");
            Add(spec, "package.requires.when", "When",
                {A("Option", ManifestValueKind::Identifier), A("Equals"), A("TargetOS"), A("Architecture"),
                 A("Compiler")},
                {C(requirementPackage), C("package.requires.project"), C("package.requires.capability")}, false, {},
                "validate-requirement-condition");

            Add(spec, "package.contributions", "Contributions", {},
                {C("package.contributions.notices", 0, 1), C("package.contributions.runtime-files", 0, 1)});
            Add(spec, "package.contributions.notices", "Notices", {}, {C("package.notice")});
            Add(spec, "package.contributions.runtime-files", "RuntimeFiles", {},
                {C("package.runtime-file"), C("package.runtime-directory")});
            Add(spec, "package.notice", "Notice",
                {Required("Include", ManifestValueKind::Path), Required("Into", ManifestValueKind::Path)});
            Add(spec, "package.runtime-file", "File",
                {Required("Include", ManifestValueKind::Path), Required("Into", ManifestValueKind::Path)});
            Add(spec, "package.runtime-directory", "Directory",
                {Required("Include", ManifestValueKind::Path), Required("Into", ManifestValueKind::Path)});

            const std::vector<Child> exportChildren{
                C("package.requires", 0, 1), C("package.provides", 0, 1), C("package.export.runtime-files", 0, 1),
                C("package.export.notices", 0, 1)};
            Add(spec, "package.exports", "Exports", {},
                {C("package.export.library"), C("package.export.tool"), C("package.export.plugin"),
                 C("package.export.action"), C("package.export.asset")},
                false, "Named semantic exports.", "validate-exports", "exports");
            for (const auto &entry : {std::pair{"library", "Library"}, std::pair{"tool", "Tool"},
                                      std::pair{"plugin", "Plugin"}})
            {
                Add(spec, "package.export." + std::string(entry.first), entry.second,
                    {Required("Name", ManifestValueKind::Identifier), A("Default", ManifestValueKind::Boolean)},
                    exportChildren);
            }
            Add(spec, "package.provides", "Provides", {}, {C("package.provides.capability")});
            Add(spec, "package.provides.capability", "Capability",
                {Required("Name", ManifestValueKind::Identifier),
                 Required("Version", ManifestValueKind::SemanticVersion)});
            Add(spec, "package.export.runtime-files", "RuntimeFiles", {},
                {C("package.runtime-file"), C("package.runtime-directory")});
            Add(spec, "package.export.notices", "Notices", {}, {C("package.notice")});

            Add(spec, "package.export.action", "Action",
                {Required("Name", ManifestValueKind::Identifier),
                 Required("Kind", ManifestValueKind::Enumeration,
                          {"Generate", "Analyze", "Format", "Validate", "Custom"}),
                 Required("Tool", ManifestValueKind::Identifier), A("Default", ManifestValueKind::Boolean),
                 A("Deterministic", ManifestValueKind::Boolean)},
                {C("package.requires", 0, 1), C("package.provides", 0, 1), C("package.action.inputs", 0, 1),
                 C("package.action.outputs", 0, 1), C("package.action.argument"),
                 C("package.action.working-directory", 0, 1), C("package.action.environment")},
                false, {}, "validate-action", "action");
            Add(spec, "package.action.inputs", "Inputs", {},
                {C("package.action.input.header"), C("package.action.input.source"), C("package.action.input.file")});
            Add(spec, "package.action.input.header", "Header", ItemAttributes());
            Add(spec, "package.action.input.source", "Source", ItemAttributes());
            Add(spec, "package.action.input.file", "File", ItemAttributes());
            Add(spec, "package.action.outputs", "Outputs", {},
                {C("package.action.output.source"), C("package.action.output.header"), C("package.action.output.file"),
                 C("package.action.output.directory")});
            Add(spec, "package.action.output.source", "Source", {Required("Path", ManifestValueKind::Path)});
            Add(spec, "package.action.output.header", "Header", {Required("Path", ManifestValueKind::Path)});
            Add(spec, "package.action.output.file", "File", {Required("Path", ManifestValueKind::Path)});
            Add(spec, "package.action.output.directory", "Directory", {Required("Path", ManifestValueKind::Path)});
            Add(spec, "package.action.argument", "Argument", {}, {}, true);
            Add(spec, "package.action.working-directory", "WorkingDirectory",
                {Required("Path", ManifestValueKind::Path)});
            Add(spec, "package.action.environment", "Environment", {Required("Name"), Required("Value")});

            Add(spec, "package.export.asset", "Asset",
                {Required("Name", ManifestValueKind::Identifier), A("Description"),
                 A("Default", ManifestValueKind::Boolean)},
                {C("package.asset.file"), C("package.asset.directory")});
            Add(spec, "package.asset.file", "File",
                {Required("Include", ManifestValueKind::Path), Required("Into", ManifestValueKind::Path)});
            Add(spec, "package.asset.directory", "Directory",
                {Required("Include", ManifestValueKind::Path), Required("Into", ManifestValueKind::Path)});

            Add(spec, "package.integrations", "Integrations", {},
                {C("cmake.add-subdirectory"), C("cmake.isolated"), C("cmake.find-package"), C("cmake.manual")},
                false, "Registered backend integration metadata.", "validate-integration-selection");
            Add(spec, "package.compatibility", "Compatibility", {},
                {C("package.compatibility.target"), C("package.compatibility.toolchain")}, false, {},
                "validate-compatibility", "compatibility");
            Add(spec, "package.compatibility.target", "Target", {A("OS"), A("Architecture")});
            Add(spec, "package.compatibility.toolchain", "Toolchain",
                {A("Compiler"), A("RuntimeLibrary"), A("Linker")});

            Add(spec, "package.root", "Package",
                {Required("Name", ManifestValueKind::Identifier),
                 Required("Version", ManifestValueKind::SemanticVersion)},
                {C(metadata, 0, 1), C(options, 0, 1), C("package.requires", 0, 1),
                 C("package.contributions", 0, 1), C("package.exports", 1, 1),
                 C("package.integrations", 0, 1), C("package.compatibility", 0, 1)},
                false, "One exact package release.", "validate-package", "package");
        }

        auto AddCMakeSpec(ManifestSpec &spec) -> void
        {
            const auto ns = std::string(CMakeIntegrationNamespace);
            const auto commonChildren = std::vector<Child>{C("cmake.cache"), C("cmake.map-option"),
                                                           C("cmake.target"), C("cmake.select")};
            Add(spec, "cmake.add-subdirectory", "AddSubdirectory", {Required("Source", ManifestValueKind::Path)},
                commonChildren, false, "Embeds a source-backed CMake package.", "validate-cmake-add-subdirectory", {}, ns);
            Add(spec, "cmake.manual", "Manual", {Required("Source", ManifestValueKind::Path)}, commonChildren, false,
                "Uses a package-owned CMake wrapper.", "validate-cmake-manual", {}, ns);
            Add(spec, "cmake.find-package", "FindPackage",
                {Required("Name"), A("Config", ManifestValueKind::Boolean), A("Required", ManifestValueKind::Boolean),
                 A("Version", ManifestValueKind::SemanticVersion)},
                {C("cmake.target"), C("cmake.select")}, false, "Maps a resolved package through find_package.",
                "validate-cmake-find-package", {}, ns);
            Add(spec, "cmake.isolated", "Isolated", {Required("Source", ManifestValueKind::Path)},
                {C("cmake.cache"), C("cmake.map-option"), C("cmake.install", 0, 1),
                 C("cmake.find-package", 1, 1), C("cmake.select")},
                false, "Builds and installs a package into an isolated prefix.", "validate-cmake-isolated", {}, ns);
            Add(spec, "cmake.cache", "Cache",
                {Required("Name"), Required("Value"),
                 A("Type", ManifestValueKind::Enumeration, false, {"BOOL", "STRING", "PATH", "FILEPATH"}),
                 A("Artifact", ManifestValueKind::Boolean)},
                {}, false, {}, "validate-cmake-cache", {}, ns);
            Add(spec, "cmake.map-option", "MapOption",
                {Required("Option", ManifestValueKind::Identifier), Required("Cache"), A("True"), A("False"),
                 A("Value"), A("Artifact", ManifestValueKind::Boolean)},
                {}, false, {}, "validate-cmake-option-map", {}, ns);
            Add(spec, "cmake.target", "Target",
                {Required("Export", ManifestValueKind::Identifier), Required("Name")}, {}, false, {},
                "validate-cmake-target-map", {}, ns);
            Add(spec, "cmake.install", "Install", {}, {}, false, {}, {}, {}, ns);
            Add(spec, "cmake.select", "Select", {},
                {C("cmake.select.configuration", 0, 1), C("cmake.select.target", 0, 1),
                 C("cmake.select.toolchain", 0, 1), C("cmake.select.option"), C("cmake.cache"),
                 C("cmake.map-option"), C("cmake.target")},
                false, {}, "validate-cmake-selection", {}, ns);
            Add(spec, "cmake.select.configuration", "Configuration", {Required("Name")});
            Add(spec, "cmake.select.target", "Target", {A("Name"), A("OS"), A("Architecture")});
            Add(spec, "cmake.select.toolchain", "Toolchain", {A("Name"), A("Compiler")});
            Add(spec, "cmake.select.option", "Option", {Required("Name"), Required("Value")});
        }

        auto AddWorkspaceSpec(ManifestSpec &spec) -> void
        {
            Add(spec, "workspace.projects", "Projects", {}, {C("workspace.projects.project", 1)});
            Add(spec, "workspace.projects.project", "Project",
                {A("Path", ManifestValueKind::Path), A("Include", ManifestValueKind::Path),
                 A("Exclude", ManifestValueKind::Path)}, {}, false, {}, "validate-project-discovery");

            Add(spec, "workspace.configurations", "Configurations", {}, {C("workspace.configuration")});
            Add(spec, "workspace.configuration", "Configuration", {Required("Name", ManifestValueKind::Identifier)},
                {C("workspace.configuration.optimization", 0, 1),
                 C("workspace.configuration.debug-symbols", 0, 1),
                 C("workspace.configuration.lto", 0, 1), C("workspace.configuration.option")});
            Add(spec, "workspace.configuration.optimization", "Optimization",
                {Required("Mode", ManifestValueKind::Enumeration, {"Off", "Size", "Speed", "Full"})});
            Add(spec, "workspace.configuration.debug-symbols", "DebugSymbols",
                {Required("Enabled", ManifestValueKind::Boolean)});
            Add(spec, "workspace.configuration.lto", "LinkTimeOptimization",
                {Required("Enabled", ManifestValueKind::Boolean)});
            Add(spec, "workspace.configuration.option", "Option", {Required("Name"), Required("Value")});

            Add(spec, "workspace.targets", "Targets", {}, {C("workspace.target")});
            Add(spec, "workspace.target", "Target",
                {Required("Name", ManifestValueKind::Identifier), Required("OS"), Required("Architecture"),
                 A("Emulator")}, {C("workspace.target.alias")});
            Add(spec, "workspace.target.alias", "Alias", {Required("Name", ManifestValueKind::Identifier)});
            Add(spec, "workspace.toolchains", "Toolchains", {}, {C("workspace.toolchain")});
            Add(spec, "workspace.toolchain", "Toolchain",
                {Required("Name", ManifestValueKind::Identifier), Required("Compiler"), A("CompilerVersion"),
                 A("RuntimeLibrary"), A("Linker"), A("ToolchainFile", ManifestValueKind::Path)});

            Add(spec, "workspace.defaults", "Defaults", {},
                {C("workspace.defaults.output-root", 0, 1), C("workspace.defaults.configuration", 0, 1),
                 C("workspace.defaults.target", 0, 1), C("workspace.defaults.toolchain", 0, 1),
                 C("workspace.defaults.option")});
            Add(spec, "workspace.defaults.output-root", "OutputRoot", {Required("Path", ManifestValueKind::Path)});
            Add(spec, "workspace.defaults.configuration", "Configuration", {Required("Name")});
            Add(spec, "workspace.defaults.target", "Target", {Required("Name")});
            Add(spec, "workspace.defaults.toolchain", "Toolchain", {Required("Name")});
            Add(spec, "workspace.defaults.option", "Option", {Required("Name"), Required("Value")});

            Add(spec, "workspace.packages", "Packages", {},
                {C("workspace.packages.source"), C("workspace.packages.local-package"),
                 C("workspace.packages.version"), C("workspace.packages.binding")},
                false, {}, "validate-package-policy");
            Add(spec, "workspace.packages.source", "Source",
                {Required("Name", ManifestValueKind::Identifier), Required("Kind"), A("Path", ManifestValueKind::Path),
                 A("Url")});
            Add(spec, "workspace.packages.local-package", "LocalPackage",
                {Required("Name", ManifestValueKind::Identifier), Required("Manifest", ManifestValueKind::Path),
                 Required("Root", ManifestValueKind::Path)});
            Add(spec, "workspace.packages.version", "Version",
                {Required("Name", ManifestValueKind::Identifier), A("Exact", ManifestValueKind::SemanticVersion),
                 A("Compatible", ManifestValueKind::VersionCompatibility), A("AtLeast", ManifestValueKind::SemanticVersion),
                 A("After", ManifestValueKind::SemanticVersion), A("AtMost", ManifestValueKind::SemanticVersion),
                 A("Before", ManifestValueKind::SemanticVersion)}, {}, false, {}, "validate-central-version");
            Add(spec, "workspace.packages.binding", "Binding",
                {Required("Package", ManifestValueKind::Identifier), Required("Source", ManifestValueKind::Identifier),
                 Required("Coordinate")});

            Add(spec, "workspace.policies", "Policies", {},
                {C("workspace.policies.providers", 0, 1), C("workspace.policies.actions", 0, 1),
                 C("workspace.policies.paths", 0, 1), C("workspace.policies.stage", 0, 1),
                 C("workspace.policies.compatibility", 0, 1)},
                false, "Non-overridable trust and reproducibility gates.", "validate-policies");
            Add(spec, "workspace.policies.providers", "PackageProviders",
                {A("Allowed"), A("IntegrityRequired", ManifestValueKind::Boolean),
                 A("Locked", ManifestValueKind::Boolean), A("AllowNonHermetic", ManifestValueKind::Boolean)});
            Add(spec, "workspace.policies.actions", "Actions",
                {A("AllowedOrigins"), A("RequireSignature", ManifestValueKind::Boolean),
                 A("RequireConfirmation", ManifestValueKind::Boolean)});
            Add(spec, "workspace.policies.paths", "Paths",
                {A("AllowSymlinks", ManifestValueKind::Boolean), A("RequireContained", ManifestValueKind::Boolean)});
            Add(spec, "workspace.policies.stage", "Stage",
                {A("Collision", ManifestValueKind::Enumeration, false, {"Error", "IdenticalBytes"})});
            Add(spec, "workspace.policies.compatibility", "Compatibility",
                {A("AllowedTargets"), A("AllowedToolchains")});

            Add(spec, "workspace.presets", "Presets", {}, {C("workspace.preset")});
            Add(spec, "workspace.preset", "Preset",
                {Required("Name", ManifestValueKind::Identifier), Required("Command")},
                {C("workspace.preset.configuration", 0, 1), C("workspace.preset.target", 0, 1),
                 C("workspace.preset.toolchain", 0, 1), C("workspace.preset.option"),
                 C("workspace.preset.launch", 0, 1)});
            Add(spec, "workspace.preset.configuration", "Configuration", {Required("Name")});
            Add(spec, "workspace.preset.target", "Target", {Required("Name")});
            Add(spec, "workspace.preset.toolchain", "Toolchain", {Required("Name")});
            Add(spec, "workspace.preset.option", "Option", {Required("Name"), Required("Value")});
            Add(spec, "workspace.preset.launch", "Launch", {Required("Name")});

            Add(spec, "workspace.root", "Workspace", {Required("Name", ManifestValueKind::Identifier)},
                {C("workspace.projects", 1, 1), C("workspace.configurations", 0, 1),
                 C("workspace.targets", 0, 1), C("workspace.toolchains", 0, 1),
                 C("workspace.defaults", 0, 1), C("workspace.packages", 0, 1),
                 C("workspace.policies", 0, 1), C("workspace.presets", 0, 1)},
                false, "Workspace discovery, selection, policy, and presets.", "validate-workspace", "workspace");
        }

        [[nodiscard]] auto BuildSpec() -> ManifestSpec
        {
            ManifestSpec spec{};
            AddProjectSpec(spec);
            AddPackageSpec(spec);
            AddCMakeSpec(spec);
            AddWorkspaceSpec(spec);
            spec.documents = {
                {ManifestDocumentKind::Project, ".nginproj", "project.root", "project.xsd"},
                {ManifestDocumentKind::Package, ".nginpkg", "package.root", "package.xsd"},
                {ManifestDocumentKind::Workspace, ".ngin", "workspace.root", "workspace.xsd"},
            };
            spec.namespaces = {{std::string(CMakeIntegrationNamespace), "cmake", "cmake-integration.xsd",
                                {"cmake.add-subdirectory", "cmake.isolated", "cmake.find-package", "cmake.manual"}}};
            std::vector<std::string> ids{};
            ids.reserve(spec.elements.size());
            for (const auto &element : spec.elements)
            {
                if (std::ranges::find(ids, element.id) != ids.end())
                {
                    throw std::logic_error("duplicate manifest element id: " + element.id);
                }
                ids.push_back(element.id);
            }
            for (const auto &element : spec.elements)
            {
                for (const auto &child : element.children)
                {
                    if (std::ranges::find(ids, child.elementId) == ids.end())
                    {
                        throw std::logic_error("manifest element '" + element.id + "' references unknown child '" +
                                               child.elementId + "'");
                    }
                }
            }
            return spec;
        }
    }

    auto ManifestSpec::Documents() const -> std::span<const ManifestDocumentSpec> { return documents; }
    auto ManifestSpec::Elements() const -> std::span<const ManifestElementSpec> { return elements; }
    auto ManifestSpec::Namespaces() const -> std::span<const ManifestNamespaceSpec> { return namespaces; }

    auto ManifestSpec::Document(const ManifestDocumentKind kind) const -> const ManifestDocumentSpec &
    {
        const auto found = std::ranges::find(documents, kind, &ManifestDocumentSpec::kind);
        if (found == documents.end())
        {
            throw std::logic_error("manifest document kind is not registered");
        }
        return *found;
    }

    auto ManifestSpec::Element(const std::string_view id) const -> const ManifestElementSpec &
    {
        const auto *element = FindElement(id);
        if (element == nullptr)
        {
            throw std::logic_error("manifest element id is not registered: " + std::string(id));
        }
        return *element;
    }

    auto ManifestSpec::FindElement(const std::string_view id) const -> const ManifestElementSpec *
    {
        const auto found = std::ranges::find(elements, id, &ManifestElementSpec::id);
        return found == elements.end() ? nullptr : &*found;
    }

    auto ManifestSpec::FindChild(const ManifestElementSpec &parent, const std::string_view namespaceUri,
                                 const std::string_view localName) const -> const ManifestElementSpec *
    {
        for (const auto &child : parent.children)
        {
            const auto &element = Element(child.elementId);
            if (element.namespaceUri == namespaceUri && element.name == localName)
            {
                return &element;
            }
        }
        return nullptr;
    }

    auto ManifestSpec::FindNamespace(const std::string_view uri) const -> const ManifestNamespaceSpec *
    {
        const auto found = std::ranges::find(namespaces, uri, &ManifestNamespaceSpec::uri);
        return found == namespaces.end() ? nullptr : &*found;
    }

    auto CurrentManifestSpec() -> const ManifestSpec &
    {
        static const auto spec = BuildSpec();
        return spec;
    }

    auto ManifestDocumentKindName(const ManifestDocumentKind kind) -> std::string_view
    {
        switch (kind)
        {
        case ManifestDocumentKind::Project: return "Project";
        case ManifestDocumentKind::Package: return "Package";
        case ManifestDocumentKind::Workspace: return "Workspace";
        }
        return "Unknown";
    }

    auto ManifestValueKindName(const ManifestValueKind kind) -> std::string_view
    {
        switch (kind)
        {
        case ManifestValueKind::String: return "string";
        case ManifestValueKind::Identifier: return "identifier";
        case ManifestValueKind::Boolean: return "boolean";
        case ManifestValueKind::Integer: return "integer";
        case ManifestValueKind::Path: return "path";
        case ManifestValueKind::SemanticVersion: return "semantic-version";
        case ManifestValueKind::VersionCompatibility: return "version-compatibility";
        case ManifestValueKind::Enumeration: return "enumeration";
        }
        return "unknown";
    }
}
