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
  excludePatterns?: string[];
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
  if (context.outputDir &&
      isSameOrChildPath(context.outputDir, context.project.directory) &&
      isSameOrChildPath(entryPath, context.outputDir)) {
    return true;
  }
  if (context.configuredOutputRoot) {
    const outputRoot = path.isAbsolute(context.configuredOutputRoot)
      ? context.configuredOutputRoot
      : path.resolve(context.workspaceRoot, context.configuredOutputRoot);
    if (isSameOrChildPath(outputRoot, context.project.directory) &&
        isSameOrChildPath(entryPath, outputRoot)) {
      return true;
    }
  }
  const relativePath = path.relative(context.project.directory, entryPath).split(path.sep).join('/');
  if ((context.excludePatterns ?? []).some((pattern) => matchesGlob(pattern, relativePath))) {
    return true;
  }
  return false;
}

function matchesGlob(pattern: string, value: string): boolean {
  const normalized = pattern.replace(/\\/g, '/').replace(/^\.\//, '');
  const flags = process.platform === 'win32' ? 'i' : '';
  if (normalized.endsWith('/**')) {
    const root = normalized.slice(0, -3);
    if (new RegExp(`^${escapeRegularExpression(root)}(?:/.*)?$`, flags).test(value)) {
      return true;
    }
  }
  let expression = '^';
  for (let index = 0; index < normalized.length; index += 1) {
    const character = normalized[index];
    if (character === '*' && normalized[index + 1] === '*') {
      if (normalized[index + 2] === '/') {
        expression += '(?:.*/)?';
        index += 2;
        continue;
      }
      expression += '.*';
      index += 1;
    } else if (character === '*') {
      expression += '[^/]*';
    } else if (character === '?') {
      expression += '[^/]';
    } else {
      expression += escapeRegularExpression(character);
    }
  }
  return new RegExp(`${expression}(?:/.*)?$`, flags).test(value);
}

function escapeRegularExpression(value: string): string {
  return value.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
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

    if (!entry.isDirectory() && !entry.isFile() && !entry.isSymbolicLink()) {
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

export class ProjectFileTreeService {
  private readonly directoryCache = new Map<string, { folderPath: string; value: Promise<ProjectFileEntry[]> }>();

  constructor(private readonly maximumEntries = 256) {}

  enumerate(
    context: ProjectFileEnumerationContext,
    folderPath: string,
    role: 'source' | 'generated'
  ): Promise<ProjectFileEntry[]> {
    const key = `${role}:${comparableFilePath(context.project.path)}:${comparableFilePath(folderPath)}:${(context.excludePatterns ?? []).join(';')}`;
    const existing = this.directoryCache.get(key);
    if (existing) {
      this.directoryCache.delete(key);
      this.directoryCache.set(key, existing);
      return existing.value;
    }
    const pending = enumerateProjectDirectory(context, folderPath, role);
    const entry = { folderPath, value: pending };
    this.directoryCache.set(key, entry);
    while (this.directoryCache.size > this.maximumEntries) {
      const oldest = this.directoryCache.keys().next().value as string | undefined;
      if (!oldest) break;
      this.directoryCache.delete(oldest);
    }
    pending.catch(() => {
      if (this.directoryCache.get(key) === entry) {
        this.directoryCache.delete(key);
      }
    });
    return pending;
  }

  invalidatePath(filePath: string): void {
    const normalized = comparableFilePath(filePath);
    for (const [key, entry] of this.directoryCache) {
      const folder = comparableFilePath(entry.folderPath);
      if (isSameOrChildPath(normalized, folder) || isSameOrChildPath(folder, path.dirname(normalized))) {
        this.directoryCache.delete(key);
      }
    }
  }

  clear(): void {
    this.directoryCache.clear();
  }
}
