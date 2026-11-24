import * as vscode from 'vscode';
import type { NginController } from '../core/controller';
import type { GraphTestRegistration, ProjectCandidate } from '../model';
import type { SourceAnalysisProvider } from './sourceAnalysis';

function testId(project: ProjectCandidate): string {
  return project.id ?? project.manifest;
}

function testOutput(value: string): string {
  return value.replace(/\r?\n/g, '\r\n');
}

export class NginTestingProvider implements vscode.Disposable {
  private readonly tests = vscode.tests.createTestController('ngin.tests', 'NGIN');
  private readonly projects = new Map<string, ProjectCandidate>();
  private readonly nativeTests = new WeakMap<vscode.TestItem, { project: ProjectCandidate; name: string }>();
  private readonly cmakeTests = new Map<string, { project: ProjectCandidate; name: string }>();
  private readonly disposables: vscode.Disposable[];

  constructor(private readonly controller: NginController, private readonly analysis: SourceAnalysisProvider) {
    this.tests.createRunProfile('Run', vscode.TestRunProfileKind.Run,
      (request, token) => this.run(request, token), true);
    this.tests.createRunProfile('Debug', vscode.TestRunProfileKind.Debug,
      (request, token) => this.debug(request, token), true);
    this.tests.resolveHandler = async item => {
      if (!item) { this.refresh(); return; }
      await this.resolveNativeChildren(item);
    };
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
    this.cmakeTests.clear();
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
      item.description = project.projectSystem === 'CMake'
        ? `CMake · ${context.configurePreset ?? 'select preset'}`
        : `${project.artifactKind ?? 'Executable'} · ${context.configuration}`;
      item.range = new vscode.Range(0, 0, 0, 0);
      if (project.projectSystem !== 'CMake') {
        item.canResolveChildren = true;
        const snapshot = this.controller.snapshot;
        if (snapshot.context?.projectManifest === project.manifest && snapshot.graph)
          this.setRegistrations(item, project, snapshot.graph.tests);
      } else {
        const snapshot = this.controller.cmakeSnapshot(project);
        const testIds = new Set((snapshot?.tests ?? []).map(test => `${id}::${test.name}`));
        item.children.forEach(child => { if (!testIds.has(child.id)) item!.children.delete(child.id); });
        for (const test of snapshot?.tests ?? []) {
          const childId = `${id}::${test.name}`;
          this.cmakeTests.set(childId, { project, name: test.name });
          if (!item.children.get(childId)) {
            item.children.add(this.tests.createTestItem(childId, test.name, vscode.Uri.file(project.directory)));
          }
        }
      }
    }
  }

  private setRegistrations(parent: vscode.TestItem, project: ProjectCandidate, registrations: GraphTestRegistration[]): void {
    const children = registrations.map(test => {
      const name = test.name ?? test.identity;
      const id = JSON.stringify([parent.id, test.identity]);
      const source = test.provenance;
      const uri = vscode.Uri.file(project.manifest);
      const existing = parent.children.get(id);
      const child = existing?.uri?.fsPath === uri.fsPath ? existing : this.tests.createTestItem(id, name, uri);
      const line = Math.max(0, (source?.line ?? 1) - 1);
      const column = Math.max(0, (source?.column ?? 1) - 1);
      child.range = new vscode.Range(line, column, line, column);
      this.nativeTests.set(child, { project, name });
      return child;
    });
    parent.children.replace(children);
  }

  private async resolveNativeChildren(item: vscode.TestItem): Promise<void> {
    const project = this.projects.get(item.id);
    if (!project || project.projectSystem === 'CMake') return;
    const graph = await this.controller.graphForContext(this.analysis.contextForProject(project), true);
    if (graph && this.tests.items.get(item.id) === item) this.setRegistrations(item, project, graph.tests);
  }

  private async selectedItems(request: vscode.TestRunRequest): Promise<vscode.TestItem[]> {
    const excluded = new Set(request.exclude?.map(item => item.id));
    const roots: vscode.TestItem[] = [];
    if (request.include) roots.push(...request.include);
    else this.tests.items.forEach(item => roots.push(item));
    const items = new Map<string, vscode.TestItem>();
    for (const item of roots) {
      if (excluded.has(item.id) || (item.parent && excluded.has(item.parent.id))) continue;
      await this.resolveNativeChildren(item);
      const cmakeSuite = this.projects.get(item.id)?.projectSystem === 'CMake';
      const excludeChild = request.exclude?.some(child => child.parent?.id === item.id);
      if (item.children.size && (!cmakeSuite || excludeChild)) item.children.forEach(child => {
        if (!excluded.has(child.id)) items.set(child.id, child);
      });
      else items.set(item.id, item);
    }
    return [...items.values()];
  }

  private async run(request: vscode.TestRunRequest, token: vscode.CancellationToken): Promise<void> {
    const run = this.tests.createTestRun(request, 'NGIN tests');
    const items = await this.selectedItems(request);
    items.forEach(item => run.enqueued(item));
    try {
      for (const item of items) {
        if (token.isCancellationRequested) {
          run.skipped(item);
          continue;
        }
        const cmakeTest = this.cmakeTests.get(item.id);
        const nativeTest = this.nativeTests.get(item);
        const project = this.projects.get(item.id) ?? cmakeTest?.project ?? nativeTest?.project;
        if (!project) {
          run.skipped(item);
          continue;
        }
        const started = Date.now();
        run.started(item);
        const context = this.analysis.contextForProject(project);
        const result = await this.controller.execute('test', cmakeTest ? ['--test-name', cmakeTest.name] : nativeTest ? [nativeTest.name] : [], false, context, {
          progress: false, announceFailure: false, token
        });
        const duration = Date.now() - started;
        if (result) {
          const output = `${result.stdout}${result.stderr}`;
          if (output) run.appendOutput(testOutput(output), undefined, item);
          run.passed(item, duration);
        } else if (token.isCancellationRequested) {
          run.skipped(item);
        } else {
          const message = this.controller.snapshot.lastOperation?.message ?? `Could not run ${project.name}. Open NGIN Output for details.`;
          run.failed(item, new vscode.TestMessage(message), duration);
        }
      }
    } finally {
      run.end();
    }
  }

  private async debug(request: vscode.TestRunRequest, token: vscode.CancellationToken): Promise<void> {
    const run = this.tests.createTestRun(request, 'Debug NGIN tests');
    const items = await this.selectedItems(request);
    items.forEach(item => run.enqueued(item));
    try {
      for (const item of items) {
        if (token.isCancellationRequested) {
          run.skipped(item);
          continue;
        }
        const cmakeTest = this.cmakeTests.get(item.id);
        const nativeTest = this.nativeTests.get(item);
        const project = this.projects.get(item.id) ?? cmakeTest?.project ?? nativeTest?.project;
        if (!project) {
          run.skipped(item);
          continue;
        }
        if (project.projectSystem === 'CMake') {
          run.errored(item, new vscode.TestMessage('Debugging CMake tests requires an explicit launch contract and is not available yet.'));
          continue;
        }
        run.started(item);
        const sessionKey = `${item.id}:${Date.now()}`;
        let session: vscode.DebugSession | undefined;
        let exitCode: number | undefined;
        const sessionDisposables: vscode.Disposable[] = [];
        const tracker = vscode.debug.registerDebugAdapterTrackerFactory('*', {
          createDebugAdapterTracker: candidate => candidate.configuration.nginTestSession === sessionKey ? {
            onDidSendMessage: message => {
              if (message.type === 'event' && message.event === 'exited') exitCode = message.body?.exitCode;
            }
          } : undefined
        });
        sessionDisposables.push(tracker);
        const completed = new Promise<void>(resolve => {
          const started = vscode.debug.onDidStartDebugSession(value => {
            if (value.configuration.nginTestSession === sessionKey) session = value;
          });
          const terminated = vscode.debug.onDidTerminateDebugSession(value => {
            if (value.configuration.nginTestSession !== sessionKey) return;
            started.dispose();
            terminated.dispose();
            resolve();
          });
          const cancellation = token.onCancellationRequested(() => {
            if (session) void vscode.debug.stopDebugging(session);
          });
          sessionDisposables.push(started, terminated, cancellation);
        });
        const started = await vscode.debug.startDebugging(undefined, {
          type: 'ngin', request: 'launch', name: `NGIN: Debug tests for ${project.name}`,
          project: project.manifest, preStage: true, test: true, testName: nativeTest?.name, nginTestSession: sessionKey
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
