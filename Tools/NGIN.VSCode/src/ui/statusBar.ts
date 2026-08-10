import * as vscode from 'vscode';
import type { NginController } from '../core/controller';
import type { SourceAnalysisProvider } from '../providers/sourceAnalysis';

export class StatusBarController implements vscode.Disposable {
  private readonly target = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 100);
  private readonly disposables: vscode.Disposable[];

  constructor(private readonly controller: NginController, private readonly analysis: SourceAnalysisProvider) {
    this.target.command = 'ngin.switchBuildTarget';
    this.disposables = [
      controller.onDidChange(() => this.update()),
      analysis.onDidChange(() => this.update()),
      vscode.workspace.onDidChangeConfiguration(event => {
        if (event.affectsConfiguration('ngin.statusBar.enabled')) this.update();
      })
    ];
    this.update();
  }

  private update(): void {
    const snapshot = this.controller.snapshot;
    const enabled = vscode.workspace.getConfiguration('ngin').get<boolean>('statusBar.enabled', true);
    if (!enabled || !snapshot.context) {
      this.target.hide();
      return;
    }
    const context = snapshot.context;
    const analysis = this.analysis.summary(context.projectManifest);
    const icon = snapshot.busy || analysis.state === 'analyzing' ? 'loading~spin'
      : snapshot.graphError || analysis.state === 'failed' ? 'warning' : 'project';
    this.target.text = `$(${icon}) ${context.projectName} · ${context.configuration}`;
    this.target.tooltip = [
      'Pinned NGIN Build/Run target',
      context.projectManifest,
      `Target: ${context.target}`,
      `Toolchain: ${context.toolchain}`,
      snapshot.busy ? `Operation: ${snapshot.busy}` : undefined,
      analysis.message ? `Analysis: ${analysis.message}` : undefined,
      'Click to switch Build target.'
    ].filter(Boolean).join('\n');
    this.target.show();
  }

  dispose(): void {
    this.disposables.forEach(value => value.dispose());
    this.target.dispose();
  }
}
