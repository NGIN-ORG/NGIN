import * as path from 'node:path';
import type { CompositionGraphPayload, GraphEditorFile, ProjectManifest } from '../core/types';
import { comparableFilePath, isSameOrChildPath } from './projectFiles';

export type FileMembershipState = 'selected' | 'unselected' | 'unknown' | 'generated' | 'external';

export interface FileMembership {
  state: FileMembershipState;
  file?: GraphEditorFile;
  profileName?: string;
}

export class ProjectMembershipIndex {
  private readonly filesByPath = new Map<string, GraphEditorFile>();
  readonly reliable: boolean;
  readonly projectRoot?: string;
  readonly profileName?: string;

  constructor(
    graph: CompositionGraphPayload | undefined,
    private readonly activeProject: ProjectManifest | undefined
  ) {
    const editor = graph?.plans?.editor;
    this.reliable = graph?.state === 'resolved' && Boolean(editor) && Boolean(activeProject);
    this.projectRoot = editor?.projectRoot;
    this.profileName = graph?.identity?.profile ?? graph?.selection?.profile;
    if (!this.reliable) {
      return;
    }
    for (const file of editor?.files ?? []) {
      this.filesByPath.set(comparableFilePath(file.absolutePath), file);
    }
  }

  membership(project: ProjectManifest, filePath: string): FileMembership {
    if (!this.reliable || !this.activeProject ||
        comparableFilePath(project.path) !== comparableFilePath(this.activeProject.path)) {
      return { state: 'unknown', profileName: this.profileName };
    }
    const file = this.filesByPath.get(comparableFilePath(filePath));
    if (!file) {
      return { state: 'unselected', profileName: this.profileName };
    }
    if (file.generated) {
      return { state: 'generated', file, profileName: this.profileName };
    }
    if (!isSameOrChildPath(file.absolutePath, project.directory)) {
      return { state: 'external', file, profileName: this.profileName };
    }
    return { state: 'selected', file, profileName: this.profileName };
  }

  containsSelectedDescendant(project: ProjectManifest, directoryPath: string): boolean {
    if (!this.reliable || !this.activeProject ||
        comparableFilePath(project.path) !== comparableFilePath(this.activeProject.path)) {
      return false;
    }
    for (const file of this.filesByPath.values()) {
      if (!file.generated &&
          isSameOrChildPath(file.absolutePath, project.directory) &&
          isSameOrChildPath(file.absolutePath, directoryPath)) {
        return true;
      }
    }
    return false;
  }

  externalFiles(project: ProjectManifest): GraphEditorFile[] {
    if (!this.reliable || !this.activeProject ||
        comparableFilePath(project.path) !== comparableFilePath(this.activeProject.path)) {
      return [];
    }
    return [...this.filesByPath.values()]
      .filter((file) => !file.generated && !isSameOrChildPath(file.absolutePath, project.directory))
      .sort(compareEditorFiles);
  }

  generatedFiles(): GraphEditorFile[] {
    if (!this.reliable) {
      return [];
    }
    return [...this.filesByPath.values()].filter((file) => file.generated).sort(compareEditorFiles);
  }
}

function compareEditorFiles(left: GraphEditorFile, right: GraphEditorFile): number {
  return path.basename(left.absolutePath).localeCompare(
    path.basename(right.absolutePath),
    undefined,
    { numeric: true, sensitivity: 'base' }
  ) || left.absolutePath.localeCompare(right.absolutePath);
}
