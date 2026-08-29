import * as path from 'node:path';
import * as vscode from 'vscode';
import { isTransientAnalysisFailure } from '../core/analysisPolicy';
import { parseActionDiagnostics } from '../core/actionDiagnostics';
import { CliFailure, type NginCli } from '../core/cli';
import { lifecycleArguments, selectionArguments, dependencyLockPath } from '../core/commandArguments';
import type { NginController } from '../core/controller';
import { parseCompositionGraph } from '../core/graph';
import type { ActionDiagnostic, NginContext, ProjectCandidate } from '../model';

export interface AnalysisSummary {
  state: 'idle' | 'analyzing' | 'ready' | 'failed' | 'disabled';
  diagnostics: number;
  message?: string;
  completedAt?: number;
}

const ownerStateKey = 'ngin.sourceOwners';
const toolingStateKey = 'ngin.toolingConsent';

function sourceDocument(document: vscode.TextDocument): boolean {
  if (document.uri.scheme !== 'file') return false;
  if (['c', 'cpp', 'cuda-cpp', 'objective-c', 'objective-cpp'].includes(document.languageId)) return true;
  return ['.c', '.cc', '.cpp', '.cxx', '.m', '.mm', '.h', '.hh', '.hpp', '.hxx', '.ixx', '.cppm']
    .includes(path.extname(document.uri.fsPath).toLowerCase());
}

async function fileExists(candidate: string): Promise<boolean> {
  try {
    return (await vscode.workspace.fs.stat(vscode.Uri.file(candidate))).type === vscode.FileType.File;
  } catch {
    return false;
  }
}

function severity(value: ActionDiagnostic['severity']): vscode.DiagnosticSeverity {
  if (value === 'error') return vscode.DiagnosticSeverity.Error;
  if (value === 'warning') return vscode.DiagnosticSeverity.Warning;
  return vscode.DiagnosticSeverity.Information;
}

function vscodeDiagnostic(value: ActionDiagnostic): vscode.Diagnostic {
  const start = new vscode.Position(Math.max(0, value.range.start.line - 1), Math.max(0, value.range.start.column - 1));
  const end = new vscode.Position(Math.max(start.line, value.range.end.line - 1), Math.max(0, value.range.end.column - 1));
  const diagnostic = new vscode.Diagnostic(new vscode.Range(start, end), value.message, severity(value.severity));
  diagnostic.source = value.source;
  diagnostic.code = value.code;
  return diagnostic;
}

export class SourceAnalysisProvider implements vscode.Disposable {
  private readonly disposables: vscode.Disposable[] = [];
  private readonly timers = new Map<string, NodeJS.Timeout>();
  private readonly activeByProject = new Map<string, vscode.CancellationTokenSource>();
  private readonly jobsByProject = new Map<string, Promise<void>>();
  private readonly graphCache = new Map<string, ReturnType<typeof parseCompositionGraph>>();
  private readonly summaries = new Map<string, AnalysisSummary>();
  private readonly actionDiagnosticsByFile = new Map<string, ActionDiagnostic[]>();
  private readonly promptedThisSession = new Set<string>();
  private readonly changed = new vscode.EventEmitter<string>();
  readonly onDidChange = this.changed.event;

  private contextKey(context: NginContext): string {
    return context.projectId ?? context.projectManifest;
  }

  constructor(
    private readonly extensionContext: vscode.ExtensionContext,
    private readonly controller: NginController,
    private readonly cli: NginCli,
    private readonly diagnostics: vscode.DiagnosticCollection
  ) {
    this.disposables.push(
      vscode.workspace.onDidOpenTextDocument(document => this.schedule(document, 0)),
      vscode.workspace.onDidSaveTextDocument(document => this.schedule(document)),
      vscode.workspace.onDidCloseTextDocument(document => this.close(document)),
      vscode.workspace.onDidChangeConfiguration(event => {
        if (event.affectsConfiguration('ngin.analysis')) this.scheduleVisibleEditors();
      }),
      controller.onDidChange(snapshot => {
        if (!snapshot.context) return;
        const key = this.contextKey(snapshot.context);
        if (snapshot.graph) this.graphCache.set(key, snapshot.graph);
        else this.graphCache.delete(key);
      })
    );
  }

  dispose(): void {
    for (const timer of this.timers.values()) clearTimeout(timer);
    for (const active of this.activeByProject.values()) active.cancel();
    this.disposables.forEach(value => value.dispose());
    this.changed.dispose();
  }

  initialize(): void {
    this.scheduleVisibleEditors();
  }

  summary(projectManifest: string): AnalysisSummary {
    return this.summaries.get(projectManifest) ?? { state: 'idle', diagnostics: 0 };
  }

  actionDiagnostics(uri: vscode.Uri, diagnostics: readonly vscode.Diagnostic[]): ActionDiagnostic[] {
    const values = this.actionDiagnosticsByFile.get(path.resolve(uri.fsPath)) ?? [];
    if (!diagnostics.length) return values;
    return values.filter(value => diagnostics.some(diagnostic =>
      diagnostic.source === value.source
      && diagnostic.message === value.message
      && String(diagnostic.code ?? '') === String(value.code ?? '')));
  }

  invalidate(projectManifest?: string): void {
    if (projectManifest) {
      this.graphCache.delete(projectManifest);
      for (const project of this.controller.projects.filter(value => value.manifest === projectManifest)) {
        if (project.id) this.graphCache.delete(project.id);
      }
    }
    else this.graphCache.clear();
  }

  private setSummary(projectManifest: string, summary: AnalysisSummary): void {
    this.summaries.set(projectManifest, summary);
    this.changed.fire(projectManifest);
  }

  private mode(): string {
    return vscode.workspace.getConfiguration('ngin').get<string>('analysis.mode', 'openAndSave');
  }

  private scheduleVisibleEditors(): void {
    for (const editor of vscode.window.visibleTextEditors) this.schedule(editor.document, 0);
  }

  private schedule(document: vscode.TextDocument, delay?: number): void {
    if (!sourceDocument(document) || this.mode() === 'off') return;
    const key = document.uri.toString();
    const existing = this.timers.get(key);
    if (existing) clearTimeout(existing);
    const configuredDelay = vscode.workspace.getConfiguration('ngin').get<number>('analysis.debounceMs', 400);
    this.timers.set(key, setTimeout(() => {
      this.timers.delete(key);
      void this.analyzeDocument(document);
    }, delay ?? configuredDelay));
  }

  private close(document: vscode.TextDocument): void {
    const key = document.uri.toString();
    const timer = this.timers.get(key);
    if (timer) clearTimeout(timer);
    this.timers.delete(key);
    this.diagnostics.delete(document.uri);
    this.actionDiagnosticsByFile.delete(path.resolve(document.uri.fsPath));
  }

  contextForProject(project: ProjectCandidate): NginContext {
    const current = this.controller.snapshot.context;
    return current && (current.projectId && project.id
      ? current.projectId === project.id
      : current.projectManifest === project.manifest)
      ? current
      : this.controller.contextForProject(project);
  }

  async projectForFile(file: string, prompt = true): Promise<ProjectCandidate | undefined> {
    const candidates = this.controller.projectsForFile(file);
    if (candidates.length === 0) return undefined;
    const deepest = candidates.filter(candidate => candidate.directory.length === candidates[0].directory.length);
    if (deepest.length === 1) return deepest[0];
    const owners = this.extensionContext.workspaceState.get<Record<string, string>>(ownerStateKey, {});
    const remembered = deepest.find(candidate => (candidate.id ?? candidate.manifest) === owners[file]);
    if (remembered) return remembered;
    if (!prompt) return undefined;
    const selected = await vscode.window.showQuickPick(deepest.map(candidate => ({
      label: candidate.name,
      description: path.relative(candidate.directory, candidate.manifest),
      candidate
    })), { title: `Choose the NGIN project that owns ${path.basename(file)}`, placeHolder: 'This choice is remembered for this file.' });
    if (!selected) return undefined;
    await this.extensionContext.workspaceState.update(ownerStateKey, {
      ...owners, [file]: selected.candidate.id ?? selected.candidate.manifest
    });
    return selected.candidate;
  }

  private async graph(context: NginContext) {
    const key = this.contextKey(context);
    const cached = this.graphCache.get(key);
    if (cached) return cached;
    const result = await this.cli.run(
      ['graph', ...selectionArguments(context), '--format', 'json'],
      context.workspaceFolder,
      { cwd: path.dirname(context.projectManifest), requireTrust: true }
    );
    const graph = parseCompositionGraph(result.stdout);
    this.graphCache.set(key, graph);
    return graph;
  }

  private async ensureTooling(context: NginContext): Promise<boolean> {
    if (!vscode.workspace.isTrusted) return false;
    const key = this.contextKey(context);
    const consent = this.extensionContext.workspaceState.get<Record<string, boolean>>(toolingStateKey, {});
    const decision = consent[key] ?? consent[context.projectManifest];
    if (decision === false) return false;
    if (decision !== true) {
      if (this.promptedThisSession.has(key)) return false;
      this.promptedThisSession.add(key);
      const answer = await vscode.window.showInformationMessage(
        `${context.projectName} provides analyzers and formatters. Enable them for this workspace?`,
        'Enable Analyzers and Formatters',
        'Not Now',
        "Don't Ask Again"
      );
      const enabled = answer === 'Enable Analyzers and Formatters';
      if (enabled) await this.extensionContext.workspaceState.update(toolingStateKey, { ...consent, [key]: true });
      if (answer === "Don't Ask Again") {
        await this.extensionContext.workspaceState.update(toolingStateKey, { ...consent, [key]: false });
      }
      if (!enabled) {
        this.setSummary(key, { state: 'disabled', diagnostics: 0, message: 'Analyzers are not enabled' });
        return false;
      }
    }
    if (await fileExists(dependencyLockPath(context))) return true;
    await this.createLock(context);
    return true;
  }

  private async createLock(context: NginContext): Promise<void> {
    await this.cli.run(lifecycleArguments('lock', context, ['--quiet']), context.workspaceFolder, {
      cwd: path.dirname(context.projectManifest), requireTrust: true, exclusive: true
    });
  }

  private async run(context: NginContext, files: string[]): Promise<ActionDiagnostic[]> {
    const key = this.contextKey(context);
    const token = new vscode.CancellationTokenSource();
    this.activeByProject.get(key)?.cancel();
    this.activeByProject.set(key, token);
    this.setSummary(key, { state: 'analyzing', diagnostics: this.summary(key).diagnostics });
    try {
      const extra = files.flatMap(file => ['--file', file]);
      const result = await this.cli.run(
        lifecycleArguments('analyze', context, [...extra, '--format', 'json', '--quiet']),
        context.workspaceFolder,
        { cwd: path.dirname(context.projectManifest), requireTrust: true, token: token.token }
      );
      this.controller.markConfigured(context);
      return parseActionDiagnostics(result.stdout).diagnostics;
    } finally {
      if (this.activeByProject.get(key) === token) this.activeByProject.delete(key);
      token.dispose();
    }
  }

  private publish(values: ActionDiagnostic[], replaceAll: boolean): void {
    const grouped = new Map<string, vscode.Diagnostic[]>();
    for (const value of values) {
      const file = path.resolve(value.file);
      const list = grouped.get(file) ?? [];
      list.push(vscodeDiagnostic(value));
      grouped.set(file, list);
    }
    if (replaceAll) {
      this.diagnostics.clear();
      this.actionDiagnosticsByFile.clear();
    }
    for (const [file, diagnostics] of grouped) {
      this.diagnostics.set(vscode.Uri.file(file), diagnostics);
      this.actionDiagnosticsByFile.set(file, values.filter(value => path.resolve(value.file) === file));
    }
  }

  private async executeNow(context: NginContext, files: string[], replaceAll: boolean, announceFailures: boolean): Promise<void> {
    const key = this.contextKey(context);
    if (!await this.ensureTooling(context)) return;
    try {
      const values = await this.run(context, files);
      if (files.length === 1 && !values.some(value => path.resolve(value.file) === path.resolve(files[0]))) {
        this.diagnostics.delete(vscode.Uri.file(files[0]));
        this.actionDiagnosticsByFile.delete(path.resolve(files[0]));
      }
      this.publish(values, replaceAll);
      this.setSummary(key, {
        state: 'ready', diagnostics: values.length, completedAt: Date.now(),
        message: values.length ? `${values.length} problem${values.length === 1 ? '' : 's'}` : 'No problems'
      });
    } catch (error) {
      if (isTransientAnalysisFailure(error)) {
        this.setSummary(key, {
          state: 'idle', diagnostics: this.summary(key).diagnostics,
          message: 'Waiting for the active NGIN operation'
        });
        this.scheduleVisibleEditors();
        return;
      }
      if (error instanceof CliFailure && /dependency lock (?:does not match|is invalid)|requires a locked PackageInstance/iu.test(error.message)) {
        const answer = await vscode.window.showWarningMessage(
          `Project tooling for ${context.projectName} has an outdated dependency lock.`,
          'Refresh Tooling Lock'
        );
        if (answer === 'Refresh Tooling Lock') {
          await this.createLock(context);
          return this.executeNow(context, files, replaceAll, announceFailures);
        }
      }
      this.setSummary(key, {
        state: 'failed', diagnostics: this.summary(key).diagnostics,
        message: error instanceof Error ? error.message : String(error)
      });
      if (announceFailures && !/cancel/iu.test(error instanceof Error ? error.message : String(error))) {
        const action = await vscode.window.showErrorMessage(
          `Analysis failed for ${context.projectName}. Check NGIN Output for details.`,
          'Show Output'
        );
        if (action === 'Show Output') this.cli.showOutput();
      }
    }
  }

  private async execute(context: NginContext, files: string[], replaceAll: boolean, announceFailures: boolean): Promise<void> {
    const key = this.contextKey(context);
    this.activeByProject.get(key)?.cancel();
    const previous = this.jobsByProject.get(key);
    const job = (async () => {
      if (previous) await previous.catch(() => undefined);
      await this.executeNow(context, files, replaceAll, announceFailures);
    })();
    this.jobsByProject.set(key, job);
    try {
      await job;
    } finally {
      if (this.jobsByProject.get(key) === job) this.jobsByProject.delete(key);
    }
  }

  async analyzeDocument(document: vscode.TextDocument): Promise<void> {
    if (!sourceDocument(document) || this.mode() === 'off') return;
    if (this.controller.snapshot.busy) {
      this.schedule(document, 500);
      return;
    }
    const project = await this.projectForFile(document.uri.fsPath, true);
    if (!project) {
      this.diagnostics.delete(document.uri);
      return;
    }
    if (project.projectSystem === 'CMake') {
      this.diagnostics.delete(document.uri);
      this.setSummary(project.id ?? project.manifest, { state: 'disabled', diagnostics: 0, message: 'NGIN Actions do not apply to CMake projects' });
      return;
    }
    const context = this.contextForProject(project);
    try {
      const graph = await this.graph(context);
      if (!graph.actions.some(action => action.kind === 'Analyze')) {
        this.diagnostics.delete(document.uri);
        return;
      }
      await this.execute(context, [document.uri.fsPath], false, false);
    } catch (error) {
      if (isTransientAnalysisFailure(error)) {
        const key = this.contextKey(context);
        this.setSummary(key, {
          state: 'idle', diagnostics: this.summary(key).diagnostics,
          message: 'Waiting for the active NGIN operation'
        });
        this.schedule(document, 750);
        return;
      }
      this.setSummary(this.contextKey(context), { state: 'failed', diagnostics: 0, message: error instanceof Error ? error.message : String(error) });
    }
  }

  async analyzeProject(project?: ProjectCandidate): Promise<void> {
    const selected = project ?? this.controller.launchProduct;
    if (!selected) throw new Error('No NGIN project is selected.');
    if (selected.projectSystem === 'CMake') {
      void vscode.window.showInformationMessage(`${selected.name} is a CMake project; use its configured C++ tooling.`);
      return;
    }
    const context = this.contextForProject(selected);
    const graph = await this.graph(context);
    if (!graph.actions.some(action => action.kind === 'Analyze')) {
      void vscode.window.showInformationMessage(`${selected.name} declares no Analyze tooling.`);
      return;
    }
    await this.execute(context, [], true, true);
  }

  async enableProjectTooling(project?: ProjectCandidate): Promise<void> {
    const selected = project ?? this.controller.launchProduct;
    if (!selected) throw new Error('No NGIN project is selected.');
    const context = this.contextForProject(selected);
    const key = this.contextKey(context);
    const consent = this.extensionContext.workspaceState.get<Record<string, boolean>>(toolingStateKey, {});
    await this.extensionContext.workspaceState.update(toolingStateKey, { ...consent, [key]: true });
    this.promptedThisSession.add(key);
    await this.createLock(context);
    this.setSummary(key, { state: 'idle', diagnostics: 0, message: 'Project tooling enabled' });
    this.scheduleVisibleEditors();
  }
}
