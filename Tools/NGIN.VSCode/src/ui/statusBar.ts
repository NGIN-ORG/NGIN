import * as vscode from 'vscode';
import type { NginController } from '../core/controller';
import { statusPresentation } from '../core/statusPresentation';
import type { SourceAnalysisProvider } from '../providers/sourceAnalysis';

export class StatusBarController implements vscode.Disposable {
  private readonly item = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 100);
  private readonly disposables: vscode.Disposable[];
  private generation = 0;

  constructor(private readonly controller: NginController, private readonly analysis: SourceAnalysisProvider) {
    this.item.name = 'NGIN project context';
    this.item.command = 'ngin.projectActions';
    this.disposables = [
      controller.onDidChange(() => void this.update()),
      analysis.onDidChange(() => void this.update()),
      vscode.window.onDidChangeActiveTextEditor(() => void this.update()),
      vscode.workspace.onDidChangeConfiguration(event => {
        if (event.affectsConfiguration('ngin.statusBar.enabled')) void this.update();
      })
    ];
    void this.update();
  }

  private async update(): Promise<void> {
    const generation = ++this.generation;
    const enabled = vscode.workspace.getConfiguration('ngin').get<boolean>('statusBar.enabled', true);
    if (!enabled) {
      this.item.hide();
      return;
    }

    const snapshot = this.controller.snapshot;
    const busyProject = snapshot.busyProjectManifest
      ? this.controller.projects.find(project => project.manifest === snapshot.busyProjectManifest)
      : undefined;
    const activeFile = vscode.window.activeTextEditor?.document.uri.fsPath;
    const owner = !busyProject && activeFile ? await this.analysis.projectForFile(activeFile, false) : undefined;
    if (generation !== this.generation) return;
    const project = busyProject ?? owner ?? this.controller.launchProduct;
    if (!project) {
      this.item.hide();
      return;
    }
    const context = snapshot.context?.projectManifest === project.manifest
      ? snapshot.context
      : this.analysis.contextForProject(project);
    const summary = this.analysis.summary(project.manifest);
    const presentation = statusPresentation({
      context,
      project,
      reason: busyProject ? 'operation' : owner ? 'activeFile' : 'launch',
      operation: busyProject ? snapshot.busy : undefined,
      graphError: snapshot.context?.projectManifest === project.manifest ? snapshot.graphError : undefined,
      analysisState: summary.state,
      analysisMessage: summary.message,
      lastOperation: snapshot.lastOperation?.projectManifest === project.manifest ? snapshot.lastOperation : undefined
    });
    this.item.text = presentation.text;
    this.item.tooltip = presentation.tooltip;
    this.item.command = { command: 'ngin.projectActions', title: 'Open NGIN Project Actions', arguments: [project] };
    this.item.accessibilityInformation = { label: presentation.accessibilityLabel, role: 'button' };
    this.item.show();
  }

  dispose(): void {
    this.disposables.forEach(value => value.dispose());
    this.item.dispose();
  }
}
