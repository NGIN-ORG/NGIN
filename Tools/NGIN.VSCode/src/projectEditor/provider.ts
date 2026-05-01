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
      --bg: var(--vscode-editor-background);
      --panel: var(--vscode-sideBar-background);
      --panel-alt: var(--vscode-input-background);
      --fg: var(--vscode-foreground);
      --muted: var(--vscode-descriptionForeground);
      --border: var(--vscode-panel-border);
      --focus: var(--vscode-focusBorder);
      --ok: var(--vscode-testing-iconPassed);
      --warn: var(--vscode-editorWarning-foreground);
      --err: var(--vscode-editorError-foreground);
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      color: var(--fg);
      background: var(--bg);
      font: var(--vscode-font-size) var(--vscode-font-family);
    }
    button, input, select {
      font: inherit;
    }
    button {
      min-height: 28px;
      border: 1px solid var(--vscode-button-border, transparent);
      border-radius: 4px;
      color: var(--vscode-button-foreground);
      background: var(--vscode-button-background);
      padding: 4px 10px;
      cursor: pointer;
    }
    button.secondary, button.quiet {
      color: var(--vscode-button-secondaryForeground);
      background: var(--vscode-button-secondaryBackground);
    }
    button.quiet {
      border-color: var(--border);
    }
    button.link {
      min-height: 0;
      border: 0;
      color: var(--vscode-textLink-foreground);
      background: transparent;
      padding: 0;
    }
    input, select {
      width: 100%;
      min-height: 28px;
      color: var(--vscode-input-foreground);
      background: var(--vscode-input-background);
      border: 1px solid var(--vscode-input-border, var(--border));
      border-radius: 4px;
      padding: 4px 8px;
    }
    label {
      display: grid;
      gap: 4px;
      min-width: 0;
    }
    label span {
      color: var(--muted);
      font-size: 12px;
    }
    header {
      display: grid;
      grid-template-columns: minmax(0, 1fr) auto;
      gap: 16px;
      align-items: center;
      padding: 14px 18px;
      border-bottom: 1px solid var(--border);
      background: var(--panel);
    }
    h1, h2, h3, p {
      margin: 0;
    }
    h1 {
      font-size: 17px;
      font-weight: 600;
    }
    h2 {
      font-size: 14px;
      font-weight: 600;
    }
    h3 {
      font-size: 13px;
      font-weight: 600;
    }
    .subtitle {
      margin-top: 4px;
      color: var(--muted);
      font-size: 12px;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
    }
    .actions, .inline, .segmented {
      display: flex;
      gap: 8px;
      align-items: center;
      flex-wrap: wrap;
    }
    .layout {
      display: grid;
      grid-template-columns: 210px minmax(0, 1fr);
      min-height: calc(100vh - 58px);
    }
    nav {
      padding: 14px 10px;
      border-right: 1px solid var(--border);
      background: var(--panel);
    }
    .nav-button {
      width: 100%;
      justify-content: flex-start;
      margin-bottom: 4px;
      border-color: transparent;
      color: var(--fg);
      background: transparent;
      text-align: left;
    }
    .nav-button.active {
      border-color: var(--focus);
      background: var(--vscode-list-activeSelectionBackground);
      color: var(--vscode-list-activeSelectionForeground);
    }
    main {
      padding: 18px;
      max-width: 1280px;
      width: 100%;
    }
    section {
      display: none;
    }
    section.active {
      display: grid;
      gap: 14px;
    }
    .band {
      display: grid;
      gap: 12px;
      padding: 14px;
      border: 1px solid var(--border);
      border-radius: 6px;
      background: var(--panel);
    }
    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
      gap: 10px;
    }
    .muted {
      color: var(--muted);
      font-size: 12px;
    }
    .details {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
      border: 1px solid var(--border);
      background: var(--panel-alt);
    }
    .detail {
      min-width: 0;
      padding: 8px 10px;
      border-right: 1px solid var(--border);
      border-bottom: 1px solid var(--border);
    }
    .detail span {
      display: block;
      color: var(--muted);
      font-size: 12px;
      margin-bottom: 2px;
    }
    .detail strong {
      display: block;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
      font-weight: 600;
    }
    .detail.ok strong { color: var(--ok); }
    .detail.warn strong { color: var(--warn); }
    .detail.err strong { color: var(--err); }
    .table {
      display: grid;
      border: 1px solid var(--border);
      background: var(--panel-alt);
      overflow: hidden;
    }
    .table-row {
      display: grid;
      grid-template-columns: var(--columns, repeat(4, minmax(120px, 1fr)));
      border-bottom: 1px solid var(--border);
    }
    .table-row:last-child {
      border-bottom: 0;
    }
    .table-row > div {
      min-width: 0;
      padding: 7px 9px;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
      border-right: 1px solid var(--border);
    }
    .table-row > div:last-child {
      border-right: 0;
    }
    .table-row.selectable {
      cursor: pointer;
    }
    .table-row.selectable:hover {
      background: var(--vscode-list-hoverBackground);
    }
    .table-row.selectable.active {
      background: var(--vscode-list-activeSelectionBackground);
      color: var(--vscode-list-activeSelectionForeground);
    }
    .table-head {
      color: var(--muted);
      background: var(--bg);
      font-size: 12px;
    }
    .table strong {
      font-weight: 600;
    }
    .table-row .row-action {
      justify-self: start;
      min-height: 24px;
      padding: 2px 8px;
    }
    .table-row.clickable {
      cursor: pointer;
    }
    .split {
      display: grid;
      grid-template-columns: minmax(240px, 0.8fr) minmax(0, 1.2fr);
      gap: 14px;
      align-items: start;
    }
    .list {
      display: grid;
      gap: 8px;
    }
    .feature {
      display: grid;
      grid-template-columns: minmax(220px, 1fr) auto;
      gap: 10px;
      align-items: center;
      padding: 10px 0;
      border-bottom: 1px solid var(--border);
    }
    .feature:last-child {
      border-bottom: 0;
    }
    .feature h3 {
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
    }
    .feature-state {
      margin-top: 4px;
      font-size: 12px;
      color: var(--muted);
    }
    .feature-state.on {
      color: var(--ok);
    }
    .feature-state.off {
      color: var(--warn);
    }
    .search-row {
      max-width: 360px;
    }
    .modal-backdrop {
      position: fixed;
      inset: 0;
      z-index: 10;
      display: grid;
      place-items: center;
      background: rgba(0, 0, 0, 0.35);
    }
    .modal-panel {
      width: min(760px, calc(100vw - 32px));
      display: grid;
      gap: 12px;
      padding: 14px;
      border: 1px solid var(--border);
      background: var(--panel);
      box-shadow: 0 8px 24px rgba(0, 0, 0, 0.35);
    }
    .modal-panel.wide {
      width: min(980px, calc(100vw - 32px));
    }
    .section-actions {
      display: flex;
      gap: 8px;
      align-items: center;
      justify-content: space-between;
      flex-wrap: wrap;
    }
    .segmented button {
      color: var(--fg);
      background: transparent;
      border-color: var(--border);
    }
    .segmented button.active {
      border-color: var(--focus);
      background: var(--vscode-button-background);
      color: var(--vscode-button-foreground);
    }
    .feature-control button.active {
      font-weight: 600;
      outline: 1px solid var(--focus);
      outline-offset: -1px;
    }
    .feature-control button.state-default.active {
      color: var(--fg);
      background: var(--vscode-button-secondaryBackground);
      border-color: var(--border);
    }
    .feature-control button.state-on.active {
      color: var(--vscode-button-foreground);
      background: var(--vscode-button-background);
      border-color: var(--focus);
    }
    .feature-control button.state-off.active {
      color: var(--err);
      background: transparent;
      border-color: var(--err);
    }
    .empty {
      padding: 16px;
      border: 1px dashed var(--border);
      color: var(--muted);
      background: var(--panel-alt);
    }
    .diagnostic {
      border-left: 3px solid var(--border);
      padding: 8px 10px;
      background: var(--panel-alt);
    }
    .diagnostic.error {
      border-left-color: var(--err);
    }
    .diagnostic.warning {
      border-left-color: var(--warn);
    }
    .error-box {
      border-color: var(--vscode-inputValidation-errorBorder);
      background: var(--vscode-inputValidation-errorBackground);
      color: var(--vscode-errorForeground);
    }
    @media (max-width: 860px) {
      header, .layout, .split {
        grid-template-columns: 1fr;
      }
      nav {
        border-right: 0;
        border-bottom: 1px solid var(--border);
      }
      .feature {
        grid-template-columns: 1fr;
      }
    }
  </style>
</head>
<body>
  <header>
    <div>
      <h1 id="title">NGIN Project</h1>
      <div class="subtitle" id="subtitle"></div>
    </div>
    <div class="actions">
      <button id="validate">Validate</button>
      <button class="secondary" id="open-source">XML Source</button>
    </div>
  </header>
  <div class="layout">
    <nav id="tabs"></nav>
    <main id="app"></main>
  </div>
  <script nonce="${scriptNonce}">
    const vscode = acquireVsCodeApi();
    let model = undefined;
    let activeTab = 'project';
    let selectedProfileName = undefined;
    let activeInputScope = '';
    let activeInputBlock = 'Sources';
    let activeReferenceScope = '';
    let packageSearchText = '';
    let selectedPackageName = undefined;
    let showPackageDetailsDialog = false;
    let showAddPackageDialog = false;
    let selectedEnvironmentName = undefined;
    let showProjectDialog = false;
    let showAddProfileDialog = false;
    let showProfileDialog = false;
    let showRunDialog = false;
    let showInputRuleDialog = false;
    let editingInputIndex = undefined;
    let showVariableDialog = false;
    let editingVariableIndex = undefined;
    let showEnvironmentDialog = false;

    const tabs = [
      ['project', 'Project'],
      ['profiles', 'Build Profiles'],
      ['inputs', 'Source Files'],
      ['packages', 'Packages'],
      ['run', 'Run'],
      ['environment', 'Environment'],
      ['advanced', 'Advanced']
    ];

    const clean = (value) => value === undefined || value === null ? '' : String(value);
    const optional = (value) => {
      const text = clean(value).trim();
      return text.length ? text : undefined;
    };
    const esc = (value) => clean(value).replace(/[&<>"']/g, (ch) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[ch]));
    const post = (message) => vscode.postMessage(message);
    const byId = (id) => document.getElementById(id);

    function currentProfile() {
      const selected = selectedProfileName || model.activeProfile;
      return model.profiles.find((profile) => profile.name === selected) || model.profiles[0];
    }

    function profileNames() {
      return model.profiles.map((profile) => profile.name);
    }

    function optionList(values, selected, includeEmptyLabel) {
      const seen = new Set();
      const options = [];
      if (includeEmptyLabel !== undefined) {
        options.push('<option value="">' + esc(includeEmptyLabel) + '</option>');
      }
      for (const value of values.filter(Boolean)) {
        if (seen.has(value)) continue;
        seen.add(value);
        options.push('<option value="' + esc(value) + '"' + (value === selected ? ' selected' : '') + '>' + esc(value) + '</option>');
      }
      return options.join('');
    }

    function field(id, label, value, attrs) {
      return '<label><span>' + esc(label) + '</span><input id="' + id + '" value="' + esc(value) + '"' + (attrs || '') + '></label>';
    }

    function selectField(id, label, values, selected, emptyLabel) {
      return '<label><span>' + esc(label) + '</span><select id="' + id + '">' + optionList(values, selected, emptyLabel) + '</select></label>';
    }

    function detail(label, value, className) {
      if (!value && value !== 0) return '';
      return '<div class="detail ' + esc(className || '') + '"><span>' + esc(label) + '</span><strong title="' + esc(value) + '">' + esc(value) + '</strong></div>';
    }

    function details(items, emptyText) {
      const body = items.filter(Boolean).join('');
      return body ? '<div class="details">' + body + '</div>' : '<div class="empty">' + esc(emptyText || 'No values.') + '</div>';
    }

    function cell(value) {
      return '<div title="' + esc(value) + '">' + esc(value) + '</div>';
    }

    function table(columns, rows, emptyText, template) {
      if (!rows.length) {
        return '<div class="empty">' + esc(emptyText) + '</div>';
      }
      const style = template ? ' style="--columns: ' + esc(template) + ';"' : '';
      return '<div class="table">' +
        '<div class="table-row table-head"' + style + '>' + columns.map(cell).join('') + '</div>' +
        rows.map((row) => '<div class="table-row"' + style + '>' + row.map(cell).join('') + '</div>').join('') +
        '</div>';
    }

    function renderTabs() {
      byId('tabs').innerHTML = tabs.map(([id, label]) =>
        '<button class="nav-button ' + (id === activeTab ? 'active' : '') + '" data-tab="' + id + '">' + esc(label) + '</button>'
      ).join('');
    }

    function render() {
      if (!model) return;
      selectedProfileName = selectedProfileName || model.activeProfile;
      selectedEnvironmentName = selectedEnvironmentName || model.environments[0]?.name;
      byId('title').textContent = model.project.name || model.resolved.projectName || 'NGIN Project';
      byId('subtitle').textContent = model.path;
      renderTabs();

      const app = byId('app');
      if (model.parseError) {
        app.innerHTML = '<section class="active"><div class="band error-box">' + esc(model.parseError) + '</div></section>';
        return;
      }

      app.innerHTML =
        section('project', renderProject()) +
        section('profiles', renderProfiles()) +
        section('inputs', renderInputs()) +
        section('packages', renderPackages()) +
        section('run', renderRun()) +
        section('environment', renderEnvironment()) +
        section('advanced', renderAdvanced());
    }

    function section(id, body) {
      return '<section id="section-' + id + '" class="' + (id === activeTab ? 'active' : '') + '">' + body + '</section>';
    }

    function renderProject() {
      const resolved = model.resolved;
      const projectRows = [
        ['Name', model.project.name || resolved.projectName || ''],
        ['Template', model.project.template || resolved.projectType || ''],
        ['Default Profile', model.project.defaultProfile || ''],
        ['Workspace', resolved.workspaceName || '']
      ].filter((row) => row[1]);
      const activeRows = [
        ['Profile', resolved.profileName || model.activeProfile || ''],
        ['Build', resolved.buildType || ''],
        ['Platform', resolved.platform || [resolved.operatingSystem, resolved.architecture].filter(Boolean).join('-')],
        ['Environment', resolved.environment || '']
      ].filter((row) => row[1]);
      return '<div class="band"><div class="section-actions"><h2>Project</h2><button class="secondary" id="edit-project">Edit</button></div>' +
        table(['Property', 'Value'], projectRows, 'No project properties.', '180px minmax(240px, 1fr)') + '</div>' +
        '<div class="band"><h2>Active Profile</h2>' +
        table(['Property', 'Value'], activeRows, 'No active profile.', '180px minmax(240px, 1fr)') + '</div>' +
        renderProjectDialog();
    }

    function renderProjectDialog() {
      if (!showProjectDialog) return '';
      return '<div class="modal-backdrop"><div class="modal-panel">' +
        '<h2>Edit Project</h2><div class="grid">' +
        field('project-name', 'Name', model.project.name) +
        selectField('project-template', 'Template', ['Application', 'Library', 'Tool', model.project.template], model.project.template, '') +
        selectField('project-default-profile', 'Default Profile', profileNames(), model.project.defaultProfile, '') +
        '</div><div class="actions"><button id="save-project">Save</button><button class="secondary" id="cancel-project">Cancel</button></div>' +
        '</div></div>';
    }

    function renderProfiles() {
      const selected = currentProfile();
      return '<div class="band"><div class="section-actions"><h2>Build Profiles</h2><button class="secondary" id="show-add-profile">Add Profile</button></div>' +
        renderProfileTable(selected?.name) + '</div>' +
        renderAddProfileDialog() +
        renderProfileDialog();
    }

    function renderProfileTable(selectedName) {
      if (!model.profiles.length) {
        return '<div class="empty">No profiles.</div>';
      }
      const style = ' style="--columns: minmax(180px, 1fr) 120px minmax(140px, 1fr) minmax(140px, 1fr);"';
      return '<div class="table">' +
        '<div class="table-row table-head"' + style + '>' + cell('Name') + cell('Build') + cell('Platform') + cell('Settings') + '</div>' +
        model.profiles.map((profile) => '<div class="table-row selectable ' + (profile.name === selectedName ? 'active' : '') + '" data-profile="' + esc(profile.name) + '" data-edit-profile' + style + '>' +
          cell(profile.name) + cell(profile.buildType || '') + cell(profile.platform || [profile.operatingSystem, profile.architecture].filter(Boolean).join('-')) + cell(profile.environment || '') +
          '</div>').join('') +
        '</div>';
    }

    function renderAddProfileDialog() {
      if (!showAddProfileDialog) return '';
      return '<div class="modal-backdrop"><div class="modal-panel">' +
        '<h2>Add Profile</h2>' + field('new-profile-name', 'Name', '', ' placeholder="Runtime"') +
        '<div class="actions"><button id="add-profile">Add</button><button class="secondary" id="cancel-add-profile">Cancel</button></div>' +
        '</div></div>';
    }

    function renderProfileDialog() {
      if (!showProfileDialog) return '';
      const profile = currentProfile();
      if (!profile) return '';
      return '<div class="modal-backdrop"><div class="modal-panel wide"><h2>Edit Profile</h2><div class="grid">' +
        field('profile-name', 'Name', profile.name) +
        field('profile-template', 'Template', profile.template) +
        selectField('profile-build-type', 'Build Type', ['Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel', profile.buildType], profile.buildType, '') +
        selectField('profile-platform', 'Platform', ['linux-x64', 'windows-x64', 'macos-x64', 'macos-arm64', profile.platform], profile.platform, '') +
        selectField('profile-os', 'Operating System', ['linux', 'windows', 'macos', profile.operatingSystem], profile.operatingSystem, '') +
        selectField('profile-arch', 'Architecture', ['x64', 'arm64', profile.architecture], profile.architecture, '') +
        selectField('profile-env', 'Environment', model.environments.map((env) => env.name).concat(profile.environment || []), profile.environment, '') +
        field('profile-launch-executable', 'Launch Executable', profile.launchExecutable) +
        field('profile-launch-working-directory', 'Launch Working Directory', profile.launchWorkingDirectory) +
        '</div><div class="actions"><button id="save-profile">Save</button><button class="secondary" id="delete-profile">Delete</button><button class="secondary" id="cancel-profile">Cancel</button></div>' +
        '</div></div>';
    }

    function selectedInputEntries() {
      const source = activeInputScope ? model.profiles.find((profile) => profile.name === activeInputScope)?.inputs : model.project.inputs;
      return source?.[activeInputBlock] || [];
    }

    function renderScopeButtons(current, includeRoot, group) {
      const entries = includeRoot ? [['', 'Project']] : [];
      for (const name of profileNames()) entries.push([name, name]);
      return '<div class="segmented">' + entries.map(([value, label]) =>
        '<button class="' + (value === current ? 'active' : '') + '" data-' + group + '="' + esc(value) + '">' + esc(label) + '</button>'
      ).join('') + '</div>';
    }

    function renderBlockButtons() {
      return '<div class="segmented">' + ['Sources', 'Headers', 'Configs'].map((block) =>
        '<button class="' + (block === activeInputBlock ? 'active' : '') + '" data-input-block="' + block + '">' + block + '</button>'
      ).join('') + '</div>';
    }

    function renderInputs() {
      const resolvedInputs = table(
        ['Type', 'Path', 'From', 'Staged As'],
        model.resolved.inputs.map((entry) => [
          entry.kind,
          entry.source || '',
          entry.ownerName || '',
          entry.stagedRelativePath || ''
        ]),
        'No files are included for the selected profile.',
        '120px minmax(240px, 1.4fr) minmax(160px, 1fr) minmax(240px, 1fr)'
      );
      return '<div class="band"><h2>Included Files</h2>' + resolvedInputs + '</div>' +
        '<div class="band"><div class="section-actions"><h2>Rules</h2><button class="secondary" id="show-add-input">Add Rule</button></div>' +
        '<div class="inline">' + renderScopeButtons(activeInputScope, true, 'input-scope') + renderBlockButtons() + '</div>' +
        renderInputRuleTable() + '</div>' +
        renderInputRuleDialog();
    }

    function renderInputRuleTable() {
      const entries = selectedInputEntries();
      if (!entries.length) {
        return '<div class="empty">No rules for this scope.</div>';
      }
      const style = ' style="--columns: 120px minmax(220px, 1fr) minmax(260px, 1.4fr) 96px;"';
      return '<div class="table">' +
        '<div class="table-row table-head"' + style + '>' + cell('Rule') + cell('Path') + cell('Applies When') + cell('') + '</div>' +
        entries.map((entry, index) => '<div class="table-row selectable"' + style + ' data-input-index="' + index + '">' +
          cell(inputRuleKind(entry)) + cell(inputRulePath(entry)) + cell(inputRuleWhen(entry)) +
          '<div><button class="secondary row-action" data-input-remove="' + index + '">Remove</button></div>' +
          '</div>').join('') +
        '</div>';
    }

    function inputRuleKind(entry) {
      if (entry.mode === 'Directory') return 'Folder scan';
      if (entry.mode === 'File') return 'Single file';
      return 'Glob pattern';
    }

    function inputRulePath(entry) {
      if (entry.mode === 'Glob') return entry.include || '';
      const include = entry.include ? ' include ' + entry.include : '';
      const exclude = entry.exclude ? ' exclude ' + entry.exclude : '';
      return clean(entry.path) + include + exclude;
    }

    function inputRuleWhen(entry) {
      const values = [
        entry.platform,
        entry.operatingSystem,
        entry.architecture,
        entry.buildType,
        entry.environment ? 'settings ' + entry.environment : undefined,
        entry.condition
      ].filter(Boolean);
      return values.length ? values.join(', ') : 'Always';
    }

    function renderInputRuleDialog() {
      if (!showInputRuleDialog) return '';
      const entry = editingInputIndex === undefined ? { mode: 'Directory' } : selectedInputEntries()[editingInputIndex] || { mode: 'Directory' };
      return '<div class="modal-backdrop"><div class="modal-panel wide">' +
        '<h2>' + (editingInputIndex === undefined ? 'Add Rule' : 'Edit Rule') + '</h2><div class="grid">' +
        '<label><span>Rule</span><select id="input-mode"><option value="Directory"' + (entry.mode === 'Directory' ? ' selected' : '') + '>Folder scan</option><option value="File"' + (entry.mode === 'File' ? ' selected' : '') + '>Single file</option><option value="Glob"' + (entry.mode === 'Glob' ? ' selected' : '') + '>Glob pattern</option></select></label>' +
        field('input-path', 'Path / root', entry.path, ' placeholder="src or src/main.cpp"') +
        field('input-include', 'Include pattern', entry.include, ' placeholder="**/*.cpp;**/*.hpp"') +
        field('input-exclude', 'Exclude pattern', entry.exclude, ' placeholder="**/*.generated.cpp"') +
        selectField('input-os', 'Operating System', ['linux', 'windows', 'macos', entry.operatingSystem], entry.operatingSystem, '') +
        selectField('input-arch', 'Architecture', ['x64', 'arm64', entry.architecture], entry.architecture, '') +
        selectField('input-build', 'Build Type', ['Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel', entry.buildType], entry.buildType, '') +
        selectField('input-env', 'Settings', model.environments.map((env) => env.name).concat(entry.environment || []), entry.environment, '') +
        field('input-condition', 'Condition', entry.condition) +
        '</div><div class="actions"><button id="confirm-input-rule">Save</button><button class="secondary" id="cancel-input-rule">Cancel</button></div>' +
        '</div></div>';
    }

    function collectInputDialog() {
      return {
        mode: byId('input-mode').value,
        path: optional(byId('input-path').value),
        include: optional(byId('input-include').value),
        exclude: optional(byId('input-exclude').value),
        operatingSystem: optional(byId('input-os').value),
        architecture: optional(byId('input-arch').value),
        buildType: optional(byId('input-build').value),
        environment: optional(byId('input-env').value),
        condition: optional(byId('input-condition').value)
      };
    }

    function hasInputRuleValue(entry) {
      return entry.path !== undefined || entry.include !== undefined || entry.exclude !== undefined;
    }

    function referencesForScope(scope) {
      return scope ? model.profiles.find((profile) => profile.name === scope)?.packageReferences || [] : model.project.packageReferences;
    }

    function packageSearchMatches(pkg) {
      const query = packageSearchText.trim().toLowerCase();
      if (!query) return true;
      return pkg.name.toLowerCase().includes(query) || clean(pkg.version).toLowerCase().includes(query);
    }

    function selectedPackage() {
      return model.resolved.packages.find((pkg) => pkg.name === selectedPackageName);
    }

    function renderPackageList(packages) {
      if (!packages.length) {
        return '<div class="empty">No packages match the current search.</div>';
      }
      const style = ' style="--columns: minmax(220px, 1fr) 100px;"';
      return '<div class="table">' +
        '<div class="table-row table-head"' + style + '>' + cell('Package') + cell('Version') + '</div>' +
        packages.map((pkg) =>
          '<div class="table-row selectable ' + (pkg.name === selectedPackage()?.name ? 'active' : '') + '" data-package-select="' + esc(pkg.name) + '"' + style + '>' +
          cell(pkg.name) + cell(pkg.version || '') +
          '</div>'
        ).join('') +
        '</div>';
    }

    function renderPackageDetailsDialog() {
      if (!showPackageDetailsDialog) return '';
      const pkg = selectedPackage();
      if (!pkg) return '';
      const packageFeatures = model.features.filter((feature) => feature.packageName === pkg.name);
      return '<div class="modal-backdrop"><div class="modal-panel">' +
        '<h2>' + esc(pkg.name) + '</h2>' + details([
        detail('Package', pkg.name),
        detail('Version', pkg.version),
        detail('Required By', pkg.requiredBy.join(', ')),
        detail('Manifest', pkg.manifestPath)
      ], 'No package details.') +
        '<h2>Features</h2><div class="list">' +
        (packageFeatures.length ? packageFeatures.map(renderFeature).join('') : '<div class="empty">This package has no features for the selected profile.</div>') +
        '</div><div class="actions"><button class="secondary" id="close-package-details">Close</button></div>' +
        '</div></div>';
    }

    function renderAddPackageDialog() {
      if (!showAddPackageDialog) return '';
      return '<div class="modal-backdrop"><div class="modal-panel">' +
        '<h2>Add Package Reference</h2><div class="grid">' +
        '<label><span>Scope</span><select id="new-reference-scope">' + optionList(profileNames(), activeReferenceScope, 'Project') + '</select></label>' +
        field('new-reference-name', 'Package', selectedPackageName || '', ' placeholder="NGIN.Core"') +
        field('new-reference-version', 'Version', '', ' placeholder=">=0.1.0 <0.2.0"') +
        '<label><span>Optional</span><select id="new-reference-optional"><option value=""></option><option value="false">No</option><option value="true">Yes</option></select></label>' +
        '</div><div class="actions"><button id="confirm-add-reference">Add</button><button class="secondary" id="cancel-add-reference">Cancel</button></div>' +
        '</div></div>';
    }

    function renderPackages() {
      const filteredPackages = model.resolved.packages.filter(packageSearchMatches);
      return '<div class="band"><h2>Packages</h2>' +
        '<div class="inline"><label class="search-row"><span>Search packages</span><input id="package-search" value="' + esc(packageSearchText) + '" placeholder="Package name or version"></label>' +
        '<button class="secondary" id="add-reference">Add Package</button></div>' +
        '<div id="package-list">' + renderPackageList(filteredPackages) + '</div>' +
        '</div>' +
        renderPackageDetailsDialog() +
        renderAddPackageDialog();
    }

    function renderFeature(feature) {
      const states = [
        ['inherit', 'Default', 'Follow the package and profile defaults', 'state-default'],
        ['use', 'On', 'Enable this feature for the selected profile', 'state-on'],
        ['disable', 'Off', 'Disable this feature for the selected profile', 'state-off']
      ];
      const effective = featureEffectiveState(feature);
      return '<div class="feature"><div><h3 title="' + esc(feature.featureName) + '">' + esc(feature.featureName) + '</h3>' +
        '<div class="muted">' + esc(feature.description) + '</div>' +
        '<div class="feature-state ' + esc(effective.className) + '">' + esc(effective.label) + '</div></div><div class="segmented feature-control">' +
        states.map(([state, label, title, className]) => '<button title="' + esc(title) + '" ' + (feature.readOnly ? 'disabled ' : '') + 'class="' + esc(className + (feature.state === state ? ' active' : '')) + '" data-feature-package="' + esc(feature.packageName) + '" data-feature-name="' + esc(feature.featureName) + '" data-feature-state="' + state + '">' + label + '</button>').join('') +
        '</div></div>';
    }

    function featureEffectiveState(feature) {
      if (feature.state === 'use') return { label: 'Effective: On', className: 'on' };
      if (feature.state === 'disable') return { label: 'Effective: Off', className: 'off' };
      if (feature.resolvedState === 'selected') return { label: 'Effective: On by default', className: 'on' };
      if (feature.resolvedState === 'available') return { label: 'Effective: Off by default', className: 'off' };
      if (feature.resolvedState === 'disabled') return { label: 'Effective: Off', className: 'off' };
      if (feature.resolvedState === 'conditionExcluded') return { label: 'Effective: Off, condition excluded', className: 'off' };
      if (feature.resolvedState === 'unavailable') return { label: 'Effective: Unavailable', className: 'off' };
      return { label: 'Effective: ' + (feature.resolvedState || 'unknown'), className: '' };
    }

    function renderRun() {
      const profile = currentProfile();
      const rows = [
        ['Profile', profile?.name || model.activeProfile || ''],
        ['Executable', profile?.launchExecutable || model.resolved.launchExecutable || ''],
        ['Working Directory', profile?.launchWorkingDirectory || model.resolved.launchWorkingDirectory || ''],
        ['Output Folder', model.resolved.outputDir || '']
      ].filter((row) => row[1]);
      return '<div class="band"><div class="section-actions"><h2>Run Target</h2><button class="secondary" id="edit-run">Edit Project Default</button></div>' +
        table(['Property', 'Value'], rows, 'No run target.', '180px minmax(260px, 1fr)') + '</div>' +
        renderRunDialog();
    }

    function renderRunDialog() {
      if (!showRunDialog) return '';
      return '<div class="modal-backdrop"><div class="modal-panel">' +
        '<h2>Edit Project Run Target</h2><div class="grid">' +
        field('root-launch-executable', 'Executable', model.project.launchExecutable || model.resolved.launchExecutable) +
        field('root-launch-working-directory', 'Working Directory', model.project.launchWorkingDirectory || model.resolved.launchWorkingDirectory) +
        '</div><div class="actions"><button id="save-root-launch">Save</button><button class="secondary" id="cancel-run">Cancel</button></div>' +
        '</div></div>';
    }

    function selectedEnvironment() {
      return model.environments.find((env) => env.name === selectedEnvironmentName) || model.environments[0];
    }

    function renderEnvironment() {
      const env = selectedEnvironment();
      const resolved = table(
        ['Name', 'Source', 'Resolved'],
        model.resolved.environmentVariables.map((variable) => [
          variable.name,
          variable.source || '',
          variable.resolved === false ? 'No' : 'Yes'
        ]),
        'No variables are active for the selected profile.',
        'minmax(180px, 1fr) minmax(220px, 1.2fr) 100px'
      );
      const options = model.environments.length ? model.environments.map((entry) => '<option value="' + esc(entry.name) + '"' + (entry.name === env?.name ? ' selected' : '') + '>' + esc(entry.name) + '</option>').join('') : '<option value=""></option>';
      return '<div class="band"><h2>Active Variables</h2>' + resolved + '</div>' +
        '<div class="band"><div class="section-actions"><h2>Definitions</h2><div class="actions"><button class="secondary" id="show-add-environment">Add Environment</button><button class="secondary" id="show-add-variable">Add Variable</button></div></div>' +
        '<label class="search-row"><span>Environment</span><select id="environment-select">' + options + '</select></label>' +
        renderVariableTable(env) + '</div>' +
        renderEnvironmentDialog() +
        renderVariableDialog();
    }

    function renderVariableTable(env) {
      const variables = env?.variables || [];
      if (!env) {
        return '<div class="empty">No environments defined.</div>';
      }
      if (!variables.length) {
        return '<div class="empty">No variables in this environment.</div>';
      }
      const style = ' style="--columns: minmax(180px, 1fr) minmax(220px, 1.2fr) 100px 100px;"';
      return '<div class="table">' +
        '<div class="table-row table-head"' + style + '>' + cell('Name') + cell('Source') + cell('Required') + cell('') + '</div>' +
        variables.map((variable, index) => '<div class="table-row selectable"' + style + ' data-variable-index="' + index + '">' +
          cell(variable.name) + cell(variableSourceLabel(variable)) + cell(variable.required === undefined ? '' : variable.required ? 'Yes' : 'No') +
          '<div><button class="secondary row-action" data-variable-remove="' + index + '">Remove</button></div>' +
          '</div>').join('') +
        '</div>';
    }

    function variableSourceLabel(variable) {
      if (variable.fromLocalSetting) return 'Local setting: ' + variable.fromLocalSetting;
      if (variable.fromEnvironment) return 'Environment: ' + variable.fromEnvironment;
      return variable.secret ? 'Value: secret' : 'Value';
    }

    function renderEnvironmentDialog() {
      if (!showEnvironmentDialog) return '';
      return '<div class="modal-backdrop"><div class="modal-panel">' +
        '<h2>Add Environment</h2>' + field('new-environment-name', 'Name', '', ' placeholder="local"') +
        '<div class="actions"><button id="confirm-add-environment">Add</button><button class="secondary" id="cancel-add-environment">Cancel</button></div>' +
        '</div></div>';
    }

    function renderVariableDialog() {
      if (!showVariableDialog) return '';
      const env = selectedEnvironment();
      const variable = editingVariableIndex === undefined ? {} : env?.variables[editingVariableIndex] || {};
      const type = variable.fromLocalSetting ? 'local' : variable.fromEnvironment ? 'environment' : 'literal';
      const sourceValue = variable.fromLocalSetting || variable.fromEnvironment || variable.value || '';
      return '<div class="modal-backdrop"><div class="modal-panel">' +
        '<h2>' + (editingVariableIndex === undefined ? 'Add Variable' : 'Edit Variable') + '</h2><div class="grid">' +
        field('variable-name', 'Name', variable.name) +
        '<label><span>Source</span><select id="variable-source"><option value="literal"' + (type === 'literal' ? ' selected' : '') + '>Value</option><option value="environment"' + (type === 'environment' ? ' selected' : '') + '>From environment variable</option><option value="local"' + (type === 'local' ? ' selected' : '') + '>From local setting</option></select></label>' +
        field('variable-value', 'Value / Key', sourceValue) +
        '<label><span>Required</span><select id="variable-required"><option value=""></option><option value="true"' + (variable.required ? ' selected' : '') + '>Yes</option><option value="false"' + (variable.required === false ? ' selected' : '') + '>No</option></select></label>' +
        '<label><span>Secret</span><select id="variable-secret"><option value=""></option><option value="true"' + (variable.secret ? ' selected' : '') + '>Yes</option><option value="false"' + (variable.secret === false ? ' selected' : '') + '>No</option></select></label>' +
        '</div><div class="actions"><button id="confirm-variable">Save</button><button class="secondary" id="cancel-variable">Cancel</button></div>' +
        '</div></div>';
    }

    function collectVariableDialog() {
      const sourceType = byId('variable-source').value;
      const sourceValue = optional(byId('variable-value').value);
      const requiredValue = optional(byId('variable-required').value);
      const secretValue = optional(byId('variable-secret').value);
      return {
        name: optional(byId('variable-name').value),
        value: sourceType === 'literal' ? sourceValue : undefined,
        fromEnvironment: sourceType === 'environment' ? sourceValue : undefined,
        fromLocalSetting: sourceType === 'local' ? sourceValue : undefined,
        required: requiredValue === undefined ? undefined : requiredValue === 'true',
        secret: secretValue === undefined ? undefined : secretValue === 'true'
      };
    }

    function renderAdvanced() {
      const unsupported = model.unsupportedSections.length
        ? table(['Section', 'Editor'], model.unsupportedSections.map((name) => [name, 'XML source']), 'No advanced XML-only sections in this file.', 'minmax(160px, 1fr) minmax(160px, 1fr)')
        : '<div class="empty">No advanced XML-only sections in this file.</div>';
      return '<div class="band"><h2>Advanced XML Sections</h2>' + unsupported + '<div class="actions"><button class="secondary" id="open-source">Open XML Source</button></div></div>' + renderDiagnostics();
    }

    function renderDiagnostics() {
      if (!model.diagnostics.length) {
        return '<div class="band"><h2>Validation</h2><div class="empty">No validation diagnostics.</div></div>';
      }
      return '<div class="band"><h2>Validation</h2><div class="list">' + model.diagnostics.map((diagnostic) => {
        const cls = diagnostic.startsWith('error:') ? 'error' : diagnostic.startsWith('warning:') ? 'warning' : '';
        return '<div class="diagnostic ' + cls + '">' + esc(diagnostic) + '</div>';
      }).join('') + '</div></div>';
    }

    document.addEventListener('click', (event) => {
      const target = event.target;
      if (!(target instanceof HTMLElement)) return;
      const packageTarget = target.closest('[data-package-select]');
      const profileTarget = target.closest('[data-edit-profile]');
      const inputTarget = target.closest('[data-input-index]');
      const variableTarget = target.closest('[data-variable-index]');
      if (target.matches('[data-tab]')) {
        activeTab = target.dataset.tab;
        render();
      }
      if (target.matches('[data-input-remove]')) {
        const index = Number(target.dataset.inputRemove);
        const entries = selectedInputEntries().slice();
        entries.splice(index, 1);
        post({ type: 'setInputEntries', profileName: optional(activeInputScope), block: activeInputBlock, entries });
        return;
      }
      if (target.matches('[data-variable-remove]')) {
        const index = Number(target.dataset.variableRemove);
        const env = selectedEnvironment();
        if (env) {
          const variables = env.variables.slice();
          variables.splice(index, 1);
          post({ type: 'setEnvironmentVariables', environmentName: env.name, variables });
        }
        return;
      }
      if (profileTarget instanceof HTMLElement) {
        selectedProfileName = profileTarget.dataset.profile;
        model.activeProfile = selectedProfileName;
        showProfileDialog = true;
        render();
      }
      if (target.matches('[data-input-scope]')) {
        activeInputScope = target.dataset.inputScope || '';
        render();
      }
      if (target.matches('[data-input-block]')) {
        activeInputBlock = target.dataset.inputBlock || 'Sources';
        render();
      }
      if (target.matches('[data-reference-scope]')) {
        activeReferenceScope = target.dataset.referenceScope || '';
        render();
      }
      if (packageTarget instanceof HTMLElement) {
        selectedPackageName = packageTarget.dataset.packageSelect;
        showPackageDetailsDialog = true;
        render();
      }
      if (inputTarget instanceof HTMLElement) {
        editingInputIndex = Number(inputTarget.dataset.inputIndex);
        showInputRuleDialog = true;
        render();
      }
      if (variableTarget instanceof HTMLElement) {
        editingVariableIndex = Number(variableTarget.dataset.variableIndex);
        showVariableDialog = true;
        render();
      }
      if (target.id === 'open-source') post({ type: 'openSource' });
      if (target.id === 'validate') post({ type: 'validate' });
      if (target.id === 'edit-project') {
        showProjectDialog = true;
        render();
      }
      if (target.id === 'cancel-project') {
        showProjectDialog = false;
        render();
      }
      if (target.id === 'save-project') {
        showProjectDialog = false;
        post({ type: 'updateProject', name: optional(byId('project-name').value), template: optional(byId('project-template').value), defaultProfile: optional(byId('project-default-profile').value) });
      }
      if (target.id === 'edit-run') {
        showRunDialog = true;
        render();
      }
      if (target.id === 'cancel-run') {
        showRunDialog = false;
        render();
      }
      if (target.id === 'save-root-launch') {
        showRunDialog = false;
        post({ type: 'setRootLaunch', executable: optional(byId('root-launch-executable').value), workingDirectory: optional(byId('root-launch-working-directory').value) });
      }
      if (target.id === 'show-add-profile') {
        showAddProfileDialog = true;
        render();
      }
      if (target.id === 'cancel-add-profile') {
        showAddProfileDialog = false;
        render();
      }
      if (target.id === 'add-profile') {
        showAddProfileDialog = false;
        post({ type: 'addProfile', name: byId('new-profile-name').value });
      }
      if (target.id === 'cancel-profile') {
        showProfileDialog = false;
        render();
      }
      if (target.id === 'delete-profile') {
        showProfileDialog = false;
        post({ type: 'deleteProfile', name: currentProfile()?.name });
      }
      if (target.id === 'save-profile') {
        showProfileDialog = false;
        post({ type: 'updateProfile', originalName: currentProfile()?.name, name: optional(byId('profile-name').value), template: optional(byId('profile-template').value), buildType: optional(byId('profile-build-type').value), platform: optional(byId('profile-platform').value), operatingSystem: optional(byId('profile-os').value), architecture: optional(byId('profile-arch').value), environment: optional(byId('profile-env').value), launchExecutable: optional(byId('profile-launch-executable').value), launchWorkingDirectory: optional(byId('profile-launch-working-directory').value) });
      }
      if (target.id === 'show-add-input') {
        editingInputIndex = undefined;
        showInputRuleDialog = true;
        render();
      }
      if (target.id === 'cancel-input-rule') {
        showInputRuleDialog = false;
        editingInputIndex = undefined;
        render();
      }
      if (target.id === 'confirm-input-rule') {
        const entries = selectedInputEntries().slice();
        const entry = collectInputDialog();
        if (hasInputRuleValue(entry)) {
          if (editingInputIndex === undefined) {
            entries.push(entry);
          } else {
            entries[editingInputIndex] = entry;
          }
        }
        showInputRuleDialog = false;
        editingInputIndex = undefined;
        post({ type: 'setInputEntries', profileName: optional(activeInputScope), block: activeInputBlock, entries });
      }
      if (target.id === 'add-reference') {
        showAddPackageDialog = true;
        render();
      }
      if (target.id === 'cancel-add-reference') {
        showAddPackageDialog = false;
        render();
      }
      if (target.id === 'close-package-details') {
        showPackageDetailsDialog = false;
        render();
      }
      if (target.id === 'confirm-add-reference') {
        const packageName = optional(byId('new-reference-name')?.value);
        if (packageName) {
          const optionalValue = optional(byId('new-reference-optional')?.value);
          const referenceScope = optional(byId('new-reference-scope')?.value);
          const nextReferences = referencesForScope(referenceScope).concat([{
            name: packageName,
            version: optional(byId('new-reference-version')?.value),
            optional: optionalValue === undefined ? undefined : optionalValue === 'true'
          }]);
          activeReferenceScope = referenceScope || '';
          showAddPackageDialog = false;
          post({ type: 'setPackageReferences', profileName: referenceScope, references: nextReferences });
        }
      }
      if (target.id === 'show-add-environment') {
        showEnvironmentDialog = true;
        render();
      }
      if (target.id === 'cancel-add-environment') {
        showEnvironmentDialog = false;
        render();
      }
      if (target.id === 'confirm-add-environment') {
        const environmentName = optional(byId('new-environment-name').value);
        if (environmentName) {
          selectedEnvironmentName = environmentName;
          showEnvironmentDialog = false;
          post({ type: 'setEnvironmentVariables', environmentName, variables: [] });
        }
      }
      if (target.id === 'show-add-variable') {
        editingVariableIndex = undefined;
        showVariableDialog = true;
        render();
      }
      if (target.id === 'cancel-variable') {
        showVariableDialog = false;
        editingVariableIndex = undefined;
        render();
      }
      if (target.id === 'confirm-variable') {
        const env = selectedEnvironment();
        const environmentName = env?.name || optional(byId('environment-select')?.value);
        if (environmentName) {
          const variables = (env?.variables || []).slice();
          const variable = collectVariableDialog();
          if (variable.name) {
            if (editingVariableIndex === undefined) {
              variables.push(variable);
            } else {
              variables[editingVariableIndex] = variable;
            }
          }
          showVariableDialog = false;
          editingVariableIndex = undefined;
          post({ type: 'setEnvironmentVariables', environmentName, variables });
        }
      }
      if (target.matches('[data-feature-state]')) {
        if (model.activeProfile) {
          post({ type: 'setFeatureState', profileName: model.activeProfile, packageName: target.dataset.featurePackage, featureName: target.dataset.featureName, state: target.dataset.featureState });
        }
      }
    });

    document.addEventListener('input', (event) => {
      const target = event.target;
      if (!(target instanceof HTMLElement)) return;
      if (target.id === 'package-search') {
        packageSearchText = target.value;
        const list = byId('package-list');
        if (list) {
          list.innerHTML = renderPackageList(model.resolved.packages.filter(packageSearchMatches));
        }
      }
    });

    document.addEventListener('change', (event) => {
      const target = event.target;
      if (!(target instanceof HTMLElement)) return;
      if (target.id === 'environment-select') {
        selectedEnvironmentName = target.value;
        render();
      }
    });

    window.addEventListener('message', (event) => {
      if (event.data?.type === 'model') {
        model = event.data.model;
        if (!profileNames().includes(selectedProfileName)) {
          selectedProfileName = model.activeProfile;
        }
        if (!model.environments.some((env) => env.name === selectedEnvironmentName)) {
          selectedEnvironmentName = model.environments[0]?.name;
        }
        if (!model.resolved.packages.some((pkg) => pkg.name === selectedPackageName)) {
          selectedPackageName = undefined;
        }
        render();
      }
    });
    post({ type: 'ready' });
  </script>
</body>
</html>`;
  }
}
