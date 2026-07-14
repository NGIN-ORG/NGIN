# NGIN.Core Breaking API Notes

This package is still pre-user and does not preserve legacy runtime APIs.

Project profiles no longer expose or parse `BuildType`. Profiles now carry
`Optimization`, `DebugSymbols`, and `LinkTimeOptimization` traits, while
selector-bearing runtime declarations use `Toolchain`. The built-in profile
conditions `Debug`, `Release`, `RelWithDebInfo`, and `MinSizeRel` match profile
names.

The preferred hosted-app entry point is now `ApplicationBuilder` with direct
methods such as `AddDefaultServices()`, `AddConfiguration()`,
`AddModule<T>()`, `AddConfigSource()`, and `AddPluginSearchPath()`.

Advanced collection APIs for services, packages, modules, plugins, and
configuration remain available for package/bootstrap scenarios, but quickstart
examples should use the direct builder methods.

Dynamic plugin loading now requires descriptors to provide a `Library`
attribute. Plugin libraries export a registrar, defaulting to
`NGIN_RegisterPlugin`, that registers module factories through
`IPluginModuleRegistry`.

`ModuleDescriptor::capabilities` and `ModuleOptions::capabilities` now contain
`ModuleCapability` values instead of strings. Set `exclusive=true` for roles
that permit only one active provider.

`ModuleContext` now receives the resolved descriptor and exposes module origin
metadata through `Descriptor()`, `ModuleRoot()`, `DescriptorPath()`,
`LibraryPath()`, `PluginName()`, and `IsDynamicModule()`. Static modules can set
`ModuleOptions::moduleRoot`; dynamic module roots come from descriptor
discovery.
