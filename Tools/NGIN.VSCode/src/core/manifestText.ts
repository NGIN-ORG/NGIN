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

export function rootIdentity(source: string): { name?: string; type?: string; root?: string } {
  const root = scanXmlTags(source).find(tag => !tag.closing);
  return { name: root?.attributes.Name, type: root?.attributes.Type, root: root?.name };
}

function names(source: string, element: string): string[] {
  return scanXmlTags(source)
    .filter(tag => !tag.closing && tag.name === element && tag.attributes.Name)
    .map(tag => tag.attributes.Name);
}

export function parseWorkspaceChoices(source: string): WorkspaceChoices {
  const identity = rootIdentity(source);
  const tags = scanXmlTags(source);
  const defaultsTag = tags.find(tag => !tag.closing && tag.name === 'Defaults');
  const defaultsBody = defaultsTag
    ? source.slice(defaultsTag.end, tags.find(tag => tag.closing && tag.name === 'Defaults' && tag.start > defaultsTag.end)?.start ?? defaultsTag.end)
    : '';
  const defaultTags = scanXmlTags(defaultsBody);
  const defaultValue = (name: string) => defaultTags.find(tag => !tag.closing && tag.name === name)?.attributes.Name;

  return {
    name: identity.name ?? 'Workspace',
    configurations: [...new Set(names(source, 'Configuration'))],
    targets: [...new Set(names(source, 'Target'))],
    toolchains: [...new Set(names(source, 'Toolchain'))],
    presets: tags
      .filter(tag => !tag.closing && tag.name === 'Preset' && tag.attributes.Name)
      .map(tag => ({ name: tag.attributes.Name, command: tag.attributes.Command })),
    defaults: {
      configuration: defaultValue('Configuration'),
      target: defaultValue('Target'),
      toolchain: defaultValue('Toolchain')
    }
  };
}

export interface ProjectRule {
  path?: string;
  include?: string;
  exclude?: string;
}

export function parseWorkspaceProjectRules(source: string): ProjectRule[] {
  return scanXmlTags(source)
    .filter(tag => !tag.closing && tag.name === 'Project' && (tag.attributes.Path || tag.attributes.Include))
    .map(tag => ({ path: tag.attributes.Path, include: tag.attributes.Include, exclude: tag.attributes.Exclude }));
}

export function relativeManifestPath(projectDirectory: string, candidate: string): string {
  return path.relative(projectDirectory, candidate).split(path.sep).join('/');
}
