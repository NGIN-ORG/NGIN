import * as path from 'node:path';
import * as vscode from 'vscode';
import type { DiscoveryResult, ProjectCandidate } from '../model';
import {
  parseWorkspaceChoices,
  parseWorkspaceProjectRules,
  rootIdentity
} from './manifestText';
import { generatedDirectoryPattern, pathsEqual } from './paths';

async function readText(file: vscode.Uri): Promise<string> {
  return new TextDecoder().decode(await vscode.workspace.fs.readFile(file));
}

function isGeneratedPath(candidate: string): boolean {
  return candidate.split(/[\\/]/).some(segment => segment === '.ngin' || segment === 'build' || segment === 'out' || segment === 'node_modules');
}

async function candidateFromUri(
  uri: vscode.Uri,
  workspaceManifest?: string,
  workspaceChoices?: DiscoveryResult['workspaceChoices']
): Promise<ProjectCandidate | undefined> {
  try {
    const source = await readText(uri);
    const identity = rootIdentity(source);
    if ((identity.root !== 'Executable' && identity.root !== 'Library') || !identity.name) return undefined;
    return {
      manifest: uri.fsPath,
      directory: path.dirname(uri.fsPath),
      name: identity.name,
      artifactKind: identity.artifactKind,
      libraryKind: identity.libraryKind as ProjectCandidate['libraryKind'],
      hasAnalyze: /<Analyze\b/u.test(source),
      hasFormat: /<Format\b/u.test(source),
      hasTests: /<Test\b/u.test(source),
      hasBenchmarks: /<Benchmark\b/u.test(source),
      hasRun: identity.artifactKind === 'Executable',
      workspaceManifest,
      workspaceChoices
    };
  } catch {
    return undefined;
  }
}


async function discoverWorkspaceProjects(workspaceUri: vscode.Uri): Promise<DiscoveryResult> {
  const source = await readText(workspaceUri);
  const choices = parseWorkspaceChoices(source);
  const directory = vscode.Uri.file(path.dirname(workspaceUri.fsPath));
  const projectUris: vscode.Uri[] = [];

  for (const rule of parseWorkspaceProjectRules(source)) {
    if (!rule.include) continue;
    const exclude = rule.exclude ? new vscode.RelativePattern(directory, rule.exclude) : generatedDirectoryPattern;
    const matches = await vscode.workspace.findFiles(new vscode.RelativePattern(directory, rule.include), exclude);
    projectUris.push(...matches.filter(uri => !isGeneratedPath(path.relative(directory.fsPath, uri.fsPath))));
  }

  const unique = [...new Map(projectUris.map(uri => [process.platform === 'win32' ? uri.fsPath.toLowerCase() : uri.fsPath, uri])).values()];
  const projects = (await Promise.all(unique.map(uri => candidateFromUri(uri, workspaceUri.fsPath, choices))))
    .filter((candidate): candidate is ProjectCandidate => Boolean(candidate))
    .sort((left, right) => left.name.localeCompare(right.name, undefined, { numeric: true }));

  return {
    workspaceFolder: directory.fsPath,
    workspaceManifest: workspaceUri.fsPath,
    workspaceChoices: choices,
    projects
  };
}

export async function discoverFolder(folder: vscode.WorkspaceFolder): Promise<DiscoveryResult[]> {
  const workspaces = (await vscode.workspace.findFiles(
    new vscode.RelativePattern(folder, '**/*.ngin'),
    generatedDirectoryPattern
  )).sort((left, right) => left.fsPath.length - right.fsPath.length || left.fsPath.localeCompare(right.fsPath));

  if (workspaces.length > 0) {
    return Promise.all(workspaces.map(discoverWorkspaceProjects));
  }

  const projectUris = await vscode.workspace.findFiles(
    new vscode.RelativePattern(folder, '**/*.nginproj'),
    generatedDirectoryPattern
  );
  const projects = (await Promise.all(projectUris.map(uri => candidateFromUri(uri))))
    .filter((candidate): candidate is ProjectCandidate => Boolean(candidate))
    .sort((left, right) => left.name.localeCompare(right.name, undefined, { numeric: true }));
  return [{ workspaceFolder: folder.uri.fsPath, projects }];
}

export async function discoverAll(): Promise<DiscoveryResult[]> {
  const folders = vscode.workspace.workspaceFolders ?? [];
  return (await Promise.all(folders.map(discoverFolder))).flat();
}

export function chooseInitialProject(
  discoveries: DiscoveryResult[],
  persistedManifest?: string,
  activeDocument?: string
): ProjectCandidate | undefined {
  const projects = discoveries.flatMap(discovery => discovery.projects);
  return projects.find(project => pathsEqual(project.manifest, persistedManifest))
    ?? projects.find(project => pathsEqual(project.manifest, activeDocument))
    ?? projects.find(project => activeDocument && activeDocument.startsWith(project.directory + path.sep))
    ?? projects[0];
}
