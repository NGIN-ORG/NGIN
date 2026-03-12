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

export function buildOverviewSections(snapshot: NginWorkspaceSnapshot): OverviewSectionModel[] {
  if (!snapshot.workspace || !snapshot.context) {
    return [];
  }

  const target = createTarget(snapshot);
  const compileDescription = snapshot.activeCompileCommandsSource === 'fallback'
    ? 'Fallback'
    : snapshot.stagedCompileCommandsAvailable
      ? 'Staged'
      : 'Missing';
  const compileTooltip = snapshot.activeCompileCommandsPath
    ? `${compileDescription} compile database\n${snapshot.activeCompileCommandsPath}`
    : 'Compile database unavailable';

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
      id: 'selection',
      label: 'Selection',
      children: [
        {
          id: 'selection-project',
          label: snapshot.context.project.name,
          description: path.relative(snapshot.workspace.root, snapshot.context.project.path),
          tooltip: snapshot.context.project.path,
          command: 'ngin.selectProject',
          icon: 'project',
          arguments: []
        },
        {
          id: 'selection-variant',
          label: snapshot.context.variant.name,
          description: snapshot.context.variant.profile ?? snapshot.context.variant.environment ?? '',
          tooltip: `${snapshot.context.project.name} [${snapshot.context.variant.name}]`,
          command: 'ngin.selectVariant',
          icon: 'symbol-enum'
        },
        {
          id: 'selection-configuration',
          label: snapshot.buildConfiguration,
          description: 'Build configuration',
          tooltip: `Active NGIN build configuration: ${snapshot.buildConfiguration}\n${snapshot.outputDir ?? 'No staged output directory resolved yet.'}`,
          command: 'ngin.selectConfiguration',
          icon: 'settings-gear'
        }
      ]
    },
    {
      id: 'stage',
      label: 'Stage',
      children: [
        {
          id: 'stage-output',
          label: 'Output',
          description: snapshot.outputDir,
          tooltip: snapshot.outputDir,
          icon: 'output'
        },
        {
          id: 'stage-target',
          label: 'Target Manifest',
          description: snapshot.targetManifestExists ? 'Available' : 'Missing',
          tooltip: snapshot.targetManifestPath,
          icon: 'file-code',
          command: snapshot.targetManifestPath ? 'ngin.internal.openPath' : undefined,
          arguments: snapshot.targetManifestPath ? [snapshot.targetManifestPath] : undefined
        },
        {
          id: 'stage-compile',
          label: 'Compile Commands',
          description: compileDescription,
          tooltip: compileTooltip,
          icon: 'symbol-file',
          command: snapshot.activeCompileCommandsPath ? 'ngin.internal.openPath' : undefined,
          arguments: snapshot.activeCompileCommandsPath ? [snapshot.activeCompileCommandsPath] : undefined
        }
      ]
    },
    {
      id: 'actions',
      label: 'Actions',
      children: [
        { id: 'action-build', label: 'Build', tooltip: 'Build the selected project and variant.', command: 'ngin.build', arguments: target ? [target] : undefined, icon: 'gear' },
        { id: 'action-validate', label: 'Validate', tooltip: 'Validate the selected project and variant.', command: 'ngin.validate', arguments: target ? [target] : undefined, icon: 'check' },
        { id: 'action-run', label: 'Run', tooltip: 'Run the selected project and variant.', command: 'ngin.run', arguments: target ? [target] : undefined, icon: 'play' },
        { id: 'action-debug', label: 'Debug', tooltip: 'Debug the selected project and variant.', command: 'ngin.debug', arguments: target ? [target] : undefined, icon: 'bug' },
        { id: 'action-graph', label: 'Graph', tooltip: 'Show the resolved dependency graph.', command: 'ngin.graph', arguments: target ? [target] : undefined, icon: 'graph-line' },
        { id: 'action-last-target', label: 'Open Last Target Manifest', tooltip: 'Open the most recently built target manifest.', command: 'ngin.openLastTargetManifest', icon: 'go-to-file' }
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
