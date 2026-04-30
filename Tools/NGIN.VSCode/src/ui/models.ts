import * as path from 'node:path';
import { NginCommandTarget, NginWorkspaceSnapshot } from '../state/workspaceState';

export interface OverviewEntryModel {
  id: string;
  label: string;
  description?: string;
  tooltip?: string;
  command?: string;
  arguments?: unknown[];
  icon?: string;
  contextValue?: string;
}

export interface OverviewSectionModel {
  id: string;
  label: string;
  children: OverviewEntryModel[];
}

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

export type ProjectTreeGroupKind = 'source' | 'config' | 'dependencies' | 'generated' | 'profiles';

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

function relativeLabel(rootPath: string, targetPath?: string): string | undefined {
  if (!targetPath) {
    return undefined;
  }

  const relativePath = path.relative(rootPath, targetPath);
  return relativePath.length > 0 ? relativePath : '.';
}

function profileDescription(snapshot: NginWorkspaceSnapshot): string | undefined {
  const operatingSystem = snapshot.context?.profile.operatingSystem;
  const architecture = snapshot.context?.profile.architecture;
  const environment = snapshot.context?.profile.environment;

  if (operatingSystem && architecture && environment) {
    return `${operatingSystem}/${architecture} • ${environment}`;
  }

  if (operatingSystem && architecture) {
    return `${operatingSystem}/${architecture}`;
  }

  return environment ?? undefined;
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
      targetPath: pkg.manifestPath
    })));
  }

  if (inspect?.packageFeatures?.length) {
    entriesByGroup.set('features', inspect.packageFeatures.map((feature) => ({
      label: `${feature.package}::${feature.feature}`,
      description: inspectStateLabel(feature.state),
      tooltip: [feature.description, feature.manifestPath].filter(Boolean).join('\n'),
      icon: feature.state === 'selected' ? 'check' : feature.state === 'disabled' || feature.state === 'conditionExcluded' ? 'circle-slash' : 'symbol-property',
      targetPath: feature.manifestPath
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
      targetPath: generator.manifestPath
    })));
  }

  const inputEntries: ProjectTreeInspectEntryModel[] = [];
  for (const [kind, inputs] of Object.entries(inspect?.inputs ?? {})) {
    for (const input of inputs) {
      inputEntries.push({
        label: `${kind}: ${input.source || input.absoluteSourcePath || input.name || '(unnamed)'}`,
        description: [input.role, input.mode, input.ownerName].filter(Boolean).join(' • ') || undefined,
        tooltip: [input.absoluteSourcePath, input.stagedRelativePath ? `Stages: ${input.stagedRelativePath}` : undefined, input.manifestPath].filter(Boolean).join('\n'),
        icon: kind === 'Source' || input.role === 'Header' ? 'file-code' : kind === 'Config' ? 'settings' : 'file',
        targetPath: input.manifestPath
      });
    }
  }
  if (inputEntries.length > 0) {
    entriesByGroup.set('inputs', inputEntries);
  }

  const launchEntries: ProjectTreeInspectEntryModel[] = [];
  if (inspect?.launch?.executable) {
    launchEntries.push({
      label: inspect.launch.executable.name,
      description: [inspect.launch.executable.target, inspect.launch.executable.origin].filter(Boolean).join(' • ') || 'Executable',
      icon: 'play'
    });
  }
  if (inspect?.launch?.workingDirectory) {
    launchEntries.push({
      label: 'Working Directory',
      description: inspect.launch.workingDirectory,
      icon: 'folder-opened'
    });
  }
  for (const file of inspect?.stagedFiles ?? []) {
    launchEntries.push({
      label: file.relativeDestination ?? file.source ?? file.kind,
      description: file.kind,
      tooltip: file.source,
      icon: 'files'
    });
  }
  for (const variable of inspect?.environmentVariables ?? []) {
    launchEntries.push({
      label: variable.name,
      description: variable.secret ? '<redacted>' : variable.value,
      icon: variable.secret ? 'lock' : 'symbol-variable'
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
    { kind: 'packages', label: 'Packages', icon: 'package', tooltip: 'Resolved packages for the active profile.' },
    { kind: 'features', label: 'Features', icon: 'symbol-property', tooltip: 'Selected and available package features.' },
    { kind: 'capabilities', label: 'Capabilities', icon: 'symbol-interface', tooltip: 'Feature-provided and required capabilities.' },
    { kind: 'generators', label: 'Generators', icon: 'run', tooltip: 'Active and excluded generators for the active profile.' },
    { kind: 'inputs', label: 'Inputs', icon: 'files', tooltip: 'Resolved typed inputs.' },
    { kind: 'launch', label: 'Launch', icon: 'play-circle', tooltip: 'Launch, staged file, environment, and lock metadata.' },
    { kind: 'diagnostics', label: 'Diagnostics', icon: 'warning', tooltip: 'Inspector diagnostics.' }
  ];

  const groups = metadata
    .filter((group) => (entriesByGroup.get(group.kind)?.length ?? 0) > 0)
    .map((group) => ({
      ...group,
      projectPath
    }));

  return { groups, entriesByGroup };
}

function artifactStatusIcon(status: 'ready' | 'fallback' | 'missing'): string {
  if (status === 'ready') {
    return 'pass-filled';
  }

  if (status === 'fallback') {
    return 'warning';
  }

  return 'circle-slash';
}

export function buildOverviewSections(snapshot: NginWorkspaceSnapshot): OverviewSectionModel[] {
  if (!snapshot.workspace || !snapshot.context) {
    return [];
  }

  const target = createTarget(snapshot);
  const compileStatus = snapshot.activeCompileCommandsSource === 'fallback'
    ? 'Workspace fallback'
    : snapshot.stagedCompileCommandsAvailable
      ? 'Staged'
      : 'Not generated';
  const compileStatusIcon = snapshot.activeCompileCommandsSource === 'fallback'
    ? artifactStatusIcon('fallback')
    : snapshot.stagedCompileCommandsAvailable
      ? artifactStatusIcon('ready')
      : artifactStatusIcon('missing');
  const compileTooltip = snapshot.activeCompileCommandsPath
    ? `${compileStatus} compile commands\n${snapshot.activeCompileCommandsPath}`
    : 'Compile commands are not available yet. Run Build to generate them.';
  const launchManifestStatus = snapshot.launchManifestExists ? 'Ready' : 'Not generated';
  const launchManifestTooltip = snapshot.launchManifestExists && snapshot.launchManifestPath
    ? `Generated launch manifest\n${snapshot.launchManifestPath}`
    : 'Launch manifest is not available yet. Run Build to generate it.';
  const outputTooltip = snapshot.outputDir ?? 'Output folder has not been resolved yet.';
  const contextDescription = profileDescription(snapshot);

  return [
    {
      id: 'workspace',
      label: 'Workspace',
      children: [
        {
          id: 'workspace-name',
          label: snapshot.workspace.workspace.name,
          description: path.basename(snapshot.workspace.root),
          tooltip: `${snapshot.workspace.workspace.name}\n${snapshot.workspace.root}`,
          icon: 'folder-library'
        },
        {
          id: 'workspace-root',
          label: 'Root',
          description: snapshot.workspace.root,
          tooltip: snapshot.workspace.root,
          icon: 'folder-opened'
        }
      ]
    },
    {
      id: 'context',
      label: 'Current Context',
      children: [
        {
          id: 'context-project',
          label: `Project: ${snapshot.context.project.name}`,
          description: path.relative(snapshot.workspace.root, snapshot.context.project.path),
          tooltip: snapshot.context.project.path,
          command: 'ngin.selectProject',
          icon: 'project',
          arguments: []
        },
        {
          id: 'context-profile',
          label: `Profile: ${snapshot.context.profile.name}`,
          description: contextDescription,
          tooltip: `${snapshot.context.project.name} [${snapshot.context.profile.name}]`,
          command: 'ngin.selectProfile',
          icon: 'symbol-enum'
        }
      ]
    },
    {
      id: 'artifacts',
      label: 'Build Artifacts',
      children: [
        {
          id: 'artifacts-output',
          label: 'Output Folder',
          description: relativeLabel(snapshot.workspace.root, snapshot.outputDir),
          tooltip: outputTooltip,
          icon: 'folder-opened',
          command: snapshot.outputDir ? 'ngin.internal.revealPath' : undefined,
          arguments: snapshot.outputDir ? [snapshot.outputDir] : undefined
        },
        {
          id: 'artifacts-launch',
          label: 'Launch Manifest',
          description: launchManifestStatus,
          tooltip: launchManifestTooltip,
          icon: snapshot.launchManifestExists ? artifactStatusIcon('ready') : artifactStatusIcon('missing'),
          command: snapshot.launchManifestExists && snapshot.launchManifestPath ? 'ngin.internal.openPath' : undefined,
          arguments: snapshot.launchManifestExists && snapshot.launchManifestPath ? [snapshot.launchManifestPath] : undefined
        },
        {
          id: 'artifacts-compile',
          label: 'Compile Commands',
          description: compileStatus,
          tooltip: compileTooltip,
          icon: compileStatusIcon,
          command: snapshot.activeCompileCommandsSource && snapshot.activeCompileCommandsPath ? 'ngin.internal.openPath' : undefined,
          arguments: snapshot.activeCompileCommandsSource && snapshot.activeCompileCommandsPath ? [snapshot.activeCompileCommandsPath] : undefined
        }
      ]
    },
    {
      id: 'actions',
      label: 'Actions',
      children: [
        { id: 'action-build', label: 'Build', tooltip: 'Build the selected project and profile.', command: 'ngin.build', arguments: target ? [target] : undefined, icon: 'gear' },
        { id: 'action-configure', label: 'Configure', tooltip: 'Generate backend build metadata for the selected project and profile.', command: 'ngin.configure', arguments: target ? [target] : undefined, icon: 'settings' },
        { id: 'action-rebuild', label: 'Rebuild', tooltip: 'Clean and rebuild the selected project and profile.', command: 'ngin.rebuild', arguments: target ? [target] : undefined, icon: 'tools' },
        { id: 'action-clean', label: 'Clean', tooltip: 'Remove NGIN-owned generated artifacts for the selected project and profile.', command: 'ngin.clean', arguments: target ? [target] : undefined, icon: 'trash' },
        { id: 'action-run', label: 'Run', tooltip: 'Run the selected project and profile.', command: 'ngin.run', arguments: target ? [target] : undefined, icon: 'play' },
        { id: 'action-debug', label: 'Debug', tooltip: 'Debug the selected project and profile.', command: 'ngin.debug', arguments: target ? [target] : undefined, icon: 'bug' },
        { id: 'action-validate', label: 'Validate', tooltip: 'Validate the selected project and profile.', command: 'ngin.validate', arguments: target ? [target] : undefined, icon: 'check' }
      ]
    },
    {
      id: 'more',
      label: 'More',
      children: [
        { id: 'action-graph', label: 'Graph', tooltip: 'Show the resolved dependency graph.', command: 'ngin.graph', arguments: target ? [target] : undefined, icon: 'graph-line' },
        { id: 'action-last-launch', label: 'Open Last Launch Manifest', tooltip: 'Open the most recently built launch manifest.', command: 'ngin.openLastLaunchManifest', icon: 'go-to-file' }
      ]
    }
  ];
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

    if (project.sourceRoots.length > 0 || project.buildSources.length > 0) {
      children.push({
        kind: 'group',
        id: `${project.path}:source`,
        label: 'Source',
        tooltip: 'Declared source roots and explicit build sources.',
        icon: 'folder-library',
        projectPath: project.path,
        group: 'source'
      });
    }

    const hasConfigInputs = project.configInputs.length > 0
      || project.profiles.some((profile) => profile.configInputs.length > 0);
    if (hasConfigInputs) {
      children.push({
        kind: 'group',
        id: `${project.path}:config`,
        label: 'Config',
        tooltip: 'Declared root and profile config inputs.',
        icon: 'settings',
        projectPath: project.path,
        group: 'config'
      });
    }

    const dependencies = buildProjectDependencies(snapshot, project.path);
    dependenciesByProject.set(project.path, dependencies);
    const inspectModel = buildInspectTreeModel(snapshot, project.path);
    if (inspectModel) {
      inspectByProject.set(project.path, inspectModel);
    }
    if (dependencies.projects.length > 0 || dependencies.packages.length > 0 || (inspectModel?.groups.length ?? 0) > 0) {
      children.push({
        kind: 'group',
        id: `${project.path}:dependencies`,
        label: 'Dependencies',
        tooltip: selectedProject ? 'Resolved package, feature, capability, generator, input, and launch state.' : 'Referenced projects and packages.',
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
