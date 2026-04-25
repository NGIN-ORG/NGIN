import * as path from 'node:path';
import { spawn } from 'node:child_process';
import * as vscode from 'vscode';
import {
  fileExists,
  findExecutableOnPath,
  getDevelopmentCliPath,
  isCliStale,
  resolveConfiguredCliPath
} from './core/cli';
import { pathExists, readTextFile } from './core/discovery';
import {
  computeLaunchManifestPath,
  getExecutableCandidatePaths,
  getWorkingDirectoryCandidates,
  parseCliDiagnostics
} from './core/helpers';
import { createNativeDebugConfiguration, quoteShellArgument } from './core/debug';
import { LaunchManifest } from './core/types';
import { parseLaunchManifest } from './core/xml';
import {
  NginCommandTarget,
  ResolvedCommandContext,
  ResolvedWorkspaceInfo,
  WorkspaceStateService
} from './state/workspaceState';
import { NginSidebarController } from './ui/sidebar';
import { NginStatusBarController } from './ui/statusBar';
import { NginCppToolsProviderService } from './cpptools/provider';

const SUPPORTED_LANGUAGE_ID = 'ngin';

interface BuildResult {
  outputDir: string;
  launchManifestPath: string;
}

interface CliRunResult {
  exitCode: number;
  output: string;
}

interface NginTaskDefinition extends vscode.TaskDefinition {
  command: 'build' | 'clean' | 'rebuild' | 'validate' | 'graph' | 'metagen' | 'workspaceStatus' | 'workspaceDoctor';
  project?: string;
  configuration?: string;
  output?: string;
}

interface NginDebugConfiguration extends vscode.DebugConfiguration {
  project?: string;
  configuration?: string;
  cliPath?: string;
  outputDir?: string;
  programArgs?: string[];
  env?: Record<string, string>;
  preBuild?: boolean;
}

function comparablePath(value: string): string {
  const normalized = path.normalize(value);
  return process.platform === 'win32' ? normalized.toLowerCase() : normalized;
}

class NginController implements vscode.Disposable {
  private readonly outputChannel = vscode.window.createOutputChannel('NGIN');
  private readonly diagnostics = vscode.languages.createDiagnosticCollection('ngin');
  private readonly workspaceState: WorkspaceStateService;
  private readonly sidebar: NginSidebarController;
  private readonly statusBar: NginStatusBarController;
  private readonly cppToolsProvider: NginCppToolsProviderService;
  private staleWarningShown = false;

  constructor(private readonly context: vscode.ExtensionContext) {
    this.workspaceState = new WorkspaceStateService(context);
    this.sidebar = new NginSidebarController();
    this.statusBar = new NginStatusBarController();
    this.cppToolsProvider = new NginCppToolsProviderService(
      context.extension.id,
      () => this.workspaceState.getSnapshot(),
      this.outputChannel
    );
  }

  dispose(): void {
    this.cppToolsProvider.dispose();
    this.statusBar.dispose();
    this.sidebar.dispose();
    this.workspaceState.dispose();
    this.outputChannel.dispose();
    this.diagnostics.dispose();
  }

  register(): vscode.Disposable[] {
    return [
      vscode.commands.registerCommand('ngin.selectProject', (arg) => this.runHandled(() => this.selectProjectCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.selectConfiguration', (arg) => this.runHandled(() => this.selectConfigurationCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.build', (arg) => this.runHandled(() => this.buildCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.clean', (arg) => this.runHandled(() => this.cleanCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.rebuild', (arg) => this.runHandled(() => this.rebuildCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.run', (arg) => this.runHandled(() => this.runCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.debug', (arg) => this.runHandled(() => this.debugCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.validate', (arg) => this.runHandled(() => this.validateCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.graph', (arg) => this.runHandled(() => this.graphCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.metagen', (arg) => this.runHandled(() => this.metaGenCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.workspaceStatus', () => this.runHandled(() => this.workspaceCommand('status'))),
      vscode.commands.registerCommand('ngin.workspaceDoctor', () => this.runHandled(() => this.workspaceCommand('doctor'))),
      vscode.commands.registerCommand('ngin.openLastLaunchManifest', () => this.runHandled(() => this.openLastLaunchManifest())),
      vscode.commands.registerCommand('ngin.refresh', () => this.runHandled(() => this.refreshUi())),
      vscode.commands.registerCommand('ngin.internal.pickProject', (arg) => this.runHandled(() => this.pickProjectFromStatusBar(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.internal.pickConfiguration', (arg) => this.runHandled(() => this.pickConfigurationFromStatusBar(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.internal.openPath', (filePath) => this.runHandled(() => this.openPathCommand(filePath))),
      vscode.commands.registerCommand('ngin.internal.revealPath', (filePath) => this.runHandled(() => this.revealPathCommand(filePath))),
      vscode.workspace.onDidSaveTextDocument((document) => this.handleDocumentSaved(document)),
      vscode.tasks.registerTaskProvider('ngin', new NginTaskProvider(this)),
      vscode.debug.registerDebugConfigurationProvider('ngin', new NginDebugConfigurationProvider(this)),
      this.workspaceState.onDidChange(() => {
        void this.refreshUi();
      })
    ];
  }

  async initialize(): Promise<void> {
    await this.refreshUi();
  }

  async getTaskContexts(): Promise<ResolvedCommandContext[]> {
    return this.workspaceState.getTaskContexts();
  }

  getConfiguredBuildOutputRoot(scope?: vscode.WorkspaceFolder): string | undefined {
    return this.workspaceState.getConfiguredBuildOutputRoot(scope);
  }

  async getCliCommandHint(workspaceRoot: string): Promise<string> {
    const configured = this.getConfiguration().get<string>('cli.path')?.trim();
    if (configured) {
      return resolveConfiguredCliPath(workspaceRoot, configured) ?? configured;
    }

    const developmentPath = getDevelopmentCliPath(workspaceRoot, process.platform);
    if (await fileExists(developmentPath)) {
      return developmentPath;
    }

    return 'ngin';
  }

  async resolveDebugConfiguration(
    folder: vscode.WorkspaceFolder | undefined,
    configuration: NginDebugConfiguration
  ): Promise<vscode.DebugConfiguration | undefined> {
    const context = await this.resolveCommandContext({
      preferredUri: folder?.uri,
      projectPath: configuration.project,
      configurationName: configuration.configuration
    }, false);

    if (!context) {
      void vscode.window.showErrorMessage('Unable to resolve the NGIN project and configuration for debugging.');
      return undefined;
    }

    const cpptools = vscode.extensions.getExtension('ms-vscode.cpptools');
    if (!cpptools) {
      void vscode.window.showErrorMessage('NGIN debugging requires the Microsoft C/C++ extension (`ms-vscode.cpptools`).');
      return undefined;
    }

    const buildResult = await this.getLaunchBuildResult(context, {
      cliOverride: configuration.cliPath,
      outputDirOverride: configuration.outputDir,
      forceBuild: configuration.preBuild === true
    });

    const launchManifest = await this.readLaunchManifest(buildResult.launchManifestPath);
    const launch = await this.resolveLaunchTarget(launchManifest, context.project.directory, buildResult.outputDir);

    const native = createNativeDebugConfiguration({
      platform: process.platform,
      program: launch.program,
      cwd: launch.cwd,
      args: configuration.programArgs ?? [],
      env: configuration.env ?? {},
      miDebuggerPath: this.getConfiguration(context.workspace.folder).get<string>('debug.miDebuggerPath')?.trim() || undefined
    });

    native.name = configuration.name ?? `NGIN: Debug ${context.project.name} [${context.configuration.name}]`;
    return native as vscode.DebugConfiguration;
  }

  async provideDebugConfigurations(folder: vscode.WorkspaceFolder | undefined): Promise<vscode.DebugConfiguration[]> {
    const workspaceInfo = await this.workspaceState.getWorkspaceInfo(folder?.uri);
    if (!workspaceInfo) {
      return [];
    }

    const configurations: vscode.DebugConfiguration[] = [];
    for (const project of workspaceInfo.projects) {
      const configurationName = project.defaultConfiguration ?? project.configurations[0]?.name;
      if (!configurationName) {
        continue;
      }

      configurations.push({
        type: 'ngin',
        request: 'launch',
        name: `NGIN: Debug ${project.name} [${configurationName}]`,
        project: project.path,
        configuration: configurationName,
        preBuild: false,
        programArgs: [],
        env: {}
      });
    }

    return configurations;
  }

  async createTask(definition: NginTaskDefinition, scope?: vscode.WorkspaceFolder): Promise<vscode.Task> {
    const workspaceInfo = await this.workspaceState.getWorkspaceInfo(scope?.uri);
    const workspaceRoot = workspaceInfo?.root ?? scope?.uri.fsPath ?? process.cwd();
    const cliCommand = await this.getCliCommandHint(workspaceRoot);
    const args = this.buildTaskArguments(workspaceInfo, definition);
    const execution = new vscode.ProcessExecution(cliCommand, args, { cwd: workspaceRoot });
    const task = new vscode.Task(
      definition,
      scope ?? vscode.TaskScope.Workspace,
      this.getTaskLabel(definition),
      'ngin',
      execution,
      ['$ngin-file']
    );

    if (definition.command === 'build') {
      task.group = vscode.TaskGroup.Build;
    }
    if (definition.command === 'rebuild') {
      task.group = vscode.TaskGroup.Build;
    }
    if (definition.command === 'clean') {
      task.group = vscode.TaskGroup.Clean;
    }

    return task;
  }

  private getConfiguration(scope?: vscode.WorkspaceFolder): vscode.WorkspaceConfiguration {
    return vscode.workspace.getConfiguration('ngin', scope);
  }

  private asCommandTarget(value: unknown): NginCommandTarget | undefined {
    if (!value || typeof value !== 'object') {
      return undefined;
    }

    const candidate = value as NginCommandTarget;
    if (!candidate.preferredUri && !candidate.projectPath && !candidate.configurationName) {
      return undefined;
    }

    return {
      preferredUri: candidate.preferredUri,
      projectPath: candidate.projectPath,
      configurationName: candidate.configurationName
    };
  }

  private async refreshUi(preferredUri?: vscode.Uri): Promise<void> {
    const snapshot = await this.workspaceState.getSnapshot(preferredUri);
    this.sidebar.refresh(snapshot);
    this.statusBar.refresh(snapshot);
    await this.cppToolsProvider.refresh(snapshot);
  }

  private async runHandled(action: () => Promise<void>): Promise<void> {
    try {
      await action();
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      this.outputChannel.show(true);
      this.outputChannel.appendLine(message);
      void vscode.window.showErrorMessage(message);
    }
  }

  private async resolveCommandContext(target?: NginCommandTarget, promptIfNeeded = true): Promise<ResolvedCommandContext | undefined> {
    const context = await this.workspaceState.resolveCommandContext({
      preferredUri: target?.preferredUri,
      explicitProjectPath: target?.projectPath,
      explicitConfiguration: target?.configurationName,
      promptIfNeeded
    });

    if (context) {
      return context;
    }

    const workspaceInfo = await this.workspaceState.getWorkspaceInfo(target?.preferredUri);
    if (!workspaceInfo) {
      void vscode.window.showErrorMessage('NGIN workspace not found. Open a folder with a .ngin workspace manifest.');
    } else if (target?.projectPath || target?.configurationName) {
      void vscode.window.showErrorMessage('Unable to resolve the selected NGIN project or configuration.');
    }

    return undefined;
  }

  private async selectProjectCommand(target?: NginCommandTarget): Promise<void> {
    if (target?.projectPath) {
      const context = await this.resolveCommandContext(target, false);
      if (context) {
        await this.refreshUi(target.preferredUri);
      }
      return;
    }

    const workspaceInfo = await this.workspaceState.getWorkspaceInfo(target?.preferredUri);
    if (!workspaceInfo) {
      void vscode.window.showErrorMessage('NGIN workspace not found.');
      return;
    }

    const project = await this.workspaceState.pickProject(workspaceInfo);
    if (!project) {
      return;
    }

    const context = await this.resolveCommandContext({ preferredUri: target?.preferredUri, projectPath: project.path }, false);
    if (!context) {
      return;
    }

    await this.refreshUi(target?.preferredUri);
    void vscode.window.showInformationMessage(`Selected NGIN project: ${context.project.name}`);
  }

  private async selectConfigurationCommand(target?: NginCommandTarget): Promise<void> {
    if (target?.projectPath && target?.configurationName) {
      const context = await this.resolveCommandContext(target, false);
      if (context) {
        await this.refreshUi(target.preferredUri);
      }
      return;
    }

    const workspaceInfo = await this.workspaceState.getWorkspaceInfo(target?.preferredUri);
    if (!workspaceInfo) {
      void vscode.window.showErrorMessage('NGIN workspace not found.');
      return;
    }

    const currentContext = await this.workspaceState.resolveCommandContext({
      preferredUri: target?.preferredUri,
      explicitProjectPath: target?.projectPath,
      promptIfNeeded: false
    });

    const project = currentContext?.project
      ?? (workspaceInfo.projects.length === 1 ? workspaceInfo.projects[0] : await this.workspaceState.pickProject(workspaceInfo));
    if (!project) {
      return;
    }

    const configuration = await this.workspaceState.pickConfiguration(project);
    if (!configuration) {
      return;
    }

    await this.workspaceState.rememberSelection({ workspace: workspaceInfo, project, configuration });
    await this.refreshUi(target?.preferredUri);
    void vscode.window.showInformationMessage(`Selected NGIN configuration: ${project.name} [${configuration.name}]`);
  }

  private async pickProjectFromStatusBar(target?: NginCommandTarget): Promise<void> {
    const workspaceInfo = await this.workspaceState.getWorkspaceInfo(target?.preferredUri);
    if (!workspaceInfo) {
      void vscode.window.showErrorMessage('NGIN workspace not found.');
      return;
    }

    const project = await this.workspaceState.promptForProject(workspaceInfo);
    if (!project) {
      return;
    }

    const configuration = await this.workspaceState.resolveStoredConfiguration(project)
      ?? await this.workspaceState.promptForConfiguration(project);
    if (!configuration) {
      return;
    }

    const context = { workspace: workspaceInfo, project, configuration };
    await this.workspaceState.rememberSelection(context);
    await this.refreshUi(target?.preferredUri);
    void vscode.window.showInformationMessage(`Selected NGIN project: ${context.project.name}`);
  }

  private async pickConfigurationFromStatusBar(target?: NginCommandTarget): Promise<void> {
    const snapshot = await this.workspaceState.getSnapshot(target?.preferredUri);
    if (!snapshot.workspace || !snapshot.context) {
      void vscode.window.showErrorMessage('NGIN workspace not found.');
      return;
    }

    const configuration = await this.workspaceState.promptForConfiguration(snapshot.context.project);
    if (!configuration) {
      return;
    }

    await this.workspaceState.rememberSelection({
      workspace: snapshot.workspace,
      project: snapshot.context.project,
      configuration
    });
    await this.refreshUi(target?.preferredUri);
    void vscode.window.showInformationMessage(`Selected NGIN configuration: ${snapshot.context.project.name} [${configuration.name}]`);
  }

  private async buildCommand(target?: NginCommandTarget): Promise<void> {
    const context = await this.resolveCommandContext(target);
    if (!context) {
      return;
    }

    await this.buildProject(context);
  }

  private async cleanCommand(target?: NginCommandTarget): Promise<void> {
    const context = await this.resolveCommandContext(target);
    if (!context) {
      return;
    }

    const outputDir = this.workspaceState.computeOutputDirectory(context);
    const launchManifestPath = computeLaunchManifestPath(outputDir, context.project.name, context.configuration.name);
    const result = await this.runCli(
      context.workspace.root,
      ['clean', '--project', context.project.path, '--configuration', context.configuration.name, '--output', outputDir],
      vscode.Uri.file(context.project.path)
    );

    if (result.exitCode !== 0) {
      throw new Error(`ngin clean failed for ${context.project.name} [${context.configuration.name}]`);
    }

    await this.workspaceState.clearLastLaunchManifestPath(launchManifestPath);
    await this.refreshUi(context.workspace.folder?.uri);
    void vscode.window.showInformationMessage(`Cleaned ${context.project.name} [${context.configuration.name}]`);
  }

  private async rebuildCommand(target?: NginCommandTarget): Promise<void> {
    const context = await this.resolveCommandContext(target);
    if (!context) {
      return;
    }

    await this.buildProject(context, { command: 'rebuild' });
  }

  private async validateCommand(target?: NginCommandTarget, options?: { silent?: boolean }): Promise<void> {
    const context = await this.resolveCommandContext(target, !options?.silent);
    if (!context) {
      return;
    }

    const result = await this.runCli(
      context.workspace.root,
      ['validate', '--project', context.project.path, '--configuration', context.configuration.name],
      vscode.Uri.file(context.project.path)
    );

    if (result.exitCode !== 0) {
      if (options?.silent) {
        return;
      }
      throw new Error(`ngin validate failed for ${context.project.name} [${context.configuration.name}]`);
    }

    if (!options?.silent) {
      void vscode.window.showInformationMessage(`Validated ${context.project.name} [${context.configuration.name}]`);
    }
  }

  private async graphCommand(target?: NginCommandTarget): Promise<void> {
    const context = await this.resolveCommandContext(target);
    if (!context) {
      return;
    }

    const result = await this.runCli(
      context.workspace.root,
      ['graph', '--project', context.project.path, '--configuration', context.configuration.name],
      vscode.Uri.file(context.project.path)
    );

    if (result.exitCode !== 0) {
      throw new Error(`ngin graph failed for ${context.project.name} [${context.configuration.name}]`);
    }
  }

  private async metaGenCommand(target?: NginCommandTarget): Promise<void> {
    const context = await this.resolveCommandContext(target);
    if (!context) {
      return;
    }

    const result = await this.runCli(
      context.workspace.root,
      ['metagen', '--project', context.project.path, '--configuration', context.configuration.name],
      vscode.Uri.file(context.project.path)
    );

    if (result.exitCode !== 0) {
      throw new Error(`ngin metagen failed for ${context.project.name} [${context.configuration.name}]`);
    }

    const generatedPath = this.extractGeneratedPath(result.output);
    if (generatedPath) {
      void vscode.window.showInformationMessage(
        `Generated metadata for ${context.project.name} [${context.configuration.name}]`,
        'Open Generated File'
      ).then((choice) => {
        if (choice === 'Open Generated File') {
          void this.openPathCommand(path.isAbsolute(generatedPath) ? generatedPath : path.resolve(context.workspace.root, generatedPath));
        }
      });
      return;
    }

    void vscode.window.showInformationMessage(`Generated metadata for ${context.project.name} [${context.configuration.name}]`);
  }

  private async workspaceCommand(subcommand: 'status' | 'doctor'): Promise<void> {
    const workspaceInfo = await this.workspaceState.getWorkspaceInfo();
    if (!workspaceInfo) {
      void vscode.window.showErrorMessage('NGIN workspace not found.');
      return;
    }

    const result = await this.runCli(
      workspaceInfo.root,
      ['workspace', subcommand],
      vscode.Uri.file(workspaceInfo.workspace.path)
    );

    if (result.exitCode !== 0) {
      throw new Error(`ngin workspace ${subcommand} failed`);
    }
  }

  private async runCommand(target?: NginCommandTarget): Promise<void> {
    const context = await this.resolveCommandContext(target);
    if (!context) {
      return;
    }

    const buildResult = await this.getLaunchBuildResult(context);
    const launchManifest = await this.readLaunchManifest(buildResult.launchManifestPath);
    const launch = await this.resolveLaunchTarget(launchManifest, context.project.directory, buildResult.outputDir);

    const terminalName = this.getConfiguration(context.workspace.folder).get<string>('run.terminalName')?.trim() || 'NGIN';
    const terminal = vscode.window.createTerminal({
      name: terminalName,
      cwd: launch.cwd
    });

    terminal.show(true);
    terminal.sendText(quoteShellArgument(launch.program), true);
  }

  private async debugCommand(target?: NginCommandTarget): Promise<void> {
    const context = await this.resolveCommandContext(target);
    if (!context) {
      return;
    }

    const configuration: NginDebugConfiguration = {
      type: 'ngin',
      request: 'launch',
      name: `NGIN: Debug ${context.project.name} [${context.configuration.name}]`,
      project: context.project.path,
      configuration: context.configuration.name,
      preBuild: false,
      programArgs: [],
      env: {}
    };

    await vscode.debug.startDebugging(context.workspace.folder, configuration);
  }

  private async openPathCommand(filePath: unknown): Promise<void> {
    if (typeof filePath !== 'string' || !filePath) {
      return;
    }

    if (!(await pathExists(filePath))) {
      void vscode.window.showErrorMessage(`Path not found: ${filePath}`);
      return;
    }

    const document = await vscode.workspace.openTextDocument(vscode.Uri.file(filePath));
    await vscode.window.showTextDocument(document, { preview: false });
  }

  private async revealPathCommand(filePath: unknown): Promise<void> {
    if (typeof filePath !== 'string' || !filePath) {
      return;
    }

    if (!(await pathExists(filePath))) {
      void vscode.window.showErrorMessage(`Path not found: ${filePath}`);
      return;
    }

    const targetUri = vscode.Uri.file(filePath);
    try {
      await vscode.commands.executeCommand('revealFileInOS', targetUri);
    } catch {
      await vscode.env.openExternal(targetUri);
    }
  }

  private async openLastLaunchManifest(): Promise<void> {
    const manifestPath = this.workspaceState.getLastLaunchManifestPath();
    if (!manifestPath) {
      void vscode.window.showErrorMessage('No NGIN launch manifest has been recorded yet.');
      return;
    }

    await this.openPathCommand(manifestPath);
  }

  private async handleDocumentSaved(document: vscode.TextDocument): Promise<void> {
    if (document.languageId !== SUPPORTED_LANGUAGE_ID) {
      return;
    }

    if (!this.getConfiguration(vscode.workspace.getWorkspaceFolder(document.uri)).get<boolean>('validate.onSave')) {
      return;
    }

    try {
      await this.validateCommand({ preferredUri: document.uri }, { silent: true });
    } catch {
      // Silent validate-on-save still updates diagnostics through CLI output parsing.
    }
  }

  private async buildProject(
    context: ResolvedCommandContext,
    options?: { cliOverride?: string; outputDirOverride?: string; command?: 'build' | 'rebuild' }
  ): Promise<BuildResult> {
    const outputDir = this.workspaceState.computeOutputDirectory(context, options?.outputDirOverride);
    const args = [
      options?.command ?? 'build',
      '--project',
      context.project.path,
      '--configuration',
      context.configuration.name,
      '--output',
      outputDir
    ];

    const result = await this.runCli(
      context.workspace.root,
      args,
      vscode.Uri.file(context.project.path),
      options?.cliOverride
    );

    if (result.exitCode !== 0) {
      throw new Error(`ngin ${options?.command ?? 'build'} failed for ${context.project.name} [${context.configuration.name}]`);
    }

    const launchManifestPath = computeLaunchManifestPath(outputDir, context.project.name, context.configuration.name);
    if (!(await pathExists(launchManifestPath))) {
      throw new Error(`Expected launch manifest was not produced: ${launchManifestPath}`);
    }

    await this.workspaceState.setLastLaunchManifestPath(launchManifestPath);
    return {
      outputDir,
      launchManifestPath
    };
  }

  private async getLaunchBuildResult(
    context: ResolvedCommandContext,
    options?: { cliOverride?: string; outputDirOverride?: string; forceBuild?: boolean }
  ): Promise<BuildResult> {
    const outputDir = this.workspaceState.computeOutputDirectory(context, options?.outputDirOverride);
    const launchManifestPath = computeLaunchManifestPath(outputDir, context.project.name, context.configuration.name);

    if (!options?.forceBuild && await pathExists(launchManifestPath)) {
      try {
        const launchManifest = await this.readLaunchManifest(launchManifestPath);
        await this.resolveLaunchTarget(launchManifest, context.project.directory, outputDir);
        await this.workspaceState.setLastLaunchManifestPath(launchManifestPath);
        return {
          outputDir,
          launchManifestPath
        };
      } catch {
        // Fall back to a rebuild when the generated output is incomplete or stale.
      }
    }

    return this.buildProject(context, options);
  }

  private async readLaunchManifest(launchManifestPath: string): Promise<LaunchManifest> {
    if (!(await pathExists(launchManifestPath))) {
      throw new Error(`Launch manifest not found: ${launchManifestPath}`);
    }

    const xml = await readTextFile(launchManifestPath);
    return parseLaunchManifest(xml, launchManifestPath);
  }

  private async resolveLaunchTarget(
    launchManifest: LaunchManifest,
    projectDir: string,
    outputDir: string
  ): Promise<{ program: string; cwd: string }> {
    const executableCandidates = getExecutableCandidatePaths(launchManifest, outputDir, process.platform);
    let program: string | undefined;
    for (const candidate of executableCandidates) {
      if (await fileExists(candidate)) {
        program = candidate;
        break;
      }
    }

    if (!program) {
      throw new Error(`Unable to resolve a staged executable from ${launchManifest.path}`);
    }

    const cwdCandidates = getWorkingDirectoryCandidates(launchManifest, outputDir, projectDir);
    let cwd = outputDir;
    for (const candidate of cwdCandidates) {
      if (await pathExists(candidate)) {
        cwd = candidate;
        break;
      }
    }

    return { program, cwd };
  }

  private async runCli(
    workspaceRoot: string,
    args: string[],
    diagnosticsResource?: vscode.Uri,
    cliOverride?: string
  ): Promise<CliRunResult> {
    const cliPath = await this.resolveCliPath(workspaceRoot, cliOverride);
    await this.warnIfCliStale(cliPath, workspaceRoot);

    this.outputChannel.show(true);
    this.outputChannel.appendLine(`> ${cliPath} ${args.join(' ')}`);

    const result = await new Promise<CliRunResult>((resolve, reject) => {
      let combined = '';
      const child = spawn(cliPath, args, { cwd: workspaceRoot });

      child.stdout.on('data', (chunk) => {
        const text = chunk.toString();
        combined += text;
        this.outputChannel.append(text);
      });

      child.stderr.on('data', (chunk) => {
        const text = chunk.toString();
        combined += text;
        this.outputChannel.append(text);
      });

      child.on('error', (error) => reject(error));
      child.on('close', (code) => {
        const exitCode = code ?? 0;
        if (exitCode !== 0) {
          combined += `\nerror: command exited with code ${exitCode}\n`;
        }
        resolve({ exitCode, output: combined });
      });
    });

    this.applyDiagnostics(result.output, diagnosticsResource);
    return result;
  }

  private extractGeneratedPath(output: string): string | undefined {
    const line = output.split(/\r?\n/).find((entry) => entry.startsWith('generated: '));
    return line?.slice('generated: '.length).trim() || undefined;
  }

  private applyDiagnostics(output: string, fallbackResource?: vscode.Uri): void {
    const parsed = parseCliDiagnostics(output);
    this.diagnostics.clear();

    if (parsed.length === 0) {
      return;
    }

    const grouped = new Map<string, vscode.Diagnostic[]>();
    for (const entry of parsed) {
      const resource = entry.file
        ? vscode.Uri.file(
            path.isAbsolute(entry.file) || !fallbackResource
              ? entry.file
              : path.resolve(path.dirname(fallbackResource.fsPath), entry.file)
          )
        : fallbackResource;
      if (!resource) {
        continue;
      }

      const diagnostic = new vscode.Diagnostic(
        new vscode.Range(0, 0, 0, 1),
        entry.message,
        entry.severity === 'warning' ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error
      );

      const key = resource.toString();
      const bucket = grouped.get(key) ?? [];
      bucket.push(diagnostic);
      grouped.set(key, bucket);
    }

    for (const [key, diagnostics] of grouped) {
      this.diagnostics.set(vscode.Uri.parse(key), diagnostics);
    }
  }

  private async resolveCliPath(workspaceRoot: string, cliOverride?: string): Promise<string> {
    const configuredCli = cliOverride || this.getConfiguration().get<string>('cli.path')?.trim();
    const configuredPath = resolveConfiguredCliPath(workspaceRoot, configuredCli);
    if (configuredCli) {
      if (configuredPath && await fileExists(configuredPath)) {
        return configuredPath;
      }
      throw new Error(
        `Configured ngin CLI not found: ${configuredPath ?? configuredCli}\n` +
        'Build it with:\n' +
        'cmake --preset dev\n' +
        'cmake --build build/dev --target ngin_cli'
      );
    }

    const developmentPath = getDevelopmentCliPath(workspaceRoot, process.platform);
    if (await fileExists(developmentPath)) {
      return developmentPath;
    }

    const pathExecutable = await findExecutableOnPath('ngin');
    if (pathExecutable) {
      return pathExecutable;
    }

    throw new Error(
      'Unable to find `ngin`.\n' +
      'Build the in-repo CLI with:\n' +
      'cmake --preset dev\n' +
      'cmake --build build/dev --target ngin_cli'
    );
  }

  private async warnIfCliStale(cliPath: string, workspaceRoot: string): Promise<void> {
    if (this.staleWarningShown) {
      return;
    }

    if (!this.getConfiguration().get<boolean>('cli.warnIfStale')) {
      return;
    }

    const developmentPath = getDevelopmentCliPath(workspaceRoot, process.platform);
    if (comparablePath(cliPath) !== comparablePath(developmentPath)) {
      return;
    }

    if (await isCliStale(cliPath, workspaceRoot)) {
      this.staleWarningShown = true;
      void vscode.window.showWarningMessage(
        'The in-repo NGIN CLI looks older than the CLI source files. Rebuild it with `cmake --build build/dev --target ngin_cli` if commands behave unexpectedly.'
      );
    }
  }

  private buildTaskArguments(workspaceInfo: ResolvedWorkspaceInfo | undefined, definition: NginTaskDefinition): string[] {
    if (definition.command === 'workspaceStatus') {
      return ['workspace', 'status'];
    }
    if (definition.command === 'workspaceDoctor') {
      return ['workspace', 'doctor'];
    }

    const args: string[] = [definition.command];
    if (definition.project) {
      args.push('--project', definition.project);
    }

    const resolvedProject = definition.project && workspaceInfo
      ? workspaceInfo.projects.find((project) => comparablePath(project.path) === comparablePath(definition.project))
      : undefined;
    const configuration = definition.configuration || resolvedProject?.defaultConfiguration;
    if (configuration) {
      args.push('--configuration', configuration);
    }

    if ((definition.command === 'build' || definition.command === 'clean' || definition.command === 'rebuild') && definition.output) {
      args.push('--output', definition.output);
    }

    return args;
  }

  private getTaskLabel(definition: NginTaskDefinition): string {
    if (definition.command === 'workspaceStatus') {
      return 'NGIN: Workspace Status';
    }
    if (definition.command === 'workspaceDoctor') {
      return 'NGIN: Workspace Doctor';
    }

    const command = definition.command.charAt(0).toUpperCase() + definition.command.slice(1);
    if (definition.project && definition.configuration) {
      return `NGIN: ${command} ${path.basename(definition.project, path.extname(definition.project))} [${definition.configuration}]`;
    }
    return `NGIN: ${command}`;
  }
}

class NginTaskProvider implements vscode.TaskProvider {
  constructor(private readonly controller: NginController) {}

  async provideTasks(): Promise<vscode.Task[]> {
    const contexts = await this.controller.getTaskContexts();
    const tasks: vscode.Task[] = [];

    for (const context of contexts) {
      const scopedFolder = context.workspace.folder;
      const configuredOutputRoot = this.controller.getConfiguredBuildOutputRoot(scopedFolder);
      const outputDir = configuredOutputRoot
        ? path.isAbsolute(configuredOutputRoot)
          ? path.join(configuredOutputRoot, context.project.name, context.configuration.name)
          : path.join(context.workspace.root, configuredOutputRoot, context.project.name, context.configuration.name)
        : undefined;

      tasks.push(await this.controller.createTask({
        type: 'ngin',
        command: 'build',
        project: context.project.path,
        configuration: context.configuration.name,
        output: outputDir
      }, scopedFolder));
      tasks.push(await this.controller.createTask({
        type: 'ngin',
        command: 'rebuild',
        project: context.project.path,
        configuration: context.configuration.name,
        output: outputDir
      }, scopedFolder));
      tasks.push(await this.controller.createTask({
        type: 'ngin',
        command: 'clean',
        project: context.project.path,
        configuration: context.configuration.name,
        output: outputDir
      }, scopedFolder));
      tasks.push(await this.controller.createTask({
        type: 'ngin',
        command: 'validate',
        project: context.project.path,
        configuration: context.configuration.name
      }, scopedFolder));
      tasks.push(await this.controller.createTask({
        type: 'ngin',
        command: 'graph',
        project: context.project.path,
        configuration: context.configuration.name
      }, scopedFolder));
      tasks.push(await this.controller.createTask({
        type: 'ngin',
        command: 'metagen',
        project: context.project.path,
        configuration: context.configuration.name
      }, scopedFolder));
    }

    return tasks;
  }

  async resolveTask(task: vscode.Task): Promise<vscode.Task | undefined> {
    const scope = task.scope && typeof task.scope !== 'number' ? task.scope : undefined;
    return this.controller.createTask(task.definition as NginTaskDefinition, scope);
  }
}

class NginDebugConfigurationProvider implements vscode.DebugConfigurationProvider {
  constructor(private readonly controller: NginController) {}

  async provideDebugConfigurations(folder: vscode.WorkspaceFolder | undefined): Promise<vscode.DebugConfiguration[]> {
    return this.controller.provideDebugConfigurations(folder);
  }

  async resolveDebugConfiguration(
    folder: vscode.WorkspaceFolder | undefined,
    configuration: NginDebugConfiguration
  ): Promise<vscode.DebugConfiguration | undefined> {
    return this.controller.resolveDebugConfiguration(folder, configuration);
  }
}

export function activate(context: vscode.ExtensionContext): void {
  const controller = new NginController(context);
  context.subscriptions.push(...controller.register(), controller);
  void controller.initialize();
}

export function deactivate(): void {}
