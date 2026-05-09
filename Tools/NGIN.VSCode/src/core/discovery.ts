import * as path from 'node:path';
import { promises as fs } from 'node:fs';
import { PackageCatalogEntry, ProjectManifest, WorkspaceManifest } from './types';
import { parsePackageManifest, parseProjectManifest, parseWorkspaceManifest } from './xml';

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

async function collectPackageManifestPaths(root: string): Promise<string[]> {
  const manifests: string[] = [];
  async function visit(directory: string): Promise<void> {
    let entries: import('node:fs').Dirent[];
    try {
      entries = await fs.readdir(directory, { withFileTypes: true });
    } catch {
      return;
    }

    for (const entry of entries) {
      const entryPath = path.join(directory, entry.name);
      if (entry.isDirectory()) {
        await visit(entryPath);
        continue;
      }
      if (entry.isFile() && entry.name.endsWith('.nginpkg')) {
        manifests.push(entryPath);
      }
    }
  }

  await visit(root);
  return manifests.sort();
}

export async function loadPackageCatalog(workspace: WorkspaceManifest): Promise<Record<string, PackageCatalogEntry>> {
  const catalog: Record<string, PackageCatalogEntry> = {};
  for (const sourceRoot of workspace.packageSourcePaths ?? []) {
    for (const manifestPath of await collectPackageManifestPaths(sourceRoot)) {
      const manifest = parsePackageManifest(await readTextFile(manifestPath), manifestPath);
      if (catalog[manifest.name]) {
        continue;
      }
      catalog[manifest.name] = {
        name: manifest.name,
        path: manifestPath,
        directory: path.dirname(manifestPath)
      };
    }
  }
  return catalog;
}

export async function loadWorkspaceProjects(workspaceManifestPath: string): Promise<{
  workspace: WorkspaceManifest;
  projects: ProjectManifest[];
  packageCatalog: Record<string, PackageCatalogEntry>;
}> {
  const workspace = await loadWorkspaceManifest(workspaceManifestPath);
  const projects: ProjectManifest[] = [];
  const packageCatalog = await loadPackageCatalog(workspace);

  for (const projectPath of workspace.projectPaths) {
    if (!(await pathExists(projectPath))) {
      continue;
    }
    const projectXml = await readTextFile(projectPath);
    projects.push(parseProjectManifest(projectXml, projectPath));
  }

  return { workspace, projects, packageCatalog };
}
