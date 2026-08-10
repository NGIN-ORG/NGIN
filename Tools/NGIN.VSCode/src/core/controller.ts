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
import { parseCompositionGraph } from './graph';
import { projectOutputDirectory } from './paths';

interface PersistedSelection {
  projectManifest?: string;
  configuration?: string;
  target?: string;
  toolchain?: string;
  preset?: string;
  options?: Record<string, string>;
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

  readonly onDidChange = this.changed.event;

  constructor(
    private readonly extensionContext: vscode.ExtensionContext,
    readonly cli: NginCli,
    private readonly diagnostics: vscode.DiagnosticCollection
  ) {
    this.disposables.push(
      vscode.workspace.onDidChangeConfiguration(event => {
        if (event.affectsConfiguration('ngin.executable') || event.affectsConfiguration('ngin.build.outputRoot')) {
          void this.refreshDiscovery();
        }
      }),
      vscode.window.onDidChangeActiveTextEditor(editor => {
        if (!editor || !vscode.workspace.getConfiguration('ngin').get<boolean>('followActiveEditor', true)) return;
        const candidate = this.projects.find(item => editor.document.uri.fsPath === item.manifest
          || editor.document.uri.fsPath.startsWith(item.directory + path.sep));
        if (candidate && candidate.manifest !== this.project?.manifest) void this.selectProject(candidate);
      })
    );
  }

  dispose(): void {
    this.changed.dispose();
    this.disposables.forEach(disposable => disposable.dispose());
  }

  get snapshot(): ContextSnapshot {
    return { context: this.contextValue, graph: this.graphValue, graphError: this.graphErrorValue, busy: this.busyValue };
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
    const active = vscode.window.activeTextEditor?.document.uri.fsPath;
    const initial = chooseInitialProject(this.discoveriesValue, persisted.projectManifest, active);
    if (initial) await this.selectProject(initial, persisted, false);
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

  private createContext(project: ProjectCandidate, selection: Partial<PersistedSelection | NginContext>): NginContext {
    const choices = project.workspaceChoices;
    const configuration = selection.configuration
      ?? choices?.defaults.configuration
      ?? choices?.configurations[0]
      ?? 'Debug';
    const target = selection.target ?? choices?.defaults.target ?? choices?.targets[0] ?? 'host';
    const toolchain = selection.toolchain ?? choices?.defaults.toolchain ?? choices?.toolchains[0] ?? 'default';
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
      preset: selection.preset,
      options: { ...(selection.options ?? {}) },
      outputDirectory: projectOutputDirectory(workspaceFolder, project, configuration, target, toolchain, outputRoot)
    };
  }

  async selectProject(
    project: ProjectCandidate,
    selection: Partial<PersistedSelection | NginContext> = {},
    persist = true
  ): Promise<void> {
    this.project = project;
    this.contextValue = this.createContext(project, selection);
    this.graphValue = undefined;
    this.graphErrorValue = undefined;
    if (persist) await this.persist();
    this.emit();
    await this.refreshGraph(false);
  }

  async updateSelection(change: Partial<Pick<NginContext, 'configuration' | 'target' | 'toolchain' | 'preset' | 'options'>>): Promise<void> {
    if (!this.project || !this.contextValue) return;
    const defined = Object.fromEntries(Object.entries(change).filter(([, value]) => value !== undefined));
    this.contextValue = this.createContext(this.project, { ...this.contextValue, ...defined });
    this.graphValue = undefined;
    this.graphErrorValue = undefined;
    await this.persist();
    this.emit();
    await this.refreshGraph(true);
  }

  private async persist(): Promise<void> {
    const context = this.contextValue;
    if (!context) return;
    await this.extensionContext.workspaceState.update(stateKey, {
      projectManifest: context.projectManifest,
      configuration: context.configuration,
      target: context.target,
      toolchain: context.toolchain,
      preset: context.preset,
      options: context.options
    } satisfies PersistedSelection);
  }

  private emit(): void {
    this.changed.fire(this.snapshot);
  }

  refreshPresentation(): void {
    this.emit();
  }

  private applyDiagnostics(diagnostics: NginDiagnostic[], fallbackDirectory: string): void {
    this.diagnostics.clear();
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
      diagnostic.source = 'NGIN';
      const entries = grouped.get(resolved) ?? [];
      entries.push(diagnostic);
      grouped.set(resolved, entries);
    }
    for (const [file, entries] of grouped) this.diagnostics.set(vscode.Uri.file(file), entries);
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
      this.graphErrorValue = undefined;
      this.emit();
      return this.graphValue;
    } catch (error) {
      if (generation !== this.graphGeneration) return this.graphValue;
      const message = error instanceof Error ? error.message : String(error);
      this.graphValue = undefined;
      this.graphErrorValue = message;
      if (error instanceof CliFailure) this.applyDiagnostics(error.result.diagnostics, path.dirname(context.projectManifest));
      this.emit();
      if (announceErrors) void vscode.window.showErrorMessage(message);
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

  async execute(command: string, extra: string[] = [], refreshGraph = false): Promise<CliResult | undefined> {
    const context = this.requireContext();
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
              revealOutput: vscode.workspace.getConfiguration('ngin').get<boolean>('revealOutputOnRun', true)
            }
          );
          if (refreshGraph) await this.refreshGraph(false);
          void vscode.window.showInformationMessage(`NGIN ${command} completed for ${context.projectName}.`);
          return result;
        } catch (error) {
          if (error instanceof CliFailure) this.applyDiagnostics(error.result.diagnostics, path.dirname(context.projectManifest));
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
