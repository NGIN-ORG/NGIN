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
- Doxygen generates C++ declarations, member lists, and cross-references from
  public headers. Document parameters, returns, ownership, failures, and
  thread-safety beside the declarations using Doxygen comments.
- Keep C++ reference material in the public headers; do not maintain parallel
  Markdown symbol catalogs. Markdown teaches workflows and explains API usage.
  Link to generated declarations with ordinary Markdown links under
  `/reference/doxygen/<library>/`. Copy the destination from Doxygen; the build validates
  generated file and anchor targets.
- Do not publish a short placeholder as if it were complete documentation. Mark
  an unfinished surface clearly or keep it out of primary navigation.

Use NGIN.Async as the structural example: [learning path](../libraries/base/async.md)
and [Doxygen reference](/reference/doxygen/base/namespaceNGIN_1_1Async.html).

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

Install Doxygen 1.9.8 or newer and Node.js 20.19 or newer. Initialize the
repository submodules so all first-party public headers are available.

```bash
cd Documentation
npm ci
npm run dev
```

Open the exact URL printed by VitePress, normally `http://localhost:5173/`.
The `Documentation` directory name is not part of the default local URL.

Both `npm run dev` and `npm run build` generate Doxygen before starting
VitePress. Each library has its own reference under
`/reference/doxygen/<library>/index.html`, such as `/reference/doxygen/base/index.html`
or `/reference/doxygen/core/index.html`. Choose a library from **C++ API** in the
navigation.
After changing headers during a preview, run `npm run reference` and reload
the reference page. The development server reads the generated HTML and assets
directly from disk, so regeneration does not require a server restart.

The reference uses [doxygen-awesome-css](https://jothepro.github.io/doxygen-awesome-css/)
v2.4.2, pinned as an npm development dependency and installed by `npm ci`.
Doxygen copies its stylesheet and MIT license into the generated site, so the
theme is served locally. Its CSS follows the system light/dark preference.
Keep `HTML_COLORSTYLE = LIGHT` and `FULL_SIDEBAR = NO` in the Doxyfile as
required by the theme.

`doxygen/libraries.mjs` selects one public include directory per library;
`doxygen/Doxyfile` holds shared extraction and theme settings. Each library has
its own search index, class list, and header browser. Generation first collects
tag files and local search indexes, then uses dependency tags to link references
between libraries. It retains the local search indexes so searches stay within
the selected library. Output under `.vitepress/doxygen/` and
`public/reference/doxygen/` is ignored by Git. Header documentation warnings
are retained in `.vitepress/doxygen/<library>/warnings.log`; missing inputs, tools, and
failed generation stop the build. Doxygen includes undocumented declarations
for discovery, so generated coverage does not imply complete prose contracts.

See the [Doxygen configuration manual](https://www.doxygen.nl/manual/config.html)
for the extraction, source browser, and tag-file settings.

To build for a site mounted below the domain root, set `NGIN_DOCS_BASE` to the
public path:

```bash
NGIN_DOCS_BASE=/NGIN/ npm run build
```

## Production check

```bash
npm test
npm run build
```

The tests check generated symbol targets and rendered links. The build checks
Markdown links, Doxygen file and anchor targets, and navigation from the guides.
It generates Doxygen HTML, raw pages, `llms.txt`, library bundles, search data,
and the static site. Doxygen has its own C++ symbol search; the VitePress search
covers Markdown guides and project-system reference pages.

## GitHub Pages deployment

The `Documentation` workflow deploys the tagged documentation to GitHub Pages
when a `v*` tag is pushed. As in the CLI release workflow, the tagged commit must
be in `main`'s history and the tag must equal `v` followed by the version in both
`Tools/NGIN.CLI/NGIN.CLI.nginproj` and the root `CMakeLists.txt`.
Documentation and CLI publishing run independently; a CLI packaging failure
does not prevent documentation deployment.

Before the first deployment:

1. In the repository's **Settings → Pages → Build and deployment**, select
   **GitHub Actions** as the source.
2. In **Settings → Environments → github-pages**, configure the deployment
   branch and tag rules to allow tags matching `v*`. A rule allowing only `main`
   does not permit tag deployments.

See GitHub's [custom Pages workflow documentation](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages)
for the repository setup and environment requirements.

The workflow checks documentation, tests reference links, builds the site with
the base path from the Pages configuration, and uploads
`Documentation/.vitepress/dist` for deployment. The default project URL is
`https://ngin-org.github.io/NGIN/`; each deployment replaces the current site.

Pull requests build and check the site without deploying. To redeploy an existing
release, manually run the `Documentation` workflow with that release tag selected.
A manual run on a branch only checks and builds the documentation.
