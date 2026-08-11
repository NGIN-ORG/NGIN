import * as vscode from 'vscode';
import type { NginController } from '../core/controller';
import type { SourceAnalysisProvider } from '../providers/sourceAnalysis';
import { projectCanLaunch } from '../core/projectCapabilities';

export class StatusBarController implements vscode.Disposable {
  private readonly target = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 100);
  private readonly configure = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 99);
  private readonly build = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 98);
  private readonly run = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 97);
  private readonly debug = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 96);
  private readonly disposables: vscode.Disposable[];

  constructor(private readonly controller: NginController, private readonly analysis: SourceAnalysisProvider) {
    this.target.name = 'NGIN selected project';
    for (const [item, name, text, command] of [
      [this.configure, 'NGIN Configure', '$(gear) Configure', 'ngin.configure'],
      [this.build, 'NGIN Build', '$(tools) Build', 'ngin.build'],
      [this.run, 'NGIN Run', '$(play) Run', 'ngin.run'],
      [this.debug, 'NGIN Debug', '$(debug-alt) Debug', 'ngin.debug']
    ] as const) {
      item.name = name;
      item.text = text;
      item.tooltip = `${name} the selected project`;
      item.command = command;
    }
    this.disposables = [
      controller.onDidChange(() => void this.update()),
      analysis.onDidChange(() => void this.update()),
      vscode.workspace.onDidChangeConfiguration(event => {
        if (event.affectsConfiguration('ngin.statusBar.enabled')) void this.update();
      })
    ];
    void this.update();
  }

  private async update(): Promise<void> {
    const snapshot = this.controller.snapshot;
    const enabled = vscode.workspace.getConfiguration('ngin').get<boolean>('statusBar.enabled', true);
    if (!enabled || !snapshot.context) {
      this.target.hide();
      this.configure.hide();
      this.build.hide();
      this.run.hide();
      this.debug.hide();
      return;
    }
    const busyProject = snapshot.busyProjectManifest
      ? this.controller.projects.find(project => project.manifest === snapshot.busyProjectManifest)
      : undefined;
    const context = busyProject ? this.analysis.contextForProject(busyProject)
      : snapshot.context;
    const project = busyProject ?? this.controller.activeProject;
    const canLaunch = projectCanLaunch(project, snapshot.graph, snapshot.context.projectManifest);
    const analysis = this.analysis.summary(context.projectManifest);
    const icon = snapshot.busy || analysis.state === 'analyzing' ? 'loading~spin'
      : snapshot.graphError || analysis.state === 'failed' ? 'warning' : 'project';
    this.target.text = `$(${icon}) ${context.projectName} · ${context.configuration}`;
    this.target.command = snapshot.busy ? 'ngin.cancel' : 'ngin.setDefaultProject';
    this.target.tooltip = [
      busyProject ? 'Project with the active NGIN operation'
        : 'Selected NGIN project',
      context.projectManifest,
      `Platform target: ${context.target}`,
      `Toolchain: ${context.toolchain}`,
      snapshot.busy ? `Operation: ${snapshot.busy}` : undefined,
      analysis.message ? `Analysis: ${analysis.message}` : undefined,
      snapshot.busy ? 'Click to cancel the active operation.' : 'Click to select another project.'
    ].filter(Boolean).join('\n');
    this.target.show();
    for (const [item, command] of [
      [this.configure, 'ngin.configure'], [this.build, 'ngin.build'],
      [this.run, 'ngin.run'], [this.debug, 'ngin.debug']
    ] as const) {
      if (snapshot.busy || !project || ((command === 'ngin.run' || command === 'ngin.debug') && !canLaunch)) {
        item.hide();
      } else {
        item.command = { command, title: `NGIN ${command.slice('ngin.'.length)}`, arguments: [project] };
        item.show();
      }
    }
  }

  dispose(): void {
    this.disposables.forEach(value => value.dispose());
    this.target.dispose();
    this.configure.dispose();
    this.build.dispose();
    this.run.dispose();
    this.debug.dispose();
  }
}
