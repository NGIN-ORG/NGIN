import * as vscode from 'vscode';
import type { NginController } from '../core/controller';
import type { ProjectCandidate } from '../model';
import type { SourceAnalysisProvider } from './sourceAnalysis';

function testId(project: ProjectCandidate): string {
  return project.manifest;
}

function testOutput(value: string): string {
  return value.replace(/\r?\n/g, '\r\n');
}

export class NginTestingProvider implements vscode.Disposable {
  private readonly tests = vscode.tests.createTestController('ngin.tests', 'NGIN');
  private readonly projects = new Map<string, ProjectCandidate>();
  private readonly disposables: vscode.Disposable[];

  constructor(private readonly controller: NginController, private readonly analysis: SourceAnalysisProvider) {
    this.tests.createRunProfile('Run', vscode.TestRunProfileKind.Run,
      (request, token) => this.run(request, token), true);
    this.tests.createRunProfile('Debug', vscode.TestRunProfileKind.Debug,
      (request, token) => this.debug(request, token), true);
    this.tests.refreshHandler = async () => this.controller.refreshDiscovery();
    this.disposables = [controller.onDidChange(() => this.refresh())];
    this.refresh();
  }

  private refresh(): void {
    const discovered = this.controller.projects.filter(project => project.hasTests);
    const ids = new Set(discovered.map(testId));
    this.tests.items.forEach(item => {
      if (!ids.has(item.id)) this.tests.items.delete(item.id);
    });
    this.projects.clear();
    for (const project of discovered) {
      const id = testId(project);
      this.projects.set(id, project);
      let item = this.tests.items.get(id);
      if (!item) {
        item = this.tests.createTestItem(id, project.name, vscode.Uri.file(project.manifest));
        this.tests.items.add(item);
      }
      const context = this.analysis.contextForProject(project);
      item.label = project.name;
      item.description = `${project.artifactKind ?? 'Executable'} · ${context.configuration}`;
      item.range = new vscode.Range(0, 0, 0, 0);
    }
  }

  private selectedItems(request: vscode.TestRunRequest): vscode.TestItem[] {
    const excluded = new Set(request.exclude?.map(item => item.id) ?? []);
    if (request.include) return request.include.filter(item => this.projects.has(item.id) && !excluded.has(item.id));
    const items: vscode.TestItem[] = [];
    this.tests.items.forEach(item => { if (!excluded.has(item.id)) items.push(item); });
    return items;
  }

  private async run(request: vscode.TestRunRequest, token: vscode.CancellationToken): Promise<void> {
    const run = this.tests.createTestRun(request, 'NGIN tests');
    const items = this.selectedItems(request);
    items.forEach(item => run.enqueued(item));
    try {
      for (const item of items) {
        if (token.isCancellationRequested) {
          run.skipped(item);
          continue;
        }
        const project = this.projects.get(item.id);
        if (!project) {
          run.skipped(item);
          continue;
        }
        const started = Date.now();
        run.started(item);
        const context = this.analysis.contextForProject(project);
        const result = await this.controller.execute('test', [], false, context, {
          progress: false, announceFailure: false, token
        });
        const duration = Date.now() - started;
        if (result) {
          const output = `${result.stdout}${result.stderr}`;
          if (output) run.appendOutput(testOutput(output), undefined, item);
          run.passed(item, duration);
        } else if (!token.isCancellationRequested) {
          const message = this.controller.snapshot.lastOperation?.message ?? `Could not run ${project.name}. Open NGIN Output for details.`;
          run.errored(item, new vscode.TestMessage(message), duration);
        }
      }
    } finally {
      run.end();
    }
  }

  private async debug(request: vscode.TestRunRequest, token: vscode.CancellationToken): Promise<void> {
    const run = this.tests.createTestRun(request, 'Debug NGIN tests');
    const items = this.selectedItems(request);
    items.forEach(item => run.enqueued(item));
    try {
      for (const item of items) {
        if (token.isCancellationRequested) {
          run.skipped(item);
          continue;
        }
        const project = this.projects.get(item.id);
        if (!project) {
          run.skipped(item);
          continue;
        }
        run.started(item);
        const sessionKey = `${item.id}:${Date.now()}`;
        let session: vscode.DebugSession | undefined;
        let exitCode: number | undefined;
        const sessionDisposables: vscode.Disposable[] = [];
        const completed = new Promise<void>(resolve => {
          const started = vscode.debug.onDidStartDebugSession(value => {
            if (value.configuration.nginTestSession === sessionKey) session = value;
          });
          const event = vscode.debug.onDidReceiveDebugSessionCustomEvent(value => {
            if (value.session.configuration.nginTestSession === sessionKey && value.event === 'exited') {
              exitCode = typeof value.body?.exitCode === 'number' ? value.body.exitCode : undefined;
            }
          });
          const terminated = vscode.debug.onDidTerminateDebugSession(value => {
            if (value.configuration.nginTestSession !== sessionKey) return;
            started.dispose();
            event.dispose();
            terminated.dispose();
            resolve();
          });
          const cancellation = token.onCancellationRequested(() => {
            if (session) void vscode.debug.stopDebugging(session);
          });
          sessionDisposables.push(started, event, terminated, cancellation);
        });
        const started = await vscode.debug.startDebugging(undefined, {
          type: 'ngin', request: 'run', name: `NGIN: Debug tests for ${project.name}`,
          project: project.manifest, preStage: true, test: true, nginTestSession: sessionKey
        });
        if (!started) {
          sessionDisposables.forEach(disposable => disposable.dispose());
          run.errored(item, new vscode.TestMessage(`Could not start the debugger for ${project.name}.`));
          continue;
        }
        await completed;
        sessionDisposables.forEach(disposable => disposable.dispose());
        if (exitCode === 0) run.passed(item);
        else if (typeof exitCode === 'number') run.failed(item, new vscode.TestMessage(`The test process exited with code ${exitCode}.`));
        else run.skipped(item);
      }
    } finally {
      run.end();
    }
  }

  dispose(): void {
    this.disposables.forEach(disposable => disposable.dispose());
    this.tests.dispose();
  }
}
