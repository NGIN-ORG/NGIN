import * as vscode from 'vscode';
import type { ContextSnapshot } from '../model';
import type { NginController } from '../core/controller';
import type { SourceAnalysisProvider } from '../providers/sourceAnalysis';

function escapeHtml(value: unknown): string {
  return String(value ?? '').replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}

function nonce(): string {
  const alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
  return Array.from({ length: 32 }, () => alphabet[Math.floor(Math.random() * alphabet.length)]).join('');
}

function card(label: string, value: unknown, detail?: unknown): string {
  return `<div class="card"><span>${escapeHtml(label)}</span><strong>${escapeHtml(value)}</strong>${detail ? `<small>${escapeHtml(detail)}</small>` : ''}</div>`;
}

function html(snapshot: ContextSnapshot, analysis: SourceAnalysisProvider, token: string): string {
  if (!snapshot.context) return `<!doctype html><html><body><h1>No NGIN Build target</h1><button data-command="switchBuildTarget">Choose Build target</button></body></html>`;
  const context = snapshot.context;
  const graph = snapshot.graph;
  const analyzer = analysis.summary(context.projectManifest);
  const lastOperation = snapshot.lastOperation?.projectManifest === context.projectManifest ? snapshot.lastOperation : undefined;
  const state = snapshot.busy ? `${snapshot.busy} in progress` : snapshot.graphError ? 'Composition unavailable'
    : snapshot.configured ? 'Ready' : 'Needs configure';
  const launches = graph?.launches.map(value => value.name ?? value.identity).join(', ') || 'None';
  const body = `
    <header><div><span class="eyebrow">${escapeHtml(graph?.product.type ?? 'NGIN Project')}</span><h1>${escapeHtml(context.projectName)}</h1><p>${escapeHtml(context.projectManifest)}</p></div>
      <div class="actions"><button data-command="build">Build</button><button data-command="run" class="primary">Run</button><button data-command="debug">Debug</button>${graph?.testing ? '<button data-command="test">Test</button>' : ''}</div></header>
    <main>
      <div class="grid">
        ${card('Project state', state, snapshot.graphError)}
        ${card('Configuration', context.configuration, `${context.target} · ${context.toolchain}`)}
        ${card('Analysis', analyzer.state, analyzer.message ?? 'Waiting for an open source file')}
        ${card('Last operation', lastOperation ? `${lastOperation.command} ${lastOperation.state}` : 'None', lastOperation?.message)}
        ${card('Launches', graph?.launches.length ?? 0, launches)}
        ${card('Packages', graph?.packages.length ?? 0)}
        ${card('Build inputs', graph?.buildItems.length ?? 0)}
      </div>
      <section><h2>Developer workflow</h2><p>Open and save a C/C++ file to run its resolved analyzers. F5 debugs the project owning the active file; this pinned target is the fallback.</p></section>
      <div class="secondary"><button data-command="openManifest">Open Manifest</button><button data-command="showGraph">Composition Details</button><button data-command="inspect">Inspect</button><button data-command="switchBuildTarget">Switch Build Target</button></div>
    </main>`;
  return `<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; script-src 'nonce-${token}';"><style>
    :root{color-scheme:light dark}body{font:13px var(--vscode-font-family);color:var(--vscode-foreground);padding:28px;max-width:1000px;margin:auto}header{display:flex;justify-content:space-between;align-items:end;gap:20px;padding-bottom:24px;border-bottom:1px solid var(--vscode-widget-border)}h1{font-size:28px;margin:4px 0}h2{font-size:16px}p,small,.card span{color:var(--vscode-descriptionForeground)}.eyebrow{color:var(--vscode-textLink-foreground);font-weight:600;text-transform:uppercase;letter-spacing:.08em}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:10px;margin:22px 0}.card{padding:14px;border:1px solid var(--vscode-widget-border);border-radius:5px;display:flex;flex-direction:column;gap:5px}.card strong{font-size:15px}.actions,.secondary{display:flex;gap:8px;flex-wrap:wrap}.secondary{margin-top:24px}button{padding:6px 12px;border:1px solid var(--vscode-button-border,transparent);color:var(--vscode-button-secondaryForeground);background:var(--vscode-button-secondaryBackground);cursor:pointer}.primary{color:var(--vscode-button-foreground);background:var(--vscode-button-background)}</style></head><body>${body}<script nonce="${token}">const vscode=acquireVsCodeApi();document.addEventListener('click',e=>{const b=e.target.closest('[data-command]');if(b)vscode.postMessage({command:b.dataset.command});});</script></body></html>`;
}

export class DashboardController implements vscode.Disposable {
  private panel?: vscode.WebviewPanel;
  private readonly disposables: vscode.Disposable[];

  constructor(private readonly controller: NginController, private readonly analysis: SourceAnalysisProvider) {
    this.disposables = [
      controller.onDidChange(snapshot => this.render(snapshot)),
      analysis.onDidChange(() => this.render(controller.snapshot))
    ];
  }

  open(): void {
    if (this.panel) this.panel.reveal(vscode.ViewColumn.Beside);
    else {
      this.panel = vscode.window.createWebviewPanel('ngin.dashboard', 'NGIN Project', vscode.ViewColumn.Beside, { enableScripts: true });
      this.panel.onDidDispose(() => { this.panel = undefined; });
      this.panel.webview.onDidReceiveMessage((message: { command?: string }) => {
        const allowed = new Set(['build', 'run', 'debug', 'test', 'openManifest', 'showGraph', 'inspect', 'switchBuildTarget']);
        if (message.command && allowed.has(message.command)) void vscode.commands.executeCommand(`ngin.${message.command}`);
      });
    }
    this.render(this.controller.snapshot);
  }

  private render(snapshot: ContextSnapshot): void {
    if (!this.panel) return;
    this.panel.title = snapshot.context ? `NGIN · ${snapshot.context.projectName}` : 'NGIN Project';
    this.panel.webview.html = html(snapshot, this.analysis, nonce());
  }

  dispose(): void {
    this.disposables.forEach(value => value.dispose());
    this.panel?.dispose();
  }
}
