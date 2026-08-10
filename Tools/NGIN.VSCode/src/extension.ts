import * as path from 'node:path';
import * as vscode from 'vscode';
import { CliFailure, NginCli } from './core/cli';
import { dependencyLockPath, selectionArguments } from './core/commandArguments';
import { NginController } from './core/controller';
import { displayOptionValue } from './core/graph';
import {
  insertBuildItem,
  kindForPath,
  removeExactBuildItemIncludes,
  updateExactBuildItemPaths,
  updateProjectAttributes,
  type OffsetEdit
} from './core/manifestEdits';
import { relativeManifestPath } from './core/manifestText';
import { isWithin } from './core/paths';
import type { ProjectFileEntry } from './core/projectFiles';
import type { GraphBuildItem, ProjectCandidate } from './model';
import { NginDebugProvider } from './providers/debug';
import { NginCppConfigurationProvider } from './providers/cppTools';
import { SourceAnalysisProvider } from './providers/sourceAnalysis';
import { NginFileDecorationProvider } from './providers/fileDecorations';
import { NginTaskProvider } from './providers/tasks';
import { registerManifestCompletion } from './manifestCompletion';
import { ProjectsTreeProvider } from './ui/tree';
import { StatusBarController } from './ui/statusBar';
import { DashboardController } from './ui/dashboard';

async function openJson(title: string, value: unknown): Promise<void> {
  const document = await vscode.workspace.openTextDocument({
    language: 'json',
    content: typeof value === 'string' ? value : JSON.stringify(value, null, 2)
  });
  await vscode.window.showTextDocument(document, { preview: true });
  void vscode.window.setStatusBarMessage(`NGIN: ${title}`, 3000);
}

function register(context: vscode.ExtensionContext, command: string, callback: (...args: any[]) => unknown): void {
  context.subscriptions.push(vscode.commands.registerCommand(command, callback));
}

async function choose(
  title: string,
  values: readonly string[],
  active: string | undefined
): Promise<string | undefined> {
  if (values.length === 0) {
    void vscode.window.showWarningMessage(`The active workspace declares no ${title.toLowerCase()} choices.`);
    return undefined;
  }
  return vscode.window.showQuickPick(values.map(value => ({ label: value, description: value === active ? 'active' : undefined })), {
    title: `Select NGIN ${title}`,
    placeHolder: active
  }).then(item => item?.label);
}

function projectFileArgument(value: unknown): ProjectFileEntry | undefined {
  return (value as { projectFile?: ProjectFileEntry } | undefined)?.projectFile;
}

function projectArgument(value: unknown): ProjectCandidate | undefined {
  const wrapped = (value as { project?: ProjectCandidate } | undefined)?.project;
  if (wrapped) return wrapped;
  const direct = value as ProjectCandidate | undefined;
  return direct?.manifest && direct?.name ? direct : undefined;
}

function resourceArgument(value: unknown): vscode.Uri | undefined {
  if (value instanceof vscode.Uri) return value;
  return vscode.window.activeTextEditor?.document.uri;
}

function addOffsetEdits(
  edit: vscode.WorkspaceEdit,
  document: vscode.TextDocument,
  changes: OffsetEdit[]
): void {
  for (const change of changes) {
    edit.replace(document.uri, new vscode.Range(document.positionAt(change.start), document.positionAt(change.end)), change.text);
  }
}

async function exists(uri: vscode.Uri): Promise<boolean> {
  try {
    await vscode.workspace.fs.stat(uri);
    return true;
  } catch {
    return false;
  }
}

export async function activate(extensionContext: vscode.ExtensionContext): Promise<void> {
  const diagnostics = vscode.languages.createDiagnosticCollection('ngin-manifest');
  const compilerDiagnostics = vscode.languages.createDiagnosticCollection('ngin-compiler');
  const sourceDiagnostics = vscode.languages.createDiagnosticCollection('ngin-analysis');
  const cli = new NginCli();
  const controller = new NginController(extensionContext, cli, diagnostics, compilerDiagnostics);
  const sourceAnalysis = new SourceAnalysisProvider(extensionContext, controller, cli, sourceDiagnostics);
  const projectsTree = new ProjectsTreeProvider(controller, sourceAnalysis);
  const decorations = new NginFileDecorationProvider(controller);
  const status = new StatusBarController(controller, sourceAnalysis);
  const tasks = new NginTaskProvider(controller, cli);
  const debug = new NginDebugProvider(controller, sourceAnalysis, extensionContext);
  const cppTools = new NginCppConfigurationProvider(controller);
  const dashboard = new DashboardController(controller, sourceAnalysis);
  const projectsView = vscode.window.createTreeView('ngin.projects', { treeDataProvider: projectsTree, showCollapseAll: true });

  extensionContext.subscriptions.push(
    diagnostics,
    compilerDiagnostics,
    sourceDiagnostics,
    cli,
    controller,
    sourceAnalysis,
    projectsTree,
    projectsView,
    decorations,
    status,
    cppTools,
    dashboard,
    registerManifestCompletion(extensionContext),
    vscode.window.registerFileDecorationProvider(decorations),
    vscode.tasks.registerTaskProvider('ngin', tasks),
    vscode.debug.registerDebugConfigurationProvider('ngin', debug),
    controller.onDidChange(snapshot => {
      void vscode.commands.executeCommand('setContext', 'ngin.busy', Boolean(snapshot.busy));
      void vscode.commands.executeCommand('setContext', 'ngin.hasProject', Boolean(snapshot.context));
      void vscode.commands.executeCommand('setContext', 'ngin.hasGraph', Boolean(snapshot.graph));
    })
  );

  register(extensionContext, 'ngin.refresh', () => controller.refreshDiscovery());
  register(extensionContext, 'ngin.refreshGraph', () => controller.refreshGraph(true));
  register(extensionContext, 'ngin.showOutput', () => cli.showOutput());
  register(extensionContext, 'ngin.cancel', () => cli.cancel());
  register(extensionContext, 'ngin.openDashboard', () => dashboard.open());

  const switchBuildTarget = async (argument?: unknown): Promise<void> => {
    const candidate = projectArgument(argument);
    const current = controller.activeProject;
    const recent = extensionContext.workspaceState.get<string[]>('ngin.recentProjects', []);
    const projects = [...controller.projects].sort((left, right) =>
      Number(right.manifest === current?.manifest) - Number(left.manifest === current?.manifest)
      || (recent.indexOf(left.manifest) < 0 ? Number.MAX_SAFE_INTEGER : recent.indexOf(left.manifest))
        - (recent.indexOf(right.manifest) < 0 ? Number.MAX_SAFE_INTEGER : recent.indexOf(right.manifest))
      || left.name.localeCompare(right.name));
    const project = candidate ?? await vscode.window.showQuickPick(
      projects.map(value => ({
        label: `${value.manifest === current?.manifest ? '$(pin) ' : ''}${value.name}`,
        description: `${value.type ?? 'Project'} · ${sourceAnalysis.contextForProject(value).configuration}`,
        detail: path.relative(vscode.workspace.getWorkspaceFolder(vscode.Uri.file(value.manifest))?.uri.fsPath ?? value.directory, value.manifest),
        value
      })),
      { title: 'Switch NGIN Build Target', placeHolder: current?.name, matchOnDescription: true, matchOnDetail: true }
    ).then(item => item?.value);
    if (project) {
      await controller.selectProject(project);
      await extensionContext.workspaceState.update('ngin.recentProjects', [project.manifest, ...recent.filter(value => value !== project.manifest)].slice(0, 20));
    }
  };
  register(extensionContext, 'ngin.selectProject', switchBuildTarget);
  register(extensionContext, 'ngin.switchBuildTarget', switchBuildTarget);

  register(extensionContext, 'ngin.selectConfiguration', async (value?: string) => {
    const context = controller.requireContext();
    const selected = value ?? await choose('Configuration', controller.activeProject?.workspaceChoices?.configurations ?? [], context.configuration);
    if (selected) await controller.updateSelection({ configuration: selected, preset: undefined });
  });
  register(extensionContext, 'ngin.selectTarget', async (value?: string) => {
    const context = controller.requireContext();
    const selected = value ?? await choose('Target', controller.activeProject?.workspaceChoices?.targets ?? [], context.target);
    if (selected) await controller.updateSelection({ target: selected, preset: undefined });
  });
  register(extensionContext, 'ngin.selectToolchain', async (value?: string) => {
    const context = controller.requireContext();
    const selected = value ?? await choose('Toolchain', controller.activeProject?.workspaceChoices?.toolchains ?? [], context.toolchain);
    if (selected) await controller.updateSelection({ toolchain: selected, preset: undefined });
  });
  register(extensionContext, 'ngin.selectPreset', async (value?: string) => {
    const presets = controller.activeProject?.workspaceChoices?.presets ?? [];
    const selected = value
      ? presets.find(preset => preset.name === value)
      : await vscode.window.showQuickPick(
        presets.map(preset => ({ label: preset.name, description: preset.command, value: preset })),
        { title: 'Run NGIN Preset' }
      ).then(item => item?.value);
    if (!selected) return;
    const context = controller.requireContext();
    const args = [selected.command ?? 'build', '--preset', selected.name, '--project', context.projectManifest];
    if (context.workspaceManifest) args.push('--workspace', context.workspaceManifest);
    if (['configure', 'build', 'stage', 'run', 'test', 'publish', 'analyze', 'format'].includes(selected.command ?? 'build')) {
      args.push('--output', context.outputDirectory);
    }
    try {
      await cli.run(args, context.workspaceFolder, { cwd: path.dirname(context.projectManifest), requireTrust: true, revealOutput: true, exclusive: true });
      await controller.refreshGraph(false);
    } catch (error) {
      cli.showOutput();
      void vscode.window.showErrorMessage(error instanceof Error ? error.message : String(error));
    }
  });
  register(extensionContext, 'ngin.setOption', async (name?: string) => {
    const context = controller.requireContext();
    const graphOptions = controller.snapshot.graph?.options ?? [];
    const selectedName = name ?? await vscode.window.showQuickPick(
      graphOptions.map(option => ({ label: option.name ?? option.identity, description: displayOptionValue(option.value), value: option.name ?? option.identity })),
      { title: 'Select NGIN Option' }
    ).then(item => item?.value);
    if (!selectedName) return;
    const value = await vscode.window.showInputBox({
      title: `Set NGIN Option ${selectedName}`,
      value: context.options[selectedName] ?? displayOptionValue(graphOptions.find(option => option.name === selectedName)?.value),
      prompt: 'The CLI validates the declared Option type and allowed values.'
    });
    if (value === undefined) return;
    await controller.updateSelection({ options: { ...context.options, [selectedName]: value }, preset: undefined });
  });
  register(extensionContext, 'ngin.clearOptions', () => controller.updateSelection({ options: {}, preset: undefined }));

  register(extensionContext, 'ngin.validate', () => controller.validate(true));
  register(extensionContext, 'ngin.validateManifest', () => {
    const active = vscode.window.activeTextEditor?.document;
    return active?.languageId === 'ngin' ? controller.validateManifest(active.uri.fsPath, true) : controller.validate(true);
  });
  for (const command of ['restore', 'configure', 'build', 'stage', 'test'] as const) {
    register(extensionContext, `ngin.${command}`, async (argument?: unknown) => {
      const project = projectArgument(argument);
      const context = project ? sourceAnalysis.contextForProject(project) : undefined;
      return controller.execute(command, [], false, context);
    });
  }
  register(extensionContext, 'ngin.lock', (argument?: unknown) => {
    const project = projectArgument(argument);
    return controller.execute('lock', [], false, project ? sourceAnalysis.contextForProject(project) : undefined);
  });
  const ensureDependencyLock = async (context = controller.requireContext()): Promise<boolean> => {
    const lock = dependencyLockPath(context);
    try {
      await vscode.workspace.fs.stat(vscode.Uri.file(lock));
      return true;
    } catch {
      const answer = await vscode.window.showWarningMessage(
        'This Action requires a verified dependency lock. Create it from the active Composition now?',
        { modal: true },
        'Create Lock'
      );
      if (answer !== 'Create Lock') return false;
      return Boolean(await controller.execute('lock', [], false, context));
    }
  };
  register(extensionContext, 'ngin.analyze', (argument?: unknown) => sourceAnalysis.analyzeProject(projectArgument(argument)));
  register(extensionContext, 'ngin.enableProjectTooling', (argument?: unknown) => sourceAnalysis.enableProjectTooling(projectArgument(argument)));
  register(extensionContext, 'ngin.analyzeFile', async (argument?: unknown) => {
    const uri = resourceArgument(argument);
    if (!uri) return;
    const document = await vscode.workspace.openTextDocument(uri);
    return sourceAnalysis.analyzeDocument(document);
  });
  register(extensionContext, 'ngin.formatSources', async (argument?: unknown) => {
    const project = projectArgument(argument);
    const context = project ? sourceAnalysis.contextForProject(project) : controller.requireContext();
    if (await ensureDependencyLock(context)) return controller.execute('format', [], false, context);
    return undefined;
  });
  register(extensionContext, 'ngin.formatFile', async (argument?: unknown) => {
    const uri = resourceArgument(argument);
    if (!uri) return;
    const project = await sourceAnalysis.projectForFile(uri.fsPath);
    if (!project) return void vscode.window.showWarningMessage(`${path.basename(uri.fsPath)} is not owned by an NGIN project.`);
    const context = sourceAnalysis.contextForProject(project);
    if (await ensureDependencyLock(context)) return controller.execute('format', ['--file', uri.fsPath], false, context);
    return undefined;
  });
  register(extensionContext, 'ngin.run', async (argument?: unknown) => {
    const project = projectArgument(argument);
    const context = project ? sourceAnalysis.contextForProject(project) : controller.requireContext();
    const args = await vscode.window.showInputBox({ title: 'NGIN Run Arguments', prompt: 'Optional arguments passed to the launched program' });
    if (args === undefined) return;
    const trailing = args.trim() ? ['--', ...args.match(/(?:[^\s"]+|"[^"]*")+/g)?.map(value => value.replace(/^"|"$/g, '')) ?? []] : [];
    await controller.execute('run', trailing, false, context);
  });
  register(extensionContext, 'ngin.publish', async () => {
    const publishes = controller.snapshot.graph?.publishes ?? [];
    const selected = publishes.length <= 1 ? publishes[0]?.name : await choose('Publish', publishes.map(item => item.name ?? item.identity), undefined);
    await controller.execute('publish', selected ? [selected] : []);
  });
  register(extensionContext, 'ngin.debug', async (argument?: unknown) => {
    const project = projectArgument(argument);
    const selected = project ?? await sourceAnalysis.projectForFile(vscode.window.activeTextEditor?.document.uri.fsPath ?? '', false)
      ?? controller.activeProject;
    if (!selected) throw new Error('No NGIN project is available to debug.');
    return vscode.debug.startDebugging(undefined, {
      type: 'ngin', request: 'launch', name: `NGIN: Debug ${selected.name}`, project: selected.manifest, preStage: true
    });
  });

  const clean = async (confirm: boolean, current = controller.requireContext()): Promise<boolean> => {
    if (!isWithin(current.workspaceFolder, current.outputDirectory) || current.outputDirectory === current.workspaceFolder) {
      throw new Error(`Refusing to clean unsafe output path: ${current.outputDirectory}`);
    }
    if (confirm) {
      const answer = await vscode.window.showWarningMessage(
        `Move ${current.outputDirectory} to the trash?`,
        { modal: true },
        'Clean'
      );
      if (answer !== 'Clean') return false;
    }
    try {
      await vscode.workspace.fs.delete(vscode.Uri.file(current.outputDirectory), { recursive: true, useTrash: true });
    } catch (error) {
      if (!(error instanceof vscode.FileSystemError && error.code === 'FileNotFound')) throw error;
    }
    void vscode.window.showInformationMessage(`Cleaned ${current.projectName}.`);
    return true;
  };
  register(extensionContext, 'ngin.clean', (argument?: unknown) => {
    const project = projectArgument(argument);
    return clean(true, project ? sourceAnalysis.contextForProject(project) : controller.requireContext());
  });
  register(extensionContext, 'ngin.rebuild', async () => {
    if (await clean(true)) await controller.execute('build');
  });

  register(extensionContext, 'ngin.showGraph', async (argument?: unknown) => {
    const project = projectArgument(argument);
    const context = project ? sourceAnalysis.contextForProject(project) : controller.requireContext();
    const graph = await controller.graphForContext(context, true);
    if (!graph) throw new Error(`${context.projectName} has no resolved Composition Graph.`);
    return openJson('Composition Graph', graph);
  });
  register(extensionContext, 'ngin.inspect', async (argument?: unknown) => {
    const project = projectArgument(argument);
    const current = project ? sourceAnalysis.contextForProject(project) : controller.requireContext();
    try {
      const result = await cli.run(['inspect', ...selectionArguments(current), '--format', 'json'], current.workspaceFolder, {
        cwd: path.dirname(current.projectManifest)
      });
      await openJson('Project Inspection', result.stdout);
    } catch (error) {
      cli.showOutput();
      void vscode.window.showErrorMessage(error instanceof Error ? error.message : String(error));
    }
  });
  register(extensionContext, 'ngin.diff', async () => {
    const current = controller.requireContext();
    const selected = await vscode.window.showOpenDialog({
      title: 'Compare Composition Graph Against Project', canSelectMany: false, filters: { 'NGIN projects': ['nginproj'] }
    });
    if (!selected?.[0]) return;
    try {
      const result = await cli.run(['diff', '--against', selected[0].fsPath, ...selectionArguments(current), '--format', 'json'], current.workspaceFolder, {
        cwd: path.dirname(current.projectManifest)
      });
      await openJson('Composition Diff', result.stdout);
    } catch (error) {
      if (error instanceof CliFailure && error.result.exitCode === 2 && error.result.stdout.trim()) {
        await openJson('Composition Diff', error.result.stdout);
        return;
      }
      cli.showOutput();
      void vscode.window.showErrorMessage(error instanceof Error ? error.message : String(error));
    }
  });
  register(extensionContext, 'ngin.explain', async (identity?: string) => {
    const current = controller.requireContext();
    const selected = identity ?? await vscode.window.showInputBox({ title: 'Explain Composition Identity', prompt: 'Graph identity' });
    if (!selected) return;
    try {
      const result = await cli.run(['explain', selected, ...selectionArguments(current), '--format', 'json'], current.workspaceFolder, {
        cwd: path.dirname(current.projectManifest)
      });
      await openJson(`Explain ${selected}`, result.stdout);
    } catch (error) {
      cli.showOutput();
      void vscode.window.showErrorMessage(error instanceof Error ? error.message : String(error));
    }
  });
  register(extensionContext, 'ngin.openManifest', async (argument?: unknown) => {
    const project = projectArgument(argument);
    const manifest = project?.manifest ?? controller.requireContext().projectManifest;
    const document = await vscode.workspace.openTextDocument(vscode.Uri.file(manifest));
    await vscode.window.showTextDocument(document);
  });
  register(extensionContext, 'ngin.openGraphSource', async (item?: GraphBuildItem) => {
    const current = controller.requireContext();
    const projectDirectory = path.dirname(current.projectManifest);
    const provenance = item?.provenance;
    const source = provenance?.document
      ? path.resolve(current.workspaceFolder, provenance.document)
      : item?.path ? path.resolve(projectDirectory, item.path) : current.projectManifest;
    const document = await vscode.workspace.openTextDocument(vscode.Uri.file(source));
    const line = Math.max(0, (provenance?.line ?? 1) - 1);
    const column = Math.max(0, (provenance?.column ?? 1) - 1);
    await vscode.window.showTextDocument(document, { selection: new vscode.Range(line, column, line, column) });
  });

  register(extensionContext, 'ngin.formatManifest', async () => {
    const active = vscode.window.activeTextEditor?.document;
    const manifest = active?.languageId === 'ngin' ? active : await vscode.workspace.openTextDocument(controller.requireContext().projectManifest);
    await manifest.save();
    const current = controller.requireContext();
    try {
      await cli.run(['manifest', 'format', '--project', manifest.uri.fsPath], current.workspaceFolder, {
        cwd: path.dirname(manifest.uri.fsPath), requireTrust: true, revealOutput: true, exclusive: true
      });
      if (vscode.window.activeTextEditor?.document.uri.toString() === manifest.uri.toString()) {
        await vscode.commands.executeCommand('workbench.action.files.revert');
      }
      await controller.refreshGraph(false);
    } catch (error) {
      if (error instanceof CliFailure) cli.showOutput();
      void vscode.window.showErrorMessage(error instanceof Error ? error.message : String(error));
    }
  });

  const author = async (args: string[]): Promise<void> => {
    const current = controller.requireContext();
    try {
      const openManifest = vscode.workspace.textDocuments.find(document => document.uri.fsPath === current.projectManifest);
      if (openManifest?.isDirty && !await openManifest.save()) throw new Error('Save the active project manifest before running an authoring command.');
      await cli.run([...args, ...selectionArguments(current)], current.workspaceFolder, {
        cwd: path.dirname(current.projectManifest), requireTrust: true, revealOutput: true, exclusive: true
      });
      await controller.refreshDiscovery();
      await controller.refreshGraph(false);
    } catch (error) {
      cli.showOutput();
      void vscode.window.showErrorMessage(error instanceof Error ? error.message : String(error));
    }
  };
  register(extensionContext, 'ngin.addPackage', async () => {
    const name = await vscode.window.showInputBox({ title: 'Add NGIN Package', prompt: 'Package name', validateInput: value => value.trim() ? undefined : 'Enter a package name.' });
    if (!name) return;
    const constraint = await vscode.window.showQuickPick(['Workspace/default version', 'Compatible version', 'Exact version'], { title: `Version for ${name}` });
    if (!constraint) return;
    const args = ['add', 'package', name.trim()];
    if (constraint !== 'Workspace/default version') {
      const version = await vscode.window.showInputBox({ title: constraint, prompt: 'Semantic version' });
      if (!version) return;
      args.push(constraint === 'Exact version' ? '--exact' : '--compatible', version);
    }
    await author(args);
  });
  register(extensionContext, 'ngin.addProjectReference', async () => {
    const selected = await vscode.window.showOpenDialog({
      title: 'Add NGIN Project Reference', canSelectMany: false, filters: { 'NGIN projects': ['nginproj'] }
    });
    if (!selected?.[0]) return;
    const relative = path.relative(path.dirname(controller.requireContext().projectManifest), selected[0].fsPath).split(path.sep).join('/');
    await author(['add', 'project-reference', relative]);
  });
  register(extensionContext, 'ngin.addAction', async () => {
    const action = await vscode.window.showInputBox({ title: 'Add NGIN Action', prompt: 'Package::Action' });
    if (!action) return;
    const kind = await vscode.window.showQuickPick(['Generate', 'Analyze', 'Format', 'Validate', 'Custom'], { title: 'Action kind' });
    if (!kind) return;
    await author(['add', 'action', action, '--kind', kind]);
  });
  register(extensionContext, 'ngin.editProduct', async () => {
    const current = controller.requireContext();
    const graph = controller.snapshot.graph;
    const field = await vscode.window.showQuickPick(['Name', 'Type', 'Version', 'Linkage'], { title: 'Edit NGIN Product' });
    if (!field) return;
    let value: string | undefined;
    if (field === 'Type') {
      value = await vscode.window.showQuickPick(['Application', 'Library', 'Tool', 'Test', 'Benchmark', 'Plugin', 'Module', 'External'], { title: 'Product Type' });
    } else if (field === 'Linkage') {
      value = await vscode.window.showQuickPick(['Static', 'Shared', 'HeaderOnly', 'Remove attribute'], { title: 'Library Linkage' });
      if (value === 'Remove attribute') value = undefined;
    } else {
      value = await vscode.window.showInputBox({
        title: `Product ${field}`,
        value: field === 'Name' ? graph?.product.name ?? current.projectName : field === 'Version' ? graph?.product.version ?? '' : '',
        prompt: field === 'Version' ? 'Leave empty to remove the Version attribute.' : undefined
      });
      if (value === undefined) return;
      if (field === 'Version' && value === '') value = undefined;
    }
    if (field !== 'Linkage' && value === undefined) return;
    const document = await vscode.workspace.openTextDocument(vscode.Uri.file(current.projectManifest));
    const workspaceEdit = new vscode.WorkspaceEdit();
    addOffsetEdits(workspaceEdit, document, updateProjectAttributes(document.getText(), { [field]: value }));
    await vscode.workspace.applyEdit(workspaceEdit);
    await vscode.window.showTextDocument(document, { preview: false, preserveFocus: true });
  });

  const changeMembership = async (argument: unknown, include: boolean): Promise<void> => {
    const entry = projectFileArgument(argument);
    const uri = argument instanceof vscode.Uri ? argument : undefined;
    if (entry?.directory || (!entry && !uri)) return;
    const target = entry?.path ?? uri!.fsPath;
    const project = entry ? controller.activeProject : await sourceAnalysis.projectForFile(target);
    if (!project) return void vscode.window.showWarningMessage(`${path.basename(target)} is not owned by an NGIN project.`);
    const current = sourceAnalysis.contextForProject(project);
    const document = await vscode.workspace.openTextDocument(vscode.Uri.file(current.projectManifest));
    const relative = relativeManifestPath(path.dirname(current.projectManifest), target);
    const kind = (entry?.kind && ['Source', 'Header', 'CxxModule', 'Resource'].includes(entry.kind)
      ? entry.kind : kindForPath(relative)) as 'Source' | 'Header' | 'CxxModule' | 'Resource';
    const change = insertBuildItem(document.getText(), kind, include ? 'Include' : 'Remove', relative);
    if (!change) {
      void vscode.window.showInformationMessage(`${relative} already has an exact ${include ? 'include' : 'remove'} rule.`);
      return;
    }
    const workspaceEdit = new vscode.WorkspaceEdit();
    addOffsetEdits(workspaceEdit, document, [change]);
    await vscode.workspace.applyEdit(workspaceEdit);
    await vscode.window.showTextDocument(document, { preview: false, preserveFocus: true });
    void vscode.window.setStatusBarMessage(`NGIN: ${include ? 'included' : 'excluded'} ${relative}; save to validate`, 5000);
  };
  register(extensionContext, 'ngin.includeFile', (argument: unknown) => changeMembership(argument, true));
  register(extensionContext, 'ngin.excludeFile', (argument: unknown) => changeMembership(argument, false));

  register(extensionContext, 'ngin.newFile', async (argument?: unknown) => {
    const current = controller.requireContext();
    const entry = projectFileArgument(argument);
    const projectDirectory = path.dirname(current.projectManifest);
    const base = entry?.directory ? entry.path : entry ? path.dirname(entry.path) : projectDirectory;
    const name = await vscode.window.showInputBox({ title: 'New Project File', prompt: 'File name or relative path' });
    if (!name) return;
    const target = path.resolve(base, name);
    if (!isWithin(projectDirectory, target)) throw new Error('The new file must remain inside the active project.');
    const uri = vscode.Uri.file(target);
    if (await exists(uri)) throw new Error(`A file already exists at ${target}.`);
    await vscode.workspace.fs.createDirectory(vscode.Uri.file(path.dirname(target)));
    const workspaceEdit = new vscode.WorkspaceEdit();
    workspaceEdit.createFile(uri, { ignoreIfExists: false });
    const manifest = await vscode.workspace.openTextDocument(vscode.Uri.file(current.projectManifest));
    const relative = relativeManifestPath(projectDirectory, target);
    const membership = insertBuildItem(manifest.getText(), kindForPath(relative), 'Include', relative);
    if (membership) addOffsetEdits(workspaceEdit, manifest, [membership]);
    if (!await vscode.workspace.applyEdit(workspaceEdit)) throw new Error('VS Code could not create the project file.');
    controller.refreshPresentation();
    await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(uri));
  });
  register(extensionContext, 'ngin.newFolder', async (argument?: unknown) => {
    const current = controller.requireContext();
    const entry = projectFileArgument(argument);
    const projectDirectory = path.dirname(current.projectManifest);
    const base = entry?.directory ? entry.path : entry ? path.dirname(entry.path) : projectDirectory;
    const name = await vscode.window.showInputBox({ title: 'New Project Folder', prompt: 'Folder name or relative path' });
    if (!name) return;
    const target = path.resolve(base, name);
    if (!isWithin(projectDirectory, target)) throw new Error('The new folder must remain inside the active project.');
    await vscode.workspace.fs.createDirectory(vscode.Uri.file(target));
    controller.refreshPresentation();
  });
  register(extensionContext, 'ngin.renameFile', async (argument: unknown) => {
    const entry = projectFileArgument(argument);
    if (!entry) return;
    const current = controller.requireContext();
    const projectDirectory = path.dirname(current.projectManifest);
    const name = await vscode.window.showInputBox({ title: 'Rename Project Item', value: entry.name });
    if (!name || name === entry.name) return;
    const target = path.resolve(path.dirname(entry.path), name);
    if (!isWithin(projectDirectory, target)) throw new Error('The renamed item must remain inside the active project.');
    const manifest = await vscode.workspace.openTextDocument(vscode.Uri.file(current.projectManifest));
    const before = relativeManifestPath(projectDirectory, entry.path);
    const after = relativeManifestPath(projectDirectory, target);
    const workspaceEdit = new vscode.WorkspaceEdit();
    workspaceEdit.renameFile(vscode.Uri.file(entry.path), vscode.Uri.file(target), { overwrite: false });
    addOffsetEdits(workspaceEdit, manifest, updateExactBuildItemPaths(manifest.getText(), before, after));
    if (!await vscode.workspace.applyEdit(workspaceEdit)) throw new Error('VS Code could not rename the project item.');
    controller.refreshPresentation();
  });
  register(extensionContext, 'ngin.duplicateFile', async (argument: unknown) => {
    const entry = projectFileArgument(argument);
    if (!entry || entry.directory) return;
    const extension = path.extname(entry.name);
    const stem = path.basename(entry.name, extension);
    const name = await vscode.window.showInputBox({ title: 'Duplicate Project File', value: `${stem}.copy${extension}` });
    if (!name) return;
    const target = path.resolve(path.dirname(entry.path), name);
    const current = controller.requireContext();
    const projectDirectory = path.dirname(current.projectManifest);
    if (!isWithin(projectDirectory, target)) throw new Error('The duplicate must remain inside the active project.');
    const targetUri = vscode.Uri.file(target);
    if (await exists(targetUri)) throw new Error(`A file already exists at ${target}.`);
    const bytes = await vscode.workspace.fs.readFile(vscode.Uri.file(entry.path));
    const workspaceEdit = new vscode.WorkspaceEdit();
    workspaceEdit.createFile(targetUri, { contents: bytes });
    if (entry.state === 'selected') {
      const manifest = await vscode.workspace.openTextDocument(vscode.Uri.file(current.projectManifest));
      const relative = relativeManifestPath(projectDirectory, target);
      const membership = insertBuildItem(manifest.getText(), (entry.kind as 'Source' | 'Header' | 'CxxModule' | 'Resource' | undefined) ?? kindForPath(relative), 'Include', relative);
      if (membership) addOffsetEdits(workspaceEdit, manifest, [membership]);
    }
    if (!await vscode.workspace.applyEdit(workspaceEdit)) throw new Error('VS Code could not duplicate the project file.');
    controller.refreshPresentation();
    await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(targetUri));
  });
  register(extensionContext, 'ngin.deleteFile', async (argument: unknown) => {
    const entry = projectFileArgument(argument);
    if (!entry) return;
    const answer = await vscode.window.showWarningMessage(`Move ${entry.relativePath} to the trash?`, { modal: true }, 'Delete');
    if (answer !== 'Delete') return;
    const current = controller.requireContext();
    const projectDirectory = path.dirname(current.projectManifest);
    if (!isWithin(projectDirectory, entry.path)) throw new Error('Only items inside the active project can be deleted.');
    const manifest = await vscode.workspace.openTextDocument(vscode.Uri.file(current.projectManifest));
    const relative = relativeManifestPath(projectDirectory, entry.path);
    const edits = removeExactBuildItemIncludes(manifest.getText(), relative);
    if (edits.length) {
      const workspaceEdit = new vscode.WorkspaceEdit();
      addOffsetEdits(workspaceEdit, manifest, edits);
      await vscode.workspace.applyEdit(workspaceEdit);
    }
    await vscode.workspace.fs.delete(vscode.Uri.file(entry.path), { recursive: entry.directory, useTrash: true });
    controller.refreshPresentation();
  });
  register(extensionContext, 'ngin.revealFile', async (argument: unknown) => {
    const entry = projectFileArgument(argument);
    if (entry) await vscode.commands.executeCommand('revealFileInOS', vscode.Uri.file(entry.path));
  });
  register(extensionContext, 'ngin.revealOwningProject', async (argument?: unknown) => {
    const uri = resourceArgument(argument);
    if (!uri) return;
    const project = await sourceAnalysis.projectForFile(uri.fsPath);
    if (!project) return void vscode.window.showInformationMessage(`${path.basename(uri.fsPath)} is not owned by an NGIN project.`);
    await vscode.commands.executeCommand('ngin.projects.focus');
    await projectsTree.getChildren();
    const node = projectsTree.projectNode(project.manifest);
    if (node) await projectsView.reveal(node, { focus: true, select: true });
  });

  extensionContext.subscriptions.push(vscode.workspace.onDidSaveTextDocument(document => {
    if (document.languageId !== 'ngin') return;
    controller.invalidateConfiguration();
    const validation = vscode.workspace.getConfiguration('ngin').get<boolean>('validateOnSave', true)
      ? controller.validateManifest(document.uri.fsPath, false)
      : Promise.resolve(true);
    void validation.then(async () => {
      if (document.uri.fsPath.endsWith('.ngin') || document.uri.fsPath.endsWith('.nginproj')) {
        sourceAnalysis.invalidate(document.uri.fsPath.endsWith('.nginproj') ? document.uri.fsPath : undefined);
        await controller.refreshDiscovery();
      } else {
        await controller.refreshGraph(false);
      }
    });
  }));

  await controller.initialize();
  await cppTools.initialize();
  sourceAnalysis.initialize();
}

export function deactivate(): void {}
