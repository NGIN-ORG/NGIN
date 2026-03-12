import * as path from 'node:path';
import { XMLParser } from 'fast-xml-parser';
import { ProjectManifest, ProjectVariant, StagedFile, TargetManifest, TargetRuntime, WorkspaceManifest } from './types';

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

  return {
    path: manifestPath,
    directory,
    name: root.Name ?? path.basename(manifestPath, path.extname(manifestPath)),
    platformVersion: root.PlatformVersion,
    projectPaths: projects
  };
}

export function parseProjectManifest(xml: string, manifestPath: string): ProjectManifest {
  const document = parser.parse(xml);
  const root = document.Project;
  if (!root) {
    throw new Error(`${manifestPath}: root element must be <Project>`);
  }

  const variants = asArray(root.Variants?.Variant).map((entry): ProjectVariant => ({
    name: entry?.Name,
    profile: entry?.Profile,
    platform: entry?.Platform,
    environment: entry?.Environment,
    workingDirectory: entry?.WorkingDirectory,
    launchExecutable: entry?.Launch?.Executable
  })).filter((entry) => Boolean(entry.name));

  return {
    path: manifestPath,
    directory: path.dirname(manifestPath),
    name: root.Name ?? path.basename(manifestPath, path.extname(manifestPath)),
    defaultVariant: root.DefaultVariant,
    variants
  };
}

export function parseTargetManifest(xml: string, manifestPath: string): TargetManifest {
  const document = parser.parse(xml);
  const root = document.TargetLayout;
  if (!root) {
    throw new Error(`${manifestPath}: root element must be <TargetLayout>`);
  }

  const runtime: TargetRuntime = {
    workingDirectory: root.Runtime?.WorkingDirectory,
    environment: root.Runtime?.Environment
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
    variant: root.Variant,
    runtime,
    selectedExecutable: root.SelectedExecutable
      ? {
          name: root.SelectedExecutable.Name,
          target: root.SelectedExecutable.Target,
          origin: root.SelectedExecutable.Origin
        }
      : undefined,
    stagedFiles
  };
}
