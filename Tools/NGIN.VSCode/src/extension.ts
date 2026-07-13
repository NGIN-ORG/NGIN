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
  extractInitializedSettingsPath,
  extractLocalSettingsWarnings,
  getExecutableCandidatePaths,
  getWorkingDirectoryCandidates,
  parseCliDiagnostics,
  parseCompositionGraphPayload
} from './core/helpers';
import {
  diagnosticFromEvent,
  eventLabel,
  eventOutputLine,
  NginEventDiagnostic,
  NginJsonlParseError,
  NginJsonlEventParser
} from './core/events';
import { addRootConfigInput, listConfigInputs, relativeManifestPath, removeConfigInputs, renameConfigInputs } from './core/projectAuthoring';
import { createNativeDebugConfiguration, quoteShellArgument } from './core/debug';
import { LaunchManifest, CompositionGraphPayload, ProjectManifest } from './core/types';
import { parseLaunchManifest, parseLocalSettingsManifest, parseProjectManifest } from './core/xml';
import {
  NginCommandTarget,
  NginWorkspaceSnapshot,
  ResolvedCommandContext,
  ResolvedWorkspaceInfo,
  WorkspaceStateService
} from './state/workspaceState';
import { NginSidebarController } from './ui/sidebar';
import { NginStatusBarController } from './ui/statusBar';
import { NginCppToolsProviderService } from './cpptools/provider';
import { NginProjectEditorProvider } from './projectEditor/provider';

const SUPPORTED_LANGUAGE_ID = 'ngin';

interface BuildResult {
  outputDir: string;
  launchManifestPath: string;
}

interface CliRunResult {
  exitCode: number;
  output: string;
  eventDiagnostics?: NginEventDiagnostic[];
  eventCommandSucceeded?: boolean;
}

interface NginTaskDefinition extends vscode.TaskDefinition {
  command: 'configure' | 'build' | 'clean' | 'rebuild' | 'validate' | 'analyze' | 'graph' | 'workspaceStatus' | 'workspaceDoctor';
  project?: string;
  profile?: string;
  output?: string;
}

interface NginDebugConfiguration extends vscode.DebugConfiguration {
  project?: string;
  profile?: string;
  cliPath?: string;
  outputDir?: string;
  programArgs?: string[];
  env?: Record<string, string>;
  preBuild?: boolean;
}

interface ProjectExplorerTarget extends NginCommandTarget {
  fsPath?: string;
  role?: 'manifest' | 'source' | 'config' | 'generated';
  isDirectory?: boolean;
  explainIdentity?: string;
}

interface ToolDiagnosticMetadata {
  run: string;
  editSetIds: string[];
  documentVersion?: number;
}

interface ToolEditPayload {
  id: string;
  label?: string;
  applicability?: 'automatic' | 'suggested' | 'unsafe' | string;
  files?: Array<{
    path?: { absolute?: string; workspaceRelative?: string };
    expectedDigest?: string;
    edits?: Array<{
      range?: {
        start?: { line?: number; column?: number };
        end?: { line?: number; column?: number };
      };
      newText?: string;
    }>;
  }>;
}

interface ToolEditsResponse {
  kind?: string;
  results?: Array<{ edits?: ToolEditPayload[] }>;
}

function comparablePath(value: string): string {
  const normalized = path.normalize(value);
  return process.platform === 'win32' ? normalized.toLowerCase() : normalized;
}

function isPathWithinDirectory(candidate: string, directory: string): boolean {
  const relative = path.relative(directory, candidate);
  return relative === '' || Boolean(relative) && !relative.startsWith('..') && !path.isAbsolute(relative);
}

function toolContentDigest(contents: Uint8Array): string {
  let hash = 14695981039346656037n;
  const prime = 1099511628211n;
  const mask = (1n << 64n) - 1n;
  for (const value of contents) {
    hash = ((hash ^ BigInt(value)) * prime) & mask;
  }
  hash = ((hash ^ 0xffn) * prime) & mask;
  return hash.toString(16).padStart(16, '0');
}

function homeLocalSettingsPath(): string | undefined {
  const home = process.env.HOME || process.env.USERPROFILE;
  return home ? path.join(home, '.ngin', 'settings.nginsettings') : undefined;
}

function hasGitignoreEntry(text: string, entry: string): boolean {
  return text.split(/\r?\n/).some((line) => line.trim() === entry);
}

function isLocalSettingsIgnoredByGitignore(text: string): boolean {
  return hasGitignoreEntry(text, '.ngin/local/')
    || hasGitignoreEntry(text, '.ngin/local/*')
    || hasGitignoreEntry(text, '.ngin/*')
    || hasGitignoreEntry(text, '.ngin/');
}

class NginVirtualDocumentProvider implements vscode.TextDocumentContentProvider, vscode.Disposable {
  private readonly onDidChangeEmitter = new vscode.EventEmitter<vscode.Uri>();
  private readonly contents = new Map<string, string>();

  readonly onDidChange = this.onDidChangeEmitter.event;

  createDocument(title: string, content: string): vscode.Uri {
    const uri = vscode.Uri.from({
      scheme: 'ngin-variables',
      path: `/${title}.txt`,
      query: String(Date.now())
    });
    this.contents.set(uri.toString(), content);
    this.onDidChangeEmitter.fire(uri);
    return uri;
  }

  provideTextDocumentContent(uri: vscode.Uri): string {
    return this.contents.get(uri.toString()) ?? '';
  }

  dispose(): void {
    this.contents.clear();
    this.onDidChangeEmitter.dispose();
  }
}

class NginLocalSettingsCompletionProvider implements vscode.CompletionItemProvider {
  constructor(private readonly workspaceProvider: (preferredUri?: vscode.Uri) => Promise<ResolvedWorkspaceInfo | undefined>) {}

  async provideCompletionItems(document: vscode.TextDocument, position: vscode.Position): Promise<vscode.CompletionItem[] | undefined> {
    const linePrefix = document.lineAt(position).text.slice(0, position.character);
    if (/FromEnvironment\s*=\s*["'][^"']*$/.test(linePrefix)) {
      return Object.keys(process.env)
        .sort((left, right) => left.localeCompare(right))
        .map((name) => {
          const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Variable);
          item.detail = 'environment variable';
          return item;
        });
    }

    if (/FromLocalSetting\s*=\s*["'][^"']*$/.test(linePrefix)) {
      const keys = await this.collectLocalSettingKeys(document);
      return [...keys].sort((left, right) => left.localeCompare(right)).map((key) => {
        const item = new vscode.CompletionItem(key, vscode.CompletionItemKind.Property);
        item.detail = 'local setting key';
        return item;
      });
    }

    return undefined;
  }

  private async collectLocalSettingKeys(document: vscode.TextDocument): Promise<Set<string>> {
    const keys = new Set<string>();
    if (document.uri.scheme === 'file' && document.uri.fsPath.endsWith('.nginsettings')) {
      this.addLocalSettingKeys(keys, document.getText(), document.uri.fsPath);
    }

    const project = await this.resolveProjectForDocument(document);
    if (project) {
      for (const settingsPath of project.localSettingsImports ?? []) {
        await this.addLocalSettingKeysFromFile(keys, settingsPath);
      }
    }

    const globalSettings = homeLocalSettingsPath();
    if (globalSettings) {
      await this.addLocalSettingKeysFromFile(keys, globalSettings);
    }

    return keys;
  }

  private async resolveProjectForDocument(document: vscode.TextDocument): Promise<ProjectManifest | undefined> {
    if (document.uri.scheme === 'file' && document.uri.fsPath.endsWith('.nginproj')) {
      try {
        return parseProjectManifest(document.getText(), document.uri.fsPath);
      } catch {
        return undefined;
      }
    }

    const workspaceInfo = await this.workspaceProvider(document.uri);
    if (!workspaceInfo || document.uri.scheme !== 'file') {
      return undefined;
    }

    const documentPath = comparablePath(document.uri.fsPath);
    return workspaceInfo.projects.find((project) => {
      const projectPath = comparablePath(project.path);
      const projectDirectory = comparablePath(project.directory) + path.sep;
      return documentPath === projectPath || documentPath.startsWith(projectDirectory);
    });
  }

  private async addLocalSettingKeysFromFile(keys: Set<string>, settingsPath: string): Promise<void> {
    if (!(await pathExists(settingsPath))) {
      return;
    }
    try {
      this.addLocalSettingKeys(keys, await readTextFile(settingsPath), settingsPath);
    } catch {
      return;
    }
  }

  private addLocalSettingKeys(keys: Set<string>, xml: string, settingsPath: string): void {
    try {
      const manifest = parseLocalSettingsManifest(xml, settingsPath);
      for (const setting of manifest.settings) {
        keys.add(setting.key);
      }
    } catch {
      return;
    }
  }
}

class NginController implements vscode.Disposable {
  private readonly outputChannel = vscode.window.createOutputChannel('NGIN');
  private readonly validateDiagnostics = vscode.languages.createDiagnosticCollection('ngin.validate');
  private readonly analyzeDiagnostics = vscode.languages.createDiagnosticCollection('ngin.analyze');
  private readonly inspectDiagnostics = vscode.languages.createDiagnosticCollection('ngin.inspect');
  private readonly analyzeDiagnosticsByOwner = new Map<string, Map<string, vscode.Diagnostic[]>>();
  private readonly toolDiagnosticMetadata = new WeakMap<vscode.Diagnostic, ToolDiagnosticMetadata>();
  private readonly cliRunGeneration = new Map<string, number>();
  private readonly activeCliProcesses = new Map<string, ReturnType<typeof spawn>>();
  private readonly activeFileTimers = new Map<string, ReturnType<typeof setTimeout>>();
  private readonly variableDocumentProvider = new NginVirtualDocumentProvider();
  private readonly workspaceState: WorkspaceStateService;
  private readonly sidebar: NginSidebarController;
  private readonly statusBar: NginStatusBarController;
  private readonly cppToolsProvider: NginCppToolsProviderService;
  private readonly inspectCache = new Map<string, { payload?: CompositionGraphPayload; error?: string }>();
  private staleWarningShown = false;
  private readonly shownLocalSettingsWarnings = new Set<string>();

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
    for (const timer of this.activeFileTimers.values()) clearTimeout(timer);
    for (const child of this.activeCliProcesses.values()) child.kill('SIGTERM');
    this.cppToolsProvider.dispose();
    this.statusBar.dispose();
    this.sidebar.dispose();
    this.workspaceState.dispose();
    this.outputChannel.dispose();
    this.validateDiagnostics.dispose();
    this.analyzeDiagnostics.dispose();
    this.inspectDiagnostics.dispose();
    this.variableDocumentProvider.dispose();
  }

  register(): vscode.Disposable[] {
    return [
      vscode.commands.registerCommand('ngin.selectProject', (arg) => this.runHandled(() => this.selectProjectCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.selectProfile', (arg) => this.runHandled(() => this.selectProfileCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.configure', (arg) => this.runHandled(() => this.configureCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.build', (arg) => this.runHandled(() => this.buildCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.clean', (arg) => this.runHandled(() => this.cleanCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.rebuild', (arg) => this.runHandled(() => this.rebuildCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.run', (arg) => this.runHandled(() => this.runCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.debug', (arg) => this.runHandled(() => this.debugCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.validate', (arg) => this.runHandled(() => this.validateCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.analyze', (arg) => this.runHandled(() => this.analyzeCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.analyzeActiveFile', () => this.runHandled(() => this.analyzeActiveFileCommand())),
      vscode.commands.registerCommand('ngin.analyzeChangedFiles', (arg) => this.runHandled(() => this.analyzeChangedFilesCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.addToolAction', (arg) => this.runHandled(() => this.addToolActionCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.runToolRun', (arg) => this.runHandled(() => this.runToolRunCommand(this.asExplorerTarget(arg)))),
      vscode.commands.registerCommand('ngin.applyToolEdits', (uri: vscode.Uri, metadata: ToolDiagnosticMetadata) => this.runHandled(() => this.applyToolEditsCommand(uri, metadata))),
      vscode.commands.registerCommand('ngin.toolingPlan', (arg) => this.runHandled(() => this.toolingPlanCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.graph', (arg) => this.runHandled(() => this.graphCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.variablesExplain', (arg) => this.runHandled(() => this.variablesExplainCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.explainSelection', (arg) => this.runHandled(() => this.explainSelectionCommand(this.asExplorerTarget(arg)))),
      vscode.commands.registerCommand('ngin.showResolvedInputs', (arg) => this.runHandled(() => this.showInspectDetailsCommand('inputs', this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.showInactiveTooling', (arg) => this.runHandled(() => this.showInspectDetailsCommand('tooling', this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.settingsInit', (arg) => this.runHandled(() => this.settingsInitCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.workspaceStatus', () => this.runHandled(() => this.workspaceCommand('status'))),
      vscode.commands.registerCommand('ngin.workspaceDoctor', () => this.runHandled(() => this.workspaceCommand('doctor'))),
      vscode.commands.registerCommand('ngin.openLastLaunchManifest', () => this.runHandled(() => this.openLastLaunchManifest())),
      vscode.commands.registerCommand('ngin.openProjectManifest', (arg) => this.runHandled(() => this.openProjectManifestCommand(this.asExplorerTarget(arg)))),
      vscode.commands.registerCommand('ngin.openProjectXmlSource', (uri?: vscode.Uri) => this.runHandled(() => this.openProjectXmlSourceCommand(uri))),
      vscode.commands.registerCommand('ngin.openPath', (arg) => this.runHandled(() => this.openExplorerPathCommand(this.asExplorerTarget(arg)))),
      vscode.commands.registerCommand('ngin.revealPath', (arg) => this.runHandled(() => this.revealExplorerPathCommand(this.asExplorerTarget(arg)))),
      vscode.commands.registerCommand('ngin.copyPath', (arg) => this.runHandled(() => this.copyExplorerPathCommand(this.asExplorerTarget(arg)))),
      vscode.commands.registerCommand('ngin.setActiveProject', (arg) => this.runHandled(() => this.selectProjectCommand(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.projectNewSourceFile', (arg) => this.runHandled(() => this.createProjectFileCommand(this.asExplorerTarget(arg), 'source'))),
      vscode.commands.registerCommand('ngin.projectNewConfigFile', (arg) => this.runHandled(() => this.createProjectFileCommand(this.asExplorerTarget(arg), 'config'))),
      vscode.commands.registerCommand('ngin.projectNewFolder', (arg) => this.runHandled(() => this.createProjectFolderCommand(this.asExplorerTarget(arg)))),
      vscode.commands.registerCommand('ngin.projectCopy', (arg) => this.runHandled(() => this.copyProjectPathCommand(this.asExplorerTarget(arg)))),
      vscode.commands.registerCommand('ngin.projectDuplicate', (arg) => this.runHandled(() => this.duplicateProjectPathCommand(this.asExplorerTarget(arg)))),
      vscode.commands.registerCommand('ngin.projectRename', (arg) => this.runHandled(() => this.renameProjectPathCommand(this.asExplorerTarget(arg)))),
      vscode.commands.registerCommand('ngin.projectDelete', (arg) => this.runHandled(() => this.deleteProjectPathCommand(this.asExplorerTarget(arg)))),
      vscode.commands.registerCommand('ngin.refresh', () => this.runHandled(() => this.refreshUi(undefined, true))),
      vscode.commands.registerCommand('ngin.internal.pickProject', (arg) => this.runHandled(() => this.pickProjectFromStatusBar(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.internal.pickProfile', (arg) => this.runHandled(() => this.pickProfileFromStatusBar(this.asCommandTarget(arg)))),
      vscode.commands.registerCommand('ngin.internal.openPath', (filePath) => this.runHandled(() => this.openPathCommand(filePath))),
      vscode.commands.registerCommand('ngin.internal.revealPath', (filePath) => this.runHandled(() => this.revealPathCommand(filePath))),
      vscode.window.registerCustomEditorProvider(
        NginProjectEditorProvider.viewType,
        new NginProjectEditorProvider({
          inspect: (document) => this.getProjectEditorInspectState(document),
          apply: (document, update) => this.applyProjectEditorUpdate(document, update),
          openSource: (uri) => this.openProjectXmlSourceCommand(uri),
          validate: (uri) => this.validateProjectEditor(uri)
        }),
        {
          supportsMultipleEditorsPerDocument: false,
          webviewOptions: { retainContextWhenHidden: true }
        }
      ),
      vscode.workspace.registerTextDocumentContentProvider('ngin-variables', this.variableDocumentProvider),
      vscode.languages.registerCompletionItemProvider(
        { language: SUPPORTED_LANGUAGE_ID },
        new NginLocalSettingsCompletionProvider(() => this.workspaceState.getWorkspaceInfo()),
        '"',
        "'",
        '.'
      ),
      vscode.workspace.onDidSaveTextDocument((document) => this.handleDocumentSaved(document)),
      vscode.languages.registerCodeActionsProvider({ scheme: 'file' }, {
        provideCodeActions: (document, _range, context) => this.provideToolCodeActions(document, context)
      }, { providedCodeActionKinds: [vscode.CodeActionKind.QuickFix] }),
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
    profileConfiguration: NginDebugConfiguration
  ): Promise<vscode.DebugConfiguration | undefined> {
    const context = await this.resolveCommandContext({
      preferredUri: folder?.uri,
      projectPath: profileConfiguration.project,
      profileName: profileConfiguration.profile
    }, false);

    if (!context) {
      void vscode.window.showErrorMessage('Unable to resolve the NGIN project and profile for debugging.');
      return undefined;
    }

    const cpptools = vscode.extensions.getExtension('ms-vscode.cpptools');
    if (!cpptools) {
      void vscode.window.showErrorMessage('NGIN debugging requires the Microsoft C/C++ extension (`ms-vscode.cpptools`).');
      return undefined;
    }

    const buildResult = await this.getLaunchBuildResult(context, {
      cliOverride: profileConfiguration.cliPath,
      outputDirOverride: profileConfiguration.outputDir,
      forceBuild: profileConfiguration.preBuild === true
    });

    const launchManifest = await this.readLaunchManifest(buildResult.launchManifestPath);
    const launch = await this.resolveLaunchTarget(launchManifest, context.project.directory, buildResult.outputDir);

    const native = createNativeDebugConfiguration({
      platform: process.platform,
      program: launch.program,
      cwd: launch.cwd,
      args: profileConfiguration.programArgs ?? [],
      env: profileConfiguration.env ?? {},
      miDebuggerPath: this.getConfiguration(context.workspace.folder).get<string>('debug.miDebuggerPath')?.trim() || undefined
    });

    native.name = profileConfiguration.name ?? `NGIN: Debug ${context.project.name} [${context.profile.name}]`;
    return native as vscode.DebugConfiguration;
  }

  async provideDebugProfiles(folder: vscode.WorkspaceFolder | undefined): Promise<vscode.DebugConfiguration[]> {
    const workspaceInfo = await this.workspaceState.getWorkspaceInfo(folder?.uri);
    if (!workspaceInfo) {
      return [];
    }

    const profiles: vscode.DebugConfiguration[] = [];
    for (const project of workspaceInfo.projects) {
      const profileName = project.defaultProfile ?? project.profiles[0]?.name;
      if (!profileName) {
        continue;
      }

      profiles.push({
        type: 'ngin',
        request: 'launch',
        name: `NGIN: Debug ${project.name} [${profileName}]`,
        project: project.path,
        profile: profileName,
        preBuild: false,
        programArgs: [],
        env: {}
      });
    }

    return profiles;
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

  private getConfigurationForRoot(workspaceRoot: string): vscode.WorkspaceConfiguration {
    const folder = vscode.workspace.workspaceFolders?.find((candidate) => comparablePath(candidate.uri.fsPath) === comparablePath(workspaceRoot));
    return this.getConfiguration(folder);
  }

  private asCommandTarget(value: unknown): NginCommandTarget | undefined {
    if (!value || typeof value !== 'object') {
      return undefined;
    }

    const candidate = value as NginCommandTarget;
    if (!candidate.preferredUri && !candidate.projectPath && !candidate.profileName) {
      return undefined;
    }

    return {
      preferredUri: candidate.preferredUri,
      projectPath: candidate.projectPath,
      profileName: candidate.profileName
    };
  }

  private asExplorerTarget(value: unknown): ProjectExplorerTarget | undefined {
    if (!value || typeof value !== 'object') {
      return undefined;
    }

    const candidate = value as ProjectExplorerTarget & { targetPath?: string };
    const fsPath = candidate.fsPath ?? candidate.targetPath;
    if (!candidate.projectPath && !candidate.profileName && !candidate.preferredUri && !fsPath) {
      return undefined;
    }

    return {
      preferredUri: candidate.preferredUri,
      projectPath: candidate.projectPath,
      profileName: candidate.profileName,
      fsPath,
      role: candidate.role,
      isDirectory: candidate.isDirectory,
      explainIdentity: candidate.explainIdentity
    };
  }

  private async refreshUi(preferredUri?: vscode.Uri, forceInspectRefresh = false): Promise<void> {
    const snapshot = await this.workspaceState.getSnapshot(preferredUri);
    await this.attachInspectSnapshot(snapshot, forceInspectRefresh);
    this.sidebar.refresh(snapshot);
    this.statusBar.refresh(snapshot);
    await this.cppToolsProvider.refresh(snapshot);
  }

  private inspectCacheKey(snapshot: NginWorkspaceSnapshot): string | undefined {
    if (!snapshot.workspace || !snapshot.context) {
      return undefined;
    }
    return [
      comparablePath(snapshot.context.project.path),
      snapshot.context.profile.name,
      snapshot.outputDir ?? ''
    ].join('|');
  }

  private async attachInspectSnapshot(snapshot: NginWorkspaceSnapshot, forceRefresh: boolean): Promise<void> {
    const key = this.inspectCacheKey(snapshot);
    if (!key || !snapshot.workspace || !snapshot.context) {
      return;
    }

    if (!forceRefresh) {
      const cached = this.inspectCache.get(key);
      if (cached) {
        snapshot.inspectGraph = cached.payload;
        snapshot.inspectError = cached.error;
        return;
      }
    }

    try {
      const payload = await this.runInspect(snapshot);
      snapshot.inspectGraph = payload;
      snapshot.inspectError = payload.plans?.diagnostics?.some((diagnostic) => diagnostic.severity === 'error')
        ? 'Inspect reported resolver diagnostics.'
        : undefined;
      this.inspectCache.set(key, { payload, error: snapshot.inspectError });
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      snapshot.inspectError = message;
      this.inspectCache.set(key, { error: message });
      this.outputChannel.appendLine(`inspect failed: ${message}`);
    }
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
      explicitProfile: target?.profileName,
      promptIfNeeded
    });

    if (context) {
      return context;
    }

    const workspaceInfo = await this.workspaceState.getWorkspaceInfo(target?.preferredUri);
    if (!workspaceInfo) {
      void vscode.window.showErrorMessage('NGIN workspace not found. Open a folder with a .ngin workspace manifest.');
    } else if (target?.projectPath || target?.profileName) {
      void vscode.window.showErrorMessage('Unable to resolve the selected NGIN project or profile.');
    }

    return undefined;
  }

  private async selectProjectCommand(target?: NginCommandTarget): Promise<void> {
    if (target?.projectPath) {
      const context = await this.resolveCommandContext(target, false);
      if (context) {
        await this.workspaceState.rememberSelection(context);
        await this.refreshUi(target.preferredUri);
        void vscode.window.showInformationMessage(`Selected NGIN project: ${context.project.name}`);
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

    await this.workspaceState.rememberSelection(context);
    await this.refreshUi(target?.preferredUri);
    void vscode.window.showInformationMessage(`Selected NGIN project: ${context.project.name}`);
  }

  private async selectProfileCommand(target?: NginCommandTarget): Promise<void> {
    if (target?.projectPath && target?.profileName) {
      const context = await this.resolveCommandContext(target, false);
      if (context) {
        await this.workspaceState.rememberSelection(context);
        await this.refreshUi(target.preferredUri);
        void vscode.window.showInformationMessage(`Selected NGIN profile: ${context.project.name} [${context.profile.name}]`);
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

    const profile = await this.workspaceState.pickProfile(project);
    if (!profile) {
      return;
    }

    await this.workspaceState.rememberSelection({ workspace: workspaceInfo, project, profile });
    await this.refreshUi(target?.preferredUri);
    void vscode.window.showInformationMessage(`Selected NGIN profile: ${project.name} [${profile.name}]`);
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

    const profile = await this.workspaceState.resolveStoredProfile(project)
      ?? await this.workspaceState.promptForProfile(project);
    if (!profile) {
      return;
    }

    const context = { workspace: workspaceInfo, project, profile };
    await this.workspaceState.rememberSelection(context);
    await this.refreshUi(target?.preferredUri);
    void vscode.window.showInformationMessage(`Selected NGIN project: ${context.project.name}`);
  }

  private async pickProfileFromStatusBar(target?: NginCommandTarget): Promise<void> {
    const snapshot = await this.workspaceState.getSnapshot(target?.preferredUri);
    if (!snapshot.workspace || !snapshot.context) {
      void vscode.window.showErrorMessage('NGIN workspace not found.');
      return;
    }

    const profile = await this.workspaceState.promptForProfile(snapshot.context.project);
    if (!profile) {
      return;
    }

    await this.workspaceState.rememberSelection({
      workspace: snapshot.workspace,
      project: snapshot.context.project,
      profile
    });
    await this.refreshUi(target?.preferredUri);
    void vscode.window.showInformationMessage(`Selected NGIN profile: ${snapshot.context.project.name} [${profile.name}]`);
  }

  private async configureCommand(target?: NginCommandTarget): Promise<void> {
    const context = await this.resolveCommandContext(target);
    if (!context) {
      return;
    }

    const outputDir = this.workspaceState.computeOutputDirectory(context);
    const result = await this.runCli(
      context.workspace.root,
      ['configure', '--project', context.project.path, '--profile', context.profile.name, '--output', outputDir],
      vscode.Uri.file(context.project.path)
    );

    if (result.exitCode !== 0) {
      throw new Error(`ngin configure failed for ${context.project.name} [${context.profile.name}]`);
    }

    await this.refreshUi(context.workspace.folder?.uri);
    void vscode.window.showInformationMessage(`Configured ${context.project.name} [${context.profile.name}]`);
  }

  private async buildCommand(target?: NginCommandTarget): Promise<void> {
    const context = await this.resolveCommandContext(target);
    if (!context) {
      return;
    }

    await this.buildProject(context);
    await this.refreshUi(context.workspace.folder?.uri, true);
  }

  private async cleanCommand(target?: NginCommandTarget): Promise<void> {
    const context = await this.resolveCommandContext(target);
    if (!context) {
      return;
    }

    const outputDir = this.workspaceState.computeOutputDirectory(context);
    const launchManifestPath = computeLaunchManifestPath(outputDir, context.project.name, context.profile.name);
    const result = await this.runCli(
      context.workspace.root,
      ['clean', '--project', context.project.path, '--profile', context.profile.name, '--output', outputDir],
      vscode.Uri.file(context.project.path)
    );

    if (result.exitCode !== 0) {
      throw new Error(`ngin clean failed for ${context.project.name} [${context.profile.name}]`);
    }

    await this.workspaceState.clearLastLaunchManifestPath(launchManifestPath);
    await this.refreshUi(context.workspace.folder?.uri);
    void vscode.window.showInformationMessage(`Cleaned ${context.project.name} [${context.profile.name}]`);
  }

  private async rebuildCommand(target?: NginCommandTarget): Promise<void> {
    const context = await this.resolveCommandContext(target);
    if (!context) {
      return;
    }

    await this.buildProject(context, { command: 'rebuild' });
    await this.refreshUi(context.workspace.folder?.uri, true);
  }

  private async validateCommand(target?: NginCommandTarget, options?: { silent?: boolean }): Promise<void> {
    const context = await this.resolveCommandContext(target, !options?.silent);
    if (!context) {
      return;
    }

    const result = await this.runCli(
      context.workspace.root,
      ['validate', '--project', context.project.path, '--profile', context.profile.name],
      vscode.Uri.file(context.project.path),
      undefined,
      'validate'
    );

    if (result.exitCode !== 0) {
      if (options?.silent) {
        return;
      }
      throw new Error(`ngin validate failed for ${context.project.name} [${context.profile.name}]`);
    }

    await this.warnIfLocalSettingsNotIgnored(context);
    if (!options?.silent) {
      void vscode.window.showInformationMessage(`Validated ${context.project.name} [${context.profile.name}]`);
    }
  }

  private async analyzeCommand(
    target?: NginCommandTarget,
    options?: { silent?: boolean; file?: string; changedSince?: string }
  ): Promise<void> {
    if (!vscode.workspace.isTrusted) {
      if (!options?.silent) throw new Error('Trust this workspace before running development tools.');
      return;
    }
    const context = await this.resolveCommandContext(target, !options?.silent);
    if (!context) {
      return;
    }

    const outputDir = this.workspaceState.computeOutputDirectory(context);
    const selectionArgs = options?.file
      ? ['--file', options.file]
      : options?.changedSince ? ['--changed-since', options.changedSince] : [];
    const result = await this.runCli(
      context.workspace.root,
      ['analyze', '--project', context.project.path, '--profile', context.profile.name,
        '--output', outputDir, ...selectionArgs],
      vscode.Uri.file(context.project.path),
      undefined,
      'analyze'
    );

    if (result.exitCode !== 0) {
      if (options?.silent) {
        return;
      }
      throw new Error(`ngin analyze failed for ${context.project.name} [${context.profile.name}]`);
    }

    if (result.eventCommandSucceeded) {
      await this.refreshUi(context.workspace.folder?.uri, true);
    }
    if (!options?.silent) {
      void vscode.window.showInformationMessage(`Analyzed ${context.project.name} [${context.profile.name}]`);
    }
  }

  private async analyzeActiveFileCommand(): Promise<void> {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.uri.scheme !== 'file') {
      throw new Error('Open a source file before running active-file analysis.');
    }
    await this.analyzeCommand(
      { preferredUri: editor.document.uri },
      { file: editor.document.uri.fsPath }
    );
  }

  private async analyzeChangedFilesCommand(target?: NginCommandTarget): Promise<void> {
    const revision = await vscode.window.showInputBox({
      title: 'Analyze Changed Files',
      prompt: 'Git revision used as the comparison base',
      value: 'HEAD',
      validateInput: (value) => value.trim() ? undefined : 'A revision is required'
    });
    if (!revision) return;
    await this.analyzeCommand(target, { changedSince: revision.trim() });
  }

  private async graphCommand(target?: NginCommandTarget): Promise<void> {
    const context = await this.resolveCommandContext(target);
    if (!context) {
      return;
    }

    const result = await this.runCli(
      context.workspace.root,
      ['graph', '--project', context.project.path, '--profile', context.profile.name],
      vscode.Uri.file(context.project.path)
    );

    if (result.exitCode !== 0) {
      throw new Error(`ngin graph failed for ${context.project.name} [${context.profile.name}]`);
    }
  }

  private async toolingPlanCommand(target?: NginCommandTarget): Promise<void> {
    const context = await this.resolveCommandContext(target);
    if (!context) {
      return;
    }

    const result = await this.runCli(
      context.workspace.root,
      ['graph', '--tooling-plan', '--project', context.project.path, '--profile', context.profile.name],
      vscode.Uri.file(context.project.path)
    );
    if (result.exitCode !== 0) {
      throw new Error(`ngin tooling plan failed for ${context.project.name} [${context.profile.name}]`);
    }
  }

  private async addToolActionCommand(target?: NginCommandTarget): Promise<void> {
    const context = await this.resolveCommandContext(target);
    if (!context) {
      return;
    }
    const listResult = await this.runCli(
      context.workspace.root,
      ['tool', 'list', '--available', '--format', 'json', '--project', context.project.path,
        '--profile', context.profile.name],
      vscode.Uri.file(context.project.path)
    );
    let availableActions: Array<{ identity: string; kind?: string; version?: string; inputContracts?: string[] }> = [];
    try {
      const payload = JSON.parse(listResult.output) as { availableActions?: typeof availableActions };
      availableActions = payload.availableActions ?? [];
    } catch {
      // The CLI error below provides the actionable failure when discovery output is invalid.
    }
    if (listResult.exitCode !== 0) throw new Error('Could not discover available tool actions.');
    const selected = await vscode.window.showQuickPick(
      availableActions.map((candidate) => ({
        label: candidate.identity,
        description: [candidate.kind, candidate.version].filter(Boolean).join(' • '),
        detail: candidate.inputContracts?.join(', ')
      })),
      { title: 'Add NGIN Tool Action', placeHolder: 'Select a package-provided action' }
    );
    const action = selected?.label;
    if (!action) {
      return;
    }
    const defaultRun = action.slice(action.lastIndexOf('::') + 2);
    const runName = await vscode.window.showInputBox({
      title: 'Tool Run Identity',
      value: defaultRun,
      prompt: 'Stable run identity used by project and profile overlays',
      validateInput: (value) => value.trim() ? undefined : 'A run identity is required'
    });
    if (!runName) {
      return;
    }
    const result = await this.runCli(
      context.workspace.root,
      ['add', 'tool-action', action, '--run', runName, '--project', context.project.path],
      vscode.Uri.file(context.project.path)
    );
    if (result.exitCode !== 0) {
      throw new Error(`Could not add tool action ${action}`);
    }
    await this.refreshUi(vscode.Uri.file(context.project.path), true);
    void vscode.window.showInformationMessage(`Added ${action} as tool run ${runName}`);
  }

  private async runToolRunCommand(target?: ProjectExplorerTarget): Promise<void> {
    if (!vscode.workspace.isTrusted) {
      throw new Error('Trust this workspace before running development tools.');
    }
    if (!target?.explainIdentity?.startsWith('run:')) {
      throw new Error('Select a resolved tool run.');
    }
    const context = await this.resolveCommandContext(target);
    if (!context) return;
    const runName = target.explainIdentity.slice('run:'.length);
    const result = await this.runCli(
      context.workspace.root,
      ['tool', 'run', runName, '--project', context.project.path, '--profile', context.profile.name,
        '--output', this.workspaceState.computeOutputDirectory(context)],
      vscode.Uri.file(context.project.path),
      undefined,
      'analyze'
    );
    if (result.exitCode !== 0) throw new Error(`Tool run ${runName} failed.`);
    await this.refreshUi(vscode.Uri.file(context.project.path), true);
  }

  private provideToolCodeActions(document: vscode.TextDocument, context: vscode.CodeActionContext): vscode.CodeAction[] {
    return context.diagnostics.flatMap((diagnostic) => {
      const metadata = this.toolDiagnosticMetadata.get(diagnostic);
      if (!metadata?.editSetIds.length) return [];
      const action = new vscode.CodeAction(`Apply NGIN fixes from ${metadata.run}`, vscode.CodeActionKind.QuickFix);
      action.diagnostics = [diagnostic];
      action.command = {
        command: 'ngin.applyToolEdits',
        title: action.title,
        arguments: [document.uri, metadata]
      };
      return [action];
    });
  }

  private async applyToolEditsCommand(uri: vscode.Uri, metadata: ToolDiagnosticMetadata): Promise<void> {
    if (!vscode.workspace.isTrusted) {
      throw new Error('Trust this workspace before applying tool-proposed edits.');
    }
    const context = await this.resolveCommandContext({ preferredUri: uri });
    if (!context) return;
    const triggeringDocument = vscode.workspace.textDocuments.find((document) => document.uri.toString() === uri.toString());
    if (triggeringDocument?.isDirty) {
      throw new Error('Save the document before applying tool-proposed edits.');
    }
    if (triggeringDocument && metadata.documentVersion !== undefined &&
        triggeringDocument.version !== metadata.documentVersion) {
      throw new Error('The document changed after the tool result was produced; rerun the tool.');
    }
    const result = await this.runCli(
      context.workspace.root,
      ['tool', 'edits', '--run', metadata.run, '--format', 'json', '--project', context.project.path,
        '--profile', context.profile.name, '--output', this.workspaceState.computeOutputDirectory(context)],
      vscode.Uri.file(context.project.path),
      undefined
    );
    if (result.exitCode !== 0) throw new Error(`Could not load edits from ${metadata.run}.`);
    let payload: ToolEditsResponse;
    try {
      payload = JSON.parse(result.output) as ToolEditsResponse;
    } catch {
      throw new Error(`Tool edits for ${metadata.run} returned malformed JSON.`);
    }
    if (payload.kind !== 'NGIN.ToolEdits') {
      throw new Error(`Tool edits for ${metadata.run} returned an unsupported payload.`);
    }
    const selectedIds = new Set(metadata.editSetIds);
    const editSets = (payload.results ?? []).flatMap((entry) => entry.edits ?? [])
      .filter((editSet) => selectedIds.has(editSet.id));
    if (editSets.length === 0) {
      throw new Error('The selected edit set is no longer available; rerun the tool.');
    }
    if (editSets.some((editSet) => editSet.applicability === 'unsafe')) {
      throw new Error('Unsafe tool edits cannot be applied as an editor quick fix.');
    }

    const workspaceEdit = new vscode.WorkspaceEdit();
    const rangesByFile = new Map<string, vscode.Range[]>();
    for (const editSet of editSets) {
      for (const fileEdit of editSet.files ?? []) {
        const filePath = fileEdit.path?.absolute;
        if (!filePath || !path.isAbsolute(filePath) ||
            !isPathWithinDirectory(filePath, context.workspace.root)) {
          throw new Error(`Tool edit '${editSet.id}' targets a path outside the workspace.`);
        }
        const targetUri = vscode.Uri.file(filePath);
        const openDocument = vscode.workspace.textDocuments.find((document) => document.uri.toString() === targetUri.toString());
        if (openDocument?.isDirty) {
          throw new Error(`Save ${path.basename(filePath)} before applying tool-proposed edits.`);
        }
        const contents = await vscode.workspace.fs.readFile(targetUri);
        if (fileEdit.expectedDigest && toolContentDigest(contents) !== fileEdit.expectedDigest) {
          throw new Error(`${path.basename(filePath)} changed after the edit was produced; rerun the tool.`);
        }
        const existingRanges = rangesByFile.get(targetUri.toString()) ?? [];
        for (const textEdit of fileEdit.edits ?? []) {
          const start = textEdit.range?.start;
          const end = textEdit.range?.end;
          if (!start?.line || !start.column || !end?.line || !end.column ||
              typeof textEdit.newText !== 'string') {
            throw new Error(`Tool edit '${editSet.id}' contains an invalid range.`);
          }
          const range = new vscode.Range(start.line - 1, start.column - 1, end.line - 1, end.column - 1);
          if (existingRanges.some((candidate) => candidate.intersection(range) !== undefined)) {
            throw new Error(`Tool edit '${editSet.id}' contains overlapping ranges.`);
          }
          existingRanges.push(range);
          workspaceEdit.replace(targetUri, range, textEdit.newText);
        }
        rangesByFile.set(targetUri.toString(), existingRanges);
      }
    }
    if (!(await vscode.workspace.applyEdit(workspaceEdit))) {
      throw new Error(`VS Code refused edits from ${metadata.run}.`);
    }
  }

  private async variablesExplainCommand(target?: NginCommandTarget): Promise<void> {
    const context = await this.resolveCommandContext(target);
    if (!context) {
      return;
    }

    const result = await this.runCli(
      context.workspace.root,
      ['variables', 'explain', '--project', context.project.path, '--profile', context.profile.name],
      vscode.Uri.file(context.project.path)
    );

    if (result.exitCode !== 0) {
      throw new Error(`ngin variables explain failed for ${context.project.name} [${context.profile.name}]`);
    }

    await this.openVirtualVariablesDocument(
      `NGIN Variables - ${context.project.name} [${context.profile.name}]`,
      result.output
    );
    await this.warnIfLocalSettingsNotIgnored(context);
  }

  private async explainSelectionCommand(target?: ProjectExplorerTarget): Promise<void> {
    if (!target?.explainIdentity) {
      throw new Error('The selected item does not provide an NGIN explain identity.');
    }
    const context = await this.resolveCommandContext(target);
    if (!context) {
      return;
    }

    const result = await this.runCli(
      context.workspace.root,
      ['explain', target.explainIdentity, '--project', context.project.path, '--profile', context.profile.name],
      vscode.Uri.file(context.project.path)
    );
    if (result.exitCode !== 0) {
      throw new Error(`ngin explain failed for ${target.explainIdentity}`);
    }
    await this.openVirtualVariablesDocument(
      `NGIN Explain - ${target.explainIdentity.replace(/[^A-Za-z0-9._-]+/g, '-')}`,
      result.output
    );
  }

  private async showInspectDetailsCommand(kind: 'inputs' | 'tooling', target?: NginCommandTarget): Promise<void> {
    const snapshot = await this.workspaceState.getSnapshot(target?.preferredUri);
    if (!snapshot.workspace || !snapshot.context) {
      throw new Error('No active NGIN project/profile is selected.');
    }
    const graph = await this.runInspect(snapshot);
    const value = kind === 'inputs'
      ? graph.plans?.build?.inputs ?? []
      : (graph.plans?.generators ?? []).filter((generator) => generator.state === 'excluded');
    const title = kind === 'inputs' ? 'Resolved Inputs' : 'Inactive Tooling';
    const content = value.length > 0 ? JSON.stringify(value, null, 2) : `(no ${title.toLowerCase()})\n`;
    await this.openVirtualVariablesDocument(
      `NGIN ${title} - ${snapshot.context.project.name} [${snapshot.context.profile.name}]`,
      content
    );
  }

  private async settingsInitCommand(target?: NginCommandTarget): Promise<void> {
    const context = await this.resolveCommandContext(target);
    if (!context) {
      return;
    }

    const result = await this.runCli(
      context.workspace.root,
      ['settings', 'init', '--project', context.project.path],
      vscode.Uri.file(context.project.path)
    );

    if (result.exitCode !== 0) {
      throw new Error(`ngin settings init failed for ${context.project.name}`);
    }

    await this.refreshUi(context.workspace.folder?.uri);
    const initializedPath = extractInitializedSettingsPath(result.output)
      ?? path.join(context.workspace.root, '.ngin', 'local', 'user.nginsettings');
    await this.warnIfLocalSettingsNotIgnored(context);
    await this.openPathCommand(initializedPath);
    void vscode.window.showInformationMessage(`Initialized local settings for ${context.project.name}`);
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

    const debugConfiguration: NginDebugConfiguration = {
      type: 'ngin',
      request: 'launch',
      name: `NGIN: Debug ${context.project.name} [${context.profile.name}]`,
      project: context.project.path,
      profile: context.profile.name,
      preBuild: false,
      programArgs: [],
      env: {}
    };

    await vscode.debug.startDebugging(context.workspace.folder, debugConfiguration);
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

  private resolveExplorerPath(target?: ProjectExplorerTarget): string | undefined {
    return target?.fsPath ?? target?.projectPath;
  }

  private async openExplorerPathCommand(target?: ProjectExplorerTarget): Promise<void> {
    await this.openPathCommand(this.resolveExplorerPath(target));
  }

  private async revealExplorerPathCommand(target?: ProjectExplorerTarget): Promise<void> {
    await this.revealPathCommand(this.resolveExplorerPath(target));
  }

  private async copyExplorerPathCommand(target?: ProjectExplorerTarget): Promise<void> {
    const filePath = this.resolveExplorerPath(target);
    if (!filePath) {
      return;
    }

    await vscode.env.clipboard.writeText(filePath);
    void vscode.window.showInformationMessage(`Copied path: ${filePath}`);
  }

  private async openVirtualVariablesDocument(title: string, content: string): Promise<void> {
    const uri = this.variableDocumentProvider.createDocument(title, content);
    const document = await vscode.workspace.openTextDocument(uri);
    await vscode.window.showTextDocument(document, { preview: false });
  }

  private surfaceLocalSettingsWarnings(output: string): void {
    for (const warning of extractLocalSettingsWarnings(output)) {
      if (this.shownLocalSettingsWarnings.has(warning)) {
        continue;
      }
      this.shownLocalSettingsWarnings.add(warning);
      void vscode.window.showWarningMessage(warning);
    }
  }

  private async warnIfLocalSettingsNotIgnored(context: ResolvedCommandContext): Promise<void> {
    const localSettingsRoot = path.join(context.workspace.root, '.ngin', 'local');
    const imports = context.project.localSettingsImports ?? [];
    if (!imports.some((settingsPath) => isPathWithinDirectory(settingsPath, localSettingsRoot))) {
      return;
    }

    const gitignorePath = path.join(context.workspace.root, '.gitignore');
    let gitignore = '';
    try {
      gitignore = await readTextFile(gitignorePath);
    } catch {
      gitignore = '';
    }

    if (isLocalSettingsIgnoredByGitignore(gitignore)) {
      return;
    }

    const warning = `${gitignorePath}: .ngin/local/ is not ignored; local settings can contain machine-specific values or secrets`;
    if (this.shownLocalSettingsWarnings.has(warning)) {
      return;
    }
    this.shownLocalSettingsWarnings.add(warning);
    void vscode.window.showWarningMessage(warning);
  }

  private async openProjectManifestCommand(target?: ProjectExplorerTarget): Promise<void> {
    const manifestPath = target?.fsPath ?? target?.projectPath ?? (await this.resolveExplorerProject(target))?.project.path;
    if (!manifestPath) {
      return;
    }
    await vscode.commands.executeCommand('vscode.open', vscode.Uri.file(manifestPath), { preview: false });
  }

  private async openProjectXmlSourceCommand(uri?: vscode.Uri): Promise<void> {
    const target = uri ?? vscode.window.activeTextEditor?.document.uri;
    if (!target) {
      return;
    }
    await vscode.commands.executeCommand('vscode.openWith', target, 'default', { preview: false });
  }

  private async getProjectEditorInspectState(document: vscode.TextDocument): Promise<{ inspectGraph?: CompositionGraphPayload; activeProfile?: string }> {
    const snapshot = await this.workspaceState.getSnapshot(document.uri);
    if (!snapshot.workspace || !snapshot.context || comparablePath(snapshot.context.project.path) !== comparablePath(document.uri.fsPath)) {
      return {};
    }
    await this.attachInspectSnapshot(snapshot, false);
    return {
      inspectGraph: snapshot.inspectGraph,
      activeProfile: snapshot.context.profile.name
    };
  }

  private async applyProjectEditorUpdate(document: vscode.TextDocument, update: (xml: string) => string): Promise<void> {
    try {
      await NginProjectEditorProvider.applyTextEdit(document, update(document.getText()));
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      void vscode.window.showErrorMessage(message);
    }
  }

  private async validateProjectEditor(uri: vscode.Uri): Promise<void> {
    await this.validateCommand({ preferredUri: uri }, { silent: false });
    await this.refreshUi(uri, true);
  }

  private async createProjectFileCommand(target: ProjectExplorerTarget | undefined, kind: 'source' | 'config'): Promise<void> {
    const resolved = await this.resolveExplorerProject(target);
    if (!resolved) {
      return;
    }

    const baseDir = await this.resolveCreationBaseDirectory(resolved.project, target, kind);
    if (!baseDir) {
      return;
    }

    const relativePath = await vscode.window.showInputBox({
      title: kind === 'source' ? 'New source file' : 'New config file',
      prompt: `Path relative to ${path.relative(resolved.project.directory, baseDir) || '.'}`,
      validateInput: (value) => {
        const trimmed = value.trim();
        if (!trimmed) {
          return 'Enter a file name.';
        }
        if (path.isAbsolute(trimmed)) {
          return 'Enter a relative path.';
        }
        if (trimmed.split(/[\\/]/).some((part) => part === '..')) {
          return 'Parent directory segments are not allowed.';
        }
        return undefined;
      }
    });
    if (!relativePath) {
      return;
    }

    const filePath = path.resolve(baseDir, relativePath.trim());
    if (await pathExists(filePath)) {
      void vscode.window.showErrorMessage(`File already exists: ${filePath}`);
      return;
    }

    await vscode.workspace.fs.createDirectory(vscode.Uri.file(path.dirname(filePath)));
    await vscode.workspace.fs.writeFile(vscode.Uri.file(filePath), Buffer.from('', 'utf8'));

    if (kind === 'config') {
      await this.addConfigInputToProjectManifest(resolved.project.path, resolved.project.directory, filePath);
    }

    await this.refreshUi(resolved.workspace.folder?.uri);
    await this.openPathCommand(filePath);
  }

  private async createProjectFolderCommand(target?: ProjectExplorerTarget): Promise<void> {
    if (target?.role === 'generated') {
      void vscode.window.showErrorMessage('Generated output is read-only from the NGIN Projects view.');
      return;
    }

    const resolved = await this.resolveExplorerProject(target);
    if (!resolved) {
      return;
    }

    const role = target?.role === 'config' ? 'config' : 'source';
    const baseDir = await this.resolveCreationBaseDirectory(resolved.project, target, role);
    if (!baseDir) {
      return;
    }

    const folderName = await vscode.window.showInputBox({
      title: 'New folder',
      prompt: `Folder path relative to ${path.relative(resolved.project.directory, baseDir) || '.'}`,
      validateInput: (value) => {
        const trimmed = value.trim();
        if (!trimmed) {
          return 'Enter a folder name.';
        }
        if (path.isAbsolute(trimmed)) {
          return 'Enter a relative path.';
        }
        if (trimmed.split(/[\\/]/).some((part) => part === '..')) {
          return 'Parent directory segments are not allowed.';
        }
        return undefined;
      }
    });
    if (!folderName) {
      return;
    }

    const folderPath = path.resolve(baseDir, folderName.trim());
    await vscode.workspace.fs.createDirectory(vscode.Uri.file(folderPath));
    await this.refreshUi(resolved.workspace.folder?.uri);
  }

  private async copyProjectPathCommand(target?: ProjectExplorerTarget): Promise<void> {
    const resolved = await this.resolveMutableProjectPath(target);
    if (!resolved) {
      return;
    }

    const suggested = await this.nextCopyPath(resolved.fsPath);
    const destinationInput = await vscode.window.showInputBox({
      title: resolved.isDirectory ? 'Copy folder' : 'Copy file',
      value: path.relative(path.dirname(resolved.fsPath), suggested),
      prompt: `Destination path relative to ${path.dirname(resolved.fsPath)}`,
      validateInput: (value) => this.validateRelativeFileSystemPath(value)
    });
    if (!destinationInput) {
      return;
    }

    const destination = path.resolve(path.dirname(resolved.fsPath), destinationInput.trim());
    await this.copyAuthoredPath(resolved, destination);
  }

  private async duplicateProjectPathCommand(target?: ProjectExplorerTarget): Promise<void> {
    const resolved = await this.resolveMutableProjectPath(target);
    if (!resolved) {
      return;
    }

    await this.copyAuthoredPath(resolved, await this.nextCopyPath(resolved.fsPath));
  }

  private async renameProjectPathCommand(target?: ProjectExplorerTarget): Promise<void> {
    const resolved = await this.resolveMutableProjectPath(target);
    if (!resolved) {
      return;
    }

    const newName = await vscode.window.showInputBox({
      title: resolved.isDirectory ? 'Rename folder' : 'Rename file',
      value: path.basename(resolved.fsPath),
      validateInput: (value) => {
        const trimmed = value.trim();
        if (!trimmed) {
          return 'Enter a name.';
        }
        if (path.isAbsolute(trimmed) || trimmed.includes('/') || trimmed.includes('\\')) {
          return 'Enter a name, not a path.';
        }
        return undefined;
      }
    });
    if (!newName || newName === path.basename(resolved.fsPath)) {
      return;
    }

    const destination = path.join(path.dirname(resolved.fsPath), newName.trim());
    if (await pathExists(destination)) {
      void vscode.window.showErrorMessage(`Path already exists: ${destination}`);
      return;
    }

    await vscode.workspace.fs.rename(vscode.Uri.file(resolved.fsPath), vscode.Uri.file(destination), { overwrite: false });
    if (resolved.role === 'config') {
      await this.renameConfigInputPaths(resolved.project.path, resolved.project.directory, resolved.fsPath, destination, resolved.isDirectory);
    }
    await this.refreshUi(resolved.workspace.folder?.uri);
  }

  private async deleteProjectPathCommand(target?: ProjectExplorerTarget): Promise<void> {
    const resolved = await this.resolveMutableProjectPath(target);
    if (!resolved) {
      return;
    }

    const choice = await vscode.window.showWarningMessage(
      `Delete ${path.basename(resolved.fsPath)}?`,
      { modal: true },
      'Delete'
    );
    if (choice !== 'Delete') {
      return;
    }

    await vscode.workspace.fs.delete(vscode.Uri.file(resolved.fsPath), { recursive: resolved.isDirectory, useTrash: true });
    if (resolved.role === 'config') {
      await this.removeConfigInputPaths(resolved.project.path, resolved.project.directory, resolved.fsPath, resolved.isDirectory);
    }
    await this.refreshUi(resolved.workspace.folder?.uri);
  }

  private async copyAuthoredPath(
    resolved: { workspace: ResolvedWorkspaceInfo; project: ResolvedCommandContext['project']; fsPath: string; role: 'source' | 'config'; isDirectory: boolean },
    destination: string
  ): Promise<void> {
    if (await pathExists(destination)) {
      void vscode.window.showErrorMessage(`Path already exists: ${destination}`);
      return;
    }

    await vscode.workspace.fs.createDirectory(vscode.Uri.file(path.dirname(destination)));
    await vscode.workspace.fs.copy(vscode.Uri.file(resolved.fsPath), vscode.Uri.file(destination), { overwrite: false });
    if (resolved.role === 'config') {
      if (resolved.isDirectory) {
        await this.duplicateConfigInputFolderPaths(resolved.project.path, resolved.project.directory, resolved.fsPath, destination);
      } else {
        await this.addConfigInputToProjectManifest(resolved.project.path, resolved.project.directory, destination);
      }
    }
    await this.refreshUi(resolved.workspace.folder?.uri);
  }

  private async resolveMutableProjectPath(target?: ProjectExplorerTarget): Promise<{ workspace: ResolvedWorkspaceInfo; project: ResolvedCommandContext['project']; fsPath: string; role: 'source' | 'config'; isDirectory: boolean } | undefined> {
    if (!target?.fsPath || (target.role !== 'source' && target.role !== 'config')) {
      void vscode.window.showErrorMessage('Select a source or config file/folder.');
      return undefined;
    }

    const resolved = await this.resolveExplorerProject(target);
    if (!resolved) {
      return undefined;
    }

    const stat = await vscode.workspace.fs.stat(vscode.Uri.file(target.fsPath));
    return {
      ...resolved,
      fsPath: target.fsPath,
      role: target.role,
      isDirectory: Boolean(stat.type & vscode.FileType.Directory)
    };
  }

  private validateRelativeFileSystemPath(value: string): string | undefined {
    const trimmed = value.trim();
    if (!trimmed) {
      return 'Enter a path.';
    }
    if (path.isAbsolute(trimmed)) {
      return 'Enter a relative path.';
    }
    if (trimmed.split(/[\\/]/).some((part) => part === '..')) {
      return 'Parent directory segments are not allowed.';
    }
    return undefined;
  }

  private async nextCopyPath(sourcePath: string): Promise<string> {
    const directory = path.dirname(sourcePath);
    const extension = path.extname(sourcePath);
    const stem = extension ? path.basename(sourcePath, extension) : path.basename(sourcePath);
    let candidate = path.join(directory, `${stem}.copy${extension}`);
    let index = 2;
    while (await pathExists(candidate)) {
      candidate = path.join(directory, `${stem}.copy${index}${extension}`);
      index += 1;
    }
    return candidate;
  }

  private async resolveExplorerProject(target?: ProjectExplorerTarget): Promise<{ workspace: ResolvedWorkspaceInfo; project: ResolvedCommandContext['project'] } | undefined> {
    const workspaceInfo = await this.workspaceState.getWorkspaceInfo(target?.preferredUri);
    if (!workspaceInfo) {
      void vscode.window.showErrorMessage('NGIN workspace not found.');
      return undefined;
    }

    const project = target?.projectPath
      ? this.workspaceState.findProject(workspaceInfo, target.projectPath)
      : (await this.workspaceState.resolveCommandContext({ preferredUri: target?.preferredUri, promptIfNeeded: false }))?.project;
    if (!project) {
      void vscode.window.showErrorMessage('Unable to resolve the selected NGIN project.');
      return undefined;
    }

    return { workspace: workspaceInfo, project };
  }

  private async resolveCreationBaseDirectory(
    project: ResolvedCommandContext['project'],
    target: ProjectExplorerTarget | undefined,
    kind: 'source' | 'config'
  ): Promise<string | undefined> {
    if (target?.role === 'generated') {
      void vscode.window.showErrorMessage('Generated output is read-only from the NGIN Projects view.');
      return undefined;
    }

    if (target?.fsPath && target.role === kind) {
      try {
        const stat = await vscode.workspace.fs.stat(vscode.Uri.file(target.fsPath));
        return stat.type & vscode.FileType.Directory ? target.fsPath : path.dirname(target.fsPath);
      } catch {
        return target.isDirectory ? target.fsPath : path.dirname(target.fsPath);
      }
    }

    if (kind === 'source') {
      const roots = project.sourceRoots.map((root) => path.isAbsolute(root) ? root : path.resolve(project.directory, root));
      return this.pickDirectory(roots, project.directory, 'Select source root');
    }

    const configDirs = Array.from(new Set([
      ...project.configInputs,
      ...project.profiles.flatMap((profile) => profile.configInputs)
    ].map((source) => path.dirname(source) === '.' ? project.directory : path.resolve(project.directory, path.dirname(source)))));

    if (configDirs.length === 0) {
      void vscode.window.showErrorMessage('This project has no existing config input directories.');
      return undefined;
    }

    return this.pickDirectory(configDirs, project.directory, 'Select config directory');
  }

  private async pickDirectory(directories: string[], projectDirectory: string, title: string): Promise<string | undefined> {
    if (directories.length === 0) {
      void vscode.window.showErrorMessage('No eligible directory is declared by this project.');
      return undefined;
    }

    const unique = Array.from(new Set(directories.map((directory) => path.normalize(directory))));
    if (unique.length === 1) {
      return unique[0];
    }

    const picked = await vscode.window.showQuickPick(unique.map((directory) => ({
      label: path.relative(projectDirectory, directory) || '.',
      description: directory,
      directory
    })), { title });

    return picked?.directory;
  }

  private async addConfigInputToProjectManifest(projectPath: string, projectDirectory: string, filePath: string): Promise<void> {
    const manifestXml = await readTextFile(projectPath);
    const result = addRootConfigInput(manifestXml, relativeManifestPath(projectDirectory, filePath));
    if (!result.changed) {
      return;
    }
    await vscode.workspace.fs.writeFile(vscode.Uri.file(projectPath), Buffer.from(result.xml, 'utf8'));
  }

  private async renameConfigInputPaths(projectPath: string, projectDirectory: string, fromPath: string, toPath: string, includeChildren: boolean): Promise<void> {
    const manifestXml = await readTextFile(projectPath);
    const result = renameConfigInputs(
      manifestXml,
      relativeManifestPath(projectDirectory, fromPath),
      relativeManifestPath(projectDirectory, toPath),
      includeChildren
    );
    if (!result.changed) {
      return;
    }
    await vscode.workspace.fs.writeFile(vscode.Uri.file(projectPath), Buffer.from(result.xml, 'utf8'));
  }

  private async removeConfigInputPaths(projectPath: string, projectDirectory: string, sourcePath: string, includeChildren: boolean): Promise<void> {
    const manifestXml = await readTextFile(projectPath);
    const result = removeConfigInputs(manifestXml, relativeManifestPath(projectDirectory, sourcePath), includeChildren);
    if (!result.changed) {
      return;
    }
    await vscode.workspace.fs.writeFile(vscode.Uri.file(projectPath), Buffer.from(result.xml, 'utf8'));
  }

  private async duplicateConfigInputFolderPaths(projectPath: string, projectDirectory: string, fromPath: string, toPath: string): Promise<void> {
    const manifestXml = await readTextFile(projectPath);
    const renamed = renameConfigInputs(
      manifestXml,
      relativeManifestPath(projectDirectory, fromPath),
      relativeManifestPath(projectDirectory, toPath),
      true
    );
    if (!renamed.changed) {
      return;
    }

    let nextXml = manifestXml;
    for (const source of listConfigInputs(renamed.xml)) {
      if (source.startsWith(`${relativeManifestPath(projectDirectory, toPath)}/`)) {
        nextXml = addRootConfigInput(nextXml, source).xml;
      }
    }
    await vscode.workspace.fs.writeFile(vscode.Uri.file(projectPath), Buffer.from(nextXml, 'utf8'));
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
    if (document.uri.scheme !== 'file') {
      return;
    }
    const configuration = this.getConfiguration(vscode.workspace.getWorkspaceFolder(document.uri));
    if (document.languageId === SUPPORTED_LANGUAGE_ID) {
      try {
        if (configuration.get<boolean>('tooling.validateManifestOnSave')) {
          await this.validateCommand({ preferredUri: document.uri }, { silent: true });
        }
        if (configuration.get<boolean>('tooling.runOnManifestSave')) {
          await this.analyzeCommand({ preferredUri: document.uri }, { silent: true });
        }
        await this.refreshUi(document.uri, true);
      } catch {
        // Silent save actions still update diagnostics through CLI event parsing.
      }
      return;
    }
    if (!configuration.get<boolean>('tooling.runActiveFileOnSave')) {
      return;
    }
    const key = document.uri.toString();
    const existing = this.activeFileTimers.get(key);
    if (existing) clearTimeout(existing);
    const delay = Math.max(0, configuration.get<number>('tooling.activeFileDebounceMs') ?? 350);
    this.activeFileTimers.set(key, setTimeout(() => {
      this.activeFileTimers.delete(key);
      void this.runHandled(async () => {
        const context = await this.resolveCommandContext({ preferredUri: document.uri }, false);
        if (!context) return;
        const outputDir = this.workspaceState.computeOutputDirectory(context);
        await this.runCli(
          context.workspace.root,
          ['analyze', '--project', context.project.path, '--profile', context.profile.name,
            '--output', outputDir, '--input-mode', 'ActiveFile', '--file', document.uri.fsPath],
          vscode.Uri.file(context.project.path),
          undefined,
          'analyze'
        );
      });
    }, delay));
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
      '--profile',
      context.profile.name,
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
      throw new Error(`ngin ${options?.command ?? 'build'} failed for ${context.project.name} [${context.profile.name}]`);
    }

    const launchManifestPath = computeLaunchManifestPath(outputDir, context.project.name, context.profile.name);
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
    const launchManifestPath = computeLaunchManifestPath(outputDir, context.project.name, context.profile.name);

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
    cliOverride?: string,
    diagnosticsKind?: 'validate' | 'analyze'
  ): Promise<CliRunResult> {
    const cliPath = await this.resolveCliPath(workspaceRoot, cliOverride);
    await this.warnIfCliStale(cliPath, workspaceRoot);
    const configuration = this.getConfigurationForRoot(workspaceRoot);
    const verbosity = configuration.get<string>('output.verbosity') ?? 'compact';
    const color = configuration.get<string>('output.color') ?? 'never';
    const actualArgs = [...args];
    const commandName = args[0] ?? 'command';
    const profileIndex = actualArgs.indexOf('--profile');
    const diagnosticsOwner = `${diagnosticsResource?.toString() ?? '<none>'}::${profileIndex >= 0 ? actualArgs[profileIndex + 1] ?? '<none>' : '<none>'}`;
    const runKey = `${commandName}::${diagnosticsOwner}`;
    const runGeneration = (this.cliRunGeneration.get(runKey) ?? 0) + 1;
    this.cliRunGeneration.set(runKey, runGeneration);
    const progressCommands = new Set(['configure', 'build', 'rebuild', 'stage', 'publish', 'analyze', 'tool']);
    const useEvents = progressCommands.has(commandName);
    if (useEvents && !actualArgs.includes('--events')) {
      actualArgs.push('--events', 'jsonl');
    }
    if (useEvents && !actualArgs.includes('--backend-output')) {
      actualArgs.push('--backend-output', verbosity === 'verbose' ? 'stream' : 'compact');
    }
    if (!actualArgs.includes('--ui') && !actualArgs.includes('--backend-output')) {
      if (verbosity === 'compact') {
        actualArgs.push('--ui', 'compact', '--backend-output', 'compact');
      } else if (verbosity === 'verbose') {
        actualArgs.push('--verbose', '--backend-output', 'stream');
      } else {
        actualArgs.push('--backend-output', 'stream');
      }
    }
    if (!actualArgs.includes('--plain') && !actualArgs.includes('--color')) {
      if (color === 'never') {
        actualArgs.push('--plain');
      } else {
        actualArgs.push('--color', color === 'always' ? 'always' : 'auto');
      }
    }

    this.outputChannel.show(true);
    this.outputChannel.appendLine(`> ${cliPath} ${actualArgs.join(' ')}`);

    const runProcess = (onEventLabel?: (label: string) => void, cancellation?: vscode.CancellationToken) => new Promise<CliRunResult>((resolve, reject) => {
      let combined = '';
      const eventDiagnostics: NginEventDiagnostic[] = [];
      let eventParseError: Error | undefined;
      let eventCommandSucceeded = false;
      const parser = useEvents ? new NginJsonlEventParser() : undefined;
      const previousProcess = this.activeCliProcesses.get(runKey);
      if (previousProcess && !previousProcess.killed) {
        if (process.platform !== 'win32' && previousProcess.pid) {
          try { process.kill(-previousProcess.pid, 'SIGTERM'); } catch { previousProcess.kill('SIGTERM'); }
        } else {
          previousProcess.kill('SIGTERM');
        }
      }
      const child = spawn(cliPath, actualArgs, { cwd: workspaceRoot, detached: process.platform !== 'win32' });
      this.activeCliProcesses.set(runKey, child);
      const cancellationSubscription = cancellation?.onCancellationRequested(() => {
        if (process.platform !== 'win32' && child.pid) {
          try { process.kill(-child.pid, 'SIGTERM'); } catch { child.kill('SIGTERM'); }
        } else {
          child.kill('SIGTERM');
        }
      });

      child.stdout.on('data', (chunk) => {
        const text = chunk.toString();
        combined += text;
        if (parser) {
          try {
            for (const event of parser.push(text)) {
              const label = eventLabel(event);
              if (label) {
                onEventLabel?.(label);
              }
              const line = eventOutputLine(event);
              if (line) {
                this.outputChannel.appendLine(line);
              }
              const diagnostic = diagnosticFromEvent(event);
              if (diagnostic) {
                eventDiagnostics.push(diagnostic);
              }
              if (event.type === 'command.completed' && event.data.status === 'success') {
                eventCommandSucceeded = true;
              }
            }
          } catch (error) {
            eventParseError = error instanceof Error ? error : new Error(String(error));
            this.outputChannel.appendLine(eventParseError.message);
          }
        } else {
          this.outputChannel.append(text);
        }
      });

      child.stderr.on('data', (chunk) => {
        const text = chunk.toString();
        combined += text;
        this.outputChannel.append(text);
      });

      child.on('error', (error) => reject(error));
      child.on('close', (code) => {
        if (this.activeCliProcesses.get(runKey) === child) this.activeCliProcesses.delete(runKey);
        cancellationSubscription?.dispose();
        if (parser) {
          try {
            for (const event of parser.finish()) {
              const label = eventLabel(event);
              if (label) {
                onEventLabel?.(label);
              }
              const line = eventOutputLine(event);
              if (line) {
                this.outputChannel.appendLine(line);
              }
              const diagnostic = diagnosticFromEvent(event);
              if (diagnostic) {
                eventDiagnostics.push(diagnostic);
              }
              if (event.type === 'command.completed' && event.data.status === 'success') {
                eventCommandSucceeded = true;
              }
            }
          } catch (error) {
            eventParseError = error instanceof Error ? error : new Error(String(error));
            this.outputChannel.appendLine(eventParseError.message);
          }
        }
        const processExitCode = cancellation?.isCancellationRequested ? 130 : code ?? 0;
        const exitCode = eventParseError instanceof NginJsonlParseError && processExitCode === 0 ? 1 : processExitCode;
        if (exitCode !== 0) {
          combined += `\nerror: command exited with code ${exitCode}\n`;
        }
        resolve({ exitCode, output: combined, eventDiagnostics, eventCommandSucceeded });
      });
    });

    const result = progressCommands.has(commandName)
      ? await vscode.window.withProgress(
        {
          location: vscode.ProgressLocation.Notification,
          title: `NGIN ${commandName}`,
          cancellable: true
        },
        async (progress, cancellation) => {
          const started = Date.now();
          const timer = setInterval(() => {
            const elapsedSeconds = Math.max(1, Math.floor((Date.now() - started) / 1000));
            progress.report({ message: `${elapsedSeconds}s elapsed` });
          }, 1000);
          try {
            progress.report({ message: 'starting' });
            return await runProcess((label) => progress.report({ message: label }), cancellation);
          } finally {
            clearInterval(timer);
          }
        })
      : await runProcess();

    if (diagnosticsKind && this.cliRunGeneration.get(runKey) === runGeneration) {
      if (result.eventDiagnostics?.length) {
        this.applyEventDiagnostics(result.eventDiagnostics, diagnosticsResource, diagnosticsKind, diagnosticsOwner);
      } else if (diagnosticsKind === 'analyze') {
        this.applyEventDiagnostics([], diagnosticsResource, diagnosticsKind, diagnosticsOwner);
      } else {
        this.applyDiagnostics(result.output, diagnosticsResource, diagnosticsKind, diagnosticsOwner);
      }
    }
    this.surfaceLocalSettingsWarnings(result.output);
    return result;
  }

  private async runInspect(snapshot: NginWorkspaceSnapshot): Promise<CompositionGraphPayload> {
    if (!snapshot.workspace || !snapshot.context) {
      throw new Error('No active NGIN project/profile is selected.');
    }

    const workspaceRoot = snapshot.workspace.root;
    const cliPath = await this.resolveCliPath(workspaceRoot);
    await this.warnIfCliStale(cliPath, workspaceRoot);
    const args = [
      'inspect',
      '--project',
      snapshot.context.project.path,
      '--profile',
      snapshot.context.profile.name,
      '--format',
      'json'
    ];
    if (snapshot.outputDir) {
      args.push('--output', snapshot.outputDir);
    }

    const result = await new Promise<{ exitCode: number; stdout: string; stderr: string }>((resolve, reject) => {
      let stdout = '';
      let stderr = '';
      const child = spawn(cliPath, args, { cwd: workspaceRoot });

      child.stdout.on('data', (chunk) => {
        stdout += chunk.toString();
      });

      child.stderr.on('data', (chunk) => {
        stderr += chunk.toString();
      });

      child.on('error', (error) => reject(error));
      child.on('close', (code) => resolve({ exitCode: code ?? 0, stdout, stderr }));
    });

    if (!result.stdout.trim()) {
      throw new Error(result.stderr.trim() || `inspect exited with code ${result.exitCode}`);
    }

    let rawPayload: unknown;
    try {
      rawPayload = JSON.parse(result.stdout);
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      throw new Error(`inspect returned invalid JSON: ${message}`);
    }

    const payload = parseCompositionGraphPayload(rawPayload);

    this.applyInspectDiagnostics(payload, snapshot.context.project.path);

    if (result.exitCode !== 0 && (!payload.plans?.diagnostics || payload.plans.diagnostics.length === 0)) {
      throw new Error(result.stderr.trim() || `inspect exited with code ${result.exitCode}`);
    }

    return payload;
  }

  private applyDiagnostics(output: string, fallbackResource: vscode.Uri | undefined, diagnosticsKind: 'validate' | 'analyze', diagnosticsOwner: string): void {
    const parsed = parseCliDiagnostics(output);
    const collection = diagnosticsKind === 'validate' ? this.validateDiagnostics : this.analyzeDiagnostics;
    if (diagnosticsKind === 'validate') {
      collection.clear();
    }

    if (parsed.length === 0) {
      if (diagnosticsKind === 'analyze') {
        this.replaceAnalyzeDiagnostics(diagnosticsOwner, new Map());
      }
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

      const line = entry.line && entry.line > 0 ? entry.line - 1 : 0;
      const column = entry.column && entry.column > 0 ? entry.column - 1 : 0;
      const diagnostic = new vscode.Diagnostic(
        new vscode.Range(line, column, line, column + 1),
        entry.message,
        entry.severity === 'warning' ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error
      );
      diagnostic.source = entry.source ?? (diagnosticsKind === 'validate' ? 'ngin validate' : 'ngin analyze');

      const key = resource.toString();
      const bucket = grouped.get(key) ?? [];
      bucket.push(diagnostic);
      grouped.set(key, bucket);
    }

    if (diagnosticsKind === 'analyze') {
      this.replaceAnalyzeDiagnostics(diagnosticsOwner, grouped);
    } else {
      for (const [key, diagnostics] of grouped) {
        collection.set(vscode.Uri.parse(key), diagnostics);
      }
    }
  }

  private applyEventDiagnostics(entries: NginEventDiagnostic[], fallbackResource: vscode.Uri | undefined, diagnosticsKind: 'validate' | 'analyze', diagnosticsOwner: string): void {
    const collection = diagnosticsKind === 'validate' ? this.validateDiagnostics : this.analyzeDiagnostics;
    if (diagnosticsKind === 'validate') {
      collection.clear();
    }

    const groupedByOwner = new Map<string, Map<string, vscode.Diagnostic[]>>();
    for (const entry of entries) {
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

      const line = entry.line && entry.line > 0 ? entry.line - 1 : 0;
      const column = entry.column && entry.column > 0 ? entry.column - 1 : 0;
      const endLine = entry.endLine && entry.endLine > 0 ? entry.endLine - 1 : line;
      const endColumn = entry.endColumn && entry.endColumn > 0 ? entry.endColumn - 1 : column + 1;
      const diagnostic = new vscode.Diagnostic(
        new vscode.Range(line, column, endLine, endColumn),
        entry.message,
        entry.severity === 'fatal' || entry.severity === 'error'
          ? vscode.DiagnosticSeverity.Error
          : entry.severity === 'warning'
            ? vscode.DiagnosticSeverity.Warning
            : entry.severity === 'info'
              ? vscode.DiagnosticSeverity.Information
              : vscode.DiagnosticSeverity.Hint
      );
      diagnostic.source = entry.run ? `${entry.source ?? 'ngin tool'} (${entry.run})` : entry.source ?? (diagnosticsKind === 'validate' ? 'ngin validate' : 'ngin analyze');
      if (entry.code) {
        diagnostic.code = entry.code;
      }
      if (entry.tags?.includes('unnecessary')) {
        diagnostic.tags = [...(diagnostic.tags ?? []), vscode.DiagnosticTag.Unnecessary];
      }
      if (entry.tags?.includes('deprecated')) {
        diagnostic.tags = [...(diagnostic.tags ?? []), vscode.DiagnosticTag.Deprecated];
      }
      diagnostic.relatedInformation = entry.relatedLocations?.map((related) => new vscode.DiagnosticRelatedInformation(
        new vscode.Location(
          vscode.Uri.file(related.file),
          new vscode.Position(Math.max(0, related.line - 1), Math.max(0, related.column - 1))
        ),
        related.message || 'Related location'
      ));
      if (entry.run && entry.editSetIds?.length) {
        const openDocument = vscode.workspace.textDocuments.find((document) => document.uri.toString() === resource.toString());
        this.toolDiagnosticMetadata.set(diagnostic, {
          run: entry.run,
          editSetIds: entry.editSetIds,
          documentVersion: openDocument?.version
        });
      }

      const ownerKey = `${diagnosticsOwner}::${entry.run ?? '<execution>'}`;
      const grouped = groupedByOwner.get(ownerKey) ?? new Map<string, vscode.Diagnostic[]>();
      const key = resource.toString();
      const bucket = grouped.get(key) ?? [];
      bucket.push(diagnostic);
      grouped.set(key, bucket);
      groupedByOwner.set(ownerKey, grouped);
    }

    if (diagnosticsKind === 'analyze') {
      for (const key of [...this.analyzeDiagnosticsByOwner.keys()]) {
        if (key.startsWith(`${diagnosticsOwner}::`)) {
          this.analyzeDiagnosticsByOwner.delete(key);
        }
      }
      for (const [owner, grouped] of groupedByOwner) {
        this.analyzeDiagnosticsByOwner.set(owner, grouped);
      }
      this.rebuildAnalyzeDiagnostics();
    } else {
      for (const grouped of groupedByOwner.values()) {
        for (const [key, diagnostics] of grouped) {
          collection.set(vscode.Uri.parse(key), diagnostics);
        }
      }
    }
  }

  private replaceAnalyzeDiagnostics(owner: string, grouped: Map<string, vscode.Diagnostic[]>): void {
    for (const key of [...this.analyzeDiagnosticsByOwner.keys()]) {
      if (key.startsWith(`${owner}::`) || key === owner) {
        this.analyzeDiagnosticsByOwner.delete(key);
      }
    }
    if (grouped.size > 0) {
      this.analyzeDiagnosticsByOwner.set(owner, grouped);
    }
    this.rebuildAnalyzeDiagnostics();
  }

  private rebuildAnalyzeDiagnostics(): void {
    this.analyzeDiagnostics.clear();
    const merged = new Map<string, vscode.Diagnostic[]>();
    for (const grouped of this.analyzeDiagnosticsByOwner.values()) {
      for (const [uri, diagnostics] of grouped) {
        merged.set(uri, [...(merged.get(uri) ?? []), ...diagnostics]);
      }
    }
    for (const [uri, diagnostics] of merged) {
      this.analyzeDiagnostics.set(vscode.Uri.parse(uri), diagnostics);
    }
  }

  private applyInspectDiagnostics(payload: CompositionGraphPayload, fallbackPath: string): void {
    this.inspectDiagnostics.clear();
    const diagnostics = payload.plans?.diagnostics ?? [];
    if (!diagnostics.length) {
      return;
    }

    this.inspectDiagnostics.set(
      vscode.Uri.file(fallbackPath),
      diagnostics.map((entry) => {
        const diagnostic = new vscode.Diagnostic(
          new vscode.Range(0, 0, 0, 1),
          entry.subject ? `${entry.subject}: ${entry.message}` : entry.message,
          entry.severity === 'warning' ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error
        );
        diagnostic.source = 'ngin inspect';
        return diagnostic;
      })
    );
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
    const profile = definition.profile || resolvedProject?.defaultProfile;
    if (profile) {
      args.push('--profile', profile);
    }

    if ((definition.command === 'configure' || definition.command === 'build' || definition.command === 'clean' || definition.command === 'rebuild' || definition.command === 'analyze') && definition.output) {
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
    if (definition.project && definition.profile) {
      return `NGIN: ${command} ${path.basename(definition.project, path.extname(definition.project))} [${definition.profile}]`;
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
          ? path.join(configuredOutputRoot, context.project.name, context.profile.name)
          : path.join(context.workspace.root, configuredOutputRoot, context.project.name, context.profile.name)
        : undefined;

      tasks.push(await this.controller.createTask({
        type: 'ngin',
        command: 'configure',
        project: context.project.path,
        profile: context.profile.name,
        output: outputDir
      }, scopedFolder));
      tasks.push(await this.controller.createTask({
        type: 'ngin',
        command: 'build',
        project: context.project.path,
        profile: context.profile.name,
        output: outputDir
      }, scopedFolder));
      tasks.push(await this.controller.createTask({
        type: 'ngin',
        command: 'rebuild',
        project: context.project.path,
        profile: context.profile.name,
        output: outputDir
      }, scopedFolder));
      tasks.push(await this.controller.createTask({
        type: 'ngin',
        command: 'clean',
        project: context.project.path,
        profile: context.profile.name,
        output: outputDir
      }, scopedFolder));
      tasks.push(await this.controller.createTask({
        type: 'ngin',
        command: 'validate',
        project: context.project.path,
        profile: context.profile.name
      }, scopedFolder));
      tasks.push(await this.controller.createTask({
        type: 'ngin',
        command: 'analyze',
        project: context.project.path,
        profile: context.profile.name,
        output: outputDir
      }, scopedFolder));
      tasks.push(await this.controller.createTask({
        type: 'ngin',
        command: 'graph',
        project: context.project.path,
        profile: context.profile.name
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

  async provideDebugProfiles(folder: vscode.WorkspaceFolder | undefined): Promise<vscode.DebugConfiguration[]> {
    return this.controller.provideDebugProfiles(folder);
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
