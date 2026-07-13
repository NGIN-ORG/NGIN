import * as path from 'node:path';
import type { GraphGeneratorOutput, StoredToolResultSummary } from '../core/types';
import { NginCommandTarget, NginWorkspaceSnapshot } from '../state/workspaceState';

export interface ProjectTreeProjectModel {
  kind: 'project';
  label: string;
  description?: string;
  tooltip?: string;
  projectPath: string;
  selected: boolean;
}

export interface ProjectTreeManifestModel {
  kind: 'manifest';
  label: string;
  tooltip?: string;
  projectPath: string;
  filePath: string;
  description?: string;
}

export type ProjectTreeGroupKind = 'dependencies' | 'tooling' | 'launch' | 'artifacts' | 'problems';

export interface ProjectTreeGroupModel {
  kind: 'group';
  id: string;
  label: string;
  tooltip?: string;
  icon: string;
  projectPath: string;
  group: ProjectTreeGroupKind;
  description?: string;
}

export type ProjectTreeDependencyKind = 'projects' | 'direct' | 'transitive';

export interface ProjectTreeDependencyModel {
  kind: ProjectTreeDependencyKind;
  label: string;
  description?: string;
  tooltip?: string;
  projectPath: string;
  targetPath?: string;
  explainIdentity?: string;
  children?: ProjectTreeInspectEntryModel[];
}

export interface ProjectTreeDependenciesModel {
  projects: ProjectTreeDependencyModel[];
  direct: ProjectTreeDependencyModel[];
  transitive: ProjectTreeDependencyModel[];
}

export type ProjectTreeInspectGroupKind =
  | 'packages'
  | 'features'
  | 'generators'
  | 'inputs'
  | 'stage'
  | 'runtime'
  | 'tooling'
  | 'launch'
  | 'publish'
  | 'packageOutputs'
  | 'toolRuns'
  | 'diagnostics'
  | 'problems';

export interface ProjectTreeInspectGroupModel {
  kind: ProjectTreeInspectGroupKind;
  label: string;
  tooltip?: string;
  icon: string;
  projectPath: string;
}

export interface ProjectTreeInspectEntryModel {
  label: string;
  description?: string;
  tooltip?: string;
  icon?: string;
  targetPath?: string;
  targetKind?: 'file' | 'configuration' | 'report';
  children?: ProjectTreeInspectEntryModel[];
  explainIdentity?: string;
  context?: string;
}

export interface ProjectTreeInspectModel {
  groups: ProjectTreeInspectGroupModel[];
  entriesByGroup: Map<ProjectTreeInspectGroupKind, ProjectTreeInspectEntryModel[]>;
}

export type ProjectTreeChildModel = ProjectTreeManifestModel | ProjectTreeGroupModel;

export interface ProjectTreeModels {
  workspaceLabel?: string;
  workspaceDescription?: string;
  projects: ProjectTreeProjectModel[];
  childrenByProject: Map<string, ProjectTreeChildModel[]>;
  dependenciesByProject: Map<string, ProjectTreeDependenciesModel>;
  inspectByProject: Map<string, ProjectTreeInspectModel>;
}

export interface StatusBarEntryModel {
  text: string;
  tooltip: string;
  command: string;
  arguments?: unknown[];
}

export interface StatusBarModel {
  visible: boolean;
  workspace?: StatusBarEntryModel;
  project?: StatusBarEntryModel;
  profile?: StatusBarEntryModel;
  configure?: StatusBarEntryModel;
  build?: StatusBarEntryModel;
  run?: StatusBarEntryModel;
  debug?: StatusBarEntryModel;
}

function createTarget(snapshot: NginWorkspaceSnapshot): NginCommandTarget | undefined {
  if (!snapshot.context) {
    return undefined;
  }

  return {
    projectPath: snapshot.context.project.path,
    profileName: snapshot.context.profile.name
  };
}

function comparablePath(value: string): string {
  const normalized = path.normalize(value);
  return process.platform === 'win32' ? normalized.toLowerCase() : normalized;
}

function ownerDescription(owners: Set<string>): string | undefined {
  return Array.from(owners).sort().join(', ') || undefined;
}

function buildProjectDependencies(snapshot: NginWorkspaceSnapshot, projectPath: string): ProjectTreeDependenciesModel {
  const workspace = snapshot.workspace;
  const project = workspace?.projects.find((candidate) => comparablePath(candidate.path) === comparablePath(projectPath));
  if (!workspace || !project) {
    return { projects: [], direct: [], transitive: [] };
  }

  const projectRefs = new Map<string, { path: string; owners: Set<string> }>();
  const authoredPackages = new Map<string, Set<string>>();
  const selectedProfile = snapshot.context?.project.path === project.path ? snapshot.context.profile : undefined;

  const addProjectRef = (referencePath: string, owner: string): void => {
    const key = comparablePath(referencePath);
    const existing = projectRefs.get(key);
    if (existing) {
      existing.owners.add(owner);
      return;
    }
    projectRefs.set(key, { path: referencePath, owners: new Set([owner]) });
  };

  const addPackageRef = (name: string, owner: string): void => {
    const owners = authoredPackages.get(name) ?? new Set<string>();
    owners.add(owner);
    authoredPackages.set(name, owners);
  };

  for (const reference of project.projectRefs ?? []) {
    addProjectRef(reference.path, 'Project');
  }
  for (const reference of project.dependencies ?? []) {
    addPackageRef(reference.name, 'Project');
  }
  if (selectedProfile) {
    for (const reference of selectedProfile.projectRefs ?? []) {
      addProjectRef(reference.path, selectedProfile.name);
    }
    for (const reference of selectedProfile.dependencies ?? []) {
      addPackageRef(reference.name, selectedProfile.name);
    }
  }

  const projectDependencies = Array.from(projectRefs.values())
    .map((reference): ProjectTreeDependencyModel => {
      const resolved = workspace.projects.find((candidate) => comparablePath(candidate.path) === comparablePath(reference.path));
      const label = resolved?.name ?? path.basename(reference.path, path.extname(reference.path));
      return {
        kind: 'projects',
        label,
        description: 'workspace',
        tooltip: reference.path,
        projectPath: project.path,
        targetPath: reference.path
      };
    })
    .sort((left, right) => left.label.localeCompare(right.label));

  const packageCatalog = workspace.packageCatalog ?? {};
  const resolvedPackages = snapshot.context?.project.path === project.path
    ? snapshot.inspectGraph?.plans?.packages ?? []
    : [];
  const resolvedByName = new Map(resolvedPackages.map((pkg) => [pkg.name, pkg]));
  const allNames = new Set([...authoredPackages.keys(), ...resolvedByName.keys()]);
  const packageFeatures = snapshot.inspectGraph?.plans?.packageFeatures ?? [];
  const packageModels = Array.from(allNames)
    .map((name): ProjectTreeDependencyModel => {
      const pkg = resolvedByName.get(name);
      const catalogEntry = packageCatalog[name];
      const direct = authoredPackages.has(name);
      const relationship = direct ? 'direct' : 'transitive';
      const scope = pkg?.scope ?? pkg?.closures?.join(', ');
      const features = packageFeatures.filter((feature) => feature.package === name);
      return {
        kind: relationship,
        label: name,
        description: [relationship, scope].filter(Boolean).join(' · '),
        tooltip: [pkg?.version, pkg?.manifestPath ?? catalogEntry?.path].filter(Boolean).join('\n') || name,
        projectPath: project.path,
        targetPath: pkg?.manifestPath ?? catalogEntry?.path,
        explainIdentity: `package:${name}`,
        children: [
          detailEntry('Version', pkg?.version, 'tag'),
          detailEntry('Scope', scope, 'references'),
          detailEntry('Declared By', ownerDescription(authoredPackages.get(name) ?? new Set()), 'references'),
          ...features.map((feature) => ({
            label: feature.feature,
            description: 'enabled feature',
            tooltip: feature.description,
            icon: 'symbol-property',
            explainIdentity: `feature:${feature.package}/${feature.feature}`,
            context: 'detail' as const
          })),
          detailEntry('Manifest', pkg?.manifestPath ?? catalogEntry?.path, 'file-code', pkg?.manifestPath ?? catalogEntry?.path, pkg?.manifestPath ?? catalogEntry?.path)
        ].filter((entry) => Boolean(entry.description))
      };
    })
    .sort((left, right) => left.label.localeCompare(right.label));

  return {
    projects: projectDependencies,
    direct: packageModels.filter((dependency) => dependency.kind === 'direct'),
    transitive: packageModels.filter((dependency) => dependency.kind === 'transitive')
  };
}

function inspectStateLabel(state?: string): string | undefined {
  if (!state) {
    return undefined;
  }
  switch (state) {
    case 'selected':
      return 'Selected';
    case 'available':
      return 'Available';
    case 'disabled':
      return 'Disabled';
    case 'conditionExcluded':
      return 'Excluded';
    case 'unavailable':
      return 'Unavailable';
    case 'active':
      return 'Active';
    case 'ready':
      return 'Ready';
    case 'invalid':
      return 'Invalid';
    case 'excluded':
      return 'Excluded';
    default:
      return state;
  }
}

function readableToolValue(value?: string): string | undefined {
  if (!value) {
    return undefined;
  }
  return value
    .replace(/([a-z0-9])([A-Z])/g, '$1 $2')
    .replace(/[-_]+/g, ' ')
    .replace(/^./, (first) => first.toUpperCase());
}

function toolInputScopeLabel(value?: string): string | undefined {
  switch (value) {
    case 'Product': return 'This project';
    case 'ProductClosure': return 'Project and dependencies';
    case 'Workspace': return 'Entire workspace';
    case 'Explicit': return 'Explicit patterns';
    case 'ActiveFile': return 'Active file';
    case 'ChangedFiles': return 'Changed files';
    default: return readableToolValue(value);
  }
}

function toolRunPolicyLabel(gate?: boolean, failOn?: string): string {
  if (!gate) {
    return 'Non-blocking';
  }
  return failOn ? `Fails on ${failOn.toLowerCase()} or higher` : 'Blocking';
}

function toolRunResultLabel(result: StoredToolResultSummary | undefined): string | undefined {
  if (!result) return undefined;
  if (result.executionStatus && result.executionStatus !== 'succeeded') return readableToolValue(result.executionStatus);
  if (result.gateStatus === 'failed') return 'Gate failed';
  const findings = result.diagnostics.filter((diagnostic) => !diagnostic.suppressed);
  const errors = findings.filter((diagnostic) => ['error', 'fatal'].includes(diagnostic.severity?.toLowerCase() ?? '')).length;
  const warnings = findings.filter((diagnostic) => diagnostic.severity?.toLowerCase() === 'warning').length;
  if (errors) return `${errors} ${errors === 1 ? 'error' : 'errors'}`;
  if (warnings) return `${warnings} ${warnings === 1 ? 'warning' : 'warnings'}`;
  if (findings.length) return `${findings.length} findings`;
  if (result.changeStatus === 'proposed') {
    return `${result.edits.length} ${result.edits.length === 1 ? 'change' : 'changes'} available`;
  }
  if (result.changeStatus === 'applied') {
    return `${result.edits.length} ${result.edits.length === 1 ? 'change' : 'changes'} applied`;
  }
  return 'No findings';
}

function toolRunContext(run: { state?: string; capabilities?: string[]; kind?: string }, result?: StoredToolResultSummary): string {
  const parts = ['toolRun', run.state ?? 'ready'];
  const capabilities = new Set((run.capabilities ?? []).map((capability) => capability.toLowerCase()));
  if (capabilities.has('active-file')) parts.push('activeFile');
  if (capabilities.has('changed-files')) parts.push('changedFiles');
  if (result?.changeStatus === 'proposed' && result.edits.length) parts.push('edits');
  return parts.join('.');
}

function detailEntry(
  label: string,
  description?: string,
  icon = 'symbol-field',
  tooltip?: string,
  targetPath?: string,
  targetKind: ProjectTreeInspectEntryModel['targetKind'] = 'file'
): ProjectTreeInspectEntryModel {
  return {
    label,
    description,
    icon,
    tooltip,
    targetPath,
    targetKind
  };
}

function inputKindLabel(kind: string): string {
  switch (kind) {
    case 'Source':
      return 'Sources';
    case 'Config':
      return 'Configs';
    case 'Content':
      return 'Contents';
    case 'Asset':
      return 'Assets';
    case 'Generated':
      return 'Generated';
    case 'ToolInput':
      return 'Tool Inputs';
    default:
      return kind;
  }
}

function inputKindIcon(kind: string): string {
  switch (kind) {
    case 'Source':
      return 'file-code';
    case 'Config':
      return 'settings';
    case 'Generated':
      return 'file-binary';
    case 'Asset':
      return 'file-media';
    default:
      return 'file';
  }
}

function inputLabel(input: { source?: string; absoluteSourcePath?: string; name?: string }): string {
  return input.source || input.absoluteSourcePath || input.name || '(unnamed)';
}

function generatorOutputs(outputs: unknown): GraphGeneratorOutput[] {
  if (!Array.isArray(outputs)) {
    return [];
  }
  return outputs.filter((output): output is GraphGeneratorOutput => typeof output === 'object' && output !== null);
}

function generatorOutputsTooltip(outputs: unknown): string | undefined {
  const detailedOutputs = generatorOutputs(outputs);
  if (detailedOutputs.length > 0) {
    return `Outputs:\n${detailedOutputs.map((output) => `- ${output.role ?? 'Output'} ${output.path ?? ''}`).join('\n')}`;
  }
  if (typeof outputs === 'number') {
    return `Outputs: ${outputs}`;
  }
  return undefined;
}

export function buildInspectTreeModel(snapshot: NginWorkspaceSnapshot, projectPath: string): ProjectTreeInspectModel | undefined {
  if (!snapshot.context || comparablePath(snapshot.context.project.path) !== comparablePath(projectPath)) {
    return undefined;
  }

  const entriesByGroup = new Map<ProjectTreeInspectGroupKind, ProjectTreeInspectEntryModel[]>();
  const graph = snapshot.inspectGraph;
  const plans = graph?.plans;

  if (plans?.packages?.length) {
    entriesByGroup.set('packages', plans.packages.map((pkg) => ({
      label: pkg.name,
      description: [pkg.version, pkg.scope ?? pkg.closures?.join(', ')].filter(Boolean).join(' • ') || undefined,
      tooltip: [pkg.name, pkg.manifestPath].filter(Boolean).join('\n'),
      icon: 'package',
      targetPath: pkg.manifestPath,
      children: [
        detailEntry('Version', pkg.version, 'tag'),
        detailEntry('Scope', pkg.scope, 'references'),
        detailEntry('Closures', pkg.closures?.join(', '), 'references'),
        detailEntry('Source', pkg.source, 'symbol-property'),
        detailEntry('Provider Root', pkg.providerRoot, 'folder-opened', pkg.providerRoot, pkg.providerRoot),
        detailEntry('Manifest', pkg.manifestPath, 'file-code', pkg.manifestPath, pkg.manifestPath)
      ].filter((entry) => Boolean(entry.description))
    })));
  }

  if (plans?.packageFeatures?.length) {
    entriesByGroup.set('features', plans.packageFeatures.map((feature) => ({
      label: `${feature.package}::${feature.feature}`,
      description: 'Selected',
      tooltip: [feature.description, feature.manifestPath].filter(Boolean).join('\n'),
      icon: 'check',
      targetPath: feature.manifestPath,
      children: [
        detailEntry('State', 'Selected', 'check'),
        detailEntry('Package', [feature.package, feature.packageVersion].filter(Boolean).join(' '), 'package'),
        detailEntry('Description', feature.description, 'comment'),
        detailEntry('Manifest', feature.manifestPath, 'file-code', feature.manifestPath, feature.manifestPath)
      ].filter((entry) => Boolean(entry.description))
    })));
  }

  if (plans?.generators?.length) {
    entriesByGroup.set('generators', plans.generators.map((generator) => {
      const outputs = generatorOutputs(generator.outputs);
      return {
        label: generator.name,
        description: [inspectStateLabel(generator.state ?? 'active'), generator.ownerName, generator.tool ?? generator.toolName].filter(Boolean).join(' • ') || undefined,
        tooltip: [
          generator.kind ? `Kind: ${generator.kind}` : undefined,
          generator.reason ? `Reason: ${generator.reason}` : undefined,
          generatorOutputsTooltip(generator.outputs),
          generator.manifestPath
        ].filter(Boolean).join('\n'),
        icon: generator.state === undefined || generator.state === 'active' ? 'run' : 'circle-slash',
        targetPath: generator.manifestPath,
        children: [
          detailEntry('State', inspectStateLabel(generator.state ?? 'active'), generator.state === undefined || generator.state === 'active' ? 'check' : 'circle-slash'),
          detailEntry('Kind', generator.kind, 'symbol-method'),
          detailEntry('Owner', generator.ownerName, 'references'),
          detailEntry('Owner Kind', generator.ownerKind, 'symbol-property'),
          detailEntry('Package', generator.package, 'package'),
          detailEntry('Tool', generator.tool ?? generator.toolName, 'tools'),
          detailEntry('Reason', generator.reason, 'comment'),
          ...outputs.map((output) => detailEntry(
            output.role ?? 'Output',
            output.path ?? output.target,
            output.role === 'Source' ? 'file-code' : 'file-binary',
            [output.path, output.target].filter(Boolean).join('\n') || undefined
          )),
          detailEntry('Manifest', generator.manifestPath, 'file-code', generator.manifestPath, generator.manifestPath)
        ].filter((entry) => Boolean(entry.description))
      };
    }));
  }

  const inputKindEntries: ProjectTreeInspectEntryModel[] = [];
  const inputsByKind = new Map<string, NonNullable<NonNullable<typeof plans>['build']>['inputs']>();
  for (const input of plans?.build?.inputs ?? []) {
    const kind = input.kind ?? input.role ?? 'Source';
    inputsByKind.set(kind, [...(inputsByKind.get(kind) ?? []), input]);
  }
  for (const [kind, inputs] of inputsByKind) {
    const inputEntries = inputs.map((input): ProjectTreeInspectEntryModel => ({
      label: inputLabel(input),
      description: [input.role, input.mode, input.ownerName ?? input.owner].filter(Boolean).join(' • ') || undefined,
      tooltip: [input.absoluteSourcePath, input.stagedRelativePath ? `Stages: ${input.stagedRelativePath}` : undefined, input.manifestPath].filter(Boolean).join('\n'),
      icon: inputKindIcon(kind),
      targetPath: input.manifestPath,
      children: [
        detailEntry('Role', input.role, 'symbol-property'),
        detailEntry('Mode', input.mode, 'symbol-enum'),
        detailEntry('Owner', input.ownerName ?? input.owner, 'references'),
        detailEntry('Owner Kind', input.ownerKind, 'symbol-property'),
        detailEntry('Visibility', input.visibility, 'eye'),
        detailEntry('Source', input.source, 'file'),
        detailEntry('Absolute Source', input.absoluteSourcePath, 'go-to-file', input.absoluteSourcePath, input.absoluteSourcePath),
        detailEntry('Target', input.target, 'target'),
        detailEntry('Target Root', input.targetRoot, 'folder-opened'),
        detailEntry('Staged Path', input.stagedRelativePath, 'files'),
        detailEntry('Manifest', input.manifestPath, 'file-code', input.manifestPath, input.manifestPath)
      ].filter((entry) => Boolean(entry.description))
    }));
    if (inputEntries.length > 0) {
      inputKindEntries.push({
        label: inputKindLabel(kind),
        description: `${inputEntries.length}`,
        tooltip: `Resolved ${inputKindLabel(kind).toLowerCase()} for the active profile.`,
        icon: inputKindIcon(kind),
        children: inputEntries
      });
    }
  }
  if (inputKindEntries.length > 0) {
    entriesByGroup.set('inputs', inputKindEntries);
  }

  if (plans?.stage?.files?.length) {
    entriesByGroup.set('stage', plans.stage.files.map((file) => ({
      label: file.target ?? file.relativeDestination ?? file.source ?? '(staged file)',
      description: file.kind,
      tooltip: file.source,
      icon: 'files'
    })));
  }

  const runtimeEntries: ProjectTreeInspectEntryModel[] = [
    ...(plans?.runtime?.requiredModules ?? []).map((module) => {
      const moduleName = typeof module === 'string' ? module : module.name;
      const moduleStage = typeof module === 'string' ? undefined : module.stage;
      const moduleOrder = typeof module === 'string' ? undefined : module.order;
      return {
        label: moduleName ?? '(module)',
        description: ['required', moduleStage, moduleOrder === undefined ? undefined : String(moduleOrder)].filter(Boolean).join(' • ') || undefined,
        icon: 'symbol-module'
      };
    }),
    ...(plans?.runtime?.optionalModules ?? []).map((module) => {
      const moduleName = typeof module === 'string' ? module : module.name;
      const moduleStage = typeof module === 'string' ? undefined : module.stage;
      const moduleOrder = typeof module === 'string' ? undefined : module.order;
      return {
        label: moduleName ?? '(module)',
        description: ['optional', moduleStage, moduleOrder === undefined ? undefined : String(moduleOrder)].filter(Boolean).join(' • ') || undefined,
        icon: 'symbol-module'
      };
    }),
    ...(plans?.runtime?.plugins ?? []).map((plugin) => {
      const pluginName = typeof plugin === 'string' ? plugin : plugin.name;
      const pluginLoad = typeof plugin === 'string' ? undefined : plugin.load;
      const pluginTarget = typeof plugin === 'string' ? undefined : plugin.target;
      return {
        label: pluginName ?? '(plugin)',
        description: [pluginLoad, pluginTarget].filter(Boolean).join(' • ') || undefined,
        icon: 'extensions'
      };
    })
  ];
  if (runtimeEntries.length > 0) {
    entriesByGroup.set('runtime', runtimeEntries);
  }

  const launchEntries: ProjectTreeInspectEntryModel[] = [];
  for (const launch of plans?.launches ?? (plans?.launch ? [plans.launch] : [])) {
    launchEntries.push({
      label: launch.name ?? 'default',
      description: launch.executable,
      icon: launch.selected ? 'play-circle' : 'play',
      explainIdentity: `launch:${launch.name ?? 'default'}`,
      context: 'launch',
      children: [
        detailEntry('Executable', launch.executable, 'target'),
        detailEntry('Working Directory', launch.workingDirectory, 'folder-opened'),
        detailEntry('Args', launch.args, 'terminal')
      ].filter((entry) => Boolean(entry.description))
    });
  }
  if (plans?.environment?.variables?.length) {
    launchEntries.push({
      label: 'Environment',
      description: `${plans.environment.variables.length}`,
      icon: 'symbol-variable',
      children: plans.environment.variables.map((variable) => ({
        label: variable.name,
        description: variable.secret ? '<redacted>' : variable.value,
        tooltip: variable.source,
        icon: variable.secret ? 'lock' : 'symbol-variable',
        children: [
          detailEntry('Resolved', variable.resolved === undefined ? undefined : String(variable.resolved), variable.resolved ? 'check' : 'circle-slash'),
          detailEntry('Source', variable.source, 'symbol-property')
        ].filter((entry) => Boolean(entry.description))
      }))
    });
  }
  if (launchEntries.length > 0) {
    entriesByGroup.set('launch', launchEntries);
  }

  if (plans?.publish?.length) {
    entriesByGroup.set('publish', plans.publish.map((publish) => ({
      label: publish.name ?? '(publish)',
      description: [publish.kind, publish.format, publish.output].filter(Boolean).join(' • ') || undefined,
      icon: 'cloud-upload'
    })));
  }

  if (plans?.packageOutputs?.length) {
    entriesByGroup.set('packageOutputs', plans.packageOutputs.map((output) => ({
      label: output.name ?? '(package output)',
      description: [output.version, output.output].filter(Boolean).join(' • ') || undefined,
      icon: 'archive'
    })));
  }

  if (plans?.tooling?.runs?.length) {
    entriesByGroup.set('toolRuns', plans.tooling.runs.map((run) => {
      const state = inspectStateLabel(run.state ?? 'ready');
      const result = snapshot.toolResults?.[run.name];
      const resultLabel = toolRunResultLabel(result);
      const files = [
        toolInputScopeLabel(run.inputScope),
        run.includeGenerated ? 'includes generated files' : undefined
      ].filter(Boolean).join(' • ');
      const advanced = [
        detailEntry('Run Identity', run.name, 'key'),
        detailEntry('Action', run.action, 'symbol-method'),
        detailEntry('Tool Resolution', [run.toolSource, run.toolPath].filter(Boolean).join(' • '), 'tools'),
        detailEntry('Driver', run.driver, 'server-process'),
        detailEntry('Driver Resolution', [run.driverSource, run.driverPath].filter(Boolean).join(' • '), 'server-process'),
        detailEntry('Package', run.package, 'package'),
        detailEntry('Input Contract', run.inputContract, 'symbol-interface'),
        detailEntry('Cache', run.cache, 'database'),
        detailEntry('Depends On', run.dependencies?.join(', '), 'references'),
        detailEntry('Provided By', [run.originProvenance?.sourceKind, run.originProvenance?.sourceName].filter(Boolean).join(' • '), 'source-control'),
        detailEntry('Effective Override', [run.provenance?.sourceKind, run.provenance?.sourceName].filter(Boolean).join(' • '), 'layers')
      ].filter((entry) => Boolean(entry.description));
      const configurations = (run.configPaths ?? []).map((config, index) => detailEntry(
        run.configNames?.[index] ? `Configuration — ${run.configNames[index]}` : 'Configuration',
        [config, run.configOptional?.[index] ? 'optional' : undefined].filter(Boolean).join(' • '),
        'settings-gear',
        undefined,
        config.includes('$(') ? undefined : path.resolve(path.dirname(projectPath), config),
        'configuration'
      ));
      const reports = (run.reportPaths ?? []).map((report, index) => detailEntry(
        run.reportNames?.[index] ? `Report — ${run.reportNames[index]}` : `Report ${index + 1}`,
        [run.reportFormats?.[index], report].filter(Boolean).join(' • '),
        'file-text',
        report,
        report.includes('$(OutputDir)') && snapshot.outputDir
          ? report.replace('$(OutputDir)', snapshot.outputDir)
          : report.includes('$(') ? undefined : path.resolve(path.dirname(projectPath), report),
        'report'
      ));
      const tooltip = [
        run.description,
        [run.kind, run.tool ? `with ${run.tool}` : undefined].filter(Boolean).join(' '),
        state,
        files ? `Files: ${files}` : undefined,
        `Policy: ${toolRunPolicyLabel(run.gate, run.failOn)}`,
        run.diagnostic
      ].filter(Boolean).join('\n');

      return {
        label: run.displayName || run.name,
        description: [run.kind, resultLabel ?? state].filter(Boolean).join(' · ') || undefined,
        tooltip,
        explainIdentity: `run:${run.name}`,
        context: toolRunContext(run, result),
        icon: run.state === 'invalid' ? 'error'
          : run.state === 'unavailable' ? 'warning'
            : run.state === 'disabled' || run.state === 'excluded' ? 'circle-slash'
              : run.kind === 'Format' ? 'symbol-color'
                : run.kind === 'Report' ? 'file-text' : 'search',
        children: [
          detailEntry('Tool', run.tool, 'tools'),
          detailEntry('Inputs', files || undefined, 'files'),
          detailEntry('Policy', toolRunPolicyLabel(run.gate, run.failOn), run.gate ? 'warning' : 'pass'),
          ...configurations,
          ...(result ? [detailEntry(
            'Last Run',
            [resultLabel, result.durationMs === undefined ? undefined : `${(result.durationMs / 1000).toFixed(1)}s`,
              result.cacheStatus && result.cacheStatus !== 'disabled' ? `cache ${result.cacheStatus}` : undefined].filter(Boolean).join(' • '),
            result.executionStatus === 'succeeded' ? 'history' : 'error',
            new Date(result.modifiedAt).toLocaleString()
          ), detailEntry('Results', path.basename(result.path), 'json', result.path, result.path)] : []),
          ...reports,
          ...(result?.artifacts ?? []).flatMap((artifact, index) => artifact.path ? [detailEntry(
            `Artifact ${index + 1}`, artifact.path, 'file-binary', artifact.path, artifact.path, 'report'
          )] : []),
          ...(run.diagnostic ? [detailEntry('Problem', run.diagnostic, 'error')] : []),
          ...(advanced.length ? [{
            label: 'Advanced',
            description: 'Execution and integration details',
            icon: 'settings-gear',
            children: advanced
          }] : [])
        ].filter((entry) => Boolean(entry.description))
      };
    }));
  }

  const diagnosticEntries = [
    ...(snapshot.inspectError && !(plans?.diagnostics?.length) ? [{
      label: snapshot.inspectError,
      description: 'Inspect',
      icon: 'warning'
    }] : []),
    ...(plans?.diagnostics ?? []).map((diagnostic) => ({
      label: diagnostic.message,
      description: diagnostic.severity,
      tooltip: diagnostic.subject,
      icon: diagnostic.severity === 'error' ? 'error' : 'warning'
    }))
  ];
  if (diagnosticEntries.length > 0) {
    entriesByGroup.set('diagnostics', diagnosticEntries);
    entriesByGroup.set('problems', diagnosticEntries.map((entry) => ({ ...entry, context: 'problem' })));
  }

  const generatorEntries = entriesByGroup.get('generators') ?? [];
  const toolingEntries = [
    ...(plans?.generators ?? []).flatMap((generator, index) => {
      const entry = generatorEntries[index];
      if (!entry || generator.state === 'excluded') {
        return [];
      }
      return [{
        ...entry,
        label: /reflection|metagen/i.test(entry.label) ? 'Reflection code generation' : entry.label,
        description: [generator.tool ?? generator.toolName, 'active'].filter(Boolean).join(' · '),
        explainIdentity: `generator:${generator.name}`,
        context: 'tooling' as const
      }];
    }),
    ...(entriesByGroup.get('toolRuns') ?? [])
  ];
  if (toolingEntries.length > 0) {
    entriesByGroup.set('tooling', toolingEntries);
  }

  const metadata: Array<{ kind: ProjectTreeInspectGroupKind; label: string; icon: string; tooltip: string }> = [
    { kind: 'tooling', label: 'Tooling', icon: 'tools', tooltip: 'Active generators and tool runs for the selected profile.' },
    { kind: 'launch', label: 'Launch', icon: 'play-circle', tooltip: 'Selected launch and environment.' },
    { kind: 'problems', label: 'Problems', icon: 'warning', tooltip: 'Resolver and inspection problems.' }
  ];

  const groups = metadata
    .filter((group) => (entriesByGroup.get(group.kind)?.length ?? 0) > 0)
    .map((group) => ({
      ...group,
      projectPath
    }));

  return { groups, entriesByGroup };
}

export function buildProjectTreeModels(snapshot: NginWorkspaceSnapshot): ProjectTreeModels {
  const projects: ProjectTreeProjectModel[] = [];
  const childrenByProject = new Map<string, ProjectTreeChildModel[]>();
  const dependenciesByProject = new Map<string, ProjectTreeDependenciesModel>();
  const inspectByProject = new Map<string, ProjectTreeInspectModel>();

  if (!snapshot.workspace) {
    return { projects, childrenByProject, dependenciesByProject, inspectByProject };
  }

  for (const project of snapshot.workspace.projects) {
    const selectedProject = snapshot.context?.project.path === project.path;
    const resolvedProblemCount = selectedProject ? snapshot.inspectGraph?.plans?.diagnostics?.length ?? 0 : 0;
    const problemCount = resolvedProblemCount + (selectedProject && snapshot.inspectError && resolvedProblemCount === 0 ? 1 : 0);
    projects.push({
      kind: 'project',
      label: project.name,
      description: selectedProject
        ? [
            'active',
            snapshot.context?.profile.name ?? project.defaultProfile ?? 'default',
            problemCount > 0 ? `${problemCount} ${problemCount === 1 ? 'problem' : 'problems'}` : undefined
          ].filter(Boolean).join(' · ')
        : undefined,
      tooltip: project.path,
      projectPath: project.path,
      selected: selectedProject
    });

    const children: ProjectTreeChildModel[] = [
      {
        kind: 'manifest',
        label: 'Manifest',
        description: path.basename(project.path),
        tooltip: project.path,
        projectPath: project.path,
        filePath: project.path
      }
    ];

    const dependencies = buildProjectDependencies(snapshot, project.path);
    dependenciesByProject.set(project.path, dependencies);
    const inspectModel = buildInspectTreeModel(snapshot, project.path);
    if (inspectModel) {
      inspectByProject.set(project.path, inspectModel);
    }
    const dependencyCount = dependencies.projects.length + dependencies.direct.length + dependencies.transitive.length;
    if (selectedProject && dependencyCount > 0) {
      children.push({
        kind: 'group',
        id: `${project.path}:dependencies`,
        label: 'Dependencies',
        description: String(dependencyCount),
        tooltip: 'Workspace references and direct/transitive package dependencies.',
        icon: 'references',
        projectPath: project.path,
        group: 'dependencies'
      });
    }

    if (selectedProject) {
      for (const group of (inspectModel?.groups ?? []).filter((candidate) => candidate.kind !== 'problems')) {
        const entries = inspectModel?.entriesByGroup.get(group.kind) ?? [];
        children.push({
          kind: 'group',
          id: `${project.path}:${group.kind}`,
          label: group.label,
          description: group.kind === 'problems' || group.kind === 'tooling' ? String(entries.length) : undefined,
          tooltip: group.tooltip,
          icon: group.icon,
          projectPath: project.path,
          group: group.kind as 'tooling' | 'launch' | 'problems'
        });
      }
    }

    if (selectedProject && (snapshot.launchManifestExists || snapshot.stagedCompileCommandsAvailable)) {
      children.push({
        kind: 'group',
        id: `${project.path}:artifacts`,
        label: 'Artifacts',
        tooltip: 'Executable, staged application, launch metadata, and compile database.',
        icon: 'archive',
        projectPath: project.path,
        group: 'artifacts'
      });
    }

    if (selectedProject) {
      const problems = inspectModel?.groups.find((group) => group.kind === 'problems');
      if (problems) {
        const entries = inspectModel?.entriesByGroup.get('problems') ?? [];
        children.push({
          kind: 'group',
          id: `${project.path}:problems`,
          label: problems.label,
          description: String(entries.length),
          tooltip: problems.tooltip,
          icon: problems.icon,
          projectPath: project.path,
          group: 'problems'
        });
      }
    }

    childrenByProject.set(project.path, children);
  }

  return {
    workspaceLabel: snapshot.workspace.workspace.name,
    workspaceDescription: snapshot.workspace.root,
    projects,
    childrenByProject,
    dependenciesByProject,
    inspectByProject
  };
}

export function buildStatusBarModel(snapshot: NginWorkspaceSnapshot): StatusBarModel {
  if (!snapshot.workspace || !snapshot.context) {
    return { visible: false };
  }

  const target = createTarget(snapshot);
  const selectionLabel = `${snapshot.context.project.name} [${snapshot.context.profile.name}]`;
  return {
    visible: true,
    workspace: {
      text: `$(folder-library) ${snapshot.workspace.workspace.name}`,
      tooltip: `${snapshot.workspace.workspace.name}\n${snapshot.workspace.root}`,
      command: 'ngin.workspaceStatus'
    },
    project: {
      text: `$(project) ${snapshot.context.project.name}`,
      tooltip: snapshot.context.project.path,
      command: 'ngin.internal.pickProject'
    },
    profile: {
      text: `$(symbol-enum) ${snapshot.context.profile.name}`,
      tooltip: `${selectionLabel}\nTarget: ${[snapshot.context.profile.operatingSystem, snapshot.context.profile.architecture].filter(Boolean).join('/') || 'n/a'}`,
      command: 'ngin.internal.pickProfile'
    },
    configure: {
      text: '$(settings) Configure',
      tooltip: `Configure build metadata for ${selectionLabel}\n${snapshot.outputDir ?? ''}`.trim(),
      command: 'ngin.configure',
      arguments: target ? [target] : undefined
    },
    build: {
      text: '$(gear) Build',
      tooltip: `Build ${selectionLabel}\n${snapshot.outputDir ?? ''}`.trim(),
      command: 'ngin.build',
      arguments: target ? [target] : undefined
    },
    run: {
      text: '$(play) Run',
      tooltip: `Run ${selectionLabel}`,
      command: 'ngin.run',
      arguments: target ? [target] : undefined
    },
    debug: {
      text: '$(bug) Debug',
      tooltip: `Debug ${selectionLabel}`,
      command: 'ngin.debug',
      arguments: target ? [target] : undefined
    }
  };
}
