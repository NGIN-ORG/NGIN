import * as path from 'node:path';
import { ProjectManifest } from './types';

function comparablePath(value: string): string {
  const normalized = path.normalize(value);
  return process.platform === 'win32' ? normalized.toLowerCase() : normalized;
}

export interface ProjectSelectionInputs {
  contextRoot: string;
  explicitProjectPath?: string;
  pinnedProjectPath?: string;
  activeDocumentPath?: string;
}

export function selectProjectByPrecedence(
  projects: ProjectManifest[],
  inputs: ProjectSelectionInputs
): ProjectManifest | undefined {
  const findProject = (projectPath: string): ProjectManifest | undefined => {
    const resolvedPath = path.isAbsolute(projectPath)
      ? projectPath
      : path.resolve(inputs.contextRoot, projectPath);
    return projects.find(
      (project) => comparablePath(project.path) === comparablePath(resolvedPath)
    );
  };

  if (inputs.explicitProjectPath) {
    return findProject(inputs.explicitProjectPath);
  }
  if (inputs.pinnedProjectPath) {
    const pinned = findProject(inputs.pinnedProjectPath);
    if (pinned) {
      return pinned;
    }
  }
  if (inputs.activeDocumentPath) {
    const documentPath = comparablePath(inputs.activeDocumentPath);
    const activeProject = projects.find((project) => {
      const projectDirectory = comparablePath(project.directory);
      return documentPath === comparablePath(project.path) ||
        documentPath === projectDirectory ||
        documentPath.startsWith(projectDirectory + path.sep);
    });
    if (activeProject) {
      return activeProject;
    }
  }
  return projects.length === 1 ? projects[0] : undefined;
}
