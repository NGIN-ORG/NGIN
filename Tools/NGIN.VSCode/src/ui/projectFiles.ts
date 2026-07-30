import * as path from 'node:path';
import { promises as fs } from 'node:fs';
import type { ProjectManifest } from '../core/types';

export type ProjectFileEntryKind = 'projectBoundary' | 'directory' | 'file' | 'link';

export interface ProjectFileEntry {
  name: string;
  path: string;
  kind: ProjectFileEntryKind;
  boundaryProject?: ProjectManifest;
}

export interface ProjectFileEnumerationContext {
  project: ProjectManifest;
  projects: ProjectManifest[];
  workspaceRoot: string;
  outputDir?: string;
  configuredOutputRoot?: string;
}

const HARD_EXCLUDED_NAMES = new Set(['.git', '.ngin', 'node_modules']);

export function comparableFilePath(value: string): string {
  const normalized = path.normalize(value);
  return process.platform === 'win32' ? normalized.toLowerCase() : normalized;
}

export function isSameOrChildPath(candidate: string, parent: string): boolean {
  const normalizedCandidate = comparableFilePath(candidate);
  const normalizedParent = comparableFilePath(parent);
  return normalizedCandidate === normalizedParent ||
    normalizedCandidate.startsWith(`${normalizedParent}${path.sep}`);
}

export function shouldExcludeProjectFile(
  context: ProjectFileEnumerationContext,
  entryPath: string,
  entryName: string
): boolean {
  if (HARD_EXCLUDED_NAMES.has(entryName.toLowerCase())) {
    return true;
  }
  if (comparableFilePath(entryPath) === comparableFilePath(context.project.path)) {
    return true;
  }
  if (context.outputDir && isSameOrChildPath(entryPath, context.outputDir)) {
    return true;
  }
  if (context.configuredOutputRoot) {
    const outputRoot = path.isAbsolute(context.configuredOutputRoot)
      ? context.configuredOutputRoot
      : path.resolve(context.workspaceRoot, context.configuredOutputRoot);
    if (isSameOrChildPath(entryPath, outputRoot)) {
      return true;
    }
  }
  return false;
}

function entryRank(kind: ProjectFileEntryKind): number {
  switch (kind) {
    case 'projectBoundary': return 0;
    case 'directory': return 1;
    case 'file': return 2;
    case 'link': return 3;
  }
}

export function compareProjectFileEntries(left: ProjectFileEntry, right: ProjectFileEntry): number {
  const rank = entryRank(left.kind) - entryRank(right.kind);
  if (rank !== 0) {
    return rank;
  }
  return left.name.localeCompare(right.name, undefined, { numeric: true, sensitivity: 'base' }) ||
    left.name.localeCompare(right.name);
}

export async function enumerateProjectDirectory(
  context: ProjectFileEnumerationContext,
  folderPath: string,
  role: 'source' | 'generated'
): Promise<ProjectFileEntry[]> {
  const entries = await fs.readdir(folderPath, { withFileTypes: true });
  const result: ProjectFileEntry[] = [];

  for (const entry of entries) {
    const entryPath = path.join(folderPath, entry.name);
    if (role === 'source' && shouldExcludeProjectFile(context, entryPath, entry.name)) {
      continue;
    }

    const boundaryProject = role === 'source'
      ? context.projects.find((candidate) =>
          comparableFilePath(candidate.path) !== comparableFilePath(context.project.path) &&
          comparableFilePath(candidate.directory) === comparableFilePath(entryPath)
        )
      : undefined;
    if (boundaryProject) {
      result.push({
        name: boundaryProject.name,
        path: entryPath,
        kind: 'projectBoundary',
        boundaryProject
      });
      continue;
    }

    result.push({
      name: entry.name,
      path: entryPath,
      kind: entry.isSymbolicLink()
        ? 'link'
        : entry.isDirectory()
          ? 'directory'
          : 'file'
    });
  }

  return result.sort(compareProjectFileEntries);
}
