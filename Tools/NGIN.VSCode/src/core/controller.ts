import * as path from 'node:path';
import * as vscode from 'vscode';
import type {
  CliResult,
  CompositionGraph,
  ContextSnapshot,
  DiscoveryResult,
  NginContext,
  NginDiagnostic,
  ProjectCandidate
} from '../model';
import { CliFailure, NginCli } from './cli';
import { lifecycleArguments, selectionArguments } from './commandArguments';
import { discoverAll, chooseInitialProject } from './discovery';
import { projectsForFile } from './projectOwnership';
import { parseCompositionGraph } from './graph';
import { contextKey, projectOutputDirectory } from './paths';
import { parseCompilerDiagnostics } from './diagnostics';
import { shouldLoadGraph } from './cliCompatibility';
import { resolveWorkspaceChoice } from './selectionChoices';

interface PersistedSelection {
  projectManifest?: string;
  launchProductManifest?: string;
  configuration?: string;
  target?: string;
  toolchain?: string;
  run?: string;
  runs?: Record<string, string>;
  profile?: string;
  options?: Record<string, string>;
  projects?: Record<string, Omit<PersistedSelection, 'projects' | 'projectManifest'>>;
}

const stateKey = 'ngin.activeSelection';

function runSelectionKey(configuration: string, target: string, toolchain: string): string {
  return [configuration, target, toolchain].join('|');
}

export interface ExecuteOptions {
  progress?: boolean;
  announceFailure?: boolean;
  token?: vscode.CancellationToken;
}

function operationLabel(command: string, project: string): string {
  const verbs: Record<string, string> = {
    restore: 'Restoring packages', lock: 'Locking dependencies', configure: 'Preparing build files',
    build: 'Building', stage: 'Staging', run: 'Running', test: 'Testing', benchmark: 'Benchmarking', publish: 'Publishing',
    analyze: 'Analyzing', format: 'Formatting'
  };
  return `${verbs[command] ?? command} ${project}…`;
}

export class NginController implements vscode.Disposable {
  private readonly changed = new vscode.EventEmitter<ContextSnapshot>();
  private readonly disposables: vscode.Disposable[] = [];
  private discoveriesValue: DiscoveryResult[] = [];
  private project?: ProjectCandidate;
  private launchProjectValue?: ProjectCandidate;
  private contextValue?: NginContext;
  private graphValue?: CompositionGraph;
  private graphErrorValue?: string;
  private busyValue?: string;
  private busyProjectManifestValue?: string;
  private graphGeneration = 0;
  private configuredValue = false;
  private configurationInvalidatedAt = 0;
  private lastOperationValue?: ContextSnapshot['lastOperation'];
  private projectSelections: Record<string, Omit<PersistedSelection, 'projects' | 'projectManifest'>> = {};
  private readonly reportedIncompatibleExecutables = new Set<string>();
  private readonly backgroundGraphCache = new Map<string, CompositionGraph | null>();
  private readonly backgroundGraphRequests = new Map<string, Promise<CompositionGraph | undefined>>();

  readonly onDidChange = this.changed.event;

  constructor(
    private readonly extensionContext: vscode.ExtensionContext,
    readonly cli: NginCli,
    private readonly diagnostics: vscode.DiagnosticCollection,
    private readonly compilerDiagnostics: vscode.DiagnosticCollection
  ) {
    this.disposables.push(
      vscode.workspace.onDidChangeConfiguration(event => {
        if (event.affectsConfiguration('ngin.executable') || event.affectsConfiguration('ngin.build.outputRoot')) {
          if (event.affectsConfiguration('ngin.executable')) this.reportedIncompatibleExecutables.clear();
          void this.refreshDiscovery();
        }
      })
    );
  }

  dispose(): void {
    this.changed.dispose();
    this.disposables.forEach(disposable => disposable.dispose());
  }

  get snapshot(): ContextSnapshot {
    return {
      context: this.contextValue, graph: this.graphValue, graphError: this.graphErrorValue,
      busy: this.busyValue, busyProjectManifest: this.busyProjectManifestValue,
      configured: this.configuredValue, lastOperation: this.lastOperationValue
    };
  }

  get discoveries(): readonly DiscoveryResult[] {
    return this.discoveriesValue;
  }

  get projects(): ProjectCandidate[] {
    return this.discoveriesValue.flatMap(discovery => discovery.projects);
  }

  get activeProject(): ProjectCandidate | undefined {
    return this.project;
  }

  get launchProduct(): ProjectCandidate | undefined {
    return this.launchProjectValue;
  }

  async initialize(): Promise<void> {
    await this.refreshDiscovery(false);
    const persisted = this.extensionContext.workspaceState.get<PersistedSelection>(stateKey) ?? {};
    this.projectSelections = persisted.projects ?? {};
    this.launchProjectValue = this.projects.find(candidate => candidate.manifest === persisted.launchProductManifest)
      ?? this.projects[0];
    const active = vscode.window.activeTextEditor?.document.uri.fsPath;
    const initial = chooseInitialProject(this.discoveriesValue, persisted.projectManifest, active);
    if (initial) await this.selectProject(initial, this.projectSelections[initial.manifest] ?? persisted, false);
    else this.emit();
  }

  async refreshDiscovery(preserveSelection = true): Promise<void> {
    const previous = this.contextValue;
    const previousLaunch = this.launchProjectValue?.manifest;
    this.backgroundGraphCache.clear();
    this.backgroundGraphRequests.clear();
    this.discoveriesValue = await discoverAll();
    this.launchProjectValue = this.projects.find(candidate => candidate.manifest === previousLaunch)
      ?? this.projects[0];
    const candidate = preserveSelection
      ? chooseInitialProject(this.discoveriesValue, previous?.projectManifest, vscode.window.activeTextEditor?.document.uri.fsPath)
      : undefined;
    if (candidate) await this.selectProject(candidate, previous, false);
    else this.emit();
  }

  contextForProject(project: ProjectCandidate, selection: Partial<PersistedSelection | NginContext> = {}): NginContext {
    const remembered = this.projectSelections[project.manifest] ?? {};
    const choices = project.workspaceChoices;
    const configuration = resolveWorkspaceChoice(
      selection.configuration ?? remembered.configuration,
      choices?.configurations,
      choices?.defaults.configuration
    ) ?? 'Debug';
    const target = resolveWorkspaceChoice(
      selection.target ?? remembered.target,
      choices?.targets,
      choices?.defaults.target
    ) ?? 'host';
    const toolchain = resolveWorkspaceChoice(
      selection.toolchain ?? remembered.toolchain,
      choices?.toolchains,
      choices?.defaults.toolchain
    ) ?? 'auto';
    const rememberedRun = remembered.runs
      ? remembered.runs[runSelectionKey(configuration, target, toolchain)]
      : remembered.run;
    const folder = vscode.workspace.getWorkspaceFolder(vscode.Uri.file(project.manifest));
    const workspaceFolder = folder?.uri.fsPath ?? project.directory;
    const outputRoot = vscode.workspace.getConfiguration('ngin', vscode.Uri.file(project.manifest)).get<string>('build.outputRoot', 'build/ngin');
    return {
      workspaceFolder,
      workspaceManifest: project.workspaceManifest,
      projectManifest: project.manifest,
      projectName: project.name,
      configuration,
      target,
      toolchain,
      run: selection.run || rememberedRun,
      profile: selection.profile ?? remembered.profile,
      options: { ...(selection.options ?? remembered.options ?? {}) },
      outputDirectory: projectOutputDirectory(workspaceFolder, project, configuration, target, toolchain, outputRoot)
    };
  }

  projectsForFile(file: string): ProjectCandidate[] {
    return projectsForFile(this.projects, file);
  }

  async selectProject(
    project: ProjectCandidate,
    selection: Partial<PersistedSelection | NginContext> = {},
    persist = true
  ): Promise<void> {
    this.project = project;
    this.contextValue = this.contextForProject(project, selection);
    this.graphValue = undefined;
    this.graphErrorValue = undefined;
    this.configuredValue = false;
    if (persist) await this.persist();
    this.emit();
    await this.refreshGraph(false);
  }

  async setLaunchProduct(project: ProjectCandidate): Promise<void> {
    this.launchProjectValue = project;
    await this.persist();
    this.emit();
  }

  async updateSelection(change: Partial<Pick<NginContext, 'configuration' | 'target' | 'toolchain' | 'run' | 'profile' | 'options'>>): Promise<void> {
    if (!this.project || !this.contextValue) return;
    const defined = Object.fromEntries(Object.entries(change).filter(([, value]) => value !== undefined));
    const selection = { ...this.contextValue, ...defined };
    if ((change.configuration || change.target || change.toolchain) && change.run === undefined) delete selection.run;
    this.contextValue = this.contextForProject(this.project, selection);
    this.graphValue = undefined;
    this.graphErrorValue = undefined;
    this.configuredValue = false;
    await this.persist();
    this.emit();
    await this.refreshGraph(true);
  }

  async updateProjectSelection(
    project: ProjectCandidate,
    change: Partial<Pick<NginContext, 'configuration' | 'target' | 'toolchain' | 'run' | 'profile' | 'options'>>
  ): Promise<void> {
    if (this.project?.manifest === project.manifest) {
      await this.updateSelection(change);
      return;
    }
    const current = this.contextForProject(project);
    const defined = Object.fromEntries(Object.entries(change).filter(([, value]) => value !== undefined));
    const selection = { ...current, ...defined };
    if ((change.configuration || change.target || change.toolchain) && change.run === undefined) delete selection.run;
    const next = this.contextForProject(project, selection);
    const previous = this.projectSelections[project.manifest] ?? {};
    const runs = { ...(previous.runs ?? {}) };
    if (next.run) runs[runSelectionKey(next.configuration, next.target, next.toolchain)] = next.run;
    this.projectSelections[project.manifest] = {
      configuration: next.configuration,
      target: next.target,
      toolchain: next.toolchain,
      run: next.run,
      runs,
      profile: next.profile,
      options: { ...next.options }
    };
    this.backgroundGraphCache.clear();
    this.backgroundGraphRequests.clear();
    await this.persist();
    this.emit();
  }

  private async persist(): Promise<void> {
    const context = this.contextValue;
    if (!context) return;
    const previous = this.projectSelections[context.projectManifest] ?? {};
    const runs = { ...(previous.runs ?? {}) };
    if (context.run) runs[runSelectionKey(context.configuration, context.target, context.toolchain)] = context.run;
    this.projectSelections[context.projectManifest] = {
      configuration: context.configuration,
      target: context.target,
      toolchain: context.toolchain,
      run: context.run,
      runs,
      profile: context.profile,
      options: { ...context.options }
    };
    await this.extensionContext.workspaceState.update(stateKey, {
      projectManifest: context.projectManifest,
      launchProductManifest: this.launchProjectValue?.manifest,
      configuration: context.configuration,
      target: context.target,
      toolchain: context.toolchain,
      run: context.run,
      runs,
      profile: context.profile,
      options: context.options,
      projects: this.projectSelections
    } satisfies PersistedSelection);
  }

  private emit(): void {
    this.changed.fire(this.snapshot);
  }

  refreshPresentation(): void {
    this.emit();
  }

  invalidateConfiguration(): void {
    this.configurationInvalidatedAt = Date.now();
    this.configuredValue = false;
    this.emit();
  }

  markConfigured(context: NginContext): void {
    if (context.projectManifest !== this.contextValue?.projectManifest) return;
    this.configuredValue = true;
    this.configurationInvalidatedAt = 0;
    this.emit();
  }

  private applyDiagnostics(diagnostics: NginDiagnostic[], fallbackDirectory: string): void {
    this.applyDiagnosticCollection(this.diagnostics, diagnostics, fallbackDirectory);
  }

  private applyDiagnosticCollection(
    collection: vscode.DiagnosticCollection,
    diagnostics: NginDiagnostic[],
    fallbackDirectory: string,
    source = 'NGIN'
  ): void {
    collection.clear();
    const grouped = new Map<string, vscode.Diagnostic[]>();
    for (const item of diagnostics) {
      const resolved = path.isAbsolute(item.path) ? item.path : path.resolve(fallbackDirectory, item.path);
      const range = new vscode.Range(
        Math.max(0, item.line - 1),
        Math.max(0, item.column - 1),
        Math.max(0, item.line - 1),
        Math.max(0, item.column)
      );
      const diagnostic = new vscode.Diagnostic(
        range,
        item.hint ? `${item.message}\n${item.hint}` : item.message,
        item.severity === 'error' ? vscode.DiagnosticSeverity.Error : vscode.DiagnosticSeverity.Warning
      );
      diagnostic.code = item.code;
      diagnostic.source = source;
      const entries = grouped.get(resolved) ?? [];
      entries.push(diagnostic);
      grouped.set(resolved, entries);
    }
    for (const [file, entries] of grouped) collection.set(vscode.Uri.file(file), entries);
  }

  private async isConfigured(context: NginContext): Promise<boolean> {
    try {
      const state = await vscode.workspace.fs.stat(vscode.Uri.file(path.join(context.outputDirectory, '.ngin-configure-state')));
      if (state.mtime < this.configurationInvalidatedAt) return false;
      await vscode.workspace.fs.stat(vscode.Uri.file(path.join(context.outputDirectory, 'cmake', 'compile_commands.json')));
      const authored = [context.projectManifest, context.workspaceManifest].filter((value): value is string => Boolean(value));
      for (const file of authored) {
        const stat = await vscode.workspace.fs.stat(vscode.Uri.file(file));
        if (stat.mtime > state.mtime) return false;
      }
      return true;
    } catch {
      return false;
    }
  }

  async refreshGraph(announceErrors = false): Promise<CompositionGraph | undefined> {
    const context = this.contextValue;
    if (!context) return undefined;
    const generation = ++this.graphGeneration;
    try {
      const result = await this.cli.run(
        ['graph', ...selectionArguments(context), '--format', 'json'],
        context.workspaceFolder,
        { cwd: path.dirname(context.projectManifest) }
      );
      if (generation !== this.graphGeneration) return this.graphValue;
      this.applyDiagnostics(result.diagnostics, path.dirname(context.projectManifest));
      this.graphValue = parseCompositionGraph(result.stdout);
      this.configuredValue = await this.isConfigured(context);
      this.graphErrorValue = undefined;
      this.emit();
      return this.graphValue;
    } catch (error) {
      if (generation !== this.graphGeneration) return this.graphValue;
      const message = error instanceof Error ? error.message : String(error);
      this.graphValue = undefined;
      this.graphErrorValue = message;
      this.configuredValue = false;
      if (error instanceof CliFailure) this.applyDiagnostics(error.result.diagnostics, path.dirname(context.projectManifest));
      this.reportCliCompatibility(error);
      this.emit();
      if (announceErrors) void this.showFailure('load', context.projectName);
      return undefined;
    }
  }

  async graphForContext(context: NginContext, announceErrors = false): Promise<CompositionGraph | undefined> {
    if (context.projectManifest === this.contextValue?.projectManifest) {
      return shouldLoadGraph(this.graphValue, this.graphErrorValue)
        ? this.refreshGraph(announceErrors)
        : this.graphValue;
    }
    const key = contextKey(context);
    if (this.backgroundGraphCache.has(key)) return this.backgroundGraphCache.get(key) ?? undefined;
    const existing = this.backgroundGraphRequests.get(key);
    if (existing) return existing;
    const request = (async (): Promise<CompositionGraph | undefined> => {
      try {
        const result = await this.cli.run(
          ['graph', ...selectionArguments(context), '--format', 'json'],
          context.workspaceFolder,
          { cwd: path.dirname(context.projectManifest) }
        );
        this.applyDiagnostics(result.diagnostics, path.dirname(context.projectManifest));
        const graph = parseCompositionGraph(result.stdout);
        this.backgroundGraphCache.set(key, graph);
        return graph;
      } catch (error) {
        if (error instanceof CliFailure) this.applyDiagnostics(error.result.diagnostics, path.dirname(context.projectManifest));
        this.backgroundGraphCache.set(key, null);
        this.reportCliCompatibility(error);
        if (announceErrors) void this.showFailure('load', context.projectName);
        return undefined;
      } finally {
        this.backgroundGraphRequests.delete(key);
      }
    })();
    this.backgroundGraphRequests.set(key, request);
    return request;
  }

  cachedGraphForContext(context: NginContext): CompositionGraph | undefined {
    if (context.projectManifest === this.contextValue?.projectManifest) return this.graphValue;
    return this.backgroundGraphCache.get(contextKey(context)) ?? undefined;
  }

  async validate(announce = true): Promise<boolean> {
    const context = this.requireContext();
    return this.validateManifest(context.projectManifest, announce, selectionArguments(context));
  }

  async validateManifest(manifest: string, announce = true, extraArguments: string[] = []): Promise<boolean> {
    const context = this.requireContext();
    try {
      const result = await this.cli.run(
        ['validate', ...(extraArguments.length ? extraArguments : ['--project', manifest]), '--quiet'],
        context.workspaceFolder,
        { cwd: path.dirname(manifest) }
      );
      this.applyDiagnostics(result.diagnostics, path.dirname(manifest));
      if (announce) void vscode.window.setStatusBarMessage(`$(pass) ${path.basename(manifest)} is valid`, 3000);
      return true;
    } catch (error) {
      if (error instanceof CliFailure) this.applyDiagnostics(error.result.diagnostics, path.dirname(manifest));
      if (announce) void this.showFailure('validate', path.basename(manifest));
      return false;
    }
  }

  private async showFailure(command: string, project: string): Promise<void> {
    const subject = `${command} ${project}`;
    const action = await vscode.window.showErrorMessage(
      `Could not ${subject}. Check Problems or NGIN Output for details.`,
      'Open Problems',
      'Show Output'
    );
    if (action === 'Open Problems') await vscode.commands.executeCommand('workbench.actions.view.problems');
    if (action === 'Show Output') this.cli.showOutput();
  }

  private reportCliCompatibility(error: unknown): void {
    if (!(error instanceof CliFailure) || !error.unsupportedOption) return;
    const executable = error.result.command;
    if (this.reportedIncompatibleExecutables.has(executable)) return;
    this.reportedIncompatibleExecutables.add(executable);
    void vscode.window.showErrorMessage(
      `NGIN Tools cannot use '${executable}' because it does not support ${error.unsupportedOption}. `
        + 'Build or install the current NGIN CLI, then update NGIN: Executable.',
      'Open Settings',
      'Show Output'
    ).then(async action => {
      if (action === 'Open Settings') {
        await vscode.commands.executeCommand('workbench.action.openSettings', 'ngin.executable');
      } else if (action === 'Show Output') {
        this.cli.showOutput();
      }
    });
  }

  requireContext(): NginContext {
    if (!this.contextValue) throw new Error('No NGIN project is selected.');
    return this.contextValue;
  }

  async execute(
    command: string,
    extra: string[] = [],
    refreshGraph = false,
    contextOverride?: NginContext,
    options: ExecuteOptions = {}
  ): Promise<CliResult | undefined> {
    const context = contextOverride ?? this.requireContext();
    const operation = async (token: vscode.CancellationToken): Promise<CliResult | undefined> => {
        const startedAt = Date.now();
        this.busyValue = command;
        this.busyProjectManifestValue = context.projectManifest;
        this.emit();
        try {
          const result = await this.cli.run(
            lifecycleArguments(command, context, extra),
            context.workspaceFolder,
            {
              cwd: path.dirname(context.projectManifest),
              token,
              requireTrust: true,
              exclusive: true,
              revealOutput: vscode.workspace.getConfiguration('ngin').get<boolean>('revealOutputOnRun', false),
              ...(['configure', 'build', 'stage', 'run', 'test', 'benchmark', 'publish'].includes(command)
                ? { presentation: 'lifecycle' as const, label: context.projectName }
                : {})
            }
          );
          this.applyDiagnostics(result.diagnostics, path.dirname(context.projectManifest));
          this.applyDiagnosticCollection(this.compilerDiagnostics,
            parseCompilerDiagnostics(`${result.stdout}\n${result.stderr}`), path.dirname(context.projectManifest), 'Compiler');
          if (context.projectManifest === this.contextValue?.projectManifest
            && ['configure', 'build', 'stage', 'run', 'test', 'benchmark'].includes(command)) {
            this.configuredValue = true;
            this.configurationInvalidatedAt = 0;
          }
          if (refreshGraph && context.projectManifest === this.contextValue?.projectManifest) await this.refreshGraph(false);
          const completedAt = Date.now();
          this.lastOperationValue = {
            projectManifest: context.projectManifest, command, state: 'succeeded', completedAt,
            durationMs: completedAt - startedAt
          };
          return result;
        } catch (error) {
          this.lastOperationValue = {
            projectManifest: context.projectManifest, command, state: 'failed', completedAt: Date.now(),
            durationMs: Date.now() - startedAt,
            message: error instanceof Error ? error.message : String(error)
          };
          if (error instanceof CliFailure) {
            this.applyDiagnostics(error.result.diagnostics, path.dirname(context.projectManifest));
            this.applyDiagnosticCollection(this.compilerDiagnostics,
              parseCompilerDiagnostics(`${error.result.stdout}\n${error.result.stderr}`), path.dirname(context.projectManifest), 'Compiler');
          }
          if (options.announceFailure !== false) void this.showFailure(command, context.projectName);
          return undefined;
        } finally {
          this.busyValue = undefined;
          this.busyProjectManifestValue = undefined;
          this.emit();
        }
    };
    if (options.progress === false || options.token) {
      if (options.token) return operation(options.token);
      const source = new vscode.CancellationTokenSource();
      try {
        return await operation(source.token);
      } finally {
        source.dispose();
      }
    }
    return vscode.window.withProgress(
      { location: vscode.ProgressLocation.Window, title: operationLabel(command, context.projectName), cancellable: true },
      async (_progress, token) => operation(token)
    );
  }
}
