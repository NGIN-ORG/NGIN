import * as vscode from 'vscode';
import type { NginController } from '../core/controller';

export class StatusBarController implements vscode.Disposable {
  private readonly project = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 100);
  private readonly selection = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 99);
  private readonly build = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 98);
  private readonly run = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 97);
  private readonly subscription: vscode.Disposable;
  private readonly configurationSubscription: vscode.Disposable;

  constructor(private readonly controller: NginController) {
    this.project.command = 'ngin.selectProject';
    this.selection.command = 'ngin.selectConfiguration';
    this.build.command = 'ngin.build';
    this.run.command = 'ngin.run';
    this.subscription = controller.onDidChange(() => this.update());
    this.configurationSubscription = vscode.workspace.onDidChangeConfiguration(event => {
      if (event.affectsConfiguration('ngin.statusBar.enabled')) this.update();
    });
    this.update();
  }

  private update(): void {
    const snapshot = this.controller.snapshot;
    const enabled = vscode.workspace.getConfiguration('ngin').get<boolean>('statusBar.enabled', true);
    if (!enabled || !snapshot.context) {
      this.project.hide();
      this.selection.hide();
      this.build.hide();
      this.run.hide();
      return;
    }
    const context = snapshot.context;
    this.project.text = snapshot.graphError ? `$(warning) ${context.projectName}` : `$(project) ${context.projectName}`;
    this.project.tooltip = snapshot.graphError ?? context.projectManifest;
    this.selection.text = `$(settings) ${context.configuration} · ${context.target} · ${context.toolchain}`;
    const options = Object.entries(context.options).map(([name, value]) => `${name}=${value}`).join(', ');
    this.selection.tooltip = `Configuration: ${context.configuration}\nTarget: ${context.target}\nToolchain: ${context.toolchain}${options ? `\nOptions: ${options}` : ''}\nOutput: ${context.outputDirectory}`;
    this.build.text = snapshot.busy ? `$(loading~spin) ${snapshot.busy}` : '$(tools) Build';
    this.build.tooltip = `Build ${context.projectName}`;
    this.run.text = '$(play) Run';
    this.run.tooltip = `Stage and run ${context.projectName}`;
    this.project.show();
    this.selection.show();
    this.build.show();
    this.run.show();
  }

  dispose(): void {
    this.subscription.dispose();
    this.configurationSubscription.dispose();
    this.project.dispose();
    this.selection.dispose();
    this.build.dispose();
    this.run.dispose();
  }
}
