---
title: Contributing documentation
description: Author navigable, verifiable, human-readable, and AI-readable NGIN documentation.
---

# Contributing documentation

Write for the current product and for a reader trying to accomplish something.

## Choose a page type

| Page type | Answers |
| --- | --- |
| Quick start | What is the shortest working path? |
| Guide | How do I complete one task? |
| Concept | How does this part of NGIN work? |
| Reference | What is the exact accepted contract? |
| Troubleshooting | Why did this fail and how do I prove the cause? |

Do not mix a complete reference table into the middle of a first-run tutorial.
Link between page types instead.

## Information architecture

Library documentation follows one stable route:

```text
Libraries → one library → one subsystem → Learn / Guides / C++ API
```

- A library page is a useful map, not a marketing summary.
- A subsystem learning page assumes no prior use and explains the mental model,
  first complete program, expected result, ownership, errors, common mistakes,
  and next steps.
- A guide solves one concrete problem and includes enough setup to reproduce it.
- A C++ reference page stays code-grounded: header, namespace, target,
  declaration, parameters, returns, members, ownership, failure behavior,
  thread-safety, and source link.
- Do not publish a short placeholder as if it were complete documentation. Mark
  an unfinished surface clearly or keep it out of primary navigation.

Use NGIN.Async as the structural example: [learning path](../libraries/base/async.md)
and [symbol reference](../reference/cpp/base/async/index.md).

## Markdown rules

- Use ordinary Markdown unless a visual component materially improves the page.
- Give every page a unique title and one-sentence description in frontmatter.
- Keep headings descriptive and stable.
- Put one complete working example before advanced variations.
- Name the expected result after a command.
- Avoid hiding required information in interactive-only UI.

## Trust

Commands and XML must match the current CLI schema, a focused test, or a
canonical example. Public library examples must compile against current public
headers.

## Local preview

```bash
cd Documentation
npm install
npm run dev
```

Open the exact URL printed by VitePress, normally `http://localhost:5173/`.
The `Documentation` directory name is not part of the default local URL.

To build for a site mounted below the domain root, set `NGIN_DOCS_BASE` to the
public path:

```bash
NGIN_DOCS_BASE=/NGIN/ npm run build
```

## Production check

```bash
npm run build
```

The build checks Markdown links and generates raw pages, `llms.txt`, library
bundles, search data, and the static site.
