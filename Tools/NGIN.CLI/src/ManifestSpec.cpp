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
            return {version, use};
        }

        auto AddPackageReference(ManifestSpec &spec, std::string_view prefix,
                                 const bool requirementVisibility = false) -> std::string
        {
            const auto base = std::string(prefix);
            const auto package = base + ".package";
            const auto [version, use] = AddVersionAndUse(spec, base);
            auto attributes = std::vector<Attribute>{
                Required("Name", ManifestValueKind::Identifier), A("Exact", ManifestValueKind::SemanticVersion),
                A("Version", ManifestValueKind::VersionCompatibility)};
            if (requirementVisibility)
                attributes.push_back(A("Public", ManifestValueKind::Boolean));
            Add(spec, package, "Package", std::move(attributes),
                {C(version, 0, 1), C(base + ".library"), C(base + ".tool"), C(base + ".plugin"),
                 C(base + ".generator"), C(base + ".analyzer"), C(base + ".formatter"),
                 C(base + ".validator"), C(base + ".action"), C(base + ".asset"), C(base + ".option")},
                false, "Package coordinate and typed export activation.",
                "validate-package-coordinate", "dependency");
            for (const auto &entry : {std::pair{"library", "Library"}, std::pair{"tool", "Tool"},
                                      std::pair{"plugin", "Plugin"}, std::pair{"generator", "Generator"},
                                      std::pair{"analyzer", "Analyzer"}, std::pair{"formatter", "Formatter"},
                                      std::pair{"validator", "Validator"}, std::pair{"action", "Action"},
                                      std::pair{"asset", "Asset"}})
            {
                auto selectionAttributes = std::vector<Attribute>{Required("Name", ManifestValueKind::Identifier)};
                if (requirementVisibility)
                    selectionAttributes.push_back(A("Public", ManifestValueKind::Boolean));
                Add(spec, base + "." + std::string(entry.first), entry.second, std::move(selectionAttributes));
            }
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
            const auto build = AddBuild(spec, "project");
            const auto [usesVersion, ignoredUse] = AddVersionAndUse(spec, "project.uses");
            (void)ignoredUse;
            Add(spec, "project.uses", "Uses", {},
                {C("project.uses.package"), C("project.uses.project"), C("project.uses.capability")}, false,
                "Packages, source products, and abstract capabilities used by this product.");
            Add(spec, "project.uses.package", "Package",
                {Required("Name", ManifestValueKind::Identifier), A("Version", ManifestValueKind::VersionCompatibility),
                 A("Exact", ManifestValueKind::SemanticVersion)},
                {C(usesVersion, 0, 1), C("project.uses.library"), C("project.uses.tool"),
                 C("project.uses.plugin"), C("project.uses.generator"), C("project.uses.analyzer"),
                 C("project.uses.formatter"), C("project.uses.validator"), C("project.uses.action"),
                 C("project.uses.asset"), C("project.uses.option")},
                false, "Package request and typed export selection.", "validate-package-coordinate", "dependency");
            for (const auto &entry : {std::pair{"library", "Library"}, std::pair{"tool", "Tool"},
                                      std::pair{"plugin", "Plugin"}, std::pair{"generator", "Generator"},
                                      std::pair{"analyzer", "Analyzer"}, std::pair{"formatter", "Formatter"},
                                      std::pair{"validator", "Validator"}, std::pair{"action", "Action"},
                                      std::pair{"asset", "Asset"}})
                Add(spec, "project.uses." + std::string(entry.first), entry.second,
                    {Required("Name", ManifestValueKind::Identifier)});
            Add(spec, "project.uses.option", "Option",
                {Required("Name", ManifestValueKind::Identifier), Required("Value")});
            Add(spec, "project.uses.project", "Project", {Required("Path", ManifestValueKind::Path)});
            Add(spec, "project.uses.capability", "Capability",
                {Required("Name", ManifestValueKind::Identifier), A("Version", ManifestValueKind::VersionCompatibility),
                 A("Exact", ManifestValueKind::SemanticVersion), A("Provider", ManifestValueKind::Identifier),
                 A("Domain", ManifestValueKind::Enumeration, false,
                   {"Acquisition", "Build", "Link", "Generation", "Artifact", "Deployment"})},
                {C("project.uses.capability.version", 0, 1)});
            Add(spec, "project.uses.capability.version", "Version",
                {A("AtLeast", ManifestValueKind::SemanticVersion), A("After", ManifestValueKind::SemanticVersion),
                 A("AtMost", ManifestValueKind::SemanticVersion), A("Before", ManifestValueKind::SemanticVersion)});

            Add(spec, "project.generate", "Generate",
                {Required("Using"), A("Version", ManifestValueKind::VersionCompatibility)},
                {C("project.generate.header"), C("project.generate.source"), C("project.generate.file"),
                 C("project.generate.option"), C("project.generate.argument")},
                false, "Selects a package Generator and introduces its host Tool dependency.",
                "validate-action-selection", "action");
            const auto actionInput = std::vector<Attribute>{Required("Include", ManifestValueKind::Path),
                                                            A("Exclude", ManifestValueKind::Path)};
            Add(spec, "project.generate.header", "Header", actionInput);
            Add(spec, "project.generate.source", "Source", actionInput);
            Add(spec, "project.generate.file", "File", actionInput);
            Add(spec, "project.generate.option", "Option",
                {Required("Name", ManifestValueKind::Identifier), Required("Value")});
            Add(spec, "project.generate.argument", "Argument", {}, {}, true);

            Add(spec, "project.tooling", "Tooling", {},
                {C("project.tooling.analyze"), C("project.tooling.format"), C("project.tooling.validate"),
                 C("project.tooling.custom")});
            for (const auto &entry : {std::pair{"analyze", "Analyze"}, std::pair{"format", "Format"},
                                      std::pair{"validate", "Validate"}, std::pair{"custom", "Custom"}})
            {
                Add(spec, "project.tooling." + std::string(entry.first), entry.second, {Required("Using")});
            }

            Add(spec, "project.stage", "Stage", {}, {C("project.stage.file"), C("project.stage.directory")}, false,
                "Project-owned deployment inputs.", "validate-stage-destinations", "stage");
            Add(spec, "project.stage.file", "File",
                {Required("From", ManifestValueKind::Path), Required("To", ManifestValueKind::Path)});
            Add(spec, "project.stage.directory", "Directory",
                {Required("From", ManifestValueKind::Path), Required("To", ManifestValueKind::Path)});

            Add(spec, "project.run", "Run",
                {A("Name", ManifestValueKind::Identifier), A("Default", ManifestValueKind::Boolean),
                 A("WorkingDirectory", ManifestValueKind::Path), A("Using")},
                {C("project.run.argument"), C("project.run.environment"), C("project.run.secret")},
                false, "Executable run definition.", "validate-run", "run");
            Add(spec, "project.run.argument", "Argument", {}, {}, true);
            Add(spec, "project.run.environment", "Environment", {Required("Name"), Required("Value")});
            Add(spec, "project.run.secret", "Secret", {Required("Name"), Required("From")});

            const auto registrationChildren = std::vector<Child>{C("project.registration.argument"),
                                                                  C("project.registration.environment")};
            Add(spec, "project.test", "Test",
                {A("Name", ManifestValueKind::Identifier), A("Timeout", ManifestValueKind::Integer)},
                registrationChildren, false, "Test runner registration attached to an Executable.", {}, "test");
            Add(spec, "project.benchmark", "Benchmark",
                {A("Name", ManifestValueKind::Identifier), A("Timeout", ManifestValueKind::Integer),
                 A("Repetitions", ManifestValueKind::Integer), A("Warmup", ManifestValueKind::Integer)},
                registrationChildren, false, "Benchmark runner registration attached to an Executable.", {},
                "benchmark");
            Add(spec, "project.registration.argument", "Argument", {}, {}, true);
            Add(spec, "project.registration.environment", "Environment", {Required("Name"), Required("Value")});

            Add(spec, "project.publish", "Publish", {},
                {C("project.publish.folder"), C("project.publish.archive"), C("project.publish.installer")}, false,
                "Backend-neutral publish results.", {}, "publish");
            Add(spec, "project.publish.folder", "Folder",
                {Required("Name", ManifestValueKind::Identifier), Required("Output", ManifestValueKind::Path)});
            Add(spec, "project.publish.archive", "Archive",
                {Required("Name", ManifestValueKind::Identifier),
                 Required("Format", ManifestValueKind::Enumeration, {"zip", "tgz"}),
                 Required("Output", ManifestValueKind::Path)});
            Add(spec, "project.publish.installer", "Installer",
                {Required("Name", ManifestValueKind::Identifier),
                 Required("Format", ManifestValueKind::Enumeration, {"msi", "deb"}),
                 Required("Output", ManifestValueKind::Path)});

            Add(spec, "project.when", "When",
                {A("Configuration", ManifestValueKind::Identifier), A("Target", ManifestValueKind::Identifier),
                 A("OS"), A("Architecture"), A("Toolchain", ManifestValueKind::Identifier), A("Compiler"),
                 A("Option", ManifestValueKind::Identifier), A("Equals")},
                {C("project.when.uses", 0, 1), C("project.when.build", 0, 1),
                 C("project.when.stage", 0, 1)},
                false, "Typed additive conditional block.", "validate-when");
            Add(spec, "project.when.uses", "Uses", {},
                {C("project.uses.package"), C("project.uses.project"), C("project.uses.capability")});
            AddBuild(spec, "project.when");
            Add(spec, "project.when.stage", "Stage", {},
                {C("project.when.stage.file"), C("project.when.stage.directory")});
            Add(spec, "project.when.stage.file", "File",
                {Required("From", ManifestValueKind::Path), Required("To", ManifestValueKind::Path)});
            Add(spec, "project.when.stage.directory", "Directory",
                {Required("From", ManifestValueKind::Path), Required("To", ManifestValueKind::Path)});

            const auto commonChildren = std::vector<Child>{C(metadata, 0, 1), C(options, 0, 1),
                                                           C("project.uses", 0, 1), C(build, 0, 1),
                                                           C("project.generate"), C("project.tooling", 0, 1),
                                                           C("project.stage", 0, 1), C("project.publish", 0, 1),
                                                           C("project.when")};
            auto executableChildren = commonChildren;
            executableChildren.push_back(C("project.run"));
            executableChildren.push_back(C("project.test"));
            executableChildren.push_back(C("project.benchmark"));
            Add(spec, "project.executable-root", "Executable",
                {Required("Name", ManifestValueKind::Identifier), A("Version", ManifestValueKind::SemanticVersion)},
                executableChildren, false, "One executable build product.", "validate-executable", "product");
            Add(spec, "project.library-root", "Library",
                {Required("Name", ManifestValueKind::Identifier), A("Version", ManifestValueKind::SemanticVersion),
                 Required("Kind", ManifestValueKind::Enumeration, {"Static", "Shared", "Interface", "Plugin"})},
                commonChildren, false, "One library build product.", "validate-library", "product");
        }

        auto AddPackageSpec(ManifestSpec &spec) -> void
        {
            const auto metadata = AddMetadata(spec, "package", true);
            const auto options = AddOptions(spec, "package");
            const auto requirementPackage = AddPackageReference(spec, "package.requires", true);

            Add(spec, "package.requires", "Uses", {},
                {C(requirementPackage), C("package.requires.project"), C("package.requires.capability"),
                 C("package.requires.option-predicate"), C("package.requires.library"),
                 C("package.requires.tool"), C("package.requires.plugin"), C("package.requires.generator"),
                 C("package.requires.analyzer"), C("package.requires.formatter"),
                 C("package.requires.validator"), C("package.requires.action"), C("package.requires.asset"),
                 C("package.requires.when")}, false, "Requirements activated with their owning export.",
                "validate-requirements");
            Add(spec, "package.requires.project", "Project",
                {Required("Path", ManifestValueKind::Path),
                 A("Public", ManifestValueKind::Boolean)});
            Add(spec, "package.requires.capability", "Capability",
                {Required("Name", ManifestValueKind::Identifier), A("Version", ManifestValueKind::VersionCompatibility),
                 A("Exact", ManifestValueKind::SemanticVersion),
                 A("Domain", ManifestValueKind::Enumeration, false,
                          {"Acquisition", "Build", "Link", "Generation", "Artifact", "Deployment"})},
                {C("package.requires.capability.version", 0, 1)}, false, {}, "validate-capability-requirement");
            Add(spec, "package.requires.capability.version", "Version",
                {A("AtLeast", ManifestValueKind::SemanticVersion), A("After", ManifestValueKind::SemanticVersion),
                 A("AtMost", ManifestValueKind::SemanticVersion), A("Before", ManifestValueKind::SemanticVersion)});
            Add(spec, "package.requires.option-predicate", "Option",
                {Required("Name", ManifestValueKind::Identifier), Required("Value")});
            Add(spec, "package.requires.when", "When",
                {A("Option", ManifestValueKind::Identifier), A("Equals"), A("OS"), A("Architecture"),
                 A("Compiler")},
                {C(requirementPackage), C("package.requires.project"), C("package.requires.capability"),
                 C("package.requires.library"), C("package.requires.tool"), C("package.requires.plugin"),
                 C("package.requires.generator"), C("package.requires.analyzer"),
                 C("package.requires.formatter"), C("package.requires.validator"),
                 C("package.requires.action"), C("package.requires.asset")},
                false, {}, "validate-requirement-condition");

            Add(spec, "package.contributions", "Contributions", {},
                {C("package.contributions.notices", 0, 1), C("package.contributions.runtime-files", 0, 1)});
            Add(spec, "package.contributions.notices", "Notices", {}, {C("package.notice")});
            Add(spec, "package.contributions.runtime-files", "RuntimeFiles", {},
                {C("package.runtime-file"), C("package.runtime-directory")});
            Add(spec, "package.notice", "Notice",
                {Required("From", ManifestValueKind::Path), Required("To", ManifestValueKind::Path)});
            Add(spec, "package.runtime-file", "File",
                {Required("From", ManifestValueKind::Path), Required("To", ManifestValueKind::Path)});
            Add(spec, "package.runtime-directory", "Directory",
                {Required("From", ManifestValueKind::Path), Required("To", ManifestValueKind::Path)});

            const std::vector<Child> exportChildren{
                C("package.requires", 0, 1), C("package.provides"), C("package.export.runtime-files", 0, 1),
                C("package.export.notices", 0, 1)};
            for (const auto &entry : {std::pair{"library", "Library"}, std::pair{"tool", "Tool"},
                                      std::pair{"plugin", "Plugin"}})
            {
                Add(spec, "package.export." + std::string(entry.first), entry.second,
                    {Required("Name", ManifestValueKind::Identifier), A("Default", ManifestValueKind::Boolean),
                     A("Product", ManifestValueKind::Identifier)},
                    exportChildren);
            }
            Add(spec, "package.provides", "Provides",
                {Required("Name", ManifestValueKind::Identifier), Required("Version", ManifestValueKind::VersionCompatibility),
                 A("Domain", ManifestValueKind::Enumeration, false,
                          {"Acquisition", "Build", "Link", "Generation", "Artifact", "Deployment"})});
            Add(spec, "package.export.runtime-files", "RuntimeFiles", {},
                {C("package.runtime-file"), C("package.runtime-directory")});
            Add(spec, "package.export.notices", "Notices", {}, {C("package.notice")});

            const auto actionChildren = std::vector<Child>{C("package.requires", 0, 1), C("package.provides"),
                 C("package.action.inputs", 0, 1), C("package.action.outputs", 0, 1),
                 C("package.action.argument"), C("package.action.working-directory", 0, 1),
                 C("package.action.environment")};
            for (const auto &entry : {std::pair{"generator", "Generator"}, std::pair{"analyzer", "Analyzer"},
                                      std::pair{"formatter", "Formatter"}, std::pair{"validator", "Validator"},
                                      std::pair{"action", "Action"}})
                Add(spec, "package.export." + std::string(entry.first), entry.second,
                    {Required("Name", ManifestValueKind::Identifier), Required("Tool", ManifestValueKind::Identifier),
                     A("Default", ManifestValueKind::Boolean), A("Deterministic", ManifestValueKind::Boolean)},
                    actionChildren, false, {}, "validate-action", "action");
            Add(spec, "package.action.inputs", "Inputs", {},
                {C("package.action.input.header"), C("package.action.input.source"), C("package.action.input.file")});
            const auto actionInput = std::vector<Attribute>{Required("Include", ManifestValueKind::Path),
                                                            A("Exclude", ManifestValueKind::Path)};
            Add(spec, "package.action.input.header", "Header", actionInput);
            Add(spec, "package.action.input.source", "Source", actionInput);
            Add(spec, "package.action.input.file", "File", actionInput);
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
                {Required("From", ManifestValueKind::Path), Required("To", ManifestValueKind::Path)});
            Add(spec, "package.asset.directory", "Directory",
                {Required("From", ManifestValueKind::Path), Required("To", ManifestValueKind::Path)});

            Add(spec, "package.adapters", "Adapters", {},
                {C("cmake.add-subdirectory"), C("cmake.isolated"), C("cmake.find-package"), C("cmake.manual")},
                false, "Registered backend adapter metadata.", "validate-adapter-selection");
            Add(spec, "package.import", "Import", {Required("Cps", ManifestValueKind::Path)});
            Add(spec, "package.capabilities", "Capabilities", {}, {C("package.capabilities.provide")});
            Add(spec, "package.capabilities.provide", "Provide",
                {Required("Name", ManifestValueKind::Identifier),
                 Required("Version", ManifestValueKind::VersionCompatibility),
                 Required("Component"),
                 A("Domain", ManifestValueKind::Enumeration, false,
                   {"Acquisition", "Build", "Link", "Generation", "Artifact", "Deployment"})});
            Add(spec, "package.compatibility", "Compatibility",
                {A("Coexistence", ManifestValueKind::Enumeration, false, {"Context", "SideBySide"})},
                {C("package.compatibility.target"), C("package.compatibility.toolchain")}, false, {},
                "validate-compatibility", "compatibility");
            Add(spec, "package.compatibility.target", "Target", {A("OS"), A("Architecture")});
            Add(spec, "package.compatibility.toolchain", "Toolchain",
                {A("Compiler"), A("RuntimeLibrary"), A("Linker")});
            Add(spec, "package.development", "Development",
                {Required("Project", ManifestValueKind::Path)}, {}, false,
                "Optional local development-project navigation metadata.");

            Add(spec, "package.root", "Package",
                {Required("Name", ManifestValueKind::Identifier),
                 Required("Version", ManifestValueKind::SemanticVersion),
                 A("CompatibleSince", ManifestValueKind::SemanticVersion)},
                {C(metadata, 0, 1), C(options, 0, 1), C("package.requires", 0, 1),
                 C("package.import", 0, 1), C("package.capabilities", 0, 1),
                 C("package.contributions", 0, 1),
                 C("package.export.library"), C("package.export.tool"), C("package.export.plugin"),
                 C("package.export.generator"), C("package.export.analyzer"), C("package.export.formatter"),
                 C("package.export.validator"), C("package.export.action"), C("package.export.asset"),
                 C("package.adapters", 0, 1), C("package.compatibility", 0, 1),
                 C("package.development", 0, 1)},
                false, "One exact package release.", "validate-package", "package");
        }

        auto AddCMakeSpec(ManifestSpec &spec) -> void
        {
            const auto ns = std::string(CMakeIntegrationNamespace);
            const auto commonChildren = std::vector<Child>{C("cmake.cache"), C("cmake.map-option"),
                                                           C("cmake.target"), C("cmake.when")};
            Add(spec, "cmake.add-subdirectory", "AddSubdirectory", {Required("Source", ManifestValueKind::Path)},
                commonChildren, false, "Embeds a source-backed CMake package.", "validate-cmake-add-subdirectory", {}, ns);
            Add(spec, "cmake.manual", "Manual", {Required("Source", ManifestValueKind::Path)}, commonChildren, false,
                "Uses a package-owned CMake wrapper.", "validate-cmake-manual", {}, ns);
            Add(spec, "cmake.find-package", "FindPackage",
                {Required("Name"), A("Config", ManifestValueKind::Boolean), A("Required", ManifestValueKind::Boolean),
                 A("Version", ManifestValueKind::SemanticVersion)},
                {C("cmake.target"), C("cmake.when")}, false, "Maps a resolved package through find_package.",
                "validate-cmake-find-package", {}, ns);
            Add(spec, "cmake.isolated", "Isolated", {Required("Source", ManifestValueKind::Path)},
                {C("cmake.cache"), C("cmake.map-option"), C("cmake.install", 0, 1),
                 C("cmake.find-package", 1, 1), C("cmake.when")},
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
            Add(spec, "cmake.when", "When",
                {A("Configuration"), A("Target"), A("OS"), A("Architecture"), A("Toolchain"), A("Compiler"),
                 A("Option", ManifestValueKind::Identifier), A("Equals")},
                {C("cmake.cache"), C("cmake.map-option"), C("cmake.target")}, false,
                "Adds backend bindings for one typed selection.", "validate-cmake-condition", {}, ns);
        }

        auto AddWorkspaceSpec(ManifestSpec &spec) -> void
        {
            Add(spec, "workspace.discover", "Discover", {},
                {C("workspace.discover.projects"), C("workspace.discover.packages")});
            Add(spec, "workspace.discover.projects", "Projects",
                {Required("Include", ManifestValueKind::Path), A("Exclude", ManifestValueKind::Path),
                 A("System", ManifestValueKind::Enumeration, false, {"Ngin", "CMake"})}, {}, false, {},
                "validate-project-discovery");
            Add(spec, "workspace.discover.packages", "Packages",
                {Required("Include", ManifestValueKind::Path), A("Exclude", ManifestValueKind::Path)}, {}, false, {},
                "validate-package-discovery");

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

            Add(spec, "workspace.versions", "Versions", {}, {C("workspace.versions.package")});
            Add(spec, "workspace.versions.package", "Package",
                {Required("Name", ManifestValueKind::Identifier), A("Version", ManifestValueKind::VersionCompatibility),
                 A("Exact", ManifestValueKind::SemanticVersion), A("AtLeast", ManifestValueKind::SemanticVersion),
                 A("After", ManifestValueKind::SemanticVersion), A("AtMost", ManifestValueKind::SemanticVersion),
                 A("Before", ManifestValueKind::SemanticVersion)}, {}, false, {}, "validate-central-version");

            Add(spec, "workspace.profiles", "Profiles", {A("Default", ManifestValueKind::Identifier)},
                {C("workspace.profile")});
            Add(spec, "workspace.profile", "Profile",
                {Required("Name", ManifestValueKind::Identifier), A("Configuration", ManifestValueKind::Identifier),
                 A("Target", ManifestValueKind::Identifier), A("Toolchain", ManifestValueKind::Identifier),
                 A("Run", ManifestValueKind::Identifier)},
                {C("workspace.profile.option")});
            Add(spec, "workspace.profile.option", "Option", {Required("Name"), Required("Value")});

            Add(spec, "workspace.capabilities", "Capabilities", {},
                {C("workspace.capabilities.prefer"), C("workspace.capabilities.when")});
            Add(spec, "workspace.capabilities.prefer", "Prefer",
                {Required("Name", ManifestValueKind::Identifier), Required("Provider", ManifestValueKind::Identifier)});
            Add(spec, "workspace.capabilities.when", "When",
                {A("OS"), A("Architecture"), A("Configuration", ManifestValueKind::Identifier)},
                {C("workspace.capabilities.prefer")});

            Add(spec, "workspace.trust", "Trust", {}, {C("workspace.trust.allow-actions")});
            Add(spec, "workspace.trust.allow-actions", "AllowActions",
                {Required("From"), Required("Reason")});

            Add(spec, "workspace.root", "Workspace", {Required("Name", ManifestValueKind::Identifier)},
                {C("workspace.discover", 0, 1), C("workspace.configurations", 0, 1),
                 C("workspace.targets", 0, 1), C("workspace.toolchains", 0, 1),
                 C("workspace.versions", 0, 1), C("workspace.profiles", 0, 1),
                 C("workspace.capabilities", 0, 1), C("workspace.trust", 0, 1)},
                false, "Workspace discovery, selection, capability preferences, and trust.",
                "validate-workspace", "workspace");
        }

        [[nodiscard]] auto BuildSpec() -> ManifestSpec
        {
            ManifestSpec spec{};
            AddProjectSpec(spec);
            AddPackageSpec(spec);
            AddCMakeSpec(spec);
            AddWorkspaceSpec(spec);
            spec.documents = {
                {ManifestDocumentKind::Project, ".nginproj",
                 {"project.executable-root", "project.library-root"}, "project.xsd"},
                {ManifestDocumentKind::Package, ".nginpkg", {"package.root"}, "package.xsd"},
                {ManifestDocumentKind::Workspace, ".ngin", {"workspace.root"}, "workspace.xsd"},
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
