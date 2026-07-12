import * as path from 'node:path';
import { promises as fs } from 'node:fs';
import * as vscode from 'vscode';
import { computeCompileCommandsPath } from '../core/compileCommands';
import { pathExists, readTextFile } from '../core/discovery';
import { computeLaunchManifestPath } from '../core/helpers';
import { ProjectManifest } from '../core/types';
import { parseLaunchManifest } from '../core/xml';
import { NginWorkspaceSnapshot } from '../state/workspaceState';
import {
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

  constructor(public readonly model: ProjectTreeInspectEntryModel, projectPath?: string) {
    super(
      model.label,
      model.children?.length ? vscode.TreeItemCollapsibleState.Collapsed : vscode.TreeItemCollapsibleState.None
    );
    this.description = model.description;
    this.tooltip = model.tooltip;
    this.targetPath = model.targetPath;
    this.fsPath = model.targetPath;
    this.projectPath = projectPath;
    this.explainIdentity = model.explainIdentity;
    this.iconPath = new vscode.ThemeIcon(model.icon ?? 'symbol-property');
    this.contextValue = model.context
      ? `nginProjectInspectEntry.${model.context}${model.targetPath ? '.openable' : ''}`
      : model.targetPath ? 'nginProjectInspectEntry.openable' : 'nginProjectInspectEntry';
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

function comparablePath(value: string): string {
  const normalized = path.normalize(value);
  return process.platform === 'win32' ? normalized.toLowerCase() : normalized;
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

type ProjectsTreeElement =
  | WorkspaceTreeItem
  | ProjectTreeItem
  | ProjectManifestTreeItem
  | ProjectGroupTreeItem
  | ProjectDependencyGroupTreeItem
  | ProjectDependencyTreeItem
  | ProjectInspectEntryTreeItem
  | ProjectFolderTreeItem
  | ProjectFileTreeItem;

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
      if (element.group === 'tooling' || element.group === 'launch' || element.group === 'problems') {
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
      return readDirectoryItems(element.projectPath, element.fsPath, element.role === 'generated' ? 'generated' : 'source');
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
    const outputDir = this.snapshot.outputDir;
    if (!outputDir) {
      return [];
    }

    const launchManifestPath = this.snapshot.launchManifestPath
      ?? computeLaunchManifestPath(outputDir, project.name, this.snapshot.context?.profile.name ?? project.defaultProfile ?? 'dev');
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
