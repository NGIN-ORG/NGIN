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
import { attributeChoices, childElementNames, loadManifestMetadata } from './core/manifestMetadata';
import { createProjectTemplate } from './core/projectTemplates';
import { projectCanLaunch } from './core/projectCapabilities';
import type { ProjectFileEntry } from './core/projectFiles';
import type { GraphBuildItem, ProjectCandidate } from './model';
import { NginDebugProvider } from './providers/debug';
import { NginCppConfigurationProvider } from './providers/cppTools';
import { SourceAnalysisProvider } from './providers/sourceAnalysis';
import { NginFileDecorationProvider } from './providers/fileDecorations';
import { NginTaskProvider } from './providers/tasks';
import { NginTestingProvider } from './providers/testing';
import { AnalyzerCodeActionProvider, ManifestCodeActionProvider } from './providers/codeActions';
import { registerManifestCompletion } from './manifestCompletion';
import { ProjectsTreeProvider, type ProjectsViewMode } from './ui/tree';
import { StatusBarController } from './ui/statusBar';
import { DashboardController } from './ui/dashboard';
import { back, inputStep, pickStep } from './ui/inputFlow';

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
  const entry = projectFileArgument(value);
  if (entry && !entry.directory) return vscode.Uri.file(entry.path);
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
  const metadata = loadManifestMetadata(vscode.Uri.joinPath(
    extensionContext.extensionUri, 'schemas', 'manifest-editor-metadata.json').fsPath);
  const diagnostics = vscode.languages.createDiagnosticCollection('ngin-manifest');
  const compilerDiagnostics = vscode.languages.createDiagnosticCollection('ngin-compiler');
  const sourceDiagnostics = vscode.languages.createDiagnosticCollection('ngin-analysis');
  const cli = new NginCli();
  const controller = new NginController(extensionContext, cli, diagnostics, compilerDiagnostics);
  const sourceAnalysis = new SourceAnalysisProvider(extensionContext, controller, cli, sourceDiagnostics);
  const projectsViewMode = extensionContext.workspaceState.get<ProjectsViewMode>('ngin.projectsViewMode', 'project');
  const projectsTree = new ProjectsTreeProvider(controller, sourceAnalysis, projectsViewMode);
  const decorations = new NginFileDecorationProvider(controller);
  const status = new StatusBarController(controller, sourceAnalysis);
  const tasks = new NginTaskProvider(controller, cli);
  const debug = new NginDebugProvider(controller, sourceAnalysis, extensionContext);
  const cppTools = new NginCppConfigurationProvider(controller);
  const dashboard = new DashboardController(controller, sourceAnalysis);
  const testing = new NginTestingProvider(controller, sourceAnalysis);
  const projectsView = vscode.window.createTreeView('ngin.projects', { treeDataProvider: projectsTree, showCollapseAll: true });
  projectsView.description = projectsViewMode === 'files' ? 'Files' : 'Project';
  const canLaunch = (project: ProjectCandidate | undefined): boolean => {
    const snapshot = controller.snapshot;
    return projectCanLaunch(project, snapshot.graph, snapshot.context?.projectManifest);
  };
  const explainNotLaunchable = async (project: ProjectCandidate): Promise<void> => {
    const kind = project.type ?? 'Project';
    const message = kind === 'Library'
      ? `${project.name} is a Library and cannot be run or debugged. Build it or select an application with a Launch configuration.`
      : `${project.name} has no Launch configuration. Add Launch intent or select a launchable project.`;
    const action = await vscode.window.showInformationMessage(message, 'Build Project');
    if (action === 'Build Project') await vscode.commands.executeCommand('ngin.build', project);
  };

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
    testing,
    projectsView.onDidChangeSelection(event => {
      const node = event.selection[0];
      const project = node?.contextValue?.startsWith('nginProjectRoot') ? node.project : undefined;
      if (project && controller.activeProject?.manifest !== project.manifest) void controller.selectProject(project);
    }),
    registerManifestCompletion(extensionContext),
    vscode.window.registerFileDecorationProvider(decorations),
    vscode.tasks.registerTaskProvider('ngin', tasks),
    vscode.debug.registerDebugConfigurationProvider('ngin', debug),
    vscode.languages.registerCodeActionsProvider(
      ['c', 'cpp', 'cuda-cpp', 'objective-c', 'objective-cpp'].map(language => ({ language, scheme: 'file' })),
      new AnalyzerCodeActionProvider(sourceAnalysis),
      { providedCodeActionKinds: AnalyzerCodeActionProvider.providedCodeActionKinds }
    ),
    vscode.languages.registerCodeActionsProvider(
      { language: 'ngin' },
      new ManifestCodeActionProvider(metadata),
      { providedCodeActionKinds: ManifestCodeActionProvider.providedCodeActionKinds }
    ),
    controller.onDidChange(snapshot => {
      void vscode.commands.executeCommand('setContext', 'ngin.busy', Boolean(snapshot.busy));
      void vscode.commands.executeCommand('setContext', 'ngin.hasProject', Boolean(snapshot.context));
      void vscode.commands.executeCommand('setContext', 'ngin.hasGraph', Boolean(snapshot.graph));
      void vscode.commands.executeCommand('setContext', 'ngin.hasProjects', controller.projects.length > 0);
      void vscode.commands.executeCommand('setContext', 'ngin.hasGraphError', Boolean(snapshot.graphError));
      void vscode.commands.executeCommand('setContext', 'ngin.canLaunch', canLaunch(controller.activeProject));
      void vscode.commands.executeCommand('setContext', 'ngin.hasTesting', Boolean(snapshot.graph?.testing) || snapshot.graph?.product.type === 'Test');
      void vscode.commands.executeCommand('setContext', 'ngin.hasAnalyze', Boolean(snapshot.graph?.actions.some(action => action.kind === 'Analyze')));
      void vscode.commands.executeCommand('setContext', 'ngin.hasFormat', Boolean(snapshot.graph?.actions.some(action => action.kind === 'Format')));
    })
  );

  register(extensionContext, 'ngin.refresh', () => controller.refreshDiscovery());
  const setProjectsViewMode = async (mode: ProjectsViewMode): Promise<void> => {
    projectsTree.setMode(mode);
    projectsView.description = mode === 'files' ? 'Files' : 'Project';
    await extensionContext.workspaceState.update('ngin.projectsViewMode', mode);
    await vscode.commands.executeCommand('setContext', 'ngin.projectsViewMode', mode);
  };
  register(extensionContext, 'ngin.showFilesView', () => setProjectsViewMode('files'));
  register(extensionContext, 'ngin.showProjectView', () => setProjectsViewMode('project'));
  register(extensionContext, 'ngin.refreshGraph', () => controller.refreshGraph(true));
  register(extensionContext, 'ngin.showOutput', () => cli.showOutput());
  register(extensionContext, 'ngin.cancel', () => cli.cancel());
  register(extensionContext, 'ngin.openDashboard', () => dashboard.open());
  register(extensionContext, 'ngin.openSettings', () =>
    vscode.commands.executeCommand('workbench.action.openSettings', '@ext:ngin.ngin-tools'));
  register(extensionContext, 'ngin.openDocumentation', () =>
    vscode.env.openExternal(vscode.Uri.parse('https://github.com/NGIN-ORG/NGIN/blob/main/Tools/NGIN.VSCode/README.md')));
  register(extensionContext, 'ngin.openWalkthrough', () =>
    vscode.commands.executeCommand('workbench.action.openWalkthrough', 'ngin.ngin-tools#ngin.gettingStarted'));

  register(extensionContext, 'ngin.createProject', async () => {
    const folders = vscode.workspace.workspaceFolders ?? [];
    if (!folders.length) {
      const action = await vscode.window.showInformationMessage('Open a folder before creating an NGIN project.', 'Open Folder');
      if (action === 'Open Folder') await vscode.commands.executeCommand('workbench.action.files.openFolder');
      return;
    }
    const folder = folders.length === 1 ? folders[0] : await vscode.window.showWorkspaceFolderPick({
      placeHolder: 'Choose the workspace folder for the new project'
    });
    if (!folder) return;
    const productTypes = attributeChoices(metadata, 'project.root', 'Type');
    let name = '';
    let type = '';
    let relativeDirectory = '';
    let step = 1;
    while (step <= 3) {
      if (step === 1) {
        const value = await inputStep({
          title: 'Create NGIN project', step, totalSteps: 3, value: name,
          prompt: 'Project name', placeholder: 'MyApp',
          validate: candidate => /^[A-Za-z_][A-Za-z0-9_.-]*$/u.test(candidate.trim())
            ? undefined : 'Use a name beginning with a letter or underscore.'
        });
        if (value === undefined) return;
        if (value === back) continue;
        name = value.trim();
        if (!relativeDirectory) relativeDirectory = name;
        step++;
        continue;
      }
      if (step === 2) {
        const value = await pickStep({ title: 'Create NGIN project', step, totalSteps: 3 },
          productTypes.map(candidate => ({
            label: candidate,
            description: candidate === 'Application' ? 'Executable application'
              : candidate === 'Library' ? 'Reusable linked library'
                : candidate === 'Plugin' ? 'Dynamically loaded product'
                  : candidate === 'External' ? 'Externally built product' : `${candidate} product`,
            value: candidate
          })));
        if (value === undefined) return;
        if (value === back) { step--; continue; }
        type = value;
        step++;
        continue;
      }
      const value = await inputStep({
        title: 'Create NGIN project', step, totalSteps: 3, value: relativeDirectory,
        prompt: 'Folder relative to the workspace', placeholder: name,
        validate: candidate => {
          if (!candidate.trim()) return 'Enter a project folder or . for the workspace root.';
          const target = path.resolve(folder.uri.fsPath, candidate.trim());
          return isWithin(folder.uri.fsPath, target) ? undefined : 'The project must stay inside the workspace folder.';
        }
      });
      if (value === undefined) return;
      if (value === back) { step--; continue; }
      relativeDirectory = value.trim();
      step++;
    }

    const directory = path.resolve(folder.uri.fsPath, relativeDirectory);
    const manifest = vscode.Uri.file(path.join(directory, `${name}.nginproj`));
    if (await exists(manifest)) throw new Error(`A project manifest already exists at ${manifest.fsPath}.`);
    const template = createProjectTemplate(name, type);
    await vscode.workspace.fs.createDirectory(vscode.Uri.file(directory));
    await vscode.workspace.fs.writeFile(manifest, new TextEncoder().encode(template.manifest));
    for (const [relative, contents] of Object.entries(template.files)) {
      const file = vscode.Uri.file(path.join(directory, ...relative.split('/')));
      await vscode.workspace.fs.createDirectory(vscode.Uri.file(path.dirname(file.fsPath)));
      await vscode.workspace.fs.writeFile(file, new TextEncoder().encode(contents));
    }
    await controller.refreshDiscovery();
    const project = controller.projects.find(candidate => path.resolve(candidate.manifest) === path.resolve(manifest.fsPath));
    if (project) await controller.selectProject(project);
    await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(manifest), { preview: false });
  });

  const setDefaultProject = async (argument?: unknown): Promise<void> => {
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
      { title: 'Select NGIN Project', placeHolder: current?.name, matchOnDescription: true, matchOnDetail: true }
    ).then(item => item?.value);
    if (project) {
      await controller.selectProject(project);
      await extensionContext.workspaceState.update('ngin.recentProjects', [project.manifest, ...recent.filter(value => value !== project.manifest)].slice(0, 20));
    }
  };
  register(extensionContext, 'ngin.selectProject', setDefaultProject);
  register(extensionContext, 'ngin.setDefaultProject', setDefaultProject);

  register(extensionContext, 'ngin.selectConfiguration', async (value?: string) => {
    const context = controller.requireContext();
    const selected = value ?? await choose('Configuration', controller.activeProject?.workspaceChoices?.configurations ?? [], context.configuration);
    if (selected) await controller.updateSelection({ configuration: selected, preset: undefined });
  });
  register(extensionContext, 'ngin.selectTarget', async (value?: string) => {
    const context = controller.requireContext();
    const selected = value ?? await choose('Platform Target', controller.activeProject?.workspaceChoices?.targets ?? [], context.target);
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
        'Formatting needs a verified dependency lock. Create it now?',
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
    const selected = project ?? controller.activeProject;
    if (!selected) throw new Error('No NGIN project is selected.');
    if (!canLaunch(selected)) return explainNotLaunchable(selected);
    const context = sourceAnalysis.contextForProject(selected);
    await controller.execute('run', [], false, context);
  });
  register(extensionContext, 'ngin.runWithArguments', async (argument?: unknown) => {
    const project = projectArgument(argument);
    const selected = project ?? controller.activeProject;
    if (!selected) throw new Error('No NGIN project is selected.');
    if (!canLaunch(selected)) return explainNotLaunchable(selected);
    const context = sourceAnalysis.contextForProject(selected);
    const args = await vscode.window.showInputBox({
      title: `Run ${context.projectName} with arguments`,
      prompt: 'Arguments appended to the selected launch configuration',
      placeHolder: '--example value'
    });
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
    const owner = project ?? await sourceAnalysis.projectForFile(vscode.window.activeTextEditor?.document.uri.fsPath ?? '', false);
    const selected = owner && canLaunch(owner) ? owner : controller.activeProject;
    if (!selected) throw new Error('No NGIN project is available to debug.');
    if (!canLaunch(selected)) return explainNotLaunchable(selected);
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
        `Move the build output for ${current.projectName} to the trash?`,
        { modal: true },
        'Move to Trash'
      );
      if (answer !== 'Move to Trash') return false;
    }
    try {
      await vscode.workspace.fs.delete(vscode.Uri.file(current.outputDirectory), { recursive: true, useTrash: true });
    } catch (error) {
      if (!(error instanceof vscode.FileSystemError && error.code === 'FileNotFound')) throw error;
    }
    void vscode.window.setStatusBarMessage(`$(trash) Removed build output for ${current.projectName}`, 3000);
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
    let name = '';
    let constraint: 'default' | 'compatible' | 'exact' = 'default';
    let version = '';
    let step = 1;
    while (step <= 3) {
      if (step === 1) {
        const value = await inputStep({
          title: 'Add package', step, totalSteps: 3, value: name, prompt: 'Package name',
          placeholder: 'Example.Package', validate: candidate => candidate.trim() ? undefined : 'Enter a package name.'
        });
        if (value === undefined) return;
        if (value === back) continue;
        name = value.trim();
        step++;
        continue;
      }
      if (step === 2) {
        const value = await pickStep({ title: 'Add package', step, totalSteps: 3 }, [
          { label: 'Workspace default', description: 'Use workspace package policy', value: 'default' as const },
          { label: 'Compatible version', description: 'Allow compatible package versions', value: 'compatible' as const },
          { label: 'Exact version', description: 'Require one package version', value: 'exact' as const }
        ]);
        if (value === undefined) return;
        if (value === back) { step--; continue; }
        constraint = value;
        if (constraint === 'default') { step = 4; break; }
        step++;
        continue;
      }
      const value = await inputStep({
        title: 'Add package', step, totalSteps: 3, value: version,
        prompt: constraint === 'exact' ? 'Exact semantic version' : 'Compatible version',
        placeholder: '1.0.0', validate: candidate => candidate.trim() ? undefined : 'Enter a version.'
      });
      if (value === undefined) return;
      if (value === back) { step--; continue; }
      version = value.trim();
      step++;
    }
    const args = ['add', 'package', name];
    if (constraint !== 'default') args.push(constraint === 'exact' ? '--exact' : '--compatible', version);
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
    const kinds = [...new Set(['Generate', ...childElementNames(metadata, 'project.tooling')])];
    let action = '';
    let kind = '';
    let step = 1;
    while (step <= 2) {
      if (step === 1) {
        const value = await inputStep({
          title: 'Add action', step, totalSteps: 2, value: action, prompt: 'Exported action identity',
          placeholder: 'Package::Action', validate: candidate => candidate.includes('::') ? undefined : 'Use Package::Action.'
        });
        if (value === undefined) return;
        if (value === back) continue;
        action = value.trim();
        step++;
        continue;
      }
      const value = await pickStep({ title: 'Add action', step, totalSteps: 2 }, kinds.map(candidate => ({
        label: candidate,
        description: candidate === 'Generate' ? 'Generate project inputs before building' : `${candidate} tooling action`,
        value: candidate
      })));
      if (value === undefined) return;
      if (value === back) { step--; continue; }
      kind = value;
      step++;
    }
    await author(['add', 'action', action, '--kind', kind]);
  });
  register(extensionContext, 'ngin.editProduct', async () => {
    const current = controller.requireContext();
    const graph = controller.snapshot.graph;
    const fields = ['Name', 'Type', 'Version', ...(graph?.product.type === 'Library' ? ['Linkage'] : [])];
    let field = '';
    let value: string | undefined;
    let step = 1;
    while (step <= 2) {
      if (step === 1) {
        const selected = await pickStep({ title: 'Edit product', step, totalSteps: 2 }, fields.map(candidate => ({
          label: candidate, value: candidate,
          description: candidate === 'Linkage' ? 'Library artifact linkage' : `Project ${candidate.toLowerCase()}`
        })));
        if (selected === undefined) return;
        field = selected as string;
        step++;
        continue;
      }
      if (field === 'Type' || field === 'Linkage') {
        const choices = field === 'Type'
          ? attributeChoices(metadata, 'project.root', 'Type')
          : [...attributeChoices(metadata, 'project.root', 'Linkage'), 'Remove attribute'];
        const selected = await pickStep({ title: 'Edit product', step, totalSteps: 2 }, choices.map(candidate => ({
          label: candidate, value: candidate,
          description: candidate === 'Remove attribute' ? 'Use the default library linkage' : undefined
        })));
        if (selected === undefined) return;
        if (selected === back) { step--; continue; }
        value = selected === 'Remove attribute' ? undefined : selected;
      } else {
        const selected = await inputStep({
          title: 'Edit product', step, totalSteps: 2,
          value: field === 'Name' ? graph?.product.name ?? current.projectName : graph?.product.version ?? '',
          prompt: field === 'Version' ? 'Leave empty to remove the version.' : 'Product name',
          validate: candidate => field === 'Name' && !candidate.trim() ? 'Enter a product name.' : undefined
        });
        if (selected === undefined) return;
        if (selected === back) { step--; continue; }
        value = selected.trim() || undefined;
      }
      step++;
    }
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
    const project = projectArgument(argument) ?? await sourceAnalysis.projectForFile(target);
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

  const createProjectFile = async (
    argument?: unknown,
    preset?: { kind: 'Source' | 'Header'; path: string; title: string; extension: string; contents?: string }
  ): Promise<void> => {
    const project = projectArgument(argument);
    const current = project ? sourceAnalysis.contextForProject(project) : controller.requireContext();
    const entry = projectFileArgument(argument);
    const projectDirectory = path.dirname(current.projectManifest);
    const base = preset ? projectDirectory : entry?.directory ? entry.path : entry ? path.dirname(entry.path) : projectDirectory;
    const value = preset?.path ?? '';
    const extension = preset?.extension ?? '';
    const stemEnd = value ? value.length - path.extname(value).length : 0;
    const name = await vscode.window.showInputBox({
      title: preset?.title ?? 'New Project File',
      prompt: preset ? 'Path relative to the project; it will be added to the build automatically.' : 'File name or relative path',
      value,
      valueSelection: value ? [Math.max(0, value.lastIndexOf('/') + 1), stemEnd] : undefined,
      validateInput: candidate => candidate.trim() ? undefined : 'Enter a file name.'
    });
    if (!name?.trim()) return;
    const requested = preset && !path.extname(name.trim()) ? `${name.trim()}${extension}` : name.trim();
    const target = path.resolve(base, requested);
    if (!isWithin(projectDirectory, target)) throw new Error('The new file must remain inside its project.');
    const uri = vscode.Uri.file(target);
    if (await exists(uri)) throw new Error(`A file already exists at ${target}.`);
    await vscode.workspace.fs.createDirectory(vscode.Uri.file(path.dirname(target)));
    const workspaceEdit = new vscode.WorkspaceEdit();
    workspaceEdit.createFile(uri, {
      ignoreIfExists: false,
      contents: preset?.contents ? new TextEncoder().encode(preset.contents) : undefined
    });
    const manifest = await vscode.workspace.openTextDocument(vscode.Uri.file(current.projectManifest));
    const relative = relativeManifestPath(projectDirectory, target);
    const membership = insertBuildItem(manifest.getText(), preset?.kind ?? kindForPath(relative), 'Include', relative);
    if (membership) addOffsetEdits(workspaceEdit, manifest, [membership]);
    if (!await vscode.workspace.applyEdit(workspaceEdit)) throw new Error('VS Code could not create the project file.');
    controller.refreshPresentation();
    await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(uri));
  };
  register(extensionContext, 'ngin.newFile', (argument?: unknown) => createProjectFile(argument));
  register(extensionContext, 'ngin.newSourceFile', (argument?: unknown) => createProjectFile(argument, {
    kind: 'Source', path: 'src/NewSource.cpp', title: 'New C++ Source File', extension: '.cpp'
  }));
  register(extensionContext, 'ngin.newHeaderFile', (argument?: unknown) => createProjectFile(argument, {
    kind: 'Header', path: 'include/NewHeader.hpp', title: 'New C++ Header File', extension: '.hpp', contents: '#pragma once\n'
  }));
  register(extensionContext, 'ngin.newFolder', async (argument?: unknown) => {
    const project = projectArgument(argument);
    const current = project ? sourceAnalysis.contextForProject(project) : controller.requireContext();
    const entry = projectFileArgument(argument);
    const projectDirectory = path.dirname(current.projectManifest);
    const base = entry?.directory ? entry.path : entry ? path.dirname(entry.path) : projectDirectory;
    const name = await vscode.window.showInputBox({ title: 'New Project Folder', prompt: 'Folder name or relative path' });
    if (!name) return;
    const target = path.resolve(base, name);
    if (!isWithin(projectDirectory, target)) throw new Error('The new folder must remain inside its project.');
    await vscode.workspace.fs.createDirectory(vscode.Uri.file(target));
    controller.refreshPresentation();
  });
  register(extensionContext, 'ngin.renameFile', async (argument: unknown) => {
    const entry = projectFileArgument(argument);
    if (!entry) return;
    const project = projectArgument(argument);
    const current = project ? sourceAnalysis.contextForProject(project) : controller.requireContext();
    const projectDirectory = path.dirname(current.projectManifest);
    const name = await vscode.window.showInputBox({ title: 'Rename Project Item', value: entry.name });
    if (!name || name === entry.name) return;
    const target = path.resolve(path.dirname(entry.path), name);
    if (!isWithin(projectDirectory, target)) throw new Error('The renamed item must remain inside its project.');
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
    const project = projectArgument(argument);
    const current = project ? sourceAnalysis.contextForProject(project) : controller.requireContext();
    const projectDirectory = path.dirname(current.projectManifest);
    if (!isWithin(projectDirectory, target)) throw new Error('The duplicate must remain inside its project.');
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
    const answer = await vscode.window.showWarningMessage(`Move ${entry.relativePath} to the trash?`, { modal: true }, 'Move to Trash');
    if (answer !== 'Move to Trash') return;
    const project = projectArgument(argument);
    const current = project ? sourceAnalysis.contextForProject(project) : controller.requireContext();
    const projectDirectory = path.dirname(current.projectManifest);
    if (!isWithin(projectDirectory, entry.path)) throw new Error('Only items inside their project can be deleted.');
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

  await vscode.commands.executeCommand('setContext', 'ngin.projectsViewMode', projectsViewMode);
  await controller.initialize();
  await cppTools.initialize();
  sourceAnalysis.initialize();
}

export function deactivate(): void {}
