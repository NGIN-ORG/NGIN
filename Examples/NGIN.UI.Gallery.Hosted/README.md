# Hosted NGIN.UI Gallery

This product runs the same public `ComposeMainView()` and model used by the
standalone [`NGIN.UI.Gallery`](../NGIN.UI.Gallery/) through `NGIN.Core`.

`NGIN.UI.Hosting` installs the platform-event-driven host loop and publishes
the UI runtime services. The presentation-stage app module resolves those
services and creates the gallery window; no UI dependency is added to
`NGIN.Core` itself.

Build through the V4 product manifest:

```sh
ngin build \
  --project Examples/NGIN.UI.Gallery.Hosted/NGIN.UI.Gallery.Hosted.nginproj \
  --profile Debug --output build/manual/NGIN.UI.Gallery.Hosted
```

Pass `--smoke` to render three hosted native frames and exit automatically.
