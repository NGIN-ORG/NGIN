# Dependencies

`Dependencies/` contains source trees that are not actively owned by the umbrella repo.

Layout:

- `Dependencies/NGIN/`: first-party `NGIN.*` repos developed externally
- `Dependencies/ThirdParty/`: third-party source dependencies when source checkout is needed

The umbrella repo consumes these trees through package wrappers in `Packages/`.
They are source availability, not the NGIN-facing package contract.
