# Hosted NGIN.UI Gallery

The same Gallery screens as the [standalone application](../NGIN.UI.Gallery),
hosted through `NGIN.Core` and `NGIN.UI.Hosting`.

```bash
ngin build --project Examples/NGIN.UI.Gallery.Hosted/NGIN.UI.Gallery.Hosted.nginproj --profile Debug --output build/manual/NGIN.UI.Gallery.Hosted
ngin run --project Examples/NGIN.UI.Gallery.Hosted/NGIN.UI.Gallery.Hosted.nginproj --profile Debug --output build/manual/NGIN.UI.Gallery.Hosted
```

Pass `--smoke` after `--` to open, draw a few frames, and exit automatically.
Use this example to compare standalone UI composition with hosted lifecycle and
service integration.
