import assert from "node:assert/strict";
import { existsSync, readFileSync, readdirSync } from "node:fs";
import { posix, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { libraries } from "../doxygen/libraries.mjs";

const root = fileURLToPath(new URL("..", import.meta.url));
const dist = resolve(root, ".vitepress/dist");
assert.ok(!existsSync(resolve(root, "reference/cpp")), "Handwritten C++ reference pages must not duplicate Doxygen.");
assert.ok(!existsSync(resolve(dist, "reference/cpp")), "Stale handwritten C++ reference output remains.");
assert.ok(!existsSync(resolve(dist, "reference/doxygen/index.html")), "A combined Doxygen reference must not remain.");
const htmlCache = new Map();
function checkTarget(url) {
  const [page, anchor] = url.split("#");
  if (!htmlCache.has(page)) {
    htmlCache.set(page, readFileSync(resolve(dist, "reference/doxygen", page), "utf8"));
  }
  if (anchor) {
    const html = htmlCache.get(page);
    assert.ok(html.includes(`id="${anchor}"`) || html.includes(`name="${anchor}"`), `Missing Doxygen anchor: ${url}`);
  }
}

let symbolCount = 0;
for (const { id, name } of libraries) {
  const symbols = JSON.parse(readFileSync(resolve(root, `.vitepress/doxygen/${id}/symbols.json`), "utf8"));
  symbolCount += Object.keys(symbols).length;
  for (const url of Object.values(symbols)) checkTarget(`${id}/${url}`);
  for (const page of ["index.html", "namespaces.html", "files.html", "search/search.js", "doxygen-awesome.css", "LICENSE"]) {
    checkTarget(`${id}/${page}`);
  }
  const tags = readFileSync(resolve(root, `.vitepress/doxygen/${id}/${id}.tag`), "utf8");
  if (/<compound kind="(?:class|struct|union)"/.test(tags)) checkTarget(`${id}/annotated.html`);
  assert.ok(htmlCache.get(`${id}/index.html`).includes(`${name} C++ API`), `Wrong library title in ${id}`);
  for (const file of readdirSync(resolve(dist, `reference/doxygen/${id}/search`))) {
    if (!file.endsWith(".js")) continue;
    const search = readFileSync(resolve(dist, `reference/doxygen/${id}/search`, file), "utf8");
    assert.doesNotMatch(search, /\.\.\/\.\.\/[a-z-]+\//, `${name} search includes another library's symbols`);
    for (const [, url] of search.matchAll(/\['\.\.\/([^']+\.html(?:#[^']*)?)'/g)) {
      checkTarget(`${id}/${url}`);
    }
  }
  // Imported tag files must link to other libraries without merging their symbols.
  if (id !== "base") assert.ok(!symbols["NGIN::Async::Task"], `${name} contains Base's Task reference`);
  if (id !== "core") assert.ok(!symbols["NGIN::Core::ApplicationBuilder"], `${name} contains Core's ApplicationBuilder reference`);
}
let crossLinks = 0;
for (const [page, html] of htmlCache) {
  if (page.endsWith(".html")) {
    assert.match(html, /href="doxygen-awesome\.css"/, `Doxygen Awesome stylesheet missing from ${page}`);
    for (const [, href] of html.matchAll(/href="(\.\.\/[^/]+\/[^"?]+\.html(?:#[^"]+)?)"/g)) {
      if (!libraries.some(({ id }) => id === href.split("/")[1])) continue;
      checkTarget(posix.join(posix.dirname(page), href));
      crossLinks++;
    }
  }
}
assert.ok(crossLinks > 0, "No links between library references were generated.");

let count = 0;
function checkPages(directory) {
  for (const entry of readdirSync(directory, { withFileTypes: true })) {
    const path = resolve(directory, entry.name);
    if (path === resolve(dist, "reference/doxygen")) continue;
    if (entry.isDirectory()) {
      checkPages(path);
    } else if (entry.name.endsWith(".html")) {
      const html = readFileSync(path, "utf8");
      assert.doesNotMatch(html, /href="[^"]*\/reference\/cpp(?:\/|\")/, `Link to removed C++ reference in ${path}`);
      for (const [link] of html.matchAll(/<a\b[^>]*>/g)) {
        const url = link.match(/\bhref="[^"]*\/reference\/doxygen\/([^"]+)"/)?.[1];
        if (!url) continue;
        assert.match(link, /\btarget="_self"/, `Doxygen link must bypass VitePress routing in ${path}: ${link}`);
        checkTarget(url);
        count++;
      }
    }
  }
}
checkPages(dist);
assert.ok(count > 0, "No Doxygen links rendered in the documentation site.");
console.log(`Verified ${libraries.length} separate references, ${symbolCount} symbol targets, ${crossLinks} cross-library links, and ${count} site links.`);
