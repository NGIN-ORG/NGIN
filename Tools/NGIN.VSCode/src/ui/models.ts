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
  | 'capabilities'
  | 'generators'
  | 'inputs'
  | 'launch'
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
  const packageRefs = new Map<string, { name: string; owners: Set<string> }>();

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
    const existing = packageRefs.get(name);
    if (existing) {
      existing.owners.add(owner);
      return;
    }
    packageRefs.set(name, { name, owners: new Set([owner]) });
  };

  for (const reference of project.projectRefs ?? []) {
    addProjectRef(reference.path, 'Project');
  }
  for (const reference of project.packageRefs ?? []) {
    addPackageRef(reference.name, 'Project');
  }
  for (const profile of project.profiles) {
    for (const reference of profile.projectRefs ?? []) {
      addProjectRef(reference.path, profile.name);
    }
    for (const reference of profile.packageRefs ?? []) {
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
  const packageDependencies = Array.from(packageRefs.values())
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
  const inspect = snapshot.inspect;

  if (inspect?.packages?.length) {
    entriesByGroup.set('packages', inspect.packages.map((pkg) => ({
      label: pkg.name,
      description: [pkg.version, pkg.requiredBy?.join(', ')].filter(Boolean).join(' • ') || undefined,
      tooltip: [pkg.name, pkg.manifestPath].filter(Boolean).join('\n'),
      icon: 'package',
      targetPath: pkg.manifestPath,
      children: [
        detailEntry('Version', pkg.version, 'tag'),
        detailEntry('Required By', pkg.requiredBy?.join(', '), 'references'),
        detailEntry('Source', pkg.source, 'symbol-property'),
        detailEntry('Provider Root', pkg.providerRoot, 'folder-opened', pkg.providerRoot, pkg.providerRoot),
        detailEntry('Manifest', pkg.manifestPath, 'file-code', pkg.manifestPath, pkg.manifestPath)
      ].filter((entry) => Boolean(entry.description))
    })));
  }

  if (inspect?.packageFeatures?.length) {
    entriesByGroup.set('features', inspect.packageFeatures.map((feature) => ({
      label: `${feature.package}::${feature.feature}`,
      description: inspectStateLabel(feature.state),
      tooltip: [feature.description, feature.manifestPath].filter(Boolean).join('\n'),
      icon: feature.state === 'selected' ? 'check' : feature.state === 'disabled' || feature.state === 'conditionExcluded' ? 'circle-slash' : 'symbol-property',
      targetPath: feature.manifestPath,
      children: [
        detailEntry('State', inspectStateLabel(feature.state), feature.state === 'selected' ? 'check' : 'symbol-property'),
        detailEntry('Package', [feature.package, feature.packageVersion].filter(Boolean).join(' '), 'package'),
        detailEntry('Description', feature.description, 'comment'),
        detailEntry('Manifest', feature.manifestPath, 'file-code', feature.manifestPath, feature.manifestPath)
      ].filter((entry) => Boolean(entry.description))
    })));
  }

  const capabilityEntries: ProjectTreeInspectEntryModel[] = [];
  for (const provider of inspect?.capabilities?.providers ?? []) {
    capabilityEntries.push({
      label: provider.name,
      description: `${provider.package}::${provider.feature}${provider.exclusive ? ' • exclusive' : ''}`,
      icon: provider.exclusive ? 'lock' : 'symbol-interface'
    });
  }
  for (const requirement of inspect?.capabilities?.requirements ?? []) {
    capabilityEntries.push({
      label: `requires ${requirement.name}`,
      description: `${requirement.package}::${requirement.feature}${requirement.missing ? ' • missing' : ''}`,
      icon: requirement.missing ? 'error' : 'link'
    });
  }
  for (const conflict of inspect?.capabilities?.exclusiveConflicts ?? []) {
    capabilityEntries.push({
      label: `conflict ${conflict}`,
      description: 'Exclusive capability conflict',
      icon: 'error'
    });
  }
  if (capabilityEntries.length > 0) {
    entriesByGroup.set('capabilities', capabilityEntries);
  }

  if (inspect?.generators?.length) {
    entriesByGroup.set('generators', inspect.generators.map((generator) => ({
      label: generator.name,
      description: [inspectStateLabel(generator.state), generator.ownerName, generator.tool].filter(Boolean).join(' • ') || undefined,
      tooltip: [
        generator.kind ? `Kind: ${generator.kind}` : undefined,
        generator.reason ? `Reason: ${generator.reason}` : undefined,
        generator.outputs?.length ? `Outputs:\n${generator.outputs.map((output) => `- ${output.role ?? 'Output'} ${output.path ?? ''}`).join('\n')}` : undefined,
        generator.manifestPath
      ].filter(Boolean).join('\n'),
      icon: generator.state === 'active' ? 'run' : 'circle-slash',
      targetPath: generator.manifestPath,
      children: [
        detailEntry('State', inspectStateLabel(generator.state), generator.state === 'active' ? 'check' : 'circle-slash'),
        detailEntry('Kind', generator.kind, 'symbol-method'),
        detailEntry('Owner', generator.ownerName, 'references'),
        detailEntry('Owner Kind', generator.ownerKind, 'symbol-property'),
        detailEntry('Package', generator.package, 'package'),
        detailEntry('Tool', generator.tool, 'tools'),
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
  for (const [kind, inputs] of Object.entries(inspect?.inputs ?? {})) {
    const inputEntries = inputs.map((input): ProjectTreeInspectEntryModel => ({
      label: inputLabel(input),
      description: [input.role, input.mode, input.ownerName].filter(Boolean).join(' • ') || undefined,
      tooltip: [input.absoluteSourcePath, input.stagedRelativePath ? `Stages: ${input.stagedRelativePath}` : undefined, input.manifestPath].filter(Boolean).join('\n'),
      icon: inputKindIcon(kind),
      targetPath: input.manifestPath,
      children: [
        detailEntry('Role', input.role, 'symbol-property'),
        detailEntry('Mode', input.mode, 'symbol-enum'),
        detailEntry('Owner', input.ownerName, 'references'),
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

  const launchEntries: ProjectTreeInspectEntryModel[] = [];
  if (inspect?.launch?.executable) {
    const executable = inspect.launch.executable;
    launchEntries.push({
      label: executable.name,
      description: [executable.target, executable.origin].filter(Boolean).join(' • ') || 'Executable',
      icon: 'play',
      children: [
        detailEntry('Target', executable.target, 'target'),
        detailEntry('Origin', executable.origin, 'symbol-property')
      ].filter((entry) => Boolean(entry.description))
    });
  }
  if (inspect?.launch?.workingDirectory) {
    launchEntries.push({
      label: 'Working Directory',
      description: inspect.launch.workingDirectory,
      icon: 'folder-opened'
    });
  }
  if (inspect?.stagedFiles?.length) {
    launchEntries.push({
      label: 'Staged Files',
      description: `${inspect.stagedFiles.length}`,
      icon: 'files',
      children: inspect.stagedFiles.map((file) => ({
        label: file.relativeDestination ?? file.source ?? file.kind,
        description: file.kind,
        tooltip: file.source,
        icon: 'files'
      }))
    });
  }
  if (inspect?.environmentVariables?.length) {
    launchEntries.push({
      label: 'Environment',
      description: `${inspect.environmentVariables.length}`,
      icon: 'symbol-variable',
      children: inspect.environmentVariables.map((variable) => ({
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
  if (inspect?.lockFile?.path) {
    launchEntries.push({
      label: 'Lock File',
      description: inspect.lockFile.status,
      tooltip: inspect.lockFile.path,
      icon: inspect.lockFile.status === 'present' ? 'lock' : 'circle-slash',
      targetPath: inspect.lockFile.path
    });
  }
  if (launchEntries.length > 0) {
    entriesByGroup.set('launch', launchEntries);
  }

  const diagnosticEntries = [
    ...(snapshot.inspectError ? [{
      label: snapshot.inspectError,
      description: 'Inspect',
      icon: 'warning'
    }] : []),
    ...(inspect?.diagnostics ?? []).map((diagnostic) => ({
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
    { kind: 'capabilities', label: 'Capabilities', icon: 'symbol-interface', tooltip: 'Feature-provided and required capabilities.' },
    { kind: 'generators', label: 'Generators', icon: 'run', tooltip: 'Active and excluded generators for the active profile.' },
    { kind: 'inputs', label: 'Inputs', icon: 'files', tooltip: 'Resolved typed inputs.' },
    { kind: 'launch', label: 'Launch', icon: 'play-circle', tooltip: 'Launch, staged file, environment, and lock metadata.' },
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
        tooltip: 'Project references, packages, features, generators, capabilities, and diagnostics.',
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
