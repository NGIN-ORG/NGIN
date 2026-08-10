import { promises as fs, type Dirent } from 'node:fs';
import * as path from 'node:path';
import type { CompositionGraph } from '../model';
import { normalizeForComparison } from './paths';

export type ProjectFileState = 'authored' | 'selected' | 'unselected' | 'generated' | 'external' | 'missing' | 'boundary';

export interface ProjectFileEntry {
  name: string;
  path: string;
  relativePath: string;
  directory: boolean;
  state: ProjectFileState;
  kind?: string;
  children?: ProjectFileEntry[];
}

const excludedDirectories = new Set(['.git', '.hg', '.svn', '.ngin', 'build', 'out', 'node_modules']);
const fileKinds = new Set(['Source', 'Header', 'CxxModule', 'Resource']);

async function entries(directory: string): Promise<Dirent[]> {
  try {
    return await fs.readdir(directory, { withFileTypes: true });
  } catch {
    return [];
  }
}

export async function enumerateProjectFiles(
  projectDirectory: string,
  projectManifest: string,
  graph: CompositionGraph,
  limit = 5000
): Promise<ProjectFileEntry[]> {
  const selected = new Map<string, { kind: string; generated: boolean; relative: string }>();
  const external: ProjectFileEntry[] = [];
  for (const item of graph.buildItems.filter(item => fileKinds.has(item.kind))) {
    const absolute = path.isAbsolute(item.path) ? path.normalize(item.path) : path.resolve(projectDirectory, item.path);
    const relative = path.relative(projectDirectory, absolute).split(path.sep).join('/');
    if (relative.startsWith('../') || path.isAbsolute(relative)) {
      external.push({ name: path.basename(absolute), path: absolute, relativePath: item.path, directory: false, state: 'external', kind: item.kind });
    } else {
      selected.set(normalizeForComparison(absolute), { kind: item.kind, generated: Boolean(item.generated), relative });
    }
  }

  let count = 0;
  const visit = async (directory: string): Promise<ProjectFileEntry[]> => {
    if (count >= limit) return [];
    const directoryEntries = await entries(directory);
    const result: ProjectFileEntry[] = [];
    for (const entry of directoryEntries.sort((left, right) => Number(right.isDirectory()) - Number(left.isDirectory()) || left.name.localeCompare(right.name, undefined, { numeric: true }))) {
      if (count++ >= limit) break;
      if (entry.isDirectory() && excludedDirectories.has(entry.name)) continue;
      const absolute = path.join(directory, entry.name);
      const relative = path.relative(projectDirectory, absolute).split(path.sep).join('/');
      if (entry.isDirectory()) {
        const childEntries = await entries(absolute);
        const boundary = childEntries.some(child => child.isFile() && child.name.endsWith('.nginproj'));
        const children = boundary ? undefined : await visit(absolute);
        result.push({ name: entry.name, path: absolute, relativePath: relative, directory: true, state: boundary ? 'boundary' : 'unselected', children });
      } else {
        const membership = selected.get(normalizeForComparison(absolute));
        result.push({
          name: entry.name,
          path: absolute,
          relativePath: relative,
          directory: false,
          state: normalizeForComparison(absolute) === normalizeForComparison(projectManifest) ? 'authored'
            : membership?.generated ? 'generated' : membership ? 'selected' : 'unselected',
          kind: membership?.kind
        });
        selected.delete(normalizeForComparison(absolute));
      }
    }
    return result;
  };

  const physical = await visit(projectDirectory);
  for (const membership of selected.values()) {
    const absolute = path.resolve(projectDirectory, membership.relative);
    physical.push({
      name: path.basename(absolute), path: absolute, relativePath: membership.relative,
      directory: false, state: membership.generated ? 'generated' : 'missing', kind: membership.kind
    });
  }
  if (external.length) {
    physical.push({ name: 'External', path: projectDirectory, relativePath: '', directory: true, state: 'external', children: external });
  }
  return physical;
}
