import * as path from 'node:path';
import * as vscode from 'vscode';
import { computeCompileCommandsPath } from '../core/compileCommands';
import { pathExists, readTextFile } from '../core/discovery';
import { computeLaunchManifestPath } from '../core/helpers';
import { ProjectManifest } from '../core/types';
import { parseLaunchManifest } from '../core/xml';
import { NginWorkspaceSnapshot } from '../state/workspaceState';
import {
  buildActiveProjectTreeModel,
  buildProjectTreeModels,
  ProjectTreeChildModel,
  ProjectTreeDependencyKind,
  ProjectTreeDependencyModel,
  ProjectTreeGroupKind,
  ProjectTreeGroupModel,
  ProjectTreeInspectEntryModel,
  ProjectTreeManifestModel,
  ProjectTreeProjectModel,
} from './models';
import {
  comparableFilePath,
  enumerateProjectDirectory,
  isSameOrChildPath,
  ProjectFileEnumerationContext
} from './projectFiles';

class WorkspaceTreeItem extends vscode.TreeItem {
  constructor(label: string, description: string, readonly manifestPath: string) {
    super(label, vscode.TreeItemCollapsibleState.Expanded);
    this.description = description;
    this.id = `ngin:workspace:${comparablePath(manifestPath)}`;
    this.iconPath = new vscode.ThemeIcon('folder-library');
    this.contextValue = 'nginWorkspace';
    this.command = {
      command: 'ngin.selectManifest',
      title: label
    };
  }
}

interface ProjectExplorerTarget {
  projectPath?: string;
  profileName?: string;
  publishName?: string;
  fsPath?: string;
  role?: 'manifest' | 'source' | 'config' | 'generated';
  isDirectory?: boolean;
}

class ProjectTreeItem extends vscode.TreeItem {
  readonly projectPath: string;
  readonly fsPath: string;

  constructor(model: ProjectTreeProjectModel) {
    super(model.label, model.selected ? vscode.TreeItemCollapsibleState.Expanded : vscode.TreeItemCollapsibleState.Collapsed);
    this.id = `ngin:project:${comparablePath(model.projectPath)}`;
    this.projectPath = model.projectPath;
    this.fsPath = model.projectPath;
    this.description = model.description;
    this.tooltip = model.tooltip;
    this.iconPath = new vscode.ThemeIcon(model.selected ? 'target' : 'project');
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
    this.id = `ngin:manifest:${comparablePath(model.filePath)}`;
    this.projectPath = model.projectPath;
    this.fsPath = model.filePath;
    this.tooltip = model.tooltip;
    this.description = model.description;
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
    this.id = `ngin:group:${model.id}`;
    this.projectPath = model.projectPath;
    this.group = model.group;
    this.tooltip = model.tooltip;
    this.description = model.description;
    this.iconPath = new vscode.ThemeIcon(model.icon);
    this.contextValue = `nginProjectGroup.${model.group}`;
  }
}

class ProjectDependencyGroupTreeItem extends vscode.TreeItem {
  readonly projectPath: string;
  readonly dependencyKind: ProjectTreeDependencyKind;

  constructor(projectPath: string, dependencyKind: ProjectTreeDependencyKind, label: string) {
    super(label, vscode.TreeItemCollapsibleState.Collapsed);
    this.id = `ngin:dependency-group:${comparablePath(projectPath)}:${dependencyKind}`;
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
  readonly explainIdentity?: string;
  readonly children?: ProjectTreeInspectEntryModel[];

  constructor(model: ProjectTreeDependencyModel) {
    super(model.label, model.children?.length ? vscode.TreeItemCollapsibleState.Collapsed : vscode.TreeItemCollapsibleState.None);
    this.id = `ngin:dependency:${comparablePath(model.projectPath)}:${model.kind}:${model.label}`;
    this.projectPath = model.projectPath;
    this.dependencyKind = model.kind;
    this.targetPath = model.targetPath;
    this.fsPath = model.targetPath;
    this.explainIdentity = model.explainIdentity;
    this.children = model.children;
    this.description = model.description;
    this.tooltip = model.tooltip;
    this.iconPath = new vscode.ThemeIcon(model.kind === 'projects' ? 'project' : 'package');
    this.contextValue = model.kind === 'projects'
      ? 'nginProjectReference'
      : model.targetPath ? 'nginProjectDependency' : 'nginProjectDependency.unresolved';
    if (model.targetPath) {
      this.command = {
        command: 'ngin.internal.openPath',
        title: model.label,
        arguments: [model.targetPath]
      };
    }
  }
}

class ProjectInspectEntryTreeItem extends vscode.TreeItem {
  readonly targetPath?: string;
  readonly fsPath?: string;
  readonly projectPath?: string;
  readonly explainIdentity?: string;
  readonly publishName?: string;
  readonly targetKind?: ProjectTreeInspectEntryModel['targetKind'];

  constructor(public readonly model: ProjectTreeInspectEntryModel, projectPath?: string) {
    super(
      model.label,
      model.children?.length ? vscode.TreeItemCollapsibleState.Collapsed : vscode.TreeItemCollapsibleState.None
    );
    this.id = `ngin:inspect:${comparablePath(projectPath ?? '')}:${model.context ?? 'entry'}:${model.explainIdentity ?? model.targetPath ?? model.label}`;
    this.description = model.description;
    this.tooltip = model.tooltip;
    this.targetPath = model.targetPath;
    this.fsPath = model.targetPath;
    this.projectPath = projectPath;
    this.explainIdentity = model.explainIdentity;
    this.publishName = model.publishName;
    this.targetKind = model.targetKind;
    this.iconPath = new vscode.ThemeIcon(model.icon ?? 'symbol-property');
    this.contextValue = model.context
      ? `nginProjectInspectEntry.${model.context}${model.targetPath ? '.openable' : ''}`
      : model.targetPath ? 'nginProjectInspectEntry.openable' : 'nginProjectInspectEntry';
    if (model.publishName) {
      this.command = {
        command: 'ngin.publish',
        title: `Publish ${model.publishName}`,
        arguments: [{ projectPath, publishName: model.publishName } satisfies ProjectExplorerTarget]
      };
    } else if (model.targetPath) {
      this.command = {
        command: 'ngin.internal.openPath',
        title: model.label,
        arguments: [model.targetPath, model.targetKind]
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
    this.id = `ngin:file:${comparablePath(projectPath)}:${comparablePath(filePath)}:${role}`;
    this.projectPath = projectPath;
    this.fsPath = filePath;
    this.role = role;
    this.description = description;
    this.tooltip = filePath;
    this.resourceUri = vscode.Uri.file(filePath);
    if (role === 'generated') {
      this.iconPath = new vscode.ThemeIcon('file-binary');
    }
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
    this.id = `ngin:folder:${comparablePath(projectPath)}:${comparablePath(folderPath)}:${role}`;
    this.projectPath = projectPath;
    this.fsPath = folderPath;
    this.role = role;
    this.description = description;
    this.tooltip = folderPath;
    this.resourceUri = vscode.Uri.file(folderPath);
    if (role === 'generated') {
      this.iconPath = new vscode.ThemeIcon('folder-library');
    }
    this.contextValue = role === 'source'
      ? 'nginProjectSourceFolder'
      : role === 'config'
        ? 'nginProjectConfigFolder'
        : 'nginProjectGeneratedFolder';
  }
}

class ProjectBoundaryTreeItem extends vscode.TreeItem {
  readonly projectPath: string;
  readonly fsPath: string;

  constructor(project: ProjectManifest) {
    super(project.name, vscode.TreeItemCollapsibleState.None);
    this.projectPath = project.path;
    this.fsPath = project.directory;
    this.id = `ngin:project-boundary:${comparablePath(project.path)}`;
    this.description = 'project';
    this.tooltip = `${project.path}\nSeparate NGIN project`;
    this.iconPath = new vscode.ThemeIcon('project');
    this.contextValue = 'nginProjectBoundary';
    this.command = {
      command: 'ngin.setActiveProject',
      title: `Activate ${project.name}`,
      arguments: [{ projectPath: project.path, fsPath: project.path, role: 'manifest' } satisfies ProjectExplorerTarget]
    };
  }
}

class ProjectLinkTreeItem extends vscode.TreeItem {
  readonly projectPath: string;
  readonly fsPath: string;
  readonly role: 'source' | 'generated';

  constructor(projectPath: string, linkPath: string, role: 'source' | 'generated') {
    super(path.basename(linkPath), vscode.TreeItemCollapsibleState.None);
    this.projectPath = projectPath;
    this.fsPath = linkPath;
    this.role = role;
    this.id = `ngin:link:${comparablePath(projectPath)}:${comparablePath(linkPath)}:${role}`;
    this.tooltip = `${linkPath}\nSymbolic link`;
    this.iconPath = new vscode.ThemeIcon('symbolic-link');
    this.contextValue = role === 'generated' ? 'nginProjectGeneratedLink' : 'nginProjectSourceLink';
    this.command = {
      command: 'ngin.internal.revealPath',
      title: `Reveal ${path.basename(linkPath)}`,
      arguments: [linkPath]
    };
  }
}

function comparablePath(value: string): string {
  return comparableFilePath(value);
}

async function readDirectoryItems(
  snapshot: NginWorkspaceSnapshot,
  project: ProjectManifest,
  folderPath: string,
  role: 'source' | 'generated'
): Promise<Array<ProjectFolderTreeItem | ProjectFileTreeItem | ProjectBoundaryTreeItem | ProjectLinkTreeItem>> {
  try {
    const context: ProjectFileEnumerationContext = {
      project,
      projects: snapshot.workspace?.projects ?? [project],
      workspaceRoot: snapshot.workspace?.root ?? project.directory,
      outputDir: snapshot.outputDir,
      configuredOutputRoot: snapshot.workspace?.workspace.outputRoot
    };
    return (await enumerateProjectDirectory(context, folderPath, role)).map((entry) => {
      if (entry.boundaryProject) {
        return new ProjectBoundaryTreeItem(entry.boundaryProject);
      }
      if (entry.kind === 'directory') {
        return new ProjectFolderTreeItem(project.path, entry.path, role);
      }
      if (entry.kind === 'link') {
        return new ProjectLinkTreeItem(project.path, entry.path, role);
      }
      return new ProjectFileTreeItem(project.path, entry.path, role);
    });
  } catch {
    return [];
  }
}

async function buildActiveArtifactTreeItems(
  snapshot: NginWorkspaceSnapshot,
  project: ProjectManifest
): Promise<ProjectsTreeElement[]> {
  const outputDir = snapshot.outputDir;
  if (!outputDir) {
    return [];
  }

  const launchManifestPath = snapshot.launchManifestPath
    ?? computeLaunchManifestPath(outputDir, project.name, snapshot.context?.profile.name ?? project.defaultProfile ?? 'dev');
  const compileCommandsPath = computeCompileCommandsPath(outputDir);
  const items: ProjectsTreeElement[] = [];

  if (await pathExists(launchManifestPath)) {
    try {
      const launch = parseLaunchManifest(await readTextFile(launchManifestPath), launchManifestPath);
      const executable = launch.stagedFiles.find((file) => file.kind.toLowerCase() === 'executable');
      if (executable) {
        const executablePath = path.isAbsolute(executable.destination)
          ? executable.destination
          : path.resolve(outputDir, executable.destination);
        items.push(new ProjectFileTreeItem(project.path, executablePath, 'generated', 'Executable', executable.relativeDestination));
      }
    } catch {
      // Keep the remaining artifacts available even when launch metadata is incomplete.
    }

    items.push(new ProjectFolderTreeItem(project.path, outputDir, 'generated', 'Staged application folder'));
    items.push(new ProjectFileTreeItem(project.path, launchManifestPath, 'generated', 'Launch manifest', path.basename(launchManifestPath)));
  }

  if (await pathExists(compileCommandsPath)) {
    items.push(new ProjectFileTreeItem(project.path, compileCommandsPath, 'generated', 'compile_commands.json', 'compile database'));
  }
  return items;
}

type ProjectsTreeElement =
  | WorkspaceTreeItem
  | ProjectTreeItem
  | ProjectManifestTreeItem
  | ProjectGroupTreeItem
  | ProjectDependencyGroupTreeItem
  | ProjectDependencyTreeItem
  | ProjectInspectEntryTreeItem
  | ProjectFolderTreeItem
  | ProjectFileTreeItem
  | ProjectBoundaryTreeItem
  | ProjectLinkTreeItem;

class ProjectsTreeDataProvider implements vscode.TreeDataProvider<ProjectsTreeElement> {
  private readonly onDidChangeTreeDataEmitter = new vscode.EventEmitter<void>();
  private snapshot: NginWorkspaceSnapshot = { launchManifestExists: false, stagedCompileCommandsAvailable: false };

  readonly onDidChangeTreeData = this.onDidChangeTreeDataEmitter.event;

  setSnapshot(snapshot: NginWorkspaceSnapshot): void {
    this.snapshot = snapshot;
    this.onDidChangeTreeDataEmitter.fire();
  }

  refreshFilePath(filePath: string): void {
    if (this.snapshot.workspace?.projects.some((project) => isSameOrChildPath(filePath, project.directory))) {
      this.onDidChangeTreeDataEmitter.fire();
    }
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
      if (model.contextKind === 'project') {
        return model.projects.map((project) => new ProjectTreeItem(project));
      }
      return [new WorkspaceTreeItem(
        model.workspaceLabel,
        model.workspaceDescription,
        this.snapshot.workspace.manifestPath
      )];
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
        return readDirectoryItems(this.snapshot, project, project.directory, 'source');
      }
      if (element.group === 'dependencies') {
        const dependencies = model.dependenciesByProject.get(element.projectPath);
        const items: ProjectsTreeElement[] = [];
        if (dependencies?.projects.length) {
          items.push(new ProjectDependencyGroupTreeItem(element.projectPath, 'projects', 'Project References'));
        }
        if (dependencies?.direct.length) {
          items.push(new ProjectDependencyGroupTreeItem(element.projectPath, 'direct', 'Direct'));
        }
        if (dependencies?.transitive.length) {
          items.push(new ProjectDependencyGroupTreeItem(element.projectPath, 'transitive', 'Transitive'));
        }
        return items;
      }
      if (element.group === 'artifacts') {
        return this.getActiveArtifactChildren(project);
      }
      if (element.group === 'tooling' || element.group === 'launch' || element.group === 'publish' || element.group === 'problems') {
        const inspectModel = model.inspectByProject.get(element.projectPath);
        return (inspectModel?.entriesByGroup.get(element.group) ?? [])
          .map((entry) => new ProjectInspectEntryTreeItem(entry, element.projectPath));
      }
    }

    if (element instanceof ProjectDependencyGroupTreeItem) {
      const dependencies = model.dependenciesByProject.get(element.projectPath);
      if (!dependencies) {
        return [];
      }
      return dependencies[element.dependencyKind].map((dependency) => new ProjectDependencyTreeItem(dependency));
    }

    if (element instanceof ProjectDependencyTreeItem) {
      return (element.children ?? []).map((entry) => new ProjectInspectEntryTreeItem(entry, element.projectPath));
    }

    if (element instanceof ProjectInspectEntryTreeItem) {
      return (element.model.children ?? []).map((entry) => new ProjectInspectEntryTreeItem(entry, element.projectPath));
    }

    if (element instanceof ProjectFolderTreeItem) {
      const project = this.findProject(element.projectPath);
      if (!project) {
        return [];
      }
      return readDirectoryItems(this.snapshot, project, element.fsPath, element.role === 'generated' ? 'generated' : 'source');
    }

    return [];
  }

  private createChildTreeItem(model: ProjectTreeChildModel): ProjectsTreeElement {
    if (model.kind === 'manifest') {
      return new ProjectManifestTreeItem(model);
    }
    return new ProjectGroupTreeItem(model);
  }

  private findProject(projectPath: string): ProjectManifest | undefined {
    return this.snapshot.workspace?.projects.find((project) => comparablePath(project.path) === comparablePath(projectPath));
  }

  private async getActiveArtifactChildren(project: ProjectManifest): Promise<ProjectsTreeElement[]> {
    return buildActiveArtifactTreeItems(this.snapshot, project);
  }

}

class ActiveProjectTreeDataProvider implements vscode.TreeDataProvider<ProjectsTreeElement> {
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
    const model = buildActiveProjectTreeModel(this.snapshot);
    if (!element) {
      return model.groups.map((group) => new ProjectGroupTreeItem(group));
    }

    if (element instanceof ProjectGroupTreeItem) {
      const project = this.snapshot.context?.project;
      if (!project || comparablePath(project.path) !== comparablePath(element.projectPath)) {
        return [];
      }
      if (element.group === 'artifacts') {
        return buildActiveArtifactTreeItems(this.snapshot, project);
      }
      if (element.group === 'tooling' || element.group === 'launch' || element.group === 'publish' || element.group === 'problems') {
        return (model.inspect?.entriesByGroup.get(element.group) ?? [])
          .map((entry) => new ProjectInspectEntryTreeItem(entry, project.path));
      }
    }

    if (element instanceof ProjectInspectEntryTreeItem) {
      return (element.model.children ?? [])
        .map((entry) => new ProjectInspectEntryTreeItem(entry, element.projectPath));
    }

    if (element instanceof ProjectFolderTreeItem && element.role === 'generated') {
      const project = this.snapshot.context?.project;
      if (!project) {
        return [];
      }
      return readDirectoryItems(this.snapshot, project, element.fsPath, 'generated');
    }

    return [];
  }
}

export class NginSidebarController implements vscode.Disposable {
  private readonly projectsProvider = new ProjectsTreeDataProvider();
  private readonly activeProjectProvider = new ActiveProjectTreeDataProvider();
  private readonly projectsTreeView: vscode.TreeView<ProjectsTreeElement>;
  private readonly activeProjectTreeView: vscode.TreeView<ProjectsTreeElement>;
  private readonly fileWatcher: vscode.FileSystemWatcher;
  private readonly fileWatcherSubscriptions: vscode.Disposable[];

  constructor() {
    this.projectsTreeView = vscode.window.createTreeView('nginWorkspace', {
      treeDataProvider: this.projectsProvider,
      showCollapseAll: true
    });
    this.activeProjectTreeView = vscode.window.createTreeView('nginActiveProject', {
      treeDataProvider: this.activeProjectProvider,
      showCollapseAll: true
    });
    this.fileWatcher = vscode.workspace.createFileSystemWatcher('**/*');
    this.fileWatcherSubscriptions = [
      this.fileWatcher.onDidCreate((uri) => this.projectsProvider.refreshFilePath(uri.fsPath)),
      this.fileWatcher.onDidDelete((uri) => this.projectsProvider.refreshFilePath(uri.fsPath)),
      this.fileWatcher.onDidChange((uri) => this.projectsProvider.refreshFilePath(uri.fsPath))
    ];
  }

  dispose(): void {
    vscode.Disposable.from(...this.fileWatcherSubscriptions).dispose();
    this.fileWatcher.dispose();
    this.projectsTreeView.dispose();
    this.activeProjectTreeView.dispose();
  }

  refresh(snapshot: NginWorkspaceSnapshot): void {
    this.projectsProvider.setSnapshot(snapshot);
    this.activeProjectProvider.setSnapshot(snapshot);

    const noWorkspaceMessage = 'Open a folder containing a .ngin or .nginproj manifest.';
    this.projectsTreeView.message = snapshot.workspace ? undefined : noWorkspaceMessage;
    this.activeProjectTreeView.description = snapshot.context
      ? `${snapshot.context.project.name} · ${snapshot.context.profile.name}`
      : undefined;
    this.activeProjectTreeView.message = snapshot.workspace && !snapshot.context
      ? 'Select an NGIN project and profile.'
      : undefined;
  }
}
