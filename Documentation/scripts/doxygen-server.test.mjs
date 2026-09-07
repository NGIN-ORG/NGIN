import assert from "node:assert/strict";
import { test } from "node:test";
import { createServer } from "node:http";
import { mkdtemp, mkdir, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { resolve } from "node:path";
import { doxygenMiddleware } from "./doxygen-server.mjs";

for (const base of ["/", "/NGIN/"]) {
  test(`Doxygen HTML and assets survive regeneration under ${base}`, async (t) => {
    const root = await mkdtemp(resolve(tmpdir(), "ngin-doxygen-server-"));
    const handler = doxygenMiddleware(root, base);
    const server = createServer((request, response) => handler(request, response, (error) => {
      response.writeHead(error ? 500 : 200, { "Content-Type": "text/html" });
      response.end("VitePress app shell");
    }));
    t.after(async () => {
      server.closeAllConnections();
      await new Promise((done) => server.close(done));
      await rm(root, { recursive: true, force: true });
    });
    await new Promise((done) => server.listen(0, "127.0.0.1", done));
    const origin = `http://127.0.0.1:${server.address().port}`;
    const reference = `${origin}${base}reference/doxygen/base/`;

    assert.equal((await fetch(`${reference}index.html`)).status, 404);
    // Files appear after startup, as they do when references are regenerated.
    await mkdir(resolve(root, "base"));
    const page = "<!doctype html><title>NGIN.Base C++ API</title>";
    await writeFile(resolve(root, "base/index.html"), page);
    await writeFile(resolve(root, "base/doxygen-awesome.css"), "html { color: blue; }");
    await writeFile(resolve(root, "base/search.js"), "var searchData = [];");
    let response = await fetch(`${reference}index.html`);
    assert.match(response.headers.get("content-type"), /^text\/html/);
    assert.equal(await response.text(), page);
    response = await fetch(`${reference}doxygen-awesome.css?v=1`);
    assert.match(response.headers.get("content-type"), /^text\/css/);
    assert.equal(await response.text(), "html { color: blue; }");
    response = await fetch(`${reference}search.js`);
    assert.match(response.headers.get("content-type"), /^text\/javascript/);
    assert.equal(await response.text(), "var searchData = [];");
    assert.equal(await (await fetch(reference)).text(), page);
    assert.equal(await (await fetch(`${reference}index.html`, { method: "HEAD" })).text(), "");

    await rm(resolve(root, "base"), { recursive: true });
    assert.equal((await fetch(`${reference}index.html`)).status, 404);
    await mkdir(resolve(root, "base"));
    await writeFile(resolve(root, "base/index.html"), "Regenerated reference");
    assert.equal(await (await fetch(`${reference}index.html`)).text(), "Regenerated reference");
    assert.equal(await (await fetch(`${origin}${base}start`)).text(), "VitePress app shell");
    assert.equal((await fetch(`${reference}%ZZ`)).status, 400);
    assert.equal((await fetch(`${reference}..%2f..%2foutside`)).status, 404);
  });
}
