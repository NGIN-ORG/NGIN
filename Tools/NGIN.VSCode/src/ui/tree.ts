import * as path from 'node:path';
import * as vscode from 'vscode';
import type { NginController } from '../core/controller';
import { enumerateProjectFiles, type ProjectFileEntry } from '../core/projectFiles';
import { kindForPath } from '../core/manifestEdits';
import type { CompositionGraph, GraphNamedNode, ProjectCandidate } from '../model';
import type { SourceAnalysisProvider } from '../providers/sourceAnalysis';

type ChildFactory = () => NginTreeNode[] | Promise<NginTreeNode[]>;
export type ProjectsViewMode = 'project' | 'files';

export class NginTreeNode extends vscode.TreeItem {
  parent?: NginTreeNode;
  project?: ProjectCandidate;

  constructor(label: string, collapsibleState = vscode.TreeItemCollapsibleState.None, readonly children?: ChildFactory) {
    super(label, collapsibleState);
  }
}

function group(label: string, icon: string, children: ChildFactory, description?: string): NginTreeNode {
  const node = new NginTreeNode(label, vscode.TreeItemCollapsibleState.Collapsed, children);
  node.iconPath = new vscode.ThemeIcon(icon);
  node.description = description;
  return node;
}

function semanticGroup(label: string, values: GraphNamedNode[]): NginTreeNode {
  return group(label, 'symbol-field', () => values.map(value => {
    const node = new NginTreeNode(value.name ?? value.identity);
    node.description = value.kind;
    node.tooltip = value.provenance?.reason ?? value.identity;
    node.command = { command: 'ngin.explain', title: 'Explain', arguments: [value.identity] };
    return node;
  }), String(values.length));
}

function compositionDetails(graph: CompositionGraph): NginTreeNode {
  return group('Advanced composition', 'type-hierarchy-sub', () => [
    semanticGroup('Packages', graph.packages),
    semanticGroup('Exports', graph.exports),
    semanticGroup('Options', graph.options),
    semanticGroup('Capabilities', graph.capabilityBindings),
    semanticGroup('Actions', graph.actions),
    semanticGroup('Plugins', graph.plugins),
    semanticGroup('Stage Contributions', graph.contributions),
    semanticGroup('Launches', graph.launches),
    semanticGroup('Graph Edges', graph.edges)
  ]);
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
  const node = group(label, icon, () => entries.map(entry => fileNode(entry, project)), String(count));
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
      : entry.state === 'unselected' && !entry.directory ? 'not included'
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
                : entry.state === 'selected' ? 'file-code' : 'file'
  );
  if (!entry.directory && entry.state !== 'missing') {
    node.command = { command: 'vscode.open', title: 'Open File', arguments: [node.resourceUri] };
  }
  return node;
}

async function projectChildren(
  controller: NginController,
  analysis: SourceAnalysisProvider,
  project: ProjectCandidate,
  mode: ProjectsViewMode
): Promise<NginTreeNode[]> {
  const context = analysis.contextForProject(project);
  const graph = mode === 'files' ? undefined
    : controller.activeProject?.manifest === project.manifest && controller.snapshot.graph
      ? controller.snapshot.graph
      : await controller.graphForContext(context, false);
  const entries = await enumerateProjectFiles(
    project.directory,
    project.manifest,
    graph,
    5000,
    mode === 'project',
    path.join(context.outputDirectory, 'actions')
  );
  if (mode === 'files') return entries.map(entry => fileNode(entry, project));
  const result: NginTreeNode[] = [];
  const graphProblem = controller.activeProject?.manifest === project.manifest ? controller.snapshot.graphError : undefined;
  if (!graph && graphProblem) {
    const problem = new NginTreeNode('Project could not be loaded');
    problem.iconPath = new vscode.ThemeIcon('error');
    problem.description = 'Open Problems or NGIN Output';
    problem.tooltip = graphProblem;
    problem.command = { command: 'ngin.showOutput', title: 'Show NGIN Output' };
    result.push(problem);
  }
  const manifest = findEntry(entries, entry => entry.state === 'authored');
  const sources = filteredEntries(entries, entry => entry.state !== 'generated'
    && (inferredKind(entry) === 'Source' || inferredKind(entry) === 'CxxModule'));
  const headers = filteredEntries(entries, entry => entry.state !== 'generated' && inferredKind(entry) === 'Header');
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
    graph?.packages.length ? semanticGroup('Dependencies', graph.packages) : undefined,
    graph?.launches.length ? semanticGroup('Launch configurations', graph.launches) : undefined,
    graph?.actions.length ? semanticGroup('Tooling', graph.actions) : undefined,
    fileGroup('Generated files', 'sparkle', generated, project),
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
  if (graph) result.push(compositionDetails(graph));
  const summary = analysis.summary(project.manifest);
  if (summary.state === 'failed' && summary.message) {
    const problem = new NginTreeNode('Tooling problem');
    problem.iconPath = new vscode.ThemeIcon('warning');
    problem.description = summary.message.split(/\r?\n/u)[0];
    problem.command = { command: 'ngin.showOutput', title: 'Show NGIN Output' };
    result.unshift(problem);
  }
  return result;
}

function projectNode(
  controller: NginController,
  analysis: SourceAnalysisProvider,
  project: ProjectCandidate,
  mode: ProjectsViewMode
): NginTreeNode {
  const node = new NginTreeNode(project.name, vscode.TreeItemCollapsibleState.Collapsed,
    () => projectChildren(controller, analysis, project, mode));
  node.project = project;
  node.id = `ngin.project:${project.manifest}`;
  node.resourceUri = vscode.Uri.file(project.manifest);
  node.contextValue = [
    'nginProjectRoot',
    project.hasTesting ? 'test' : undefined,
    project.hasLaunch ? 'launch' : undefined,
    project.hasAnalyze ? 'analyze' : undefined,
    project.hasFormat ? 'format' : undefined
  ].filter(Boolean).join('.');
  node.iconPath = new vscode.ThemeIcon(project.type === 'Library' ? 'library' : 'project');
  const active = controller.activeProject?.manifest === project.manifest;
  const busy = controller.snapshot.busyProjectManifest === project.manifest ? controller.snapshot.busy : undefined;
  const summary = analysis.summary(project.manifest);
  node.description = [project.type, active ? 'selected' : undefined,
    busy ? `${busy} in progress` : undefined,
    summary.state === 'analyzing' ? 'analyzing' : summary.state === 'ready' ? summary.message : undefined]
    .filter(Boolean).join(' · ');
  node.tooltip = `${project.manifest}${active ? '\nSelected NGIN project' : '\nClick to select this project'}`;
  return node;
}

export class ProjectsTreeProvider implements vscode.TreeDataProvider<NginTreeNode>, vscode.Disposable {
  private readonly changed = new vscode.EventEmitter<NginTreeNode | undefined>();
  readonly onDidChangeTreeData = this.changed.event;
  private readonly disposables: vscode.Disposable[];
  private readonly projectNodes = new Map<string, NginTreeNode>();

  constructor(
    private readonly controller: NginController,
    private readonly analysis: SourceAnalysisProvider,
    private modeValue: ProjectsViewMode = 'project'
  ) {
    this.disposables = [
      controller.onDidChange(() => this.changed.fire(undefined)),
      analysis.onDidChange(() => this.changed.fire(undefined))
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

  get mode(): ProjectsViewMode { return this.modeValue; }

  setMode(mode: ProjectsViewMode): void {
    if (mode === this.modeValue) return;
    this.modeValue = mode;
    this.projectNodes.clear();
    this.changed.fire(undefined);
  }

  private nodeFor(project: ProjectCandidate): NginTreeNode {
    const node = projectNode(this.controller, this.analysis, project, this.modeValue);
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
    if (this.controller.projects.length === 0) {
      return [];
    }
    if (this.controller.discoveries.length === 1) {
      return this.controller.projects.map(project => this.nodeFor(project));
    }
    return this.controller.discoveries.map(discovery => {
      const label = discovery.workspaceChoices?.name ?? path.basename(discovery.workspaceFolder);
      const projects = discovery.projects.map(project => this.nodeFor(project));
      const workspace = group(label, 'root-folder', () => projects, `${discovery.projects.length} projects`);
      projects.forEach(project => { project.parent = workspace; });
      workspace.collapsibleState = vscode.TreeItemCollapsibleState.Expanded;
      return workspace;
    });
  }
}
