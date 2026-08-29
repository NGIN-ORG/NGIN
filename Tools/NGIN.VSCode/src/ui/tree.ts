import * as path from 'node:path';
import * as vscode from 'vscode';
import type { NginController } from '../core/controller';
import {
  classifyPhysicalEntry,
  isDefaultHiddenName,
  productFileId,
  semanticFileIndex,
  type ProductFileNode,
  type ProductSemanticIndex
} from '../core/projectFiles';
import { normalizeForComparison, pathsEqual } from '../core/paths';
import { projectTreePresentation } from '../core/projectTreePresentation';
import type { CompositionGraph, GraphNamedNode, GraphPackage, ProjectCandidate } from '../model';
import type { SourceAnalysisProvider } from '../providers/sourceAnalysis';

type ChildFactory = () => NginTreeNode[] | Promise<NginTreeNode[]>;

export class NginTreeNode extends vscode.TreeItem {
  parent?: NginTreeNode;
  project?: ProjectCandidate;
  projectFile?: ProductFileNode;
  package?: GraphPackage;
  directoryPath?: string;

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
  project: ProjectCandidate,
  description?: (value: GraphNamedNode) => string | undefined
): NginTreeNode {
  const node = group(label, icon, () => values.map(value => {
    const child = new NginTreeNode(value.name ?? value.identity);
    child.project = project;
    child.description = description?.(value) ?? value.kind;
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

function packageGroup(values: GraphPackage[], project: ProjectCandidate): NginTreeNode {
  const node = group('Packages', 'package', () => values.map(value => {
    const child = new NginTreeNode(value.name ?? value.identity);
    child.project = project;
    child.package = value;
    child.description = [value.version, value.context].filter(Boolean).join(' · ');
    const document = value.provenance?.document;
    const direct = Boolean(document) && (pathsEqual(document, project.manifest)
      || normalizeForComparison(project.manifest).endsWith(`${path.sep}${normalizeForComparison(document!)}`));
    child.contextValue = direct
      ? 'nginDirectPackage'
      : 'nginTransitivePackage';
    child.tooltip = new vscode.MarkdownString([
      `**${value.name ?? value.identity}**`,
      value.version ? `Version: ${value.version}` : undefined,
      value.providerKind ? `Provider: ${value.providerKind}` : undefined,
      value.provenance?.reason,
      value.provenance?.document ? `Declared in ${value.provenance.document}` : undefined
    ].filter(Boolean).join('\n\n'));
    if (value.provenance?.document) {
      child.command = { command: 'ngin.openGraphSource', title: 'Open Declaring Source', arguments: [value, project] };
    }
    return child;
  }), String(values.length), `ngin.group:${project.manifest}:Packages`);
  node.project = project;
  node.contextValue = 'nginPackagesGroup';
  return node;
}

function semanticDescription(entry: ProductFileNode): string | undefined {
  if (entry.state === 'selected') return entry.kind;
  if (entry.state === 'candidate') return 'Not in build';
  if (entry.state === 'input') return `${entry.kind ?? 'file'} · input`;
  if (entry.state === 'generated') return `${entry.kind ?? 'file'} · generated`;
  if (entry.state === 'missing') return `${entry.kind ?? 'file'} · missing`;
  if (entry.state === 'boundary') return 'product boundary';
  if (entry.state === 'external') return entry.owner ?? entry.relativePath;
  if (entry.state === 'ignored') return 'ignored';
  return undefined;
}

function semanticIcon(entry: ProductFileNode): vscode.ThemeIcon | undefined {
  if (entry.state === 'missing') return new vscode.ThemeIcon('warning');
  if (entry.state === 'input') return new vscode.ThemeIcon('references');
  if (entry.state === 'generated') return new vscode.ThemeIcon('sparkle');
  if (entry.state === 'external') return new vscode.ThemeIcon('link-external');
  if (entry.state === 'boundary') return new vscode.ThemeIcon('root-folder');
  if (entry.state === 'candidate') return new vscode.ThemeIcon('circle-outline');
  return undefined;
}

interface ProductProjection {
  project: ProjectCandidate;
  graph?: CompositionGraph;
  semantics: ProductSemanticIndex;
  nestedBoundaries: ReadonlySet<string>;
  showIgnored: boolean;
}

export class ProjectsTreeProvider implements vscode.TreeDataProvider<NginTreeNode>, vscode.Disposable {
  private readonly changed = new vscode.EventEmitter<NginTreeNode | undefined>();
  readonly onDidChangeTreeData = this.changed.event;
  private readonly disposables: vscode.Disposable[] = [];
  private readonly projectNodes = new Map<string, NginTreeNode>();
  private readonly directoryNodes = new Map<string, NginTreeNode>();

  constructor(
    private readonly controller: NginController,
    private readonly analysis: SourceAnalysisProvider
  ) {
    const refreshProjectRoots = (): void => {
      this.projectNodes.clear();
      this.directoryNodes.clear();
      this.changed.fire(undefined);
    };
    this.disposables.push(
      controller.onDidChange(refreshProjectRoots),
      analysis.onDidChange(refreshProjectRoots),
      vscode.window.onDidChangeActiveTextEditor(refreshProjectRoots),
      vscode.workspace.onDidChangeConfiguration(event => {
        if (event.affectsConfiguration('ngin.workspace.showIgnoredFiles') || event.affectsConfiguration('files.exclude')) {
          refreshProjectRoots();
        }
      })
    );
    const watcher = vscode.workspace.createFileSystemWatcher('**/*');
    const reconcile = (uri: vscode.Uri): void => {
      const parent = this.directoryNodes.get(normalizeForComparison(path.dirname(uri.fsPath)));
      this.changed.fire(parent);
    };
    this.disposables.push(watcher, watcher.onDidCreate(reconcile), watcher.onDidChange(reconcile), watcher.onDidDelete(reconcile));
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

  refresh(element?: NginTreeNode): void {
    this.changed.fire(element);
  }

  private registerDirectory(node: NginTreeNode, directory: string): void {
    node.directoryPath = directory;
    this.directoryNodes.set(normalizeForComparison(directory), node);
  }

  private fileNode(entry: ProductFileNode, projection: ProductProjection): NginTreeNode {
    const expandable = entry.directory && entry.state !== 'boundary';
    let node: NginTreeNode;
    node = new NginTreeNode(
      entry.name,
      expandable ? vscode.TreeItemCollapsibleState.Collapsed : vscode.TreeItemCollapsibleState.None,
      expandable ? () => this.directoryChildren(entry.path, projection, node) : undefined
    );
    node.id = entry.id;
    node.project = projection.project;
    node.projectFile = entry;
    node.resourceUri = vscode.Uri.file(entry.path);
    node.contextValue = `${entry.directory ? 'nginProjectDirectory' : 'nginProjectFile'}.${entry.state}`;
    node.description = semanticDescription(entry);
    node.tooltip = [entry.relativePath || entry.path, entry.kind, entry.owner, entry.provenance].filter(Boolean).join('\n');
    node.iconPath = semanticIcon(entry);
    if (entry.directory) this.registerDirectory(node, entry.path);
    if (!entry.directory && entry.state !== 'missing') {
      node.command = { command: 'vscode.open', title: 'Open File', arguments: [node.resourceUri] };
    }
    return node;
  }

  private excludedByVsCode(uri: vscode.Uri, name: string): boolean {
    const excludes = vscode.workspace.getConfiguration('files', uri).get<Record<string, boolean>>('exclude', {});
    return Object.entries(excludes).some(([pattern, enabled]) => enabled && (
      pattern === name || pattern === `**/${name}` || pattern === `${name}/**` || pattern === `**/${name}/**`
    ));
  }

  private async directoryChildren(
    directory: string,
    projection: ProductProjection,
    parent?: NginTreeNode
  ): Promise<NginTreeNode[]> {
    let entries: [string, vscode.FileType][];
    const directoryUri = vscode.Uri.file(directory);
    try {
      entries = await vscode.workspace.fs.readDirectory(directoryUri);
    } catch {
      return [];
    }
    const result = entries
      .filter(([name]) => !pathsEqual(path.join(directory, name), projection.project.manifest))
      .flatMap(([name, type]) => {
        const absolute = path.join(directory, name);
        const hidden = isDefaultHiddenName(name) || this.excludedByVsCode(directoryUri, name);
        if (hidden && !projection.showIgnored) return [];
        const boundary = projection.nestedBoundaries.has(normalizeForComparison(absolute));
        const entry = classifyPhysicalEntry(
          projection.project.directory,
          absolute,
          Boolean(type & vscode.FileType.Directory),
          projection.semantics,
          boundary,
          hidden
        );
        return [this.fileNode(entry, projection)];
      })
      .sort((left, right) => Number(Boolean(right.projectFile?.directory)) - Number(Boolean(left.projectFile?.directory))
        || String(left.label).localeCompare(String(right.label), undefined, { numeric: true }));
    result.forEach(node => { node.parent = parent; });
    return result;
  }

  private compositionNode(graph: CompositionGraph, project: ProjectCandidate): NginTreeNode | undefined {
    const children: NginTreeNode[] = [];
    if (graph.packages.length) children.push(packageGroup(graph.packages, project));
    if (graph.actions.length) children.push(semanticGroup('Actions', 'tools', graph.actions, project));
    if (graph.runs.length > 1) children.push(semanticGroup('Runs', 'run', graph.runs, project));
    if (graph.tests.length) children.push(semanticGroup('Tests', 'beaker', graph.tests, project));
    if (graph.benchmarks.length) children.push(semanticGroup('Benchmarks', 'dashboard', graph.benchmarks, project));
    if (!children.length) return undefined;
    const composition = group('Composition', 'references', () => children, `${graph.packages.length} packages`,
      `ngin.group:${project.manifest}:Composition`);
    composition.project = project;
    return composition;
  }

  private async missingNodes(projection: ProductProjection): Promise<NginTreeNode[]> {
    const result: NginTreeNode[] = [];
    for (const [absolute, role] of projection.semantics.roles) {
      if (role.state === 'generated') continue;
      try {
        await vscode.workspace.fs.stat(vscode.Uri.file(absolute));
      } catch {
        result.push(this.fileNode({
          id: productFileId(projection.project.directory, absolute, 'missing'),
          name: path.basename(absolute),
          path: absolute,
          relativePath: path.relative(projection.project.directory, absolute).split(path.sep).join('/'),
          directory: false,
          state: 'missing',
          kind: role.kind,
          owner: role.owner,
          provenance: role.provenance
        }, projection));
      }
    }
    return result;
  }

  private async productChildren(project: ProjectCandidate, parent: NginTreeNode): Promise<NginTreeNode[]> {
    const context = this.analysis.contextForProject(project);
    const graph = this.controller.cachedGraphForContext(context);
    if (!graph) {
      void this.controller.graphForContext(context, false).then(() => this.changed.fire(parent));
    }
    const semantics = semanticFileIndex(project.directory, graph, path.join(context.outputDirectory, 'actions'));
    const nestedBoundaries = new Set(this.controller.projects
      .filter(candidate => candidate.manifest !== project.manifest && candidate.directory.startsWith(project.directory + path.sep))
      .map(candidate => normalizeForComparison(candidate.directory)));
    const projection: ProductProjection = {
      project,
      graph,
      semantics,
      nestedBoundaries,
      showIgnored: vscode.workspace.getConfiguration('ngin', vscode.Uri.file(project.manifest))
        .get<boolean>('workspace.showIgnoredFiles', false)
    };
    const result = await this.directoryChildren(project.directory, projection, parent);
    const composition = graph ? this.compositionNode(graph, project) : undefined;
    if (composition) result.unshift(composition);
    if (semantics.generated.length) {
      const generated = group('Generated', 'sparkle', () => semantics.generated.map(entry => this.fileNode(entry, projection)),
        String(semantics.generated.length), `ngin.group:${project.manifest}:Generated`);
      generated.project = project;
      result.push(generated);
    }
    if (semantics.external.length) {
      const external = group('External', 'link-external', () => semantics.external.map(entry => this.fileNode(entry, projection)),
        String(semantics.external.length), `ngin.group:${project.manifest}:External`);
      external.project = project;
      external.contextValue = 'nginExternalGroup';
      result.push(external);
    }
    const issues = await this.missingNodes(projection);
    const graphProblem = this.controller.snapshot.context?.projectManifest === project.manifest
      ? this.controller.snapshot.graphError
      : undefined;
    if (!graph && graphProblem) {
      const problem = new NginTreeNode('Product model unavailable');
      problem.iconPath = new vscode.ThemeIcon('error');
      problem.description = 'Physical files remain available';
      problem.tooltip = graphProblem;
      problem.command = { command: 'workbench.actions.view.problems', title: 'Open Problems' };
      issues.unshift(problem);
    }
    if (issues.length) {
      const node = group('Issues', 'warning', () => issues, String(issues.length),
        `ngin.group:${project.manifest}:Issues`);
      node.project = project;
      result.push(node);
    }
    result.forEach(node => { node.parent = parent; });
    return result;
  }

  private async activeFileProject(): Promise<ProjectCandidate | undefined> {
    const file = vscode.window.activeTextEditor?.document.uri.fsPath;
    return file ? this.analysis.projectForFile(file, false) : undefined;
  }

  private nodeFor(project: ProjectCandidate, owner: ProjectCandidate | undefined): NginTreeNode {
    const existing = this.projectNodes.get(project.manifest);
    if (existing) return existing;
    let node: NginTreeNode;
    node = new NginTreeNode(project.name, vscode.TreeItemCollapsibleState.Collapsed,
      () => this.productChildren(project, node));
    node.project = project;
    node.id = `ngin.product:${project.manifest}`;
    node.resourceUri = vscode.Uri.file(project.manifest);
    const launch = this.controller.launchProduct?.manifest === project.manifest;
    const activeFile = owner?.manifest === project.manifest;
    const busy = this.controller.snapshot.busyProjectManifest === project.manifest ? this.controller.snapshot.busy : undefined;
    const stateApplies = this.controller.snapshot.context?.projectManifest === project.manifest;
    const graphIssue = stateApplies ? this.controller.snapshot.graphError : undefined;
    const context = this.analysis.contextForProject(project);
    const summary = this.analysis.summary(project.manifest);
    const lastOperation = this.controller.snapshot.lastOperation?.projectManifest === project.manifest
      ? this.controller.snapshot.lastOperation
      : undefined;
    const presentation = projectTreePresentation({
      configuration: context.configuration,
      activeFile,
      fallback: launch,
      busy,
      graphIssue,
      graphReady: stateApplies && Boolean(this.controller.snapshot.graph),
      configured: stateApplies ? this.controller.snapshot.configured : undefined,
      analysisState: summary.state === 'analyzing' || summary.state === 'failed' ? summary.state : undefined,
      lastOperation
    });
    node.contextValue = [
      'nginProductRoot',
      project.hasTests ? 'test' : undefined,
      project.hasRun ? 'run' : undefined,
      project.hasAnalyze ? 'analyze' : undefined,
      project.hasFormat ? 'format' : undefined,
      launch ? 'launch' : undefined,
      activeFile ? 'activeFile' : undefined
    ].filter(Boolean).join('.');
    const hasIssue = Boolean(graphIssue) || summary.state === 'failed' || lastOperation?.state === 'failed';
    node.iconPath = new vscode.ThemeIcon(hasIssue ? 'warning' : launch ? 'pass-filled' : activeFile ? 'file-code'
      : project.artifactKind === 'Library' ? 'library' : 'project');
    node.description = presentation.description;
    node.tooltip = [
      project.manifest,
      `${project.libraryKind ?? project.artifactKind ?? 'Product'}${activeFile ? ' · Owns current file' : ''}${launch ? ' · Active Project' : ''}`,
      `Configuration: ${context.configuration}`,
      `Target: ${context.target}`,
      `Toolchain: ${context.toolchain}`,
      presentation.status ? `Status: ${presentation.status}` : undefined
    ].filter(Boolean).join('\n');
    this.projectNodes.set(project.manifest, node);
    return node;
  }

  private async workspaceFiles(discoveryRoot: string, projects: readonly ProjectCandidate[], parent: NginTreeNode): Promise<NginTreeNode[]> {
    const projectDirectories = new Set(projects.map(project => normalizeForComparison(project.directory)));
    let entries: [string, vscode.FileType][];
    try {
      entries = await vscode.workspace.fs.readDirectory(vscode.Uri.file(discoveryRoot));
    } catch {
      return [];
    }
    return entries.flatMap(([name, type]) => {
      const absolute = path.join(discoveryRoot, name);
      if (projectDirectories.has(normalizeForComparison(absolute)) || name.endsWith('.ngin')) return [];
      if (isDefaultHiddenName(name)) return [];
      const entry: ProductFileNode = {
        id: productFileId(discoveryRoot, name, 'workspace'), name, path: absolute, relativePath: name,
        directory: Boolean(type & vscode.FileType.Directory), state: 'ordinary'
      };
      const projection: ProductProjection = {
        project: projects[0] ?? { manifest: discoveryRoot, directory: discoveryRoot, name: 'Workspace' },
        semantics: { roles: new Map(), generated: [], external: [] },
        nestedBoundaries: projectDirectories,
        showIgnored: false
      };
      const node = this.fileNode(entry, projection);
      node.parent = parent;
      return [node];
    });
  }

  async revealFile(project: ProjectCandidate, file: string): Promise<NginTreeNode | undefined> {
    const root = this.projectNodes.get(project.manifest);
    if (!root) return undefined;
    let children = await this.getChildren(root);
    let current: NginTreeNode | undefined;
    const segments = path.relative(project.directory, file).split(path.sep);
    let absolute = project.directory;
    for (const segment of segments) {
      absolute = path.join(absolute, segment);
      current = children.find(node => pathsEqual(node.resourceUri?.fsPath, absolute));
      if (!current) return undefined;
      children = current.projectFile?.directory ? await this.getChildren(current) : [];
    }
    return current;
  }

  async getChildren(element?: NginTreeNode): Promise<NginTreeNode[]> {
    if (element?.children) {
      const children = await element.children();
      children.forEach(child => { child.parent = element; });
      return children;
    }
    if (element) return [];
    this.projectNodes.clear();
    this.directoryNodes.clear();
    if (this.controller.projects.length === 0) return [];
    const owner = await this.activeFileProject();
    const roots: NginTreeNode[] = [];
    const standalone: NginTreeNode[] = [];
    for (const discovery of this.controller.discoveries) {
      const products = discovery.projects.map(project => this.nodeFor(project, owner));
      if (!discovery.workspaceManifest) {
        standalone.push(...products);
        continue;
      }
      let workspace: NginTreeNode;
      workspace = group(
        discovery.workspaceChoices?.name ?? path.basename(discovery.workspaceManifest, '.ngin'),
        'root-folder',
        async () => {
          const children = [...products];
          const files = await this.workspaceFiles(discovery.workspaceFolder, discovery.projects, workspace);
          if (files.length) children.push(group('Workspace Files', 'files', () => files, String(files.length),
            `ngin.workspace-files:${discovery.workspaceFolder}`));
          return children;
        },
        `${products.length} products`,
        `ngin.workspace:${discovery.workspaceManifest}`
      );
      workspace.collapsibleState = vscode.TreeItemCollapsibleState.Expanded;
      products.forEach(product => { product.parent = workspace; });
      roots.push(workspace);
    }
    if (standalone.length === 1 && roots.length === 0) roots.push(standalone[0]);
    else if (standalone.length) {
      const loose = group('Standalone Products', 'root-folder', () => standalone, String(standalone.length),
        'ngin.workspace:standalone');
      loose.collapsibleState = vscode.TreeItemCollapsibleState.Expanded;
      standalone.forEach(product => { product.parent = loose; });
      roots.push(loose);
    }
    return roots;
  }
}
