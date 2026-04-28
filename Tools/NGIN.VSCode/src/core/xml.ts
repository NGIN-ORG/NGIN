import * as path from 'node:path';
import { XMLParser } from 'fast-xml-parser';
import {
  LaunchManifest,
  LaunchDescriptor,
  PackageManifest,
  PackageReference,
  LocalSettingsManifest,
  ProjectConfiguration,
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

function parseConfigSources(node: unknown): string[] {
  const parent = node as { ConfigSources?: { Config?: unknown } } | undefined;
  return asArray(parent?.ConfigSources?.Config)
    .map((entry) => (entry as { Source?: string } | undefined)?.Source)
    .filter((entry): entry is string => Boolean(entry));
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
      const ref = entry as { Path?: string; Configuration?: string } | undefined;
      if (!ref?.Path) {
        return undefined;
      }
      return {
        path: path.resolve(baseDirectory, ref.Path),
        configuration: ref.Configuration
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
    projectPaths: projects,
    packageSourcePaths: packageSources
  };
}

export function parseProjectManifest(xml: string, manifestPath: string): ProjectManifest {
  const document = parser.parse(xml);
  const root = document.Project;
  if (!root) {
    throw new Error(`${manifestPath}: root element must be <Project>`);
  }

  const configurations = asArray(root.Configurations?.Configuration).map((entry): ProjectConfiguration => ({
    name: entry?.Name,
    buildConfiguration: entry?.BuildConfiguration,
    operatingSystem: entry?.OperatingSystem,
    architecture: entry?.Architecture,
    environment: entry?.Environment,
    launchExecutable: entry?.Launch?.Executable,
    launchWorkingDirectory: entry?.Launch?.WorkingDirectory,
    configSources: parseConfigSources(entry),
    projectRefs: parseProjectReferences(entry, path.dirname(manifestPath)),
    packageRefs: parsePackageReferences(entry)
  })).filter((entry) => Boolean(entry.name));

  const sourceRoots = asArray(root.SourceRoots?.SourceRoot)
    .map((entry) => entry?.Path as string | undefined)
    .filter((entry): entry is string => Boolean(entry));
  const buildSources = asArray(root.Build?.Sources?.Source)
    .map((entry) => entry?.Path as string | undefined)
    .filter((entry): entry is string => Boolean(entry));

  return {
    path: manifestPath,
    directory: path.dirname(manifestPath),
    name: root.Name ?? path.basename(manifestPath, path.extname(manifestPath)),
    defaultConfiguration: root.DefaultConfiguration,
    sourceRoots,
    configSources: parseConfigSources(root),
    localSettingsImports: parseLocalSettingsImports(root, path.dirname(manifestPath)),
    buildSources,
    projectRefs: parseProjectReferences(root, path.dirname(manifestPath)),
    packageRefs: parsePackageReferences(root),
    configurations
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
    configuration: root.Configuration,
    type: root.Type,
    buildConfiguration: root.BuildConfiguration,
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
