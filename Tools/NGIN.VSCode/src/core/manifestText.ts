import * as path from 'node:path';
import type { WorkspaceChoices } from '../model';

export interface XmlTag {
  name: string;
  attributes: Record<string, string>;
  start: number;
  end: number;
  closing: boolean;
  selfClosing: boolean;
}

export function decodeXml(value: string): string {
  return value
    .replace(/&quot;/g, '"')
    .replace(/&apos;/g, "'")
    .replace(/&lt;/g, '<')
    .replace(/&gt;/g, '>')
    .replace(/&amp;/g, '&');
}

export function encodeXmlAttribute(value: string): string {
  return value
    .replace(/&/g, '&amp;')
    .replace(/"/g, '&quot;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');
}

export function parseAttributes(source: string): Record<string, string> {
  const attributes: Record<string, string> = {};
  const expression = /([A-Za-z_][\w:.-]*)\s*=\s*("([^"]*)"|'([^']*)')/g;
  for (let match = expression.exec(source); match; match = expression.exec(source)) {
    attributes[match[1]] = decodeXml(match[3] ?? match[4] ?? '');
  }
  return attributes;
}

export function scanXmlTags(source: string): XmlTag[] {
  const tags: XmlTag[] = [];
  const expression = /<(?!\?|!--|!\[CDATA\[|!DOCTYPE)(\/)?([A-Za-z_][\w:.-]*)([^<>]*?)(\/?)>/gs;
  for (let match = expression.exec(source); match; match = expression.exec(source)) {
    tags.push({
      name: match[2],
      attributes: parseAttributes(match[3]),
      start: match.index,
      end: expression.lastIndex,
      closing: Boolean(match[1]),
      selfClosing: Boolean(match[4])
    });
  }
  return tags;
}

export function rootIdentity(source: string): {
  name?: string;
  artifactKind?: 'Executable' | 'Library';
  libraryKind?: string;
  root?: string;
} {
  const root = scanXmlTags(source).find(tag => !tag.closing);
  const artifactKind = root?.name === 'Executable' || root?.name === 'Library' ? root.name : undefined;
  return { name: root?.attributes.Name, artifactKind, libraryKind: root?.attributes.Kind, root: root?.name };
}

function names(source: string, element: string): string[] {
  return scanXmlTags(source)
    .filter(tag => !tag.closing && tag.name === element && tag.attributes.Name)
    .map(tag => tag.attributes.Name);
}

export function parseWorkspaceChoices(source: string): WorkspaceChoices {
  const identity = rootIdentity(source);
  const tags = scanXmlTags(source);
  const profilesTag = tags.find(tag => !tag.closing && tag.name === 'Profiles');
  const defaultProfile = profilesTag?.attributes.Default;
  const profileTags = tags.filter(tag => !tag.closing && tag.name === 'Profile' && tag.attributes.Name);
  const selectedProfile = profileTags.find(tag => tag.attributes.Name === defaultProfile);

  return {
    name: identity.name ?? 'Workspace',
    configurations: [...new Set(['Debug', 'Release', ...names(source, 'Configuration')])],
    targets: [...new Set(['host', ...names(source, 'Target')])],
    toolchains: [...new Set(['auto', ...names(source, 'Toolchain')])],
    profiles: profileTags.map(tag => ({
      name: tag.attributes.Name,
      configuration: tag.attributes.Configuration,
      target: tag.attributes.Target,
      toolchain: tag.attributes.Toolchain,
      run: tag.attributes.Run
    })),
    defaults: {
      configuration: selectedProfile?.attributes.Configuration ?? 'Debug',
      target: selectedProfile?.attributes.Target ?? 'host',
      toolchain: selectedProfile?.attributes.Toolchain ?? 'auto'
    }
  };
}

export interface ProjectRule {
  include?: string;
  exclude?: string;
}

export function parseWorkspaceProjectRules(source: string): ProjectRule[] {
  return scanXmlTags(source)
    .filter(tag => !tag.closing && tag.name === 'Projects' && tag.attributes.Include)
    .map(tag => ({ include: tag.attributes.Include, exclude: tag.attributes.Exclude }));
}

export function relativeManifestPath(projectDirectory: string, candidate: string): string {
  return path.relative(projectDirectory, candidate).split(path.sep).join('/');
}
