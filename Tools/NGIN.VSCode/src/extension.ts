import * as path from 'node:path';
import * as vscode from 'vscode';
import { CliFailure, NginCli } from './core/cli';
import { dependencyLockPath, selectionArguments } from './core/commandArguments';
import { NginController } from './core/controller';
import { displayOptionValue } from './core/graph';
import {
  kindForPath,
  updateProjectAttributes,
  type OffsetEdit
} from './core/manifestEdits';
import { relativeManifestPath } from './core/manifestText';
import { isWithin } from './core/paths';
import { attributeChoices, childElementNames, documentRoots, loadManifestMetadata } from './core/manifestMetadata';
import { createProjectTemplate, type CustomProjectLayout, type ProjectLayout, type ProjectProductKind } from './core/projectTemplates';
import { projectCanRun } from './core/projectCapabilities';
import { graphOwnsFile } from './core/projectOwnership';
import type { ProjectFileEntry } from './core/projectFiles';
import type { GraphBuildItem, GraphNamedNode, GraphPackage, ProjectCandidate } from './model';
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
import { OperationCoordinator } from './core/operationCoordinator';
import { AuthoringService } from './features/authoring';

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

function packageArgument(value: unknown): GraphPackage | undefined {
  return (value as { package?: GraphPackage } | undefined)?.package;
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
  const operations = new OperationCoordinator();
  const authoring = new AuthoringService(cli, operations);
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
    return activeFile ? await sourceAnalysis.projectForFile(activeFile, false) ?? controller.launchProduct : controller.launchProduct;
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
  register(extensionContext, 'ngin.openFile', async (argument?: unknown) => {
    const uri = resourceArgument(argument);
    if (uri) await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(uri));
  });
  register(extensionContext, 'ngin.copyFilePath', async (argument?: unknown) => {
    const uri = resourceArgument(argument);
    if (!uri) return;
    await vscode.env.clipboard.writeText(uri.fsPath);
    void vscode.window.setStatusBarMessage(`Copied ${uri.fsPath}`, 2500);
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
    const productTypes: ProjectProductKind[] = documentRoots(metadata, 'Project').includes('Executable')
      ? ['Executable', ...attributeChoices(metadata, 'project.library-root', 'Kind')
        .filter((value): value is Exclude<ProjectProductKind, 'Executable'> =>
          value === 'Static' || value === 'Shared' || value === 'Interface' || value === 'Plugin')]
      : [];
    let name = '';
    let type: ProjectProductKind = 'Executable';
    let layout: ProjectLayout = 'split';
    let customLayout: CustomProjectLayout | undefined;
    let relativeDirectory = '';
    let step = 1;
    while (step <= 5) {
      if (step === 1) {
        const value = await inputStep({
          title: 'Create NGIN project', step, totalSteps: 5, value: name,
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
        const value = await pickStep({ title: 'Create NGIN project', step, totalSteps: 5 },
          productTypes.map(candidate => ({
            label: candidate,
            description: candidate === 'Executable' ? 'Executable source product' : `${candidate} Library product`,
            value: candidate
          })));
        if (value === undefined) return;
        if (value === back) { step--; continue; }
        type = value;
        step++;
        continue;
      }
      if (step === 3) {
        const value = await pickStep({ title: 'Create NGIN project', step, totalSteps: 5 }, [
          { label: 'Include/source split', description: 'Separate public headers and compiled sources', value: 'split' as const },
          { label: 'Co-located', description: 'Keep headers and sources together', value: 'colocated' as const },
          { label: 'Public/private split', description: 'Use explicit Public and Private trees', value: 'public-private' as const },
          { label: 'Custom', description: 'Choose header and source roots', value: 'custom' as const }
        ]);
        if (value === undefined) return;
        if (value === back) { step--; continue; }
        layout = value;
        if (layout === 'custom') {
          const headerRoot = await vscode.window.showInputBox({
            title: 'Custom project layout', prompt: 'Header root relative to the product', value: 'Include'
          });
          if (!headerRoot?.trim()) continue;
          const sourceRoot = type === 'Interface' ? 'Source' : await vscode.window.showInputBox({
            title: 'Custom project layout', prompt: 'Source root relative to the product', value: 'Source'
          });
          if (!sourceRoot?.trim()) continue;
          customLayout = { headerRoot: headerRoot.trim(), sourceRoot: sourceRoot.trim() };
        }
        step++;
        continue;
      }
      if (step === 4) {
        const value = await inputStep({
          title: 'Create NGIN project', step, totalSteps: 5, value: relativeDirectory,
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
      const confirmation = await pickStep({ title: 'Create NGIN project', step, totalSteps: 5 }, [{
        label: 'Create project',
        description: `${type} · ${layout} · ${relativeDirectory}/${name}.nginproj`,
        value: true
      }]);
      if (confirmation === undefined) return;
      if (confirmation === back) { step--; continue; }
      step++;
    }

    const directory = path.resolve(folder.uri.fsPath, relativeDirectory);
    const manifest = vscode.Uri.file(path.join(directory, `${name}.nginproj`));
    if (await exists(manifest)) throw new Error(`A project manifest already exists at ${manifest.fsPath}.`);
    const template = createProjectTemplate(name, type, layout, customLayout);
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

  const setLaunchProduct = async (argument?: unknown): Promise<void> => {
    const candidate = projectArgument(argument);
    const current = controller.launchProduct;
    const recent = extensionContext.workspaceState.get<string[]>('ngin.recentProjects', []);
    const projects = controller.projects.sort((left, right) =>
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
      { title: 'Select Active Project', placeHolder: current?.name, matchOnDescription: true, matchOnDetail: true }
    ).then(item => item?.value);
    if (project) {
      await controller.setLaunchProduct(project);
      await extensionContext.workspaceState.update('ngin.recentProjects', [project.manifest, ...recent.filter(value => value !== project.manifest)].slice(0, 20));
    }
  };
  register(extensionContext, 'ngin.setLaunchProduct', setLaunchProduct);
  // Compatibility aliases retained for one deprecation cycle.
  register(extensionContext, 'ngin.selectProject', setLaunchProduct);
  register(extensionContext, 'ngin.setDefaultProject', setLaunchProduct);

  register(extensionContext, 'ngin.selectConfiguration', async (argument?: unknown) => {
    const project = projectArgument(argument) ?? await effectiveProject();
    if (!project) throw new Error('No NGIN project is available.');
    const context = sourceAnalysis.contextForProject(project);
    const selected = typeof argument === 'string'
      ? argument
      : await choose('Configuration', project.workspaceChoices?.configurations ?? [], context.configuration);
    if (selected) await controller.updateProjectSelection(project, { configuration: selected, profile: undefined });
  });
  register(extensionContext, 'ngin.selectTarget', async (argument?: unknown) => {
    const project = projectArgument(argument) ?? await effectiveProject();
    if (!project) throw new Error('No NGIN project is available.');
    const context = sourceAnalysis.contextForProject(project);
    const selected = typeof argument === 'string'
      ? argument
      : await choose('Platform Target', project.workspaceChoices?.targets ?? [], context.target);
    if (selected) await controller.updateProjectSelection(project, { target: selected, profile: undefined });
  });
  register(extensionContext, 'ngin.selectToolchain', async (argument?: unknown) => {
    const project = projectArgument(argument) ?? await effectiveProject();
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
    const selected = owner ?? controller.launchProduct;
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
    const wrapped = item as (GraphBuildItem | GraphNamedNode) & { package?: GraphPackage; project?: ProjectCandidate };
    const graphItem = wrapped?.package ?? item;
    const project = await effectiveProject(argument ?? wrapped);
    if (!project) throw new Error('No NGIN project is available.');
    const current = sourceAnalysis.contextForProject(project);
    const projectDirectory = path.dirname(current.projectManifest);
    const provenance = graphItem?.provenance;
    const itemPath = graphItem && 'path' in graphItem && typeof graphItem.path === 'string' ? graphItem.path : undefined;
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
      if (openManifest?.isDirty) throw new Error('Save or revert the project manifest before running this authoring command.');
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
  const applyPackagePlan = async (
    project: ProjectCandidate,
    request: { intent: 'AddPackage' | 'ChangePackageRequirement' | 'RemovePackage'; package: string; version?: string; exact?: boolean }
  ): Promise<void> => {
    const current = sourceAnalysis.contextForProject(project);
    const plan = await authoring.plan(current, request);
    if (plan.state !== 'ready') {
      throw new Error(plan.diagnostics.map(value => value.message).join('\n') || 'The package authoring plan was rejected.');
    }
    await authoring.apply(current, plan);
    controller.invalidateConfiguration();
    await controller.refreshDiscovery();
    projectsTree.refresh();
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
    await applyPackagePlan(project, {
      intent: 'AddPackage', package: name,
      version: constraint === 'default' ? undefined : version,
      exact: constraint === 'exact'
    });
  });
  register(extensionContext, 'ngin.changePackageRequirement', async (argument?: unknown) => {
    const project = projectArgument(argument) ?? await effectiveProject(argument);
    const dependency = packageArgument(argument);
    if (!project || !dependency?.name) return;
    const value = await vscode.window.showInputBox({
      title: `Change ${dependency.name} Requirement`, prompt: 'Compatible semantic version',
      value: dependency.version ?? '', validateInput: candidate => candidate.trim() ? undefined : 'Enter a version.'
    });
    if (!value?.trim()) return;
    await applyPackagePlan(project, {
      intent: 'ChangePackageRequirement', package: dependency.name, version: value.trim()
    });
  });
  register(extensionContext, 'ngin.removePackage', async (argument?: unknown) => {
    const project = projectArgument(argument) ?? await effectiveProject(argument);
    const dependency = packageArgument(argument);
    if (!project || !dependency?.name) return;
    const confirmation = await vscode.window.showWarningMessage(
      `Remove direct package ${dependency.name} from ${project.name}?`, { modal: true }, 'Remove'
    );
    if (confirmation === 'Remove') {
      await applyPackagePlan(project, { intent: 'RemovePackage', package: dependency.name });
    }
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
    const relative = relativeManifestPath(path.dirname(current.projectManifest), target);
    const kind = (entry?.kind && ['Source', 'Header', 'CxxModule', 'Resource'].includes(entry.kind)
      ? entry.kind : kindForPath(relative)) as 'Source' | 'Header' | 'CxxModule' | 'Resource';
    const plan = await authoring.plan(current, {
      intent: include ? 'IncludeItems' : 'ExcludeItems',
      items: [{ path: relative, kind }]
    });
    if (plan.state !== 'ready') {
      throw new Error(plan.diagnostics.map(value => value.message).join('\n') || 'The membership change is not safe in this Build Context.');
    }
    if (!plan.textEdits.length) {
      void vscode.window.showInformationMessage(`${relative} is already ${include ? 'included in' : 'excluded from'} this Build Context.`);
      return;
    }
    await authoring.apply(current, plan);
    controller.invalidateConfiguration();
    projectsTree.refresh();
    const document = await vscode.workspace.openTextDocument(vscode.Uri.file(current.projectManifest));
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
    const project = projectArgument(argument) ?? await effectiveProject();
    const current = project ? sourceAnalysis.contextForProject(project) : controller.requireContext();
    const entry = projectFileArgument(argument);
    const projectDirectory = path.dirname(current.projectManifest);
    if (profile?.kind === 'Source' && project?.libraryKind === 'Interface') {
      throw new Error(`${project.name} is an Interface Library and cannot contain compiled Source items.`);
    }
    const selectedBase = entry?.directory ? entry.path : entry ? path.dirname(entry.path) : undefined;
    const base = selectedBase ?? projectDirectory;
    const value = selectedBase && profile ? path.basename(profile.path) : profile?.path ?? '';
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
    const relative = relativeManifestPath(projectDirectory, target);
    const kind = profile?.kind ?? kindForPath(relative);
    const plan = await authoring.plan(current, { intent: 'CreateItems', items: [{ path: relative, kind }] });
    if (plan.state !== 'ready') {
      throw new Error(plan.diagnostics.map(value => value.message).join('\n') || 'The item creation plan was rejected.');
    }
    const confirmation = await vscode.window.showQuickPick([{
      label: `Create ${path.basename(target)}`,
      description: relative,
      detail: plan.textEdits.length ? `Create the file and update ${path.basename(current.projectManifest)}`
        : 'Create the file; an existing Build rule already covers it.'
    }], {
      title: profile?.title ?? 'New Project File',
      placeHolder: 'Confirm the authored file and manifest change'
    });
    if (!confirmation) return;
    await vscode.workspace.fs.createDirectory(vscode.Uri.file(path.dirname(target)));
    await authoring.apply(current, plan, new Map([[relative,
      profile?.contents ? new TextEncoder().encode(profile.contents) : new Uint8Array()]]));
    controller.refreshPresentation();
    await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(uri));
  };
  register(extensionContext, 'ngin.newSourceFile', (argument?: unknown) => createProjectFile(argument, {
    kind: 'Source', path: 'src/NewSource.cpp', title: 'New C++ Source File', extension: '.cpp'
  }));
  register(extensionContext, 'ngin.newHeaderFile', (argument?: unknown) => createProjectFile(argument, {
    kind: 'Header', path: 'include/NewHeader.hpp', title: 'New C++ Header File', extension: '.hpp', contents: '#pragma once\n'
  }));
  const physicalDestination = (argument: unknown, project: ProjectCandidate): string => {
    const entry = projectFileArgument(argument);
    return entry?.directory ? entry.path : entry ? path.dirname(entry.path) : project.directory;
  };
  register(extensionContext, 'ngin.newFile', async (argument?: unknown) => {
    const project = projectArgument(argument) ?? await effectiveProject(argument);
    if (!project) throw new Error('No NGIN product is available.');
    const base = physicalDestination(argument, project);
    const value = await vscode.window.showInputBox({
      title: 'New File', prompt: 'File name or path relative to the selected folder',
      validateInput: candidate => candidate.trim() ? undefined : 'Enter a file name.'
    });
    if (!value?.trim()) return;
    const target = path.resolve(base, value.trim());
    if (!isWithin(project.directory, target)) throw new Error('The new file must remain inside its product.');
    const uri = vscode.Uri.file(target);
    if (await exists(uri)) throw new Error(`A file already exists at ${target}.`);
    await vscode.workspace.fs.createDirectory(vscode.Uri.file(path.dirname(target)));
    const edit = new vscode.WorkspaceEdit();
    edit.createFile(uri, { ignoreIfExists: false });
    if (!await vscode.workspace.applyEdit(edit)) throw new Error('VS Code could not create the file.');
    projectsTree.refresh();
    await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(uri));
  });
  register(extensionContext, 'ngin.newFolder', async (argument?: unknown) => {
    const project = projectArgument(argument) ?? await effectiveProject(argument);
    if (!project) throw new Error('No NGIN product is available.');
    const base = physicalDestination(argument, project);
    const value = await vscode.window.showInputBox({
      title: 'New Folder', prompt: 'Folder name or path relative to the selected folder',
      validateInput: candidate => candidate.trim() ? undefined : 'Enter a folder name.'
    });
    if (!value?.trim()) return;
    const target = path.resolve(base, value.trim());
    if (!isWithin(project.directory, target)) throw new Error('The new folder must remain inside its product.');
    if (await exists(vscode.Uri.file(target))) throw new Error(`A file or folder already exists at ${target}.`);
    await vscode.workspace.fs.createDirectory(vscode.Uri.file(target));
    projectsTree.refresh();
  });

  const createSemanticItems = async (
    project: ProjectCandidate,
    items: Array<{ path: string; kind: 'Source' | 'Header' | 'CxxModule'; contents: string }>,
    title: string
  ): Promise<void> => {
    if (project.libraryKind === 'Interface' && items.some(item => item.kind === 'Source')) {
      throw new Error(`${project.name} is an Interface Library and cannot contain compiled Source items.`);
    }
    const context = sourceAnalysis.contextForProject(project);
    for (const item of items) {
      const target = path.resolve(project.directory, ...item.path.split('/'));
      if (!isWithin(project.directory, target)) throw new Error('New items must remain inside their product.');
      if (await exists(vscode.Uri.file(target))) throw new Error(`A file already exists at ${target}.`);
    }
    const plan = await authoring.plan(context, {
      intent: 'CreateItems',
      items: items.map(item => ({ path: item.path, kind: item.kind }))
    });
    if (plan.state !== 'ready') {
      throw new Error(plan.diagnostics.map(value => value.message).join('\n') || 'The item creation plan was rejected.');
    }
    const preview = await vscode.window.showQuickPick([{
      label: `Create ${items.length} ${items.length === 1 ? 'item' : 'items'}`,
      description: items.map(item => item.path).join(', '),
      detail: plan.textEdits.length === 0 ? 'Existing Build rules already cover the requested items.'
        : `Update ${path.basename(project.manifest)} with ${plan.textEdits.length} minimal text edit${plan.textEdits.length === 1 ? '' : 's'}.`
    }], { title, placeHolder: 'Review and apply the authoring plan' });
    if (!preview) return;
    for (const item of items) {
      await vscode.workspace.fs.createDirectory(vscode.Uri.file(path.dirname(path.resolve(project.directory, item.path))));
    }
    await authoring.apply(context, plan,
      new Map(items.map(item => [item.path, new TextEncoder().encode(item.contents)])));
    controller.invalidateConfiguration();
    projectsTree.refresh();
    const first = vscode.Uri.file(path.resolve(project.directory, items[0].path));
    await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(first));
  };
  register(extensionContext, 'ngin.newClass', async (argument?: unknown) => {
    const project = projectArgument(argument) ?? await effectiveProject(argument);
    if (!project) throw new Error('No NGIN product is available.');
    if (project.libraryKind === 'Interface') {
      throw new Error(`${project.name} is an Interface Library; create a Header or C++ Module instead.`);
    }
    const name = await vscode.window.showInputBox({
      title: 'New C++ Class', prompt: 'Class name', value: 'NewClass',
      validateInput: value => /^[A-Za-z_]\w*$/u.test(value.trim()) ? undefined : 'Enter a valid C++ identifier.'
    });
    if (!name?.trim()) return;
    const selected = projectFileArgument(argument);
    const selectedRelative = selected?.directory ? selected.relativePath : selected ? path.posix.dirname(selected.relativePath) : undefined;
    const header = selectedRelative && selectedRelative !== '.' ? `${selectedRelative}/${name}.hpp` : `include/${name}.hpp`;
    const source = selectedRelative && selectedRelative !== '.' ? `${selectedRelative}/${name}.cpp` : `src/${name}.cpp`;
    await createSemanticItems(project, [
      { path: header, kind: 'Header', contents: `#pragma once\n\nclass ${name}\n{\npublic:\n    ${name}() = default;\n};\n` },
      { path: source, kind: 'Source', contents: `#include "${path.posix.basename(header)}"\n` }
    ], 'New C++ Class');
  });
  register(extensionContext, 'ngin.newModule', async (argument?: unknown) => {
    const project = projectArgument(argument) ?? await effectiveProject(argument);
    if (!project) throw new Error('No NGIN product is available.');
    const name = await vscode.window.showInputBox({
      title: 'New C++ Module', prompt: 'Module name', value: project.name,
      validateInput: value => /^[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*$/u.test(value.trim()) ? undefined : 'Enter a valid C++ module name.'
    });
    if (!name?.trim()) return;
    const selected = projectFileArgument(argument);
    const directory = selected?.directory ? selected.relativePath : selected ? path.posix.dirname(selected.relativePath) : 'modules';
    const modulePath = `${directory && directory !== '.' ? `${directory}/` : ''}${name.replaceAll('.', '-')}.cppm`;
    await createSemanticItems(project, [{
      path: modulePath, kind: 'CxxModule', contents: `export module ${name};\n`
    }], 'New C++ Module');
  });
  register(extensionContext, 'ngin.newCppItem', async (argument?: unknown) => {
    const item = await vscode.window.showQuickPick([
      { label: 'C++ Source File', command: 'ngin.newSourceFile' },
      { label: 'C++ Header File', command: 'ngin.newHeaderFile' },
      { label: 'C++ Class', command: 'ngin.newClass' },
      { label: 'C++ Module', command: 'ngin.newModule' }
    ], { title: 'New C++ Item' });
    if (item) await vscode.commands.executeCommand(item.command, argument);
  });
  const managedFile = async (argument: unknown): Promise<{
    project: ProjectCandidate;
    context: ReturnType<SourceAnalysisProvider['contextForProject']>;
    entry: ProjectFileEntry;
    kind: 'Source' | 'Header' | 'CxxModule' | 'Resource';
  } | undefined> => {
    const entry = projectFileArgument(argument);
    if (!entry || entry.directory) return undefined;
    const project = projectArgument(argument) ?? await sourceAnalysis.projectForFile(entry.path);
    if (!project) return undefined;
    const kind = (entry.kind && ['Source', 'Header', 'CxxModule', 'Resource'].includes(entry.kind)
      ? entry.kind : kindForPath(entry.relativePath)) as 'Source' | 'Header' | 'CxxModule' | 'Resource';
    return { project, context: sourceAnalysis.contextForProject(project), entry, kind };
  };
  const relocateItem = async (argument: unknown, move: boolean): Promise<void> => {
    const target = await managedFile(argument);
    if (!target) return;
    let destination: string | undefined;
    if (move) {
      const selected = await vscode.window.showOpenDialog({
        title: `Move ${target.entry.name}`, defaultUri: vscode.Uri.file(target.project.directory),
        canSelectFiles: false, canSelectFolders: true, canSelectMany: false
      });
      if (!selected?.[0]) return;
      if (!isWithin(target.project.directory, selected[0].fsPath)) {
        throw new Error('The destination must remain inside the current product boundary.');
      }
      destination = relativeManifestPath(target.project.directory, path.join(selected[0].fsPath, target.entry.name));
    } else {
      const name = await vscode.window.showInputBox({
        title: `Rename ${target.entry.name}`, value: target.entry.name,
        valueSelection: [0, target.entry.name.length - path.extname(target.entry.name).length],
        validateInput: value => value.trim() && !value.includes('/') && !value.includes('\\')
          ? undefined : 'Enter a file name without path separators.'
      });
      if (!name?.trim() || name.trim() === target.entry.name) return;
      destination = path.posix.join(path.posix.dirname(target.entry.relativePath), name.trim());
    }
    const intent = move ? 'MoveItems' : 'RenameItems';
    const plan = await authoring.plan(target.context, {
      intent, from: target.entry.relativePath, to: destination,
      items: [{ path: target.entry.relativePath, kind: target.kind }]
    });
    if (plan.state !== 'ready') {
      throw new Error(plan.diagnostics.map(value => value.message).join('\n') || `The ${move ? 'move' : 'rename'} is not safe.`);
    }
    const confirmation = await vscode.window.showQuickPick([{
      label: `${move ? 'Move' : 'Rename'} ${target.entry.name}`,
      description: `${target.entry.relativePath} → ${destination}`,
      detail: `${plan.textEdits.length} manifest reference edit${plan.textEdits.length === 1 ? '' : 's'}`
    }], { title: `${move ? 'Move' : 'Rename'} Product File`, placeHolder: 'Review and apply the authoring plan' });
    if (!confirmation) return;
    await vscode.workspace.fs.createDirectory(vscode.Uri.file(path.dirname(path.resolve(target.project.directory, destination))));
    await authoring.apply(target.context, plan);
    controller.invalidateConfiguration();
    projectsTree.refresh();
  };
  register(extensionContext, 'ngin.renameItem', (argument: unknown) => relocateItem(argument, false));
  register(extensionContext, 'ngin.moveItem', (argument: unknown) => relocateItem(argument, true));
  register(extensionContext, 'ngin.deleteItem', async (argument: unknown) => {
    const target = await managedFile(argument);
    if (!target) return;
    const plan = await authoring.plan(target.context, {
      intent: 'DeleteItems', items: [{ path: target.entry.relativePath, kind: target.kind }]
    });
    if (plan.state !== 'ready') {
      throw new Error(plan.diagnostics.map(value => value.message).join('\n') || 'The delete is not safe.');
    }
    const confirmation = await vscode.window.showWarningMessage(
      `Delete ${target.entry.relativePath}? The file and ${plan.textEdits.length} manifest edit${plan.textEdits.length === 1 ? '' : 's'} can be restored with Undo.`,
      { modal: true }, 'Delete'
    );
    if (confirmation !== 'Delete') return;
    await authoring.apply(target.context, plan);
    controller.invalidateConfiguration();
    projectsTree.refresh();
  });
  register(extensionContext, 'ngin.duplicateItem', async (argument: unknown) => {
    const target = await managedFile(argument);
    if (!target) return;
    const extension = path.extname(target.entry.name);
    const stem = path.basename(target.entry.name, extension);
    const name = await vscode.window.showInputBox({
      title: `Duplicate ${target.entry.name}`,
      prompt: target.entry.state === 'selected'
        ? 'The duplicate will preserve build membership through an NGIN authoring plan.'
        : 'Ordinary files are duplicated without changing the product manifest.',
      value: `${stem} copy${extension}`,
      valueSelection: [0, stem.length + 5],
      validateInput: value => value.trim() && !value.includes('/') && !value.includes('\\')
        ? undefined : 'Enter a file name without path separators.'
    });
    if (!name?.trim()) return;
    const destination = path.posix.join(path.posix.dirname(target.entry.relativePath), name.trim());
    const destinationUri = vscode.Uri.file(path.resolve(target.project.directory, destination));
    if (await exists(destinationUri)) throw new Error(`A file already exists at ${destination}.`);
    const contents = await vscode.workspace.fs.readFile(vscode.Uri.file(target.entry.path));
    if (target.entry.state === 'selected') {
      const plan = await authoring.plan(target.context, {
        intent: 'CreateItems', items: [{ path: destination, kind: target.kind }]
      });
      if (plan.state !== 'ready') {
        throw new Error(plan.diagnostics.map(value => value.message).join('\n') || 'The duplicate plan was rejected.');
      }
      await authoring.apply(target.context, plan, new Map([[destination, contents]]));
      controller.invalidateConfiguration();
    } else {
      const edit = new vscode.WorkspaceEdit();
      edit.createFile(destinationUri, { contents });
      if (!await vscode.workspace.applyEdit(edit)) throw new Error('VS Code could not duplicate the file.');
    }
    projectsTree.refresh();
    await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(destinationUri));
  });
  register(extensionContext, 'ngin.importItems', async (argument?: unknown) => {
    const project = projectArgument(argument) ?? await effectiveProject(argument);
    if (!project) throw new Error('No NGIN product is available.');
    const selected = await vscode.window.showOpenDialog({
      title: 'Import C++ Items', canSelectMany: true, canSelectFiles: true, canSelectFolders: false,
      filters: { 'C++ items': ['c', 'cc', 'cpp', 'cxx', 'h', 'hh', 'hpp', 'hxx', 'ixx', 'cppm'] }
    });
    if (!selected?.length) return;
    const base = physicalDestination(argument, project);
    const items: Array<{ path: string; kind: 'Source' | 'Header' | 'CxxModule'; contents: string }> = [];
    const names = new Set<string>();
    for (const source of selected) {
      if (names.has(source.path.split('/').at(-1)!)) throw new Error('Imported item names must be unique.');
      names.add(source.path.split('/').at(-1)!);
      const target = path.join(base, path.basename(source.fsPath));
      if (!isWithin(project.directory, target)) throw new Error('Imported items must remain inside the product.');
      const relative = relativeManifestPath(project.directory, target);
      const inferred = kindForPath(relative);
      if (inferred === 'Resource') throw new Error(`${path.basename(source.fsPath)} is not a recognized C++ item.`);
      items.push({ path: relative, kind: inferred, contents: new TextDecoder().decode(await vscode.workspace.fs.readFile(source)) });
    }
    await createSemanticItems(project, items, 'Import C++ Items');
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
  register(extensionContext, 'ngin.locateActiveFile', async () => {
    const uri = vscode.window.activeTextEditor?.document.uri;
    if (!uri) return void vscode.window.showInformationMessage('Open a product file to locate it in the NGIN Workspace.');
    const project = await sourceAnalysis.projectForFile(uri.fsPath, false);
    if (!project) return void vscode.window.showInformationMessage(`${path.basename(uri.fsPath)} is not inside an NGIN product.`);
    await vscode.commands.executeCommand('ngin.projects.focus');
    await projectsTree.getChildren();
    const root = projectsTree.projectNode(project.manifest);
    if (!root) return;
    await projectsView.reveal(root, { expand: true });
    const node = await projectsTree.revealFile(project, uri.fsPath);
    if (node) await projectsView.reveal(node, { focus: true, select: true, expand: false });
  });
  register(extensionContext, 'ngin.toggleIgnoredFiles', async () => {
    const configuration = vscode.workspace.getConfiguration('ngin');
    const current = configuration.get<boolean>('workspace.showIgnoredFiles', false);
    await configuration.update('workspace.showIgnoredFiles', !current, vscode.ConfigurationTarget.Workspace);
    projectsTree.refresh();
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
