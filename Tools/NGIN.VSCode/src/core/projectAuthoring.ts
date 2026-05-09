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
  for (const match of xml.matchAll(/<Config\b[^>]*\b(?:Source|Path)=(["'])(.*?)\1[^>]*\/?>/g)) {
    if (normalizeManifestPath(match[2]) === normalizedSource) {
      return { xml, changed: false };
    }
  }

  const existingStage = xml.match(/\n([ \t]*)<\/Stage>/);
  if (existingStage?.index !== undefined) {
    const childIndent = `${existingStage[1]}  `;
    const insert = `\n${childIndent}<Config Source="${normalizedSource}" />`;
    return {
      xml: `${xml.slice(0, existingStage.index)}${insert}${xml.slice(existingStage.index)}`,
      changed: true
    };
  }

  const productClose = xml.match(/\n([ \t]*)<\/(Application|Tool|Plugin|External)>/);
  if (productClose?.index !== undefined) {
    const indent = `${productClose[1]}  `;
    const insert = `${indent}<Stage>\n${indent}  <Config Source="${normalizedSource}" />\n${indent}</Stage>\n`;
    return {
      xml: `${xml.slice(0, productClose.index + 1)}${insert}${xml.slice(productClose.index + 1)}`,
      changed: true
    };
  }

  const projectClose = xml.lastIndexOf('</Project>');
  if (projectClose >= 0) {
    const section = `  <Application>\n    <Stage>\n      <Config Source="${normalizedSource}" />\n    </Stage>\n  </Application>\n`;
    return {
      xml: `${xml.slice(0, projectClose)}${section}${xml.slice(projectClose)}`,
      changed: true
    };
  }

  return {
    xml: `${xml}\n<Application>\n  <Stage>\n    <Config Source="${normalizedSource}" />\n  </Stage>\n</Application>\n`,
    changed: true
  };
}

function replaceConfigTextLines(xml: string, mapper: (source: string) => string | undefined): { xml: string; changed: boolean } {
  let changed = false;
  const updated = xml.replace(/(<Configs\b[^>]*>)([\s\S]*?)(<\/Configs>)/g, (match, open: string, body: string, close: string) => {
    const nextBody = body
      .split(/(\r?\n)/)
      .map((part) => {
        if (/^\r?\n$/.test(part)) {
          return part;
        }
        const trimmed = part.trim();
        if (!trimmed || trimmed.startsWith('<')) {
          return part;
        }
        const replacement = mapper(trimmed);
        if (!replacement) {
          return part;
        }
        changed = true;
        return part.replace(trimmed, replacement);
      })
      .join('');
    return match === `${open}${nextBody}${close}` ? match : `${open}${nextBody}${close}`;
  });
  return { xml: updated, changed };
}

function replaceConfigFileEntries(xml: string, mapper: (source: string) => string | undefined): { xml: string; changed: boolean } {
  let changed = false;
  const updated = xml.replace(/(<(?:File|Config)\b[^>]*\b(?:Path|Source)=)(["'])(.*?)\2([^>]*\/?>)/g, (match, prefix: string, quote: string, source: string, suffix: string) => {
    const replacement = mapper(source);
    if (!replacement) {
      return match;
    }
    changed = true;
    return `${prefix}${quote}${replacement}${quote}${suffix}`;
  });
  return { xml: updated, changed };
}

export function renameConfigInputs(xml: string, fromPath: string, toPath: string, includeChildren = false): { xml: string; changed: boolean } {
  const mapper = (source: string) => remapManifestPath(source, fromPath, toPath, includeChildren);
  const text = replaceConfigTextLines(xml, mapper);
  const files = replaceConfigFileEntries(text.xml, mapper);
  return { xml: files.xml, changed: text.changed || files.changed };
}

export function removeConfigInputs(xml: string, sourcePath: string, includeChildren = false): { xml: string; changed: boolean } {
  let changed = false;
  const normalizedSource = normalizeManifestPath(sourcePath);
  const shouldRemove = (source: string) => {
    const normalizedCandidate = normalizeManifestPath(source);
    return includeChildren
      ? isSameOrChild(normalizedCandidate, normalizedSource)
      : normalizedCandidate === normalizedSource;
  };
  const text = xml.replace(/(<Configs\b[^>]*>)([\s\S]*?)(<\/Configs>)/g, (_match, open: string, body: string, close: string) => {
    const lines = body.split(/(\r?\n)/);
    const next = lines.map((part) => {
      if (/^\r?\n$/.test(part)) {
        return part;
      }
      const trimmed = part.trim();
      if (!trimmed || trimmed.startsWith('<') || !shouldRemove(trimmed)) {
        return part;
      }
      changed = true;
      return '';
    }).join('');
    return `${open}${next}${close}`;
  });
  const files = text.replace(/^([ \t]*<(?:File|Config)\b[^>]*\b(?:Path|Source)=(["'])(.*?)\2[^>]*\/?>\r?\n?)/gm, (match, _line: string, _quote: string, source: string) => {
    if (!shouldRemove(source)) {
      return match;
    }
    changed = true;
    return '';
  });

  return { xml: files, changed };
}

export function listConfigInputs(xml: string): string[] {
  const paths: string[] = [];
  for (const match of xml.matchAll(/<Config\b[^>]*\b(?:Source|Path)=(["'])(.*?)\1[^>]*\/?>/g)) {
    paths.push(match[2]);
  }
  for (const section of xml.matchAll(/<Configs\b[^>]*>([\s\S]*?)<\/Configs>/g)) {
    for (const line of section[1].split(/\r?\n/)) {
      const value = line.trim();
      if (value && !value.startsWith('<')) {
        paths.push(value);
      }
    }
    for (const match of section[1].matchAll(/<File\b[^>]*\bPath=(["'])(.*?)\1[^>]*\/?>/g)) {
      paths.push(match[2]);
    }
  }
  return paths;
}
