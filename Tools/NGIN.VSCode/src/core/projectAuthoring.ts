import * as path from 'node:path';

function toManifestPath(filePath: string): string {
  return filePath.split(path.sep).join('/');
}

function normalizeManifestPath(filePath: string): string {
  return toManifestPath(path.normalize(filePath)).replace(/^\.\//, '');
}

function isSameOrChild(candidate: string, parent: string): boolean {
  return candidate === parent || candidate.startsWith(`${parent}/`);
}

function remapManifestPath(candidate: string, fromPath: string, toPath: string, includeChildren: boolean): string | undefined {
  const normalizedCandidate = normalizeManifestPath(candidate);
  const normalizedFrom = normalizeManifestPath(fromPath);
  const normalizedTo = normalizeManifestPath(toPath);
  if (normalizedCandidate === normalizedFrom) {
    return normalizedTo;
  }
  if (!includeChildren || !isSameOrChild(normalizedCandidate, normalizedFrom)) {
    return undefined;
  }
  return `${normalizedTo}${normalizedCandidate.slice(normalizedFrom.length)}`;
}

export function relativeManifestPath(projectDirectory: string, filePath: string): string {
  return normalizeManifestPath(path.relative(projectDirectory, filePath));
}

export function addRootConfigInput(xml: string, sourcePath: string): { xml: string; changed: boolean } {
  const normalizedSource = normalizeManifestPath(sourcePath);
  const configPattern = /<Config\b[^>]*\bPath=(["'])(.*?)\1[^>]*\/?>/g;
  for (const match of xml.matchAll(configPattern)) {
    if (normalizeManifestPath(match[2]) === normalizedSource) {
      return { xml, changed: false };
    }
  }

  const configLine = `<Config Path="${normalizedSource}" />`;
  const existingSection = xml.match(/\n([ \t]*)<\/Inputs>/);
  if (existingSection?.index !== undefined) {
    const childIndent = `${existingSection[1]}  `;
    const insert = `\n${childIndent}${configLine}`;
    return {
      xml: `${xml.slice(0, existingSection.index)}${insert}${xml.slice(existingSection.index)}`,
      changed: true
    };
  }

  const section = `  <Inputs>\n    ${configLine}\n  </Inputs>\n`;
  const insertionPoint = xml.search(/\n\s*<(Runtime|Environments|Profiles)\b/);
  if (insertionPoint >= 0) {
    return {
      xml: `${xml.slice(0, insertionPoint + 1)}${section}${xml.slice(insertionPoint + 1)}`,
      changed: true
    };
  }

  const projectClose = xml.lastIndexOf('</Project>');
  if (projectClose >= 0) {
    return {
      xml: `${xml.slice(0, projectClose)}${section}${xml.slice(projectClose)}`,
      changed: true
    };
  }

  return {
    xml: `${xml}\n${section}`,
    changed: true
  };
}

export function renameConfigInputs(xml: string, fromPath: string, toPath: string, includeChildren = false): { xml: string; changed: boolean } {
  let changed = false;
  const updated = xml.replace(/(<Config\b[^>]*\bPath=)(["'])(.*?)\2([^>]*\/?>)/g, (match, prefix: string, quote: string, source: string, suffix: string) => {
    const replacement = remapManifestPath(source, fromPath, toPath, includeChildren);
    if (!replacement) {
      return match;
    }
    changed = true;
    return `${prefix}${quote}${replacement}${quote}${suffix}`;
  });

  return { xml: updated, changed };
}

export function removeConfigInputs(xml: string, sourcePath: string, includeChildren = false): { xml: string; changed: boolean } {
  let changed = false;
  const normalizedSource = normalizeManifestPath(sourcePath);
  const updated = xml.replace(/^([ \t]*<Config\b[^>]*\bPath=(["'])(.*?)\2[^>]*\/?>\r?\n?)/gm, (match, line: string, _quote: string, source: string) => {
    const normalizedCandidate = normalizeManifestPath(source);
    const shouldRemove = includeChildren
      ? isSameOrChild(normalizedCandidate, normalizedSource)
      : normalizedCandidate === normalizedSource;
    if (!shouldRemove) {
      return match;
    }
    changed = true;
    return '';
  });

  return { xml: updated, changed };
}
