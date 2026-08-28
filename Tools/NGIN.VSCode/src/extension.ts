import * as path from 'node:path';
import * as vscode from 'vscode';
import { CliFailure, NginCli } from './core/cli';
import { dependencyLockPath, selectionArguments } from './core/commandArguments';
import { NginController } from './core/controller';
import { displayOptionValue } from './core/graph';
import {
  insertBuildItem,
  kindForPath,
  updateProjectAttributes,
  type OffsetEdit
} from './core/manifestEdits';
import { relativeManifestPath } from './core/manifestText';
import { isWithin } from './core/paths';
import { attributeChoices, childElementNames, documentRoots, loadManifestMetadata } from './core/manifestMetadata';
import { createProjectTemplate } from './core/projectTemplates';
import { projectCanRun } from './core/projectCapabilities';
import { graphOwnsFile } from './core/projectOwnership';
import type { ProjectFileEntry } from './core/projectFiles';
import type { GraphBuildItem, GraphNamedNode, ProjectCandidate } from './model';
import { NginDebugProvider } from './providers/debug';
import { NginCppConfigurationProvider } from './providers/cppTools';
import { SourceAnalysisProvider } from './providers/sourceAnalysis';
import { NginFileDecorationProvider } from './providers/fileDecorations';
import { NginTaskProvider } from './providers/tasks';
import { NginTestingProvider } from './providers/testing';
import { AnalyzerCodeActionProvider, ManifestCodeActionProvider } from './providers/codeActions';
import { registerManifestCompletion } from './manifestCompletion';
import { ProjectsTreeProvider } from './ui/tree';
import { StatusBarController } from './ui/statusBar';
import { openProjectActions } from './ui/projectActions';
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
  return vscode.window.showQuickPick(values.map(value => ({ label: value, description: value === active ? 'selected' : undefined })), {
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
  const projectsTree = new ProjectsTreeProvider(controller, sourceAnalysis);
  const decorations = new NginFileDecorationProvider(controller);
  const status = new StatusBarController(controller, sourceAnalysis);
  const tasks = new NginTaskProvider(controller, cli);
  const debug = new NginDebugProvider(controller, sourceAnalysis);
  const cppTools = new NginCppConfigurationProvider(controller);
  const testing = new NginTestingProvider(controller, sourceAnalysis);
  const projectsView = vscode.window.createTreeView('ngin.projects', { treeDataProvider: projectsTree, showCollapseAll: true });
  const canRun = (project: ProjectCandidate | undefined): boolean => {
    const snapshot = controller.snapshot;
    return projectCanRun(project, snapshot.graph, snapshot.context?.projectManifest);
  };
  const explainNotRunable = async (project: ProjectCandidate): Promise<void> => {
    const kind = project.libraryKind ?? project.artifactKind ?? 'Project';
    const message = kind === 'Library'
      ? `${project.name} is a Library and cannot be run or debugged. Build it or select an application with a Run configuration.`
      : `${project.name} has no Run configuration. Add Run intent or select a runable project.`;
    const action = await vscode.window.showInformationMessage(message, 'Build Project');
    if (action === 'Build Project') await vscode.commands.executeCommand('ngin.build', project);
  };
  const effectiveProject = async (argument?: unknown): Promise<ProjectCandidate | undefined> => {
    const requested = projectArgument(argument);
    if (requested) return requested;
    const activeFile = vscode.window.activeTextEditor?.document.uri.fsPath;
    return activeFile ? await sourceAnalysis.projectForFile(activeFile, false) ?? controller.activeProject : controller.activeProject;
  };
  let cliVerified = false;
  const updateContextKeys = async (): Promise<void> => {
    const snapshot = controller.snapshot;
    const effective = await effectiveProject();
    const effectiveGraph = effective?.manifest === snapshot.context?.projectManifest ? snapshot.graph : undefined;
    await Promise.all([
      vscode.commands.executeCommand('setContext', 'ngin.busy', Boolean(snapshot.busy)),
      vscode.commands.executeCommand('setContext', 'ngin.hasProject', Boolean(effective)),
      vscode.commands.executeCommand('setContext', 'ngin.hasGraph', Boolean(snapshot.graph)),
      vscode.commands.executeCommand('setContext', 'ngin.hasProjects', controller.projects.length > 0),
      vscode.commands.executeCommand('setContext', 'ngin.hasGraphError', Boolean(snapshot.graphError)),
      vscode.commands.executeCommand('setContext', 'ngin.canRun', projectCanRun(effective, effectiveGraph, effective?.manifest)),
      vscode.commands.executeCommand('setContext', 'ngin.hasTests', Boolean(effectiveGraph?.tests.length) || Boolean(effective?.hasTests)),
      vscode.commands.executeCommand('setContext', 'ngin.hasBenchmarks', Boolean(effectiveGraph?.benchmarks.length) || Boolean(effective?.hasBenchmarks)),
      vscode.commands.executeCommand('setContext', 'ngin.hasAnalyze', Boolean(effectiveGraph?.actions.some(action => action.kind === 'Analyze')) || Boolean(effective?.hasAnalyze)),
      vscode.commands.executeCommand('setContext', 'ngin.hasFormat', Boolean(effectiveGraph?.actions.some(action => action.kind === 'Format')) || Boolean(effective?.hasFormat)),
      vscode.commands.executeCommand('setContext', 'ngin.cliReady', cliVerified || Boolean(snapshot.graph)),
      vscode.commands.executeCommand('setContext', 'ngin.hasSuccessfulBuild', snapshot.lastOperation?.command === 'build' && snapshot.lastOperation.state === 'succeeded'),
      vscode.commands.executeCommand('setContext', 'ngin.hasSuccessfulRun', snapshot.lastOperation?.command === 'run' && snapshot.lastOperation.state === 'succeeded')
    ]);
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
    testing,
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
    controller.onDidChange(() => void updateContextKeys()),
    vscode.window.onDidChangeActiveTextEditor(() => void updateContextKeys()),
    sourceAnalysis.onDidChange(projectManifest => {
      const summary = sourceAnalysis.summary(projectManifest);
      void vscode.commands.executeCommand('setContext', 'ngin.toolingDecided', summary.state !== 'idle');
    }),
    vscode.debug.onDidStartDebugSession(session => {
      if (session.configuration.type === 'ngin' || session.configuration.name?.startsWith('NGIN:')) {
        void vscode.commands.executeCommand('setContext', 'ngin.hasSuccessfulRun', true);
      }
    })
  );

  register(extensionContext, 'ngin.refresh', () => controller.refreshDiscovery());
  register(extensionContext, 'ngin.showFilesView', () => vscode.commands.executeCommand('workbench.view.explorer'));
  register(extensionContext, 'ngin.showProjectView', () => vscode.commands.executeCommand('ngin.projects.focus'));
  register(extensionContext, 'ngin.refreshGraph', () => controller.refreshGraph(true));
  register(extensionContext, 'ngin.showOutput', () => cli.showOutput());
  register(extensionContext, 'ngin.openOutputDirectory', async (argument?: unknown) => {
    const project = await effectiveProject(argument);
    if (!project) throw new Error('No NGIN project is available.');
    const output = vscode.Uri.file(sourceAnalysis.contextForProject(project).outputDirectory);
    if (!await exists(output)) {
      const action = await vscode.window.showInformationMessage(`No output exists for ${project.name} yet.`, 'Build Project');
      if (action === 'Build Project') await vscode.commands.executeCommand('ngin.build', project);
      return;
    }
    await vscode.commands.executeCommand('revealFileInOS', output);
  });
  register(extensionContext, 'ngin.revealProjectFile', async (argument?: unknown) => {
    const uri = resourceArgument(argument);
    if (uri) await vscode.commands.executeCommand('revealInExplorer', uri);
  });
  register(extensionContext, 'ngin.cancel', () => cli.cancel());
  register(extensionContext, 'ngin.projectActions', (argument?: unknown) =>
    openProjectActions(controller, sourceAnalysis, projectArgument(argument)));
  register(extensionContext, 'ngin.openDashboard', (argument?: unknown) =>
    openProjectActions(controller, sourceAnalysis, projectArgument(argument)));
  register(extensionContext, 'ngin.openSettings', () =>
    vscode.commands.executeCommand('workbench.action.openSettings', '@ext:ngin.ngin-tools'));
  register(extensionContext, 'ngin.openDocumentation', () =>
    vscode.env.openExternal(vscode.Uri.parse('https://github.com/NGIN-ORG/NGIN/blob/main/Tools/NGIN.VSCode/README.md')));
  register(extensionContext, 'ngin.openWalkthrough', () =>
    vscode.commands.executeCommand('workbench.action.openWalkthrough', 'ngin.ngin-tools#ngin.gettingStarted'));
  register(extensionContext, 'ngin.checkSetup', async (argument?: unknown) => {
    const project = await effectiveProject(argument);
    const workspaceFolder = project
      ? vscode.workspace.getWorkspaceFolder(vscode.Uri.file(project.manifest))?.uri.fsPath ?? project.directory
      : vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
    if (!workspaceFolder) {
      const action = await vscode.window.showInformationMessage('Open a folder to check NGIN setup.', 'Open Folder');
      if (action === 'Open Folder') await vscode.commands.executeCommand('workbench.action.files.openFolder');
      return;
    }
    let executable = await cli.resolveExecutable(workspaceFolder);
    let version = 'Unavailable';
    let cliReady = false;
    try {
      const result = await cli.run(['--version'], workspaceFolder, { cwd: workspaceFolder });
      executable = result.command;
      version = result.stdout.trim() || 'Version not reported';
      cliReady = true;
    } catch {
      // The action list below exposes Settings and Output as remediation.
    }
    cliVerified = cliReady;
    await vscode.commands.executeCommand('setContext', 'ngin.cliReady', cliReady);
    const context = project ? sourceAnalysis.contextForProject(project) : undefined;
    const graph = context ? await controller.graphForContext(context, false) : undefined;
    const items: Array<vscode.QuickPickItem & { command?: string }> = [
      { label: 'Setup status', kind: vscode.QuickPickItemKind.Separator },
      { label: `${cliReady ? '$(pass)' : '$(error)'} NGIN CLI`, description: version, detail: executable },
      { label: `${vscode.workspace.isTrusted ? '$(pass)' : '$(warning)'} Workspace trust`, description: vscode.workspace.isTrusted ? 'Trusted' : 'Trust is required to run project tools' },
      { label: `${project ? '$(pass)' : '$(warning)'} Project discovery`, description: project ? project.name : 'No NGIN project found' },
      ...(project ? [{
        label: `${graph ? '$(pass)' : '$(error)'} Project model`,
        description: graph ? `${graph.product.artifactKind} · ${context?.configuration}` : 'Could not resolve the Composition Graph',
        detail: !graph && controller.snapshot.graphError ? controller.snapshot.graphError.split(/\r?\n/u)[0] : undefined
      }] : []),
      { label: 'Actions', kind: vscode.QuickPickItemKind.Separator },
      { label: '$(settings-gear) Open NGIN Settings', description: 'Configure the CLI path and extension behavior', command: 'ngin.openSettings' },
      { label: '$(refresh) Refresh Workspace', description: 'Discover projects and reload the model', command: 'ngin.refresh' },
      { label: '$(output) Show NGIN Output', description: 'Read detailed setup and command output', command: 'ngin.showOutput' }
    ];
    const selected = await vscode.window.showQuickPick(items, {
      title: 'NGIN: Check Setup',
      placeHolder: cliReady && project && graph ? 'Setup is ready' : 'Choose an action to resolve setup issues',
      matchOnDescription: true,
      matchOnDetail: true
    });
    if (selected?.command) await vscode.commands.executeCommand(selected.command);
  });

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
    const productTypes = documentRoots(metadata, 'Project').filter(
      (candidate): candidate is 'Executable' | 'Library' => candidate === 'Executable' || candidate === 'Library'
    );
    let name = '';
    let type: 'Executable' | 'Library' = 'Executable';
    let relativeDirectory = '';
    let step = 1;
    while (step <= 4) {
      if (step === 1) {
        const value = await inputStep({
          title: 'Create NGIN project', step, totalSteps: 4, value: name,
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
        const value = await pickStep({ title: 'Create NGIN project', step, totalSteps: 4 },
          productTypes.map(candidate => ({
            label: candidate,
            description: candidate === 'Executable' ? 'Executable source product'
              : 'Static library source product',
            value: candidate
          })));
        if (value === undefined) return;
        if (value === back) { step--; continue; }
        type = value;
        step++;
        continue;
      }
      if (step === 3) {
        const value = await inputStep({
          title: 'Create NGIN project', step, totalSteps: 4, value: relativeDirectory,
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
        continue;
      }
      const confirmation = await pickStep({ title: 'Create NGIN project', step, totalSteps: 4 }, [{
        label: 'Create project',
        description: `${type} · ${relativeDirectory}/${name}.nginproj`,
        value: true
      }]);
      if (confirmation === undefined) return;
      if (confirmation === back) { step--; continue; }
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
        description: `${value.libraryKind ?? value.artifactKind ?? 'Project'} · ${sourceAnalysis.contextForProject(value).configuration}`,
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

  register(extensionContext, 'ngin.selectConfiguration', async (argument?: unknown) => {
    const project = projectArgument(argument) ?? controller.activeProject;
    if (!project) throw new Error('No NGIN project is available.');
    const context = sourceAnalysis.contextForProject(project);
    const selected = typeof argument === 'string'
      ? argument
      : await choose('Configuration', project.workspaceChoices?.configurations ?? [], context.configuration);
    if (selected) await controller.updateProjectSelection(project, { configuration: selected, profile: undefined });
  });
  register(extensionContext, 'ngin.selectTarget', async (argument?: unknown) => {
    const project = projectArgument(argument) ?? controller.activeProject;
    if (!project) throw new Error('No NGIN project is available.');
    const context = sourceAnalysis.contextForProject(project);
    const selected = typeof argument === 'string'
      ? argument
      : await choose('Platform Target', project.workspaceChoices?.targets ?? [], context.target);
    if (selected) await controller.updateProjectSelection(project, { target: selected, profile: undefined });
  });
  register(extensionContext, 'ngin.selectToolchain', async (argument?: unknown) => {
    const project = projectArgument(argument) ?? controller.activeProject;
    if (!project) throw new Error('No NGIN project is available.');
    const context = sourceAnalysis.contextForProject(project);
    const selected = typeof argument === 'string'
      ? argument
      : await choose('Toolchain', project.workspaceChoices?.toolchains ?? [], context.toolchain);
    if (selected) await controller.updateProjectSelection(project, { toolchain: selected, profile: undefined });
  });
  register(extensionContext, 'ngin.selectRun', (argument?: unknown) => debug.selectRun(projectArgument(argument)));
  register(extensionContext, 'ngin.selectProfile', async (value?: string) => {
    const profiles = controller.activeProject?.workspaceChoices?.profiles ?? [];
    const selected = value
      ? profiles.find(profile => profile.name === value)
      : await vscode.window.showQuickPick(
        profiles.map(profile => ({ label: profile.name, value: profile })),
        { title: 'Select NGIN Profile' }
      ).then(item => item?.value);
    if (!selected) return;
    const current = controller.requireContext();
    await controller.updateSelection({
      profile: selected.name,
      configuration: selected.configuration ?? current.configuration,
      target: selected.target ?? current.target,
      toolchain: selected.toolchain ?? current.toolchain,
      run: selected.run ?? current.run
    });
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
    await controller.updateSelection({ options: { ...context.options, [selectedName]: value }, profile: undefined });
  });
  register(extensionContext, 'ngin.clearOptions', () => controller.updateSelection({ options: {}, profile: undefined }));

  register(extensionContext, 'ngin.validate', () => controller.validate(true));
  register(extensionContext, 'ngin.validateManifest', () => {
    const active = vscode.window.activeTextEditor?.document;
    return active?.languageId === 'ngin' ? controller.validateManifest(active.uri.fsPath, true) : controller.validate(true);
  });
  for (const command of ['restore', 'configure', 'build', 'stage', 'test', 'benchmark'] as const) {
    register(extensionContext, `ngin.${command}`, async (argument?: unknown) => {
      const project = await effectiveProject(argument);
      const context = project ? sourceAnalysis.contextForProject(project) : undefined;
      return controller.execute(command, [], false, context);
    });
  }
  register(extensionContext, 'ngin.lock', async (argument?: unknown) => {
    const project = await effectiveProject(argument);
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
  register(extensionContext, 'ngin.analyze', async (argument?: unknown) => sourceAnalysis.analyzeProject(await effectiveProject(argument)));
  register(extensionContext, 'ngin.enableProjectTooling', async (argument?: unknown) => sourceAnalysis.enableProjectTooling(await effectiveProject(argument)));
  register(extensionContext, 'ngin.analyzeFile', async (argument?: unknown) => {
    const uri = resourceArgument(argument);
    if (!uri) return;
    const document = await vscode.workspace.openTextDocument(uri);
    return sourceAnalysis.analyzeDocument(document);
  });
  register(extensionContext, 'ngin.formatSources', async (argument?: unknown) => {
    const project = await effectiveProject(argument);
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
    const selected = await effectiveProject(argument);
    if (!selected) throw new Error('No NGIN project is selected.');
    if (!canRun(selected)) return explainNotRunable(selected);
    let context = sourceAnalysis.contextForProject(selected);
    const graph = await controller.graphForContext(context, true);
    if (!graph) return;
    if (graph.runs.length > 1 && !context.run) {
      if (!await debug.selectRun(selected)) return;
      context = sourceAnalysis.contextForProject(selected);
    }
    await controller.execute('run', [], false, context);
  });
  register(extensionContext, 'ngin.runWithArguments', async (argument?: unknown) => {
    const selected = await effectiveProject(argument);
    if (!selected) throw new Error('No NGIN project is selected.');
    if (!canRun(selected)) return explainNotRunable(selected);
    let context = sourceAnalysis.contextForProject(selected);
    const graph = await controller.graphForContext(context, true);
    if (!graph) return;
    if (graph.runs.length > 1 && !context.run) {
      if (!await debug.selectRun(selected)) return;
      context = sourceAnalysis.contextForProject(selected);
    }
    const args = await vscode.window.showInputBox({
      title: `Run ${context.projectName} with arguments`,
      prompt: 'Arguments appended to the selected run configuration',
      placeHolder: '--example value'
    });
    if (args === undefined) return;
    const trailing = args.trim() ? ['--', ...args.match(/(?:[^\s"]+|"[^"]*")+/g)?.map(value => value.replace(/^"|"$/g, '')) ?? []] : [];
    await controller.execute('run', trailing, false, context);
  });
  register(extensionContext, 'ngin.publish', async (argument?: unknown) => {
    const project = await effectiveProject(argument);
    if (!project) throw new Error('No NGIN project is available.');
    const context = sourceAnalysis.contextForProject(project);
    const graph = await controller.graphForContext(context, true);
    const publishes = graph?.publishes ?? [];
    const selected = publishes.length <= 1 ? publishes[0]?.name : await choose('Publish', publishes.map(item => item.name ?? item.identity), undefined);
    await controller.execute('publish', selected ? [selected] : [], false, context);
  });
  register(extensionContext, 'ngin.debug', async (argument?: unknown) => {
    const project = projectArgument(argument);
    const owner = project ?? await sourceAnalysis.projectForFile(vscode.window.activeTextEditor?.document.uri.fsPath ?? '', false);
    const selected = owner ?? controller.activeProject;
    if (!selected) throw new Error('No NGIN project is available to debug.');
    if (!canRun(selected)) return explainNotRunable(selected);
    return vscode.debug.startDebugging(undefined, {
      type: 'ngin', request: 'run', name: `NGIN: Debug ${selected.name}`, project: selected.manifest, preStage: true
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
  register(extensionContext, 'ngin.clean', async (argument?: unknown) => {
    const project = await effectiveProject(argument);
    return clean(true, project ? sourceAnalysis.contextForProject(project) : controller.requireContext());
  });
  register(extensionContext, 'ngin.rebuild', async (argument?: unknown) => {
    const project = await effectiveProject(argument);
    const current = project ? sourceAnalysis.contextForProject(project) : controller.requireContext();
    if (await clean(true, current)) await controller.execute('build', [], false, current);
  });

  register(extensionContext, 'ngin.showGraph', async (argument?: unknown) => {
    const project = await effectiveProject(argument);
    const context = project ? sourceAnalysis.contextForProject(project) : controller.requireContext();
    const graph = await controller.graphForContext(context, true);
    if (!graph) throw new Error(`${context.projectName} has no resolved Composition Graph.`);
    return openJson('Composition Graph', graph);
  });
  register(extensionContext, 'ngin.inspect', async (argument?: unknown) => {
    const project = await effectiveProject(argument);
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
  register(extensionContext, 'ngin.diff', async (argument?: unknown) => {
    const project = await effectiveProject(argument);
    if (!project) throw new Error('No NGIN project is available.');
    const current = sourceAnalysis.contextForProject(project);
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
  register(extensionContext, 'ngin.explain', async (argument?: unknown) => {
    const project = await effectiveProject(argument);
    if (!project) throw new Error('No NGIN project is available.');
    const current = sourceAnalysis.contextForProject(project);
    const selected = typeof argument === 'string'
      ? argument
      : await vscode.window.showInputBox({ title: 'Explain Composition Identity', prompt: 'Graph identity' });
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
    const project = await effectiveProject(argument);
    const manifest = project?.manifest ?? controller.requireContext().projectManifest;
    const document = await vscode.workspace.openTextDocument(vscode.Uri.file(manifest));
    await vscode.window.showTextDocument(document);
  });
  register(extensionContext, 'ngin.openGraphSource', async (item?: GraphBuildItem | GraphNamedNode, argument?: unknown) => {
    const project = await effectiveProject(argument);
    if (!project) throw new Error('No NGIN project is available.');
    const current = sourceAnalysis.contextForProject(project);
    const projectDirectory = path.dirname(current.projectManifest);
    const provenance = item?.provenance;
    const itemPath = item && 'path' in item && typeof item.path === 'string' ? item.path : undefined;
    const source = provenance?.document
      ? path.resolve(current.workspaceFolder, provenance.document)
      : itemPath ? path.resolve(projectDirectory, itemPath) : current.projectManifest;
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
      await cli.run(['format', '--project', manifest.uri.fsPath], current.workspaceFolder, {
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

  const author = async (args: string[], project?: ProjectCandidate): Promise<void> => {
    const current = project ? sourceAnalysis.contextForProject(project) : controller.requireContext();
    try {
      const openManifest = vscode.workspace.textDocuments.find(document => document.uri.fsPath === current.projectManifest);
      if (openManifest?.isDirty && !await openManifest.save()) throw new Error('Save the active project manifest before running an authoring command.');
      await cli.run([...args, ...selectionArguments(current)], current.workspaceFolder, {
        cwd: path.dirname(current.projectManifest), requireTrust: true, revealOutput: true, exclusive: true
      });
      await controller.refreshDiscovery();
      if (controller.activeProject?.manifest === current.projectManifest) await controller.refreshGraph(false);
    } catch (error) {
      cli.showOutput();
      void vscode.window.showErrorMessage(error instanceof Error ? error.message : String(error));
    }
  };
  register(extensionContext, 'ngin.addPackage', async (argument?: unknown) => {
    const project = await effectiveProject(argument);
    if (!project) throw new Error('No NGIN project is available.');
    let name = '';
    let constraint: 'default' | 'version' | 'exact' = 'default';
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
          { label: 'Compatible version', description: 'Allow compatible package versions', value: 'version' as const },
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
    if (constraint !== 'default') args.push(constraint === 'exact' ? '--exact' : '--version', version);
    await author(args, project);
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
    const fields = ['Name', 'Version', ...(graph?.product.artifactKind === 'Library' ? ['Kind'] : [])];
    let field = '';
    let value: string | undefined;
    let step = 1;
    while (step <= 2) {
      if (step === 1) {
        const selected = await pickStep({ title: 'Edit product', step, totalSteps: 2 }, fields.map(candidate => ({
          label: candidate, value: candidate,
          description: candidate === 'Kind' ? 'Library artifact kind' : `Project ${candidate.toLowerCase()}`
        })));
        if (selected === undefined) return;
        field = selected as string;
        step++;
        continue;
      }
      if (field === 'Kind') {
        const choices = attributeChoices(metadata, 'project.library-root', 'Kind');
        const selected = await pickStep({ title: 'Edit product', step, totalSteps: 2 }, choices.map(candidate => ({
          label: candidate, value: candidate,
          description: 'Library output and consumption behavior'
        })));
        if (selected === undefined) return;
        if (selected === back) { step--; continue; }
        value = selected;
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
  register(extensionContext, 'ngin.changeMembership', async (argument?: unknown) => {
    const uri = resourceArgument(argument);
    if (!uri) return;
    const project = await sourceAnalysis.projectForFile(uri.fsPath);
    if (!project) return void vscode.window.showWarningMessage(`${path.basename(uri.fsPath)} is not owned by an NGIN project.`);
    const context = sourceAnalysis.contextForProject(project);
    const graph = await controller.graphForContext(context, true);
    if (!graph) return;
    await changeMembership(uri, !graphOwnsFile(graph, context, uri.fsPath));
  });

  const createProjectFile = async (
    argument?: unknown,
    profile?: { kind: 'Source' | 'Header'; path: string; title: string; extension: string; contents?: string }
  ): Promise<void> => {
    const project = projectArgument(argument);
    const current = project ? sourceAnalysis.contextForProject(project) : controller.requireContext();
    const entry = projectFileArgument(argument);
    const projectDirectory = path.dirname(current.projectManifest);
    const base = profile ? projectDirectory : entry?.directory ? entry.path : entry ? path.dirname(entry.path) : projectDirectory;
    const value = profile?.path ?? '';
    const extension = profile?.extension ?? '';
    const stemEnd = value ? value.length - path.extname(value).length : 0;
    const name = await vscode.window.showInputBox({
      title: profile?.title ?? 'New Project File',
      prompt: profile ? 'Path relative to the project; it will be added to the build automatically.' : 'File name or relative path',
      value,
      valueSelection: value ? [Math.max(0, value.lastIndexOf('/') + 1), stemEnd] : undefined,
      validateInput: candidate => candidate.trim() ? undefined : 'Enter a file name.'
    });
    if (!name?.trim()) return;
    const requested = profile && !path.extname(name.trim()) ? `${name.trim()}${extension}` : name.trim();
    const target = path.resolve(base, requested);
    if (!isWithin(projectDirectory, target)) throw new Error('The new file must remain inside its project.');
    const uri = vscode.Uri.file(target);
    if (await exists(uri)) throw new Error(`A file already exists at ${target}.`);
    const confirmation = await vscode.window.showQuickPick([{
      label: `Create ${path.basename(target)}`,
      description: relativeManifestPath(projectDirectory, target),
      detail: `Create the file and add it to ${path.basename(current.projectManifest)}`
    }], {
      title: profile?.title ?? 'New Project File',
      placeHolder: 'Confirm the authored file and manifest change'
    });
    if (!confirmation) return;
    await vscode.workspace.fs.createDirectory(vscode.Uri.file(path.dirname(target)));
    const workspaceEdit = new vscode.WorkspaceEdit();
    workspaceEdit.createFile(uri, {
      ignoreIfExists: false,
      contents: profile?.contents ? new TextEncoder().encode(profile.contents) : undefined
    });
    const manifest = await vscode.workspace.openTextDocument(vscode.Uri.file(current.projectManifest));
    const relative = relativeManifestPath(projectDirectory, target);
    const membership = insertBuildItem(manifest.getText(), profile?.kind ?? kindForPath(relative), 'Include', relative);
    if (membership) addOffsetEdits(workspaceEdit, manifest, [membership]);
    if (!await vscode.workspace.applyEdit(workspaceEdit)) throw new Error('VS Code could not create the project file.');
    controller.refreshPresentation();
    await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(uri));
  };
  register(extensionContext, 'ngin.newSourceFile', (argument?: unknown) => createProjectFile(argument, {
    kind: 'Source', path: 'src/NewSource.cpp', title: 'New C++ Source File', extension: '.cpp'
  }));
  register(extensionContext, 'ngin.newHeaderFile', (argument?: unknown) => createProjectFile(argument, {
    kind: 'Header', path: 'include/NewHeader.hpp', title: 'New C++ Header File', extension: '.hpp', contents: '#pragma once\n'
  }));
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
