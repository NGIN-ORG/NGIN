# NGIN.UI with NGIN.Core

The hosted path uses the same `Composer` view and controls as a standalone
application. `NGIN.UI.Hosting` owns the UI application, installs a Core host
run loop, publishes UI services, and provides a thread-safe posting boundary.

The complete buildable reference is
[`NGIN.UI.Gallery.Hosted`](../../Examples/NGIN.UI.Gallery.Hosted/). It shares
[`ComposeMainView()`](../../Examples/NGIN.UI.Gallery/src/Gallery.cpp) with the
standalone gallery.

## Product manifest

The application product declares the Core runtime, hosting bridge, and concrete
backend:

```xml
<Project SchemaVersion="4"
         Name="Hello.UI.Hosted"
         DefaultProfile="Debug">
  <Application>
    <Uses>
      <Runtime Name="NGIN.Core"
               Version=">=0.1.0 &lt;0.2.0"
               Scope="Target;Runtime" />
      <Package Name="NGIN.UI.Hosting"
               Version=">=0.3.0 &lt;0.4.0"
               Scope="Target;Runtime" />
      <Package Name="NGIN.UI.Backend.SDL3"
               Version=">=0.3.0 &lt;0.4.0"
               Scope="Target">
        <Feature Name="RuntimeNotices" />
      </Package>
      <Package Name="NGIN.UI"
               Version=">=0.3.0 &lt;0.4.0"
               Scope="Target">
        <Feature Name="RuntimeAssets" />
      </Package>
    </Uses>
    <Build>
      <Sources Path="src/**.cpp" />
    </Build>
    <Runtime>
      <Module Name="Hello.UI.Presentation" Stage="Presentation">
        <Requires Service="NGIN.UI.IApplication" />
        <Requires Service="NGIN.UI.IUIDispatcher" />
      </Module>
    </Runtime>
    <Launch Executable="Hello.UI.Hosted" WorkingDirectory="." />
  </Application>
</Project>
```

The service requirements make startup order explicit in the resolved
Composition Graph.

## Presentation module

Resolve the hosted runtime in `OnStart()` and create content on the UI thread:

```cpp
#include <NGIN/UI/Hosting/Hosting.hpp>

class PresentationModule final : public NGIN::Core::IModule {
public:
  auto OnStart(NGIN::Core::ModuleContext &context) noexcept
      -> NGIN::Core::CoreResult<void> override {
    using namespace NGIN::UI::Hosting;

    auto runtime = context.Services().ResolveRequired<HostedUIRuntime>(
        UIApplicationServiceName);
    if (!runtime) {
      return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
          runtime.Error());
    }
    auto dispatcher = context.Services().ResolveRequired<IUIDispatcher>(
        UIDispatcherServiceName);
    if (!dispatcher) {
      return NGIN::Utilities::Unexpected<NGIN::Core::KernelError>(
          dispatcher.Error());
    }

    m_runtime = runtime.Value();
    m_dispatcher = dispatcher.Value();
    auto window = m_runtime->UI().CreateWindow(NGIN::UI::WindowCreateInfo{
        .id = NGIN::Text::String{"main"},
        .title = NGIN::Text::String{"Hosted NGIN.UI"},
    });
    if (!window) {
      return NGIN::Utilities::Unexpected(
          NGIN::Core::MakeKernelError(
              NGIN::Core::KernelErrorCode::InternalError,
              "Hello.UI.Hosted", "CreateWindow",
              window.Error().message.CStr()));
    }

    auto *text = &m_runtime->Text();
    window.Value()->SetContent([text](NGIN::UI::Composer &composer) {
      NGIN::UI::NodeProperties label{};
      label.text.fontSize = 24.0F;
      label.text.geometry = text;
      composer.Text(NGIN::Text::String{"Hello from NGIN.Core"}, *text, *text,
                    label, "heading");
    });
    return {};
  }

private:
  NGIN::Memory::Shared<NGIN::UI::Hosting::HostedUIRuntime> m_runtime{};
  NGIN::Memory::Shared<NGIN::UI::Hosting::IUIDispatcher> m_dispatcher{};
};
```

The gallery's
[`main.cpp`](../../Examples/NGIN.UI.Gallery.Hosted/src/main.cpp) shows the full
builder registration, module registration, stop flow, smoke traversal, and
structured Core/UI error conversion.

## Register hosting

During host construction, select the concrete backends:

```cpp
auto registration = NGIN::UI::Hosting::ConfigureUIHosting(
    *builder,
    {
        .application = {
            .platform = NGIN::UI::SDL3::CreatePlatformBackend(),
            .renderer = NGIN::UI::SDL3::CreateRendererBackend(),
            .applicationName = NGIN::Text::String{"Hosted NGIN.UI"},
        },
    });
```

Keep the returned `UIHostingRegistration` alive with the host. It owns the
runtime, dispatcher, and UI host run loop.

## Worker completions

UI objects are thread-affine. A worker may compute ordinary data, then post the
state mutation:

```cpp
auto posted = dispatcher->Post([model, window, result = std::move(result)]() {
  model->Apply(std::move(result));
  window->Invalidate(NGIN::UI::InvalidationKind::All);
});
```

`Post()` is safe off the UI thread and wakes the host run loop. Posted work is
drained between platform iterations. A callback that posts another callback
defers it to the next drain instead of recursing.

Do not call `Composer`, `Window`, `Application`, render backends, text services,
or `ImageTextureCache` from a worker. Use
`IUIDispatcher::IsCurrentThread()` for assertions at integration boundaries.

## Build and run

```powershell
build/dev/Tools/NGIN.CLI/ngin.exe build `
  --project Examples/NGIN.UI.Gallery.Hosted/NGIN.UI.Gallery.Hosted.nginproj `
  --profile Debug `
  --output build/manual/NGIN.UI.Gallery.Hosted

build/manual/NGIN.UI.Gallery.Hosted/bin/NGIN.UI.Gallery.Hosted.exe
```

The same view can be tested without Core using the standalone or headless
gallery. That is the recommended split: keep view composition independent and
put service resolution, worker scheduling, and shutdown coordination in the
presentation module.
