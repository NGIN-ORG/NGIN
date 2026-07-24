# NGIN.UI Hosting

`NGIN.UI.Hosting` is the optional bridge between standalone `NGIN.UI` and the
`NGIN.Core` application host. It depends on the UI and Core contracts but not
on SDL or another concrete backend.

`ConfigureUIHosting()`:

- creates a hosted UI application and native text system from injected backend
  instances;
- installs an event-driven `IHostRunLoop`;
- registers the `NGIN.UI.Runtime` platform-stage module;
- publishes application, window-manager, dispatcher, platform-backend, and
  render-backend service references;
- drains posted UI work on the UI thread and wakes the platform wait whenever
  work or a Core stop request arrives.

Worker tasks can post completion work through `IUIDispatcher::Post()`. The
dispatcher swaps its pending queue before invoking callbacks, so callbacks that
post more work are deferred to the next UI iteration instead of recursively
running.

The concrete backend remains application-selected:

```cpp
auto registration = NGIN::UI::Hosting::ConfigureUIHosting(
    *builder,
    {
        .application = {
            .platform = NGIN::UI::SDL3::CreatePlatformBackend(),
            .renderer = NGIN::UI::SDL3::CreateRendererBackend(),
        },
    });
```
