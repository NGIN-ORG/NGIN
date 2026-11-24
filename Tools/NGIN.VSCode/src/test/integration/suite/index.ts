import { NginDebugProvider } from '../../../providers/debug';
import assert from 'node:assert/strict';
import { promises as fs } from 'node:fs';
import { tmpdir } from 'node:os';
import * as path from 'node:path';
import * as vscode from 'vscode';
import type { NginController } from '../../../core/controller';
import type { CompositionGraph, ContextSnapshot, NginContext } from '../../../model';
import { NginCppConfigurationProvider } from '../../../providers/cppTools';

export async function run(): Promise<void> {
  const extension = vscode.extensions.getExtension('ngin.ngin-tools');
  assert.ok(extension, 'NGIN extension is discoverable in the extension host');
  await extension.activate();
  assert.equal(extension.isActive, true);

  const commands = await vscode.commands.getCommands(true);
  for (const command of ['ngin.explainMembership', 'ngin.copyCliCommand', 'ngin.selectProject', 'ngin.setDefaultProject', 'ngin.setLaunchProduct', 'ngin.projectActions', 'ngin.checkSetup', 'ngin.createProject', 'ngin.showFilesView', 'ngin.showProjectView', 'ngin.newFile', 'ngin.newFolder', 'ngin.newSourceFile', 'ngin.newHeaderFile', 'ngin.newClass', 'ngin.newModule', 'ngin.newCppItem', 'ngin.renameItem', 'ngin.moveItem', 'ngin.deleteItem', 'ngin.duplicateItem', 'ngin.importItems', 'ngin.locateActiveFile', 'ngin.toggleIgnoredFiles', 'ngin.configure', 'ngin.build', 'ngin.rebuild', 'ngin.clean', 'ngin.run', 'ngin.runWithArguments', 'ngin.debug', 'ngin.test', 'ngin.runCMakeTests', 'ngin.runCMakeTest', 'ngin.benchmark', 'ngin.selectRun', 'ngin.selectProfile', 'ngin.selectConfigurePreset', 'ngin.buildCMakeTarget', 'ngin.openCMakeDeclaration', 'ngin.showCMakeArtifacts', 'ngin.openCMakeProject', 'ngin.refreshCMakeProject', 'ngin.openDevelopmentProject', 'ngin.showConsumingProjects', 'ngin.showExportedTargets', 'ngin.lock', 'ngin.analyzeFile', 'ngin.changeMembership', 'ngin.revealOwningProject', 'ngin.revealProjectFile', 'ngin.openFile', 'ngin.copyFilePath', 'ngin.showGraph', 'ngin.openDashboard', 'ngin.openOutputDirectory']) {
    assert.ok(commands.includes(command), `${command} is registered`);
  }
  for (const command of ['ngin.renameFile', 'ngin.duplicateFile', 'ngin.deleteFile', 'ngin.revealFile']) {
    assert.equal(commands.includes(command), false, `${command} is retired in favor of the native Explorer`);
  }

  const workspace = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
  assert.ok(workspace, 'test workspace is open');
  await vscode.commands.executeCommand('ngin.showFilesView');
  await vscode.commands.executeCommand('ngin.showProjectView');
  const manifest = vscode.Uri.file(path.join(workspace, 'Examples', 'Hello.Native', 'Hello.Native.nginproj'));
  await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(manifest));
  await vscode.commands.executeCommand('ngin.refresh');
  await vscode.commands.executeCommand('ngin.setLaunchProduct', {
    manifest: manifest.fsPath,
    directory: path.dirname(manifest.fsPath),
    name: 'Hello.Native',
    artifactKind: 'Executable'
  });
  await vscode.commands.executeCommand('ngin.showGraph');
  const graphDocument = vscode.window.activeTextEditor?.document;
  assert.equal(graphDocument?.languageId, 'json');
  assert.match(graphDocument?.getText() ?? '', /"kind": "NGIN\.CompositionGraph"/);
  assert.match(graphDocument?.getText() ?? '', /"name": "Hello\.Native"/);

  const composition = JSON.parse(graphDocument!.getText()) as CompositionGraph;
  const providerOutput = await fs.mkdtemp(path.join(tmpdir(), 'ngin-cpp-provider-'));
  const providerContext: NginContext = {
    workspaceFolder: workspace,
    workspaceManifest: path.join(workspace, 'NGIN.ngin'),
    projectManifest: manifest.fsPath,
    projectName: 'Hello.Native',
    configuration: 'Debug', target: 'host', toolchain: 'auto', options: {},
    outputDirectory: providerOutput
  };
  const providerEvents = new vscode.EventEmitter<ContextSnapshot>();
  const cppProvider = new NginCppConfigurationProvider({
    snapshot: { context: providerContext, graph: composition },
    onDidChange: providerEvents.event
  } as unknown as NginController);
  const nativeSource = vscode.Uri.file(path.join(workspace, 'Examples', 'Hello.Native', 'src', 'main.cpp'));
  const providerStarted = Date.now();
  assert.equal(await cppProvider.canProvideConfiguration(nativeSource), true);
  const provided = await cppProvider.provideConfigurations([nativeSource]);
  assert.equal(provided.length, 1, 'graph fallback supplies IntelliSense without a compile database');
  assert.ok(Date.now() - providerStarted < 2_000, 'IntelliSense provider responds within the C/C++ deadline');

  const packageSource = path.join(workspace, 'Packages', 'NGIN.UI', 'src', 'NGIN', 'UI', 'Controls.cpp');
  const packageInclude = path.join(workspace, 'Packages', 'NGIN.UI', 'include');
  await fs.mkdir(path.join(providerOutput, 'cmake'), { recursive: true });
  await fs.writeFile(path.join(providerOutput, 'cmake', 'compile_commands.json'), JSON.stringify([{
    directory: workspace,
    file: packageSource,
    arguments: ['/usr/bin/c++', '-I', packageInclude, '-std=c++23', '-c', packageSource]
  }]), 'utf8');
  const dependencyUri = vscode.Uri.file(packageSource);
  assert.equal(await cppProvider.canProvideConfiguration(dependencyUri), false,
    'dependency ownership is prepared asynchronously');
  const dependencyDeadline = Date.now() + 2_000;
  while (!await cppProvider.canProvideConfiguration(dependencyUri) && Date.now() < dependencyDeadline) {
    await new Promise(resolve => setTimeout(resolve, 10));
  }
  const dependencyConfiguration = await cppProvider.provideConfigurations([dependencyUri]);
  assert.equal(dependencyConfiguration.length, 1, 'active build compile entry owns dependency source');
  assert.ok(dependencyConfiguration[0].configuration.includePath.includes(packageInclude),
    'dependency source receives include paths from its exact compile command');
  cppProvider.dispose();
  providerEvents.dispose();
  await fs.rm(providerOutput, { recursive: true, force: true });

  const tasks = await vscode.tasks.fetchTasks({ type: 'ngin' });
  assert.ok(tasks.some(task => task.definition.command === 'build'), 'active project supplies a build task');

  const hostedSource = vscode.Uri.file(path.join(workspace, 'Examples', 'Hello.Hosted', 'src', 'main.cpp'));
  await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(hostedSource));
  const terminalExit = new Promise<number | undefined>((resolve, reject) => {
    const subscription = vscode.tasks.onDidEndTaskProcess(event => {
      if (event.execution.task.definition.type !== 'ngin' || event.execution.task.definition.command !== 'run') return;
      clearTimeout(timer);
      subscription.dispose();
      resolve(event.exitCode);
    });
    const timer = setTimeout(() => { subscription.dispose(); reject(new Error('Interactive Run task timed out')); }, 45_000);
  });
  const launched = await Promise.all([terminalExit, vscode.commands.executeCommand('ngin.run')]);
  assert.equal(launched[0], 0, 'running with another source open launches the active project in a terminal');
  // F5 selection can be exercised without installing a native debug adapter.
  const active = { manifest: manifest.fsPath, name: 'Hello.Native' };
  let preparedProject: string | undefined;
  const debug = new NginDebugProvider({
    snapshot: { context: providerContext }, launchProduct: active, projects: [active],
    contextForProject: () => providerContext,
    graphForContext: async () => composition,
    prepareLaunch: async (context: NginContext) => {
      preparedProject = context.projectManifest;
      return { program: '/selected/app', cwd: '/selected', args: [], environment: {} };
    }
  } as unknown as NginController, {} as import('../../../providers/sourceAnalysis').SourceAnalysisProvider);
  const configuration = await debug.resolveDebugConfiguration(undefined,
    { type: 'ngin', request: 'launch', name: 'F5' });
  assert.equal(configuration?.program, '/selected/app');
  assert.equal(preparedProject, manifest.fsPath, 'F5 keeps the selected application while editing another project');
  await vscode.commands.executeCommand('ngin.revealOwningProject', hostedSource);
  const tasksAfterReveal = await vscode.tasks.fetchTasks({ type: 'ngin' });
  assert.ok(tasksAfterReveal.some(task => task.name.includes('Hello.Native')),
    'revealing a project does not change the selected Active Project');

  const emptyManifest = await vscode.workspace.openTextDocument({ language: 'ngin', content: '<' });
  const rootCompletion = await vscode.commands.executeCommand<vscode.CompletionList>(
    'vscode.executeCompletionItemProvider', emptyManifest.uri, new vscode.Position(0, 1), '<'
  );
  assert.ok(rootCompletion.items.some(item => item.label === 'Executable'));
  assert.ok(rootCompletion.items.some(item => item.label === 'Library'));
  assert.equal(rootCompletion.items.some(item => item.label === 'Project'), false);

  const libraryManifest = await vscode.workspace.openTextDocument({
    language: 'ngin', content: '<Library Name="Example" Kind="" />'
  });
  const kindOffset = libraryManifest.getText().indexOf('Kind="') + 'Kind="'.length;
  const kindCompletion = await vscode.commands.executeCommand<vscode.CompletionList>(
    'vscode.executeCompletionItemProvider', libraryManifest.uri, libraryManifest.positionAt(kindOffset), '"'
  );
  for (const kind of ['Static', 'Shared', 'Interface', 'Plugin']) {
    assert.ok(kindCompletion.items.some(item => item.label === kind), `${kind} is completed from generated metadata`);
  }
  const valid = await vscode.commands.executeCommand<boolean>('ngin.validate');
  assert.equal(valid, true);

  const staleManifest = await vscode.workspace.openTextDocument({
    language: 'ngin', content: '<Library Name="Legacy" Kind="Module" />'
  });
  const fixes = await vscode.commands.executeCommand<(vscode.CodeAction | vscode.Command)[]>(
    'vscode.executeCodeActionProvider', staleManifest.uri, new vscode.Range(0, 0, 0, staleManifest.getText().length)
  );
  assert.ok(fixes.some(fix => fix.title === 'Change library Kind to Plugin'));
  assert.equal(fixes.some(fix => /product type|linkage/iu.test(fix.title)), false);

  const analyzerManifest = path.join(workspace, 'Examples', 'Hello.Analyzer', 'Hello.Analyzer.nginproj');
  const analyzerProject = {
    manifest: analyzerManifest,
    directory: path.dirname(analyzerManifest),
    name: 'Hello.Analyzer',
    artifactKind: 'Executable',
    workspaceManifest: path.join(workspace, 'NGIN.ngin'),
    hasAnalyze: true
  };
  await vscode.commands.executeCommand('ngin.enableProjectTooling', analyzerProject);
  const analyzedSource = vscode.Uri.file(path.join(path.dirname(analyzerManifest), 'src', 'main.cpp'));
  await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(analyzedSource));
  const deadline = Date.now() + 20_000;
  let sourceProblems = vscode.languages.getDiagnostics(analyzedSource);
  while (!sourceProblems.some(problem => problem.code === 'readability-magic-numbers') && Date.now() < deadline) {
    await new Promise(resolve => setTimeout(resolve, 100));
    sourceProblems = vscode.languages.getDiagnostics(analyzedSource);
  }
  const tidy = sourceProblems.find(problem => problem.code === 'readability-magic-numbers');
  assert.ok(tidy, 'opening a source file publishes its resolved analyzer diagnostics');
  assert.equal(tidy.source, 'NGIN.Tooling.ClangTidy::Analyze');
}
