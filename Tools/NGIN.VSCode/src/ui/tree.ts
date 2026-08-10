import * as path from 'node:path';
import * as vscode from 'vscode';
import type { NginController } from '../core/controller';
import type { CompositionGraph, GraphNamedNode, ProjectCandidate } from '../model';
import type { SourceAnalysisProvider } from '../providers/sourceAnalysis';

type ChildFactory = () => NginTreeNode[] | Promise<NginTreeNode[]>;

export class NginTreeNode extends vscode.TreeItem {
  parent?: NginTreeNode;
  project?: ProjectCandidate;

  constructor(label: string, collapsibleState = vscode.TreeItemCollapsibleState.None, readonly children?: ChildFactory) {
    super(label, collapsibleState);
  }
}

function commandNode(label: string, command: string, icon: string, project: ProjectCandidate): NginTreeNode {
  const node = new NginTreeNode(label);
  node.iconPath = new vscode.ThemeIcon(icon);
  node.command = { command, title: label, arguments: [{ project }] };
  node.contextValue = 'nginProjectCommand';
  node.project = project;
  return node;
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
  return group('Composition Details', 'type-hierarchy-sub', () => [
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

function projectChildren(
  controller: NginController,
  analysis: SourceAnalysisProvider,
  project: ProjectCandidate
): NginTreeNode[] {
  const result = [
    commandNode('Build', 'ngin.build', 'tools', project),
    commandNode('Run', 'ngin.run', 'play', project),
    commandNode('Debug', 'ngin.debug', 'debug-alt', project)
  ];
  if (project.hasTesting) result.push(commandNode('Test', 'ngin.test', 'beaker', project));
  const activeGraph = controller.activeProject?.manifest === project.manifest ? controller.snapshot.graph : undefined;
  if (project.hasAnalyze || activeGraph?.actions.some(action => action.kind === 'Analyze')) {
    result.push(commandNode('Analyze Project', 'ngin.analyze', 'search-fuzzy', project));
  }
  const more = group('More', 'ellipsis', () => [
    commandNode('Set as Build Target', 'ngin.switchBuildTarget', 'pin', project),
    commandNode('Open Manifest', 'ngin.openManifest', 'file-code', project),
    commandNode('Configure', 'ngin.configure', 'gear', project),
    commandNode('Restore Packages', 'ngin.restore', 'cloud-download', project),
    commandNode('Lock Dependencies', 'ngin.lock', 'lock', project),
    commandNode('Enable Project Tooling', 'ngin.enableProjectTooling', 'verified-filled', project),
    commandNode('Clean', 'ngin.clean', 'trash', project),
    commandNode('Show Composition Graph', 'ngin.showGraph', 'json', project),
    commandNode('Inspect Composition', 'ngin.inspect', 'inspect', project)
  ]);
  result.push(more);
  if (controller.activeProject?.manifest === project.manifest && controller.snapshot.graph) {
    result.push(compositionDetails(controller.snapshot.graph));
  }
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

function projectNode(controller: NginController, analysis: SourceAnalysisProvider, project: ProjectCandidate): NginTreeNode {
  const node = new NginTreeNode(project.name, vscode.TreeItemCollapsibleState.Collapsed,
    () => projectChildren(controller, analysis, project));
  node.project = project;
  node.resourceUri = vscode.Uri.file(project.manifest);
  node.contextValue = 'nginProject';
  node.iconPath = new vscode.ThemeIcon(project.type === 'Library' ? 'library' : 'project');
  const active = controller.activeProject?.manifest === project.manifest;
  const summary = analysis.summary(project.manifest);
  node.description = [project.type, active ? 'build target' : undefined,
    active && controller.snapshot.configured === false ? 'needs configure' : undefined,
    summary.state === 'analyzing' ? 'analyzing' : summary.state === 'ready' ? summary.message : undefined]
    .filter(Boolean).join(' · ');
  node.tooltip = `${project.manifest}${active ? '\nDefault Build/Run target' : ''}`;
  return node;
}

export class ProjectsTreeProvider implements vscode.TreeDataProvider<NginTreeNode>, vscode.Disposable {
  private readonly changed = new vscode.EventEmitter<NginTreeNode | undefined>();
  readonly onDidChangeTreeData = this.changed.event;
  private readonly disposables: vscode.Disposable[];
  private readonly projectNodes = new Map<string, NginTreeNode>();

  constructor(private readonly controller: NginController, private readonly analysis: SourceAnalysisProvider) {
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

  private nodeFor(project: ProjectCandidate): NginTreeNode {
    const existing = this.projectNodes.get(project.manifest);
    if (existing) return existing;
    const node = projectNode(this.controller, this.analysis, project);
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
      const empty = new NginTreeNode('No NGIN projects found');
      empty.description = 'Refresh project discovery';
      empty.iconPath = new vscode.ThemeIcon('search');
      empty.command = { command: 'ngin.refresh', title: 'Refresh NGIN Projects' };
      return [empty];
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
