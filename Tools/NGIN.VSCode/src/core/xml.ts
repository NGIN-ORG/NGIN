import * as path from 'node:path';
import { XMLParser } from 'fast-xml-parser';
import {
  LaunchManifest,
  LaunchDescriptor,
  PackageManifest,
  PackageReference,
  LocalSettingsManifest,
  ModelDefaults,
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

function parseConfigInputs(node: unknown): string[] {
  const parent = node as { Inputs?: { Config?: unknown } } | undefined;
  return asArray(parent?.Inputs?.Config)
    .map((entry) => {
      const config = entry as { Path?: string; Pattern?: string } | undefined;
      return config?.Path ?? config?.Pattern;
    })
    .filter((entry): entry is string => Boolean(entry));
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
      const ref = entry as { Path?: string; Profile?: string } | undefined;
      if (!ref?.Path) {
        return undefined;
      }
      return {
        path: path.resolve(baseDirectory, ref.Path),
        profile: ref.Profile
      };
    })
    .filter((entry): entry is ProjectReference => Boolean(entry));
}

function parsePackageReferences(node: unknown): PackageReference[] {
  const parent = node as { References?: { Package?: unknown } } | undefined;
  return asArray(parent?.References?.Package)
    .map((entry): PackageReference | undefined => {
      const ref = entry as { Name?: string; Version?: string; Optional?: string | boolean } | undefined;
      if (!ref?.Name) {
        return undefined;
      }
      return {
        name: ref.Name,
        version: ref.Version,
        optional: ref.Optional === true || ref.Optional === 'true'
      };
    })
    .filter((entry): entry is PackageReference => Boolean(entry));
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
      configInputs: parseConfigInputs(entry),
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
    result.configInputs = [...result.configInputs, ...parseConfigInputs(entry)];
    result.projectRefs = [...(result.projectRefs ?? []), ...parseProjectReferences(entry, path.dirname(manifestPath))];
    result.packageRefs = [...(result.packageRefs ?? []), ...parsePackageReferences(entry)];
    return result;
  }).filter((entry) => Boolean(entry.name));

  const sourceRoots = asArray(root.SourceRoots?.SourceRoot)
    .map((entry) => entry?.Path as string | undefined)
    .filter((entry): entry is string => Boolean(entry));
  const typedSourceRoots = [
    ...asArray(root.Sources?.Public?.Root),
    ...asArray(root.Sources?.Private?.Root)
  ]
    .map((entry) => entry?.Path as string | undefined)
    .filter((entry): entry is string => Boolean(entry));
  const buildSources = asArray(root.Build?.Sources?.Source)
    .map((entry) => entry?.Path as string | undefined)
    .filter((entry): entry is string => Boolean(entry));
  const typedSourceFiles = [
    ...asArray(root.Sources?.Public?.File),
    ...asArray(root.Sources?.Private?.File)
  ]
    .map((entry) => entry?.Path as string | undefined)
    .filter((entry): entry is string => Boolean(entry));
  const typedSourceFileLists = [
    ...asArray(root.Sources?.Public?.Files),
    ...asArray(root.Sources?.Private?.Files)
  ].flatMap((entry) => splitPathList(textContent(entry)));

  return {
    path: manifestPath,
    directory: path.dirname(manifestPath),
    name: root.Name ?? path.basename(manifestPath, path.extname(manifestPath)),
    defaultProfile: root.DefaultProfile,
    modelIncludes: parseModelIncludes(root, path.dirname(manifestPath)),
    defaults: projectDefaults,
    sourceRoots: [...sourceRoots, ...typedSourceRoots],
    configInputs: parseConfigInputs(root),
    localSettingsImports: parseLocalSettingsImports(root, path.dirname(manifestPath)),
    buildSources: [...buildSources, ...typedSourceFiles, ...typedSourceFileLists],
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
    version: root.Version
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
