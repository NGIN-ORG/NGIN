# Settings panel

The same component tree runs in a standalone desktop window and over an animated
scene. Edit the profile name, adjust master volume, and choose Apply to update the
preview. Drag the divider or focus it and use the arrow keys to resize the panes.
The meter implements custom measurement, painting, and accessibility.

```bash
ngin run --project Examples/NGIN.UI.Settings/NGIN.UI.Settings.nginproj
ngin run --project Examples/NGIN.UI.Settings/NGIN.UI.Settings.nginproj -- --game
```

In the game host, Escape toggles the menu. Space pauses or resumes the scene
while the game owns input. The host clears the scene, submits the retained UI
packet to its own SDL renderer, and presents once. Pass `--smoke` to either host
for a bounded run.

[SettingsPanel.cpp](src/SettingsPanel.cpp) contains the complete view and model.
[GameHost.cpp](src/GameHost.cpp) owns the external frame loop.
