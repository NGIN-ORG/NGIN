(() => {
  const vscode = acquireVsCodeApi();
  const app = document.getElementById('app');

  let model;
  let activePage = 'project';
  let selectedProfileName;
  let activeInputScope = '';
  let activeInputBlock = 'Sources';
  let activeDependencyScope = '';
  let selectedEnvironmentName;
  let drawer = undefined;

  const pages = [
    ['project', 'Project'],
    ['build', 'Build'],
    ['dependencies', 'Dependencies'],
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

  function profileNames() {
    return (model?.profiles || []).map((profile) => profile.name);
  }

  function currentProfile() {
    const selected = selectedProfileName || model?.activeProfile;
    return model?.profiles.find((profile) => profile.name === selected) || model?.profiles[0];
  }

  function selectedEnvironment() {
    return model?.environments.find((env) => env.name === selectedEnvironmentName) || model?.environments[0];
  }

  function selectedInputEntries() {
    const source = activeInputScope ? model.profiles.find((profile) => profile.name === activeInputScope)?.inputs : model.project.inputs;
    return source?.[activeInputBlock] || [];
  }

  function referencesForScope(scope) {
    return scope ? model.profiles.find((profile) => profile.name === scope)?.dependencies || [] : model.project.dependencies;
  }

  function optionList(values, selected, emptyLabel) {
    const seen = new Set();
    const options = [];
    if (emptyLabel !== undefined) {
      options.push(`<option value="">${esc(emptyLabel)}</option>`);
    }
    for (const value of values.filter(Boolean)) {
      if (seen.has(value)) continue;
      seen.add(value);
      options.push(`<option value="${esc(value)}"${value === selected ? ' selected' : ''}>${esc(value)}</option>`);
    }
    return options.join('');
  }

  function field(id, label, value, attrs = '') {
    return `<label><span>${esc(label)}</span><input id="${id}" value="${esc(value)}"${attrs}></label>`;
  }

  function selectField(id, label, values, selected, emptyLabel = '') {
    return `<label><span>${esc(label)}</span><select id="${id}">${optionList(values, selected, emptyLabel)}</select></label>`;
  }

  function badge(value, cls = '') {
    if (!value && value !== 0) return '';
    return `<span class="badge ${esc(cls)}" title="${esc(value)}">${esc(value)}</span>`;
  }

  function cell(value) {
    return `<div class="cell"><span class="truncate" title="${esc(value)}">${esc(value)}</span></div>`;
  }

  function rawCell(value) {
    return `<div class="cell">${value}</div>`;
  }

  const usedForOptions = [
    ['Build', 'Build', 'Used while building, usually tools or code generators'],
    ['Target', 'Link', 'Used to compile or link the C++ product'],
    ['Runtime', 'Run', 'Needed in the staged output when the app runs'],
    ['Test', 'Test', 'Only used by test products'],
    ['Dev', 'Dev', 'Editor, analyzer, or developer-only tooling'],
    ['Publish', 'Pack', 'Used while publishing or packaging']
  ];

  function scopeValues(scope) {
    return new Set(clean(scope).split(/[;,]/).map((entry) => entry.trim()).filter(Boolean));
  }

  function usedForBadges(scope) {
    const values = scopeValues(scope);
    if (!values.size) {
      return badge('Default');
    }
    return usedForOptions
      .filter(([value]) => values.has(value))
      .map(([, label, title]) => `<span class="badge" title="${esc(title)}">${esc(label)}</span>`)
      .join('');
  }

  function usedForCheckboxes(scope) {
    const values = scopeValues(scope);
    return `<div class="tag-grid">${usedForOptions.map(([value, label, title]) => `
      <label class="tag-check" title="${esc(title)}">
        <input type="checkbox" data-used-for="${esc(value)}"${values.has(value) ? ' checked' : ''}>
        <span>${esc(label)}</span>
      </label>
    `).join('')}</div>`;
  }

  function collectUsedForScope() {
    const values = Array.from(document.querySelectorAll('[data-used-for]'))
      .filter((entry) => entry instanceof HTMLInputElement && entry.checked)
      .map((entry) => entry.dataset.usedFor);
    return values.length ? values.join(';') : undefined;
  }

  function applyToLabel(value) {
    return value ? `${value} Only` : 'All Profiles';
  }

  function table(columns, rows, emptyText, template) {
    if (!rows.length) {
      return `<div class="empty">${esc(emptyText)}</div>`;
    }
    const style = template ? ` style="--columns: ${esc(template)};"` : '';
    return `<div class="table">
      <div class="row head"${style}>${columns.map(cell).join('')}</div>
      ${rows.map((row) => `<div class="row"${style}>${row.join('')}</div>`).join('')}
    </div>`;
  }

  function selectableTable(columns, rows, emptyText, template) {
    if (!rows.length) {
      return `<div class="empty">${esc(emptyText)}</div>`;
    }
    const style = template ? ` style="--columns: ${esc(template)};"` : '';
    return `<div class="table">
      <div class="row head"${style}>${columns.map(cell).join('')}</div>
      ${rows.map((row) => `<div class="row selectable ${row.active ? 'active' : ''}" ${row.attrs || ''}${style}>${row.cells.join('')}</div>`).join('')}
    </div>`;
  }

  function diagnosticSummaryBadge() {
    const errors = model.resolved.diagnosticErrorCount || model.diagnostics.filter((item) => item.startsWith('error:')).length;
    const warnings = model.resolved.diagnosticWarningCount || model.diagnostics.filter((item) => item.startsWith('warning:')).length;
    if (errors) return badge(`${errors} errors`, 'err');
    if (warnings) return badge(`${warnings} warnings`, 'warn');
    return badge('valid', 'ok');
  }

  function navCount(id) {
    if (!model) return '';
    if (id === 'project') return model.profiles.length;
    if (id === 'build') return selectedInputEntries().length;
    if (id === 'dependencies') return referencesForScope(activeDependencyScope).length;
    if (id === 'environment') return selectedEnvironment()?.variables.length || '';
    if (id === 'advanced') return model.diagnostics.length + model.unsupportedSections.length;
    return '';
  }

  function renderShell(content) {
    const resolved = model.resolved;
    const title = model.project.name || resolved.projectName || 'NGIN Project';
    const product = model.project.productKind || resolved.projectType || 'Project';
    app.innerHTML = `
      <header class="topbar">
        <div>
          <div class="title-row">
            <h1>${esc(title)}</h1>
            ${badge(product)}
            ${diagnosticSummaryBadge()}
          </div>
          <div class="subtitle">${esc(model.path)}</div>
        </div>
        <div class="toolbar">
          <button id="validate">Validate</button>
          <button class="secondary" id="open-source">XML Source</button>
        </div>
      </header>
      <div class="workspace">
        <nav class="rail">${pages.map(([id, label]) => `
          <button class="nav-item ${id === activePage ? 'active' : ''}" data-page="${id}">
            <span class="truncate">${esc(label)}</span>
            <span class="nav-count">${esc(navCount(id))}</span>
          </button>
        `).join('')}</nav>
        <main class="content">${content}</main>
      </div>
      ${renderDrawer()}
    `;
  }

  function render() {
    if (!model) return;
    selectedProfileName = selectedProfileName || model.activeProfile || model.profiles[0]?.name;
    selectedEnvironmentName = selectedEnvironmentName || model.environments[0]?.name;

    if (model.parseError) {
      renderShell(`<section class="page"><div class="panel error-box"><h2>Manifest Parse Error</h2><p>${esc(model.parseError)}</p></div></section>`);
      return;
    }

    const pageRenderers = {
      project: renderProject,
      build: renderBuild,
      dependencies: renderDependencies,
      run: renderRun,
      environment: renderEnvironment,
      advanced: renderAdvanced
    };
    renderShell(`<section class="page">${(pageRenderers[activePage] || renderProject)()}</section>`);
  }

  function renderProject() {
    const resolved = model.resolved;
    const selected = currentProfile();
    const profileRows = model.profiles.map((profile, index) => ({
      active: profile.name === selected?.name,
      attrs: `data-open-profile="${index}"`,
      cells: [
        cell(profile.name),
        rawCell(`<div class="chips">${badge(profile.optimization || 'default')}${badge(profile.debugSymbols ? 'symbols' : 'no symbols')}${badge(profile.platform || [profile.operatingSystem, profile.architecture].filter(Boolean).join('-') || 'host')}</div>`),
        cell(profile.environment || ''),
        rawCell(`<button class="ghost" data-open-profile="${index}">Edit</button>`)
      ]
    }));

    return `
      <div class="panel">
        <div class="panel-header">
          <div class="panel-title">
            <h2>Project Settings</h2>
            <p>These values are written to the authored project manifest.</p>
          </div>
          <button data-save-project-inline>Save</button>
        </div>
        <div class="settings-grid">
          ${field('project-name', 'Name', model.project.name || resolved.projectName)}
          <label><span>Product</span><input value="${esc(model.project.productKind || resolved.projectType || '')}" readonly></label>
          ${selectField('project-default-profile', 'Default Profile', profileNames(), model.project.defaultProfile, '')}
          <label><span>Workspace</span><input value="${esc(resolved.workspaceName || '')}" readonly></label>
        </div>
      </div>
      <div class="panel">
        <div class="panel-header">
          <div class="panel-title">
            <h2>Profiles</h2>
            <p>Profiles choose build traits, platform, environment, and optional launch overrides.</p>
          </div>
          <button class="secondary" data-drawer="add-profile">Add Profile</button>
        </div>
        ${selectableTable(['Name', 'Defaults', 'Environment', ''], profileRows, 'No profiles authored.', 'minmax(170px, 1fr) minmax(220px, 1.2fr) minmax(140px, 1fr) 80px')}
      </div>
      <div class="panel compact">
        <div class="panel-title"><h2>Active Resolved Profile</h2></div>
        <div class="summary-strip">
          ${badge(resolved.profileName || model.activeProfile || 'no profile')}
          ${badge(resolved.optimization || 'default optimization')}
          ${badge(resolved.debugSymbols ? 'symbols' : 'no symbols')}
          ${resolved.backendConfiguration ? badge(resolved.backendConfiguration) : ''}
          ${badge(resolved.platform || [resolved.operatingSystem, resolved.architecture].filter(Boolean).join('-') || 'host')}
          ${badge(resolved.environment || 'no environment')}
          ${resolved.outputDir ? badge(resolved.outputDir) : ''}
        </div>
      </div>
    `;
  }

  function renderBuild() {
    const resolvedRows = model.resolved.inputs.slice(0, 80).map((entry) => [
      rawCell(`<div class="chips">${badge(entry.kind || 'Source')}${badge(entry.mode || '')}</div>`),
      cell(entry.source || ''),
      cell(entry.ownerName || ''),
      cell(entry.stagedRelativePath || '')
    ]);
    const ruleRows = selectedInputEntries().map((entry, index) => ({
      attrs: `data-open-input="${index}"`,
      cells: [
        rawCell(`<div class="chips">${badge(inputRuleKind(entry))}${badge(applyToLabel(activeInputScope))}</div>`),
        cell(inputRulePath(entry)),
        cell(inputRuleWhen(entry)),
        rawCell(`<button class="ghost" data-open-input="${index}">Edit</button><button class="icon danger" title="Remove" data-remove-input="${index}">x</button>`)
      ]
    }));
    const stageRows = (model.resolved.stageFiles || []).slice(0, 40).map((file) => [
      rawCell(badge(file.kind || 'file')),
      cell(file.source || ''),
      cell(file.target || ''),
      cell(file.owner || '')
    ]);
    return `
      <div class="panel">
        <div class="panel-header">
          <div class="panel-title">
            <h2>Input Rules</h2>
            <p>Pick where the rule is authored, then edit source/header/config inputs.</p>
          </div>
          <button class="secondary" data-drawer="input">Add Rule</button>
        </div>
        <div class="setting-row"><span>Apply To</span>${renderScopeButtons(activeInputScope, true, 'input-scope')}</div>
        <div class="setting-row"><span>Inputs</span>${renderInputBlockButtons()}</div>
        ${selectableTable(['Rule', 'Path', 'Applies When', ''], ruleRows, 'No rules authored here.', '150px minmax(240px, 1.2fr) minmax(250px, 1.4fr) 112px')}
      </div>
      <details class="details-block" open>
        <summary>Resolved files for the active profile</summary>
        ${table(['Type', 'Path', 'Owner', 'Staged As'], resolvedRows, 'No files are included for the selected profile.', '150px minmax(260px, 1.4fr) minmax(150px, 1fr) minmax(220px, 1fr)')}
      </details>
      <details class="details-block">
        <summary>Staged output</summary>
        ${table(['Kind', 'Source', 'Target', 'Owner'], stageRows, 'No staged files are resolved for this profile.', '130px minmax(260px, 1.2fr) minmax(220px, 1fr) minmax(160px, 1fr)')}
      </details>
    `;
  }

  function renderDependencies() {
    const referenceRows = referencesForScope(activeDependencyScope).map((reference, index) => ({
      attrs: `data-open-dependency="${index}"`,
      cells: [
        cell(reference.name),
        cell(reference.version || ''),
        rawCell(`<div class="chips">${usedForBadges(reference.scope)}</div>`),
        rawCell(`<div class="chips">${badge(applyToLabel(activeDependencyScope))}${reference.optional === undefined ? '' : badge(reference.optional ? 'optional' : 'required')}</div>`),
        rawCell(`<button class="ghost" data-open-dependency="${index}">Edit</button><button class="icon danger" title="Remove" data-remove-dependency="${index}">x</button>`)
      ]
    }));
    const packageRows = model.resolved.packages.map((pkg) => [
      rawCell(`<div class="chips">${badge(pkg.name)}${badge(pkg.version || '')}</div>`),
      cell(pkg.requiredBy.join(', ')),
      cell(pkg.manifestPath || '')
    ]);
    return `
      <div class="panel">
        <div class="panel-header">
          <div class="panel-title">
            <h2>Dependencies</h2>
            <p>Add the packages this C++ target builds with, links to, runs with, or uses for tooling.</p>
          </div>
          <button class="secondary" data-drawer="dependency">Add Dependency</button>
        </div>
        <div class="setting-row"><span>Apply To</span>${renderScopeButtons(activeDependencyScope, true, 'dependency-scope')}</div>
        ${selectableTable(['Name', 'Version', 'Used For', 'Apply To', ''], referenceRows, 'No dependencies authored here.', 'minmax(200px, 1fr) minmax(150px, 0.8fr) minmax(210px, 1fr) minmax(180px, 1fr) 112px')}
      </div>
      ${renderFeaturesPanel()}
      <details class="details-block">
        <summary>Resolved package graph</summary>
        ${table(['Package', 'Required By', 'Manifest'], packageRows, 'No resolved packages.', 'minmax(220px, 1fr) minmax(180px, 1fr) minmax(260px, 1.3fr)')}
      </details>
    `;
  }

  function renderRun() {
    const profile = currentProfile();
    const launchRows = (model.resolved.launches || []).map((entry) => [
      rawCell(`<div class="chips">${badge(entry.name || 'default')}${entry.selected ? badge('selected', 'ok') : ''}</div>`),
      cell(entry.executable || ''),
      cell(entry.workingDirectory || ''),
      cell(entry.args || ''),
      cell(entry.source || '')
    ]);
    return `
      <div class="panel">
        <div class="panel-header">
          <div class="panel-title">
            <h2>Project Launch</h2>
            <p>Default run settings authored in the project product section.</p>
          </div>
          <button data-save-run-inline>Save</button>
        </div>
        <div class="settings-grid">
          ${field('root-launch-executable', 'Executable', model.project.launchExecutable || model.resolved.launchExecutable, ' placeholder="$(OutputName)"')}
          ${field('root-launch-working-directory', 'Working Directory', model.project.launchWorkingDirectory || model.resolved.launchWorkingDirectory, ' placeholder="."')}
          <label><span>Selected Profile Override</span><input value="${esc(profile?.launchExecutable || '')}" readonly></label>
          <label><span>Resolved Output</span><input value="${esc(model.resolved.outputDir || '')}" readonly></label>
        </div>
      </div>
      <details class="details-block" open>
        <summary>Resolved launch entries</summary>
        ${table(['Name', 'Executable', 'Working Directory', 'Args', 'Source'], launchRows, 'No launch plan is resolved.', 'minmax(140px, 1fr) minmax(180px, 1fr) minmax(180px, 1fr) minmax(160px, 1fr) minmax(120px, 1fr)')}
      </details>
    `;
  }

  function renderEnvironment() {
    const env = selectedEnvironment();
    const options = model.environments.length ? model.environments.map((entry) => `<option value="${esc(entry.name)}"${entry.name === env?.name ? ' selected' : ''}>${esc(entry.name)}</option>`).join('') : '<option value=""></option>';
    const authoredRows = (env?.variables || []).map((variable, index) => ({
      attrs: `data-open-variable="${index}"`,
      cells: [
        cell(variable.name),
        cell(variableSourceLabel(variable)),
        rawCell(`<div class="chips">${variable.required === undefined ? '' : badge(variable.required ? 'required' : 'optional')}${variable.secret ? badge('secret') : ''}</div>`),
        rawCell(`<button class="ghost" data-open-variable="${index}">Edit</button><button class="icon danger" title="Remove" data-remove-variable="${index}">x</button>`)
      ]
    }));
    const resolvedRows = model.resolved.environmentVariables.map((variable) => [
      rawCell(`<div class="chips">${badge(variable.name)}${variable.secret ? badge('secret') : ''}</div>`),
      cell(variable.source || ''),
      rawCell(variable.resolved === false ? badge('missing', 'err') : badge('resolved', 'ok'))
    ]);
    return `
      <div class="panel">
        <div class="panel-header">
          <div class="panel-title">
            <h2>Environment Variables</h2>
            <p>Author literal values, host environment imports, and local settings keys.</p>
          </div>
          <div class="actions"><button class="secondary" data-drawer="environment">Add Environment</button><button class="secondary" data-drawer="variable">Add Variable</button></div>
        </div>
        <label class="short-field"><span>Environment</span><select id="environment-select">${options}</select></label>
        ${selectableTable(['Name', 'Source', 'Flags', ''], authoredRows, env ? 'No variables in this environment.' : 'No environments defined.', 'minmax(180px, 1fr) minmax(240px, 1.3fr) minmax(140px, 1fr) 112px')}
      </div>
      <details class="details-block">
        <summary>Resolved variables</summary>
        ${table(['Name', 'Source', 'Status'], resolvedRows, 'No variables are active for the selected profile.', 'minmax(190px, 1fr) minmax(260px, 1.2fr) 110px')}
      </details>
    `;
  }

  function renderAdvanced() {
    const runtimeRows = (model.resolved.runtimeEntries || []).map((entry) => [
      rawCell(`<div class="chips">${badge(entry.kind)}${badge(entry.state || '')}</div>`),
      cell(entry.name || ''),
      cell(entry.source || '')
    ]);
    const toolRows = model.resolved.toolRuns.map((run) => [
      rawCell(`<div class="chips">${badge(run.displayName || run.name)}${badge(run.state || 'ready', run.state === 'invalid' ? 'err' : '')}</div>`),
      cell(run.kind || ''),
      cell(run.tool || run.driver || ''),
      cell(run.gate ? `Gate: ${run.failOn || 'Error'}` : 'Report only')
    ]);
    const unsupportedRows = model.unsupportedSections.map((name) => [cell(name), rawCell(badge('XML source'))]);
    return `
      ${renderDiagnosticsPanel()}
      <details class="details-block" open>
        <summary>Runtime composition</summary>
        ${table(['Kind', 'Name', 'Source'], runtimeRows, 'No runtime modules or plugins are resolved.', '150px minmax(240px, 1fr) minmax(180px, 1fr)')}
      </details>
      <details class="details-block">
        <summary>Tooling</summary>
        ${table(['Name', 'Kind', 'Tool', 'Policy'], toolRows, 'No tool runs are active for this profile.', 'minmax(190px, 1.1fr) 100px minmax(160px, 1fr) minmax(140px, 1fr)')}
      </details>
      <details class="details-block">
        <summary>XML-only sections</summary>
        ${table(['Section', 'Editor'], unsupportedRows, 'No advanced XML-only sections in this file.', 'minmax(180px, 1fr) minmax(160px, 1fr)')}
      </details>
    `;
  }

  function renderScopeButtons(current, includeRoot, group) {
    const entries = includeRoot ? [['', 'Project']] : [];
    for (const name of profileNames()) entries.push([name, name]);
    return `<div class="segmented">${entries.map(([value]) => `<button class="${value === current ? 'active' : ''}" data-${group}="${esc(value)}">${esc(applyToLabel(value))}</button>`).join('')}</div>`;
  }

  function renderInputBlockButtons() {
    return `<div class="segmented">${['Sources', 'Headers', 'Configs'].map((block) => `<button class="${block === activeInputBlock ? 'active' : ''}" data-input-block="${block}">${block}</button>`).join('')}</div>`;
  }

  function inputRuleKind(entry) {
    if (entry.mode === 'Directory') return 'Folder';
    if (entry.mode === 'File') return 'File';
    return 'Glob';
  }

  function inputRulePath(entry) {
    if (entry.mode === 'Glob') return entry.include || entry.path || '';
    const include = entry.include ? ` include ${entry.include}` : '';
    const exclude = entry.exclude ? ` exclude ${entry.exclude}` : '';
    return `${clean(entry.path)}${include}${exclude}`;
  }

  function inputRuleWhen(entry) {
    const values = [
      entry.platform,
      entry.operatingSystem,
      entry.architecture,
      entry.toolchain ? `toolchain ${entry.toolchain}` : undefined,
      entry.environment ? `env ${entry.environment}` : undefined,
      entry.condition
    ].filter(Boolean);
    return values.length ? values.join(', ') : 'Always';
  }

  function renderFeaturesPanel() {
    const features = model.features || [];
    if (!features.length) {
      return `<details class="details-block"><summary>Package features</summary><div class="empty">No feature plans are available for this profile.</div></details>`;
    }
    return `<details class="details-block" open>
      <summary>Package features</summary>
      <div class="table">
        <div class="row head" style="--columns: minmax(220px, 1fr) minmax(180px, 1fr) 230px;"><div class="cell">Feature</div><div class="cell">Effective</div><div class="cell">Override</div></div>
        ${features.map((feature) => `<div class="row" style="--columns: minmax(220px, 1fr) minmax(180px, 1fr) 230px;">
          ${rawCell(`<div class="truncate" title="${esc(feature.packageName)}">${esc(feature.packageName)}</div><div class="muted truncate" title="${esc(feature.featureName)}">${esc(feature.featureName)}</div>`)}
          ${rawCell(`<div class="chips">${badge(featureEffectiveLabel(feature), featureEffectiveClass(feature))}</div>`)}
          ${rawCell(`<div class="segmented">${['inherit', 'use', 'disable'].map((state) => `<button class="${feature.state === state ? 'active' : ''}" data-feature-package="${esc(feature.packageName)}" data-feature-name="${esc(feature.featureName)}" data-feature-state="${state}">${state === 'inherit' ? 'Default' : state === 'use' ? 'On' : 'Off'}</button>`).join('')}</div>`)}
        </div>`).join('')}
      </div>
    </details>`;
  }

  function featureEffectiveLabel(feature) {
    if (feature.state === 'use') return 'On';
    if (feature.state === 'disable') return 'Off';
    if (feature.resolvedState === 'selected') return 'On by default';
    if (feature.resolvedState === 'available') return 'Off by default';
    return feature.resolvedState || 'Unknown';
  }

  function featureEffectiveClass(feature) {
    const label = featureEffectiveLabel(feature).toLowerCase();
    return label.startsWith('on') ? 'ok' : label.startsWith('off') ? 'warn' : '';
  }

  function variableSourceLabel(variable) {
    if (variable.fromLocalSetting) return `Local setting: ${variable.fromLocalSetting}`;
    if (variable.fromEnvironment) return `Environment: ${variable.fromEnvironment}`;
    return variable.secret ? 'Literal secret' : 'Literal value';
  }

  function renderDiagnosticsPanel() {
    if (!model.diagnostics.length) {
      return `<div class="panel compact"><div class="panel-title"><h2>Diagnostics</h2></div><div class="empty">No validation diagnostics are reported.</div></div>`;
    }
    return `<div class="panel"><div class="panel-title"><h2>Diagnostics</h2></div>
      ${model.diagnostics.map((diagnostic) => {
        const cls = diagnostic.startsWith('error:') ? 'error' : diagnostic.startsWith('warning:') ? 'warning' : '';
        return `<div class="diagnostic ${cls}">${esc(diagnostic)}</div>`;
      }).join('')}
    </div>`;
  }

  function renderDrawer() {
    if (!drawer) return '';
    const content = drawerContent();
    if (!content) return '';
    return `<div class="drawer-backdrop" data-close-drawer>
      <aside class="drawer" role="dialog" aria-modal="true" aria-label="${esc(content.title)}" data-drawer-panel>
        <header><h2>${esc(content.title)}</h2><button class="icon ghost" data-close-drawer title="Close">x</button></header>
        <div class="drawer-body">${content.body}</div>
        <footer>${content.footer}</footer>
      </aside>
    </div>`;
  }

  function drawerContent() {
    if (drawer.type === 'add-profile') {
      return {
        title: 'Add Profile',
        body: field('new-profile-name', 'Name', '', ' placeholder="Debug"'),
        footer: `<button data-add-profile>Add</button><button class="secondary" data-close-drawer>Cancel</button>`
      };
    }
    if (drawer.type === 'profile') {
      const profile = model.profiles[drawer.index];
      if (!profile) return undefined;
      return {
        title: 'Edit Profile',
        body: `<div class="settings-grid">
          ${field('profile-name', 'Name', profile.name)}
          ${selectField('profile-optimization', 'Optimization', ['Off', 'Speed', 'Size', profile.optimization], profile.optimization, '')}
          ${selectField('profile-symbols', 'Debug Symbols', ['true', 'false'], String(profile.debugSymbols ?? true), '')}
          ${selectField('profile-lto', 'Link-Time Optimization', ['true', 'false'], String(profile.linkTimeOptimization ?? false), '')}
          ${field('profile-toolchain', 'Toolchain', profile.toolchain)}
          ${selectField('profile-platform', 'Platform', ['host', 'linux-x64', 'windows-x64', 'macos-x64', 'macos-arm64', profile.platform], profile.platform, '')}
          ${selectField('profile-os', 'Operating System', ['linux', 'windows', 'macos', profile.operatingSystem], profile.operatingSystem, '')}
          ${selectField('profile-arch', 'Architecture', ['x64', 'arm64', profile.architecture], profile.architecture, '')}
          ${selectField('profile-env', 'Environment', model.environments.map((env) => env.name).concat(profile.environment || []), profile.environment, '')}
          ${field('profile-launch-executable', 'Launch Executable', profile.launchExecutable)}
          ${field('profile-launch-working-directory', 'Launch Working Directory', profile.launchWorkingDirectory)}
        </div>`,
        footer: `<button data-save-profile="${esc(profile.name)}">Save</button><button class="danger" data-delete-profile="${esc(profile.name)}">Delete</button><button class="secondary" data-close-drawer>Cancel</button>`
      };
    }
    if (drawer.type === 'input') {
      const entry = drawer.index === undefined ? { mode: 'Directory' } : selectedInputEntries()[drawer.index] || { mode: 'Directory' };
      return {
        title: drawer.index === undefined ? 'Add Input Rule' : 'Edit Input Rule',
        body: `<div class="settings-grid">
          <label><span>Rule</span><select id="input-mode"><option value="Directory"${entry.mode === 'Directory' ? ' selected' : ''}>Folder scan</option><option value="File"${entry.mode === 'File' ? ' selected' : ''}>Single file</option><option value="Glob"${entry.mode === 'Glob' ? ' selected' : ''}>Glob pattern</option></select></label>
          ${field('input-path', 'Path / root', entry.path, ' placeholder="src or src/main.cpp"')}
          ${field('input-include', 'Include pattern', entry.include, ' placeholder="**/*.cpp;**/*.hpp"')}
          ${field('input-exclude', 'Exclude pattern', entry.exclude, ' placeholder="**/*.generated.cpp"')}
          ${selectField('input-os', 'Operating System', ['linux', 'windows', 'macos', entry.operatingSystem], entry.operatingSystem, '')}
          ${selectField('input-arch', 'Architecture', ['x64', 'arm64', entry.architecture], entry.architecture, '')}
          ${field('input-toolchain', 'Toolchain', entry.toolchain, ' placeholder="clang"')}
          ${selectField('input-env', 'Environment', model.environments.map((env) => env.name).concat(entry.environment || []), entry.environment, '')}
          ${field('input-condition', 'Condition', entry.condition)}
        </div>`,
        footer: `<button data-save-input="${drawer.index === undefined ? '' : drawer.index}">Save</button><button class="secondary" data-close-drawer>Cancel</button>`
      };
    }
    if (drawer.type === 'dependency') {
      const editing = drawer.index !== undefined;
      const reference = editing ? referencesForScope(activeDependencyScope)[drawer.index] || {} : {};
      return {
        title: editing ? 'Edit Dependency' : 'Add Dependency',
        body: `<div class="settings-grid">
          ${editing
            ? `<label><span>Apply To</span><input value="${esc(applyToLabel(activeDependencyScope))}" readonly></label>`
            : `<label><span>Apply To</span><select id="new-reference-scope">${optionList(profileNames(), activeDependencyScope, 'All Profiles')}</select></label>`}
          ${field('new-reference-name', 'Package', reference.name || '', ' placeholder="NGIN.Core"')}
          ${field('new-reference-version', 'Version', reference.version || '', ' placeholder=">=0.1.0 <0.2.0"')}
          <label class="wide-field"><span>Used For</span>${usedForCheckboxes(reference.scope)}</label>
          <label><span>Optional</span><select id="new-reference-optional"><option value=""></option><option value="false"${reference.optional === false ? ' selected' : ''}>No</option><option value="true"${reference.optional === true ? ' selected' : ''}>Yes</option></select></label>
        </div>`,
        footer: `<button data-save-dependency="${editing ? drawer.index : ''}">${editing ? 'Save' : 'Add'}</button><button class="secondary" data-close-drawer>Cancel</button>`
      };
    }
    if (drawer.type === 'environment') {
      return {
        title: 'Add Environment',
        body: field('new-environment-name', 'Name', '', ' placeholder="local"'),
        footer: `<button data-add-environment>Add</button><button class="secondary" data-close-drawer>Cancel</button>`
      };
    }
    if (drawer.type === 'variable') {
      const env = selectedEnvironment();
      const variable = drawer.index === undefined ? {} : env?.variables[drawer.index] || {};
      const type = variable.fromLocalSetting ? 'local' : variable.fromEnvironment ? 'environment' : 'literal';
      const sourceValue = variable.fromLocalSetting || variable.fromEnvironment || variable.value || '';
      return {
        title: drawer.index === undefined ? 'Add Variable' : 'Edit Variable',
        body: `<div class="settings-grid">
          ${field('variable-name', 'Name', variable.name)}
          <label><span>Source</span><select id="variable-source"><option value="literal"${type === 'literal' ? ' selected' : ''}>Value</option><option value="environment"${type === 'environment' ? ' selected' : ''}>From environment variable</option><option value="local"${type === 'local' ? ' selected' : ''}>From local setting</option></select></label>
          ${field('variable-value', 'Value / Key', sourceValue)}
          <label><span>Required</span><select id="variable-required"><option value=""></option><option value="true"${variable.required ? ' selected' : ''}>Yes</option><option value="false"${variable.required === false ? ' selected' : ''}>No</option></select></label>
          <label><span>Secret</span><select id="variable-secret"><option value=""></option><option value="true"${variable.secret ? ' selected' : ''}>Yes</option><option value="false"${variable.secret === false ? ' selected' : ''}>No</option></select></label>
        </div>`,
        footer: `<button data-save-variable="${drawer.index === undefined ? '' : drawer.index}">Save</button><button class="secondary" data-close-drawer>Cancel</button>`
      };
    }
    return undefined;
  }

  function closeDrawer() {
    drawer = undefined;
    render();
  }

  function collectInputDialog() {
    return {
      mode: byId('input-mode').value,
      path: optional(byId('input-path').value),
      include: optional(byId('input-include').value),
      exclude: optional(byId('input-exclude').value),
      operatingSystem: optional(byId('input-os').value),
      architecture: optional(byId('input-arch').value),
      toolchain: optional(byId('input-toolchain').value),
      environment: optional(byId('input-env').value),
      condition: optional(byId('input-condition').value)
    };
  }

  function hasInputRuleValue(entry) {
    return entry.path !== undefined || entry.include !== undefined || entry.exclude !== undefined;
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

  function clicked(target, selector) {
    return target instanceof HTMLElement ? target.closest(selector) : null;
  }

  document.addEventListener('click', (event) => {
    const target = event.target;
    if (!(target instanceof HTMLElement)) return;

    const closeTarget = clicked(target, '[data-close-drawer]');
    if (closeTarget && (!target.closest('[data-drawer-panel]') || closeTarget.tagName === 'BUTTON')) {
      closeDrawer();
      return;
    }

    const pageTarget = clicked(target, '[data-page]');
    if (pageTarget) {
      activePage = pageTarget.dataset.page;
      render();
      return;
    }

    const drawerTarget = clicked(target, '[data-drawer]');
    if (drawerTarget) {
      drawer = { type: drawerTarget.dataset.drawer };
      render();
      return;
    }

    const profileTarget = clicked(target, '[data-open-profile]');
    if (profileTarget) {
      drawer = { type: 'profile', index: Number(profileTarget.dataset.openProfile) };
      selectedProfileName = model.profiles[drawer.index]?.name || selectedProfileName;
      render();
      return;
    }

    const inputTarget = clicked(target, '[data-open-input]');
    if (inputTarget) {
      drawer = { type: 'input', index: Number(inputTarget.dataset.openInput) };
      render();
      return;
    }

    const dependencyTarget = clicked(target, '[data-open-dependency]');
    if (dependencyTarget) {
      drawer = { type: 'dependency', index: Number(dependencyTarget.dataset.openDependency) };
      render();
      return;
    }

    const variableTarget = clicked(target, '[data-open-variable]');
    if (variableTarget) {
      drawer = { type: 'variable', index: Number(variableTarget.dataset.openVariable) };
      render();
      return;
    }

    const inputScopeTarget = clicked(target, '[data-input-scope]');
    if (inputScopeTarget) {
      activeInputScope = inputScopeTarget.dataset.inputScope || '';
      render();
      return;
    }

    const inputBlockTarget = clicked(target, '[data-input-block]');
    if (inputBlockTarget) {
      activeInputBlock = inputBlockTarget.dataset.inputBlock || 'Sources';
      render();
      return;
    }

    const dependencyScopeTarget = clicked(target, '[data-dependency-scope]');
    if (dependencyScopeTarget) {
      activeDependencyScope = dependencyScopeTarget.dataset.dependencyScope || '';
      render();
      return;
    }

    const removeInputTarget = clicked(target, '[data-remove-input]');
    if (removeInputTarget) {
      const entries = selectedInputEntries().slice();
      entries.splice(Number(removeInputTarget.dataset.removeInput), 1);
      post({ type: 'setInputEntries', profileName: optional(activeInputScope), block: activeInputBlock, entries });
      return;
    }

    const removeDependencyTarget = clicked(target, '[data-remove-dependency]');
    if (removeDependencyTarget) {
      const references = referencesForScope(activeDependencyScope).slice();
      references.splice(Number(removeDependencyTarget.dataset.removeDependency), 1);
      post({ type: 'setDependencyUses', profileName: optional(activeDependencyScope), references });
      return;
    }

    const removeVariableTarget = clicked(target, '[data-remove-variable]');
    if (removeVariableTarget) {
      const env = selectedEnvironment();
      if (env) {
        const variables = env.variables.slice();
        variables.splice(Number(removeVariableTarget.dataset.removeVariable), 1);
        post({ type: 'setEnvironmentVariables', environmentName: env.name, variables });
      }
      return;
    }

    const featureTarget = clicked(target, '[data-feature-state]');
    if (featureTarget) {
      if (model.activeProfile) {
        post({ type: 'setFeatureState', profileName: model.activeProfile, packageName: featureTarget.dataset.featurePackage, featureName: featureTarget.dataset.featureName, state: featureTarget.dataset.featureState });
      }
      return;
    }

    if (target.id === 'open-source' || clicked(target, '#open-source')) {
      post({ type: 'openSource' });
      return;
    }
    if (target.id === 'validate' || clicked(target, '#validate')) {
      post({ type: 'validate' });
      return;
    }

    if (clicked(target, '[data-save-project-inline]')) {
      post({ type: 'updateProject', name: optional(byId('project-name').value), defaultProfile: optional(byId('project-default-profile').value) });
      return;
    }

    if (clicked(target, '[data-save-run-inline]')) {
      post({ type: 'setRootLaunch', executable: optional(byId('root-launch-executable').value), workingDirectory: optional(byId('root-launch-working-directory').value) });
      return;
    }

    if (clicked(target, '[data-add-profile]')) {
      drawer = undefined;
      post({ type: 'addProfile', name: byId('new-profile-name').value });
      return;
    }

    const saveProfileTarget = clicked(target, '[data-save-profile]');
    if (saveProfileTarget) {
      drawer = undefined;
      post({
        type: 'updateProfile',
        originalName: saveProfileTarget.dataset.saveProfile,
        name: optional(byId('profile-name').value),
        optimization: optional(byId('profile-optimization').value),
        debugSymbols: byId('profile-symbols').value === 'true',
        linkTimeOptimization: byId('profile-lto').value === 'true',
        toolchain: optional(byId('profile-toolchain').value),
        platform: optional(byId('profile-platform').value),
        operatingSystem: optional(byId('profile-os').value),
        architecture: optional(byId('profile-arch').value),
        environment: optional(byId('profile-env').value),
        launchExecutable: optional(byId('profile-launch-executable').value),
        launchWorkingDirectory: optional(byId('profile-launch-working-directory').value)
      });
      return;
    }

    const deleteProfileTarget = clicked(target, '[data-delete-profile]');
    if (deleteProfileTarget) {
      drawer = undefined;
      post({ type: 'deleteProfile', name: deleteProfileTarget.dataset.deleteProfile });
      return;
    }

    const saveInputTarget = clicked(target, '[data-save-input]');
    if (saveInputTarget) {
      const entries = selectedInputEntries().slice();
      const index = saveInputTarget.dataset.saveInput === '' ? undefined : Number(saveInputTarget.dataset.saveInput);
      const entry = collectInputDialog();
      if (hasInputRuleValue(entry)) {
        if (index === undefined) entries.push(entry);
        else entries[index] = entry;
      }
      drawer = undefined;
      post({ type: 'setInputEntries', profileName: optional(activeInputScope), block: activeInputBlock, entries });
      return;
    }

    const saveDependencyTarget = clicked(target, '[data-save-dependency]');
    if (saveDependencyTarget) {
      const packageName = optional(byId('new-reference-name').value);
      if (packageName) {
        const index = saveDependencyTarget.dataset.saveDependency === '' ? undefined : Number(saveDependencyTarget.dataset.saveDependency);
        const referenceScope = index === undefined ? optional(byId('new-reference-scope').value) : optional(activeDependencyScope);
        const optionalValue = optional(byId('new-reference-optional').value);
        const references = referencesForScope(referenceScope).slice();
        const nextReference = {
          name: packageName,
          version: optional(byId('new-reference-version').value),
          scope: collectUsedForScope(),
          optional: optionalValue === undefined ? undefined : optionalValue === 'true'
        };
        if (index === undefined) {
          references.push(nextReference);
        } else {
          references[index] = nextReference;
        }
        activeDependencyScope = referenceScope || '';
        drawer = undefined;
        post({ type: 'setDependencyUses', profileName: referenceScope, references });
      }
      return;
    }

    if (clicked(target, '[data-add-environment]')) {
      const environmentName = optional(byId('new-environment-name').value);
      if (environmentName) {
        selectedEnvironmentName = environmentName;
        drawer = undefined;
        post({ type: 'setEnvironmentVariables', environmentName, variables: [] });
      }
      return;
    }

    const saveVariableTarget = clicked(target, '[data-save-variable]');
    if (saveVariableTarget) {
      const env = selectedEnvironment();
      const environmentName = env?.name || optional(byId('environment-select')?.value);
      if (environmentName) {
        const variables = (env?.variables || []).slice();
        const index = saveVariableTarget.dataset.saveVariable === '' ? undefined : Number(saveVariableTarget.dataset.saveVariable);
        const variable = collectVariableDialog();
        if (variable.name) {
          if (index === undefined) variables.push(variable);
          else variables[index] = variable;
        }
        drawer = undefined;
        post({ type: 'setEnvironmentVariables', environmentName, variables });
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
    if (event.data?.type !== 'model') return;
    model = event.data.model;
    if (!pages.some(([id]) => id === activePage)) {
      activePage = 'project';
    }
    if (!profileNames().includes(selectedProfileName)) {
      selectedProfileName = model.activeProfile || model.profiles[0]?.name;
    }
    if (!model.environments.some((env) => env.name === selectedEnvironmentName)) {
      selectedEnvironmentName = model.environments[0]?.name;
    }
    render();
  });

  post({ type: 'ready' });
})();
