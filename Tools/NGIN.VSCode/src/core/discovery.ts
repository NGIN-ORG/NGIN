import * as path from 'node:path';
import { promises as fs } from 'node:fs';
import { ProjectManifest, WorkspaceManifest } from './types';
import { parseProjectManifest, parseWorkspaceManifest } from './xml';

export async function pathExists(candidate: string): Promise<boolean> {
  try {
    await fs.access(candidate);
    return true;
  } catch {
    return false;
  }
}

export async function readTextFile(filePath: string): Promise<string> {
  return fs.readFile(filePath, 'utf8');
}

export async function findNearestWorkspaceManifest(startPath: string): Promise<string | undefined> {
  let current = startPath;
  try {
    const stat = await fs.stat(current);
    if (stat.isFile()) {
      current = path.dirname(current);
    }
  } catch {
    current = path.dirname(current);
  }

  while (true) {
    const entries = await fs.readdir(current, { withFileTypes: true });
    const matches = entries
      .filter((entry) => entry.isFile() && entry.name.endsWith('.ngin'))
      .map((entry) => path.join(current, entry.name))
      .sort();

    if (matches.length > 0) {
      return matches[0];
    }

    const parent = path.dirname(current);
    if (parent === current) {
      return undefined;
    }
    current = parent;
  }
}

export async function loadWorkspaceManifest(manifestPath: string): Promise<WorkspaceManifest> {
  const xml = await readTextFile(manifestPath);
  return parseWorkspaceManifest(xml, manifestPath);
}

export async function loadProjectManifest(manifestPath: string): Promise<ProjectManifest> {
  const xml = await readTextFile(manifestPath);
  return parseProjectManifest(xml, manifestPath);
}

export async function loadWorkspaceProjects(workspaceManifestPath: string): Promise<{
  workspace: WorkspaceManifest;
  projects: ProjectManifest[];
}> {
  const workspace = await loadWorkspaceManifest(workspaceManifestPath);
  const projects: ProjectManifest[] = [];

  for (const projectPath of workspace.projectPaths) {
    if (!(await pathExists(projectPath))) {
      continue;
    }
    projects.push(await loadProjectManifest(projectPath));
  }

  return { workspace, projects };
}
