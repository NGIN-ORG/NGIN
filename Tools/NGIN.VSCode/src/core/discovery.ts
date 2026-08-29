import * as path from 'node:path';
import * as vscode from 'vscode';
import type { DiscoveryResult, ProjectCandidate } from '../model';
import type { NginCli } from './cli';
import { parseEditorWorkspaceSnapshot } from './editorProtocol';
import {
  parseWorkspaceChoices,
  rootIdentity
} from './manifestText';
import { generatedDirectoryPattern, pathsEqual } from './paths';

async function readText(file: vscode.Uri): Promise<string> {
  return new TextDecoder().decode(await vscode.workspace.fs.readFile(file));
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
      projectSystem: 'Ngin',
      capabilities: ['Inspect', 'Build', 'SourceOwnership', 'OpenDeclaration', 'CompositionGraph', 'AuthoringPlan'],
      manifest: uri.fsPath,
      directory: path.dirname(uri.fsPath),
      root: path.dirname(uri.fsPath),
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


async function discoverWorkspaceProjects(cli: NginCli, workspaceUri: vscode.Uri): Promise<DiscoveryResult> {
  const source = await readText(workspaceUri);
  const choices = parseWorkspaceChoices(source);
  const directory = vscode.Uri.file(path.dirname(workspaceUri.fsPath));
  const result = await cli.run(
    ['editor', 'workspace', '--workspace', workspaceUri.fsPath],
    directory.fsPath,
    { cwd: directory.fsPath }
  );
  const snapshot = parseEditorWorkspaceSnapshot(result.stdout);
  const workspace = snapshot.workspaces.find(value => path.resolve(value.manifest) === path.resolve(workspaceUri.fsPath));
  if (!workspace) throw new Error(`NGIN CLI did not return workspace '${workspaceUri.fsPath}'.`);
  const projects: ProjectCandidate[] = workspace.projects.map(project => ({
    id: project.id,
    projectSystem: project.projectSystem,
    capabilities: project.capabilities,
    manifest: project.manifest ?? project.root,
    directory: project.root,
    root: project.root,
    name: project.name,
    artifactKind: project.artifactKind,
    libraryKind: project.libraryKind === 'None' ? undefined : project.libraryKind,
    hasTests: project.capabilities.includes('Test'),
    hasBenchmarks: project.capabilities.includes('Benchmark'),
    hasRun: project.capabilities.includes('Run'),
    workspaceManifest: workspaceUri.fsPath,
    workspaceChoices: choices
  }));

  return {
    workspaceId: workspace.id,
    workspaceFolder: directory.fsPath,
    workspaceManifest: workspaceUri.fsPath,
    workspaceChoices: choices,
    packages: workspace.packages,
    projects
  };
}

export async function discoverFolder(folder: vscode.WorkspaceFolder, cli?: NginCli): Promise<DiscoveryResult[]> {
  const workspaces = (await vscode.workspace.findFiles(
    new vscode.RelativePattern(folder, '*.ngin'),
    generatedDirectoryPattern
  )).sort((left, right) => left.fsPath.length - right.fsPath.length || left.fsPath.localeCompare(right.fsPath));

  if (workspaces.length > 0) {
    if (!cli) throw new Error('The NGIN CLI is required for authored workspace discovery.');
    return Promise.all(workspaces.map(workspace => discoverWorkspaceProjects(cli, workspace)));
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

export async function discoverAll(cli?: NginCli): Promise<DiscoveryResult[]> {
  const folders = vscode.workspace.workspaceFolders ?? [];
  return (await Promise.all(folders.map(folder => discoverFolder(folder, cli)))).flat();
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
