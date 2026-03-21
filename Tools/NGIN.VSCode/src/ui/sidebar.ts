import * as vscode from 'vscode';
import { NginWorkspaceSnapshot } from '../state/workspaceState';
import {
  buildOverviewSections,
  buildProjectTreeModels,
  OverviewEntryModel,
  OverviewSectionModel,
  ProjectTreeConfigurationModel,
  ProjectTreeProjectModel,
} from './models';

class OverviewSectionTreeItem extends vscode.TreeItem {
  constructor(public readonly model: OverviewSectionModel) {
    super(model.label, vscode.TreeItemCollapsibleState.Expanded);
    this.contextValue = 'nginOverviewSection';
    this.iconPath = new vscode.ThemeIcon('list-tree');
  }
}

class OverviewEntryTreeItem extends vscode.TreeItem {
  constructor(public readonly model: OverviewEntryModel) {
    super(model.label, vscode.TreeItemCollapsibleState.None);
    this.description = model.description;
    this.tooltip = model.tooltip;
    this.contextValue = model.contextValue;
    this.iconPath = model.icon ? new vscode.ThemeIcon(model.icon) : undefined;
    if (model.command) {
      this.command = {
        command: model.command,
        title: model.label,
        arguments: model.arguments
      };
    }
  }
}

class OverviewTreeDataProvider implements vscode.TreeDataProvider<OverviewSectionTreeItem | OverviewEntryTreeItem> {
  private readonly onDidChangeTreeDataEmitter = new vscode.EventEmitter<void>();
  private snapshot: NginWorkspaceSnapshot = { launchManifestExists: false, stagedCompileCommandsAvailable: false };

  readonly onDidChangeTreeData = this.onDidChangeTreeDataEmitter.event;

  setSnapshot(snapshot: NginWorkspaceSnapshot): void {
    this.snapshot = snapshot;
    this.onDidChangeTreeDataEmitter.fire();
  }

  getTreeItem(element: OverviewSectionTreeItem | OverviewEntryTreeItem): vscode.TreeItem {
    return element;
  }

  getChildren(element?: OverviewSectionTreeItem | OverviewEntryTreeItem): vscode.ProviderResult<(OverviewSectionTreeItem | OverviewEntryTreeItem)[]> {
    if (element instanceof OverviewEntryTreeItem) {
      return [];
    }

    if (!element) {
      return buildOverviewSections(this.snapshot).map((section) => new OverviewSectionTreeItem(section));
    }

    return element.model.children.map((child) => new OverviewEntryTreeItem(child));
  }
}

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

class ProjectTreeItem extends vscode.TreeItem {
  readonly projectPath: string;

  constructor(model: ProjectTreeProjectModel) {
    super(model.label, vscode.TreeItemCollapsibleState.Collapsed);
    this.projectPath = model.projectPath;
    this.description = model.description;
    this.tooltip = model.tooltip;
    this.iconPath = new vscode.ThemeIcon(model.selected ? 'pass-filled' : 'project');
    this.contextValue = 'nginProject';
    this.command = {
      command: 'ngin.selectProject',
      title: model.label,
      arguments: [{ projectPath: model.projectPath }]
    };
  }
}

class ConfigurationTreeItem extends vscode.TreeItem {
  readonly projectPath: string;
  readonly configurationName: string;

  constructor(model: ProjectTreeConfigurationModel) {
    super(model.label, vscode.TreeItemCollapsibleState.None);
    this.projectPath = model.projectPath;
    this.configurationName = model.configurationName;
    this.description = model.description;
    this.tooltip = model.tooltip;
    this.iconPath = new vscode.ThemeIcon(model.selected ? 'play-circle' : 'symbol-enum');
    this.contextValue = 'nginConfiguration';
    this.command = {
      command: 'ngin.selectConfiguration',
      title: model.label,
      arguments: [{ projectPath: model.projectPath, configurationName: model.configurationName }]
    };
  }
}

class ProjectsTreeDataProvider implements vscode.TreeDataProvider<WorkspaceTreeItem | ProjectTreeItem | ConfigurationTreeItem> {
  private readonly onDidChangeTreeDataEmitter = new vscode.EventEmitter<void>();
  private snapshot: NginWorkspaceSnapshot = { launchManifestExists: false, stagedCompileCommandsAvailable: false };

  readonly onDidChangeTreeData = this.onDidChangeTreeDataEmitter.event;

  setSnapshot(snapshot: NginWorkspaceSnapshot): void {
    this.snapshot = snapshot;
    this.onDidChangeTreeDataEmitter.fire();
  }

  getTreeItem(element: WorkspaceTreeItem | ProjectTreeItem | ConfigurationTreeItem): vscode.TreeItem {
    return element;
  }

  getChildren(element?: WorkspaceTreeItem | ProjectTreeItem | ConfigurationTreeItem): vscode.ProviderResult<(WorkspaceTreeItem | ProjectTreeItem | ConfigurationTreeItem)[]> {
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
      return (model.configurationsByProject.get(element.projectPath) ?? []).map((configuration) => new ConfigurationTreeItem(configuration));
    }

    return [];
  }
}

export class NginSidebarController implements vscode.Disposable {
  private readonly overviewProvider = new OverviewTreeDataProvider();
  private readonly projectsProvider = new ProjectsTreeDataProvider();
  private readonly overviewTreeView: vscode.TreeView<OverviewSectionTreeItem | OverviewEntryTreeItem>;
  private readonly projectsTreeView: vscode.TreeView<WorkspaceTreeItem | ProjectTreeItem | ConfigurationTreeItem>;

  constructor() {
    this.overviewTreeView = vscode.window.createTreeView('nginOverview', {
      treeDataProvider: this.overviewProvider,
      showCollapseAll: true
    });
    this.projectsTreeView = vscode.window.createTreeView('nginProjects', {
      treeDataProvider: this.projectsProvider,
      showCollapseAll: true
    });
  }

  dispose(): void {
    this.overviewTreeView.dispose();
    this.projectsTreeView.dispose();
  }

  refresh(snapshot: NginWorkspaceSnapshot): void {
    this.overviewProvider.setSnapshot(snapshot);
    this.projectsProvider.setSnapshot(snapshot);

    const noWorkspaceMessage = 'Open a folder with a .ngin workspace manifest.';
    this.overviewTreeView.message = snapshot.workspace ? undefined : noWorkspaceMessage;
    this.projectsTreeView.message = snapshot.workspace ? undefined : noWorkspaceMessage;
  }
}
