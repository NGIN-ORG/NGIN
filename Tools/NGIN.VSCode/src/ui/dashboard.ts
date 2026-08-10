import * as vscode from 'vscode';
import type { CompositionGraph, ContextSnapshot, GraphNamedNode } from '../model';
import type { NginController } from '../core/controller';

function escapeHtml(value: unknown): string {
  return String(value ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

function nonce(): string {
  const alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
  return Array.from({ length: 32 }, () => alphabet[Math.floor(Math.random() * alphabet.length)]).join('');
}

function summary(label: string, value: unknown): string {
  return `<div class="summary"><span>${escapeHtml(label)}</span><strong>${escapeHtml(value)}</strong></div>`;
}

function nodeTable(title: string, values: GraphNamedNode[], empty = 'None'): string {
  const rows = values.length
    ? values.map(value => `<tr><td>${escapeHtml(value.name ?? value.identity)}</td><td>${escapeHtml(value.kind ?? '')}</td><td><code>${escapeHtml(value.identity)}</code></td></tr>`).join('')
    : `<tr><td class="muted" colspan="3">${escapeHtml(empty)}</td></tr>`;
  return `<section><h2>${escapeHtml(title)} <span class="count">${values.length}</span></h2><table><thead><tr><th>Name</th><th>Kind</th><th>Identity</th></tr></thead><tbody>${rows}</tbody></table></section>`;
}

function graphBody(graph: CompositionGraph): string {
  return `
    <div class="hero">
      <div><div class="eyebrow">${escapeHtml(graph.product.type)}</div><h1>${escapeHtml(graph.product.name)}</h1><p>${escapeHtml(graph.product.identity)}</p></div>
      <div class="actions">
        <button data-command="configure">Configure</button><button data-command="build">Build</button><button data-command="run" class="primary">Run</button><button data-command="debug">Debug</button>
      </div>
    </div>
    <div class="summary-grid">
      ${summary('Configuration', graph.selection.configuration)}
      ${summary('Target', `${graph.selection.targetOperatingSystem}/${graph.selection.targetArchitecture}`)}
      ${summary('Compiler', graph.selection.compiler)}
      ${summary('Language', graph.product.languageStandard ?? 'default')}
      ${summary('Build inputs', graph.buildItems.length)}
      ${summary('Packages', graph.packages.length)}
    </div>
    <div class="toolbar">
      <button data-command="openManifest">Open XML</button><button data-command="editProduct">Edit product</button><button data-command="validate">Validate</button>
      <button data-command="showGraph">Graph JSON</button><button data-command="inspect">Inspect</button>
      <button data-command="addPackage">Add package</button><button data-command="addProjectReference">Add project reference</button>
    </div>
    ${nodeTable('Packages', graph.packages)}
    ${nodeTable('Active exports', graph.exports)}
    ${nodeTable('Options', graph.options)}
    ${nodeTable('Capabilities', graph.capabilityBindings)}
    ${nodeTable('Actions', graph.actions)}
    ${nodeTable('Plugins', graph.plugins)}
    ${nodeTable('Launches', graph.launches)}
    ${nodeTable('Publish', graph.publishes)}
  `;
}

function html(snapshot: ContextSnapshot, token: string): string {
  const body = snapshot.graph ? graphBody(snapshot.graph)
    : snapshot.context
      ? `<div class="empty"><h1>${escapeHtml(snapshot.context.projectName)}</h1><p>The Composition Graph is unavailable.</p><pre>${escapeHtml(snapshot.graphError)}</pre><button data-command="refreshGraph">Retry</button><button data-command="openManifest">Open XML</button></div>`
      : '<div class="empty"><h1>No active NGIN project</h1><p>Select a project from the Solution view.</p><button data-command="selectProject">Select project</button></div>';
  return `<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
  <meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; script-src 'nonce-${token}';">
  <style>
    :root { color-scheme: light dark; } body { font: 13px var(--vscode-font-family); color: var(--vscode-foreground); padding: 24px; max-width: 1200px; margin: auto; }
    .hero { display:flex; justify-content:space-between; align-items:flex-end; gap:20px; padding:22px; border:1px solid var(--vscode-widget-border); background:var(--vscode-sideBar-background); border-radius:8px; }
    h1 { font-size:28px; margin:2px 0 4px; } h2 { margin-top:28px; font-size:16px; } p { color:var(--vscode-descriptionForeground); }
    .eyebrow { color:var(--vscode-textLink-foreground); font-weight:600; text-transform:uppercase; letter-spacing:.08em; font-size:11px; }
    .summary-grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(150px,1fr)); gap:10px; margin:16px 0; }
    .summary { padding:12px; border:1px solid var(--vscode-widget-border); border-radius:6px; display:flex; flex-direction:column; gap:5px; }
    .summary span,.muted { color:var(--vscode-descriptionForeground); } .summary strong { font-size:14px; }
    button { border:1px solid var(--vscode-button-border,transparent); color:var(--vscode-button-secondaryForeground); background:var(--vscode-button-secondaryBackground); padding:6px 11px; border-radius:3px; cursor:pointer; }
    button:hover { background:var(--vscode-button-secondaryHoverBackground); } button.primary { color:var(--vscode-button-foreground); background:var(--vscode-button-background); }
    .actions,.toolbar { display:flex; flex-wrap:wrap; gap:7px; } .toolbar { margin:18px 0; }
    table { width:100%; border-collapse:collapse; } th,td { text-align:left; padding:8px; border-bottom:1px solid var(--vscode-widget-border); } th { color:var(--vscode-descriptionForeground); font-weight:500; }
    code { font-family:var(--vscode-editor-font-family); font-size:11px; } .count { color:var(--vscode-descriptionForeground); font-weight:400; }
    .empty { text-align:center; padding:80px 20px; } pre { white-space:pre-wrap; color:var(--vscode-errorForeground); }
  </style></head><body>${body}<script nonce="${token}">
    const vscode = acquireVsCodeApi();
    document.addEventListener('click', event => { const button = event.target.closest('[data-command]'); if (button) vscode.postMessage({ command: button.dataset.command }); });
  </script></body></html>`;
}

export class DashboardController implements vscode.Disposable {
  private panel?: vscode.WebviewPanel;
  private readonly subscription: vscode.Disposable;

  constructor(private readonly controller: NginController) {
    this.subscription = controller.onDidChange(snapshot => this.render(snapshot));
  }

  open(): void {
    if (this.panel) {
      this.panel.reveal(vscode.ViewColumn.Beside);
      this.render(this.controller.snapshot);
      return;
    }
    this.panel = vscode.window.createWebviewPanel('ngin.dashboard', 'NGIN Project', vscode.ViewColumn.Beside, { enableScripts: true, retainContextWhenHidden: true });
    this.panel.onDidDispose(() => { this.panel = undefined; });
    this.panel.webview.onDidReceiveMessage((message: { command?: string }) => {
      const allowed = new Set(['configure', 'build', 'run', 'debug', 'openManifest', 'editProduct', 'validate', 'showGraph', 'inspect', 'addPackage', 'addProjectReference', 'refreshGraph', 'selectProject']);
      if (message.command && allowed.has(message.command)) void vscode.commands.executeCommand(`ngin.${message.command}`);
    });
    this.render(this.controller.snapshot);
  }

  private render(snapshot: ContextSnapshot): void {
    if (!this.panel) return;
    this.panel.title = snapshot.context ? `NGIN · ${snapshot.context.projectName}` : 'NGIN Project';
    this.panel.webview.html = html(snapshot, nonce());
  }

  dispose(): void {
    this.subscription.dispose();
    this.panel?.dispose();
  }
}
