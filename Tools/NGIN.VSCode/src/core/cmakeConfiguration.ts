import * as path from 'node:path';
import type { CMakeProjectSnapshot, CMakeTargetDescription } from '../model';
import { isWithin, normalizeForComparison } from './paths';

export type CMakeCompileGroup = CMakeTargetDescription['compileGroups'][number];

export interface CMakeCompileSelection {
  target: CMakeTargetDescription;
  group: CMakeCompileGroup;
}

interface RankedSelection extends CMakeCompileSelection {
  includeDepth: number;
  languageRank: number;
  targetRank: number;
}

function preferredLanguage(file: string): string {
  return path.extname(file).toLowerCase() === '.c' ? 'C' : 'CXX';
}

function targetRank(type: string): number {
  switch (type) {
    case 'STATIC_LIBRARY':
    case 'SHARED_LIBRARY':
    case 'MODULE_LIBRARY': return 0;
    case 'OBJECT_LIBRARY': return 1;
    case 'EXECUTABLE': return 2;
    default: return 3;
  }
}

function rankSelection(
  target: CMakeTargetDescription,
  group: CMakeCompileGroup,
  file: string
): RankedSelection {
  const containingRoots = group.includes.filter(include => isWithin(include, file));
  return {
    target,
    group,
    includeDepth: containingRoots.reduce((depth, include) => Math.max(depth, path.resolve(include).length), 0),
    languageRank: group.language === preferredLanguage(file) ? 0 : 1,
    targetRank: targetRank(target.type)
  };
}

function compareSelections(left: RankedSelection, right: RankedSelection): number {
  return right.includeDepth - left.includeDepth
    || left.languageRank - right.languageRank
    || left.targetRank - right.targetRank
    || left.target.name.localeCompare(right.target.name)
    || left.group.id.localeCompare(right.group.id);
}

/**
 * Selects the authoritative compile group for a CMake source or header.
 * Exact File API source ownership wins. Headers omitted from target source
 * lists use the most specific containing include root, then a deterministic
 * project compile-group fallback.
 */
export function selectCMakeCompileGroup(snapshot: CMakeProjectSnapshot, file: string): CMakeCompileSelection | undefined {
  const normalized = normalizeForComparison(file);
  for (const target of snapshot.targets) {
    const source = target.sources.find(value => normalizeForComparison(value.path) === normalized);
    if (!source) continue;
    const group = target.compileGroups.find(value => value.id === source.compileGroup) ?? target.compileGroups[0];
    if (group) return { target, group };
  }

  if (!isWithin(snapshot.project.root, file)) return undefined;
  const selections = snapshot.targets.flatMap(target => target.compileGroups.map(group =>
    rankSelection(target, group, file)));
  if (!selections.length) return undefined;

  const containing = selections.filter(selection => selection.includeDepth > 0);
  const selected = (containing.length ? containing : selections).sort(compareSelections)[0];
  return selected ? { target: selected.target, group: selected.group } : undefined;
}

export function sameCMakeSnapshotSet(
  left: ReadonlyMap<string, CMakeProjectSnapshot | undefined>,
  right: ReadonlyMap<string, CMakeProjectSnapshot | undefined>
): boolean {
  if (left.size !== right.size) return false;
  for (const [key, value] of left) {
    if (!right.has(key) || right.get(key) !== value) return false;
  }
  return true;
}
