# Manifest model migration ledger

Status: Normative removal map for the pre-release flag-day migration

This ledger maps every public construct reported by the current CLI schema and
the repository's authored manifests to the new authoring model. It is not a
compatibility specification. The implementation must delete the old grammar;
old spellings remain only in negative tests proving rejection.

## Migration policy

- There is no dual parser, schema negotiation, or legacy fallback.
- Conversion helpers may exist only on development branches and are removed
  before Milestone 11 completes.
- Positive examples and fixtures use only the new grammar.
- Old XML in negative fixtures is named clearly and never used as input to a
  successful command.
- Semantic behavior is preserved only where this ledger names a replacement.
  Rows marked Removed are intentional behavior changes.

## Project roots and products

| Old construct | New construct | Disposition |
| --- | --- | --- |
| `Project@SchemaVersion` | none | Removed; authored manifest has no format number before official release |
| `Project@DefaultProfile` | Workspace default or Preset | Removed from semantic Project identity |
| `<Application>...</Application>` | `<Project Type="Application">` direct sections | Wrapper removed |
| `<Library>...</Library>` | `<Project Type="Library">` direct sections | Wrapper removed |
| `<Tool>...</Tool>` product wrapper | `<Project Type="Tool">` direct sections | Wrapper removed; package Tool export remains distinct |
| `<Test>...</Test>` product wrapper | `<Project Type="Test">` direct sections | Wrapper removed |
| `<Benchmark>...</Benchmark>` product wrapper | `<Project Type="Benchmark">` direct sections | Wrapper removed |
| `<Plugin>...</Plugin>` product wrapper | `<Project Type="Plugin">` direct sections | Wrapper removed |
| `<Module>...</Module>` product wrapper | `Library`, `Plugin`, or `CxxModule` build item | Generic Module product removed |
| `<External>...</External>` | `<Project Type="External">` direct sections | Wrapper removed |
| Root-level normalized product sections/fallback | valid direct section for selected Type | Old permissive fallback deleted |

## Profiles, conditions, and selection

| Old construct | New construct | Disposition |
| --- | --- | --- |
| Project/Workspace `<Profiles>` | `Configurations`, `Targets`, `Toolchains`, typed `Options`, and `Presets` | Split by semantic responsibility |
| `<Profile Name>` inheritance/overlay | pre-resolution Preset plus structured `Refinement` | Profile object removed |
| `DefaultProfile` | Workspace Defaults or command `--preset` | Profile name never enters graph identity |
| `Profile="..."` selector | `<Select><Configuration .../></Select>` or Preset input | Converted to concrete facet |
| `OperatingSystem` selector | structured Target selector | Preserved as Target fact |
| `Architecture` selector | structured Target selector | Preserved as Target fact |
| `Environment` selector | Launch/Stage/Publish definition, or typed Option for real compile difference | Removed as build dimension |
| `<Conditions>` / named `<Condition>` | structured `Refinement/Select` or typed Option predicate | General/nested expression model removed |
| `When="condition-name"` | placement inside matching Refinement or package structured `When` | Named arbitrary condition removed |
| Generic Variant dimension | typed project/package Option | Removed |
| XML order as overlay precedence | category merge law and specificity | Removed |

## Dependencies

| Old construct | New construct | Disposition |
| --- | --- | --- |
| `<Uses>` | `<Dependencies>` or package `<Requires>` | Renamed by semantic ownership |
| `<Project Name Path Scope>` | `<Project Name Path>` in relevant dependency context | Scope string removed |
| `<Package Name Version Scope>` | `<Package Name Exact/Compatible>` or child `Version` | Structured version and context |
| `<Tool ... Scope="Build">` | selected Action -> Tool export host edge, or explicit Tool export use | Edge role inferred |
| `<Runtime ...>` dependency kind | Library/Plugin export activation plus automatic RuntimeFiles | Runtime composition kind removed |
| `<Test>` dependency kind | dependency inside Testing or root dependency of Type=Test | Context supplies role |
| `Scope="Build;Target;Runtime;Test;Dev;Publish"` | typed export plus containing context | Semicolon scope removed |
| `Optional="true"` | typed Option/Capability/Asset/Action activation | Generic optionality removed |
| `Link="true|false"` | selected export kind and requirements | Generic link trait removed |
| `Version=">=a <b"` or encoded equivalent | `Compatible`, `Exact`, or `<Version AtLeast Before>` | Comparison text removed |
| Dependency nested `<Feature>` | explicit export `Use`, package Option, Action, Asset, or Capability | Feature removed |

## Build and item operations

| Old construct | New construct | Disposition |
| --- | --- | --- |
| `<Sources Path>` | `<Source Include>` | Singular typed item |
| `<Headers Path Visibility>` | `<Header Include Visibility>` | Singular typed item |
| `<Language ...>` | direct `Build/Language` | Preserved with strict types |
| `<Optimization Mode>` | Configuration default/refinement scalar | Category-specific merge |
| `<DebugSymbols Enabled>` | Configuration default/refinement scalar | Category-specific merge |
| `<LinkTimeOptimization Enabled>` | Configuration/refinement scalar | Category-specific merge |
| `<IncludePath Path>` | `<IncludeDirectory Path>` | Renamed |
| `<Define Name Value>` | `<Define Name Value Visibility>` | Preserved, stable keyed identity |
| `<CompileOption Value>` | same singular element | Strict repeated item |
| `<LinkOption Value>` | same singular element | Strict repeated item |
| `<LinkLibrary>` | package/project Library export dependency | Raw link item allowed only as CMake integration/private external artifact mapping where justified |
| `<PrecompiledHeader>` | singular typed build item | Preserved |
| `<UnityBuild>` | singleton typed build item | Preserved |
| Generic `Remove` overlays | `Include`, `Exclude`, `Remove`, `Update` on typed build items | Explicit item algebra |
| Implicit source fallback | named, explainable file conventions | Permissive fallback removed |
| C++ module product | `<CxxModule Include Kind Visibility>` | Language module becomes build item |

## Generation and tooling

| Old construct | New construct | Disposition |
| --- | --- | --- |
| Package `<Generator>` | package `<Action Kind="Generate">` plus Tool export | Unified execution model |
| Project generator declaration | `<Generate Action="Package::Action">` | Typed project verb |
| Package `<ToolActions>` | package Action exports | Unified |
| Package `<ToolDrivers>` | Tool export/integration protocol metadata | Kept only where semantic tool protocol requires it |
| Project Analyzer/Formatter feature | `<Tooling><Analyze/Format Action=...>` | Feature wrapper removed |
| Tool run/profile overlay | explicit Action invocation/refinement | Generic overlay removed |
| Package presence causing tooling | explicit Action selection | Implicit execution prohibited |

## Runtime, staging, and launch

| Old construct | New construct | Disposition |
| --- | --- | --- |
| Project `<Runtime><Module>` | C++/NGIN.Core application composition | Removed from manifests |
| Project runtime Module `Stage` | C++ runtime framework lifecycle | Removed |
| Module `<Requires Service>` / `<Provides Service>` | C++ service/module APIs | Removed |
| Runtime `<Setting>` | typed Launch environment, staged config file, or application C++ configuration | No generic runtime setting bag |
| Runtime `<Plugin>` activation/order | Plugin export plus stage contribution; loading remains runtime-owned | Composition removed |
| Generic Host/Module/Before/After | C++/runtime framework | Removed from core and extensions |
| `<Stage><Config Source>` | `<Stage><File Include Into>` | Typed project-owned staged file |
| `<Stage><Content Source Target>` | `File` or `Directory` with `Include`/`Into` | Typed staging item |
| Package feature RuntimeNotices | package-level required Notice contribution | Automatic when any export active |
| Package feature RuntimeAssets | export RuntimeFiles or optional Asset export | Tied to activation unit |
| `Launch@Executable` | `<Executable Product>` or Tool export child | Structured launch |
| `Launch@WorkingDirectory` | `<WorkingDirectory Path>` | Structured launch |
| Encoded launch arguments | repeated `<Argument>` | No list encoding |
| `<Environment><Env/LaunchEnv>` | typed Launch/Action Environment entries | Context-owned |
| `<Environment><Secret>` value | external secret reference only | Secret content prohibited |

## Product execution and publishing

| Old construct | New construct | Disposition |
| --- | --- | --- |
| Tool `<Run>` | named `Launch` or Action depending intent | Process versus semantic action separated |
| Test `<Run>`, `<Report>`, `<TestSettings>` | Type=Test root execution fields / TestPlan | Direct product semantics |
| Benchmark `<Run>`, `<Report>`, `<BenchmarkSettings>` | Type=Benchmark root execution fields / TestPlan or benchmark plan projection | Direct product semantics |
| `<Publish Kind Format Output ...>` attributes | named Publish with `Folder`, `Archive`, or `Installer` child | Typed output family |
| CPack values in authored Publish | semantic publisher fields | CPack remains adapter-private |
| `<PackageOutput>` | Publish developer/package output | Unified publish intent |
| Project `<Exports>` | product Type plus package output metadata | Project has one product identity; package manifest owns reusable export vocabulary |

## Package manifest

| Old construct | New construct | Disposition |
| --- | --- | --- |
| `Package@SchemaVersion` | none | Removed |
| `CompatiblePlatformRange` | structured Compatibility facts or package requirements | Old platform schema-version coupling removed |
| Generic `<Build Backend="CMake" Mode="...">` | `<Integrations><cmake:AddSubdirectory/Isolated/FindPackage/Manual>` | Backend made explicit |
| `Build@CMakePackage` | `cmake:FindPackage@Name` | CMake namespace |
| Build semicolon `Linkage` / `RuntimeArtifacts` | typed integration/artifact children | Encoded lists removed |
| `<Build><Options><Option>` | `cmake:Cache` or `cmake:MapOption` | Backend inputs separated from public semantic Options |
| Package `<Uses>` | package-level/export-level `<Requires>` | Activation-aware |
| `<Library><Exports><LibraryTarget>` | semantic Library export plus `cmake:Target` binding | CMake name leaves semantics |
| Package `<Tool>` | Tool export | Preserved with host PackageInstance semantics |
| `<Features><Feature>` | Library/Tool/Plugin/Action/Asset export, public Option, Capability, or contribution | Generic Feature deleted |
| Feature dependencies/settings/generator/runtime/tooling | typed concept at correct package/export level | Provenance retained without feature bag |
| `<Provides><Capability Name>` | versioned Capability implementation on active export | Version required |
| Compatibility OS/architecture lists | repeated structured `Compatibility/Target` | Normalized |

## Workspace manifest

| Old construct | New construct | Disposition |
| --- | --- | --- |
| `Workspace@SchemaVersion` | none | Removed |
| `Workspace@DefaultProfile` | Defaults and/or Preset | Profile removed |
| `PlatformVersion` | package/product Versions and capability Versions | Global platform schema coupling removed |
| `<Imports>` definition fragments | explicit manifest discovery/import mechanism only if ratified separately | Implicit semantic fragment merge removed |
| `<Defaults><Backend Name="CMake">` | installed/selected CMake adapter configuration outside semantic graph | CMake remains sole initial adapter |
| `<HostPlatform>` / `<TargetPlatform>` | Defaults Target plus host discovery | Structured contexts |
| `<Platforms>` | `<Targets>` aliases | Renamed/structured |
| `<Toolchains>` | strict Toolchain definitions | Preserved with ABI/runtime ownership |
| Workspace `<Profiles>` product overlays | Configurations, Options, Refinements, Presets | Removed |
| `<Packages><Source>` | PackageProvider Source | Preserved with normalized result boundary |
| `<Version Name Range>` | central `Version` with Exact/Compatible/structured child | Constraint intersection |
| `<PackageProvider Name Root>` | `LocalPackage` or Package `Binding` to a Source | Acquisition separated from package name |
| External Vcpkg/Conan provider | PackageProvider Source/Binding; CMake inputs in CMakeIntegrationBindings | Existing behavior migrated, not expanded |

## Feature-specific repository mapping

| Current repository feature | Replacement |
| --- | --- |
| `NGIN.UI.Backend.SDL3:RuntimeNotices` | package-level required Notice contribution |
| `NGIN.UI:RuntimeAssets` | export RuntimeFiles for required assets and named Asset for optional example data |
| `NGIN.Core:Reflection` | public Boolean Option plus option-dependent package requirement/CMake mapping/versioned capability if semantically needed |
| `NGIN.Reflection.MetaGen:ReflectionCodegen` | Tool `MetaGen` plus Action `ReflectionCodegen` Kind Generate |
| `NGIN.Tooling.ClangTidy:Analyzer` | Tool plus Action Kind Analyze |
| `NGIN.Tooling.ClangFormat:Formatter` | Tool plus Action Kind Format |
| TLS/Crypto provider features | versioned Capability implementations on the exact active Library export |

## Composition Graph and command migration

| Old behavior | New behavior |
| --- | --- |
| `ResolvedLaunch` carries authored project/profile/package/features | immutable semantic Composition Graph plus separate bindings/plans |
| Graph `packageFeatures` | active exports, Options, CapabilityBindings, contributions |
| Graph `runtime` service/module plan | removed; runtime composition stays in C++ |
| Graph/backend CMake target data | CMakeIntegrationBindings and BuildPlan only |
| One graph object contains command work details | Build/Action/Stage/Launch/Test/Publish plans derived from graph |
| Package lock stores broad composition inputs | dependency lock stores acquired PackageInstances; graph supplies composition fingerprint |
| Command rereads project/package XML | prohibited after graph construction |
| `explain feature/runtime-module` | explain export/option/capability-binding/plugin-contribution |

## Removal verification

The migration is not complete until repository search and negative tests prove:

- no positive authored manifest contains `SchemaVersion`;
- no Project contains Application/Library/Tool/Test/Benchmark/Plugin/Module
  wrappers;
- no manifest contains `Feature`, semicolon `Scope`, generic `Optional`, Profile,
  named Condition, Host, runtime Module, service requirement/provision, or
  startup phase;
- no ordinary version constraint contains an escaped comparison operator;
- no semantic package/graph type contains CMake names or opaque bindings;
- no production parser branch accepts any removed construct;
- old constructs appear only in named rejection fixtures and historical docs
  explicitly marked superseded.
