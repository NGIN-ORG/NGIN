import { decodeXml, encodeXmlAttribute, scanXmlTags, type XmlTag } from './manifestText';

export interface OffsetEdit {
  start: number;
  end: number;
  text: string;
}

export type BuildItemKind = 'Source' | 'Header' | 'CxxModule' | 'Resource';

function lineStart(source: string, offset: number): number {
  return source.lastIndexOf('\n', Math.max(0, offset - 1)) + 1;
}

function indentation(source: string, offset: number): string {
  return /^\s*/.exec(source.slice(lineStart(source, offset), offset))?.[0] ?? '';
}

function newline(source: string): string {
  return source.includes('\r\n') ? '\r\n' : '\n';
}

function matchingClose(tags: XmlTag[], openingIndex: number): XmlTag | undefined {
  const opening = tags[openingIndex];
  let depth = 0;
  for (let index = openingIndex + 1; index < tags.length; index++) {
    if (tags[index].name !== opening.name) continue;
    if (!tags[index].closing && !tags[index].selfClosing) depth++;
    if (tags[index].closing) {
      if (depth === 0) return tags[index];
      depth--;
    }
  }
  return undefined;
}

function directChildren(source: string, rootIndex: number, tags: XmlTag[]): XmlTag[] {
  const root = tags[rootIndex];
  const close = matchingClose(tags, rootIndex);
  if (!close) return [];
  const result: XmlTag[] = [];
  let depth = 0;
  for (let index = rootIndex + 1; index < tags.length && tags[index].start < close.start; index++) {
    const tag = tags[index];
    if (tag.closing) {
      depth--;
    } else {
      if (depth === 0) result.push(tag);
      if (!tag.selfClosing) depth++;
    }
  }
  return result;
}

function hasExactRule(source: string, kind: BuildItemKind, attribute: 'Include' | 'Remove', value: string): boolean {
  return scanXmlTags(source).some(tag => !tag.closing && tag.name === kind && tag.attributes[attribute] === value);
}

export function insertBuildItem(
  source: string,
  kind: BuildItemKind,
  attribute: 'Include' | 'Remove',
  value: string
): OffsetEdit | undefined {
  if (hasExactRule(source, kind, attribute, value)) return undefined;
  const tags = scanXmlTags(source);
  const rootIndex = tags.findIndex(tag => !tag.closing && (tag.name === 'Executable' || tag.name === 'Library'));
  if (rootIndex < 0) throw new Error('The document does not contain an Executable or Library root element.');
  const root = tags[rootIndex];
  const escaped = encodeXmlAttribute(value);
  const nl = newline(source);
  const children = directChildren(source, rootIndex, tags);
  const build = children.find(tag => tag.name === 'Build');

  if (build) {
    const buildIndex = tags.indexOf(build);
    const itemIndent = `${indentation(source, build.start)}  `;
    if (build.selfClosing) {
      const openText = source.slice(build.start, build.end).replace(/\s*\/>$/, '>');
      const replacement = `${openText}${nl}${itemIndent}<${kind} ${attribute}="${escaped}" />${nl}${indentation(source, build.start)}</Build>`;
      return { start: build.start, end: build.end, text: replacement };
    }
    const close = matchingClose(tags, buildIndex);
    if (!close) throw new Error('The product Build element is not closed.');
    const closeLine = lineStart(source, close.start);
    const closeIsOnOwnLine = source.slice(closeLine, close.start).trim() === '';
    const offset = closeIsOnOwnLine ? closeLine : close.start;
    const prefix = closeIsOnOwnLine ? '' : nl;
    return { start: offset, end: offset, text: `${prefix}${itemIndent}<${kind} ${attribute}="${escaped}" />${nl}` };
  }

  const order = ['Metadata', 'Options', 'Uses', 'Build', 'Generate', 'Tooling', 'Stage', 'Run', 'Test', 'Benchmark', 'Publish', 'When'];
  const later = children.find(tag => order.indexOf(tag.name) > order.indexOf('Build'));
  const rootClose = matchingClose(tags, rootIndex);
  if (!rootClose) throw new Error('The product root element is not closed.');
  const offset = lineStart(source, later?.start ?? rootClose.start);
  const rootIndent = indentation(source, root.start);
  const block = `${rootIndent}  <Build>${nl}${rootIndent}    <${kind} ${attribute}="${escaped}" />${nl}${rootIndent}  </Build>${nl}`;
  return { start: offset, end: offset, text: block };
}

export function updateExactBuildItemPaths(source: string, oldPath: string, newPath: string): OffsetEdit[] {
  const edits: OffsetEdit[] = [];
  const kinds = new Set(['Source', 'Header', 'CxxModule', 'Resource']);
  for (const tag of scanXmlTags(source)) {
    if (tag.closing || !kinds.has(tag.name)) continue;
    const text = source.slice(tag.start, tag.end);
    const expression = /\b(Include|Remove|Update|Exclude)\s*=\s*(["'])(.*?)\2/g;
    for (let match = expression.exec(text); match; match = expression.exec(text)) {
      const decoded = decodeXml(match[3]);
      if (decoded !== oldPath && !decoded.startsWith(`${oldPath}/`)) continue;
      const valueOffset = match.index + match[0].indexOf(match[3]);
      edits.push({
        start: tag.start + valueOffset,
        end: tag.start + valueOffset + match[3].length,
        text: encodeXmlAttribute(newPath + decoded.slice(oldPath.length))
      });
    }
  }
  return edits;
}

export function removeExactBuildItemIncludes(source: string, value: string): OffsetEdit[] {
  const result: OffsetEdit[] = [];
  const kinds = new Set(['Source', 'Header', 'CxxModule', 'Resource']);
  for (const tag of scanXmlTags(source)) {
    if (tag.closing || !tag.selfClosing || !kinds.has(tag.name)
      || (tag.attributes.Include !== value && !tag.attributes.Include?.startsWith(`${value}/`))) continue;
    const start = lineStart(source, tag.start);
    const after = source.indexOf('\n', tag.end);
    result.push({ start, end: after < 0 ? tag.end : after + 1, text: '' });
  }
  return result;
}

export function kindForPath(value: string): BuildItemKind {
  const extension = value.toLowerCase().split('.').pop() ?? '';
  if (['h', 'hh', 'hpp', 'hxx', 'inl'].includes(extension)) return 'Header';
  if (['ixx', 'cppm', 'mpp'].includes(extension)) return 'CxxModule';
  if (['c', 'cc', 'cpp', 'cxx'].includes(extension)) return 'Source';
  return 'Resource';
}

export function updateProjectAttributes(source: string, changes: Record<string, string | undefined>): OffsetEdit[] {
  const root = scanXmlTags(source).find(tag => !tag.closing && (tag.name === 'Executable' || tag.name === 'Library'));
  if (!root) throw new Error('The document does not contain an Executable or Library root element.');
  const text = source.slice(root.start, root.end);
  const edits: OffsetEdit[] = [];
  const additions: string[] = [];
  for (const [name, value] of Object.entries(changes)) {
    const expression = new RegExp(`\\s+${name.replace(/[.*+?^${}()|[\\]\\]/g, '\\$&')}\\s*=\\s*(["'])(.*?)\\1`);
    const match = expression.exec(text);
    if (!match) {
      if (value !== undefined && value !== '') additions.push(` ${name}="${encodeXmlAttribute(value)}"`);
      continue;
    }
    if (value === undefined || value === '') {
      edits.push({ start: root.start + match.index, end: root.start + match.index + match[0].length, text: '' });
      continue;
    }
    const valueOffset = match.index + match[0].indexOf(match[2]);
    edits.push({
      start: root.start + valueOffset,
      end: root.start + valueOffset + match[2].length,
      text: encodeXmlAttribute(value)
    });
  }
  if (additions.length) {
    const insertion = root.end - (text.endsWith('/>') ? 2 : 1);
    edits.push({ start: insertion, end: insertion, text: additions.join('') });
  }
  return edits;
}
