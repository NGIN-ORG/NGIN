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
    .row {
      border: 1px solid var(--border);
      background: var(--panel-alt);
    }
    .muted {
      color: var(--muted);
      font-size: 12px;
    }
    .row {
      display: grid;
      grid-template-columns: repeat(4, minmax(120px, 1fr)) auto;
      gap: 8px;
      align-items: end;
      padding: 10px;
    }
    .row.variable {
      grid-template-columns: minmax(140px, 1fr) 150px minmax(180px, 1.3fr) 100px 100px auto;
    }
    .row.reference {
      grid-template-columns: minmax(160px, 1.3fr) minmax(160px, 1fr) 100px auto;
    }
    .row.file-rule {
      grid-template-columns: 130px minmax(160px, 1.4fr) minmax(160px, 1.2fr) minmax(140px, 1fr) 110px 110px 110px minmax(130px, 1fr) auto;
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
    .table-head {
      color: var(--muted);
      background: var(--bg);
      font-size: 12px;
    }
    .table strong {
      font-weight: 600;
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
    .profile-card {
      text-align: left;
      color: var(--fg);
      background: var(--panel-alt);
      border: 1px solid var(--border);
      padding: 10px;
    }
    .profile-card.active {
      border-color: var(--focus);
      outline: 1px solid var(--focus);
    }
    .profile-card .details,
    .feature .details {
      border: 0;
      background: transparent;
      grid-template-columns: repeat(auto-fit, minmax(110px, 1fr));
    }
    .profile-card .detail,
    .feature .detail {
      padding: 5px 0 0;
      border: 0;
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
      .row, .row.variable, .row.reference, .feature {
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
    let selectedEnvironmentName = undefined;

    const tabs = [
      ['project', 'Project'],
      ['profiles', 'Build Profiles'],
      ['inputs', 'Source Files'],
      ['packages', 'Dependencies'],
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

    function statusDetails() {
      const resolved = model.resolved;
      return details([
        detail('Profile', resolved.profileName || model.activeProfile),
        detail('Build', resolved.buildType),
        detail('Platform', resolved.platform || [resolved.operatingSystem, resolved.architecture].filter(Boolean).join('-')),
        detail('Environment', resolved.environment),
        detail('Errors', resolved.diagnosticErrorCount, resolved.diagnosticErrorCount ? 'err' : 'ok'),
        detail('Warnings', resolved.diagnosticWarningCount, resolved.diagnosticWarningCount ? 'warn' : '')
      ]);
    }

    function renderProject() {
      const resolved = model.resolved;
      const unsupported = model.unsupportedSections.length
        ? table(['Section', 'Editor'], model.unsupportedSections.map((name) => [name, 'XML source']), 'No advanced XML-only sections in this file.', 'minmax(160px, 1fr) minmax(160px, 1fr)')
        : '<div class="muted">No advanced XML-only sections in this file.</div>';

      return '<div class="band"><h2>Project Properties</h2><div class="grid">' +
        field('project-name', 'Name', model.project.name) +
        selectField('project-template', 'Template', ['Application', 'Library', 'Tool', model.project.template], model.project.template, '') +
        selectField('project-default-profile', 'Default Profile', profileNames(), model.project.defaultProfile, '') +
        '</div><div class="actions"><button id="save-project">Apply Project Properties</button></div></div>' +
        '<div class="band"><h2>Selected Profile</h2>' + statusDetails() + '</div>' +
        '<div class="band"><h2>Project Summary</h2>' + details([
        detail('Package References', resolved.packageCount),
        detail('Enabled Features', resolved.activeFeatureCount + ' / ' + resolved.featureCount),
        detail('Code Generators', resolved.activeGeneratorCount + ' / ' + resolved.generatorCount),
        detail('Files Staged', resolved.stagedFileCount),
        detail('Environment Variables', resolved.environmentVariableCount)
        ]) + '</div>' +
        '<div class="band"><h2>Output</h2>' + details([
        detail('Workspace', resolved.workspaceName),
        detail('Project type', resolved.projectType),
        detail('Output folder', resolved.outputDir),
        detail('Executable', resolved.launchExecutable),
        detail('Working directory', resolved.launchWorkingDirectory)
        ]) + '</div>' +
        '<div class="band"><h2>Advanced XML Sections</h2>' + unsupported + '</div>';
    }

    function renderProfiles() {
      const selected = currentProfile();
      const profileList = model.profiles.length
        ? model.profiles.map((profile) => renderProfileCard(profile, selected?.name)).join('')
        : '<div class="empty">No profiles.</div>';
      const editor = selected ? renderProfileEditor(selected) : '';
      return '<div class="split"><div class="band"><h2>Build Profiles</h2><div class="list">' + profileList +
        '</div><div class="row-actions inline"><input id="new-profile-name" placeholder="New profile name"><button class="secondary" id="add-profile">Add Profile</button></div></div>' +
        '<div class="band">' + editor + '</div></div>';
    }

    function renderProfileCard(profile, selectedName) {
      return '<button class="profile-card ' + (profile.name === selectedName ? 'active' : '') + '" data-profile="' + esc(profile.name) + '">' +
        '<h3>' + esc(profile.name) + '</h3>' +
        details([
          detail('Template', profile.template),
          detail('Build', profile.buildType || model.resolved.buildType),
          detail('Platform', profile.platform || model.resolved.platform),
          detail('Environment', profile.environment || model.resolved.environment)
        ], 'No profile values.') +
        '</button>';
    }

    function renderProfileEditor(profile) {
      return '<h2>' + esc(profile.name) + '</h2><div class="grid">' +
        field('profile-name', 'Name', profile.name) +
        field('profile-template', 'Template', profile.template) +
        selectField('profile-build-type', 'Build Type', ['Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel', profile.buildType], profile.buildType, '') +
        selectField('profile-platform', 'Platform', ['linux-x64', 'windows-x64', 'macos-x64', 'macos-arm64', profile.platform], profile.platform, '') +
        selectField('profile-os', 'Operating System', ['linux', 'windows', 'macos', profile.operatingSystem], profile.operatingSystem, '') +
        selectField('profile-arch', 'Architecture', ['x64', 'arm64', profile.architecture], profile.architecture, '') +
        selectField('profile-env', 'Environment', model.environments.map((env) => env.name).concat(profile.environment || []), profile.environment, '') +
        field('profile-launch-executable', 'Launch Executable', profile.launchExecutable) +
        field('profile-launch-working-directory', 'Launch Working Directory', profile.launchWorkingDirectory) +
        '</div><div class="actions"><button id="save-profile">Apply Build Profile</button><button class="secondary" id="delete-profile">Delete Profile</button></div>';
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
        ['Type', 'Path', 'Rule', 'From', 'Staged As'],
        model.resolved.inputs.map((entry) => [
          entry.kind,
          entry.source || '',
          entry.mode || '',
          entry.ownerName || '',
          entry.stagedRelativePath || ''
        ]),
        'No files are included for the selected profile.',
        '120px minmax(220px, 1.4fr) 120px minmax(180px, 1fr) minmax(220px, 1fr)'
      );
      return '<div class="band"><h2>Files Included For Selected Profile</h2>' + resolvedInputs + '</div>' +
        '<div class="band"><h2>File Rules</h2><div class="inline">' + renderScopeButtons(activeInputScope, true, 'input-scope') + renderBlockButtons() + '</div>' +
        '<div id="input-list" class="list">' + selectedInputEntries().map((entry) => inputRow(entry)).join('') + '</div>' +
        '<div class="actions"><button class="secondary" id="add-input">Add File Rule</button><button id="save-inputs">Apply File Rules</button></div></div>';
    }

    function inputRow(values) {
      return '<div class="row file-rule" data-row="input">' +
        '<label><span>Rule</span><select><option value="Directory"' + (values.mode === 'Directory' ? ' selected' : '') + '>Folder scan</option><option value="File"' + (values.mode === 'File' ? ' selected' : '') + '>Single file</option><option value="Glob"' + (values.mode === 'Glob' ? ' selected' : '') + '>Glob pattern</option></select></label>' +
        '<label><span>Path / root</span><input value="' + esc(values.path) + '" placeholder="src or src/main.cpp"></label>' +
        '<label><span>Include pattern</span><input value="' + esc(values.include) + '" placeholder="**/*.cpp;**/*.hpp"></label>' +
        '<label><span>Exclude pattern</span><input value="' + esc(values.exclude) + '" placeholder="**/*.generated.cpp"></label>' +
        '<label><span>OS</span><select><option value=""></option><option value="linux"' + (values.operatingSystem === 'linux' ? ' selected' : '') + '>Linux</option><option value="windows"' + (values.operatingSystem === 'windows' ? ' selected' : '') + '>Windows</option><option value="macos"' + (values.operatingSystem === 'macos' ? ' selected' : '') + '>macOS</option></select></label>' +
        '<label><span>Arch</span><select><option value=""></option><option value="x64"' + (values.architecture === 'x64' ? ' selected' : '') + '>x64</option><option value="arm64"' + (values.architecture === 'arm64' ? ' selected' : '') + '>arm64</option></select></label>' +
        '<label><span>Build</span><select><option value=""></option><option value="Debug"' + (values.buildType === 'Debug' ? ' selected' : '') + '>Debug</option><option value="Release"' + (values.buildType === 'Release' ? ' selected' : '') + '>Release</option><option value="RelWithDebInfo"' + (values.buildType === 'RelWithDebInfo' ? ' selected' : '') + '>RelWithDebInfo</option></select></label>' +
        '<label><span>Condition</span><input value="' + esc(values.condition) + '" placeholder="DesktopDebug"></label>' +
        '<button class="secondary" data-remove-row>Remove</button></div>';
    }

    function selectedReferences() {
      return activeReferenceScope ? model.profiles.find((profile) => profile.name === activeReferenceScope)?.packageReferences || [] : model.project.packageReferences;
    }

    function renderPackages() {
      const resolvedPackages = table(
        ['Package', 'Version', 'Required By', 'Manifest'],
        model.resolved.packages.map((pkg) => [
          pkg.name,
          pkg.version || '',
          pkg.requiredBy.join(', '),
          pkg.manifestPath || ''
        ]),
        'No packages are used by the selected profile.',
        'minmax(180px, 1fr) 100px minmax(180px, 1fr) minmax(280px, 1.8fr)'
      );
      return '<div class="band"><h2>Packages Used By Selected Profile</h2>' + resolvedPackages + '</div>' +
        '<div class="band"><h2>Package References</h2>' + renderScopeButtons(activeReferenceScope, true, 'reference-scope') +
        '<div id="reference-list" class="list">' + selectedReferences().map((entry) => referenceRow(entry)).join('') + '</div>' +
        '<div class="actions"><button class="secondary" id="add-reference">Add Package</button><button id="save-references">Apply Package References</button></div></div>' +
        '<div class="band"><h2>Package Features</h2><div class="list">' + (model.features.length ? model.features.map(renderFeature).join('') : '<div class="empty">No package features are available for the selected profile.</div>') + '</div></div>';
    }

    function referenceRow(values) {
      return '<div class="row reference" data-row="reference">' +
        '<label><span>Package</span><input value="' + esc(values.name) + '"></label>' +
        '<label><span>Version</span><input value="' + esc(values.version) + '"></label>' +
        '<label><span>Optional</span><select><option value=""></option><option value="true"' + (values.optional ? ' selected' : '') + '>Yes</option><option value="false"' + (values.optional === false ? ' selected' : '') + '>No</option></select></label>' +
        '<button class="secondary" data-remove-row>Remove</button></div>';
    }

    function renderFeature(feature) {
      const states = [['inherit', 'Inherit'], ['use', 'Use'], ['disable', 'Disable']];
      return '<div class="feature"><div><h3>' + esc(feature.packageName + ' / ' + feature.featureName) + '</h3>' +
        details([
          detail('Resolved', feature.resolvedState),
          detail('Version', feature.packageVersion),
          detail('Manifest', feature.manifestPath)
        ], 'No feature metadata.') +
        '<div class="muted">' + esc(feature.description) + '</div></div><div class="segmented">' +
        states.map(([state, label]) => '<button ' + (feature.readOnly ? 'disabled ' : '') + 'class="' + (feature.state === state ? 'active' : '') + '" data-feature-package="' + esc(feature.packageName) + '" data-feature-name="' + esc(feature.featureName) + '" data-feature-state="' + state + '">' + label + '</button>').join('') +
        '</div></div>';
    }

    function renderRun() {
      const profile = currentProfile();
      return '<div class="band"><h2>Run Target</h2><div class="grid">' +
        field('root-launch-executable', 'Executable', model.project.launchExecutable || model.resolved.launchExecutable) +
        field('root-launch-working-directory', 'Working Directory', model.project.launchWorkingDirectory || model.resolved.launchWorkingDirectory) +
        '</div><div class="actions"><button id="save-root-launch">Apply Project Run Target</button></div></div>' +
        '<div class="band"><h2>Selected Profile Run Target</h2>' + details([
        detail('Profile', profile?.name),
        detail('Executable', profile?.launchExecutable || model.resolved.launchExecutable),
        detail('Working directory', profile?.launchWorkingDirectory || model.resolved.launchWorkingDirectory),
        detail('Output folder', model.resolved.outputDir)
        ]) + '</div>';
    }

    function selectedEnvironment() {
      return model.environments.find((env) => env.name === selectedEnvironmentName) || model.environments[0];
    }

    function renderEnvironment() {
      const env = selectedEnvironment();
      const resolved = table(
        ['Name', 'Source', 'Secret', 'Resolved'],
        model.resolved.environmentVariables.map((variable) => [
          variable.name,
          variable.source || '',
          variable.secret ? 'Yes' : 'No',
          variable.resolved === false ? 'No' : 'Yes'
        ]),
        'No variables are active for the selected profile.',
        'minmax(180px, 1fr) minmax(180px, 1fr) 90px 100px'
      );
      const options = model.environments.length ? model.environments.map((entry) => '<option value="' + esc(entry.name) + '"' + (entry.name === env?.name ? ' selected' : '') + '>' + esc(entry.name) + '</option>').join('') : '<option value=""></option>';
      return '<div class="band"><h2>Variables Active For Selected Profile</h2>' + resolved + '</div>' +
        '<div class="band"><h2>Environment Definitions</h2><div class="grid"><label><span>Environment</span><select id="environment-select">' + options + '</select></label>' +
        '<label><span>New Environment</span><input id="new-environment-name"></label></div><div id="variable-list" class="list">' +
        ((env?.variables || []).map((entry) => variableRow(entry)).join('')) +
        '</div><div class="actions"><button class="secondary" id="add-variable">Add Variable</button><button id="save-environment">Apply Environment Definition</button></div></div>';
    }

    function variableRow(values) {
      const type = values.fromLocalSetting ? 'local' : values.fromEnvironment ? 'environment' : 'literal';
      const sourceValue = values.fromLocalSetting || values.fromEnvironment || values.value || '';
      return '<div class="row variable" data-row="variable">' +
        '<label><span>Name</span><input value="' + esc(values.name) + '"></label>' +
        '<label><span>Source</span><select><option value="literal"' + (type === 'literal' ? ' selected' : '') + '>Literal</option><option value="environment"' + (type === 'environment' ? ' selected' : '') + '>Environment</option><option value="local"' + (type === 'local' ? ' selected' : '') + '>Local Setting</option></select></label>' +
        '<label><span>Value</span><input value="' + esc(sourceValue) + '"></label>' +
        '<label><span>Required</span><select><option value=""></option><option value="true"' + (values.required ? ' selected' : '') + '>Yes</option><option value="false"' + (values.required === false ? ' selected' : '') + '>No</option></select></label>' +
        '<label><span>Secret</span><select><option value=""></option><option value="true"' + (values.secret ? ' selected' : '') + '>Yes</option><option value="false"' + (values.secret === false ? ' selected' : '') + '>No</option></select></label>' +
        '<button class="secondary" data-remove-row>Remove</button></div>';
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

    function collectRows(kind) {
      return Array.from(document.querySelectorAll('[data-row="' + kind + '"]')).map((node) => {
        const inputs = Array.from(node.querySelectorAll('input, select'));
        if (kind === 'reference') {
          return {
            name: optional(inputs[0].value),
            version: optional(inputs[1].value),
            optional: optional(inputs[2].value) === undefined ? undefined : inputs[2].value === 'true'
          };
        }
        if (kind === 'variable') {
          const sourceType = inputs[1].value;
          const sourceValue = optional(inputs[2].value);
          return {
            name: optional(inputs[0].value),
            value: sourceType === 'literal' ? sourceValue : undefined,
            fromEnvironment: sourceType === 'environment' ? sourceValue : undefined,
            fromLocalSetting: sourceType === 'local' ? sourceValue : undefined,
            required: optional(inputs[3].value) === undefined ? undefined : inputs[3].value === 'true',
            secret: optional(inputs[4].value) === undefined ? undefined : inputs[4].value === 'true'
          };
        }
        return {
          mode: inputs[0].value,
          path: optional(inputs[1].value),
          include: optional(inputs[2].value),
          exclude: optional(inputs[3].value),
          operatingSystem: optional(inputs[4].value),
          architecture: optional(inputs[5].value),
          buildType: optional(inputs[6].value),
          condition: optional(inputs[7].value)
        };
      }).filter((entry) => entry.name !== undefined || entry.path !== undefined || entry.include !== undefined);
    }

    document.addEventListener('click', (event) => {
      const target = event.target;
      if (!(target instanceof HTMLElement)) return;
      if (target.matches('[data-tab]')) {
        activeTab = target.dataset.tab;
        render();
      }
      if (target.matches('[data-profile]')) {
        selectedProfileName = target.dataset.profile;
        model.activeProfile = selectedProfileName;
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
      if (target.id === 'open-source') post({ type: 'openSource' });
      if (target.id === 'validate') post({ type: 'validate' });
      if (target.id === 'save-project') post({ type: 'updateProject', name: optional(byId('project-name').value), template: optional(byId('project-template').value), defaultProfile: optional(byId('project-default-profile').value) });
      if (target.id === 'save-root-launch') post({ type: 'setRootLaunch', executable: optional(byId('root-launch-executable').value), workingDirectory: optional(byId('root-launch-working-directory').value) });
      if (target.id === 'add-profile') post({ type: 'addProfile', name: byId('new-profile-name').value });
      if (target.id === 'delete-profile') post({ type: 'deleteProfile', name: currentProfile()?.name });
      if (target.id === 'save-profile') post({ type: 'updateProfile', originalName: currentProfile()?.name, name: optional(byId('profile-name').value), template: optional(byId('profile-template').value), buildType: optional(byId('profile-build-type').value), platform: optional(byId('profile-platform').value), operatingSystem: optional(byId('profile-os').value), architecture: optional(byId('profile-arch').value), environment: optional(byId('profile-env').value), launchExecutable: optional(byId('profile-launch-executable').value), launchWorkingDirectory: optional(byId('profile-launch-working-directory').value) });
      if (target.id === 'add-input') byId('input-list').insertAdjacentHTML('beforeend', inputRow({ mode: 'Directory' }));
      if (target.id === 'save-inputs') post({ type: 'setInputEntries', profileName: optional(activeInputScope), block: activeInputBlock, entries: collectRows('input') });
      if (target.id === 'add-reference') byId('reference-list').insertAdjacentHTML('beforeend', referenceRow({}));
      if (target.id === 'save-references') post({ type: 'setPackageReferences', profileName: optional(activeReferenceScope), references: collectRows('reference') });
      if (target.id === 'add-variable') byId('variable-list').insertAdjacentHTML('beforeend', variableRow({}));
      if (target.id === 'save-environment') {
        const environmentName = optional(byId('new-environment-name').value) || optional(byId('environment-select').value);
        post({ type: 'setEnvironmentVariables', environmentName, variables: collectRows('variable') });
      }
      if (target.matches('[data-feature-state]')) {
        if (model.activeProfile) {
          post({ type: 'setFeatureState', profileName: model.activeProfile, packageName: target.dataset.featurePackage, featureName: target.dataset.featureName, state: target.dataset.featureState });
        }
      }
      if (target.matches('[data-remove-row]')) {
        target.closest('[data-row]')?.remove();
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
        render();
      }
    });
    post({ type: 'ready' });
  </script>
</body>
</html>`;
  }
}
