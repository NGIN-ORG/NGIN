# NGIN.Core Breaking API Notes

This package is still pre-user and does not preserve legacy runtime APIs.

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
