import * as vscode from 'vscode';
import type { ContextSnapshot } from '../model';
import type { NginController } from '../core/controller';
import type { SourceAnalysisProvider } from '../providers/sourceAnalysis';

interface DashboardState {
  hasProject: boolean;
  name: string;
  manifest: string;
  productType: string;
  projectStatus: string;
  statusDetail: string;
  configuration: string;
  target: string;
  toolchain: string;
  analysis: string;
  analysisDetail: string;
  lastOperation: string;
  launches: number;
  packages: number;
  inputs: number;
  canLaunch: boolean;
  canTest: boolean;
  hasTooling: boolean;
  busy: boolean;
  readiness: Array<{ label: string; detail: string; state: 'ready' | 'pending' | 'issue' }>;
}

function dashboardState(snapshot: ContextSnapshot, analysis: SourceAnalysisProvider): DashboardState {
  if (!snapshot.context) {
    return {
      hasProject: false, name: 'No selected project', manifest: '', productType: 'NGIN',
      projectStatus: 'Choose a project', statusDetail: 'Select the project used when the active file has no owner.',
      configuration: '—', target: '—', toolchain: '—', analysis: '—', analysisDetail: '', lastOperation: 'None',
      launches: 0, packages: 0, inputs: 0, canLaunch: false, canTest: false, hasTooling: false, busy: false,
      readiness: [{ label: 'Selected project', detail: 'Choose a project to build, run, or debug.', state: 'pending' }]
    };
  }
  const context = snapshot.context;
  const graph = snapshot.graph;
  const analyzer = analysis.summary(context.projectManifest);
  const last = snapshot.lastOperation?.projectManifest === context.projectManifest ? snapshot.lastOperation : undefined;
  const busyHere = snapshot.busy && snapshot.busyProjectManifest === context.projectManifest;
  const projectStatus = busyHere ? `${snapshot.busy} in progress`
    : snapshot.busy ? 'Another project operation is in progress'
    : snapshot.graphError ? 'Project could not be loaded' : 'Ready';
  const statusDetail = snapshot.graphError
    ? 'Open Problems or NGIN Output for details.'
    : snapshot.configured ? 'Build files are prepared.' : 'Build files will be prepared automatically when needed.';
  const hasTooling = Boolean(graph?.actions.some(action => action.kind === 'Analyze' || action.kind === 'Format'));
  const readiness: DashboardState['readiness'] = [
    {
      label: 'Project model',
      detail: graph ? 'Resolved from the NGIN CLI.' : snapshot.graphError ? 'Resolve the project errors to continue.' : 'Loading…',
      state: graph ? 'ready' : snapshot.graphError ? 'issue' : 'pending'
    },
    {
      label: 'Build setup',
      detail: snapshot.configured ? 'Prepared and reusable.' : 'Prepared automatically on the next build, run, or debug action.',
      state: snapshot.configured ? 'ready' : 'pending'
    }
  ];
  if (hasTooling) readiness.push({
    label: 'Analyzers and formatters',
    detail: analyzer.state === 'disabled' ? 'Available but not enabled.'
      : analyzer.state === 'failed' ? 'The last analysis failed.'
        : analyzer.message ?? 'Enabled when you approve project tooling.',
    state: analyzer.state === 'failed' ? 'issue' : analyzer.state === 'ready' || analyzer.state === 'analyzing' ? 'ready' : 'pending'
  });
  return {
    hasProject: true, name: context.projectName, manifest: context.projectManifest,
    productType: graph?.product.type ?? 'NGIN project', projectStatus, statusDetail,
    configuration: context.configuration, target: context.target, toolchain: context.toolchain,
    analysis: analyzer.state, analysisDetail: analyzer.message ?? 'No analysis has run',
    lastOperation: last ? `${last.command} ${last.state}` : 'None', launches: graph?.launches.length ?? 0,
    packages: graph?.packages.length ?? 0, inputs: graph?.buildItems.length ?? 0,
    canLaunch: Boolean(graph?.launches.length),
    canTest: Boolean(graph?.testing) || graph?.product.type === 'Test', hasTooling, busy: Boolean(snapshot.busy), readiness
  };
}

function nonce(): string {
  const alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
  return Array.from({ length: 32 }, () => alphabet[Math.floor(Math.random() * alphabet.length)]).join('');
}

function html(initial: DashboardState): string {
  const token = nonce();
  const initialJson = JSON.stringify(initial).replace(/</g, '\\u003c');
  return `<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'nonce-${token}'; script-src 'nonce-${token}';">
<style nonce="${token}">
:root{color-scheme:light dark}*{box-sizing:border-box}body{font:13px var(--vscode-font-family);color:var(--vscode-foreground);padding:28px;max-width:1000px;margin:auto}header{display:flex;justify-content:space-between;align-items:flex-end;gap:24px;padding-bottom:22px;border-bottom:1px solid var(--vscode-widget-border)}h1{font-size:26px;line-height:1.2;margin:4px 0}h2{font-size:16px;margin:28px 0 10px}p{color:var(--vscode-descriptionForeground);margin:5px 0}.eyebrow{color:var(--vscode-textLink-foreground);font-weight:600;text-transform:uppercase;letter-spacing:.07em}.actions,.selectors,.secondary{display:flex;gap:8px;flex-wrap:wrap}.selectors{margin-top:18px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:10px;margin-top:20px}.card,.readiness li{padding:14px;border:1px solid var(--vscode-widget-border);border-radius:5px}.card{display:flex;flex-direction:column;gap:5px}.card span,.card small{color:var(--vscode-descriptionForeground)}.card strong{font-size:15px}.readiness{display:grid;gap:8px;list-style:none;padding:0}.readiness li{display:grid;grid-template-columns:18px 1fr;gap:10px}.readiness strong{display:block}.readiness small{color:var(--vscode-descriptionForeground)}.state-ready{color:var(--vscode-testing-iconPassed)}.state-pending{color:var(--vscode-descriptionForeground)}.state-issue{color:var(--vscode-testing-iconFailed)}button{font:inherit;padding:6px 12px;border:1px solid var(--vscode-button-border,transparent);border-radius:2px;color:var(--vscode-button-secondaryForeground);background:var(--vscode-button-secondaryBackground);cursor:pointer}button:hover{background:var(--vscode-button-secondaryHoverBackground)}button:focus-visible{outline:1px solid var(--vscode-focusBorder);outline-offset:2px}.primary{color:var(--vscode-button-foreground);background:var(--vscode-button-background)}.primary:hover{background:var(--vscode-button-hoverBackground)}button:disabled{opacity:.55;cursor:default}.secondary{margin-top:26px}#live-status{position:absolute;width:1px;height:1px;padding:0;margin:-1px;overflow:hidden;clip:rect(0,0,0,0);white-space:nowrap;border:0}@media(max-width:650px){body{padding:18px}header{align-items:flex-start;flex-direction:column}}
</style></head><body>
<header><div><span id="product-type" class="eyebrow"></span><h1 id="project-name"></h1><p id="manifest"></p></div><div class="actions"><button data-command="build" class="primary">Build</button><button data-command="run" id="run-action">Run</button><button data-command="debug" id="debug-action">Debug</button><button data-command="test" id="test-action">Test</button><button data-command="cancel" id="cancel-action">Cancel operation</button></div></header>
<main><div class="selectors" aria-label="Project selection"><button data-command="selectConfiguration" id="configuration"></button><button data-command="selectTarget" id="target"></button><button data-command="selectToolchain" id="toolchain"></button></div>
<div class="grid" aria-label="Project summary"><div class="card"><span>Project status</span><strong id="project-status"></strong><small id="status-detail"></small></div><div class="card"><span>Analysis</span><strong id="analysis"></strong><small id="analysis-detail"></small></div><div class="card"><span>Last operation</span><strong id="last-operation"></strong></div><div class="card"><span>Resolved contents</span><strong id="contents"></strong><small id="content-detail"></small></div></div>
<section><h2>Readiness</h2><ul id="readiness" class="readiness"></ul><div class="actions"><button data-command="enableProjectTooling" id="tooling-action">Enable analyzers and formatters</button><button data-command="showOutput">Show output</button></div></section>
<div class="secondary"><button data-command="openManifest">Open manifest</button><button data-command="showGraph">Open resolved project JSON</button><button data-command="inspect">Inspect project</button><button data-command="setDefaultProject">Select another project</button></div></main>
<div id="live-status" aria-live="polite"></div>
<script nonce="${token}">const vscode=acquireVsCodeApi();
const set=(id,value)=>{document.getElementById(id).textContent=value};
function render(state){set('product-type',state.productType);set('project-name',state.name);set('manifest',state.manifest);set('project-status',state.projectStatus);set('status-detail',state.statusDetail);set('analysis',state.analysis);set('analysis-detail',state.analysisDetail);set('last-operation',state.lastOperation);set('configuration','Configuration: '+state.configuration);set('target','Platform: '+state.target);set('toolchain','Toolchain: '+state.toolchain);set('contents',state.inputs+' build inputs');set('content-detail',state.packages+' packages · '+state.launches+' launches');set('live-status',state.projectStatus);document.getElementById('run-action').hidden=!state.canLaunch;document.getElementById('debug-action').hidden=!state.canLaunch;document.getElementById('test-action').hidden=!state.canTest;document.getElementById('tooling-action').hidden=!state.hasTooling;document.getElementById('cancel-action').hidden=!state.busy;document.querySelectorAll('[data-command]').forEach(button=>{button.disabled=!['showOutput','cancel'].includes(button.dataset.command)&&(!state.hasProject||state.busy)});const list=document.getElementById('readiness');list.replaceChildren(...state.readiness.map(item=>{const li=document.createElement('li');const icon=document.createElement('span');icon.className='state-'+item.state;icon.setAttribute('aria-hidden','true');icon.textContent=item.state==='ready'?'✓':item.state==='issue'?'!':'○';const text=document.createElement('div');const strong=document.createElement('strong');strong.textContent=item.label;const small=document.createElement('small');small.textContent=item.detail;text.append(strong,small);li.append(icon,text);return li}))}
document.addEventListener('click',event=>{const button=event.target.closest('[data-command]');if(button&&!button.disabled)vscode.postMessage({command:button.dataset.command})});window.addEventListener('message',event=>{if(event.data?.type==='state')render(event.data.state)});render(${initialJson});</script></body></html>`;
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
    if (this.panel) {
      this.panel.reveal(vscode.ViewColumn.Beside);
      this.render(this.controller.snapshot);
      return;
    }
    this.panel = vscode.window.createWebviewPanel('ngin.dashboard', 'NGIN project overview', vscode.ViewColumn.Beside, { enableScripts: true });
    this.panel.onDidDispose(() => { this.panel = undefined; });
    this.panel.webview.onDidReceiveMessage((message: { command?: string }) => {
      const allowed = new Set([
        'build', 'run', 'debug', 'test', 'openManifest', 'showGraph', 'inspect', 'setDefaultProject',
        'selectConfiguration', 'selectTarget', 'selectToolchain', 'enableProjectTooling', 'showOutput', 'cancel'
      ]);
      if (message.command && allowed.has(message.command)) void vscode.commands.executeCommand(`ngin.${message.command}`);
    });
    const state = dashboardState(this.controller.snapshot, this.analysis);
    this.panel.webview.html = html(state);
    this.panel.title = state.hasProject ? `NGIN: ${state.name}` : 'NGIN project overview';
  }

  private render(snapshot: ContextSnapshot): void {
    if (!this.panel) return;
    const state = dashboardState(snapshot, this.analysis);
    this.panel.title = state.hasProject ? `NGIN: ${state.name}` : 'NGIN project overview';
    void this.panel.webview.postMessage({ type: 'state', state });
  }

  dispose(): void {
    this.disposables.forEach(value => value.dispose());
    this.panel?.dispose();
  }
}
