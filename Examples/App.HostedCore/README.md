# App.HostedCore

`App.HostedCore` demonstrates the optional `NGIN.Core` hosted application
runtime.

The project is still built and staged by NGIN tooling, but the executable does
not load its source `.nginproj` at runtime. Startup is code-first:

- services are registered through `ApplicationBuilder`
- config is loaded from staged `config/app.cfg`
- a static runtime module is registered and enabled in code

This is the recommended shape for apps that want the hosted runtime model while
remaining shippable as a normal native application layout.

