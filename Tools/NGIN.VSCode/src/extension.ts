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
import {
  pathExists,
  readTextFile
} from './core/discovery';
import {
  NGIN_BUILD_CONFIGURATIONS,
  normalizeBuildConfiguration
} from './core/buildConfiguration';
import {
  computeOutputDir,
  computeTargetManifestPath,
  getExecutableCandidatePaths,
  getWorkingDirectoryCandidates,
  parseCliDiagnostics
} from './core/helpers';
import { createNativeDebugConfiguration, quoteShellArgument } from './core/debug';
import { TargetManifest } from './core/types';
import { parseTargetManifest } from './core/xml';
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
  targetManifestPath: string;
}

interface CliRunResult {
  exitCode: number;
  output: string;
}

interface NginTaskDefinition extends vscode.TaskDefinition {
  command: 'build' | 'validate' | 'graph' | 'workspaceStatus' | 'workspaceDoctor';
  project?: string;
  variant?: string;
  configuration?: string;
  output?: string;
}

interface NginDebugConfiguration extends vscode.DebugConfiguration {
  project?: string;
  variant?: string;
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
      vscode.commands.registerCommand('ngin.selectVariant', (arg) => this.runHandled(() => this.selectVariantCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.selectConfiguration', () => this.runHandled(() => this.selectConfigurationCommand())),
      vscode.commands.registerCommand('ngin.build', (arg) => this.runHandled(() => this.buildCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.run', (arg) => this.runHandled(() => this.runCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.debug', (arg) => this.runHandled(() => this.debugCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.validate', (arg) => this.runHandled(() => this.validateCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.graph', (arg) => this.runHandled(() => this.graphCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.workspaceStatus', () => this.runHandled(() => this.workspaceCommand('status'))),
      vscode.commands.registerCommand('ngin.workspaceDoctor', () => this.runHandled(() => this.workspaceCommand('doctor'))),
      vscode.commands.registerCommand('ngin.openLastTargetManifest', () => this.runHandled(() => this.openLastTargetManifest())),
      vscode.commands.registerCommand('ngin.refresh', () => this.runHandled(() => this.refreshUi())),
      vscode.commands.registerCommand('ngin.internal.openPath', (filePath) => this.runHandled(() => this.openPathCommand(filePath))),
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

  getSelectedBuildConfiguration(): string {
    return this.workspaceState.getSelectedBuildConfiguration();
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
      variantName: configuration.variant
    }, false);

    if (!context) {
      void vscode.window.showErrorMessage('Unable to resolve the NGIN project and variant for debugging.');
      return undefined;
    }

    const cpptools = vscode.extensions.getExtension('ms-vscode.cpptools');
    if (!cpptools) {
      void vscode.window.showErrorMessage('NGIN debugging requires the Microsoft C/C++ extension (`ms-vscode.cpptools`).');
      return undefined;
    }

    const buildResult = await this.getLaunchBuildResult(context, {
      configurationOverride: configuration.configuration,
      cliOverride: configuration.cliPath,
      outputDirOverride: configuration.outputDir,
      forceBuild: configuration.preBuild === true
    });

    const targetManifest = await this.readTargetManifest(buildResult.targetManifestPath);
    const launch = await this.resolveLaunchTarget(targetManifest, context.project.directory, buildResult.outputDir);

    const native = createNativeDebugConfiguration({
      platform: process.platform,
      program: launch.program,
      cwd: launch.cwd,
      args: configuration.programArgs ?? [],
      env: configuration.env ?? {},
      miDebuggerPath: this.getConfiguration(context.workspace.folder).get<string>('debug.miDebuggerPath')?.trim() || undefined
    });

    native.name = configuration.name ?? `NGIN: Debug ${context.project.name} [${context.variant.name}]`;
    return native as vscode.DebugConfiguration;
  }

  async provideDebugConfigurations(folder: vscode.WorkspaceFolder | undefined): Promise<vscode.DebugConfiguration[]> {
    const workspaceInfo = await this.workspaceState.getWorkspaceInfo(folder?.uri);
    if (!workspaceInfo) {
      return [];
    }

    const configurations: vscode.DebugConfiguration[] = [];
    for (const project of workspaceInfo.projects) {
      const variantName = project.defaultVariant ?? project.variants[0]?.name;
      if (!variantName) {
        continue;
      }

      configurations.push({
        type: 'ngin',
        request: 'launch',
        name: `NGIN: Debug ${project.name} [${variantName}]`,
        project: project.path,
        variant: variantName,
        configuration: this.workspaceState.getSelectedBuildConfiguration(),
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

    return task;
  }

  private getConfiguration(scope?: vscode.WorkspaceFolder): vscode.WorkspaceConfiguration {
    return vscode.workspace.getConfiguration('ngin', scope);
  }

  private asCommandTarget(value: unknown): NginCommandTarget | undefined {
    if (!value || typeof value !== 'object') {
      return undefined;
    }

    const candidate = value as {
      preferredUri?: vscode.Uri;
      projectPath?: string;
      variantName?: string;
    };

    if (!candidate.preferredUri && !candidate.projectPath && !candidate.variantName) {
      return undefined;
    }

    return {
      preferredUri: candidate.preferredUri,
      projectPath: candidate.projectPath,
      variantName: candidate.variantName
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
      explicitVariant: target?.variantName,
      promptIfNeeded
    });

    if (context) {
      return context;
    }

    const workspaceInfo = await this.workspaceState.getWorkspaceInfo(target?.preferredUri);
    if (!workspaceInfo) {
      void vscode.window.showErrorMessage('NGIN workspace not found. Open a folder with a .ngin workspace manifest.');
    } else if (target?.projectPath || target?.variantName) {
      void vscode.window.showErrorMessage('Unable to resolve the selected NGIN project or variant.');
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

  private async selectVariantCommand(target?: NginCommandTarget): Promise<void> {
    if (target?.projectPath && target?.variantName) {
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

    const variant = await this.workspaceState.pickVariant(project);
    if (!variant) {
      return;
    }

    await this.workspaceState.rememberSelection({ workspace: workspaceInfo, project, variant });
    await this.refreshUi(target?.preferredUri);
    void vscode.window.showInformationMessage(`Selected NGIN variant: ${project.name} [${variant.name}]`);
  }

  private async selectConfigurationCommand(): Promise<void> {
    const workspaceInfo = await this.workspaceState.getWorkspaceInfo();
    if (!workspaceInfo) {
      void vscode.window.showErrorMessage('NGIN workspace not found.');
      return;
    }

    const current = this.workspaceState.getSelectedBuildConfiguration();
    const picked = await vscode.window.showQuickPick(
      NGIN_BUILD_CONFIGURATIONS.map((configuration) => ({
        label: configuration.name,
        description: configuration.name === current ? 'Current' : configuration.description
      })),
      {
        title: 'Select NGIN build configuration'
      }
    );
    if (!picked) {
      return;
    }

    await this.workspaceState.setSelectedBuildConfiguration(picked.label);
    await this.refreshUi(workspaceInfo.folder?.uri);
    void vscode.window.showInformationMessage(`Selected NGIN build configuration: ${picked.label}`);
  }

  private async buildCommand(target?: NginCommandTarget): Promise<void> {
    const context = await this.resolveCommandContext(target);
    if (!context) {
      return;
    }

    await this.buildProject(context);
  }

  private async validateCommand(target?: NginCommandTarget, options?: { silent?: boolean }): Promise<void> {
    const context = await this.resolveCommandContext(target, !options?.silent);
    if (!context) {
      return;
    }

    const result = await this.runCli(
      context.workspace.root,
      ['project', 'validate', '--project', context.project.path, '--variant', context.variant.name],
      vscode.Uri.file(context.project.path)
    );

    if (result.exitCode !== 0) {
      if (options?.silent) {
        return;
      }
      throw new Error(`ngin validate failed for ${context.project.name} [${context.variant.name}]`);
    }

    if (!options?.silent) {
      void vscode.window.showInformationMessage(`Validated ${context.project.name} [${context.variant.name}]`);
    }
  }

  private async graphCommand(target?: NginCommandTarget): Promise<void> {
    const context = await this.resolveCommandContext(target);
    if (!context) {
      return;
    }

    const result = await this.runCli(
      context.workspace.root,
      ['project', 'graph', '--project', context.project.path, '--variant', context.variant.name],
      vscode.Uri.file(context.project.path)
    );

    if (result.exitCode !== 0) {
      throw new Error(`ngin graph failed for ${context.project.name} [${context.variant.name}]`);
    }
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
    const targetManifest = await this.readTargetManifest(buildResult.targetManifestPath);
    const launch = await this.resolveLaunchTarget(targetManifest, context.project.directory, buildResult.outputDir);

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
      name: `NGIN: Debug ${context.project.name} [${context.variant.name}]`,
      project: context.project.path,
      variant: context.variant.name,
      configuration: this.workspaceState.getSelectedBuildConfiguration(),
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

  private async openLastTargetManifest(): Promise<void> {
    const manifestPath = this.workspaceState.getLastTargetManifestPath();
    if (!manifestPath) {
      void vscode.window.showErrorMessage('No NGIN target manifest has been recorded yet.');
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
    options?: { cliOverride?: string; outputDirOverride?: string; configurationOverride?: string }
  ): Promise<BuildResult> {
    const buildConfiguration = this.resolveBuildConfiguration(options?.configurationOverride);
    const outputDir = this.workspaceState.computeOutputDirectory(context, options?.outputDirOverride, buildConfiguration);
    const args = [
      'project',
      'build',
      '--project',
      context.project.path,
      '--variant',
      context.variant.name,
      '--configuration',
      buildConfiguration,
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
      throw new Error(`ngin build failed for ${context.project.name} [${context.variant.name}]`);
    }

    const targetManifestPath = computeTargetManifestPath(outputDir, context.project.name, context.variant.name);
    if (!(await pathExists(targetManifestPath))) {
      throw new Error(`Expected staged target manifest was not produced: ${targetManifestPath}`);
    }

    await this.workspaceState.setLastTargetManifestPath(targetManifestPath);
    return {
      outputDir,
      targetManifestPath
    };
  }

  private async getLaunchBuildResult(
    context: ResolvedCommandContext,
    options?: { cliOverride?: string; outputDirOverride?: string; configurationOverride?: string; forceBuild?: boolean }
  ): Promise<BuildResult> {
    const buildConfiguration = this.resolveBuildConfiguration(options?.configurationOverride);
    const outputDir = this.workspaceState.computeOutputDirectory(context, options?.outputDirOverride, buildConfiguration);
    const targetManifestPath = computeTargetManifestPath(outputDir, context.project.name, context.variant.name);

    if (!options?.forceBuild && await pathExists(targetManifestPath)) {
      try {
        const targetManifest = await this.readTargetManifest(targetManifestPath);
        await this.resolveLaunchTarget(targetManifest, context.project.directory, outputDir);
        await this.workspaceState.setLastTargetManifestPath(targetManifestPath);
        return {
          outputDir,
          targetManifestPath
        };
      } catch {
        // Fall back to a rebuild when the staged output is incomplete or stale.
      }
    }

    return this.buildProject(context, options);
  }

  private async readTargetManifest(targetManifestPath: string): Promise<TargetManifest> {
    if (!(await pathExists(targetManifestPath))) {
      throw new Error(`Target manifest not found: ${targetManifestPath}`);
    }

    const xml = await readTextFile(targetManifestPath);
    return parseTargetManifest(xml, targetManifestPath);
  }

  private async resolveLaunchTarget(
    targetManifest: TargetManifest,
    projectDir: string,
    outputDir: string
  ): Promise<{ program: string; cwd: string }> {
    const executableCandidates = getExecutableCandidatePaths(targetManifest, outputDir, process.platform);
    let program: string | undefined;
    for (const candidate of executableCandidates) {
      if (await fileExists(candidate)) {
        program = candidate;
        break;
      }
    }

    if (!program) {
      throw new Error(`Unable to resolve a staged executable from ${targetManifest.path}`);
    }

    const cwdCandidates = getWorkingDirectoryCandidates(targetManifest, outputDir, projectDir);
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
      let exitCode = 0;
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
        exitCode = code ?? 0;
        if (exitCode !== 0) {
          combined += `\nerror: command exited with code ${exitCode}\n`;
        }
        resolve({ exitCode, output: combined });
      });
    });

    this.applyDiagnostics(result.output, diagnosticsResource);
    return result;
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

  private resolveBuildConfiguration(explicitConfiguration?: string): string {
    return normalizeBuildConfiguration(explicitConfiguration ?? this.workspaceState.getSelectedBuildConfiguration());
  }

  private buildTaskArguments(workspaceInfo: ResolvedWorkspaceInfo | undefined, definition: NginTaskDefinition): string[] {
    if (definition.command === 'workspaceStatus') {
      return ['workspace', 'status'];
    }
    if (definition.command === 'workspaceDoctor') {
      return ['workspace', 'doctor'];
    }

    const args = ['project', definition.command];
    if (definition.project) {
      args.push('--project', definition.project);
    }

    const resolvedProject = definition.project && workspaceInfo
      ? workspaceInfo.projects.find((project) => comparablePath(project.path) === comparablePath(definition.project))
      : undefined;
    const variant = definition.variant || resolvedProject?.defaultVariant;
    if (variant) {
      args.push('--variant', variant);
    }

    if (definition.command === 'build') {
      args.push('--configuration', this.resolveBuildConfiguration(definition.configuration));
      if (definition.output) {
        args.push('--output', definition.output);
      }
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
    if (definition.project && definition.variant) {
      const configurationSuffix = definition.command === 'build' && definition.configuration
        ? ` ${definition.configuration}`
        : '';
      return `NGIN: ${command} ${path.basename(definition.project, path.extname(definition.project))} [${definition.variant}]${configurationSuffix}`;
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
      const buildConfiguration = this.controller.getSelectedBuildConfiguration();
      const outputDir = configuredOutputRoot
        ? computeOutputDir(context.workspace.root, context.project.name, context.variant.name, configuredOutputRoot, buildConfiguration)
        : undefined;
      tasks.push(await this.controller.createTask({
        type: 'ngin',
        command: 'build',
        project: context.project.path,
        variant: context.variant.name,
        configuration: buildConfiguration,
        output: outputDir
      }, scopedFolder));
      tasks.push(await this.controller.createTask({
        type: 'ngin',
        command: 'validate',
        project: context.project.path,
        variant: context.variant.name
      }, scopedFolder));
      tasks.push(await this.controller.createTask({
        type: 'ngin',
        command: 'graph',
        project: context.project.path,
        variant: context.variant.name
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
