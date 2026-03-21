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

export interface ProjectTreeVariantModel {
  label: string;
  description?: string;
  tooltip?: string;
  projectPath: string;
  variantName: string;
  selected: boolean;
}

export interface ProjectTreeModels {
  workspaceLabel?: string;
  workspaceDescription?: string;
  projects: ProjectTreeProjectModel[];
  variantsByProject: Map<string, ProjectTreeVariantModel[]>;
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
  variant?: StatusBarEntryModel;
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
    variantName: snapshot.context.variant.name
  };
}

function relativeLabel(rootPath: string, targetPath?: string): string | undefined {
  if (!targetPath) {
    return undefined;
  }

  const relativePath = path.relative(rootPath, targetPath);
  return relativePath.length > 0 ? relativePath : '.';
}

function variantDescription(snapshot: NginWorkspaceSnapshot): string | undefined {
  const profile = snapshot.context?.variant.profile;
  const environment = snapshot.context?.variant.environment;

  if (profile && environment) {
    return `${profile} • ${environment}`;
  }

  return profile ?? environment ?? undefined;
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
  const targetManifestStatus = snapshot.targetManifestExists ? 'Ready' : 'Not generated';
  const targetManifestTooltip = snapshot.targetManifestExists && snapshot.targetManifestPath
    ? `Staged target manifest\n${snapshot.targetManifestPath}`
    : 'Target manifest is not available yet. Run Build to generate it.';
  const outputTooltip = snapshot.outputDir ?? 'Output folder has not been resolved yet.';
  const contextDescription = variantDescription(snapshot);

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
          id: 'context-variant',
          label: `Variant: ${snapshot.context.variant.name}`,
          description: contextDescription,
          tooltip: `${snapshot.context.project.name} [${snapshot.context.variant.name}]`,
          command: 'ngin.selectVariant',
          icon: 'symbol-enum'
        },
        {
          id: 'context-configuration',
          label: `Configuration: ${snapshot.buildConfiguration}`,
          tooltip: `Active NGIN build configuration: ${snapshot.buildConfiguration}\n${snapshot.outputDir ?? 'No staged output directory resolved yet.'}`,
          command: 'ngin.selectConfiguration',
          icon: 'settings-gear'
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
          id: 'artifacts-target',
          label: 'Target Manifest',
          description: targetManifestStatus,
          tooltip: targetManifestTooltip,
          icon: snapshot.targetManifestExists ? artifactStatusIcon('ready') : artifactStatusIcon('missing'),
          command: snapshot.targetManifestExists && snapshot.targetManifestPath ? 'ngin.internal.openPath' : undefined,
          arguments: snapshot.targetManifestExists && snapshot.targetManifestPath ? [snapshot.targetManifestPath] : undefined
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
        { id: 'action-build', label: 'Build', tooltip: 'Build the selected project and variant.', command: 'ngin.build', arguments: target ? [target] : undefined, icon: 'gear' },
        { id: 'action-run', label: 'Run', tooltip: 'Run the selected project and variant.', command: 'ngin.run', arguments: target ? [target] : undefined, icon: 'play' },
        { id: 'action-debug', label: 'Debug', tooltip: 'Debug the selected project and variant.', command: 'ngin.debug', arguments: target ? [target] : undefined, icon: 'bug' },
        { id: 'action-validate', label: 'Validate', tooltip: 'Validate the selected project and variant.', command: 'ngin.validate', arguments: target ? [target] : undefined, icon: 'check' }
      ]
    },
    {
      id: 'more',
      label: 'More',
      children: [
        { id: 'action-graph', label: 'Graph', tooltip: 'Show the resolved dependency graph.', command: 'ngin.graph', arguments: target ? [target] : undefined, icon: 'graph-line' },
        { id: 'action-last-target', label: 'Open Last Build Manifest', tooltip: 'Open the most recently built target manifest.', command: 'ngin.openLastTargetManifest', icon: 'go-to-file' }
      ]
    }
  ];
}

export function buildProjectTreeModels(snapshot: NginWorkspaceSnapshot): ProjectTreeModels {
  const projects: ProjectTreeProjectModel[] = [];
  const variantsByProject = new Map<string, ProjectTreeVariantModel[]>();

  if (!snapshot.workspace) {
    return { projects, variantsByProject };
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

    variantsByProject.set(project.path, project.variants.map((variant) => ({
      label: variant.name,
      description: snapshot.context?.project.path === project.path && snapshot.context.variant.name === variant.name
        ? 'Current'
        : variant.profile ?? variant.environment ?? '',
      tooltip: `${project.name} [${variant.name}]`,
      projectPath: project.path,
      variantName: variant.name,
      selected: snapshot.context?.project.path === project.path && snapshot.context.variant.name === variant.name
    })));
  }

  return {
    workspaceLabel: snapshot.workspace.workspace.name,
    workspaceDescription: snapshot.workspace.root,
    projects,
    variantsByProject
  };
}

export function buildStatusBarModel(snapshot: NginWorkspaceSnapshot): StatusBarModel {
  if (!snapshot.workspace || !snapshot.context) {
    return { visible: false };
  }

  const target = createTarget(snapshot);
  const selectionLabel = `${snapshot.context.project.name} [${snapshot.context.variant.name}]`;
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
      command: 'ngin.selectProject'
    },
    variant: {
      text: `$(symbol-enum) ${snapshot.context.variant.name}`,
      tooltip: `${selectionLabel}\nProfile: ${snapshot.context.variant.profile ?? 'n/a'}`,
      command: 'ngin.selectVariant'
    },
    configuration: {
      text: `$(settings-gear) ${snapshot.buildConfiguration}`,
      tooltip: `Active NGIN build configuration: ${snapshot.buildConfiguration}\n${snapshot.outputDir ?? 'No staged output directory resolved yet.'}`,
      command: 'ngin.selectConfiguration'
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
