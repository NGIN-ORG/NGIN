{
  "cps_version": "0.15.0",
  "name": "Portable",
  "version": "1.2.0",
  "cps_path": "@prefix@",
  "default_components": ["Core"],
  "components": {
    "Core": {
      "type": "interface",
      "includes": ["include"]
    },
    "Inspector": {
      "type": "executable",
      "location": "bin/portable-inspector"
    },
    "Extension": {
      "type": "module",
      "location": "lib/portable-extension.so"
    }
  }
}
