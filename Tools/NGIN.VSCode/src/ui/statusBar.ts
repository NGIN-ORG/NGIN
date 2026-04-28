import * as vscode from 'vscode';
import { NginWorkspaceSnapshot } from '../state/workspaceState';
import { buildStatusBarModel } from './models';

export class NginStatusBarController implements vscode.Disposable {
  private readonly workspaceItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 100);
  private readonly projectItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 99);
  private readonly profileItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 98);
  private readonly configureItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 97);
  private readonly buildItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 96);
  private readonly runItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 95);
  private readonly debugItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 94);

  dispose(): void {
    this.workspaceItem.dispose();
    this.projectItem.dispose();
    this.profileItem.dispose();
    this.configureItem.dispose();
    this.buildItem.dispose();
    this.runItem.dispose();
    this.debugItem.dispose();
  }

  refresh(snapshot: NginWorkspaceSnapshot): void {
    const enabled = vscode.workspace.getConfiguration('ngin').get<boolean>('ui.statusBar.enabled', true);
    const model = buildStatusBarModel(snapshot);

    if (!enabled || !model.visible) {
      this.hideAll();
      return;
    }

    this.apply(this.workspaceItem, model.workspace);
    this.apply(this.projectItem, model.project);
    this.apply(this.profileItem, model.profile);
    this.apply(this.configureItem, model.configure);
    this.apply(this.buildItem, model.build);
    this.apply(this.runItem, model.run);
    this.apply(this.debugItem, model.debug);
  }

  private apply(item: vscode.StatusBarItem, model?: ReturnType<typeof buildStatusBarModel>['workspace']): void {
    if (!model) {
      item.hide();
      return;
    }

    item.text = model.text;
    item.tooltip = model.tooltip;
    item.command = model.arguments?.length
      ? {
          command: model.command,
          title: model.text,
          arguments: model.arguments
        }
      : model.command;
    item.show();
  }

  private hideAll(): void {
    this.workspaceItem.hide();
    this.projectItem.hide();
    this.profileItem.hide();
    this.configureItem.hide();
    this.buildItem.hide();
    this.runItem.hide();
    this.debugItem.hide();
  }
}
