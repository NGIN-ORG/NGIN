// Doxygen's tag file supplies URLs; never guess its generated filenames/anchors.
function xmlText(value) {
  return value.replace(/&(lt|gt|quot|apos|amp);/g, (_, entity) =>
    ({ lt: "<", gt: ">", quot: '"', apos: "'", amp: "&" })[entity]);
}

function field(body, name) {
  return xmlText(body.match(new RegExp(`<${name}\\b[^>]*>([\\s\\S]*?)</${name}>`))?.[1] ?? "");
}

export function symbolLinks(tagFile) {
  const candidates = new Map();
  function add(name, url) {
    if (!name || !url || /(^|::)(detail|Detail)(::|$)/.test(name)) return;
    const urls = candidates.get(name) ?? new Set();
    urls.add(url);
    candidates.set(name, urls);
  }

  for (const match of tagFile.matchAll(/<compound kind="([^"]+)"[^>]*>([\s\S]*?)<\/compound>/g)) {
    const [, kind, body] = match;
    if (!["class", "struct", "namespace", "concept", "union"].includes(kind)) continue;
    const name = field(body, "name");
    if (!name.startsWith("NGIN") || /(^|::)(detail|Detail)(::|$)/.test(name)) continue;
    const filename = field(body, "filename");
    add(name, filename);
    add(name.split("::").at(-1), filename);
    for (const member of body.matchAll(/<member\b([^>]*)>([\s\S]*?)<\/member>/g)) {
      if (/protection="(private|protected)"/.test(member[1])) continue;
      const memberName = field(member[2], "name");
      const anchorFile = field(member[2], "anchorfile");
      const anchor = field(member[2], "anchor");
      if (!anchorFile || !anchor) continue;
      const url = `${anchorFile}#${anchor}`;
      add(`${name}::${memberName}`, url);
      add(memberName, url);
    }
  }

  // Ambiguous names stay plain text. Overloads in one compound link to its page.
  return Object.fromEntries([...candidates].flatMap(([name, urls]) => {
    if (urls.size === 1) return [[name, [...urls][0]]];
    const pages = new Set([...urls].map((url) => url.split("#")[0]));
    return pages.size === 1 ? [[name, [...pages][0]]] : [];
  }));
}

export function referenceLinks(md, { base }) {
  md.core.ruler.after("inline", "ngin-reference-links", (state) => {
    for (const block of state.tokens) {
      for (const token of block.children ?? []) {
        if (token.type !== "link_open") continue;
        const href = token.attrGet("href");
        if (!href?.startsWith("/reference/doxygen/")) continue;
        token.attrSet("href", `${base}${href.slice(1)}`);
        // Doxygen is a separate HTML application, so bypass VitePress routing.
        token.attrSet("target", "_self");
      }
    }
  });
}
