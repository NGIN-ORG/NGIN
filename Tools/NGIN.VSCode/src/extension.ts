import * as path from 'node:path';
import { promises as fs } from 'node:fs';
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
  findNearestWorkspaceManifest,
  loadProjectManifest,
  loadWorkspaceProjects,
  pathExists,
  readTextFile
} from './core/discovery';
import {
  computeOutputDir,
  computeTargetManifestPath,
  getExecutableCandidatePaths,
  getWorkingDirectoryCandidates,
  parseCliDiagnostics
} from './core/helpers';
import { createNativeDebugConfiguration, quoteShellArgument } from './core/debug';
import { ProjectManifest, ProjectVariant, TargetManifest, WorkspaceManifest } from './core/types';
import { parseTargetManifest } from './core/xml';

const LAST_PROJECT_KEY = 'ngin.lastProject';
const LAST_TARGET_MANIFEST_KEY = 'ngin.lastTargetManifest';
const LAST_VARIANT_PREFIX = 'ngin.lastVariant:';
const SUPPORTED_LANGUAGE_ID = 'ngin';

interface ResolvedWorkspaceInfo {
  workspace: WorkspaceManifest;
  projects: ProjectManifest[];
  root: string;
  folder?: vscode.WorkspaceFolder;
}

interface ResolvedCommandContext {
  workspace: ResolvedWorkspaceInfo;
  project: ProjectManifest;
  variant: ProjectVariant;
}

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
  output?: string;
}

interface NginDebugConfiguration extends vscode.DebugConfiguration {
  project?: string;
  variant?: string;
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

function workspaceVariantKey(projectPath: string): string {
  return `${LAST_VARIANT_PREFIX}${comparablePath(projectPath)}`;
}

class NginController {
  private readonly outputChannel = vscode.window.createOutputChannel('NGIN');
  private readonly diagnostics = vscode.languages.createDiagnosticCollection('ngin');
  private staleWarningShown = false;

  constructor(private readonly context: vscode.ExtensionContext) {}

  dispose(): void {
    this.outputChannel.dispose();
    this.diagnostics.dispose();
  }

  register(): vscode.Disposable[] {
    return [
      this.outputChannel,
      this.diagnostics,
      vscode.commands.registerCommand('ngin.selectProject', () => this.runHandled(() => this.selectProjectCommand())),
      vscode.commands.registerCommand('ngin.selectVariant', () => this.runHandled(() => this.selectVariantCommand())),
      vscode.commands.registerCommand('ngin.build', () => this.runHandled(() => this.buildCommand())),
      vscode.commands.registerCommand('ngin.run', () => this.runHandled(() => this.runCommand())),
      vscode.commands.registerCommand('ngin.debug', () => this.runHandled(() => this.debugCommand())),
      vscode.commands.registerCommand('ngin.validate', () => this.runHandled(() => this.validateCommand())),
      vscode.commands.registerCommand('ngin.graph', () => this.runHandled(() => this.graphCommand())),
      vscode.commands.registerCommand('ngin.workspaceStatus', () => this.runHandled(() => this.workspaceCommand('status'))),
      vscode.commands.registerCommand('ngin.workspaceDoctor', () => this.runHandled(() => this.workspaceCommand('doctor'))),
      vscode.commands.registerCommand('ngin.openLastTargetManifest', () => this.runHandled(() => this.openLastTargetManifest())),
      vscode.workspace.onDidSaveTextDocument((document) => this.handleDocumentSaved(document)),
      vscode.tasks.registerTaskProvider('ngin', new NginTaskProvider(this)),
      vscode.debug.registerDebugConfigurationProvider('ngin', new NginDebugConfigurationProvider(this))
    ];
  }

  async getWorkspaceInfo(preferredUri?: vscode.Uri): Promise<ResolvedWorkspaceInfo | undefined> {
    const candidatePaths: string[] = [];

    if (preferredUri?.scheme === 'file') {
      candidatePaths.push(preferredUri.fsPath);
    }

    const activeUri = vscode.window.activeTextEditor?.document.uri;
    if (activeUri?.scheme === 'file' && !candidatePaths.includes(activeUri.fsPath)) {
      candidatePaths.push(activeUri.fsPath);
    }

    for (const folder of vscode.workspace.workspaceFolders ?? []) {
      if (!candidatePaths.includes(folder.uri.fsPath)) {
        candidatePaths.push(folder.uri.fsPath);
      }
    }

    for (const candidate of candidatePaths) {
      const manifestPath = await findNearestWorkspaceManifest(candidate);
      if (!manifestPath) {
        continue;
      }

      const { workspace, projects } = await loadWorkspaceProjects(manifestPath);
      const root = path.dirname(manifestPath);
      return {
        workspace,
        projects,
        root,
        folder: vscode.workspace.getWorkspaceFolder(vscode.Uri.file(root))
      };
    }

    return undefined;
  }

  async resolveCommandContext(options?: {
    preferredUri?: vscode.Uri;
    explicitProjectPath?: string;
    explicitVariant?: string;
    promptIfNeeded?: boolean;
  }): Promise<ResolvedCommandContext | undefined> {
    const promptIfNeeded = options?.promptIfNeeded ?? true;
    const workspaceInfo = await this.getWorkspaceInfo(options?.preferredUri);
    if (!workspaceInfo) {
      void vscode.window.showErrorMessage('NGIN workspace not found. Open a folder with a .ngin workspace manifest.');
      return undefined;
    }

    const project = await this.resolveProject(workspaceInfo, options?.explicitProjectPath, options?.preferredUri, promptIfNeeded);
    if (!project) {
      return undefined;
    }

    const variant = await this.resolveVariant(project, options?.explicitVariant, promptIfNeeded);
    if (!variant) {
      return undefined;
    }

    await this.context.workspaceState.update(LAST_PROJECT_KEY, project.path);
    await this.context.workspaceState.update(workspaceVariantKey(project.path), variant.name);

    return {
      workspace: workspaceInfo,
      project,
      variant
    };
  }

  async getTaskContexts(): Promise<ResolvedCommandContext[]> {
    const workspaceInfo = await this.getWorkspaceInfo();
    if (!workspaceInfo) {
      return [];
    }

    const contexts: ResolvedCommandContext[] = [];
    for (const project of workspaceInfo.projects) {
      for (const variant of project.variants) {
        contexts.push({ workspace: workspaceInfo, project, variant });
      }
    }
    return contexts;
  }

  getConfiguredBuildOutputRoot(scope?: vscode.WorkspaceFolder): string | undefined {
    return this.getConfiguration(scope).get<string>('build.outputRoot')?.trim() || undefined;
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

  async resolveDebugConfiguration(folder: vscode.WorkspaceFolder | undefined, configuration: NginDebugConfiguration): Promise<vscode.DebugConfiguration | undefined> {
    const context = await this.resolveCommandContext({
      preferredUri: folder?.uri,
      explicitProjectPath: configuration.project,
      explicitVariant: configuration.variant,
      promptIfNeeded: false
    });

    if (!context) {
      void vscode.window.showErrorMessage('Unable to resolve the NGIN project and variant for debugging.');
      return undefined;
    }

    const cpptools = vscode.extensions.getExtension('ms-vscode.cpptools');
    if (!cpptools) {
      void vscode.window.showErrorMessage('NGIN debugging requires the Microsoft C/C++ extension (`ms-vscode.cpptools`).');
      return undefined;
    }

    const buildResult = configuration.preBuild === false
      ? {
          outputDir: this.computeOutputDirectory(context, configuration.outputDir),
          targetManifestPath: computeTargetManifestPath(
            this.computeOutputDirectory(context, configuration.outputDir),
            context.project.name,
            context.variant.name
          )
        }
      : await this.buildProject(context, { cliOverride: configuration.cliPath, outputDirOverride: configuration.outputDir });

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
    const workspaceInfo = await this.getWorkspaceInfo(folder?.uri);
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
        preBuild: true,
        programArgs: [],
        env: {}
      });
    }

    return configurations;
  }

  async createTask(definition: NginTaskDefinition, scope?: vscode.WorkspaceFolder): Promise<vscode.Task> {
    const workspaceInfo = await this.getWorkspaceInfo(scope?.uri);
    const workspaceRoot = workspaceInfo?.root ?? scope?.uri.fsPath ?? process.cwd();
    const cliCommand = await this.getCliCommandHint(workspaceRoot);
    const args = this.buildTaskArguments(workspaceInfo, definition);
    const execution = new vscode.ProcessExecution(cliCommand, args, { cwd: workspaceRoot });
    const task = new vscode.Task(definition, scope ?? vscode.TaskScope.Workspace, this.getTaskLabel(definition), 'ngin', execution, ['$ngin-file']);

    if (definition.command === 'build') {
      task.group = vscode.TaskGroup.Build;
    }

    return task;
  }

  async getUserFacingProjectChoice(workspaceInfo: ResolvedWorkspaceInfo): Promise<ProjectManifest | undefined> {
    const picked = await vscode.window.showQuickPick(
      workspaceInfo.projects.map((project) => ({
        label: project.name,
        description: path.relative(workspaceInfo.root, project.path),
        project
      })),
      {
        title: 'Select NGIN project'
      }
    );

    if (!picked) {
      return undefined;
    }

    await this.context.workspaceState.update(LAST_PROJECT_KEY, picked.project.path);
    return picked.project;
  }

  async getUserFacingVariantChoice(project: ProjectManifest): Promise<ProjectVariant | undefined> {
    const picked = await vscode.window.showQuickPick(
      project.variants.map((variant) => ({
        label: variant.name,
        description: variant.profile ?? '',
        variant
      })),
      {
        title: `Select variant for ${project.name}`
      }
    );

    if (!picked) {
      return undefined;
    }

    await this.context.workspaceState.update(workspaceVariantKey(project.path), picked.variant.name);
    return picked.variant;
  }

  private getConfiguration(scope?: vscode.WorkspaceFolder): vscode.WorkspaceConfiguration {
    return vscode.workspace.getConfiguration('ngin', scope);
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

  private async resolveProject(
    workspaceInfo: ResolvedWorkspaceInfo,
    explicitProjectPath?: string,
    preferredUri?: vscode.Uri,
    promptIfNeeded = true
  ): Promise<ProjectManifest | undefined> {
    if (explicitProjectPath) {
      const projectPath = path.isAbsolute(explicitProjectPath)
        ? explicitProjectPath
        : path.resolve(workspaceInfo.root, explicitProjectPath);
      return workspaceInfo.projects.find((project) => comparablePath(project.path) === comparablePath(projectPath));
    }

    const activeDocument = preferredUri?.scheme === 'file'
      ? preferredUri.fsPath
      : vscode.window.activeTextEditor?.document.uri.scheme === 'file'
        ? vscode.window.activeTextEditor.document.uri.fsPath
        : undefined;

    if (activeDocument) {
      const matchedProject = workspaceInfo.projects.find((project) => {
        const projectDir = comparablePath(project.directory) + path.sep;
        const documentPath = comparablePath(activeDocument);
        return documentPath === comparablePath(project.path) || documentPath.startsWith(projectDir);
      });
      if (matchedProject) {
        return matchedProject;
      }
    }

    if (workspaceInfo.projects.length === 1) {
      return workspaceInfo.projects[0];
    }

    const lastProjectPath = this.context.workspaceState.get<string>(LAST_PROJECT_KEY);
    if (lastProjectPath) {
      const lastProject = workspaceInfo.projects.find((project) => comparablePath(project.path) === comparablePath(lastProjectPath));
      if (lastProject) {
        return lastProject;
      }
    }

    if (!promptIfNeeded) {
      return undefined;
    }

    return this.getUserFacingProjectChoice(workspaceInfo);
  }

  private async resolveVariant(project: ProjectManifest, explicitVariant?: string, promptIfNeeded = true): Promise<ProjectVariant | undefined> {
    if (explicitVariant) {
      return project.variants.find((variant) => variant.name === explicitVariant);
    }

    const storedVariant = this.context.workspaceState.get<string>(workspaceVariantKey(project.path));
    if (storedVariant) {
      const matched = project.variants.find((variant) => variant.name === storedVariant);
      if (matched) {
        return matched;
      }
    }

    if (project.defaultVariant) {
      const matched = project.variants.find((variant) => variant.name === project.defaultVariant);
      if (matched) {
        return matched;
      }
    }

    if (project.variants.length === 1) {
      return project.variants[0];
    }

    if (!promptIfNeeded) {
      return undefined;
    }

    return this.getUserFacingVariantChoice(project);
  }

  private async selectProjectCommand(): Promise<void> {
    const workspaceInfo = await this.getWorkspaceInfo();
    if (!workspaceInfo) {
      void vscode.window.showErrorMessage('NGIN workspace not found.');
      return;
    }

    const project = await this.getUserFacingProjectChoice(workspaceInfo);
    if (!project) {
      return;
    }

    void vscode.window.showInformationMessage(`Selected NGIN project: ${project.name}`);
  }

  private async selectVariantCommand(): Promise<void> {
    const workspaceInfo = await this.getWorkspaceInfo();
    if (!workspaceInfo) {
      void vscode.window.showErrorMessage('NGIN workspace not found.');
      return;
    }

    const project = await this.resolveProject(workspaceInfo, undefined, undefined, true);
    if (!project) {
      return;
    }

    const variant = await this.getUserFacingVariantChoice(project);
    if (!variant) {
      return;
    }

    void vscode.window.showInformationMessage(`Selected NGIN variant: ${project.name} [${variant.name}]`);
  }

  private async buildCommand(): Promise<void> {
    const context = await this.resolveCommandContext();
    if (!context) {
      return;
    }

    await this.buildProject(context);
  }

  private async validateCommand(options?: { preferredUri?: vscode.Uri; silent?: boolean }): Promise<void> {
    const context = await this.resolveCommandContext({
      preferredUri: options?.preferredUri,
      promptIfNeeded: !options?.silent
    });
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

  private async graphCommand(): Promise<void> {
    const context = await this.resolveCommandContext();
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
    const workspaceInfo = await this.getWorkspaceInfo();
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

  private async runCommand(): Promise<void> {
    const context = await this.resolveCommandContext();
    if (!context) {
      return;
    }

    const buildResult = await this.buildProject(context);
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

  private async debugCommand(): Promise<void> {
    const context = await this.resolveCommandContext();
    if (!context) {
      return;
    }

    const configuration: NginDebugConfiguration = {
      type: 'ngin',
      request: 'launch',
      name: `NGIN: Debug ${context.project.name} [${context.variant.name}]`,
      project: context.project.path,
      variant: context.variant.name,
      preBuild: true,
      programArgs: [],
      env: {}
    };

    await vscode.debug.startDebugging(context.workspace.folder, configuration);
  }

  private async openLastTargetManifest(): Promise<void> {
    const manifestPath = this.context.workspaceState.get<string>(LAST_TARGET_MANIFEST_KEY);
    if (!manifestPath) {
      void vscode.window.showErrorMessage('No NGIN target manifest has been recorded yet.');
      return;
    }

    if (!(await pathExists(manifestPath))) {
      void vscode.window.showErrorMessage(`NGIN target manifest not found: ${manifestPath}`);
      return;
    }

    const document = await vscode.workspace.openTextDocument(vscode.Uri.file(manifestPath));
    await vscode.window.showTextDocument(document, { preview: false });
  }

  private async handleDocumentSaved(document: vscode.TextDocument): Promise<void> {
    if (document.languageId !== SUPPORTED_LANGUAGE_ID) {
      return;
    }

    if (!this.getConfiguration(vscode.workspace.getWorkspaceFolder(document.uri)).get<boolean>('validate.onSave')) {
      return;
    }

    try {
      await this.validateCommand({ preferredUri: document.uri, silent: true });
    } catch {
      // Silent validate-on-save still updates diagnostics through CLI output parsing.
    }
  }

  private computeOutputDirectory(context: ResolvedCommandContext, override?: string): string {
    if (override) {
      return path.isAbsolute(override) ? override : path.resolve(context.workspace.root, override);
    }

    return computeOutputDir(
      context.workspace.root,
      context.project.name,
      context.variant.name,
      this.getConfiguredBuildOutputRoot(context.workspace.folder)
    );
  }

  private async buildProject(
    context: ResolvedCommandContext,
    options?: { cliOverride?: string; outputDirOverride?: string }
  ): Promise<BuildResult> {
    const outputDir = this.computeOutputDirectory(context, options?.outputDirOverride);
    const args = ['project', 'build', '--project', context.project.path, '--variant', context.variant.name];
    if (options?.outputDirOverride || this.getConfiguredBuildOutputRoot(context.workspace.folder)) {
      args.push('--output', outputDir);
    }

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

    await this.context.workspaceState.update(LAST_TARGET_MANIFEST_KEY, targetManifestPath);
    return {
      outputDir,
      targetManifestPath
    };
  }

  private async readTargetManifest(targetManifestPath: string): Promise<TargetManifest> {
    if (!(await pathExists(targetManifestPath))) {
      throw new Error(`Target manifest not found: ${targetManifestPath}`);
    }

    const xml = await readTextFile(targetManifestPath);
    return parseTargetManifest(xml, targetManifestPath);
  }

  private async resolveLaunchTarget(targetManifest: TargetManifest, projectDir: string, outputDir: string): Promise<{
    program: string;
    cwd: string;
  }> {
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
      ? workspaceInfo.projects.find((project) => comparablePath(project.path) === comparablePath(definition.project!))
      : undefined;
    const variant = definition.variant || resolvedProject?.defaultVariant;
    if (variant) {
      args.push('--variant', variant);
    }

    if (definition.command === 'build' && definition.output) {
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
    if (definition.project && definition.variant) {
      return `NGIN: ${command} ${path.basename(definition.project, path.extname(definition.project))} [${definition.variant}]`;
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
        ? computeOutputDir(context.workspace.root, context.project.name, context.variant.name, configuredOutputRoot)
        : undefined;
      tasks.push(await this.controller.createTask({
        type: 'ngin',
        command: 'build',
        project: context.project.path,
        variant: context.variant.name,
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
}

export function deactivate(): void {}
