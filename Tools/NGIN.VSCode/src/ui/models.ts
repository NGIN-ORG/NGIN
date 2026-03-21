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
  label: string;
  description?: string;
  tooltip?: string;
  projectPath: string;
  selected: boolean;
}

export interface ProjectTreeConfigurationModel {
  label: string;
  description?: string;
  tooltip?: string;
  projectPath: string;
  configurationName: string;
  selected: boolean;
}

export interface ProjectTreeModels {
  workspaceLabel?: string;
  workspaceDescription?: string;
  projects: ProjectTreeProjectModel[];
  configurationsByProject: Map<string, ProjectTreeConfigurationModel[]>;
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
  configuration?: StatusBarEntryModel;
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
    configurationName: snapshot.context.configuration.name
  };
}

function relativeLabel(rootPath: string, targetPath?: string): string | undefined {
  if (!targetPath) {
    return undefined;
  }

  const relativePath = path.relative(rootPath, targetPath);
  return relativePath.length > 0 ? relativePath : '.';
}

function configurationDescription(snapshot: NginWorkspaceSnapshot): string | undefined {
  const hostProfile = snapshot.context?.configuration.hostProfile;
  const environment = snapshot.context?.configuration.environment;

  if (hostProfile && environment) {
    return `${hostProfile} • ${environment}`;
  }

  return hostProfile ?? environment ?? undefined;
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
  const contextDescription = configurationDescription(snapshot);

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
          id: 'context-configuration',
          label: `Configuration: ${snapshot.context.configuration.name}`,
          description: contextDescription,
          tooltip: `${snapshot.context.project.name} [${snapshot.context.configuration.name}]`,
          command: 'ngin.selectConfiguration',
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
        { id: 'action-build', label: 'Build', tooltip: 'Build the selected project and configuration.', command: 'ngin.build', arguments: target ? [target] : undefined, icon: 'gear' },
        { id: 'action-run', label: 'Run', tooltip: 'Run the selected project and configuration.', command: 'ngin.run', arguments: target ? [target] : undefined, icon: 'play' },
        { id: 'action-debug', label: 'Debug', tooltip: 'Debug the selected project and configuration.', command: 'ngin.debug', arguments: target ? [target] : undefined, icon: 'bug' },
        { id: 'action-validate', label: 'Validate', tooltip: 'Validate the selected project and configuration.', command: 'ngin.validate', arguments: target ? [target] : undefined, icon: 'check' }
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
  const configurationsByProject = new Map<string, ProjectTreeConfigurationModel[]>();

  if (!snapshot.workspace) {
    return { projects, configurationsByProject };
  }

  for (const project of snapshot.workspace.projects) {
    const selectedProject = snapshot.context?.project.path === project.path;
    projects.push({
      label: project.name,
      description: selectedProject ? 'Selected' : path.relative(snapshot.workspace.root, project.path),
      tooltip: project.path,
      projectPath: project.path,
      selected: selectedProject
    });

    configurationsByProject.set(project.path, project.configurations.map((configuration) => ({
      label: configuration.name,
      description: snapshot.context?.project.path === project.path && snapshot.context.configuration.name === configuration.name
        ? 'Current'
        : configuration.hostProfile ?? configuration.environment ?? '',
      tooltip: `${project.name} [${configuration.name}]`,
      projectPath: project.path,
      configurationName: configuration.name,
      selected: snapshot.context?.project.path === project.path && snapshot.context.configuration.name === configuration.name
    })));
  }

  return {
    workspaceLabel: snapshot.workspace.workspace.name,
    workspaceDescription: snapshot.workspace.root,
    projects,
    configurationsByProject
  };
}

export function buildStatusBarModel(snapshot: NginWorkspaceSnapshot): StatusBarModel {
  if (!snapshot.workspace || !snapshot.context) {
    return { visible: false };
  }

  const target = createTarget(snapshot);
  const selectionLabel = `${snapshot.context.project.name} [${snapshot.context.configuration.name}]`;
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
    configuration: {
      text: `$(symbol-enum) ${snapshot.context.configuration.name}`,
      tooltip: `${selectionLabel}\nHost Profile: ${snapshot.context.configuration.hostProfile ?? 'n/a'}`,
      command: 'ngin.internal.pickConfiguration'
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
