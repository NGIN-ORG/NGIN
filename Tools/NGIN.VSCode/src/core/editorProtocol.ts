export const editorProtocolVersion = 1;

export type EditorIntent =
  | 'CreateItems'
  | 'IncludeItems'
  | 'ExcludeItems'
  | 'RenameItems'
  | 'MoveItems'
  | 'DeleteItems'
  | 'AddPackage'
  | 'ChangePackageRequirement'
  | 'RemovePackage';

export type EditorBuildItemKind = 'Source' | 'Header' | 'CxxModule' | 'Resource';
export type EditorVisibility = 'Private' | 'Public' | 'Interface';

export interface EditorItemRequest {
  path: string;
  kind: EditorBuildItemKind;
  visibility?: EditorVisibility;
}

export interface EditorProductSnapshot {
  kind: 'NGIN.EditorProductSnapshot';
  version: 1;
  state: 'ready' | 'degraded';
  manifestHash: string;
  product: {
    id: string;
    name: string;
    manifest: string;
    boundary: string;
    artifactKind: 'Executable' | 'Library';
    libraryKind: 'None' | 'Static' | 'Shared' | 'Interface' | 'Plugin';
  };
  context: { configuration: string; target: string; toolchain: string };
  fileRoles: Array<{
    path: string;
    role: 'Build' | 'StageInput' | 'ActionInput' | 'ActionOutput' | string;
    kind: string;
    visibility?: string;
    generated: boolean;
  }>;
  capabilities: { authoringPlan: boolean; fileRoles: boolean };
}

export interface EditorWorkspaceSnapshot {
  kind: 'NGIN.EditorWorkspaceSnapshot';
  version: 1;
  state: 'ready' | 'degraded';
  diagnostics: Array<{ severity: 'error' | 'warning'; code: string; message: string }>;
  workspaces: Array<{
    id: string;
    name: string;
    manifest: string;
    boundary: string;
    products: EditorProductSnapshot['product'][];
  }>;
  standaloneProducts: EditorProductSnapshot['product'][];
}

export interface EditorAuthoringPlan {
  kind: 'NGIN.EditorAuthoringPlan';
  version: 1;
  state: 'ready' | 'rejected';
  intent: EditorIntent;
  filesystem: Array<{ operation: 'create' | 'rename' | 'move' | 'delete'; path: string; to?: string }>;
  textEdits: Array<{ path: string; start: number; end: number; text: string }>;
  preconditions: Array<{ path: string; sha256: string }>;
  diagnostics: Array<{ severity: 'error' | 'warning'; code: string; message: string; path?: string }>;
  affectedProducts: string[];
  refresh: string[];
  items: Array<{
    path: string;
    kind: string;
    visibility?: string;
    before: { selected: boolean };
    after: { selected: boolean };
    matchedRule?: { pattern: string; line: number; column: number };
  }>;
}

function object(value: unknown, label: string): Record<string, unknown> {
  if (!value || typeof value !== 'object' || Array.isArray(value)) throw new Error(`${label} must be a JSON object.`);
  return value as Record<string, unknown>;
}

function envelope<T>(source: string, kind: string): T {
  const value = object(JSON.parse(source) as unknown, 'NGIN editor response');
  if (value.kind !== kind) throw new Error(`Expected ${kind}, received ${String(value.kind)}.`);
  if (value.version !== editorProtocolVersion) {
    throw new Error(`Unsupported NGIN editor protocol version ${String(value.version)}; expected ${editorProtocolVersion}.`);
  }
  return value as T;
}

export function parseEditorProductSnapshot(source: string): EditorProductSnapshot {
  const value = envelope<EditorProductSnapshot>(source, 'NGIN.EditorProductSnapshot');
  if (!value.product || !Array.isArray(value.fileRoles) || typeof value.manifestHash !== 'string') {
    throw new Error('The NGIN editor product snapshot is incomplete.');
  }
  return value;
}

export function parseEditorWorkspaceSnapshot(source: string): EditorWorkspaceSnapshot {
  const value = envelope<EditorWorkspaceSnapshot>(source, 'NGIN.EditorWorkspaceSnapshot');
  if (!Array.isArray(value.workspaces) || !Array.isArray(value.standaloneProducts)
    || !Array.isArray(value.diagnostics)) {
    throw new Error('The NGIN editor workspace snapshot is incomplete.');
  }
  return value;
}

export function parseEditorAuthoringPlan(source: string): EditorAuthoringPlan {
  const value = envelope<EditorAuthoringPlan>(source, 'NGIN.EditorAuthoringPlan');
  if (!Array.isArray(value.filesystem) || !Array.isArray(value.textEdits)
    || !Array.isArray(value.preconditions) || !Array.isArray(value.diagnostics)) {
    throw new Error('The NGIN editor authoring plan is incomplete.');
  }
  return value;
}

export function encodeEditorItem(item: EditorItemRequest): string {
  if (item.path.includes('|')) throw new Error("NGIN editor item paths cannot contain '|'.");
  return `${item.kind}|${item.visibility ?? ''}|${item.path}`;
}
