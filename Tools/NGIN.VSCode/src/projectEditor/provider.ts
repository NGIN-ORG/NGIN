import * as vscode from 'vscode';
import {
  addProfile,
  deleteProfile,
  ProjectEnvironmentVariableEdit,
  ProjectFeatureState,
  ProjectInputBlock,
  ProjectInputEdit,
  ProjectPackageReferenceEdit,
  setEnvironmentVariables,
  setInputEntries,
  setLaunch,
  setPackageReferences,
  setProfileFeatureState,
  updateProfile,
  updateProjectAttributes
} from './authoring';
import { buildProjectEditorModel } from './model';
import { ProjectInspectPayload } from '../core/types';

interface ProjectEditorInspectState {
  inspect?: ProjectInspectPayload;
  activeProfile?: string;
}

export interface NginProjectEditorServices {
  inspect(document: vscode.TextDocument): Promise<ProjectEditorInspectState>;
  apply(document: vscode.TextDocument, update: (xml: string) => string): Promise<void>;
  openSource(uri: vscode.Uri): Promise<void>;
  validate(uri: vscode.Uri): Promise<void>;
}

type ProjectEditorMessage =
  | { type: 'ready' }
  | { type: 'openSource' }
  | { type: 'validate' }
  | { type: 'updateProject'; name?: string; template?: string; defaultProfile?: string }
  | { type: 'setRootLaunch'; executable?: string; workingDirectory?: string }
  | { type: 'addProfile'; name: string }
  | { type: 'deleteProfile'; name: string }
  | {
      type: 'updateProfile';
      originalName: string;
      name: string;
      template?: string;
      buildType?: string;
      platform?: string;
      operatingSystem?: string;
      architecture?: string;
      environment?: string;
      launchExecutable?: string;
      launchWorkingDirectory?: string;
    }
  | { type: 'setPackageReferences'; profileName?: string; references: ProjectPackageReferenceEdit[] }
  | { type: 'setInputEntries'; profileName?: string; block: ProjectInputBlock; entries: ProjectInputEdit[] }
  | { type: 'setFeatureState'; profileName: string; packageName: string; featureName: string; state: ProjectFeatureState }
  | { type: 'setEnvironmentVariables'; environmentName: string; variables: ProjectEnvironmentVariableEdit[] };

function nonce(): string {
  const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
  let value = '';
  for (let index = 0; index < 32; ++index) {
    value += chars.charAt(Math.floor(Math.random() * chars.length));
  }
  return value;
}

function fullDocumentRange(document: vscode.TextDocument): vscode.Range {
  const last = document.lineAt(Math.max(0, document.lineCount - 1));
  return new vscode.Range(new vscode.Position(0, 0), last.range.end);
}

export class NginProjectEditorProvider implements vscode.CustomTextEditorProvider {
  static readonly viewType = 'ngin.projectEditor';

  constructor(private readonly services: NginProjectEditorServices) {}

  async resolveCustomTextEditor(document: vscode.TextDocument, webviewPanel: vscode.WebviewPanel): Promise<void> {
    webviewPanel.webview.options = { enableScripts: true };
    webviewPanel.webview.html = this.html(webviewPanel.webview);
    let inspectGeneration = 0;

    const postModel = async (state?: ProjectEditorInspectState): Promise<void> => {
      const model = buildProjectEditorModel(document.getText(), document.uri.fsPath, document.uri.toString(), state?.inspect, state?.activeProfile);
      await webviewPanel.webview.postMessage({ type: 'model', model });
    };

    const update = async (includeInspect: boolean): Promise<void> => {
      await postModel();
      if (!includeInspect) {
        return;
      }

      const generation = ++inspectGeneration;
      try {
        const state = await this.services.inspect(document);
        if (generation === inspectGeneration) {
          await postModel(state);
        }
      } catch {
        // The parse-only editor remains usable when inspect is unavailable.
      }
    };

    const documentChange = vscode.workspace.onDidChangeTextDocument((event) => {
      if (event.document.uri.toString() === document.uri.toString()) {
        void update(false);
      }
    });

    webviewPanel.onDidDispose(() => documentChange.dispose());
    webviewPanel.webview.onDidReceiveMessage((message: ProjectEditorMessage) => {
      void this.handleMessage(document, message).then(() => update(true));
    });

    void update(true);
  }

  private async handleMessage(document: vscode.TextDocument, message: ProjectEditorMessage): Promise<void> {
    switch (message.type) {
      case 'ready':
        return;
      case 'openSource':
        await this.services.openSource(document.uri);
        return;
      case 'validate':
        await this.services.validate(document.uri);
        return;
      case 'updateProject':
        await this.services.apply(document, (xml) => updateProjectAttributes(xml, {
          name: message.name,
          template: message.template,
          defaultProfile: message.defaultProfile
        }));
        return;
      case 'setRootLaunch':
        await this.services.apply(document, (xml) => setLaunch(xml, undefined, message.executable, message.workingDirectory));
        return;
      case 'addProfile':
        await this.services.apply(document, (xml) => addProfile(xml, message.name));
        return;
      case 'deleteProfile':
        await this.services.apply(document, (xml) => deleteProfile(xml, message.name));
        return;
      case 'updateProfile':
        await this.services.apply(document, (xml) => updateProfile(xml, message));
        return;
      case 'setPackageReferences':
        await this.services.apply(document, (xml) => setPackageReferences(xml, message.references, message.profileName));
        return;
      case 'setInputEntries':
        await this.services.apply(document, (xml) => setInputEntries(xml, message.block, message.entries, message.profileName));
        return;
      case 'setFeatureState':
        await this.services.apply(document, (xml) => setProfileFeatureState(xml, message.profileName, message.packageName, message.featureName, message.state));
        return;
      case 'setEnvironmentVariables':
        await this.services.apply(document, (xml) => setEnvironmentVariables(xml, message.environmentName, message.variables));
        return;
    }
  }

  static async applyTextEdit(document: vscode.TextDocument, nextText: string): Promise<void> {
    if (nextText === document.getText()) {
      return;
    }
    const edit = new vscode.WorkspaceEdit();
    edit.replace(document.uri, fullDocumentRange(document), nextText);
    await vscode.workspace.applyEdit(edit);
  }

  private html(webview: vscode.Webview): string {
    const scriptNonce = nonce();
    return /* html */`<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src ${webview.cspSource} 'unsafe-inline'; script-src 'nonce-${scriptNonce}';">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>NGIN Project Editor</title>
  <style>
    :root {
      color-scheme: light dark;
      --border: var(--vscode-panel-border);
      --muted: var(--vscode-descriptionForeground);
      --focus: var(--vscode-focusBorder);
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      color: var(--vscode-foreground);
      background: var(--vscode-editor-background);
      font: var(--vscode-font-size) var(--vscode-font-family);
    }
    header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
      padding: 12px 16px;
      border-bottom: 1px solid var(--border);
      background: var(--vscode-sideBar-background);
    }
    h1 { margin: 0; font-size: 15px; font-weight: 600; }
    h2 { margin: 0 0 10px; font-size: 13px; font-weight: 600; }
    .actions, .tabs, .row-actions { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }
    .tabs {
      padding: 8px 16px 0;
      border-bottom: 1px solid var(--border);
    }
    button, select, input {
      color: var(--vscode-input-foreground);
      background: var(--vscode-input-background);
      border: 1px solid var(--vscode-input-border, var(--border));
      border-radius: 4px;
      min-height: 28px;
      padding: 4px 8px;
      font: inherit;
    }
    button {
      color: var(--vscode-button-foreground);
      background: var(--vscode-button-background);
      border-color: var(--vscode-button-border, transparent);
      cursor: pointer;
    }
    button.secondary {
      color: var(--vscode-button-secondaryForeground);
      background: var(--vscode-button-secondaryBackground);
    }
    button.tab {
      color: var(--vscode-foreground);
      background: transparent;
      border: 0;
      border-bottom: 2px solid transparent;
      border-radius: 0;
    }
    button.tab.active { border-bottom-color: var(--focus); }
    main { padding: 16px; }
    section { display: none; max-width: 1100px; }
    section.active { display: block; }
    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
      gap: 10px;
      margin-bottom: 12px;
    }
    label { display: flex; flex-direction: column; gap: 4px; min-width: 0; }
    label span { color: var(--muted); font-size: 12px; }
    input, select { width: 100%; }
    .panel {
      border: 1px solid var(--border);
      border-radius: 6px;
      padding: 12px;
      margin-bottom: 12px;
    }
    .list { display: grid; gap: 8px; }
    .item {
      display: grid;
      grid-template-columns: repeat(4, minmax(120px, 1fr)) auto;
      gap: 8px;
      align-items: end;
    }
    .item.three { grid-template-columns: repeat(3, minmax(140px, 1fr)) auto; }
    .item.two { grid-template-columns: repeat(2, minmax(160px, 1fr)) auto; }
    .feature {
      display: grid;
      grid-template-columns: minmax(180px, 1.5fr) minmax(120px, 1fr) minmax(120px, 1fr);
      gap: 8px;
      align-items: center;
      padding: 8px 0;
      border-bottom: 1px solid var(--border);
    }
    .muted { color: var(--muted); }
    .error {
      color: var(--vscode-errorForeground);
      border-color: var(--vscode-inputValidation-errorBorder);
      background: var(--vscode-inputValidation-errorBackground);
    }
    @media (max-width: 760px) {
      header { align-items: flex-start; flex-direction: column; }
      .item, .item.three, .item.two, .feature { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <header>
    <h1 id="title">NGIN Project</h1>
    <div class="actions">
      <button class="secondary" id="validate">Validate</button>
      <button class="secondary" id="open-source">XML Source</button>
    </div>
  </header>
  <nav class="tabs" id="tabs"></nav>
  <main id="app"></main>
  <script nonce="${scriptNonce}">
    const vscode = acquireVsCodeApi();
    let model = undefined;
    let activeTab = 'overview';
    const tabs = [
      ['overview', 'Overview'],
      ['profiles', 'Profiles'],
      ['inputs', 'Inputs'],
      ['references', 'References & Features'],
      ['environment', 'Environment'],
      ['diagnostics', 'Diagnostics']
    ];

    const clean = (value) => value === undefined || value === null ? '' : String(value);
    const optional = (value) => {
      const text = clean(value).trim();
      return text.length ? text : undefined;
    };
    const esc = (value) => clean(value).replace(/[&<>"']/g, (ch) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[ch]));
    const currentProfile = () => model.profiles.find((profile) => profile.name === model.activeProfile) ?? model.profiles[0];

    function post(message) { vscode.postMessage(message); }
    function input(id) { return document.getElementById(id); }

    function renderTabs() {
      input('tabs').innerHTML = tabs.map(([id, label]) => '<button class="tab ' + (id === activeTab ? 'active' : '') + '" data-tab="' + id + '">' + label + '</button>').join('');
    }

    function render() {
      if (!model) { return; }
      input('title').textContent = model.project.name || 'NGIN Project';
      renderTabs();
      const app = input('app');
      if (model.parseError) {
        app.innerHTML = '<section class="active"><div class="panel error">' + esc(model.parseError) + '</div></section>';
        return;
      }
      app.innerHTML =
        section('overview', renderOverview()) +
        section('profiles', renderProfiles()) +
        section('inputs', renderInputs()) +
        section('references', renderReferences()) +
        section('environment', renderEnvironment()) +
        section('diagnostics', renderDiagnostics());
    }

    function section(id, body) {
      return '<section id="section-' + id + '" class="' + (id === activeTab ? 'active' : '') + '">' + body + '</section>';
    }

    function field(id, label, value) {
      return '<label><span>' + label + '</span><input id="' + id + '" value="' + esc(value) + '"></label>';
    }

    function renderOverview() {
      return '<div class="panel"><h2>Project</h2><div class="grid">' +
        field('project-name', 'Name', model.project.name) +
        field('project-template', 'Template', model.project.template) +
        field('project-default-profile', 'Default Profile', model.project.defaultProfile) +
        '</div><div class="row-actions"><button id="save-project">Save Project</button></div></div>' +
        '<div class="panel"><h2>Root Launch</h2><div class="grid">' +
        field('root-launch-executable', 'Executable', model.project.launchExecutable) +
        field('root-launch-working-directory', 'Working Directory', model.project.launchWorkingDirectory) +
        '</div><div class="row-actions"><button id="save-root-launch">Save Launch</button></div></div>' +
        unsupportedNotice();
    }

    function profileOptions(selected, includeRoot = false) {
      const root = includeRoot ? '<option value="">Project root</option>' : '';
      return root + model.profiles.map((profile) => '<option value="' + esc(profile.name) + '"' + (profile.name === selected ? ' selected' : '') + '>' + esc(profile.name) + '</option>').join('');
    }

    function renderProfiles() {
      const profile = currentProfile();
      if (!profile) {
        return '<div class="panel"><h2>Profiles</h2><div class="row-actions"><input id="new-profile-name" placeholder="Profile name"><button id="add-profile">Add Profile</button></div></div>';
      }
      return '<div class="panel"><h2>Profiles</h2><div class="grid"><label><span>Profile</span><select id="profile-select">' + profileOptions(profile.name) + '</select></label>' +
        field('profile-name', 'Name', profile.name) +
        field('profile-template', 'Template', profile.template) +
        field('profile-build-type', 'Build Type', profile.buildType) +
        field('profile-platform', 'Platform', profile.platform) +
        field('profile-os', 'Operating System', profile.operatingSystem) +
        field('profile-arch', 'Architecture', profile.architecture) +
        field('profile-env', 'Environment', profile.environment) +
        field('profile-launch-executable', 'Launch Executable', profile.launchExecutable) +
        field('profile-launch-working-directory', 'Launch Working Directory', profile.launchWorkingDirectory) +
        '</div><div class="row-actions"><button id="save-profile">Save Profile</button><button class="secondary" id="delete-profile">Delete Profile</button><input id="new-profile-name" placeholder="New profile"><button class="secondary" id="add-profile">Add Profile</button></div></div>';
    }

    function renderInputs() {
      const profile = currentProfile();
      const selectedScope = profile?.name ?? '';
      return '<div class="panel"><h2>Inputs</h2><div class="grid"><label><span>Scope</span><select id="input-scope">' + profileOptions(selectedScope, true) + '</select></label><label><span>Block</span><select id="input-block"><option>Sources</option><option>Headers</option><option>Configs</option></select></label></div><div id="input-list" class="list"></div><div class="row-actions"><button class="secondary" id="add-input">Add Input</button><button id="save-inputs">Save Inputs</button></div></div>';
    }

    function renderReferences() {
      const profile = currentProfile();
      return '<div class="panel"><h2>Package References</h2><div class="grid"><label><span>Scope</span><select id="reference-scope">' + profileOptions(profile?.name ?? '', true) + '</select></label></div><div id="reference-list" class="list"></div><div class="row-actions"><button class="secondary" id="add-reference">Add Reference</button><button id="save-references">Save References</button></div></div>' +
        '<div class="panel"><h2>Package Features</h2><div class="list">' +
        (model.features.length ? model.features.map(renderFeature).join('') : '<p class="muted">No package features are available for the active profile.</p>') +
        '</div></div>';
    }

    function renderFeature(feature) {
      const disabled = feature.readOnly ? ' disabled' : '';
      return '<div class="feature"><div><strong>' + esc(feature.packageName + ' / ' + feature.featureName) + '</strong><div class="muted">' + esc(feature.description || '') + '</div></div><div class="muted">' + esc(feature.resolvedState || '') + '</div><select data-feature-package="' + esc(feature.packageName) + '" data-feature-name="' + esc(feature.featureName) + '"' + disabled + '><option value="inherit"' + (feature.state === 'inherit' ? ' selected' : '') + '>Inherit</option><option value="use"' + (feature.state === 'use' ? ' selected' : '') + '>Use</option><option value="disable"' + (feature.state === 'disable' ? ' selected' : '') + '>Disable</option></select></div>';
    }

    function renderEnvironment() {
      const env = model.environments[0];
      return '<div class="panel"><h2>Environment</h2><div class="grid"><label><span>Environment</span><select id="environment-select">' + model.environments.map((entry) => '<option value="' + esc(entry.name) + '">' + esc(entry.name) + '</option>').join('') + '</select></label><label><span>New Environment</span><input id="new-environment-name"></label></div><div id="variable-list" class="list"></div><div class="row-actions"><button class="secondary" id="add-variable">Add Variable</button><button id="save-environment">Save Environment</button></div></div>';
    }

    function renderDiagnostics() {
      return '<div class="panel"><h2>Diagnostics</h2>' +
        (model.diagnostics.length ? '<div class="list">' + model.diagnostics.map((diagnostic) => '<div>' + esc(diagnostic) + '</div>').join('') + '</div>' : '<p class="muted">No diagnostics reported.</p>') +
        '</div>' + unsupportedNotice();
    }

    function unsupportedNotice() {
      return model.unsupportedSections.length
        ? '<div class="panel"><h2>XML-managed Sections</h2><p class="muted">' + esc(model.unsupportedSections.join(', ')) + '</p></div>'
        : '';
    }

    function row(values, kind) {
      if (kind === 'reference') {
        return '<div class="item three" data-row="reference">' + field('', 'Package', values.name) + field('', 'Version', values.version) + '<label><span>Optional</span><select><option value=""></option><option value="true"' + (values.optional ? ' selected' : '') + '>true</option><option value="false"' + (values.optional === false ? ' selected' : '') + '>false</option></select></label><button class="secondary" data-remove-row>Remove</button></div>';
      }
      if (kind === 'variable') {
        return '<div class="item" data-row="variable">' + field('', 'Name', values.name) + field('', 'Value', values.value) + field('', 'From Environment', values.fromEnvironment) + field('', 'From Local Setting', values.fromLocalSetting) + '<label><span>Required</span><select><option value=""></option><option value="true"' + (values.required ? ' selected' : '') + '>true</option><option value="false"' + (values.required === false ? ' selected' : '') + '>false</option></select></label><label><span>Secret</span><select><option value=""></option><option value="true"' + (values.secret ? ' selected' : '') + '>true</option><option value="false"' + (values.secret === false ? ' selected' : '') + '>false</option></select></label><button class="secondary" data-remove-row>Remove</button></div>';
      }
      return '<div class="item three" data-row="input"><label><span>Mode</span><select><option value="Directory"' + (values.mode === 'Directory' ? ' selected' : '') + '>Directory</option><option value="File"' + (values.mode === 'File' ? ' selected' : '') + '>File</option><option value="Glob"' + (values.mode === 'Glob' ? ' selected' : '') + '>Glob</option></select></label>' + field('', 'Path', values.path) + field('', 'Include', values.include) + field('', 'Exclude', values.exclude) + '<button class="secondary" data-remove-row>Remove</button></div>';
    }

    function selectedInputEntries() {
      const scope = input('input-scope')?.value || '';
      const block = input('input-block')?.value || 'Sources';
      const source = scope ? model.profiles.find((profile) => profile.name === scope)?.inputs : model.project.inputs;
      return source?.[block] ?? [];
    }

    function refreshInputList() {
      const list = input('input-list');
      if (list) list.innerHTML = selectedInputEntries().map((entry) => row(entry, 'input')).join('');
    }

    function selectedReferences() {
      const scope = input('reference-scope')?.value || '';
      return scope ? model.profiles.find((profile) => profile.name === scope)?.packageReferences ?? [] : model.project.packageReferences;
    }

    function refreshReferenceList() {
      const list = input('reference-list');
      if (list) list.innerHTML = selectedReferences().map((entry) => row(entry, 'reference')).join('');
    }

    function selectedEnvironment() {
      const selected = input('environment-select')?.value || model.environments[0]?.name;
      return model.environments.find((environment) => environment.name === selected);
    }

    function refreshVariableList() {
      const list = input('variable-list');
      if (list) list.innerHTML = (selectedEnvironment()?.variables ?? []).map((entry) => row(entry, 'variable')).join('');
    }

    function collectRows(kind) {
      return Array.from(document.querySelectorAll('[data-row="' + kind + '"]')).map((node) => {
        const inputs = Array.from(node.querySelectorAll('input, select'));
        if (kind === 'reference') {
          return { name: optional(inputs[0].value), version: optional(inputs[1].value), optional: optional(inputs[2].value) === undefined ? undefined : inputs[2].value === 'true' };
        }
        if (kind === 'variable') {
          return { name: optional(inputs[0].value), value: optional(inputs[1].value), fromEnvironment: optional(inputs[2].value), fromLocalSetting: optional(inputs[3].value), required: optional(inputs[4].value) === undefined ? undefined : inputs[4].value === 'true', secret: optional(inputs[5].value) === undefined ? undefined : inputs[5].value === 'true' };
        }
        return { mode: inputs[0].value, path: optional(inputs[1].value), include: optional(inputs[2].value), exclude: optional(inputs[3].value) };
      }).filter((entry) => entry.name !== undefined || entry.path !== undefined || entry.include !== undefined);
    }

    document.addEventListener('click', (event) => {
      const target = event.target;
      if (target.matches('[data-tab]')) { activeTab = target.dataset.tab; render(); setTimeout(() => { refreshInputList(); refreshReferenceList(); refreshVariableList(); }, 0); }
      if (target.id === 'open-source') post({ type: 'openSource' });
      if (target.id === 'validate') post({ type: 'validate' });
      if (target.id === 'save-project') post({ type: 'updateProject', name: optional(input('project-name').value), template: optional(input('project-template').value), defaultProfile: optional(input('project-default-profile').value) });
      if (target.id === 'save-root-launch') post({ type: 'setRootLaunch', executable: optional(input('root-launch-executable').value), workingDirectory: optional(input('root-launch-working-directory').value) });
      if (target.id === 'add-profile') post({ type: 'addProfile', name: input('new-profile-name').value });
      if (target.id === 'delete-profile') post({ type: 'deleteProfile', name: input('profile-select').value });
      if (target.id === 'save-profile') post({ type: 'updateProfile', originalName: input('profile-select').value, name: optional(input('profile-name').value), template: optional(input('profile-template').value), buildType: optional(input('profile-build-type').value), platform: optional(input('profile-platform').value), operatingSystem: optional(input('profile-os').value), architecture: optional(input('profile-arch').value), environment: optional(input('profile-env').value), launchExecutable: optional(input('profile-launch-executable').value), launchWorkingDirectory: optional(input('profile-launch-working-directory').value) });
      if (target.id === 'add-input') input('input-list').insertAdjacentHTML('beforeend', row({ mode: 'Directory' }, 'input'));
      if (target.id === 'save-inputs') post({ type: 'setInputEntries', profileName: optional(input('input-scope').value), block: input('input-block').value, entries: collectRows('input') });
      if (target.id === 'add-reference') input('reference-list').insertAdjacentHTML('beforeend', row({}, 'reference'));
      if (target.id === 'save-references') post({ type: 'setPackageReferences', profileName: optional(input('reference-scope').value), references: collectRows('reference') });
      if (target.id === 'add-variable') input('variable-list').insertAdjacentHTML('beforeend', row({}, 'variable'));
      if (target.id === 'save-environment') post({ type: 'setEnvironmentVariables', environmentName: optional(input('new-environment-name').value) || input('environment-select').value, variables: collectRows('variable') });
      if (target.matches('[data-remove-row]')) target.closest('[data-row]').remove();
    });

    document.addEventListener('change', (event) => {
      const target = event.target;
      if (target.id === 'profile-select') { model.activeProfile = target.value; render(); setTimeout(() => { refreshInputList(); refreshReferenceList(); refreshVariableList(); }, 0); }
      if (target.id === 'input-scope' || target.id === 'input-block') refreshInputList();
      if (target.id === 'reference-scope') refreshReferenceList();
      if (target.id === 'environment-select') refreshVariableList();
      if (target.dataset.featurePackage && target.dataset.featureName) {
        if (model.activeProfile) post({ type: 'setFeatureState', profileName: model.activeProfile, packageName: target.dataset.featurePackage, featureName: target.dataset.featureName, state: target.value });
      }
    });

    window.addEventListener('message', (event) => {
      if (event.data?.type === 'model') {
        model = event.data.model;
        render();
        setTimeout(() => { refreshInputList(); refreshReferenceList(); refreshVariableList(); }, 0);
      }
    });
    post({ type: 'ready' });
  </script>
</body>
</html>`;
  }
}
