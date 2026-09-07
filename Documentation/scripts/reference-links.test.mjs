import assert from "node:assert/strict";
import { test } from "node:test";
import { createMarkdownRenderer, disposeMdItInstance } from "vitepress";
import { referenceLinks, symbolLinks } from "./reference-links.mjs";

test("tag URLs preserve anchors, disambiguate names, and omit private details", () => {
  const links = symbolLinks(`<tagfile>
    <compound kind="class"><name>NGIN::Async::Task</name><filename>task.html</filename>
      <member kind="function"><name>Run</name><anchorfile>task.html</anchorfile><anchor>first</anchor></member>
      <member kind="function"><name>Run</name><anchorfile>task.html</anchorfile><anchor>second</anchor></member>
      <member kind="function"><name>IsStarted</name><anchorfile>task.html</anchorfile><anchor>started</anchor></member>
      <member kind="function" protection="private"><name>Hidden</name><anchorfile>task.html</anchorfile><anchor>hidden</anchor></member>
    </compound>
    <compound kind="class"><name>NGIN::Other::Task</name><filename>other.html</filename></compound>
    <compound kind="class"><name>NGIN::Async::detail::Internal</name><filename>internal.html</filename></compound>
    <compound kind="struct"><name>NGIN::Box&lt; void &gt;</name><filename>void.html</filename></compound>
  </tagfile>`);
  assert.equal(links["NGIN::Async::Task"], "task.html");
  assert.equal(links["NGIN::Async::Task::Run"], "task.html");
  assert.equal(links.IsStarted, "task.html#started");
  assert.equal(links.Task, undefined);
  assert.equal(links.Hidden, undefined);
  assert.equal(links.Internal, undefined);
  assert.equal(links["NGIN::Box< void >"], "void.html");
});

for (const base of ["/", "/NGIN/"]) {
  test(`Doxygen links use full-page navigation under ${base}`, async () => {
    disposeMdItInstance();
    const md = await createMarkdownRenderer(process.cwd(), {
      config(md) { md.use(referenceLinks, { base }); }
    }, base);
    const html = md.render(
      '[`Task`](/reference/doxygen/base/task.html#member) [Guide](./guide.md) `Task`',
      { relativePath: "libraries/base/async.md" }
    );
    assert.ok(html.includes(`href="${base}reference/doxygen/base/task.html#member" target="_self"`));
    assert.match(html, /href="\.\/guide.html">Guide<\/a>/);
    assert.match(html, /<\/a> <code>Task<\/code>/);
  });
}
