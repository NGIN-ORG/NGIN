import * as path from 'node:path';
import { promises as fs } from 'node:fs';
import * as vscode from 'vscode';
import { computeCompileCommandsPath } from '../core/compileCommands';
import { pathExists, readTextFile } from '../core/discovery';
import { computeLaunchManifestPath, computeOutputDir } from '../core/helpers';
import { ProjectManifest, StagedFile } from '../core/types';
import { parseLaunchManifest } from '../core/xml';
import { NginWorkspaceSnapshot } from '../state/workspaceState';
import {
  buildProjectTreeModels,
  ProjectTreeChildModel,
  ProjectTreeProfileModel,
  ProjectTreeDependencyKind,
  ProjectTreeDependencyModel,
  ProjectTreeGroupKind,
  ProjectTreeGroupModel,
  ProjectTreeInspectEntryModel,
  ProjectTreeInspectGroupKind,
  ProjectTreeInspectGroupModel,
  ProjectTreeManifestModel,
  ProjectTreeProjectModel,
} from './models';

class WorkspaceTreeItem extends vscode.TreeItem {
  constructor(label: string, description: string) {
    super(label, vscode.TreeItemCollapsibleState.Expanded);
    this.description = description;
    this.iconPath = new vscode.ThemeIcon('folder-library');
    this.contextValue = 'nginWorkspace';
    this.command = {
      command: 'ngin.workspaceStatus',
      title: label
    };
  }
}

interface ProjectExplorerTarget {
  projectPath?: string;
  profileName?: string;
  fsPath?: string;
  role?: 'manifest' | 'source' | 'config' | 'generated';
  isDirectory?: boolean;
}

class ProjectTreeItem extends vscode.TreeItem {
  readonly projectPath: string;
  readonly fsPath: string;

  constructor(model: ProjectTreeProjectModel) {
    super(model.label, model.selected ? vscode.TreeItemCollapsibleState.Expanded : vscode.TreeItemCollapsibleState.Collapsed);
    this.projectPath = model.projectPath;
    this.fsPath = model.projectPath;
    this.description = model.description;
    this.tooltip = model.tooltip;
    this.iconPath = new vscode.ThemeIcon(model.selected ? 'pass-filled' : 'project');
    this.contextValue = 'nginProject';
    this.command = {
      command: 'ngin.setActiveProject',
      title: model.label,
      arguments: [{ projectPath: model.projectPath, fsPath: model.projectPath, role: 'manifest' } satisfies ProjectExplorerTarget]
    };
  }
}

class ProjectManifestTreeItem extends vscode.TreeItem {
  readonly projectPath: string;
  readonly fsPath: string;

  constructor(model: ProjectTreeManifestModel) {
    super(model.label, vscode.TreeItemCollapsibleState.None);
    this.projectPath = model.projectPath;
    this.fsPath = model.filePath;
    this.tooltip = model.tooltip;
    this.iconPath = new vscode.ThemeIcon('file-code');
    this.contextValue = 'nginProjectManifest';
    this.command = {
      command: 'ngin.openProjectManifest',
      title: model.label,
      arguments: [{ projectPath: model.projectPath, fsPath: model.filePath, role: 'manifest' } satisfies ProjectExplorerTarget]
    };
  }
}

class ProjectGroupTreeItem extends vscode.TreeItem {
  readonly projectPath: string;
  readonly group: ProjectTreeGroupKind;

  constructor(model: ProjectTreeGroupModel) {
    super(model.label, vscode.TreeItemCollapsibleState.Collapsed);
    this.projectPath = model.projectPath;
    this.group = model.group;
    this.tooltip = model.tooltip;
    this.iconPath = new vscode.ThemeIcon(model.icon);
    this.contextValue = `nginProjectGroup.${model.group}`;
  }
}

class ProjectDependencyGroupTreeItem extends vscode.TreeItem {
  readonly projectPath: string;
  readonly dependencyKind: ProjectTreeDependencyKind;

  constructor(projectPath: string, dependencyKind: ProjectTreeDependencyKind, label: string) {
    super(label, vscode.TreeItemCollapsibleState.Collapsed);
    this.projectPath = projectPath;
    this.dependencyKind = dependencyKind;
    this.iconPath = new vscode.ThemeIcon(dependencyKind === 'projects' ? 'project' : 'package');
    this.contextValue = `nginProjectDependencyGroup.${dependencyKind}`;
  }
}

class ProjectDependencyTreeItem extends vscode.TreeItem {
  readonly projectPath: string;
  readonly dependencyKind: ProjectTreeDependencyKind;
  readonly targetPath?: string;
  readonly fsPath?: string;

  constructor(model: ProjectTreeDependencyModel) {
    super(model.label, vscode.TreeItemCollapsibleState.None);
    this.projectPath = model.projectPath;
    this.dependencyKind = model.kind;
    this.targetPath = model.targetPath;
    this.fsPath = model.targetPath;
    this.description = model.description;
    this.tooltip = model.tooltip;
    this.iconPath = new vscode.ThemeIcon(model.kind === 'projects' ? 'project' : 'package');
    this.contextValue = model.targetPath ? 'nginProjectDependency' : 'nginProjectDependency.unresolved';
    if (model.targetPath) {
      this.command = {
        command: 'ngin.internal.openPath',
        title: model.label,
        arguments: [model.targetPath]
      };
    }
  }
}

class ProjectInspectGroupTreeItem extends vscode.TreeItem {
  readonly projectPath: string;
  readonly inspectGroup: ProjectTreeInspectGroupKind;

  constructor(model: ProjectTreeInspectGroupModel) {
    super(model.label, vscode.TreeItemCollapsibleState.Collapsed);
    this.projectPath = model.projectPath;
    this.inspectGroup = model.kind;
    this.tooltip = model.tooltip;
    this.iconPath = new vscode.ThemeIcon(model.icon);
    this.contextValue = `nginProjectInspectGroup.${model.kind}`;
  }
}

class ProjectInspectEntryTreeItem extends vscode.TreeItem {
  readonly targetPath?: string;
  readonly fsPath?: string;

  constructor(public readonly model: ProjectTreeInspectEntryModel) {
    super(
      model.label,
      model.children?.length ? vscode.TreeItemCollapsibleState.Collapsed : vscode.TreeItemCollapsibleState.None
    );
    this.description = model.description;
    this.tooltip = model.tooltip;
    this.targetPath = model.targetPath;
    this.fsPath = model.targetPath;
    this.iconPath = new vscode.ThemeIcon(model.icon ?? 'symbol-property');
    this.contextValue = model.targetPath ? 'nginProjectInspectEntry.openable' : 'nginProjectInspectEntry';
    if (model.targetPath) {
      this.command = {
        command: 'ngin.internal.openPath',
        title: model.label,
        arguments: [model.targetPath]
      };
    }
  }
}

class ProjectFileTreeItem extends vscode.TreeItem {
  readonly projectPath: string;
  readonly fsPath: string;
  readonly role: 'source' | 'config' | 'generated';

  constructor(projectPath: string, filePath: string, role: 'source' | 'config' | 'generated', label?: string, description?: string) {
    super(label ?? path.basename(filePath), vscode.TreeItemCollapsibleState.None);
    this.projectPath = projectPath;
    this.fsPath = filePath;
    this.role = role;
    this.description = description;
    this.tooltip = filePath;
    this.resourceUri = vscode.Uri.file(filePath);
    this.iconPath = new vscode.ThemeIcon(role === 'generated' ? 'file-binary' : 'file');
    this.contextValue = role === 'source'
      ? 'nginProjectSourceFile'
      : role === 'config'
        ? 'nginProjectConfigFile'
        : 'nginProjectGeneratedFile';
    this.command = {
      command: 'ngin.internal.openPath',
      title: String(label ?? path.basename(filePath)),
      arguments: [filePath]
    };
  }
}

class ProjectFolderTreeItem extends vscode.TreeItem {
  readonly projectPath: string;
  readonly fsPath: string;
  readonly role: 'source' | 'config' | 'generated';

  constructor(projectPath: string, folderPath: string, role: 'source' | 'config' | 'generated', label?: string, description?: string) {
    super(label ?? path.basename(folderPath), vscode.TreeItemCollapsibleState.Collapsed);
    this.projectPath = projectPath;
    this.fsPath = folderPath;
    this.role = role;
    this.description = description;
    this.tooltip = folderPath;
    this.resourceUri = vscode.Uri.file(folderPath);
    this.iconPath = new vscode.ThemeIcon(role === 'generated' ? 'folder-library' : 'folder');
    this.contextValue = role === 'source'
      ? 'nginProjectSourceFolder'
      : role === 'config'
        ? 'nginProjectConfigFolder'
        : 'nginProjectGeneratedFolder';
  }
}

class ProjectConfigFolderTreeItem extends ProjectFolderTreeItem {
  readonly relativePath: string;

  constructor(projectPath: string, folderPath: string, relativePath: string, label?: string) {
    super(projectPath, folderPath, 'config', label);
    this.relativePath = relativePath;
  }
}

class ProjectConfigFileTreeItem extends ProjectFileTreeItem {
  readonly relativePath: string;

  constructor(projectPath: string, filePath: string, relativePath: string, description?: string) {
    super(projectPath, filePath, 'config', path.basename(relativePath), description);
    this.relativePath = relativePath;
  }
}

class GeneratedProfileTreeItem extends vscode.TreeItem {
  readonly projectPath: string;
  readonly profileName: string;
  readonly outputDir: string;
  readonly fsPath: string;
  readonly role = 'generated' as const;
  readonly isDirectory = true;

  constructor(projectPath: string, profileName: string, outputDir: string) {
    super(profileName, vscode.TreeItemCollapsibleState.Collapsed);
    this.projectPath = projectPath;
    this.profileName = profileName;
    this.outputDir = outputDir;
    this.fsPath = outputDir;
    this.description = path.basename(outputDir);
    this.tooltip = outputDir;
    this.iconPath = new vscode.ThemeIcon('archive');
    this.contextValue = 'nginProjectGeneratedProfile';
  }
}

class GeneratedStagedFilesTreeItem extends vscode.TreeItem {
  readonly projectPath: string;
  readonly outputDir: string;
  readonly launchManifestPath: string;
  readonly fsPath: string;
  readonly role = 'generated' as const;
  readonly isDirectory = true;

  constructor(projectPath: string, outputDir: string, launchManifestPath: string) {
    super('Staged Files', vscode.TreeItemCollapsibleState.Collapsed);
    this.projectPath = projectPath;
    this.outputDir = outputDir;
    this.launchManifestPath = launchManifestPath;
    this.fsPath = outputDir;
    this.iconPath = new vscode.ThemeIcon('files');
    this.contextValue = 'nginProjectGeneratedFolder';
  }
}

class ProfileTreeItem extends vscode.TreeItem {
  readonly projectPath: string;
  readonly profileName: string;

  constructor(model: ProjectTreeProfileModel) {
    super(model.label, vscode.TreeItemCollapsibleState.None);
    this.projectPath = model.projectPath;
    this.profileName = model.profileName;
    this.description = model.description;
    this.tooltip = model.tooltip;
    this.iconPath = new vscode.ThemeIcon(model.selected ? 'play-circle' : 'symbol-enum');
    this.contextValue = 'nginProfile';
    this.command = {
      command: 'ngin.selectProfile',
      title: model.label,
      arguments: [{ projectPath: model.projectPath, profileName: model.profileName }]
    };
  }
}

function comparablePath(value: string): string {
  const normalized = path.normalize(value);
  return process.platform === 'win32' ? normalized.toLowerCase() : normalized;
}

function resolveProjectPath(project: ProjectManifest, value: string): string {
  return path.isAbsolute(value) ? path.normalize(value) : path.resolve(project.directory, value);
}

function allConfigInputs(project: ProjectManifest): Array<{ source: string; owner: string }> {
  return [
    ...project.configInputs.map((source) => ({ source, owner: 'Project' })),
    ...project.profiles.flatMap((profile) => profile.configInputs.map((source) => ({
      source,
      owner: profile.name
    })))
  ];
}

async function readDirectoryItems(projectPath: string, folderPath: string, role: 'source' | 'generated'): Promise<Array<ProjectFolderTreeItem | ProjectFileTreeItem>> {
  try {
    const entries = await fs.readdir(folderPath, { withFileTypes: true });
    return entries
      .filter((entry) => !entry.name.startsWith('.'))
      .sort((left, right) => {
        if (left.isDirectory() !== right.isDirectory()) {
          return left.isDirectory() ? -1 : 1;
        }
        return left.name.localeCompare(right.name);
      })
      .map((entry) => {
        const entryPath = path.join(folderPath, entry.name);
        return entry.isDirectory()
          ? new ProjectFolderTreeItem(projectPath, entryPath, role)
          : new ProjectFileTreeItem(projectPath, entryPath, role);
      });
  } catch {
    return [];
  }
}

function directConfigChildren(
  project: ProjectManifest,
  projectPath: string,
  relativePath: string,
  sources: Array<{ source: string; owner: string }>
): Array<ProjectConfigFolderTreeItem | ProjectConfigFileTreeItem> {
  const folders = new Map<string, string>();
  const files: ProjectConfigFileTreeItem[] = [];
  const normalizedFolder = relativePath ? path.normalize(relativePath) : '';

  for (const entry of sources) {
    const normalizedSource = path.normalize(entry.source);
    const parent = path.dirname(normalizedSource) === '.' ? '' : path.dirname(normalizedSource);
    if (parent === normalizedFolder) {
      files.push(new ProjectConfigFileTreeItem(projectPath, resolveProjectPath(project, entry.source), entry.source, entry.owner));
      continue;
    }

    const relativeToFolder = normalizedFolder ? path.relative(normalizedFolder, normalizedSource) : normalizedSource;
    if (relativeToFolder.startsWith('..') || path.isAbsolute(relativeToFolder)) {
      continue;
    }

    const firstSegment = relativeToFolder.split(path.sep)[0];
    if (!firstSegment || firstSegment === path.basename(normalizedSource)) {
      continue;
    }

    const childRelative = normalizedFolder ? path.join(normalizedFolder, firstSegment) : firstSegment;
    folders.set(childRelative, resolveProjectPath(project, childRelative));
  }

  return [
    ...Array.from(folders.entries())
      .sort(([left], [right]) => left.localeCompare(right))
      .map(([childRelative, childPath]) => new ProjectConfigFolderTreeItem(projectPath, childPath, childRelative, path.basename(childRelative))),
    ...files.sort((left, right) => String(left.label).localeCompare(String(right.label)))
  ];
}

type ProjectsTreeElement =
  | WorkspaceTreeItem
  | ProjectTreeItem
  | ProjectManifestTreeItem
  | ProjectGroupTreeItem
  | ProjectDependencyGroupTreeItem
  | ProjectDependencyTreeItem
  | ProjectInspectGroupTreeItem
  | ProjectInspectEntryTreeItem
  | ProjectFolderTreeItem
  | ProjectFileTreeItem
  | ProjectConfigFolderTreeItem
  | ProjectConfigFileTreeItem
  | GeneratedProfileTreeItem
  | GeneratedStagedFilesTreeItem
  | ProfileTreeItem;

class ProjectsTreeDataProvider implements vscode.TreeDataProvider<ProjectsTreeElement> {
  private readonly onDidChangeTreeDataEmitter = new vscode.EventEmitter<void>();
  private snapshot: NginWorkspaceSnapshot = { launchManifestExists: false, stagedCompileCommandsAvailable: false };

  readonly onDidChangeTreeData = this.onDidChangeTreeDataEmitter.event;

  setSnapshot(snapshot: NginWorkspaceSnapshot): void {
    this.snapshot = snapshot;
    this.onDidChangeTreeDataEmitter.fire();
  }

  getTreeItem(element: ProjectsTreeElement): vscode.TreeItem {
    return element;
  }

  async getChildren(element?: ProjectsTreeElement): Promise<ProjectsTreeElement[]> {
    const model = buildProjectTreeModels(this.snapshot);

    if (!element) {
      if (!this.snapshot.workspace || !model.workspaceLabel || !model.workspaceDescription) {
        return [];
      }
      return [new WorkspaceTreeItem(model.workspaceLabel, model.workspaceDescription)];
    }

    if (element instanceof WorkspaceTreeItem) {
      return model.projects.map((project) => new ProjectTreeItem(project));
    }

    if (element instanceof ProjectTreeItem) {
      return (model.childrenByProject.get(element.projectPath) ?? []).map((child) => this.createChildTreeItem(child));
    }

    if (element instanceof ProjectGroupTreeItem) {
      const project = this.findProject(element.projectPath);
      if (!project) {
        return [];
      }
      if (element.group === 'files') {
        return this.getFileGroupChildren(project);
      }
      if (element.group === 'source') {
        return this.getSourceChildren(project);
      }
      if (element.group === 'config') {
        return directConfigChildren(project, project.path, '', allConfigInputs(project));
      }
      if (element.group === 'dependencies') {
        const dependencies = model.dependenciesByProject.get(element.projectPath);
        const items: ProjectsTreeElement[] = [];
        if (dependencies?.projects.length) {
          items.push(new ProjectDependencyGroupTreeItem(element.projectPath, 'projects', 'Project References'));
        }
        if (dependencies?.packages.length) {
          items.push(new ProjectDependencyGroupTreeItem(element.projectPath, 'packages', 'Package References'));
        }
        const inspectModel = model.inspectByProject.get(element.projectPath);
        items.push(...(inspectModel?.groups ?? []).map((group) => new ProjectInspectGroupTreeItem(group)));
        return items;
      }
      if (element.group === 'generated') {
        return this.getGeneratedProfileChildren(project);
      }
      if (element.group === 'profiles') {
        return (model.profilesByProject.get(element.projectPath) ?? []).map((profile) => new ProfileTreeItem(profile));
      }
    }

    if (element instanceof ProjectConfigFolderTreeItem) {
      const project = this.findProject(element.projectPath);
      return project ? directConfigChildren(project, project.path, element.relativePath, allConfigInputs(project)) : [];
    }

    if (element instanceof ProjectDependencyGroupTreeItem) {
      const dependencies = model.dependenciesByProject.get(element.projectPath);
      if (!dependencies) {
        return [];
      }
      return dependencies[element.dependencyKind].map((dependency) => new ProjectDependencyTreeItem(dependency));
    }

    if (element instanceof ProjectInspectGroupTreeItem) {
      const inspectModel = model.inspectByProject.get(element.projectPath);
      return (inspectModel?.entriesByGroup.get(element.inspectGroup) ?? []).map((entry) => new ProjectInspectEntryTreeItem(entry));
    }

    if (element instanceof ProjectInspectEntryTreeItem) {
      return (element.model.children ?? []).map((entry) => new ProjectInspectEntryTreeItem(entry));
    }

    if (element instanceof ProjectFolderTreeItem) {
      return readDirectoryItems(element.projectPath, element.fsPath, element.role === 'generated' ? 'generated' : 'source');
    }

    if (element instanceof GeneratedProfileTreeItem) {
      return this.getGeneratedArtifactChildren(element);
    }

    if (element instanceof GeneratedStagedFilesTreeItem) {
      return this.getStagedFileChildren(element);
    }

    return [];
  }

  private createChildTreeItem(model: ProjectTreeChildModel): ProjectsTreeElement {
    if (model.kind === 'manifest') {
      return new ProjectManifestTreeItem(model);
    }
    if (model.kind === 'group') {
      return new ProjectGroupTreeItem(model);
    }
    return new ProfileTreeItem(model);
  }

  private findProject(projectPath: string): ProjectManifest | undefined {
    return this.snapshot.workspace?.projects.find((project) => comparablePath(project.path) === comparablePath(projectPath));
  }

  private getFileGroupChildren(project: ProjectManifest): ProjectsTreeElement[] {
    const children: ProjectsTreeElement[] = [];
    if (project.sourceRoots.length > 0 || project.buildSources.length > 0) {
      children.push(this.createChildTreeItem({
        kind: 'group',
        id: `${project.path}:source`,
        label: 'Sources',
        tooltip: 'Declared source roots and explicit build sources.',
        icon: 'folder-library',
        projectPath: project.path,
        group: 'source'
      }));
    }

    const hasConfigInputs = project.configInputs.length > 0
      || project.profiles.some((profile) => profile.configInputs.length > 0);
    if (hasConfigInputs) {
      children.push(this.createChildTreeItem({
        kind: 'group',
        id: `${project.path}:config`,
        label: 'Config',
        tooltip: 'Declared root and profile config inputs.',
        icon: 'settings',
        projectPath: project.path,
        group: 'config'
      }));
    }

    return children;
  }

  private getSourceChildren(project: ProjectManifest): ProjectsTreeElement[] {
    const seen = new Set<string>();
    const items: ProjectsTreeElement[] = [];
    for (const sourceRoot of project.sourceRoots) {
      const sourceRootPath = resolveProjectPath(project, sourceRoot);
      const key = comparablePath(sourceRootPath);
      if (!seen.has(key)) {
        seen.add(key);
        items.push(new ProjectFolderTreeItem(project.path, sourceRootPath, 'source', sourceRoot));
      }
    }
    for (const source of project.buildSources) {
      const sourcePath = resolveProjectPath(project, source);
      const key = comparablePath(sourcePath);
      if (!seen.has(key)) {
        seen.add(key);
        items.push(new ProjectFileTreeItem(project.path, sourcePath, 'source', path.basename(source), source));
      }
    }
    return items;
  }

  private async getGeneratedProfileChildren(project: ProjectManifest): Promise<ProjectsTreeElement[]> {
    const workspace = this.snapshot.workspace;
    if (!workspace) {
      return [];
    }

    const items: ProjectsTreeElement[] = [];
    for (const profile of project.profiles) {
      const outputDir = computeOutputDir(workspace.root, project.name, profile.name, this.snapshot.buildOutputRoot);
      const launchManifestPath = computeLaunchManifestPath(outputDir, project.name, profile.name);
      const compileCommandsPath = computeCompileCommandsPath(outputDir);
      if (await pathExists(outputDir) || await pathExists(launchManifestPath) || await pathExists(compileCommandsPath)) {
        items.push(new GeneratedProfileTreeItem(project.path, profile.name, outputDir));
      }
    }
    return items;
  }

  private async getGeneratedArtifactChildren(element: GeneratedProfileTreeItem): Promise<ProjectsTreeElement[]> {
    const project = this.findProject(element.projectPath);
    if (!project) {
      return [];
    }

    const launchManifestPath = computeLaunchManifestPath(element.outputDir, project.name, element.profileName);
    const compileCommandsPath = computeCompileCommandsPath(element.outputDir);
    const items: ProjectsTreeElement[] = [];
    if (await pathExists(launchManifestPath)) {
      items.push(new ProjectFileTreeItem(element.projectPath, launchManifestPath, 'generated', path.basename(launchManifestPath), 'launch'));
      items.push(new GeneratedStagedFilesTreeItem(element.projectPath, element.outputDir, launchManifestPath));
    }
    if (await pathExists(compileCommandsPath)) {
      items.push(new ProjectFileTreeItem(element.projectPath, compileCommandsPath, 'generated', 'compile_commands.json', 'compile database'));
    }
    return items;
  }

  private async getStagedFileChildren(element: GeneratedStagedFilesTreeItem): Promise<ProjectsTreeElement[]> {
    try {
      const launch = parseLaunchManifest(await readTextFile(element.launchManifestPath), element.launchManifestPath);
      return launch.stagedFiles.map((file) => {
        const destination = path.isAbsolute(file.destination) ? file.destination : path.resolve(element.outputDir, file.destination);
        return new ProjectFileTreeItem(element.projectPath, destination, 'generated', file.relativeDestination ?? path.basename(destination), file.kind);
      });
    } catch {
      return [];
    }
  }
}

export class NginSidebarController implements vscode.Disposable {
  private readonly projectsProvider = new ProjectsTreeDataProvider();
  private readonly projectsTreeView: vscode.TreeView<ProjectsTreeElement>;

  constructor() {
    this.projectsTreeView = vscode.window.createTreeView('nginWorkspace', {
      treeDataProvider: this.projectsProvider,
      showCollapseAll: true
    });
  }

  dispose(): void {
    this.projectsTreeView.dispose();
  }

  refresh(snapshot: NginWorkspaceSnapshot): void {
    this.projectsProvider.setSnapshot(snapshot);

    const noWorkspaceMessage = 'Open a folder with a .ngin workspace manifest.';
    this.projectsTreeView.message = snapshot.workspace ? undefined : noWorkspaceMessage;
  }
}
