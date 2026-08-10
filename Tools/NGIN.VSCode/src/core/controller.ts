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
import { projectOutputDirectory } from './paths';
import { parseCompilerDiagnostics } from './diagnostics';

interface PersistedSelection {
  projectManifest?: string;
  configuration?: string;
  target?: string;
  toolchain?: string;
  preset?: string;
  options?: Record<string, string>;
  projects?: Record<string, Omit<PersistedSelection, 'projects' | 'projectManifest'>>;
}

const stateKey = 'ngin.activeSelection';

export class NginController implements vscode.Disposable {
  private readonly changed = new vscode.EventEmitter<ContextSnapshot>();
  private readonly disposables: vscode.Disposable[] = [];
  private discoveriesValue: DiscoveryResult[] = [];
  private project?: ProjectCandidate;
  private contextValue?: NginContext;
  private graphValue?: CompositionGraph;
  private graphErrorValue?: string;
  private busyValue?: string;
  private graphGeneration = 0;
  private configuredValue = false;
  private configurationInvalidatedAt = 0;
  private lastOperationValue?: ContextSnapshot['lastOperation'];
  private projectSelections: Record<string, Omit<PersistedSelection, 'projects' | 'projectManifest'>> = {};

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
      busy: this.busyValue, configured: this.configuredValue, lastOperation: this.lastOperationValue
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

  async initialize(): Promise<void> {
    await this.refreshDiscovery(false);
    const persisted = this.extensionContext.workspaceState.get<PersistedSelection>(stateKey) ?? {};
    this.projectSelections = persisted.projects ?? {};
    const active = vscode.window.activeTextEditor?.document.uri.fsPath;
    const initial = chooseInitialProject(this.discoveriesValue, persisted.projectManifest, active);
    if (initial) await this.selectProject(initial, this.projectSelections[initial.manifest] ?? persisted, false);
    else this.emit();
  }

  async refreshDiscovery(preserveSelection = true): Promise<void> {
    const previous = this.contextValue;
    this.discoveriesValue = await discoverAll();
    const candidate = preserveSelection
      ? chooseInitialProject(this.discoveriesValue, previous?.projectManifest, vscode.window.activeTextEditor?.document.uri.fsPath)
      : undefined;
    if (candidate) await this.selectProject(candidate, previous, false);
    else this.emit();
  }

  contextForProject(project: ProjectCandidate, selection: Partial<PersistedSelection | NginContext> = {}): NginContext {
    const remembered = this.projectSelections[project.manifest] ?? {};
    const choices = project.workspaceChoices;
    const configuration = selection.configuration ?? remembered.configuration
      ?? choices?.defaults.configuration
      ?? choices?.configurations[0]
      ?? 'Debug';
    const target = selection.target ?? remembered.target ?? choices?.defaults.target ?? choices?.targets[0] ?? 'host';
    const toolchain = selection.toolchain ?? remembered.toolchain ?? choices?.defaults.toolchain ?? choices?.toolchains[0] ?? 'default';
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
      preset: selection.preset ?? remembered.preset,
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

  async updateSelection(change: Partial<Pick<NginContext, 'configuration' | 'target' | 'toolchain' | 'preset' | 'options'>>): Promise<void> {
    if (!this.project || !this.contextValue) return;
    const defined = Object.fromEntries(Object.entries(change).filter(([, value]) => value !== undefined));
    this.contextValue = this.contextForProject(this.project, { ...this.contextValue, ...defined });
    this.graphValue = undefined;
    this.graphErrorValue = undefined;
    this.configuredValue = false;
    await this.persist();
    this.emit();
    await this.refreshGraph(true);
  }

  private async persist(): Promise<void> {
    const context = this.contextValue;
    if (!context) return;
    this.projectSelections[context.projectManifest] = {
      configuration: context.configuration,
      target: context.target,
      toolchain: context.toolchain,
      preset: context.preset,
      options: { ...context.options }
    };
    await this.extensionContext.workspaceState.update(stateKey, {
      projectManifest: context.projectManifest,
      configuration: context.configuration,
      target: context.target,
      toolchain: context.toolchain,
      preset: context.preset,
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
      this.emit();
      if (announceErrors) void vscode.window.showErrorMessage(message);
      return undefined;
    }
  }

  async graphForContext(context: NginContext, announceErrors = false): Promise<CompositionGraph | undefined> {
    if (context.projectManifest === this.contextValue?.projectManifest) {
      return this.graphValue ?? this.refreshGraph(announceErrors);
    }
    try {
      const result = await this.cli.run(
        ['graph', ...selectionArguments(context), '--format', 'json'],
        context.workspaceFolder,
        { cwd: path.dirname(context.projectManifest) }
      );
      this.applyDiagnostics(result.diagnostics, path.dirname(context.projectManifest));
      return parseCompositionGraph(result.stdout);
    } catch (error) {
      if (error instanceof CliFailure) this.applyDiagnostics(error.result.diagnostics, path.dirname(context.projectManifest));
      if (announceErrors) void vscode.window.showErrorMessage(error instanceof Error ? error.message : String(error));
      return undefined;
    }
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
      if (announce) void vscode.window.showInformationMessage(`${path.basename(manifest)} is valid.`);
      return true;
    } catch (error) {
      if (error instanceof CliFailure) this.applyDiagnostics(error.result.diagnostics, path.dirname(manifest));
      if (announce) void vscode.window.showErrorMessage(error instanceof Error ? error.message : String(error));
      return false;
    }
  }

  requireContext(): NginContext {
    if (!this.contextValue) throw new Error('No NGIN project is selected.');
    return this.contextValue;
  }

  async execute(
    command: string,
    extra: string[] = [],
    refreshGraph = false,
    contextOverride?: NginContext
  ): Promise<CliResult | undefined> {
    const context = contextOverride ?? this.requireContext();
    return vscode.window.withProgress(
      { location: vscode.ProgressLocation.Notification, title: `NGIN: ${command} ${context.projectName}`, cancellable: true },
      async (_progress, token) => {
        this.busyValue = command;
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
              revealOutput: vscode.workspace.getConfiguration('ngin').get<boolean>('revealOutputOnRun', false)
            }
          );
          this.applyDiagnostics(result.diagnostics, path.dirname(context.projectManifest));
          this.applyDiagnosticCollection(this.compilerDiagnostics,
            parseCompilerDiagnostics(`${result.stdout}\n${result.stderr}`), path.dirname(context.projectManifest), 'Compiler');
          if (context.projectManifest === this.contextValue?.projectManifest
            && ['configure', 'build', 'stage', 'run', 'test'].includes(command)) {
            this.configuredValue = true;
            this.configurationInvalidatedAt = 0;
          }
          if (refreshGraph && context.projectManifest === this.contextValue?.projectManifest) await this.refreshGraph(false);
          this.lastOperationValue = { projectManifest: context.projectManifest, command, state: 'succeeded', completedAt: Date.now() };
          void vscode.window.showInformationMessage(`NGIN ${command} completed for ${context.projectName}.`);
          return result;
        } catch (error) {
          this.lastOperationValue = {
            projectManifest: context.projectManifest, command, state: 'failed', completedAt: Date.now(),
            message: error instanceof Error ? error.message : String(error)
          };
          if (error instanceof CliFailure) {
            this.applyDiagnostics(error.result.diagnostics, path.dirname(context.projectManifest));
            this.applyDiagnosticCollection(this.compilerDiagnostics,
              parseCompilerDiagnostics(`${error.result.stdout}\n${error.result.stderr}`), path.dirname(context.projectManifest), 'Compiler');
          }
          this.cli.showOutput();
          void vscode.window.showErrorMessage(error instanceof Error ? error.message : String(error));
          return undefined;
        } finally {
          this.busyValue = undefined;
          this.emit();
        }
      }
    );
  }
}
