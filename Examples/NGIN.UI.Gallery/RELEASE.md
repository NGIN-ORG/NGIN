# NGIN.UI Gallery 0.4.0

This folder is a runnable NGIN.UI demo. It does not need the NGIN source tree.

Run the executable from this folder so it can find its fonts, image, shared
libraries, and notices:

- Windows: `bin/NGIN.UI.Gallery.exe`
- Linux and macOS: `bin/NGIN.UI.Gallery`

Use `--page "Control templates"` to browse field recordings as cards or rows,
`--page "Property Editor"` to try the mixer, or `--page Layout` to compare sizing strategies. Use `--smoke` to draw every page and exit.

The `licenses` folder contains the NGIN license, bundled-font licenses, image
decoder notice, SDL license, and third-party runtime notices. Color emoji are
not supported in version 0.4; the Typography page explains the monochrome
symbol policy.


The interface uses Inter from the Google Fonts revision
`6f2d0cd65e61cb237409c0802757a1e55481422d`. Inter is distributed under
the SIL Open Font License, included at `licenses/NGIN.UI.Gallery/Inter-OFL.txt`.
