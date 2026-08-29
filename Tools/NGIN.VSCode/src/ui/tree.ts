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
import type {
  CMakeTargetDescription, CompositionGraph, GraphNamedNode, GraphPackage, ProjectCandidate, WorkspacePackage
} from '../model';
import type { SourceAnalysisProvider } from '../providers/sourceAnalysis';

type ChildFactory = () => NginTreeNode[] | Promise<NginTreeNode[]>;

function projectKey(project: ProjectCandidate): string {
  return project.id ?? project.manifest;
}

function contextMatchesProject(project: ProjectCandidate, projectId: string | undefined, manifest: string | undefined): boolean {
  return project.id && projectId ? project.id === projectId : project.manifest === manifest;
}

export class NginTreeNode extends vscode.TreeItem {
  parent?: NginTreeNode;
  project?: ProjectCandidate;
  projectFile?: ProductFileNode;
  package?: GraphPackage;
  workspacePackage?: WorkspacePackage;
  developmentProject?: ProjectCandidate;
  cmakeTarget?: CMakeTargetDescription;
  cmakeTestName?: string;
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
  }), String(values.length), `ngin.group:${projectKey(project)}:${label}`);
  node.project = project;
  return node;
}

function packageGroup(values: GraphPackage[], project: ProjectCandidate, controller: NginController): NginTreeNode {
  const node = group('Packages', 'package', () => values.map(value => {
    const child = new NginTreeNode(value.name ?? value.identity);
    child.project = project;
    child.package = value;
    child.workspacePackage = controller.workspacePackageForProject(project, value.name ?? value.identity);
    child.developmentProject = controller.developmentProjectForPackage(project, value.name ?? value.identity);
    child.description = [value.version, value.context].filter(Boolean).join(' · ');
    const document = value.provenance?.document;
    const direct = Boolean(document) && (pathsEqual(document, project.manifest)
      || normalizeForComparison(project.manifest).endsWith(`${path.sep}${normalizeForComparison(document!)}`));
    child.contextValue = `${direct ? 'nginDirectPackage' : 'nginTransitivePackage'}${child.developmentProject ? 'Development' : ''}`;
    child.tooltip = new vscode.MarkdownString([
      `**${value.name ?? value.identity}**`,
      value.version ? `Version: ${value.version}` : undefined,
      value.providerKind ? `Provider: ${value.providerKind}` : undefined,
      child.workspacePackage?.exportedTargets.length
        ? `Exports: ${child.workspacePackage.exportedTargets.join(', ')}` : undefined,
      value.provenance?.reason,
      value.provenance?.document ? `Declared in ${value.provenance.document}` : undefined
    ].filter(Boolean).join('\n\n'));
    if (value.provenance?.document) {
      child.command = { command: 'ngin.openGraphSource', title: 'Open Declaring Source', arguments: [value, project] };
    }
    return child;
  }), String(values.length), `ngin.group:${projectKey(project)}:Packages`);
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
    if (graph.packages.length) children.push(packageGroup(graph.packages, project, this.controller));
    if (graph.actions.length) children.push(semanticGroup('Actions', 'tools', graph.actions, project));
    if (graph.runs.length > 1) children.push(semanticGroup('Runs', 'run', graph.runs, project));
    if (graph.tests.length) children.push(semanticGroup('Tests', 'beaker', graph.tests, project));
    if (graph.benchmarks.length) children.push(semanticGroup('Benchmarks', 'dashboard', graph.benchmarks, project));
    if (!children.length) return undefined;
    const composition = group('Composition', 'references', () => children, `${graph.packages.length} packages`,
      `ngin.group:${projectKey(project)}:Composition`);
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
    if (project.projectSystem === 'CMake') return this.cmakeProjectChildren(project, parent);
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
        String(semantics.generated.length), `ngin.group:${projectKey(project)}:Generated`);
      generated.project = project;
      result.push(generated);
    }
    if (semantics.external.length) {
      const external = group('External', 'link-external', () => semantics.external.map(entry => this.fileNode(entry, projection)),
        String(semantics.external.length), `ngin.group:${projectKey(project)}:External`);
      external.project = project;
      external.contextValue = 'nginExternalGroup';
      result.push(external);
    }
    const issues = await this.missingNodes(projection);
    const graphProblem = contextMatchesProject(project, this.controller.snapshot.context?.projectId,
      this.controller.snapshot.context?.projectManifest)
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
        `ngin.group:${projectKey(project)}:Issues`);
      node.project = project;
      result.push(node);
    }
    result.forEach(node => { node.parent = parent; });
    return result;
  }

  private cmakeTargetNode(project: ProjectCandidate, target: CMakeTargetDescription): NginTreeNode {
    const children = (): NginTreeNode[] => {
      const sources = target.sources.map(source => {
        const node = new NginTreeNode(path.basename(source.path));
        node.project = project;
        node.resourceUri = vscode.Uri.file(source.path);
        node.description = path.relative(project.directory, source.path).split(path.sep).join('/');
        node.tooltip = [source.path, source.declaration
          ? `Declared in ${source.declaration}:${source.declarationLine ?? 1}` : undefined].filter(Boolean).join('\n');
        node.command = { command: 'vscode.open', title: 'Open Source', arguments: [node.resourceUri] };
        return node;
      });
      return sources;
    };
    const node = new NginTreeNode(target.name,
      target.sources.length ? vscode.TreeItemCollapsibleState.Collapsed : vscode.TreeItemCollapsibleState.None,
      target.sources.length ? children : undefined);
    node.project = project;
    node.cmakeTarget = target;
    node.contextValue = `nginCMakeTarget.${target.type === 'EXECUTABLE' ? 'executable' : 'buildable'}`;
    node.description = target.type.replaceAll('_', ' ').toLowerCase();
    node.iconPath = new vscode.ThemeIcon(target.type === 'EXECUTABLE' ? 'run' : 'symbol-method');
    node.tooltip = [target.id, target.declaration
      ? `Declared in ${target.declaration}:${target.declarationLine ?? 1}` : undefined,
      target.artifacts.length ? `Artifacts:\n${target.artifacts.join('\n')}` : undefined].filter(Boolean).join('\n');
    return node;
  }

  private async cmakeProjectChildren(project: ProjectCandidate, parent: NginTreeNode): Promise<NginTreeNode[]> {
    let snapshot = this.controller.cmakeSnapshot(project);
    if (!snapshot) {
      void this.controller.refreshCMakeProject(project, false).then(() => this.changed.fire(parent));
    }
    snapshot = this.controller.cmakeSnapshot(project);
    const roles = new Map<string, ProductSemanticIndex['roles'] extends ReadonlyMap<string, infer R> ? R : never>();
    const semantics: ProductSemanticIndex = { roles, generated: [], external: [] };
    if (snapshot) {
      for (const target of snapshot.targets) {
        for (const source of target.sources) {
          const existing = semantics.roles.get(normalizeForComparison(source.path));
          roles.set(normalizeForComparison(source.path), {
            state: 'selected',
            kind: 'CMake source',
            owner: existing?.owner ? `${existing.owner}, ${target.name}` : target.name,
            provenance: source.declaration
          });
        }
      }
    }
    const projection: ProductProjection = {
      project,
      semantics,
      nestedBoundaries: new Set(),
      showIgnored: vscode.workspace.getConfiguration('ngin', vscode.Uri.file(project.directory))
        .get<boolean>('workspace.showIgnoredFiles', false)
    };
    const result = await this.directoryChildren(project.directory, projection, parent);
    if (snapshot?.targets.length) {
      const targets = group('Targets', 'symbol-method',
        () => snapshot!.targets.map(target => this.cmakeTargetNode(project, target)),
        String(snapshot.targets.length), `ngin.cmake-targets:${project.id ?? project.manifest}`);
      targets.project = project;
      targets.contextValue = 'nginCMakeTargetsGroup';
      result.unshift(targets);
    }
    if (snapshot?.tests.length) {
      const tests = group('Tests', 'beaker', () => snapshot!.tests.map(test => {
        const node = new NginTreeNode(test.name);
        node.project = project;
        node.cmakeTestName = test.name;
        node.contextValue = /_NOT_BUILT-[^/]+$/u.test(test.name) ? 'nginCMakeTestNotBuilt' : 'nginCMakeTest';
        return node;
      }), String(snapshot.tests.length), `ngin.cmake-tests:${project.id ?? project.manifest}`);
      tests.project = project;
      tests.contextValue = 'nginCMakeTestsGroup';
      result.unshift(tests);
    }
    if (!vscode.workspace.isTrusted || snapshot?.cmake.stale || snapshot?.diagnostics.length) {
      const issues: NginTreeNode[] = [];
      if (!vscode.workspace.isTrusted) {
        const trust = new NginTreeNode('Trust workspace to configure and build');
        trust.iconPath = new vscode.ThemeIcon('shield');
        trust.command = { command: 'workbench.trust.manage', title: 'Manage Workspace Trust' };
        issues.push(trust);
      }
      if (snapshot?.cmake.stale) {
        const stale = new NginTreeNode('CMake model is stale');
        stale.description = 'Configure to refresh';
        stale.iconPath = new vscode.ThemeIcon('warning');
        issues.push(stale);
      }
      for (const diagnostic of snapshot?.diagnostics ?? []) {
        const issue = new NginTreeNode(diagnostic);
        issue.iconPath = new vscode.ThemeIcon('warning');
        issues.push(issue);
      }
      if (issues.length) result.push(group('Issues', 'warning', () => issues, String(issues.length)));
    }
    result.forEach(node => { node.parent = parent; });
    return result;
  }

  private async activeFileProject(): Promise<ProjectCandidate | undefined> {
    const file = vscode.window.activeTextEditor?.document.uri.fsPath;
    return file ? this.analysis.projectForFile(file, false) : undefined;
  }

  private nodeFor(project: ProjectCandidate, owner: ProjectCandidate | undefined): NginTreeNode {
    const existing = this.projectNodes.get(projectKey(project));
    if (existing) return existing;
    let node: NginTreeNode;
    node = new NginTreeNode(project.name, vscode.TreeItemCollapsibleState.Collapsed,
      () => this.productChildren(project, node));
    node.project = project;
    node.id = `ngin.product:${projectKey(project)}`;
    node.resourceUri = vscode.Uri.file(project.manifest);
    const launch = this.controller.isActiveProject(project);
    const activeFile = owner ? projectKey(owner) === projectKey(project) : false;
    const busy = contextMatchesProject(project, this.controller.snapshot.busyProjectId,
      this.controller.snapshot.busyProjectManifest) ? this.controller.snapshot.busy : undefined;
    const stateApplies = contextMatchesProject(project, this.controller.snapshot.context?.projectId,
      this.controller.snapshot.context?.projectManifest);
    const graphIssue = stateApplies ? this.controller.snapshot.graphError : undefined;
    const context = this.analysis.contextForProject(project);
    const cmake = project.projectSystem === 'CMake' ? this.controller.cmakeSnapshot(project) : undefined;
    const summary = this.analysis.summary(projectKey(project));
    const lastOperation = contextMatchesProject(project, this.controller.snapshot.lastOperation?.projectId,
      this.controller.snapshot.lastOperation?.projectManifest)
      ? this.controller.snapshot.lastOperation
      : undefined;
    const presentation = projectTreePresentation({
      configuration: project.projectSystem === 'CMake' ? context.configurePreset ?? 'not configured' : context.configuration,
      activeFile,
      fallback: launch,
      busy,
      graphIssue,
      graphReady: project.projectSystem === 'CMake' ? Boolean(cmake) : stateApplies && Boolean(this.controller.snapshot.graph),
      configured: stateApplies ? this.controller.snapshot.configured : undefined,
      analysisState: summary.state === 'analyzing' || summary.state === 'failed' ? summary.state : undefined,
      lastOperation
    });
    node.contextValue = [
      project.projectSystem === 'CMake' ? 'nginCMakeProjectRoot' : 'nginProductRoot',
      project.hasTests ? 'test' : undefined,
      project.hasRun ? 'run' : undefined,
      project.hasAnalyze ? 'analyze' : undefined,
      project.hasFormat ? 'format' : undefined,
      launch ? 'launch' : undefined,
      activeFile ? 'activeFile' : undefined
    ].filter(Boolean).join('.');
    const hasIssue = Boolean(graphIssue) || summary.state === 'failed' || lastOperation?.state === 'failed';
    node.iconPath = new vscode.ThemeIcon(hasIssue ? 'warning' : launch ? 'pass-filled' : activeFile ? 'file-code'
      : project.projectSystem === 'CMake' ? 'symbol-method' : project.artifactKind === 'Library' ? 'library' : 'project');
    node.description = presentation.description;
    node.tooltip = [
      project.manifest,
      `${project.projectSystem ?? 'Ngin'}${project.libraryKind ?? project.artifactKind ? ` · ${project.libraryKind ?? project.artifactKind}` : ''}${activeFile ? ' · Owns current file' : ''}${launch ? ' · Active Project' : ''}`,
      project.projectSystem === 'CMake' ? `Configure preset: ${context.configurePreset ?? 'Select a preset'}`
        : `Configuration: ${context.configuration}`,
      project.projectSystem === 'CMake' ? `Configuration: ${context.configuration}` : `Target: ${context.target}`,
      project.projectSystem === 'CMake' ? undefined : `Toolchain: ${context.toolchain}`,
      presentation.status ? `Status: ${presentation.status}` : undefined
    ].filter(Boolean).join('\n');
    this.projectNodes.set(projectKey(project), node);
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

  private workspacePackages(packages: readonly WorkspacePackage[], projects: readonly ProjectCandidate[]): NginTreeNode {
    const node = group('Package Wrappers', 'package', () => packages.map(value => {
      const child = new NginTreeNode(value.name);
      child.workspacePackage = value;
      child.resourceUri = vscode.Uri.file(value.manifest);
      child.developmentProject = projects.find(project => project.id === value.developmentProjectId);
      child.description = child.developmentProject
        ? `Development: ${child.developmentProject.name}`
        : undefined;
      child.contextValue = child.developmentProject ? 'nginWorkspacePackageDevelopment' : 'nginWorkspacePackage';
      child.iconPath = new vscode.ThemeIcon('package');
      child.command = { command: 'vscode.open', title: 'Open Package Manifest', arguments: [child.resourceUri] };
      child.tooltip = [value.manifest, child.developmentProject
        ? `Development project: ${child.developmentProject.name}`
        : 'No development project is registered in this workspace',
      value.exportedTargets.length ? `Exports: ${value.exportedTargets.join(', ')}` : undefined,
      value.consumingProjectIds.length ? `${value.consumingProjectIds.length} consuming project(s)` : undefined]
        .filter(Boolean).join('\n');
      return child;
    }), String(packages.length), `ngin.workspace-packages:${packages.map(value => value.manifest).join('|')}`);
    node.contextValue = 'nginWorkspacePackagesGroup';
    return node;
  }

  async revealFile(project: ProjectCandidate, file: string): Promise<NginTreeNode | undefined> {
    const root = this.projectNodes.get(projectKey(project));
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
          if (discovery.packages?.length) children.push(this.workspacePackages(discovery.packages, discovery.projects));
          const files = await this.workspaceFiles(discovery.workspaceFolder, discovery.projects, workspace);
          if (files.length) children.push(group('Workspace Files', 'files', () => files, String(files.length),
            `ngin.workspace-files:${discovery.workspaceFolder}`));
          return children;
        },
        `${products.length} projects`,
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
