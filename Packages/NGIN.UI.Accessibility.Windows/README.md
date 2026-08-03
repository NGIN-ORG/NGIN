# NGIN.UI accessibility for Windows

This package exposes the `NGIN.UI` semantic tree through Windows UI Automation.
It is optional and selected only for Windows desktop applications.

Current package version: `0.4.0`.

The provider translates immutable accessibility snapshots and routes supported
actions back to the UI thread. Password values remain protected by the core
semantic contract.

See [Windows accessibility](../../docs/guides/ngin-ui-windows-accessibility.md)
for setup, supported patterns, diagnostics, and testing.
