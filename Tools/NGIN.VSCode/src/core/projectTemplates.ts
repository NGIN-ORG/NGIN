export type ProjectProductKind = 'Executable' | 'Static' | 'Shared' | 'Interface' | 'Plugin';
export type ProjectLayout = 'colocated' | 'split' | 'public-private' | 'custom';

export interface CustomProjectLayout {
  sourceRoot: string;
  headerRoot: string;
}
