export type ProjectFeatureState = 'inherit' | 'use' | 'disable';
export type ProjectInputBlock = 'Sources' | 'Headers' | 'Configs';

export interface ProjectAttributeUpdate {
  name?: string;
  template?: string;
  defaultProfile?: string;
}

export interface ProjectProfileUpdate {
  originalName: string;
  name: string;
  template?: string;
  buildType?: string;
  platform?: string;
  operatingSystem?: string;
  architecture?: string;
  environment?: string;
  launchExecutable?: string;
  launchWorkingDirectory?: string;
}

export interface ProjectPackageReferenceEdit {
  name: string;
  version?: string;
  optional?: boolean;
}

export interface ProjectInputEdit {
  mode: 'File' | 'Directory' | 'Glob';
  path?: string;
  include?: string;
  exclude?: string;
  profile?: string;
  platform?: string;
  operatingSystem?: string;
  architecture?: string;
  buildType?: string;
  environment?: string;
  condition?: string;
}

export interface ProjectEnvironmentVariableEdit {
  name: string;
  value?: string;
  fromEnvironment?: string;
  fromLocalSetting?: string;
  required?: boolean;
  secret?: boolean;
}

function escapeAttribute(value: string): string {
  return value
    .replace(/&/g, '&amp;')
    .replace(/"/g, '&quot;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');
}

function attrValue(value: string | boolean | undefined): string | undefined {
  if (value === undefined || value === '') {
    return undefined;
  }
  return typeof value === 'boolean' ? (value ? 'true' : 'false') : value;
}

function setAttribute(tag: string, name: string, value: string | boolean | undefined): string {
  const normalized = attrValue(value);
  const pattern = new RegExp(`\\s${name}=(["'])(.*?)\\1`);
  if (normalized === undefined) {
    return tag.replace(pattern, '');
  }
  const replacement = ` ${name}="${escapeAttribute(normalized)}"`;
  if (pattern.test(tag)) {
    return tag.replace(pattern, replacement);
  }
  return tag.replace(/\s*\/?>$/, (suffix) => `${replacement}${suffix}`);
}

function setAttributes(tag: string, attributes: Record<string, string | boolean | undefined>): string {
  let next = tag;
  for (const [name, value] of Object.entries(attributes)) {
    next = setAttribute(next, name, value);
  }
  return next;
}

function readAttribute(tag: string, name: string): string | undefined {
  const match = tag.match(new RegExp(`\\s${name}=(["'])(.*?)\\1`));
  return match?.[2];
}

function indentationBefore(xml: string, index: number): string {
  const lineStart = xml.lastIndexOf('\n', index - 1) + 1;
  return xml.slice(lineStart, index).match(/^\s*/)?.[0] ?? '';
}

function childIndent(parentIndent: string): string {
  return `${parentIndent}  `;
}

function projectCloseIndex(xml: string): number {
  const close = xml.lastIndexOf('</Project>');
  if (close < 0) {
    throw new Error('Project manifest is missing </Project>.');
  }
  return close;
}

function projectOpenMatch(xml: string): RegExpMatchArray {
  const match = xml.match(/<Project\b[^>]*>/);
  if (!match || match.index === undefined) {
    throw new Error('Project manifest is missing <Project>.');
  }
  return match;
}

function directChildDepth(xml: string): number {
  const scrubbed = xml
    .replace(/<!--[\s\S]*?-->/g, '')
    .replace(/<\?[\s\S]*?\?>/g, '');
  const tagPattern = /<\/?([A-Za-z_][\w.-]*)\b[^>]*>/g;
  let depth = 0;
  for (const match of scrubbed.matchAll(tagPattern)) {
    const tag = match[0];
    if (tag.startsWith('</')) {
      depth = Math.max(0, depth - 1);
    } else if (!tag.endsWith('/>')) {
      depth += 1;
    }
  }
  return depth;
}

function findDirectSection(xml: string, rangeStart: number, rangeEnd: number, name: string): { start: number; end: number; openEnd: number; bodyStart: number; bodyEnd: number; indent: string } | undefined {
  const source = xml.slice(rangeStart, rangeEnd);
  const pattern = new RegExp(`<${name}\\b[^>]*>`, 'g');
  for (const match of source.matchAll(pattern)) {
    if (match.index === undefined) {
      continue;
    }
    if (directChildDepth(source.slice(0, match.index)) !== 0) {
      continue;
    }
    const start = rangeStart + match.index;
    const openEnd = start + match[0].length;
    const closeText = `</${name}>`;
    const bodyEnd = xml.indexOf(closeText, openEnd);
    if (bodyEnd < 0 || bodyEnd > rangeEnd) {
      continue;
    }
    return {
      start,
      end: bodyEnd + closeText.length,
      openEnd,
      bodyStart: openEnd,
      bodyEnd,
      indent: indentationBefore(xml, start)
    };
  }
  return undefined;
}

function profileNamePattern(name: string): RegExp {
  const escaped = name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  return new RegExp(`<Profile\\b(?=[^>]*\\sName=(["'])${escaped}\\1)[^>]*(?:/>|>)`, 'g');
}

function findProfile(xml: string, name: string): { start: number; end: number; openEnd: number; bodyStart: number; bodyEnd: number; openTag: string; selfClosing: boolean; indent: string } | undefined {
  const profiles = findDirectSection(xml, projectOpenMatch(xml).index! + projectOpenMatch(xml)[0].length, projectCloseIndex(xml), 'Profiles');
  if (!profiles) {
    return undefined;
  }

  const source = xml.slice(profiles.bodyStart, profiles.bodyEnd);
  for (const match of source.matchAll(profileNamePattern(name))) {
    if (match.index === undefined) {
      continue;
    }
    const start = profiles.bodyStart + match.index;
    const openTag = match[0];
    const openEnd = start + openTag.length;
    const selfClosing = /\/>$/.test(openTag);
    if (selfClosing) {
      return {
        start,
        end: openEnd,
        openEnd,
        bodyStart: openEnd,
        bodyEnd: openEnd,
        openTag,
        selfClosing,
        indent: indentationBefore(xml, start)
      };
    }
    const close = xml.indexOf('</Profile>', openEnd);
    if (close < 0 || close > profiles.bodyEnd) {
      continue;
    }
    return {
      start,
      end: close + '</Profile>'.length,
      openEnd,
      bodyStart: openEnd,
      bodyEnd: close,
      openTag,
      selfClosing,
      indent: indentationBefore(xml, start)
    };
  }
  return undefined;
}

function ensureProfilesSection(xml: string): { xml: string; section: ReturnType<typeof findDirectSection> } {
  const open = projectOpenMatch(xml);
  const rootStart = open.index! + open[0].length;
  const rootEnd = projectCloseIndex(xml);
  const existing = findDirectSection(xml, rootStart, rootEnd, 'Profiles');
  if (existing) {
    return { xml, section: existing };
  }

  const projectIndent = indentationBefore(xml, projectCloseIndex(xml));
  const insert = `${projectIndent}<Profiles>\n${projectIndent}</Profiles>\n`;
  const next = `${xml.slice(0, rootEnd)}${insert}${xml.slice(rootEnd)}`;
  return { xml: next, section: findDirectSection(next, rootStart, projectCloseIndex(next), 'Profiles') };
}

function replaceRange(xml: string, start: number, end: number, replacement: string): string {
  return `${xml.slice(0, start)}${replacement}${xml.slice(end)}`;
}

function scopeRange(xml: string, profileName?: string): { xml: string; start: number; end: number; indent: string; closeInsertIndex: number } {
  if (!profileName) {
    const open = projectOpenMatch(xml);
    const start = open.index! + open[0].length;
    const end = projectCloseIndex(xml);
    return { xml, start, end, indent: '  ', closeInsertIndex: end };
  }

  const profile = findProfile(xml, profileName);
  if (!profile) {
    throw new Error(`Profile not found: ${profileName}`);
  }

  if (!profile.selfClosing) {
    return { xml, start: profile.bodyStart, end: profile.bodyEnd, indent: childIndent(profile.indent), closeInsertIndex: profile.bodyEnd };
  }

  const open = profile.openTag.replace(/\s*\/>$/, '>');
  const replacement = `${open}\n${profile.indent}</Profile>`;
  const next = replaceRange(xml, profile.start, profile.end, replacement);
  const expanded = findProfile(next, profileName);
  if (!expanded) {
    throw new Error(`Profile not found after expansion: ${profileName}`);
  }
  return { xml: next, start: expanded.bodyStart, end: expanded.bodyEnd, indent: childIndent(expanded.indent), closeInsertIndex: expanded.bodyEnd };
}

function ensureScopeSection(xml: string, sectionName: string, profileName?: string): { xml: string; section: NonNullable<ReturnType<typeof findDirectSection>> } {
  const range = scopeRange(xml, profileName);
  const existing = findDirectSection(range.xml, range.start, range.end, sectionName);
  if (existing) {
    return { xml: range.xml, section: existing };
  }
  const insert = `\n${range.indent}<${sectionName}>\n${range.indent}</${sectionName}>`;
  const next = replaceRange(range.xml, range.closeInsertIndex, range.closeInsertIndex, insert);
  const nextRange = scopeRange(next, profileName);
  const section = findDirectSection(next, nextRange.start, nextRange.end, sectionName);
  if (!section) {
    throw new Error(`Failed to create <${sectionName}> section.`);
  }
  return { xml: next, section };
}

function formatPackageReference(reference: ProjectPackageReferenceEdit, indent: string, tagName = 'Package'): string {
  let tag = `<${tagName}`;
  tag = setAttributes(tag + ' />', {
    Name: reference.name,
    Version: reference.version,
    Optional: reference.optional
  });
  return `${indent}${tag}`;
}

function formatInput(block: ProjectInputBlock, entry: ProjectInputEdit, indent: string): string {
  const selectorAttributes = {
    Profile: entry.profile,
    Platform: entry.platform,
    OperatingSystem: entry.operatingSystem,
    Architecture: entry.architecture,
    BuildType: entry.buildType,
    Environment: entry.environment,
    Condition: entry.condition
  };
  if (entry.mode === 'Directory' && entry.path) {
    const tag = setAttributes(`<${block} />`, {
      Path: entry.path,
      Include: entry.include,
      Exclude: entry.exclude,
      ...selectorAttributes
    });
    return `${indent}${tag}`;
  }
  if (entry.mode === 'File' && entry.path) {
    const tag = setAttributes(`<${block}>`, selectorAttributes);
    return `${indent}${tag}\n${childIndent(indent)}${escapeAttribute(entry.path)}\n${indent}</${block}>`;
  }
  let tag = `<${block}`;
  tag = setAttributes(tag + ' />', {
    Include: entry.include,
    Exclude: entry.exclude,
    ...selectorAttributes
  });
  return `${indent}${tag}`;
}

export function updateProjectAttributes(xml: string, update: ProjectAttributeUpdate): string {
  const match = projectOpenMatch(xml);
  const nextTag = setAttributes(match[0], {
    Name: update.name,
    Template: update.template,
    DefaultProfile: update.defaultProfile
  });
  return replaceRange(xml, match.index!, match.index! + match[0].length, nextTag);
}

export function addProfile(xml: string, name: string): string {
  if (!name.trim()) {
    throw new Error('Profile name is required.');
  }
  if (findProfile(xml, name)) {
    throw new Error(`Profile already exists: ${name}`);
  }
  const ensured = ensureProfilesSection(xml);
  const indent = childIndent(ensured.section.indent);
  const insert = `${indent}<Profile Name="${escapeAttribute(name)}" />\n`;
  return replaceRange(ensured.xml, ensured.section.bodyEnd, ensured.section.bodyEnd, insert);
}

export function deleteProfile(xml: string, name: string): string {
  const profile = findProfile(xml, name);
  if (!profile) {
    return xml;
  }
  let start = profile.start;
  let end = profile.end;
  if (xml[end] === '\n') {
    end += 1;
  } else if (xml[start - 1] === '\n') {
    start -= 1;
  }
  let next = replaceRange(xml, start, end, '');
  const project = projectOpenMatch(next);
  if (readAttribute(project[0], 'DefaultProfile') === name) {
    const firstProfile = next.match(/<Profile\b[^>]*\sName=(["'])(.*?)\1/);
    next = updateProjectAttributes(next, { defaultProfile: firstProfile?.[2] });
  }
  return next;
}

export function updateProfile(xml: string, update: ProjectProfileUpdate): string {
  if (!update.name?.trim()) {
    throw new Error('Profile name is required.');
  }
  const profile = findProfile(xml, update.originalName);
  if (!profile) {
    throw new Error(`Profile not found: ${update.originalName}`);
  }
  let openTag = setAttributes(profile.openTag, {
    Name: update.name,
    Template: update.template,
    BuildType: update.buildType,
    Platform: update.platform,
    OperatingSystem: update.operatingSystem,
    Architecture: update.architecture,
    Environment: update.environment
  });
  let next = replaceRange(xml, profile.start, profile.openEnd, openTag);
  next = setLaunch(next, update.name, update.launchExecutable, update.launchWorkingDirectory);
  const root = projectOpenMatch(next);
  if (readAttribute(root[0], 'DefaultProfile') === update.originalName) {
    next = updateProjectAttributes(next, { defaultProfile: update.name });
  }
  return next;
}

export function setLaunch(xml: string, profileName: string | undefined, executable?: string, workingDirectory?: string): string {
  const range = scopeRange(xml, profileName);
  const existing = findDirectSection(range.xml, range.start, range.end, 'Launch');
  const hasValues = Boolean(executable || workingDirectory);
  if (!hasValues) {
    return existing ? replaceRange(range.xml, existing.start, existing.end + (range.xml[existing.end] === '\n' ? 1 : 0), '') : range.xml;
  }
  const indent = existing?.indent ?? range.indent;
  const tag = setAttributes('<Launch />', {
    Executable: executable,
    WorkingDirectory: workingDirectory
  });
  const replacement = `${indent}${tag}`;
  if (existing) {
    return replaceRange(range.xml, existing.start, existing.end, replacement);
  }
  return replaceRange(range.xml, range.closeInsertIndex, range.closeInsertIndex, `\n${replacement}`);
}

export function setPackageReferences(xml: string, references: ProjectPackageReferenceEdit[], profileName?: string): string {
  const ensured = ensureScopeSection(xml, 'References', profileName);
  const indent = childIndent(ensured.section.indent);
  const body = references.map((reference) => formatPackageReference(reference, indent)).join('\n');
  const replacement = body ? `\n${body}\n${ensured.section.indent}` : '';
  return replaceRange(ensured.xml, ensured.section.bodyStart, ensured.section.bodyEnd, replacement);
}

export function setInputEntries(xml: string, block: ProjectInputBlock, entries: ProjectInputEdit[], profileName?: string): string {
  const range = scopeRange(xml, profileName);
  let next = range.xml;
  const inputs = findDirectSection(next, range.start, range.end, 'Inputs');
  if (!inputs && entries.length === 0) {
    return next;
  }
  const ensured = ensureScopeSection(next, 'Inputs', profileName);
  next = ensured.xml;
  const section = findDirectSection(next, ensured.section.bodyStart, ensured.section.bodyEnd, block);
  if (section) {
    let start = section.start;
    let end = section.end;
    if (next[end] === '\n') {
      end += 1;
    }
    next = replaceRange(next, start, end, '');
  }
  if (entries.length === 0) {
    return next;
  }
  const refreshed = ensureScopeSection(next, 'Inputs', profileName);
  const indent = childIndent(refreshed.section.indent);
  const body = entries.map((entry) => formatInput(block, entry, indent)).join('\n');
  return replaceRange(refreshed.xml, refreshed.section.bodyEnd, refreshed.section.bodyEnd, `${body}\n`);
}

export function setProfileFeatureState(xml: string, profileName: string, packageName: string, featureName: string, state: ProjectFeatureState): string {
  if (!findProfile(xml, profileName)) {
    if (state === 'inherit') {
      return xml;
    }
    xml = addProfile(xml, profileName);
  }
  const ensured = ensureScopeSection(xml, 'Features', profileName);
  const entryPattern = new RegExp(`^[ \\t]*<(Use|Disable)\\b(?=[^>]*\\sPackage=(["'])${packageName.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}\\2)(?=[^>]*\\sFeature=(["'])${featureName.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}\\3)[^>]*/>\\r?\\n?`, 'gm');
  let next = replaceRange(
    ensured.xml,
    ensured.section.bodyStart,
    ensured.section.bodyEnd,
    ensured.xml.slice(ensured.section.bodyStart, ensured.section.bodyEnd).replace(entryPattern, '')
  );
  if (state === 'inherit') {
    return next;
  }
  const refreshed = ensureScopeSection(next, 'Features', profileName);
  const indent = childIndent(refreshed.section.indent);
  const tagName = state === 'use' ? 'Use' : 'Disable';
  const tag = setAttributes(`<${tagName} />`, {
    Package: packageName,
    Feature: featureName
  });
  return replaceRange(refreshed.xml, refreshed.section.bodyEnd, refreshed.section.bodyEnd, `${indent}${tag}\n`);
}

export function setEnvironmentVariables(xml: string, environmentName: string, variables: ProjectEnvironmentVariableEdit[]): string {
  if (!environmentName.trim()) {
    throw new Error('Environment name is required.');
  }
  const open = projectOpenMatch(xml);
  const rootStart = open.index! + open[0].length;
  const rootEnd = projectCloseIndex(xml);
  let next = xml;
  let environments = findDirectSection(next, rootStart, rootEnd, 'Environments');
  if (!environments) {
    const indent = '  ';
    const insert = `${indent}<Environments>\n${indent}</Environments>\n`;
    next = replaceRange(next, rootEnd, rootEnd, insert);
    environments = findDirectSection(next, rootStart, projectCloseIndex(next), 'Environments');
  }
  if (!environments) {
    throw new Error('Failed to create <Environments>.');
  }

  const escapedName = environmentName.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const envPattern = new RegExp(`<Environment\\b(?=[^>]*\\sName=(["'])${escapedName}\\1)[^>]*(?:/>|>)`, 'g');
  const source = next.slice(environments.bodyStart, environments.bodyEnd);
  let envStart = -1;
  let envEnd = -1;
  let envIndent = childIndent(environments.indent);
  for (const match of source.matchAll(envPattern)) {
    if (match.index === undefined) {
      continue;
    }
    envStart = environments.bodyStart + match.index;
    envIndent = indentationBefore(next, envStart);
    if (/\/>$/.test(match[0])) {
      envEnd = envStart + match[0].length;
    } else {
      const close = next.indexOf('</Environment>', envStart + match[0].length);
      envEnd = close + '</Environment>'.length;
    }
    break;
  }

  const variableIndent = `${envIndent}    `;
  const variableLines = variables.map((variable) => {
    const tag = setAttributes('<Variable />', {
      Name: variable.name,
      Value: variable.value,
      FromEnvironment: variable.fromEnvironment,
      FromLocalSetting: variable.fromLocalSetting,
      Required: variable.required === undefined ? undefined : variable.required,
      Secret: variable.secret === undefined ? undefined : variable.secret
    });
    return `${variableIndent}${tag}`;
  }).join('\n');
  const replacement = `${envIndent}<Environment Name="${escapeAttribute(environmentName)}">\n${envIndent}  <Variables>${variableLines ? `\n${variableLines}\n${envIndent}  ` : ''}</Variables>\n${envIndent}</Environment>`;

  if (envStart >= 0 && envEnd >= envStart) {
    return replaceRange(next, envStart, envEnd, replacement);
  }
  return replaceRange(next, environments.bodyEnd, environments.bodyEnd, `${replacement}\n`);
}
