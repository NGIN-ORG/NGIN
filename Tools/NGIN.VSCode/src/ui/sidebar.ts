import * as path from 'node:path';
import * as vscode from 'vscode';
import { computeCompileCommandsPath } from '../core/compileCommands';
import { pathExists, readTextFile } from '../core/discovery';
import { computeLaunchManifestPath } from '../core/helpers';
import { GraphEditorFile, ProjectManifest } from '../core/types';
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
  isSameOrChildPath,
  ProjectFileEnumerationContext,
  ProjectFileTreeService
} from './projectFiles';
import { FileMembership, ProjectMembershipIndex } from './membership';

class WorkspaceTreeItem extends vscode.TreeItem {
  constructor(label: string, description: string, readonly manifestPath: string) {
    super(label, vscode.TreeItemCollapsibleState.Expanded);
    this.description = description;
    this.tooltip = manifestPath;
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
  role?: 'manifest' | 'source' | 'config' | 'generated' | 'external';
  isDirectory?: boolean;
}

class ProjectTreeItem extends vscode.TreeItem {
  readonly projectPath: string;
  readonly fsPath: string;
  readonly role = 'manifest' as const;
  readonly isDirectory = true;

  constructor(model: ProjectTreeProjectModel) {
    super(model.label, model.selected ? vscode.TreeItemCollapsibleState.Expanded : vscode.TreeItemCollapsibleState.Collapsed);
    this.id = `ngin:project:${comparablePath(model.projectPath)}`;
    this.projectPath = model.projectPath;
    this.fsPath = model.projectDirectory;
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
  readonly role: 'source' | 'config' | 'generated' | 'external';
  readonly explainIdentity?: string;
  readonly declaringManifestPath?: string;
  readonly membershipKind?: string;
  readonly membershipRole?: string;

  constructor(
    projectPath: string,
    filePath: string,
    role: 'source' | 'config' | 'generated' | 'external',
    label?: string,
    description?: string,
    membership?: FileMembership
  ) {
    super(label ?? path.basename(filePath), vscode.TreeItemCollapsibleState.None);
    this.id = `ngin:file:${comparablePath(projectPath)}:${comparablePath(filePath)}:${role}`;
    this.projectPath = projectPath;
    this.fsPath = filePath;
    this.role = role;
    this.explainIdentity = membership?.file?.explainIdentity;
    this.declaringManifestPath = membership?.file?.manifestPath;
    this.membershipKind = membership?.file?.kind;
    this.membershipRole = membership?.file?.role;
    const state = membership?.state;
    this.description = description ?? (
      state === 'unselected' ? 'not selected' :
        state === 'unknown' ? 'membership unknown' :
          state === 'external' ? membership.file?.ownerName :
            state === 'generated' && membership.file && !membership.file.exists ? 'not generated yet' :
              undefined
    );
    this.tooltip = membershipTooltip(filePath, membership);
    const decorationState =
      state === 'generated' && membership?.file && !membership.file.exists
        ? 'generated-missing'
        : state;
    this.resourceUri = vscode.Uri.from({
      scheme: 'ngin-solution',
      path: filePath.replace(/\\/g, '/'),
      query: decorationState ?? ''
    });
    if (role === 'generated') {
      this.iconPath = new vscode.ThemeIcon('file-binary');
    }
    this.contextValue = role === 'source'
      ? `nginProjectSourceFile.${state ?? 'unknown'}`
      : role === 'config'
        ? `nginProjectConfigFile.${state ?? 'selected'}`
        : role === 'external'
          ? 'nginProjectExternalFile'
          : 'nginProjectGeneratedFile';
    if (membership?.file?.exists !== false) {
      this.command = {
        command: 'ngin.internal.openPath',
        title: String(label ?? path.basename(filePath)),
        arguments: [filePath]
      };
    }
  }
}

function membershipTooltip(filePath: string, membership?: FileMembership): vscode.MarkdownString | string {
  if (!membership) {
    return filePath;
  }
  const lines = [
    filePath,
    membership.profileName ? `Profile: ${membership.profileName}` : undefined,
    `Membership: ${membership.state === 'unknown' ? 'unknown (graph unavailable)' : membership.state}`,
    membership.file?.kind ? `Kind: ${membership.file.kind}${membership.file.role ? ` / ${membership.file.role}` : ''}` : undefined,
    membership.file?.ownerName ? `Owner: ${membership.file.ownerKind} ${membership.file.ownerName}` : undefined,
    membership.file?.manifestPath ? `Declared by: ${membership.file.manifestPath}` : undefined,
    membership.file?.provenance.reason ? `Reason: ${membership.file.provenance.reason}` : undefined
  ].filter((line): line is string => Boolean(line));
  return lines.join('\n');
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

class ProjectMessageTreeItem extends vscode.TreeItem {
  constructor(label: string, icon = 'info', command?: vscode.Command) {
    super(label, vscode.TreeItemCollapsibleState.None);
    this.id = `ngin:message:${label}`;
    this.iconPath = new vscode.ThemeIcon(icon);
    this.contextValue = 'nginProjectMessage';
    this.command = command;
  }
}

class ProjectDirectoryErrorTreeItem extends vscode.TreeItem {
  constructor(readonly fsPath: string) {
    super('Unable to read folder — select to retry', vscode.TreeItemCollapsibleState.None);
    this.id = `ngin:directory-error:${comparablePath(fsPath)}`;
    this.iconPath = new vscode.ThemeIcon('warning');
    this.tooltip = fsPath;
    this.contextValue = 'nginProjectDirectoryError';
    this.command = { command: 'ngin.refresh', title: 'Retry' };
  }
}

class NginSolutionDecorationProvider implements vscode.FileDecorationProvider {
  provideFileDecoration(uri: vscode.Uri): vscode.FileDecoration | undefined {
    if (uri.scheme !== 'ngin-solution') {
      return undefined;
    }
    switch (uri.query) {
      case 'unselected':
        return new vscode.FileDecoration(undefined, 'Not selected by the active NGIN profile', new vscode.ThemeColor('disabledForeground'));
      case 'generated-missing':
        return new vscode.FileDecoration('!', 'Declared output has not been generated yet', new vscode.ThemeColor('list.warningForeground'));
      case 'external':
        return new vscode.FileDecoration('↗', 'Selected external input', new vscode.ThemeColor('textLink.foreground'));
      case 'unknown':
        return new vscode.FileDecoration(undefined, 'NGIN membership is unavailable');
      default:
        return undefined;
    }
  }
}

function comparablePath(value: string): string {
  return comparableFilePath(value);
}

async function readDirectoryItems(
  snapshot: NginWorkspaceSnapshot,
  project: ProjectManifest,
  folderPath: string,
  role: 'source' | 'generated',
  fileTree: ProjectFileTreeService,
  membership: ProjectMembershipIndex,
  inputsOnly: boolean
): Promise<Array<ProjectFolderTreeItem | ProjectFileTreeItem | ProjectBoundaryTreeItem | ProjectLinkTreeItem | ProjectDirectoryErrorTreeItem>> {
  try {
    const configuredExcludes = vscode.workspace.getConfiguration('ngin').get<Record<string, boolean>>('solutionExplorer.exclude') ?? {};
    const filesExcludes = vscode.workspace.getConfiguration('files', vscode.Uri.file(project.directory)).get<Record<string, boolean>>('exclude') ?? {};
    const context: ProjectFileEnumerationContext = {
      project,
      projects: snapshot.workspace?.projects ?? [project],
      workspaceRoot: snapshot.workspace?.root ?? project.directory,
      outputDir: snapshot.outputDir,
      configuredOutputRoot: snapshot.workspace?.workspace.outputRoot,
      excludePatterns: [
        ...Object.entries(configuredExcludes).filter(([, enabled]) => enabled).map(([pattern]) => pattern),
        ...Object.entries(filesExcludes).filter(([, enabled]) => enabled).map(([pattern]) => pattern)
      ]
    };
    const items: Array<ProjectFolderTreeItem | ProjectFileTreeItem | ProjectBoundaryTreeItem | ProjectLinkTreeItem> = [];
    for (const entry of await fileTree.enumerate(context, folderPath, role)) {
      if (entry.boundaryProject) {
        items.push(new ProjectBoundaryTreeItem(entry.boundaryProject));
        continue;
      }
      if (entry.kind === 'directory') {
        if (!inputsOnly || role === 'generated' || membership.containsSelectedDescendant(project, entry.path)) {
          items.push(new ProjectFolderTreeItem(project.path, entry.path, role));
        }
        continue;
      }
      if (entry.kind === 'link') {
        if (!inputsOnly) {
          items.push(new ProjectLinkTreeItem(project.path, entry.path, role));
        }
        continue;
      }
      const fileMembership = role === 'source'
        ? membership.membership(project, entry.path)
        : { state: 'generated' as const };
      if (!inputsOnly || role === 'generated' || fileMembership.state === 'selected') {
        const itemRole = role === 'source' &&
          (fileMembership.file?.kind === 'Config' || fileMembership.file?.kind === 'Content')
          ? 'config'
          : role;
        items.push(new ProjectFileTreeItem(project.path, entry.path, itemRole, undefined, undefined, fileMembership));
      }
    }
    return items;
  } catch {
    return [new ProjectDirectoryErrorTreeItem(folderPath)];
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
  | ProjectLinkTreeItem
  | ProjectMessageTreeItem
  | ProjectDirectoryErrorTreeItem;

const NGIN_TREE_MIME = 'application/vnd.code.tree.nginWorkspace';
const URI_LIST_MIME = 'text/uri-list';

class NginSolutionDragAndDropController implements vscode.TreeDragAndDropController<ProjectsTreeElement> {
  readonly dragMimeTypes = [NGIN_TREE_MIME];
  readonly dropMimeTypes = [NGIN_TREE_MIME, URI_LIST_MIME];

  constructor(private readonly projectDirectory: (projectPath: string) => string | undefined) {}

  handleDrag(source: readonly ProjectsTreeElement[], dataTransfer: vscode.DataTransfer): void {
    const paths = source.flatMap((element) => {
      if ((element instanceof ProjectFileTreeItem || element instanceof ProjectFolderTreeItem) &&
          (element.role === 'source' || element.role === 'config')) {
        return [{
          projectPath: element.projectPath,
          fsPath: element.fsPath,
          role: element.role,
          isDirectory: element instanceof ProjectFolderTreeItem
        }];
      }
      return [];
    });
    if (paths.length > 0) {
      dataTransfer.set(NGIN_TREE_MIME, new vscode.DataTransferItem(JSON.stringify(paths)));
    }
  }

  async handleDrop(target: ProjectsTreeElement | undefined, dataTransfer: vscode.DataTransfer): Promise<void> {
    if (!target) return;
    const dropTarget =
      target instanceof ProjectFolderTreeItem && (target.role === 'source' || target.role === 'config')
        ? { destination: target.fsPath, projectPath: target.projectPath }
        : target instanceof ProjectGroupTreeItem && target.group === 'files'
          ? { destination: this.projectDirectory(target.projectPath), projectPath: target.projectPath }
          : undefined;
    if (!dropTarget?.destination) return;

    const internal = dataTransfer.get(NGIN_TREE_MIME);
    if (internal) {
      const sources = JSON.parse(await internal.asString()) as ProjectExplorerTarget[];
      await vscode.commands.executeCommand('ngin.moveProjectItems', {
        sources,
        destination: dropTarget.destination
      });
      return;
    }

    const uriList = dataTransfer.get(URI_LIST_MIME);
    if (!uriList) return;
    const sources = (await uriList.asString())
      .split(/\r?\n/)
      .map((value) => value.trim())
      .filter((value) => value && !value.startsWith('#'))
      .map((value) => vscode.Uri.parse(value))
      .filter((uri) => uri.scheme === 'file')
      .map((uri) => uri.fsPath);
    if (sources.length > 0) {
      await vscode.commands.executeCommand('ngin.copyExternalItems', {
        sources,
        destination: dropTarget.destination,
        projectPath: dropTarget.projectPath
      });
    }
  }
}

class ProjectsTreeDataProvider implements vscode.TreeDataProvider<ProjectsTreeElement> {
  private readonly onDidChangeTreeDataEmitter = new vscode.EventEmitter<ProjectsTreeElement | undefined>();
  private readonly fileTree = new ProjectFileTreeService();
  private snapshot: NginWorkspaceSnapshot = { launchManifestExists: false, stagedCompileCommandsAvailable: false };
  private generation = 0;

  readonly onDidChangeTreeData = this.onDidChangeTreeDataEmitter.event;

  constructor(private readonly inputsOnly: () => boolean) {}

  setSnapshot(snapshot: NginWorkspaceSnapshot): void {
    this.generation += 1;
    this.snapshot = snapshot;
    this.fileTree.clear();
    this.onDidChangeTreeDataEmitter.fire(undefined);
  }

  refreshFilePath(filePath: string): void {
    const project = [...(this.snapshot.workspace?.projects ?? [])]
      .filter((candidate) => isSameOrChildPath(filePath, candidate.directory))
      .sort((left, right) => right.directory.length - left.directory.length)[0];
    if (!project) return;
    this.generation += 1;
    this.fileTree.invalidatePath(filePath);
    const parentPath = path.dirname(filePath);
    this.onDidChangeTreeDataEmitter.fire(
      comparablePath(parentPath) === comparablePath(project.directory)
        ? new ProjectGroupTreeItem({
            kind: 'group',
            id: `${project.path}:files`,
            label: 'Project Files',
            icon: 'files',
            projectPath: project.path,
            group: 'files'
          })
        : new ProjectFolderTreeItem(project.path, parentPath, 'source')
    );
  }

  getTreeItem(element: ProjectsTreeElement): vscode.TreeItem {
    return element;
  }

  async getChildren(element?: ProjectsTreeElement): Promise<ProjectsTreeElement[]> {
    const generation = this.generation;
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
        if (this.inputsOnly() &&
            comparablePath(project.path) !== comparablePath(this.snapshot.context?.project.path ?? '')) {
          return [new ProjectMessageTreeItem('Activate project to resolve inputs')];
        }
        const children = await readDirectoryItems(
          this.snapshot,
          project,
          project.directory,
          'source',
          this.fileTree,
          this.membershipIndex(),
          this.inputsOnly()
        );
        if (generation !== this.generation) return [];
        return children.length > 0
          ? children
          : [new ProjectMessageTreeItem(
              'Project Files is empty — create a file',
              'new-file',
              {
                command: 'ngin.projectNewFile',
                title: 'New File',
                arguments: [{ projectPath: project.path, fsPath: project.directory, role: 'source', isDirectory: true } satisfies ProjectExplorerTarget]
              }
            )];
      }
      if (element.group === 'externalInputs') {
        return this.membershipIndex().externalFiles(project).map((file) =>
          this.graphFileItem(project, file, 'external')
        );
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
      const children = await readDirectoryItems(
        this.snapshot,
        project,
        element.fsPath,
        element.role === 'generated' ? 'generated' : 'source',
        this.fileTree,
        this.membershipIndex(),
        this.inputsOnly()
      );
      return generation === this.generation ? children : [];
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

  getParent(element: ProjectsTreeElement): ProjectsTreeElement | undefined {
    const model = buildProjectTreeModels(this.snapshot);
    if (element instanceof ProjectTreeItem) {
      return model.contextKind === 'workspace' && model.workspaceLabel && model.workspaceDescription && this.snapshot.workspace
        ? new WorkspaceTreeItem(model.workspaceLabel, model.workspaceDescription, this.snapshot.workspace.manifestPath)
        : undefined;
    }
    if (element instanceof ProjectManifestTreeItem || element instanceof ProjectGroupTreeItem) {
      const projectModel = model.projects.find((project) => comparablePath(project.projectPath) === comparablePath(element.projectPath));
      return projectModel ? new ProjectTreeItem(projectModel) : undefined;
    }
    if (element instanceof ProjectFileTreeItem || element instanceof ProjectFolderTreeItem || element instanceof ProjectLinkTreeItem) {
      const project = this.findProject(element.projectPath);
      if (!project) return undefined;
      if (element instanceof ProjectFileTreeItem && element.role === 'external') {
        return new ProjectGroupTreeItem({
          kind: 'group',
          id: `${project.path}:external-inputs`,
          label: 'External Inputs',
          icon: 'link-external',
          projectPath: project.path,
          group: 'externalInputs'
        });
      }
      const parentPath = path.dirname(element.fsPath);
      if (comparablePath(parentPath) === comparablePath(project.directory)) {
        return new ProjectGroupTreeItem({
          kind: 'group',
          id: `${project.path}:files`,
          label: 'Project Files',
          icon: 'files',
          projectPath: project.path,
          group: 'files'
        });
      }
      return new ProjectFolderTreeItem(project.path, parentPath, 'source');
    }
    return undefined;
  }

  fileItemForReveal(project: ProjectManifest, filePath: string): ProjectFileTreeItem {
    return new ProjectFileTreeItem(
      project.path,
      filePath,
      'source',
      undefined,
      undefined,
      this.membershipIndex().membership(project, filePath)
    );
  }

  projectDirectory(projectPath: string): string | undefined {
    return this.findProject(projectPath)?.directory;
  }

  private membershipIndex(): ProjectMembershipIndex {
    return new ProjectMembershipIndex(this.snapshot.inspectGraph, this.snapshot.context?.project);
  }

  private graphFileItem(
    project: ProjectManifest,
    file: GraphEditorFile,
    role: 'external' | 'generated'
  ): ProjectFileTreeItem {
    const membership = this.membershipIndex();
    return new ProjectFileTreeItem(
      project.path,
      file.absolutePath,
      role,
      path.basename(file.absolutePath),
      undefined,
      { state: role, file, profileName: membership.profileName }
    );
  }

}

class ActiveProjectTreeDataProvider implements vscode.TreeDataProvider<ProjectsTreeElement> {
  private readonly onDidChangeTreeDataEmitter = new vscode.EventEmitter<void>();
  private readonly fileTree = new ProjectFileTreeService(64);
  private snapshot: NginWorkspaceSnapshot = { launchManifestExists: false, stagedCompileCommandsAvailable: false };
  private generation = 0;

  readonly onDidChangeTreeData = this.onDidChangeTreeDataEmitter.event;

  setSnapshot(snapshot: NginWorkspaceSnapshot): void {
    this.generation += 1;
    this.snapshot = snapshot;
    this.fileTree.clear();
    this.onDidChangeTreeDataEmitter.fire();
  }

  getTreeItem(element: ProjectsTreeElement): vscode.TreeItem {
    return element;
  }

  async getChildren(element?: ProjectsTreeElement): Promise<ProjectsTreeElement[]> {
    const generation = this.generation;
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
      if (element.group === 'generatedInputs') {
        const membership = new ProjectMembershipIndex(this.snapshot.inspectGraph, project);
        return membership.generatedFiles().map((file) =>
          new ProjectFileTreeItem(
            project.path,
            file.absolutePath,
            'generated',
            path.basename(file.absolutePath),
            undefined,
            { state: 'generated', file, profileName: membership.profileName }
          )
        );
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
      const children = await readDirectoryItems(
        this.snapshot,
        project,
        element.fsPath,
        'generated',
        this.fileTree,
        new ProjectMembershipIndex(this.snapshot.inspectGraph, project),
        false
      );
      return generation === this.generation ? children : [];
    }

    return [];
  }
}

export class NginSidebarController implements vscode.Disposable {
  private readonly projectsProvider: ProjectsTreeDataProvider;
  private readonly activeProjectProvider = new ActiveProjectTreeDataProvider();
  private readonly projectsTreeView: vscode.TreeView<ProjectsTreeElement>;
  private readonly activeProjectTreeView: vscode.TreeView<ProjectsTreeElement>;
  private readonly fileWatcher: vscode.FileSystemWatcher;
  private readonly fileWatcherSubscriptions: vscode.Disposable[];
  private readonly subscriptions: vscode.Disposable[];
  private readonly fileRefreshTimers = new Map<string, ReturnType<typeof setTimeout>>();
  private snapshot: NginWorkspaceSnapshot = { launchManifestExists: false, stagedCompileCommandsAvailable: false };
  private inputsOnly: boolean;
  private followActiveEditor: boolean;

  constructor(
    private readonly context: vscode.ExtensionContext,
    private readonly activateProjectForFollow?: (project: ProjectManifest, uri: vscode.Uri) => Promise<void>
  ) {
    this.inputsOnly = context.workspaceState.get<boolean>('ngin.solutionExplorer.inputsOnly', false);
    this.followActiveEditor = context.workspaceState.get<boolean>('ngin.solutionExplorer.followActiveEditor', false);
    this.projectsProvider = new ProjectsTreeDataProvider(() => this.inputsOnly);
    this.projectsTreeView = vscode.window.createTreeView('nginWorkspace', {
      treeDataProvider: this.projectsProvider,
      showCollapseAll: true,
      canSelectMany: true,
      dragAndDropController: new NginSolutionDragAndDropController(
        (projectPath) => this.projectsProvider.projectDirectory(projectPath)
      )
    });
    this.activeProjectTreeView = vscode.window.createTreeView('nginActiveProject', {
      treeDataProvider: this.activeProjectProvider,
      showCollapseAll: true
    });
    this.fileWatcher = vscode.workspace.createFileSystemWatcher('**/*');
    this.fileWatcherSubscriptions = [
      this.fileWatcher.onDidCreate((uri) => this.scheduleFileRefresh(uri.fsPath)),
      this.fileWatcher.onDidDelete((uri) => this.scheduleFileRefresh(uri.fsPath)),
      this.fileWatcher.onDidChange((uri) => this.scheduleFileRefresh(uri.fsPath))
    ];
    this.subscriptions = [
      vscode.window.registerFileDecorationProvider(new NginSolutionDecorationProvider()),
      vscode.window.onDidChangeActiveTextEditor((editor) => {
        if (this.followActiveEditor && editor?.document.uri.scheme === 'file') {
          void this.revealUri(editor.document.uri, true);
        }
      }),
      vscode.workspace.onDidChangeConfiguration((event) => {
        if (event.affectsConfiguration('ngin.solutionExplorer.exclude') ||
            event.affectsConfiguration('files.exclude')) {
          this.projectsProvider.setSnapshot(this.snapshot);
        }
      })
    ];
    void vscode.commands.executeCommand('setContext', 'ngin.solutionExplorer.inputsOnly', this.inputsOnly);
    void vscode.commands.executeCommand('setContext', 'ngin.solutionExplorer.followActiveEditor', this.followActiveEditor);
  }

  dispose(): void {
    for (const timer of this.fileRefreshTimers.values()) clearTimeout(timer);
    this.fileRefreshTimers.clear();
    vscode.Disposable.from(...this.subscriptions).dispose();
    vscode.Disposable.from(...this.fileWatcherSubscriptions).dispose();
    this.fileWatcher.dispose();
    this.projectsTreeView.dispose();
    this.activeProjectTreeView.dispose();
  }

  private scheduleFileRefresh(filePath: string): void {
    const key = comparablePath(path.dirname(filePath));
    const existing = this.fileRefreshTimers.get(key);
    if (existing) clearTimeout(existing);
    this.fileRefreshTimers.set(key, setTimeout(() => {
      this.fileRefreshTimers.delete(key);
      this.projectsProvider.refreshFilePath(filePath);
    }, 75));
  }

  refresh(snapshot: NginWorkspaceSnapshot): void {
    this.snapshot = snapshot;
    void vscode.commands.executeCommand('setContext', 'ngin.hasWorkspace', Boolean(snapshot.workspace));
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

  async toggleInputsOnly(): Promise<void> {
    this.inputsOnly = !this.inputsOnly;
    await this.context.workspaceState.update('ngin.solutionExplorer.inputsOnly', this.inputsOnly);
    await vscode.commands.executeCommand('setContext', 'ngin.solutionExplorer.inputsOnly', this.inputsOnly);
    this.projectsProvider.setSnapshot(this.snapshot);
  }

  async toggleFollowActiveEditor(): Promise<void> {
    this.followActiveEditor = !this.followActiveEditor;
    await this.context.workspaceState.update('ngin.solutionExplorer.followActiveEditor', this.followActiveEditor);
    await vscode.commands.executeCommand('setContext', 'ngin.solutionExplorer.followActiveEditor', this.followActiveEditor);
    if (this.followActiveEditor && vscode.window.activeTextEditor?.document.uri.scheme === 'file') {
      await this.revealUri(vscode.window.activeTextEditor.document.uri, true);
    }
  }

  async revealActiveFile(): Promise<void> {
    const uri = vscode.window.activeTextEditor?.document.uri;
    if (!uri || uri.scheme !== 'file') {
      void vscode.window.showInformationMessage('The active editor is not a local project file.');
      return;
    }
    await this.revealUri(uri, false);
  }

  private async revealUri(uri: vscode.Uri, allowActivation: boolean): Promise<void> {
    const project = [...(this.snapshot.workspace?.projects ?? [])]
      .filter((candidate) => isSameOrChildPath(uri.fsPath, candidate.directory))
      .sort((left, right) => right.directory.length - left.directory.length)[0];
    if (!project) {
      return;
    }
    if (allowActivation &&
        comparablePath(project.path) !== comparablePath(this.snapshot.context?.project.path ?? '') &&
        this.activateProjectForFollow) {
      await this.activateProjectForFollow(project, uri);
    }
    await this.projectsTreeView.reveal(
      this.projectsProvider.fileItemForReveal(project, uri.fsPath),
      { expand: 3, focus: false, select: true }
    );
  }
}
