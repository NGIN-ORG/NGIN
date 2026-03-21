import * as path from 'node:path';
import { XMLParser } from 'fast-xml-parser';
import { LaunchManifest, LaunchRuntime, ProjectConfiguration, ProjectManifest, StagedFile, WorkspaceManifest } from './types';

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

  const configurations = asArray(root.Configurations?.Configuration).map((entry): ProjectConfiguration => ({
    name: entry?.Name,
    hostProfile: entry?.HostProfile ?? entry?.Profile,
    buildConfiguration: entry?.BuildConfiguration,
    platform: entry?.Platform,
    environment: entry?.Environment,
    workingDirectory: entry?.WorkingDirectory,
    launchExecutable: entry?.Launch?.Executable
  })).filter((entry) => Boolean(entry.name));

  return {
    path: manifestPath,
    directory: path.dirname(manifestPath),
    name: root.Name ?? path.basename(manifestPath, path.extname(manifestPath)),
    defaultConfiguration: root.DefaultConfiguration,
    configurations
  };
}

export function parseLaunchManifest(xml: string, manifestPath: string): LaunchManifest {
  const document = parser.parse(xml);
  const root = document.LaunchManifest;
  if (!root) {
    throw new Error(`${manifestPath}: root element must be <LaunchManifest>`);
  }

  const runtime: LaunchRuntime = {
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
    configuration: root.Configuration,
    type: root.Type,
    buildConfiguration: root.BuildConfiguration,
    hostProfile: root.HostProfile,
    platform: root.Platform,
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
