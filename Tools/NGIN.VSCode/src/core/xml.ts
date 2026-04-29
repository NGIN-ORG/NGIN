import * as path from 'node:path';
import { XMLParser } from 'fast-xml-parser';
import {
  LaunchManifest,
  LaunchDescriptor,
  PackageManifest,
  PackageReference,
  ConditionDefinition,
  LocalSettingsManifest,
  ModelDefaults,
  InputDeclaration,
  ProjectProfile,
  ProjectProfileTemplate,
  ProjectManifest,
  ProjectReference,
  StagedFile,
  WorkspaceManifest
} from './types';

const parser = new XMLParser({
  ignoreAttributes: false,
  attributeNamePrefix: '',
  parseTagValue: false,
  trimValues: true
});

function asArray<T>(value: T | T[] | undefined): T[] {
  if (value === undefined || value === null) {
    return [];
  }
  return Array.isArray(value) ? value : [value];
}

function splitPathList(text: string | undefined): string[] {
  if (!text) {
    return [];
  }
  return text
    .split(/[\r\n;,]+/)
    .map((entry) => entry.trim())
    .filter((entry) => entry.length > 0);
}

function parseBool(value: unknown, fallback = true): boolean {
  if (value === undefined || value === null || value === '') {
    return fallback;
  }
  return value === true || value === 'true' || value === '1' || value === 'yes';
}

interface TypedInputNode {
  Name?: string;
  Role?: string;
  Path?: string;
  Mode?: string;
  Visibility?: string;
  Target?: string;
  TargetRoot?: string;
  BasePath?: string;
  ContentKind?: string;
  Required?: string | boolean;
  Profile?: string;
  Platform?: string;
  OperatingSystem?: string;
  Architecture?: string;
  BuildType?: string;
  Environment?: string;
  Condition?: string;
  Include?: string;
  Exclude?: string;
  File?: unknown;
  Directory?: unknown;
  Glob?: unknown;
  '#text'?: string;
}

function splitTextLines(text: string | undefined): string[] {
  if (!text) {
    return [];
  }
  return text
    .split(/\r?\n/)
    .map((entry) => entry.trim())
    .filter((entry) => entry.length > 0 && !entry.startsWith('#'));
}

function typedBlockBase(name: string, node: TypedInputNode): Partial<InputDeclaration> | undefined {
  if (name === 'Sources') {
    return { kind: 'Source', role: 'Source', visibility: 'Private' };
  }
  if (name === 'Headers') {
    return { kind: 'Source', role: 'Header', visibility: 'Public' };
  }
  if (name === 'Configs') {
    return { kind: 'Config', visibility: 'Private' };
  }
  if (name === 'Contents') {
    return { kind: 'Content', visibility: 'Private' };
  }
  if (name === 'Assets') {
    return { kind: 'Asset', visibility: 'Private' };
  }
  if (name === 'ToolInputs') {
    return { kind: 'ToolInput', visibility: 'Private' };
  }
  if (name === 'Generated') {
    return { kind: 'Generated', role: node.Role, visibility: 'Private' };
  }
  return undefined;
}

function parseInputEntry(entry: unknown, inherited: Partial<InputDeclaration>): InputDeclaration | undefined {
  const node = entry as TypedInputNode | undefined;
  const include = node?.Include ? splitPathList(node.Include) : inherited.include;
  const mode = node?.Mode ?? inherited.mode ?? (node?.Path ? 'File' : include && include.length > 0 ? 'Glob' : undefined);
  if (!inherited.kind || !mode) {
    return undefined;
  }
  if ((mode === 'File' || mode === 'Directory') && !node?.Path && !inherited.path) {
    return undefined;
  }
  if (mode === 'Glob' && (!include || include.length === 0)) {
    return undefined;
  }
  return {
    ...inherited,
    name: node?.Name ?? inherited.name,
    kind: inherited.kind,
    role: inherited.role,
    path: node?.Path ?? inherited.path,
    pattern: mode === 'Glob' ? include.join(';') : inherited.pattern,
    mode,
    visibility: node?.Visibility ?? inherited.visibility ?? 'Private',
    target: node?.Target ?? inherited.target,
    targetRoot: node?.TargetRoot ?? inherited.targetRoot,
    basePath: node?.BasePath ?? inherited.basePath,
    contentKind: node?.ContentKind ?? inherited.contentKind,
    required: parseBool(node?.Required, inherited.required ?? true),
    profile: node?.Profile ?? inherited.profile,
    platform: node?.Platform ?? inherited.platform,
    operatingSystem: node?.OperatingSystem ?? inherited.operatingSystem,
    architecture: node?.Architecture ?? inherited.architecture,
    buildType: node?.BuildType ?? inherited.buildType,
    environment: node?.Environment ?? inherited.environment,
    condition: node?.Condition ?? inherited.condition,
    include,
    exclude: node?.Exclude ? splitPathList(node.Exclude) : inherited.exclude,
    setName: inherited.setName
  };
}

function parseInputs(node: unknown): InputDeclaration[] {
  const parent = node as { Inputs?: Record<string, unknown> } | undefined;
  const result: InputDeclaration[] = [];
  const inputs = parent?.Inputs;
  if (!inputs) {
    return result;
  }
  for (const [blockName, blockValue] of Object.entries(inputs)) {
    if (!typedBlockBase(blockName, {})) {
      continue;
    }
    for (const block of asArray(blockValue)) {
      const blockNode = block as TypedInputNode;
      const base = typedBlockBase(blockName, blockNode);
      if (!base) {
        continue;
      }
      const inherited: Partial<InputDeclaration> = {
        ...base,
        setName: blockNode.Name,
        visibility: blockNode.Visibility ?? base.visibility,
        targetRoot: blockNode.TargetRoot,
        basePath: blockNode.BasePath,
        contentKind: blockNode.ContentKind,
        required: parseBool(blockNode.Required, true),
        profile: blockNode.Profile,
        platform: blockNode.Platform,
        operatingSystem: blockNode.OperatingSystem,
        architecture: blockNode.Architecture,
        buildType: blockNode.BuildType,
        environment: blockNode.Environment,
        condition: blockNode.Condition,
        include: blockNode.Include ? splitPathList(blockNode.Include) : undefined,
        exclude: blockNode.Exclude ? splitPathList(blockNode.Exclude) : undefined
      };
      if (blockNode.Path || inherited.include) {
        const input = parseInputEntry(
          { Path: blockNode.Path, Mode: blockNode.Path ? 'Directory' : 'Glob', Include: blockNode.Include, Exclude: blockNode.Exclude },
          inherited
        );
        if (input) {
          result.push(input);
        }
      }
      for (const line of splitTextLines(textContent(block))) {
        const input = parseInputEntry({ Path: line, Mode: 'File' }, inherited);
        if (input) {
          result.push(input);
        }
      }
      for (const entry of asArray(blockNode.File)) {
        const input = parseInputEntry({ ...(entry as object), Mode: 'File' }, inherited);
        if (input) {
          result.push(input);
        }
      }
      for (const entry of asArray(blockNode.Directory)) {
        const input = parseInputEntry({ ...(entry as object), Mode: 'Directory' }, inherited);
        if (input) {
          result.push(input);
        }
      }
      for (const entry of asArray(blockNode.Glob)) {
        const input = parseInputEntry({ ...(entry as object), Mode: 'Glob' }, inherited);
        if (input) {
          result.push(input);
        }
      }
    }
  }
  return result;
}

function configInputsFrom(inputs: InputDeclaration[]): string[] {
  return inputs
    .filter((entry) => entry.kind === 'Config')
    .map((entry) => entry.path ?? entry.pattern)
    .filter((entry): entry is string => Boolean(entry));
}

function sourceRootsFrom(inputs: InputDeclaration[]): string[] {
  return inputs
    .filter((entry) => (entry.kind === 'Source' || (entry.kind === 'Generated' && entry.role === 'Source')) && entry.mode === 'Directory' && Boolean(entry.path))
    .map((entry) => entry.path as string);
}

function buildSourcesFrom(inputs: InputDeclaration[]): string[] {
  return inputs
    .filter((entry) => (entry.kind === 'Source' || (entry.kind === 'Generated' && entry.role === 'Source')) && entry.mode === 'File' && Boolean(entry.path))
    .map((entry) => entry.path as string);
}

function textContent(node: unknown): string | undefined {
  if (typeof node === 'string') {
    return node;
  }
  const entry = node as { '#text'?: string } | undefined;
  return entry?.['#text'];
}

function parseLocalSettingsImports(node: unknown, baseDirectory: string): string[] {
  const parent = node as { LocalSettings?: { Import?: unknown } } | undefined;
  return asArray(parent?.LocalSettings?.Import)
    .map((entry) => (entry as { Path?: string } | undefined)?.Path)
    .filter((entry): entry is string => Boolean(entry))
    .map((entry) => path.isAbsolute(entry) ? entry : path.resolve(baseDirectory, entry));
}

function parseProjectReferences(node: unknown, baseDirectory: string): ProjectReference[] {
  const parent = node as { References?: { Project?: unknown } } | undefined;
  return asArray(parent?.References?.Project)
    .map((entry): ProjectReference | undefined => {
      const ref = entry as { Path?: string; Profile?: string; Platform?: string; OperatingSystem?: string; Architecture?: string; BuildType?: string; Environment?: string; Condition?: string } | undefined;
      if (!ref?.Path) {
        return undefined;
      }
      const parsed: ProjectReference = {
        path: path.resolve(baseDirectory, ref.Path),
        profile: ref.Profile
      };
      if (ref.Platform) { parsed.platform = ref.Platform; }
      if (ref.OperatingSystem) { parsed.operatingSystem = ref.OperatingSystem; }
      if (ref.Architecture) { parsed.architecture = ref.Architecture; }
      if (ref.BuildType) { parsed.buildType = ref.BuildType; }
      if (ref.Environment) { parsed.environment = ref.Environment; }
      if (ref.Condition) { parsed.condition = ref.Condition; }
      return parsed;
    })
    .filter((entry): entry is ProjectReference => Boolean(entry));
}

function parsePackageReferences(node: unknown): PackageReference[] {
  const parent = node as { References?: { Package?: unknown } } | undefined;
  return asArray(parent?.References?.Package)
    .map((entry): PackageReference | undefined => {
      const ref = entry as { Name?: string; Version?: string; Optional?: string | boolean; Profile?: string; Platform?: string; OperatingSystem?: string; Architecture?: string; BuildType?: string; Environment?: string; Condition?: string } | undefined;
      if (!ref?.Name) {
        return undefined;
      }
      const parsed: PackageReference = {
        name: ref.Name,
        version: ref.Version,
        optional: ref.Optional === true || ref.Optional === 'true'
      };
      if (ref.Profile) { parsed.profile = ref.Profile; }
      if (ref.Platform) { parsed.platform = ref.Platform; }
      if (ref.OperatingSystem) { parsed.operatingSystem = ref.OperatingSystem; }
      if (ref.Architecture) { parsed.architecture = ref.Architecture; }
      if (ref.BuildType) { parsed.buildType = ref.BuildType; }
      if (ref.Environment) { parsed.environment = ref.Environment; }
      if (ref.Condition) { parsed.condition = ref.Condition; }
      return parsed;
    })
    .filter((entry): entry is PackageReference => Boolean(entry));
}

function parseConditions(node: unknown): ConditionDefinition[] {
  const parent = node as { Conditions?: { Condition?: unknown } } | undefined;
  return asArray(parent?.Conditions?.Condition)
    .map((entry): ConditionDefinition | undefined => {
      const condition = entry as { Name?: string; Profile?: string; Platform?: string; OperatingSystem?: string; Architecture?: string; BuildType?: string; Environment?: string; Match?: { Profile?: string; Platform?: string; OperatingSystem?: string; Architecture?: string; BuildType?: string; Environment?: string } } | undefined;
      if (!condition?.Name) {
        return undefined;
      }
      const match = condition.Match;
      return {
        name: condition.Name,
        profile: condition.Profile ?? match?.Profile,
        platform: condition.Platform ?? match?.Platform,
        operatingSystem: condition.OperatingSystem ?? match?.OperatingSystem,
        architecture: condition.Architecture ?? match?.Architecture,
        buildType: condition.BuildType ?? match?.BuildType,
        environment: condition.Environment ?? match?.Environment
      };
    })
    .filter((entry): entry is ConditionDefinition => Boolean(entry));
}

function parseModelIncludes(root: { Includes?: { Include?: unknown } } | undefined, baseDirectory: string): string[] {
  return asArray(root?.Includes?.Include)
    .map((entry) => (entry as { Path?: string } | undefined)?.Path)
    .filter((entry): entry is string => Boolean(entry))
    .map((entry) => path.isAbsolute(entry) ? entry : path.resolve(baseDirectory, entry));
}

function parseDefaults(root: { Defaults?: unknown } | undefined): ModelDefaults | undefined {
  const defaults = root?.Defaults as {
    BuildType?: string;
    Platform?: string;
    OperatingSystem?: string;
    Architecture?: string;
    Environment?: string;
  } | undefined;
  if (!defaults) {
    return undefined;
  }
  return {
    buildType: defaults.BuildType,
    platform: defaults.Platform,
    operatingSystem: defaults.OperatingSystem,
    architecture: defaults.Architecture,
    environment: defaults.Environment
  };
}

function mergeDefaults(base: ModelDefaults | undefined, override: ModelDefaults | undefined): ModelDefaults | undefined {
  if (!base && !override) {
    return undefined;
  }
  return { ...(base ?? {}), ...(override ?? {}) };
}

function parseProfileTemplates(root: { ProfileTemplates?: { ProfileTemplate?: unknown } } | undefined, baseDirectory: string): Record<string, ProjectProfileTemplate> {
  const templates: Record<string, ProjectProfileTemplate> = {};
  for (const entry of asArray(root?.ProfileTemplates?.ProfileTemplate)) {
    const node = entry as {
      Name?: string;
      Extends?: string;
      BuildType?: string;
      Platform?: string;
      OperatingSystem?: string;
      Architecture?: string;
      Environment?: string;
      Launch?: { Executable?: string; WorkingDirectory?: string };
    } | undefined;
    if (!node?.Name) {
      continue;
    }
    templates[node.Name] = {
      name: node.Name,
      extends: node.Extends,
      buildType: node.BuildType,
      platform: node.Platform,
      operatingSystem: node.OperatingSystem,
      architecture: node.Architecture,
      environment: node.Environment,
      launchExecutable: node.Launch?.Executable,
      launchWorkingDirectory: node.Launch?.WorkingDirectory,
      inputs: parseInputs(entry),
      configInputs: configInputsFrom(parseInputs(entry)),
      projectRefs: parseProjectReferences(entry, baseDirectory),
      packageRefs: parsePackageReferences(entry)
    };
  }
  return templates;
}

function mergeProfileTemplateMaps(
  base: Record<string, ProjectProfileTemplate> | undefined,
  override: Record<string, ProjectProfileTemplate> | undefined
): Record<string, ProjectProfileTemplate> | undefined {
  if (!base && !override) {
    return undefined;
  }
  return { ...(base ?? {}), ...(override ?? {}) };
}

function applyProfileTemplate(
  target: ProjectProfile,
  templateName: string | undefined,
  templates: Record<string, ProjectProfileTemplate> | undefined,
  stack: string[] = []
): void {
  if (!templateName || !templates?.[templateName] || stack.includes(templateName)) {
    return;
  }
  const template = templates[templateName];
  applyProfileTemplate(target, template.extends, templates, [...stack, templateName]);
  target.buildType = template.buildType ?? target.buildType;
  target.platform = template.platform ?? target.platform;
  target.operatingSystem = template.operatingSystem ?? target.operatingSystem;
  target.architecture = template.architecture ?? target.architecture;
  target.environment = template.environment ?? target.environment;
  target.launchExecutable = template.launchExecutable ?? target.launchExecutable;
  target.launchWorkingDirectory = template.launchWorkingDirectory ?? target.launchWorkingDirectory;
  target.inputs = [...(target.inputs ?? []), ...template.inputs];
  target.configInputs = [...(target.configInputs ?? []), ...template.configInputs];
  target.projectRefs = [...(target.projectRefs ?? []), ...(template.projectRefs ?? [])];
  target.packageRefs = [...(target.packageRefs ?? []), ...(template.packageRefs ?? [])];
}

export interface ModelManifest {
  path: string;
  directory: string;
  modelIncludes: string[];
  defaults?: ModelDefaults;
  profileTemplates?: Record<string, ProjectProfileTemplate>;
}

export function parseModelManifest(xml: string, manifestPath: string): ModelManifest {
  const document = parser.parse(xml);
  const root = document.Model;
  if (!root) {
    throw new Error(`${manifestPath}: root element must be <Model>`);
  }
  const directory = path.dirname(manifestPath);
  return {
    path: manifestPath,
    directory,
    modelIncludes: parseModelIncludes(root, directory),
    defaults: parseDefaults(root),
    profileTemplates: parseProfileTemplates(root, directory)
  };
}

export function parseProjectModelIncludes(xml: string, manifestPath: string): string[] {
  const document = parser.parse(xml);
  return parseModelIncludes(document.Project, path.dirname(manifestPath));
}

export interface ProjectParseOptions {
  defaults?: ModelDefaults;
  profileTemplates?: Record<string, ProjectProfileTemplate>;
}

export function parseWorkspaceManifest(xml: string, manifestPath: string): WorkspaceManifest {
  const document = parser.parse(xml);
  const root = document.Workspace;
  if (!root) {
    throw new Error(`${manifestPath}: root element must be <Workspace>`);
  }

  const directory = path.dirname(manifestPath);
  const projects = asArray(root.Projects?.Project)
    .map((entry) => entry?.Path as string | undefined)
    .filter((entry): entry is string => Boolean(entry))
    .map((entry) => path.resolve(directory, entry));
  const packageSources = asArray(root.PackageSources?.PackageSource)
    .map((entry) => entry?.Path as string | undefined)
    .filter((entry): entry is string => Boolean(entry))
    .map((entry) => path.resolve(directory, entry));

  return {
    path: manifestPath,
    directory,
    name: root.Name ?? path.basename(manifestPath, path.extname(manifestPath)),
    platformVersion: root.PlatformVersion,
    modelIncludes: parseModelIncludes(root, directory),
    defaults: parseDefaults(root),
    profileTemplates: parseProfileTemplates(root, directory),
    projectPaths: projects,
    packageSourcePaths: packageSources
  };
}

export function parseProjectManifest(xml: string, manifestPath: string, options: ProjectParseOptions = {}): ProjectManifest {
  const document = parser.parse(xml);
  const root = document.Project;
  if (!root) {
    throw new Error(`${manifestPath}: root element must be <Project>`);
  }

  const projectDefaults = mergeDefaults(options.defaults, parseDefaults(root));
  const profileTemplates = mergeProfileTemplateMaps(options.profileTemplates, parseProfileTemplates(root, path.dirname(manifestPath)));
  const rootLaunch = root.Launch as { Executable?: string; WorkingDirectory?: string } | undefined;
  const profiles = asArray(root.Profiles?.Profile).map((entry): ProjectProfile => {
    const profile = entry as {
      Name?: string;
      Template?: string;
      BuildType?: string;
      Platform?: string;
      OperatingSystem?: string;
      Architecture?: string;
      Environment?: string;
      Launch?: { Executable?: string; WorkingDirectory?: string };
    } | undefined;
    const result: ProjectProfile = {
      name: profile?.Name,
      buildType: projectDefaults?.buildType,
      platform: projectDefaults?.platform,
      operatingSystem: projectDefaults?.operatingSystem,
      architecture: projectDefaults?.architecture,
      environment: projectDefaults?.environment,
      inputs: [],
      configInputs: []
    };
    applyProfileTemplate(result, profile?.Template, profileTemplates);
    result.buildType = profile?.BuildType ?? result.buildType;
    result.platform = profile?.Platform ?? result.platform;
    result.operatingSystem = profile?.OperatingSystem ?? result.operatingSystem;
    result.architecture = profile?.Architecture ?? result.architecture;
    result.environment = profile?.Environment ?? result.environment;
    result.launchExecutable = profile?.Launch?.Executable ?? result.launchExecutable ?? rootLaunch?.Executable;
    result.launchWorkingDirectory = profile?.Launch?.WorkingDirectory ?? result.launchWorkingDirectory ?? rootLaunch?.WorkingDirectory;
    if (result.launchExecutable === '$(OutputName)') {
      result.launchExecutable = root.Output?.Name ?? root.Name;
    }
    result.inputs = [...result.inputs, ...parseInputs(entry)];
    result.configInputs = configInputsFrom(result.inputs);
    result.projectRefs = [...(result.projectRefs ?? []), ...parseProjectReferences(entry, path.dirname(manifestPath))];
    result.packageRefs = [...(result.packageRefs ?? []), ...parsePackageReferences(entry)];
    return result;
  }).filter((entry) => Boolean(entry.name));

  const inputs = parseInputs(root);
  const buildSources = asArray(root.Build?.Sources?.Source)
    .map((entry) => entry?.Path as string | undefined)
    .filter((entry): entry is string => Boolean(entry));

  return {
    path: manifestPath,
    directory: path.dirname(manifestPath),
    name: root.Name ?? path.basename(manifestPath, path.extname(manifestPath)),
    defaultProfile: root.DefaultProfile,
    modelIncludes: parseModelIncludes(root, path.dirname(manifestPath)),
    defaults: projectDefaults,
    inputs,
    sourceRoots: sourceRootsFrom(inputs),
    configInputs: configInputsFrom(inputs),
    localSettingsImports: parseLocalSettingsImports(root, path.dirname(manifestPath)),
    buildSources: [...buildSources, ...buildSourcesFrom(inputs)],
    conditions: parseConditions(root),
    projectRefs: parseProjectReferences(root, path.dirname(manifestPath)),
    packageRefs: parsePackageReferences(root),
    profiles
  };
}

export function parseLocalSettingsManifest(xml: string, manifestPath: string): LocalSettingsManifest {
  const document = parser.parse(xml);
  const root = document.LocalSettings;
  if (!root) {
    throw new Error(`${manifestPath}: root element must be <LocalSettings>`);
  }

  const settings = asArray(root.Settings?.Setting)
    .map((entry): { key: string; secret?: boolean } | undefined => {
      const setting = entry as { Key?: string; Secret?: string | boolean } | undefined;
      if (!setting?.Key) {
        return undefined;
      }
      return {
        key: setting.Key,
        secret: setting.Secret === true || setting.Secret === 'true'
      };
    })
    .filter((entry): entry is { key: string; secret?: boolean } => Boolean(entry));

  return {
    path: manifestPath,
    directory: path.dirname(manifestPath),
    settings
  };
}

export function parsePackageManifest(xml: string, manifestPath: string): PackageManifest {
  const document = parser.parse(xml);
  const root = document.Package;
  if (!root) {
    throw new Error(`${manifestPath}: root element must be <Package>`);
  }

  return {
    path: manifestPath,
    directory: path.dirname(manifestPath),
    name: root.Name ?? path.basename(manifestPath, path.extname(manifestPath)),
    version: root.Version,
    conditions: parseConditions(root)
  };
}

export function parseLaunchManifest(xml: string, manifestPath: string): LaunchManifest {
  const document = parser.parse(xml);
  const root = document.LaunchManifest;
  if (!root) {
    throw new Error(`${manifestPath}: root element must be <LaunchManifest>`);
  }

  const launch: LaunchDescriptor = {
    workingDirectory: root.Launch?.WorkingDirectory,
    executable: root.Launch?.Executable,
    target: root.Launch?.Target,
    origin: root.Launch?.Origin
  };

  const stagedFiles = asArray(root.StagedFiles?.File).map((entry): StagedFile => ({
    kind: entry?.Kind ?? '',
    source: entry?.Source,
    destination: entry?.Destination,
    relativeDestination: entry?.RelativeDestination
  })).filter((entry) => Boolean(entry.destination));

  return {
    path: manifestPath,
    directory: path.dirname(manifestPath),
    project: root.Project,
    profile: root.Profile,
    type: root.Type,
    buildType: root.BuildType,
    platform: root.Platform,
    operatingSystem: root.OperatingSystem,
    architecture: root.Architecture,
    environmentName: root.Environment?.Name,
    launch,
    selectedExecutable: root.Launch?.Executable
      ? {
          name: root.Launch.Executable,
          target: root.Launch.Target,
          origin: root.Launch.Origin
        }
      : undefined,
    stagedFiles
  };
}
