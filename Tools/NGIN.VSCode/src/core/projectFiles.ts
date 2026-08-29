import * as path from 'node:path';
import type { CompositionGraph } from '../model';
import { normalizeForComparison } from './paths';

export type ProductFileState =
  | 'ordinary'
  | 'selected'
  | 'candidate'
  | 'input'
  | 'generated'
  | 'external'
  | 'missing'
  | 'boundary'
  | 'ignored';

export interface ProductFileRole {
  state: Exclude<ProductFileState, 'ordinary' | 'candidate' | 'boundary' | 'ignored'>;
  kind?: string;
  owner?: string;
  provenance?: string;
}

export interface ProductFileNode {
  id: string;
  name: string;
  path: string;
  relativePath: string;
  directory: boolean;
  state: ProductFileState;
  kind?: string;
  owner?: string;
  provenance?: string;
}

// Compatibility name for command arguments contributed by the pre-overhaul tree.
export type ProjectFileEntry = ProductFileNode;

export interface ProductSemanticIndex {
  roles: ReadonlyMap<string, ProductFileRole>;
  generated: readonly ProductFileNode[];
  external: readonly ProductFileNode[];
}

const hiddenRoots = new Set(['.git', '.hg', '.svn', '.ngin', 'build', 'out', 'node_modules']);
const sourceExtensions = new Map([
  ['.c', 'Source'], ['.cc', 'Source'], ['.cpp', 'Source'], ['.cxx', 'Source'],
  ['.h', 'Header'], ['.hh', 'Header'], ['.hpp', 'Header'], ['.hxx', 'Header'], ['.inl', 'Header'],
  ['.ixx', 'CxxModule'], ['.cppm', 'CxxModule'], ['.mpp', 'CxxModule']
]);
const fileKinds = new Set(['Source', 'Header', 'CxxModule', 'Resource']);

function portableRelative(base: string, absolute: string): string {
  return path.relative(base, absolute).split(path.sep).join('/');
}

function isExternal(relative: string): boolean {
  return relative.startsWith('../') || path.isAbsolute(relative);
}

function roleNode(
  projectDirectory: string,
  absolute: string,
  relativePath: string,
  state: ProductFileNode['state'],
  kind?: string,
  owner?: string,
  provenance?: string
): ProductFileNode {
  return {
    id: productFileId(projectDirectory, relativePath, state),
    name: path.basename(absolute),
    path: absolute,
    relativePath,
    directory: false,
    state,
    kind,
    owner,
    provenance
  };
}

export function productFileId(projectDirectory: string, relativePath: string, suffix = ''): string {
  const normalized = relativePath.replaceAll('\\', '/').replace(/^\.\//u, '');
  return `ngin.file:${normalizeForComparison(projectDirectory)}:${normalized}${suffix ? `:${suffix}` : ''}`;
}

export function plausibleBuildKind(value: string): string | undefined {
  return sourceExtensions.get(path.extname(value).toLowerCase());
}

export function isDefaultHiddenName(name: string): boolean {
  return hiddenRoots.has(name);
}

export function semanticFileIndex(
  projectDirectory: string,
  graph?: CompositionGraph,
  generatedDirectory?: string
): ProductSemanticIndex {
  const roles = new Map<string, ProductFileRole>();
  const generated: ProductFileNode[] = [];
  const external: ProductFileNode[] = [];

  const add = (
    authoredPath: string,
    state: ProductFileRole['state'],
    kind?: string,
    owner?: string,
    provenance?: string,
    generatedBase = generatedDirectory ?? projectDirectory
  ): void => {
    const base = state === 'generated' ? generatedBase : projectDirectory;
    const absolute = path.isAbsolute(authoredPath) ? path.normalize(authoredPath) : path.resolve(base, authoredPath);
    const relative = portableRelative(state === 'generated' ? generatedBase : projectDirectory, absolute);
    if (state !== 'generated' && isExternal(relative)) {
      external.push(roleNode(projectDirectory, absolute, authoredPath, 'external', kind, owner, provenance));
      return;
    }
    const role = { state, kind, owner, provenance } satisfies ProductFileRole;
    roles.set(normalizeForComparison(absolute), role);
    if (state === 'generated') generated.push(roleNode(projectDirectory, absolute, relative, state, kind, owner, provenance));
  };

  for (const item of graph?.buildItems ?? []) {
    if (!fileKinds.has(item.kind)) continue;
    add(
      item.path,
      item.generated ? 'generated' : 'selected',
      item.kind,
      item.provenance?.owner,
      item.provenance?.reason
    );
  }
  for (const action of graph?.actions ?? []) {
    const owner = action.name ?? action.identity;
    for (const input of Array.isArray(action.inputs) ? action.inputs : []) {
      if (typeof input === 'string' && !roles.has(normalizeForComparison(path.resolve(projectDirectory, input)))) {
        add(input, 'input', plausibleBuildKind(input), owner, action.provenance?.reason);
      }
    }
    for (const output of Array.isArray(action.outputs) ? action.outputs : []) {
      if (typeof output === 'string') add(output, 'generated', plausibleBuildKind(output), owner, action.provenance?.reason);
    }
  }
  for (const contribution of graph?.contributions ?? []) {
    if (typeof contribution.include !== 'string' || !contribution.include) continue;
    add(
      contribution.include,
      'input',
      contribution.kind,
      typeof contribution.owner === 'string' ? contribution.owner : undefined,
      contribution.provenance?.reason
    );
  }

  return { roles, generated, external };
}

export function classifyPhysicalEntry(
  projectDirectory: string,
  absolute: string,
  directory: boolean,
  semantics: ProductSemanticIndex,
  boundary = false,
  ignored = false
): ProductFileNode {
  const relativePath = portableRelative(projectDirectory, absolute);
  const role = semantics.roles.get(normalizeForComparison(absolute));
  const state: ProductFileState = boundary ? 'boundary'
    : ignored ? 'ignored'
      : role?.state ?? (!directory && plausibleBuildKind(relativePath) ? 'candidate' : 'ordinary');
  return {
    id: productFileId(projectDirectory, relativePath),
    name: path.basename(absolute),
    path: absolute,
    relativePath,
    directory,
    state,
    kind: role?.kind ?? (!directory ? plausibleBuildKind(relativePath) : undefined),
    owner: role?.owner,
    provenance: role?.provenance
  };
}
