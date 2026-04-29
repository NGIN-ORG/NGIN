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

export type ProjectTreeChildModel = ProjectTreeManifestModel | ProjectTreeGroupModel | ProjectTreeProfileModel;

export interface ProjectTreeModels {
  workspaceLabel?: string;
  workspaceDescription?: string;
  projects: ProjectTreeProjectModel[];
  childrenByProject: Map<string, ProjectTreeChildModel[]>;
  profilesByProject: Map<string, ProjectTreeProfileModel[]>;
  dependenciesByProject: Map<string, ProjectTreeDependenciesModel>;
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

  if (!snapshot.workspace) {
    return { projects, childrenByProject, profilesByProject, dependenciesByProject };
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
    if (dependencies.projects.length > 0 || dependencies.packages.length > 0) {
      children.push({
        kind: 'group',
        id: `${project.path}:dependencies`,
        label: 'Dependencies',
        tooltip: 'Referenced projects and packages.',
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
    dependenciesByProject
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
