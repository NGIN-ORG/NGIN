# Hosted NGIN.UI Gallery

This app shows the same screens as the
[standalone gallery](../NGIN.UI.Gallery/) inside an `NGIN.Core` application.
The controls should look and behave the same in both apps.

Build through the V4 product manifest:

```sh
ngin build \
  --project Examples/NGIN.UI.Gallery.Hosted/NGIN.UI.Gallery.Hosted.nginproj \
  --profile Debug --output build/manual/NGIN.UI.Gallery.Hosted
```

Pass `--smoke` to open, draw a few frames, and exit automatically.

The hosted and standalone products use the same searchable navigation, page
examples, controls, and model. A `Release` profile is available for product
verification; the standalone product owns the versioned demo archive.
