import * as path from 'node:path';
import * as vscode from 'vscode';
import type {
  CompositionGraph,
  GraphNamedNode,
  GraphProvenance,
  ProjectCandidate,
  WorkspaceChoices
} from '../model';
import type { NginController } from '../core/controller';
import { displayOptionValue } from '../core/graph';
import { enumerateProjectFiles, type ProjectFileEntry } from '../core/projectFiles';

type ChildFactory = () => NginTreeNode[] | Promise<NginTreeNode[]>;

export class NginTreeNode extends vscode.TreeItem {
  constructor(
    label: string,
    collapsibleState = vscode.TreeItemCollapsibleState.None,
    readonly children?: ChildFactory
  ) {
    super(label, collapsibleState);
  }
}

function commandNode(label: string, command: string, icon: string, args?: unknown[]): NginTreeNode {
  const node = new NginTreeNode(label);
  node.iconPath = new vscode.ThemeIcon(icon);
  node.command = { command, title: label, arguments: args };
  node.contextValue = 'nginCommand';
  return node;
}

function choiceNodes(
  values: string[],
  selected: string | undefined,
  command: string,
  icon: string
): NginTreeNode[] {
  return values.map(value => {
    const node = commandNode(value, command, value === selected ? 'check' : icon, [value]);
    node.description = value === selected ? 'active' : undefined;
    return node;
  });
}

function group(label: string, icon: string, children: ChildFactory, description?: string): NginTreeNode {
  const node = new NginTreeNode(label, vscode.TreeItemCollapsibleState.Collapsed, children);
  node.iconPath = new vscode.ThemeIcon(icon);
  node.description = description;
  return node;
}

function workspaceChildren(controller: NginController, choices: WorkspaceChoices | undefined, projects: ProjectCandidate[]): NginTreeNode[] {
  const context = controller.snapshot.context;
  const result: NginTreeNode[] = [
    group('Projects', 'project', () => projects.map(projectNode), String(projects.length))
  ];
  if (choices) {
    result.push(
      group('Configurations', 'symbol-constant', () => choiceNodes(choices.configurations, context?.configuration, 'ngin.selectConfiguration', 'circle-outline'), context?.configuration),
      group('Targets', 'target', () => choiceNodes(choices.targets, context?.target, 'ngin.selectTarget', 'circle-outline'), context?.target),
      group('Toolchains', 'tools', () => choiceNodes(choices.toolchains, context?.toolchain, 'ngin.selectToolchain', 'circle-outline'), context?.toolchain)
    );
    if (choices.presets.length > 0) {
      result.push(group('Presets', 'symbol-event', () => choices.presets.map(preset => {
        const node = commandNode(preset.name, 'ngin.selectPreset', 'play', [preset.name]);
        node.description = preset.command;
        return node;
      })));
    }
  }
  result.push(group('Actions', 'run-all', () => [
    commandNode('Validate', 'ngin.validate', 'pass'),
    commandNode('Restore Packages', 'ngin.restore', 'cloud-download'),
    commandNode('Lock Dependencies', 'ngin.lock', 'lock'),
    commandNode('Configure', 'ngin.configure', 'gear'),
    commandNode('Build', 'ngin.build', 'tools'),
    commandNode('Stage', 'ngin.stage', 'package'),
    commandNode('Run', 'ngin.run', 'play'),
    commandNode('Debug', 'ngin.debug', 'debug-alt'),
    commandNode('Test', 'ngin.test', 'beaker'),
    commandNode('Analyze', 'ngin.analyze', 'search-fuzzy'),
    commandNode('Format Sources', 'ngin.formatSources', 'symbol-color')
  ]));
  return result;
}

function projectNode(project: ProjectCandidate): NginTreeNode {
  const node = new NginTreeNode(project.name);
  node.description = project.type;
  node.tooltip = project.manifest;
  node.resourceUri = vscode.Uri.file(project.manifest);
  node.iconPath = new vscode.ThemeIcon(project.type === 'Library' ? 'library' : 'project');
  node.command = { command: 'ngin.selectProject', title: 'Select Project', arguments: [project] };
  node.contextValue = 'nginProject';
  (node as NginTreeNode & { project: ProjectCandidate }).project = project;
  return node;
}

export class SolutionTreeProvider implements vscode.TreeDataProvider<NginTreeNode>, vscode.Disposable {
  private readonly changed = new vscode.EventEmitter<NginTreeNode | undefined>();
  readonly onDidChangeTreeData = this.changed.event;
  private readonly subscription: vscode.Disposable;

  constructor(private readonly controller: NginController) {
    this.subscription = controller.onDidChange(() => this.changed.fire(undefined));
  }

  dispose(): void {
    this.subscription.dispose();
    this.changed.dispose();
  }

  getTreeItem(element: NginTreeNode): vscode.TreeItem {
    return element;
  }

  async getChildren(element?: NginTreeNode): Promise<NginTreeNode[]> {
    if (element?.children) return element.children();
    if (element) return [];
    if (this.controller.discoveries.length === 0) {
      const empty = commandNode('No NGIN manifests found', 'ngin.refresh', 'search');
      empty.description = 'Refresh discovery';
      return [empty];
    }
    return this.controller.discoveries.map(discovery => {
      const label = discovery.workspaceChoices?.name ?? path.basename(discovery.workspaceFolder);
      const node = new NginTreeNode(label, vscode.TreeItemCollapsibleState.Expanded, () =>
        workspaceChildren(this.controller, discovery.workspaceChoices, discovery.projects));
      node.iconPath = new vscode.ThemeIcon(discovery.workspaceManifest ? 'root-folder' : 'folder');
      node.description = discovery.workspaceManifest ? 'workspace' : 'standalone';
      node.tooltip = discovery.workspaceManifest ?? discovery.workspaceFolder;
      node.contextValue = 'nginWorkspace';
      return node;
    });
  }
}

function provenanceTooltip(provenance: GraphProvenance | undefined, detail?: string): vscode.MarkdownString | undefined {
  if (!provenance && !detail) return undefined;
  const markdown = new vscode.MarkdownString();
  if (detail) markdown.appendMarkdown(`${detail}\n\n`);
  if (provenance?.reason) markdown.appendMarkdown(`**Why:** ${provenance.reason}\n\n`);
  if (provenance?.document) markdown.appendMarkdown(`**Source:** ${provenance.document}:${provenance.line ?? 1}:${provenance.column ?? 1}`);
  return markdown;
}

function semanticNode(node: GraphNamedNode, fallbackLabel?: string): NginTreeNode {
  const label = node.name ?? node.identity ?? fallbackLabel ?? 'Item';
  const item = new NginTreeNode(label);
  item.description = node.kind && node.kind !== label ? node.kind : undefined;
  item.tooltip = provenanceTooltip(node.provenance, node.identity);
  item.contextValue = 'nginSemanticNode';
  item.iconPath = new vscode.ThemeIcon('symbol-field');
  item.command = { command: 'ngin.explain', title: 'Explain', arguments: [node.identity] };
  return item;
}

function semanticGroup(label: string, icon: string, values: GraphNamedNode[]): NginTreeNode {
  return group(label, icon, () => values.map(value => semanticNode(value)), String(values.length));
}

function projectFileNode(entry: ProjectFileEntry): NginTreeNode {
  const node = new NginTreeNode(
    entry.name,
    entry.directory && entry.children?.length ? vscode.TreeItemCollapsibleState.Collapsed : vscode.TreeItemCollapsibleState.None,
    entry.directory ? () => (entry.children ?? []).map(projectFileNode) : undefined
  );
  node.resourceUri = vscode.Uri.file(entry.path);
  node.contextValue = entry.directory ? `nginProjectDirectory.${entry.state}` : `nginProjectFile.${entry.state}`;
  node.description = entry.state === 'selected' ? entry.kind
    : entry.state === 'authored' ? 'manifest'
    : entry.state === 'unselected' ? undefined : entry.state;
  node.tooltip = `${entry.relativePath || entry.path}\n${entry.state}${entry.kind ? ` · ${entry.kind}` : ''}`;
  node.iconPath = new vscode.ThemeIcon(
    entry.state === 'missing' ? 'warning'
      : entry.state === 'authored' ? 'file-code'
      : entry.state === 'generated' ? 'sparkle'
        : entry.state === 'external' ? 'link-external'
          : entry.state === 'boundary' ? 'root-folder'
            : entry.directory ? 'folder'
              : entry.state === 'selected' ? 'file-code' : 'file'
  );
  if (!entry.directory && entry.state !== 'missing') {
    node.command = { command: 'vscode.open', title: 'Open File', arguments: [node.resourceUri] };
  }
  (node as NginTreeNode & { projectFile: ProjectFileEntry }).projectFile = entry;
  return node;
}

function graphChildren(graph: CompositionGraph, controller: NginController): NginTreeNode[] {
  const product = new NginTreeNode(graph.product.name);
  product.description = `${graph.product.type}${graph.product.linkage && graph.product.linkage !== 'None' ? ` · ${graph.product.linkage}` : ''}`;
  product.iconPath = new vscode.ThemeIcon('symbol-class');
  product.tooltip = provenanceTooltip(graph.product.provenance, graph.product.identity);
  product.contextValue = 'nginProduct';

  const selection = group('Selection', 'settings', () =>
    Object.entries({
      Configuration: graph.selection.configuration,
      Platform: `${graph.selection.targetOperatingSystem}/${graph.selection.targetArchitecture}`,
      Compiler: [graph.selection.compiler, graph.selection.compilerVersion].filter(Boolean).join(' '),
      Optimization: graph.selection.optimization ?? '',
      'Debug symbols': String(Boolean(graph.selection.debugSymbols)),
      LTO: String(Boolean(graph.selection.linkTimeOptimization))
    }).filter(([, value]) => value).map(([label, value]) => {
      const item = new NginTreeNode(label);
      item.description = value;
      item.iconPath = new vscode.ThemeIcon('symbol-property');
      return item;
    }), graph.selection.configuration);

  const context = controller.requireContext();
  const files = group('Project Files', 'files', async () => {
    const entries = await enumerateProjectFiles(path.dirname(context.projectManifest), context.projectManifest, graph);
    return entries.map(projectFileNode);
  }, String(graph.buildItems.filter(item => ['Source', 'Header', 'CxxModule', 'Resource'].includes(item.kind)).length));
  files.contextValue = 'nginProjectFiles';

  const dependencies = group('Dependencies', 'package', () => [
    semanticGroup('Packages', 'package', graph.packages),
    semanticGroup('Active Exports', 'references', graph.exports)
  ], `${graph.packages.length} packages`);

  const result: NginTreeNode[] = [product, selection, files, dependencies];
  if (graph.options.length) result.push(group('Options', 'symbol-variable', () => graph.options.map(value => {
    const node = semanticNode(value);
    node.contextValue = 'nginOption';
    node.description = displayOptionValue(value.value) || value.type;
    node.command = { command: 'ngin.setOption', title: 'Set Option Override', arguments: [value.name ?? value.identity] };
    return node;
  }), String(graph.options.length)));
  if (graph.capabilityBindings.length) result.push(semanticGroup('Capabilities', 'plug', graph.capabilityBindings));
  if (graph.actions.length) result.push(group('Actions', 'wand', () => graph.actions.map(value => {
    const node = semanticNode(value);
    node.contextValue = `nginAction.${value.kind ?? 'Custom'}`;
    return node;
  }), String(graph.actions.length)));
  if (graph.plugins.length) result.push(semanticGroup('Plugins', 'extensions', graph.plugins));
  if (graph.contributions.length) result.push(semanticGroup('Stage Contributions', 'files', graph.contributions));
  if (graph.launches.length) result.push(semanticGroup('Launches', 'play', graph.launches));
  if (graph.testing) result.push(semanticGroup('Testing', 'beaker', [graph.testing]));
  if (graph.publishes.length) result.push(semanticGroup('Publish', 'package', graph.publishes));
  result.push(semanticGroup('Graph Edges', 'type-hierarchy-sub', graph.edges));
  return result;
}

export class ProjectTreeProvider implements vscode.TreeDataProvider<NginTreeNode>, vscode.Disposable {
  private readonly changed = new vscode.EventEmitter<NginTreeNode | undefined>();
  readonly onDidChangeTreeData = this.changed.event;
  private readonly subscription: vscode.Disposable;

  constructor(private readonly controller: NginController) {
    this.subscription = controller.onDidChange(() => this.changed.fire(undefined));
  }

  dispose(): void {
    this.subscription.dispose();
    this.changed.dispose();
  }

  getTreeItem(element: NginTreeNode): vscode.TreeItem {
    return element;
  }

  async getChildren(element?: NginTreeNode): Promise<NginTreeNode[]> {
    if (element?.children) return element.children();
    if (element) return [];
    const snapshot = this.controller.snapshot;
    if (!snapshot.context) return [commandNode('Select an NGIN project', 'ngin.selectProject', 'project')];
    if (snapshot.busy) {
      const busy = commandNode(`${snapshot.busy} in progress`, 'ngin.showOutput', 'loading~spin');
      busy.description = snapshot.context.projectName;
      return [busy];
    }
    if (!snapshot.graph) {
      const error = commandNode('Composition unavailable', 'ngin.refreshGraph', 'warning');
      error.tooltip = snapshot.graphError;
      error.description = 'Click to retry';
      return [error];
    }
    return graphChildren(snapshot.graph, this.controller);
  }
}
