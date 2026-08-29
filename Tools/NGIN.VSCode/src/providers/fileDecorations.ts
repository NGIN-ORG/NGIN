import * as path from 'node:path';
import * as vscode from 'vscode';
import type { NginController } from '../core/controller';
import { plausibleBuildKind } from '../core/projectFiles';

function key(value: string): string {
  const resolved = path.resolve(value);
  return process.platform === 'win32' ? resolved.toLowerCase() : resolved;
}

export class NginFileDecorationProvider implements vscode.FileDecorationProvider, vscode.Disposable {
  private readonly changed = new vscode.EventEmitter<vscode.Uri | vscode.Uri[] | undefined>();
  readonly onDidChangeFileDecorations = this.changed.event;
  private readonly subscriptions: vscode.Disposable[];

  constructor(private readonly controller: NginController) {
    this.subscriptions = [
      controller.onDidChange(() => this.changed.fire(undefined)),
      vscode.workspace.onDidChangeConfiguration(event => {
        if (event.affectsConfiguration('ngin.decorations.mode')) this.changed.fire(undefined);
      })
    ];
  }

  provideFileDecoration(uri: vscode.Uri): vscode.ProviderResult<vscode.FileDecoration> {
    if (uri.scheme !== 'file') return undefined;
    const mode = vscode.workspace.getConfiguration('ngin').get<string>('decorations.mode', 'minimal');
    if (mode === 'off') return undefined;
    const owners = this.controller.projectsForFile(uri.fsPath);
    if (owners.length === 0) return undefined;
    const context = this.controller.snapshot.context;
    const graph = this.controller.snapshot.graph;
    if (!context || !graph || !owners.some(owner => owner.manifest === context.projectManifest)) {
      if (mode !== 'detailed') return undefined;
      return { badge: 'N', tooltip: `Owned by ${owners.map(owner => owner.name).join(', ')}`, propagate: false };
    }
    const projectDirectory = path.dirname(context.projectManifest);
    const item = graph.buildItems.find(candidate =>
      ['Source', 'Header', 'CxxModule', 'Resource'].includes(candidate.kind)
      && key(path.resolve(projectDirectory, candidate.path)) === key(uri.fsPath));
    if (item?.generated) {
      return { badge: 'G', tooltip: 'Generated NGIN build input', color: new vscode.ThemeColor('charts.purple'), propagate: false };
    }
    if (item) {
      if (mode !== 'detailed') return undefined;
      return { badge: '✓', tooltip: `${item.kind} included in ${graph.product.name}`, color: new vscode.ThemeColor('testing.iconPassed'), propagate: false };
    }
    if (mode !== 'detailed' || !plausibleBuildKind(uri.fsPath)) return undefined;
    return { badge: '–', tooltip: `Not selected by ${graph.product.name}`, color: new vscode.ThemeColor('disabledForeground'), propagate: false };
  }

  dispose(): void {
    this.subscriptions.forEach(subscription => subscription.dispose());
    this.changed.dispose();
  }
}
