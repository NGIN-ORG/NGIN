# Documentation style

Write for the current product, not the history of how it was designed.

## Choose one home

| Information | Source of truth |
| --- | --- |
| Manifest syntax | CLI schema and one reference page |
| CLI commands | `ngin` command output and CLI reference |
| Library API | Public headers and generated API docs |
| Tutorials | Guides backed by runnable examples |
| Design decisions | Short architecture decision records |
| Work in progress | GitHub issues and projects |
| Release changes | Release notes |
| Superseded designs | Git history |

## Voice

- Lead with what the reader can do.
- Prefer short sentences and concrete names.
- Show one working example before listing options.
- State experimental limits directly.
- Avoid marketing claims that the repository cannot verify.
- Do not present internal schema generations as product branding.

## Trust

Commands and XML snippets should match a runnable example or a focused test.
Use `ngin schema --format json` and the CLI command registry when updating
reference material. Link to one authoritative page instead of repeating the
same contract in several READMEs.

Delete resolved plans, reviews, drafts, and superseded contracts after their
lasting result has moved into code, tests, reference material, or an
architecture decision. Git retains their history.

Run the documentation guard before submitting a broad documentation change:

```powershell
./Tools/scripts/check-docs.ps1
```

It checks relative links and rejects retired NGIN version terminology outside
release and migration material.
