import * as path from 'node:path';
import * as vscode from 'vscode';
import type { NginController } from '../core/controller';
import { enumerateProjectFiles, type ProjectFileEntry } from '../core/projectFiles';
import { kindForPath } from '../core/manifestEdits';
import type { GraphNamedNode, ProjectCandidate } from '../model';
import type { SourceAnalysisProvider } from '../providers/sourceAnalysis';

type ChildFactory = () => NginTreeNode[] | Promise<NginTreeNode[]>;

export class NginTreeNode extends vscode.TreeItem {
  parent?: NginTreeNode;
  project?: ProjectCandidate;

  constructor(label: string, collapsibleState = vscode.TreeItemCollapsibleState.None, readonly children?: ChildFactory) {
    super(label, collapsibleState);
  }
}

function group(
  label: string,
  icon: string,
  children: ChildFactory,
  description?: string,
  id?: string
): NginTreeNode {
  const node = new NginTreeNode(label, vscode.TreeItemCollapsibleState.Collapsed, children);
  node.iconPath = new vscode.ThemeIcon(icon);
  node.description = description;
  node.id = id;
  return node;
}

function semanticGroup(
  label: string,
  icon: string,
  values: GraphNamedNode[],
  project: ProjectCandidate
): NginTreeNode {
  const node = group(label, icon, () => values.map(value => {
    const child = new NginTreeNode(value.name ?? value.identity);
    child.project = project;
    child.description = value.kind;
    child.tooltip = new vscode.MarkdownString([
      `**${value.name ?? value.identity}**`,
      value.kind ? `Type: ${value.kind}` : undefined,
      value.provenance?.reason,
      value.provenance?.document ? `Declared in ${value.provenance.document}` : undefined
    ].filter(Boolean).join('\n\n'));
    if (value.provenance?.document) {
      child.command = { command: 'ngin.openGraphSource', title: 'Open Declaring Source', arguments: [value, project] };
    }
    return child;
  }), String(values.length), `ngin.group:${project.manifest}:${label}`);
  node.project = project;
  return node;
}

function filteredEntries(
  entries: ProjectFileEntry[],
  include: (entry: ProjectFileEntry) => boolean
): ProjectFileEntry[] {
  return entries.flatMap(entry => {
    const children = filteredEntries(entry.children ?? [], include);
    if (!include(entry) && children.length === 0) return [];
    return [{ ...entry, children: entry.directory ? children : entry.children }];
  });
}

function entryCount(entries: ProjectFileEntry[]): number {
  return entries.reduce((count, entry) => {
    const children = entry.children ?? [];
    return count + (entry.directory ? (children.length ? entryCount(children) : 1) : 1);
  }, 0);
}

function fileGroup(
  label: string,
  icon: string,
  entries: ProjectFileEntry[],
  project: ProjectCandidate,
  contextValue?: string
): NginTreeNode | undefined {
  const count = entryCount(entries);
  if (!count) return undefined;
  const node = group(
    label,
    icon,
    () => entries.map(entry => fileNode(entry, project)),
    String(count),
    `ngin.group:${project.manifest}:${label}`
  );
  node.project = project;
  node.contextValue = contextValue;
  return node;
}

function findEntry(entries: ProjectFileEntry[], predicate: (entry: ProjectFileEntry) => boolean): ProjectFileEntry | undefined {
  for (const entry of entries) {
    if (predicate(entry)) return entry;
    const nested = findEntry(entry.children ?? [], predicate);
    if (nested) return nested;
  }
  return undefined;
}

function inferredKind(entry: ProjectFileEntry): string | undefined {
  return entry.kind ?? (!entry.directory ? kindForPath(entry.relativePath) : undefined);
}

function fileNode(entry: ProjectFileEntry, project: ProjectCandidate): NginTreeNode {
  const children = entry.children?.map(child => fileNode(child, project));
  const node = new NginTreeNode(
    entry.name,
    entry.directory && children?.length
      ? vscode.TreeItemCollapsibleState.Collapsed
      : vscode.TreeItemCollapsibleState.None,
    children ? () => children : undefined
  );
  node.project = project;
  node.resourceUri = vscode.Uri.file(entry.path);
  node.contextValue = `${entry.directory ? 'nginProjectDirectory' : 'nginProjectFile'}.${entry.state}`;
  (node as NginTreeNode & { projectFile: ProjectFileEntry }).projectFile = entry;
  node.description = entry.state === 'authored' ? 'manifest'
    : entry.state === 'selected' ? entry.kind
      : entry.state === 'generated' ? `${entry.kind ?? 'file'} · generated`
        : entry.state === 'missing' ? `${entry.kind ?? 'file'} · missing`
          : entry.state === 'boundary' ? 'nested project'
            : entry.state === 'external' && !entry.directory ? entry.relativePath : undefined;
  node.tooltip = `${entry.relativePath || entry.path}${entry.kind ? `\n${entry.kind}` : ''}`;
  node.iconPath = new vscode.ThemeIcon(
    entry.state === 'missing' ? 'warning'
      : entry.state === 'authored' ? 'file-code'
        : entry.state === 'generated' ? 'sparkle'
          : entry.state === 'external' ? (entry.directory ? 'references' : 'link-external')
            : entry.state === 'boundary' ? 'root-folder'
              : entry.directory ? 'folder'
                : 'file-code'
  );
  if (!entry.directory && entry.state !== 'missing') {
    node.command = { command: 'vscode.open', title: 'Open File', arguments: [node.resourceUri] };
  }
  return node;
}

async function projectChildren(
  controller: NginController,
  analysis: SourceAnalysisProvider,
  project: ProjectCandidate
): Promise<NginTreeNode[]> {
  const context = analysis.contextForProject(project);
  const graph = controller.activeProject?.manifest === project.manifest && controller.snapshot.graph
    ? controller.snapshot.graph
    : await controller.graphForContext(context, false);
  const entries = await enumerateProjectFiles(
    project.directory,
    project.manifest,
    graph,
    5000,
    true,
    path.join(context.outputDirectory, 'actions')
  );
  const result: NginTreeNode[] = [];
  const graphProblem = controller.activeProject?.manifest === project.manifest ? controller.snapshot.graphError : undefined;
  if (!graph && graphProblem) {
    const problem = new NginTreeNode('Project model unavailable');
    problem.iconPath = new vscode.ThemeIcon('error');
    problem.description = 'Open Problems or NGIN Output';
    problem.tooltip = graphProblem;
    problem.command = { command: 'workbench.actions.view.problems', title: 'Open Problems' };
    result.push(problem);
  }

  const manifest = findEntry(entries, entry => entry.state === 'authored');
  const sources = filteredEntries(entries, entry => entry.state === 'selected'
    && (inferredKind(entry) === 'Source' || inferredKind(entry) === 'CxxModule'));
  const headers = filteredEntries(entries, entry => entry.state === 'selected' && inferredKind(entry) === 'Header');
  const resources = filteredEntries(entries, entry => entry.state === 'selected' && inferredKind(entry) === 'Resource');
  const generated = filteredEntries(entries, entry => entry.state === 'generated');
  const external = filteredEntries(entries, entry => entry.state === 'external');
  const missing = filteredEntries(entries, entry => entry.state === 'missing');
  const boundaries = filteredEntries(entries, entry => entry.state === 'boundary');

  for (const node of [
    manifest ? fileNode(manifest, project) : undefined,
    fileGroup('Sources', 'file-code', sources, project, 'nginSourcesGroup'),
    fileGroup('Headers', 'symbol-file', headers, project, 'nginHeadersGroup'),
    fileGroup('Resources', 'file-media', resources, project),
    graph?.packages.length ? semanticGroup('Dependencies', 'package', graph.packages, project) : undefined,
    graph?.runs.length ? semanticGroup('Runs', 'run', graph.runs, project) : undefined,
    fileGroup('Generated', 'sparkle', generated, project),
    fileGroup('External inputs', 'link-external', external, project),
    fileGroup('Nested projects', 'root-folder', boundaries, project)
  ]) if (node) result.push(node);

  if (missing.length) {
    const issues = fileGroup('Issues', 'warning', missing, project);
    if (issues) {
      issues.collapsibleState = vscode.TreeItemCollapsibleState.Expanded;
      result.push(issues);
    }
  }

  const summary = analysis.summary(project.manifest);
  if (summary.state === 'failed' && summary.message) {
    const problem = new NginTreeNode('Analyzer unavailable');
    problem.iconPath = new vscode.ThemeIcon('warning');
    problem.description = 'Show NGIN Output';
    problem.tooltip = summary.message;
    problem.command = { command: 'ngin.showOutput', title: 'Show NGIN Output' };
    result.unshift(problem);
  }
  return result;
}

async function activeFileProject(analysis: SourceAnalysisProvider): Promise<ProjectCandidate | undefined> {
  const file = vscode.window.activeTextEditor?.document.uri.fsPath;
  if (!file) return undefined;
  return analysis.projectForFile(file, false);
}

function projectNode(
  controller: NginController,
  analysis: SourceAnalysisProvider,
  project: ProjectCandidate,
  owner: ProjectCandidate | undefined
): NginTreeNode {
  const node = new NginTreeNode(project.name, vscode.TreeItemCollapsibleState.Collapsed,
    () => projectChildren(controller, analysis, project));
  node.project = project;
  node.id = `ngin.project:${project.manifest}`;
  node.resourceUri = vscode.Uri.file(project.manifest);
  const fallback = controller.activeProject?.manifest === project.manifest;
  const activeFile = owner?.manifest === project.manifest;
  const busy = controller.snapshot.busyProjectManifest === project.manifest ? controller.snapshot.busy : undefined;
  const graphIssue = fallback ? controller.snapshot.graphError : undefined;
  node.contextValue = [
    'nginProjectRoot',
    project.hasTests ? 'test' : undefined,
    project.hasRun ? 'run' : undefined,
    project.hasAnalyze ? 'analyze' : undefined,
    project.hasFormat ? 'format' : undefined,
    fallback ? 'default' : undefined,
    activeFile ? 'activeFile' : undefined
  ].filter(Boolean).join('.');
  node.iconPath = new vscode.ThemeIcon(graphIssue ? 'warning' : activeFile ? 'file-code' : fallback ? 'pinned' : project.artifactKind === 'Library' ? 'library' : 'project');
  const summary = analysis.summary(project.manifest);
  node.description = [
    project.libraryKind ?? project.artifactKind,
    activeFile ? 'active file' : fallback ? 'default' : undefined,
    busy ? `${busy} in progress` : undefined,
    graphIssue ? 'model issue' : undefined,
    summary.state === 'analyzing' ? 'analyzing' : summary.state === 'ready' ? summary.message : undefined
  ].filter(Boolean).join(' · ');
  const contextDescription = activeFile
    ? 'Effective because this project owns the active file.'
    : fallback ? 'Default when the active file has no project owner.' : 'Available NGIN project.';
  node.tooltip = `${project.manifest}\n${contextDescription}`;
  node.accessibilityInformation = {
    label: [project.name, project.libraryKind ?? project.artifactKind ?? 'NGIN project', activeFile ? 'active file project' : fallback ? 'default project' : undefined,
      busy ? `${busy} in progress` : undefined, graphIssue ? 'project model issue' : undefined].filter(Boolean).join(', '),
    role: 'treeitem'
  };
  return node;
}

export class ProjectsTreeProvider implements vscode.TreeDataProvider<NginTreeNode>, vscode.Disposable {
  private readonly changed = new vscode.EventEmitter<NginTreeNode | undefined>();
  readonly onDidChangeTreeData = this.changed.event;
  private readonly disposables: vscode.Disposable[];
  private readonly projectNodes = new Map<string, NginTreeNode>();

  constructor(
    private readonly controller: NginController,
    private readonly analysis: SourceAnalysisProvider
  ) {
    this.disposables = [
      controller.onDidChange(() => this.changed.fire(undefined)),
      analysis.onDidChange(() => this.changed.fire(undefined)),
      vscode.window.onDidChangeActiveTextEditor(() => this.changed.fire(undefined))
    ];
  }

  dispose(): void {
    this.disposables.forEach(value => value.dispose());
    this.changed.dispose();
  }

  getTreeItem(element: NginTreeNode): vscode.TreeItem { return element; }
  getParent(element: NginTreeNode): NginTreeNode | undefined { return element.parent; }

  projectNode(manifest: string): NginTreeNode | undefined {
    return this.projectNodes.get(manifest);
  }

  private nodeFor(project: ProjectCandidate, owner: ProjectCandidate | undefined): NginTreeNode {
    const node = projectNode(this.controller, this.analysis, project, owner);
    this.projectNodes.set(project.manifest, node);
    return node;
  }

  async getChildren(element?: NginTreeNode): Promise<NginTreeNode[]> {
    if (element?.children) {
      const children = await element.children();
      children.forEach(child => { child.parent = element; });
      return children;
    }
    if (element) return [];
    this.projectNodes.clear();
    if (this.controller.projects.length === 0) return [];
    const owner = await activeFileProject(this.analysis);
    if (this.controller.discoveries.length === 1) {
      return this.controller.projects.map(project => this.nodeFor(project, owner));
    }
    return this.controller.discoveries.map(discovery => {
      const label = discovery.workspaceChoices?.name ?? path.basename(discovery.workspaceFolder);
      const projects = discovery.projects.map(project => this.nodeFor(project, owner));
      const workspace = group(label, 'root-folder', () => projects, `${discovery.projects.length} projects`, `ngin.workspace:${discovery.workspaceFolder}`);
      projects.forEach(project => { project.parent = workspace; });
      workspace.collapsibleState = vscode.TreeItemCollapsibleState.Expanded;
      return workspace;
    });
  }
}
