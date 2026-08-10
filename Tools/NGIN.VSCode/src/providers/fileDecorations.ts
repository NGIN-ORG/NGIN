import * as path from 'node:path';
import * as vscode from 'vscode';
import type { NginController } from '../core/controller';

function key(value: string): string {
  const resolved = path.resolve(value);
  return process.platform === 'win32' ? resolved.toLowerCase() : resolved;
}

export class NginFileDecorationProvider implements vscode.FileDecorationProvider, vscode.Disposable {
  private readonly changed = new vscode.EventEmitter<vscode.Uri | vscode.Uri[] | undefined>();
  readonly onDidChangeFileDecorations = this.changed.event;
  private readonly subscription: vscode.Disposable;

  constructor(private readonly controller: NginController) {
    this.subscription = controller.onDidChange(() => this.changed.fire(undefined));
  }

  provideFileDecoration(uri: vscode.Uri): vscode.ProviderResult<vscode.FileDecoration> {
    if (uri.scheme !== 'file') return undefined;
    const owners = this.controller.projectsForFile(uri.fsPath);
    if (owners.length === 0) return undefined;
    const context = this.controller.snapshot.context;
    const graph = this.controller.snapshot.graph;
    if (!context || !graph || !owners.some(owner => owner.manifest === context.projectManifest)) {
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
      return { badge: '✓', tooltip: `${item.kind} included in ${graph.product.name}`, color: new vscode.ThemeColor('testing.iconPassed'), propagate: false };
    }
    return { badge: '–', tooltip: `Not selected by ${graph.product.name}`, color: new vscode.ThemeColor('disabledForeground'), propagate: false };
  }

  dispose(): void {
    this.subscription.dispose();
    this.changed.dispose();
  }
}
