export type ProjectFeatureState = 'inherit' | 'use' | 'disable';
export type ProjectInputBlock = 'Sources' | 'Headers' | 'Configs';

export interface ProjectAttributeUpdate {
  name?: string;
  defaultProfile?: string;
}

export interface ProjectProfileUpdate {
  originalName: string;
  name: string;
  buildType?: string;
  platform?: string;
  operatingSystem?: string;
  architecture?: string;
  environment?: string;
  launchExecutable?: string;
  launchWorkingDirectory?: string;
}

export interface ProjectDependencyUseEdit {
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

const productKinds = ['Application', 'Library', 'Tool', 'Test', 'Benchmark', 'Plugin', 'Module', 'External'];

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
  const pattern = new RegExp(`<${name}\\b[^>]*(?:/>|>)`, 'g');
  for (const match of source.matchAll(pattern)) {
    if (match.index === undefined || directChildDepth(source.slice(0, match.index)) !== 0) {
      continue;
    }
    const start = rangeStart + match.index;
    const openEnd = start + match[0].length;
    if (/\/>$/.test(match[0])) {
      return { start, end: openEnd, openEnd, bodyStart: openEnd, bodyEnd: openEnd, indent: indentationBefore(xml, start) };
    }
    const closeText = `</${name}>`;
    const bodyEnd = xml.indexOf(closeText, openEnd);
    if (bodyEnd < 0 || bodyEnd > rangeEnd) {
      continue;
    }
    return { start, end: bodyEnd + closeText.length, openEnd, bodyStart: openEnd, bodyEnd, indent: indentationBefore(xml, start) };
  }
  return undefined;
}

function replaceRange(xml: string, start: number, end: number, replacement: string): string {
  return `${xml.slice(0, start)}${replacement}${xml.slice(end)}`;
}

function profileNamePattern(name: string): RegExp {
  const escaped = name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  return new RegExp(`<Profile\\b(?=[^>]*\\sName=(["'])${escaped}\\1)[^>]*(?:/>|>)`, 'g');
}

function findProfile(xml: string, name: string): { start: number; end: number; openEnd: number; bodyStart: number; bodyEnd: number; openTag: string; selfClosing: boolean; indent: string } | undefined {
  const rootStart = projectOpenMatch(xml).index! + projectOpenMatch(xml)[0].length;
  const rootEnd = projectCloseIndex(xml);
  const source = xml.slice(rootStart, rootEnd);
  for (const match of source.matchAll(profileNamePattern(name))) {
    if (match.index === undefined || directChildDepth(source.slice(0, match.index)) !== 0) {
      continue;
    }
    const start = rootStart + match.index;
    const openTag = match[0];
    const openEnd = start + openTag.length;
    const selfClosing = /\/>$/.test(openTag);
    if (selfClosing) {
      return { start, end: openEnd, openEnd, bodyStart: openEnd, bodyEnd: openEnd, openTag, selfClosing, indent: indentationBefore(xml, start) };
    }
    const close = xml.indexOf('</Profile>', openEnd);
    if (close < 0 || close > rootEnd) {
      continue;
    }
    return { start, end: close + '</Profile>'.length, openEnd, bodyStart: openEnd, bodyEnd: close, openTag, selfClosing, indent: indentationBefore(xml, start) };
  }
  return undefined;
}

function rootProductKind(xml: string): string {
  const open = projectOpenMatch(xml);
  const rootStart = open.index! + open[0].length;
  const rootEnd = projectCloseIndex(xml);
  for (const kind of productKinds) {
    if (findDirectSection(xml, rootStart, rootEnd, kind)) {
      return kind;
    }
  }
  return 'Application';
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

function ensureProfile(xml: string, name: string): string {
  if (findProfile(xml, name)) {
    return xml;
  }
  const rootEnd = projectCloseIndex(xml);
  const projectIndent = indentationBefore(xml, rootEnd);
  const insert = `${projectIndent}<Profile Name="${escapeAttribute(name)}" />\n`;
  return replaceRange(xml, rootEnd, rootEnd, insert);
}

function ensureProduct(xml: string, profileName?: string): { xml: string; section: NonNullable<ReturnType<typeof findDirectSection>>; kind: string } {
  const kind = rootProductKind(xml);
  const range = scopeRange(xml, profileName);
  const existing = findDirectSection(range.xml, range.start, range.end, kind);
  if (existing) {
    if (existing.bodyStart === existing.bodyEnd) {
      const replacement = `<${kind}>\n${existing.indent}</${kind}>`;
      const next = replaceRange(range.xml, existing.start, existing.end, replacement);
      const expandedRange = scopeRange(next, profileName);
      const expanded = findDirectSection(next, expandedRange.start, expandedRange.end, kind);
      if (!expanded) {
        throw new Error(`Failed to expand <${kind}> section.`);
      }
      return { xml: next, section: expanded, kind };
    }
    return { xml: range.xml, section: existing, kind };
  }
  const insert = `\n${range.indent}<${kind}>\n${range.indent}</${kind}>`;
  const next = replaceRange(range.xml, range.closeInsertIndex, range.closeInsertIndex, insert);
  const nextRange = scopeRange(next, profileName);
  const section = findDirectSection(next, nextRange.start, nextRange.end, kind);
  if (!section) {
    throw new Error(`Failed to create <${kind}> section.`);
  }
  return { xml: next, section, kind };
}

function ensureProductSection(xml: string, sectionName: string, profileName?: string): { xml: string; section: NonNullable<ReturnType<typeof findDirectSection>> } {
  const product = ensureProduct(xml, profileName);
  const existing = findDirectSection(product.xml, product.section.bodyStart, product.section.bodyEnd, sectionName);
  if (existing) {
    return { xml: product.xml, section: existing };
  }
  const insert = `\n${childIndent(product.section.indent)}<${sectionName}>\n${childIndent(product.section.indent)}</${sectionName}>`;
  const next = replaceRange(product.xml, product.section.bodyEnd, product.section.bodyEnd, insert);
  const refreshed = ensureProduct(next, profileName);
  const section = findDirectSection(next, refreshed.section.bodyStart, refreshed.section.bodyEnd, sectionName);
  if (!section) {
    throw new Error(`Failed to create <${sectionName}> section.`);
  }
  return { xml: next, section };
}

function formatDependencyUse(reference: ProjectDependencyUseEdit, indent: string): string {
  const tag = setAttributes('<Package />', {
    Name: reference.name,
    Version: reference.version,
    Optional: reference.optional
  });
  return `${indent}${tag}`;
}

function formatInput(block: ProjectInputBlock, entry: ProjectInputEdit, indent: string): string {
  const selectorAttributes = {
    When: entry.condition,
    TargetPlatform: entry.platform,
    OperatingSystem: entry.operatingSystem,
    Architecture: entry.architecture,
    BuildType: entry.buildType,
    Environment: entry.environment
  };
  if (block === 'Configs') {
    const tag = setAttributes('<Config />', {
      Source: entry.path ?? entry.include,
      ...selectorAttributes
    });
    return `${indent}${tag}`;
  }
  const pathValue = entry.path ?? entry.include;
  const tag = setAttributes(`<${block} />`, {
    Path: pathValue,
    Include: entry.mode === 'Directory' ? entry.include : undefined,
    Exclude: entry.exclude,
    ...selectorAttributes
  });
  return `${indent}${tag}`;
}

export function updateProjectAttributes(xml: string, update: ProjectAttributeUpdate): string {
  const match = projectOpenMatch(xml);
  const nextTag = setAttributes(match[0], {
    Name: update.name,
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
  return ensureProfile(xml, name);
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

function setDefaults(xml: string, profileName: string, update: ProjectProfileUpdate): string {
  let next = ensureProfile(xml, profileName);
  const ensured = (() => {
    const range = scopeRange(next, profileName);
    const existing = findDirectSection(next, range.start, range.end, 'Defaults');
    if (existing) {
      return { xml: next, section: existing };
    }
    const insert = `\n${range.indent}<Defaults>\n${range.indent}</Defaults>`;
    next = replaceRange(next, range.closeInsertIndex, range.closeInsertIndex, insert);
    const nextRange = scopeRange(next, profileName);
    const section = findDirectSection(next, nextRange.start, nextRange.end, 'Defaults');
    if (!section) {
      throw new Error('Failed to create <Defaults>.');
    }
    return { xml: next, section };
  })();
  const indent = childIndent(ensured.section.indent);
  const lines = [
    update.buildType ? `${indent}<BuildType Name="${escapeAttribute(update.buildType)}" />` : undefined,
    update.platform ? `${indent}<TargetPlatform Name="${escapeAttribute(update.platform)}" />` : undefined,
    update.operatingSystem ? `${indent}<OperatingSystem Name="${escapeAttribute(update.operatingSystem)}" />` : undefined,
    update.architecture ? `${indent}<Architecture Name="${escapeAttribute(update.architecture)}" />` : undefined,
    update.environment ? `${indent}<Environment Name="${escapeAttribute(update.environment)}" />` : undefined
  ].filter((line): line is string => Boolean(line));
  const replacement = lines.length > 0 ? `\n${lines.join('\n')}\n${ensured.section.indent}` : '';
  return replaceRange(ensured.xml, ensured.section.bodyStart, ensured.section.bodyEnd, replacement);
}

export function updateProfile(xml: string, update: ProjectProfileUpdate): string {
  if (!update.name?.trim()) {
    throw new Error('Profile name is required.');
  }
  const profile = findProfile(xml, update.originalName);
  if (!profile) {
    throw new Error(`Profile not found: ${update.originalName}`);
  }
  const openTag = setAttributes(profile.openTag.replace(/\s*\/>$/, '>'), {
    Name: update.name
  });
  let next = profile.selfClosing
    ? replaceRange(xml, profile.start, profile.end, `${openTag}\n${profile.indent}</Profile>`)
    : replaceRange(xml, profile.start, profile.openEnd, openTag);
  next = setDefaults(next, update.name, update);
  next = setLaunch(next, update.name, update.launchExecutable, update.launchWorkingDirectory);
  const root = projectOpenMatch(next);
  if (readAttribute(root[0], 'DefaultProfile') === update.originalName) {
    next = updateProjectAttributes(next, { defaultProfile: update.name });
  }
  return next;
}

export function setLaunch(xml: string, profileName: string | undefined, executable?: string, workingDirectory?: string): string {
  const product = ensureProduct(xml, profileName);
  const existing = findDirectSection(product.xml, product.section.bodyStart, product.section.bodyEnd, 'Launch');
  const hasValues = Boolean(executable || workingDirectory);
  if (!hasValues) {
    return existing ? replaceRange(product.xml, existing.start, existing.end + (product.xml[existing.end] === '\n' ? 1 : 0), '') : product.xml;
  }
  const indent = existing?.indent ?? childIndent(product.section.indent);
  const tag = setAttributes('<Launch />', {
    Executable: executable,
    WorkingDirectory: workingDirectory
  });
  const replacement = `${indent}${tag}`;
  if (existing) {
    return replaceRange(product.xml, existing.start, existing.end, replacement);
  }
  return replaceRange(product.xml, product.section.bodyEnd, product.section.bodyEnd, `\n${replacement}`);
}

export function setDependencyUses(xml: string, references: ProjectDependencyUseEdit[], profileName?: string): string {
  const ensured = ensureProductSection(xml, 'Uses', profileName);
  const indent = childIndent(ensured.section.indent);
  const body = references.map((reference) => formatDependencyUse(reference, indent)).join('\n');
  const replacement = body ? `\n${body}\n${ensured.section.indent}` : '';
  return replaceRange(ensured.xml, ensured.section.bodyStart, ensured.section.bodyEnd, replacement);
}

export function addClangTidyAnalyzerPackage(xml: string): string {
  const ensured = ensureProductSection(xml, 'Uses');
  const body = ensured.xml.slice(ensured.section.bodyStart, ensured.section.bodyEnd);
  const packagePattern = /<Package\b(?=[^>]*\sName=(["'])NGIN\.Tooling\.ClangTidy\1)[^>]*(?:\/>|>[\s\S]*?<\/Package>)/;
  const existing = body.match(packagePattern);
  if (existing?.[0]?.includes('<Feature') && /<Feature\b(?=[^>]*\sName=(["'])Analyzer\1)/.test(existing[0])) {
    return ensured.xml;
  }

  if (existing?.index !== undefined) {
    const absoluteStart = ensured.section.bodyStart + existing.index;
    const absoluteEnd = absoluteStart + existing[0].length;
    const indent = indentationBefore(ensured.xml, absoluteStart);
    const featureLine = `${childIndent(indent)}<Feature Name="Analyzer" />`;
    if (existing[0].endsWith('/>')) {
      const open = existing[0].replace(/\s*\/>$/, '>');
      return replaceRange(ensured.xml, absoluteStart, absoluteEnd, `${open}\n${featureLine}\n${indent}</Package>`);
    }

    const close = existing[0].lastIndexOf('</Package>');
    return replaceRange(
      ensured.xml,
      absoluteStart,
      absoluteEnd,
      `${existing[0].slice(0, close)}${featureLine}\n${indent}${existing[0].slice(close)}`
    );
  }

  const indent = childIndent(ensured.section.indent);
  const packageBlock = [
    `${indent}<Package Name="NGIN.Tooling.ClangTidy"`,
    `${indent}         Version="[0.1.0,0.2.0)"`,
    `${indent}         Scope="Dev">`,
    `${childIndent(indent)}<Feature Name="Analyzer" />`,
    `${indent}</Package>`
  ].join('\n');
  return replaceRange(ensured.xml, ensured.section.bodyEnd, ensured.section.bodyEnd, `${packageBlock}\n`);
}

export function setInputEntries(xml: string, block: ProjectInputBlock, entries: ProjectInputEdit[], profileName?: string): string {
  const sectionName = block === 'Configs' ? 'Stage' : 'Build';
  const ensured = ensureProductSection(xml, sectionName, profileName);
  const tagName = block === 'Configs' ? 'Config' : block;
  let next = ensured.xml.replace(new RegExp(`^[ \\t]*<${tagName}\\b[^>]*(?:/>|>[\\s\\S]*?</${tagName}>)\\r?\\n?`, 'gm'), (match, offset: number) => {
    return offset >= ensured.section.bodyStart && offset < ensured.section.bodyEnd ? '' : match;
  });
  if (entries.length === 0) {
    return next;
  }
  const refreshed = ensureProductSection(next, sectionName, profileName);
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
  const ensured = ensureProductSection(xml, 'Uses', profileName);
  const escapedPackage = packageName.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const escapedFeature = featureName.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const entryPattern = new RegExp(`^[ \\t]*<Package\\b(?=[^>]*\\sName=(["'])${escapedPackage}\\1)[^>]*>(?=[\\s\\S]*?<Feature\\b(?=[^>]*\\sName=(["'])${escapedFeature}\\2))[\\s\\S]*?</Package>\\r?\\n?`, 'gm');
  let next = replaceRange(
    ensured.xml,
    ensured.section.bodyStart,
    ensured.section.bodyEnd,
    ensured.xml.slice(ensured.section.bodyStart, ensured.section.bodyEnd).replace(entryPattern, '')
  );
  if (state === 'inherit') {
    return next;
  }
  const refreshed = ensureProductSection(next, 'Uses', profileName);
  const indent = childIndent(refreshed.section.indent);
  const featureTag = setAttributes('<Feature />', {
    Name: featureName,
    Enabled: state === 'disable' ? false : undefined
  });
  const packageBlock = [
    `${indent}<Package Name="${escapeAttribute(packageName)}">`,
    `${childIndent(indent)}${featureTag}`,
    `${indent}</Package>`
  ].join('\n');
  return replaceRange(refreshed.xml, refreshed.section.bodyEnd, refreshed.section.bodyEnd, `${packageBlock}\n`);
}

export function setEnvironmentVariables(xml: string, environmentName: string, variables: ProjectEnvironmentVariableEdit[]): string {
  if (!environmentName.trim()) {
    throw new Error('Environment name is required.');
  }
  const ensured = ensureProductSection(xml, 'Environment');
  const indent = childIndent(ensured.section.indent);
  const lines = variables.map((variable) => {
    if (variable.secret || variable.fromLocalSetting) {
      const tag = setAttributes('<Secret />', {
        Name: variable.name,
        From: variable.fromLocalSetting ? `local:${variable.fromLocalSetting}` : variable.fromEnvironment ? `env:${variable.fromEnvironment}` : undefined,
        Required: variable.required === undefined ? undefined : variable.required
      });
      return `${indent}${tag}`;
    }
    const tag = setAttributes('<Env />', {
      Name: variable.name,
      Value: variable.value,
      FromEnvironment: variable.fromEnvironment,
      Required: variable.required === undefined ? undefined : variable.required
    });
    return `${indent}${tag}`;
  }).join('\n');
  const replacement = lines ? `\n${lines}\n${ensured.section.indent}` : '';
  return replaceRange(ensured.xml, ensured.section.bodyStart, ensured.section.bodyEnd, replacement);
}
