import * as path from 'node:path';
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
}

export type ProjectTreeGroupKind = 'files' | 'source' | 'config' | 'dependencies' | 'generated' | 'profiles';

export interface ProjectTreeGroupModel {
  kind: 'group';
  id: string;
  label: string;
  tooltip?: string;
  icon: string;
  projectPath: string;
  group: ProjectTreeGroupKind;
}

export interface ProjectTreeProfileModel {
  kind: 'profile';
  label: string;
  description?: string;
  tooltip?: string;
  projectPath: string;
  profileName: string;
  selected: boolean;
}

export type ProjectTreeDependencyKind = 'projects' | 'packages';

export interface ProjectTreeDependencyModel {
  kind: ProjectTreeDependencyKind;
  label: string;
  description?: string;
  tooltip?: string;
  projectPath: string;
  targetPath?: string;
}

export interface ProjectTreeDependenciesModel {
  projects: ProjectTreeDependencyModel[];
  packages: ProjectTreeDependencyModel[];
}

export type ProjectTreeInspectGroupKind =
  | 'packages'
  | 'features'
  | 'generators'
  | 'inputs'
  | 'stage'
  | 'runtime'
  | 'launch'
  | 'publish'
  | 'packageOutputs'
  | 'analyzers'
  | 'diagnostics';

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
  children?: ProjectTreeInspectEntryModel[];
}

export interface ProjectTreeInspectModel {
  groups: ProjectTreeInspectGroupModel[];
  entriesByGroup: Map<ProjectTreeInspectGroupKind, ProjectTreeInspectEntryModel[]>;
}

export type ProjectTreeChildModel = ProjectTreeManifestModel | ProjectTreeGroupModel | ProjectTreeProfileModel;

export interface ProjectTreeModels {
  workspaceLabel?: string;
  workspaceDescription?: string;
  projects: ProjectTreeProjectModel[];
  childrenByProject: Map<string, ProjectTreeChildModel[]>;
  profilesByProject: Map<string, ProjectTreeProfileModel[]>;
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
    return { projects: [], packages: [] };
  }

  const projectRefs = new Map<string, { path: string; owners: Set<string> }>();
  const dependencies = new Map<string, { name: string; owners: Set<string> }>();

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
    const existing = dependencies.get(name);
    if (existing) {
      existing.owners.add(owner);
      return;
    }
    dependencies.set(name, { name, owners: new Set([owner]) });
  };

  for (const reference of project.projectRefs ?? []) {
    addProjectRef(reference.path, 'Project');
  }
  for (const reference of project.dependencies ?? []) {
    addPackageRef(reference.name, 'Project');
  }
  for (const profile of project.profiles) {
    for (const reference of profile.projectRefs ?? []) {
      addProjectRef(reference.path, profile.name);
    }
    for (const reference of profile.dependencies ?? []) {
      addPackageRef(reference.name, profile.name);
    }
  }

  const projectDependencies = Array.from(projectRefs.values())
    .map((reference): ProjectTreeDependencyModel => {
      const resolved = workspace.projects.find((candidate) => comparablePath(candidate.path) === comparablePath(reference.path));
      const label = resolved?.name ?? path.basename(reference.path, path.extname(reference.path));
      return {
        kind: 'projects',
        label,
        description: ownerDescription(reference.owners),
        tooltip: reference.path,
        projectPath: project.path,
        targetPath: reference.path
      };
    })
    .sort((left, right) => left.label.localeCompare(right.label));

  const packageCatalog = workspace.packageCatalog ?? {};
  const packageDependencies = Array.from(dependencies.values())
    .map((reference): ProjectTreeDependencyModel => {
      const resolved = packageCatalog[reference.name];
      return {
        kind: 'packages',
        label: reference.name,
        description: ownerDescription(reference.owners),
        tooltip: resolved?.path ?? reference.name,
        projectPath: project.path,
        targetPath: resolved?.path
      };
    })
    .sort((left, right) => left.label.localeCompare(right.label));

  return {
    projects: projectDependencies,
    packages: packageDependencies
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
    case 'excluded':
      return 'Excluded';
    default:
      return state;
  }
}

function detailEntry(
  label: string,
  description?: string,
  icon = 'symbol-field',
  tooltip?: string,
  targetPath?: string
): ProjectTreeInspectEntryModel {
  return {
    label,
    description,
    icon,
    tooltip,
    targetPath
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
    entriesByGroup.set('generators', plans.generators.map((generator) => ({
      label: generator.name,
      description: [inspectStateLabel(generator.state ?? 'active'), generator.ownerName, generator.tool ?? generator.toolName].filter(Boolean).join(' • ') || undefined,
      tooltip: [
        generator.kind ? `Kind: ${generator.kind}` : undefined,
        generator.reason ? `Reason: ${generator.reason}` : undefined,
        generator.outputs?.length ? `Outputs:\n${generator.outputs.map((output) => `- ${output.role ?? 'Output'} ${output.path ?? ''}`).join('\n')}` : undefined,
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
        ...(generator.outputs ?? []).map((output) => detailEntry(
          output.role ?? 'Output',
          output.path ?? output.target,
          output.role === 'Source' ? 'file-code' : 'file-binary',
          [output.path, output.target].filter(Boolean).join('\n') || undefined
        )),
        detailEntry('Manifest', generator.manifestPath, 'file-code', generator.manifestPath, generator.manifestPath)
      ].filter((entry) => Boolean(entry.description))
    })));
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
    ...(plans?.runtime?.requiredModules ?? []).map((module) => ({
      label: module.name ?? '(module)',
      description: ['required', module.stage, module.order === undefined ? undefined : String(module.order)].filter(Boolean).join(' • ') || undefined,
      icon: 'symbol-module'
    })),
    ...(plans?.runtime?.optionalModules ?? []).map((module) => ({
      label: module.name ?? '(module)',
      description: ['optional', module.stage, module.order === undefined ? undefined : String(module.order)].filter(Boolean).join(' • ') || undefined,
      icon: 'symbol-module'
    })),
    ...(plans?.runtime?.plugins ?? []).map((plugin) => ({
      label: plugin.name ?? '(plugin)',
      description: [plugin.load, plugin.target].filter(Boolean).join(' • ') || undefined,
      icon: 'extensions'
    }))
  ];
  if (runtimeEntries.length > 0) {
    entriesByGroup.set('runtime', runtimeEntries);
  }

  const launchEntries: ProjectTreeInspectEntryModel[] = [];
  for (const launch of plans?.launches ?? (plans?.launch ? [plans.launch] : [])) {
    launchEntries.push({
      label: launch.name ?? 'default',
      description: [launch.selected ? 'Selected' : undefined, launch.executable].filter(Boolean).join(' • ') || undefined,
      icon: launch.selected ? 'play-circle' : 'play',
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

  if (plans?.quality?.analyzers?.length) {
    entriesByGroup.set('analyzers', plans.quality.analyzers.map((analyzer) => ({
      label: analyzer.name,
      description: [analyzer.severity, analyzer.package].filter(Boolean).join(' • ') || undefined,
      icon: 'search',
      children: [
        detailEntry('Tool', analyzer.tool, 'tools'),
        detailEntry('Package', analyzer.package, 'package'),
        detailEntry('Scope', analyzer.scope, 'symbol-property'),
        detailEntry('Severity', analyzer.severity, 'warning'),
        detailEntry('Config', analyzer.configPath, 'settings'),
        detailEntry('Config Optional', analyzer.configOptional === undefined ? undefined : String(analyzer.configOptional), analyzer.configOptional ? 'check' : 'circle-slash')
      ].filter((entry) => Boolean(entry.description))
    })));
  }

  const diagnosticEntries = [
    ...(snapshot.inspectError ? [{
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
  }

  const metadata: Array<{ kind: ProjectTreeInspectGroupKind; label: string; icon: string; tooltip: string }> = [
    { kind: 'packages', label: 'Resolved Packages', icon: 'package', tooltip: 'Resolved packages for the active profile.' },
    { kind: 'features', label: 'Features', icon: 'symbol-property', tooltip: 'Selected and available package features.' },
    { kind: 'generators', label: 'Generators', icon: 'run', tooltip: 'Active and excluded generators for the active profile.' },
    { kind: 'inputs', label: 'Inputs', icon: 'files', tooltip: 'Resolved typed inputs.' },
    { kind: 'stage', label: 'Stage', icon: 'files', tooltip: 'Resolved staged files.' },
    { kind: 'runtime', label: 'Runtime', icon: 'symbol-module', tooltip: 'Resolved runtime modules and plugins.' },
    { kind: 'launch', label: 'Launch', icon: 'play-circle', tooltip: 'Launch, staged file, environment, and lock metadata.' },
    { kind: 'publish', label: 'Publish', icon: 'cloud-upload', tooltip: 'Resolved publish entries.' },
    { kind: 'packageOutputs', label: 'Package Outputs', icon: 'archive', tooltip: 'Resolved package outputs.' },
    { kind: 'analyzers', label: 'Analyzers', icon: 'search', tooltip: 'Resolved quality analyzers.' },
    { kind: 'diagnostics', label: 'Diagnostics', icon: 'warning', tooltip: 'Resolved project diagnostics.' }
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
  const profilesByProject = new Map<string, ProjectTreeProfileModel[]>();
  const dependenciesByProject = new Map<string, ProjectTreeDependenciesModel>();
  const inspectByProject = new Map<string, ProjectTreeInspectModel>();

  if (!snapshot.workspace) {
    return { projects, childrenByProject, profilesByProject, dependenciesByProject, inspectByProject };
  }

  for (const project of snapshot.workspace.projects) {
    const selectedProject = snapshot.context?.project.path === project.path;
    projects.push({
      kind: 'project',
      label: project.name,
      description: selectedProject ? 'Selected' : path.relative(snapshot.workspace.root, project.path),
      tooltip: project.path,
      projectPath: project.path,
      selected: selectedProject
    });

    const children: ProjectTreeChildModel[] = [
      {
        kind: 'manifest',
        label: path.basename(project.path),
        tooltip: project.path,
        projectPath: project.path,
        filePath: project.path
      }
    ];

    const profiles = project.profiles.map((profile) => ({
      kind: 'profile' as const,
      label: profile.name,
      description: snapshot.context?.project.path === project.path && snapshot.context.profile.name === profile.name
        ? 'Current'
        : profile.environment || [profile.operatingSystem, profile.architecture].filter(Boolean).join('/'),
      tooltip: `${project.name} [${profile.name}]`,
      projectPath: project.path,
      profileName: profile.name,
      selected: snapshot.context?.project.path === project.path && snapshot.context.profile.name === profile.name
    }));
    profilesByProject.set(project.path, profiles);

    if (profiles.length > 0) {
      children.push({
        kind: 'group',
        id: `${project.path}:profiles`,
        label: 'Profiles',
        tooltip: 'Project profiles. Click one to make it current.',
        icon: 'symbol-enum',
        projectPath: project.path,
        group: 'profiles'
      });
    }

    const dependencies = buildProjectDependencies(snapshot, project.path);
    dependenciesByProject.set(project.path, dependencies);
    const inspectModel = buildInspectTreeModel(snapshot, project.path);
    if (inspectModel) {
      inspectByProject.set(project.path, inspectModel);
    }
    if ((inspectModel?.groups.length ?? 0) > 0 || dependencies.projects.length > 0 || dependencies.packages.length > 0) {
      children.push({
        kind: 'group',
        id: `${project.path}:dependencies`,
        label: 'Dependencies',
        tooltip: 'Authored uses plus resolved packages, features, generators, launch, publish, analyzers, and diagnostics.',
        icon: 'references',
        projectPath: project.path,
        group: 'dependencies'
      });
    }

    children.push({
      kind: 'group',
      id: `${project.path}:generated`,
      label: 'Generated',
      tooltip: 'Existing generated output and staged artifacts.',
      icon: 'symbol-misc',
      projectPath: project.path,
      group: 'generated'
    });

    const hasSourceInputs = project.sourceRoots.length > 0 || project.buildSources.length > 0;
    const hasConfigInputs = project.configInputs.length > 0
      || project.profiles.some((profile) => profile.configInputs.length > 0);
    if (hasSourceInputs || hasConfigInputs) {
      children.push({
        kind: 'group',
        id: `${project.path}:files`,
        label: 'Files',
        tooltip: 'Declared source and config inputs.',
        icon: 'files',
        projectPath: project.path,
        group: 'files'
      });
    }

    childrenByProject.set(project.path, children);
  }

  return {
    workspaceLabel: snapshot.workspace.workspace.name,
    workspaceDescription: snapshot.workspace.root,
    projects,
    childrenByProject,
    profilesByProject,
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
