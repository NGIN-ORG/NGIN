import * as path from 'node:path';
import type { NginContext, ProjectCandidate } from '../model';

export type ProjectActionGroup = 'Lifecycle' | 'Build context' | 'Launch' | 'Project' | 'Diagnostics' | 'Advanced';

export interface ProjectActionDescriptor {
  group: ProjectActionGroup;
  label: string;
  description?: string;
  detail?: string;
  icon?: string;
  command: string;
  argument?: unknown;
}

export interface ProjectActionState {
  project: ProjectCandidate;
  context: NginContext;
  canRun: boolean;
  canTest: boolean;
  canBenchmark: boolean;
  hasAnalyze: boolean;
  hasFormat: boolean;
  graphReady: boolean;
  canPublish: boolean;
  configurationChoices: number;
  targetChoices: number;
  toolchainChoices: number;
  selectedRun?: string;
  busy?: string;
  graphError?: string;
  lastOperation?: { command: string; state: 'succeeded' | 'failed' };
  trusted?: boolean;
}

export const projectActionGroupOrder: ProjectActionGroup[] = ['Lifecycle', 'Build context', 'Launch', 'Project', 'Diagnostics', 'Advanced'];

export function projectActionDescriptors(state: ProjectActionState): ProjectActionDescriptor[] {
  const project = state.project;
  const result: ProjectActionDescriptor[] = [];
  if (state.busy) {
    result.push(
      { group: 'Lifecycle', label: 'Cancel active operation', description: state.busy, icon: 'debug-stop', command: 'ngin.cancel' },
      { group: 'Diagnostics', label: 'Show NGIN Output', description: 'Follow operation details', icon: 'output', command: 'ngin.showOutput' }
    );
    return result;
  }

  if (project.projectSystem === 'CMake') {
    if (state.trusted !== false) {
      result.push(
        { group: 'Lifecycle', label: 'Configure Project', description: state.context.configurePreset ?? 'Select a configure preset', icon: 'gear', command: 'ngin.configure', argument: project },
        { group: 'Lifecycle', label: 'Build Project', description: project.name, icon: 'tools', command: 'ngin.build', argument: project }
      );
      if (project.capabilities?.includes('Test')) result.push(
        { group: 'Lifecycle', label: 'Test Project', description: 'Run tests through CTest', icon: 'beaker', command: 'ngin.test', argument: project }
      );
    } else {
      result.push({ group: 'Diagnostics', label: 'Trust Workspace to Run CMake', description: 'Configure, Build, and Test are disabled', icon: 'shield', command: 'workbench.trust.manage' });
    }
    result.push(
      { group: 'Build context', label: 'Select Configure Preset', description: state.context.configurePreset, icon: 'settings-gear', command: 'ngin.selectConfigurePreset', argument: project },
      { group: 'Build context', label: 'Select Configuration', description: state.context.configuration, icon: 'symbol-parameter', command: 'ngin.selectConfiguration', argument: project },
      { group: 'Project', label: 'Select Active Project', description: 'Workspace-scoped fallback project', icon: 'pass-filled', command: 'ngin.setLaunchProduct', argument: project },
      { group: 'Project', label: 'Open Project Root', description: project.directory, icon: 'folder-opened', command: 'ngin.openCMakeProject', argument: project },
      { group: 'Diagnostics', label: 'Show NGIN Output', icon: 'output', command: 'ngin.showOutput' },
      { group: 'Diagnostics', label: 'Check Setup', icon: 'pulse', command: 'ngin.checkSetup', argument: project },
      { group: 'Advanced', label: 'Inspect CMake Project', icon: 'inspect', command: 'ngin.inspect', argument: project },
      { group: 'Advanced', label: 'Refresh CMake Model', icon: 'refresh', command: 'ngin.refreshCMakeProject', argument: project }
    );
    return result;
  }

  result.push({ group: 'Lifecycle', label: 'Build Project', description: project.name, icon: 'tools', command: 'ngin.build', argument: project });
  if (state.canRun) {
    result.push(
      { group: 'Lifecycle', label: 'Run Project', description: project.name, icon: 'play', command: 'ngin.run', argument: project },
      { group: 'Lifecycle', label: 'Debug Project', description: project.name, icon: 'debug-alt', command: 'ngin.debug', argument: project }
    );
  }
  if (state.canTest) {
    result.push({ group: 'Lifecycle', label: 'Test Project', description: project.name, icon: 'beaker', command: 'ngin.test', argument: project });
  }
  if (state.canBenchmark) {
    result.push({ group: 'Lifecycle', label: 'Benchmark Project', description: project.name, icon: 'dashboard', command: 'ngin.benchmark', argument: project });
  }

  if (state.configurationChoices) result.push(
    { group: 'Build context', label: 'Select Configuration', description: state.context.configuration, icon: 'symbol-parameter', command: 'ngin.selectConfiguration', argument: project }
  );
  if (state.targetChoices) result.push(
    { group: 'Build context', label: 'Select Target', description: state.context.target, icon: 'device-desktop', command: 'ngin.selectTarget', argument: project }
  );
  if (state.toolchainChoices) result.push(
    { group: 'Build context', label: 'Select Toolchain', description: state.context.toolchain, icon: 'tools', command: 'ngin.selectToolchain', argument: project }
  );
  if (state.canRun) {
    result.push({
      group: 'Build context', label: 'Select Run', description: state.selectedRun || 'Choose the Run and Debug configuration',
      icon: 'run', command: 'ngin.selectRun', argument: project
    });
  }

  result.push(
    { group: 'Project', label: 'Open Manifest', description: path.basename(project.manifest), icon: 'file-code', command: 'ngin.openManifest', argument: project },
    { group: 'Project', label: 'Open Output Folder', description: 'Current build context', detail: state.context.outputDirectory, icon: 'folder-opened', command: 'ngin.openOutputDirectory', argument: project },
    { group: 'Project', label: 'Select Active Project', description: 'Default for build, F5, and Ctrl+F5', icon: 'pass-filled', command: 'ngin.setLaunchProduct', argument: project },
    { group: 'Project', label: 'Add Package', description: 'Add a semantic package dependency', icon: 'package', command: 'ngin.addPackage', argument: project },
    { group: 'Project', label: 'New C++ Source File', description: 'Create and include a source file', icon: 'new-file', command: 'ngin.newSourceFile', argument: project },
    { group: 'Project', label: 'New C++ Header File', description: 'Create and include a header', icon: 'new-file', command: 'ngin.newHeaderFile', argument: project }
  );

  result.push(
    { group: 'Diagnostics', label: 'Open Problems', description: state.graphError ? 'Project model issue' : undefined, icon: 'warning', command: 'workbench.actions.view.problems' },
    { group: 'Diagnostics', label: 'Show NGIN Output', description: state.lastOperation ? `${state.lastOperation.command} ${state.lastOperation.state}` : 'Detailed command output', icon: 'output', command: 'ngin.showOutput' },
    { group: 'Diagnostics', label: 'Check Setup', description: 'CLI path, version, workspace, and project readiness', icon: 'pulse', command: 'ngin.checkSetup', argument: project }
  );

  result.push(
    { group: 'Advanced', label: 'Configure Project', description: 'Force regeneration for troubleshooting', icon: 'gear', command: 'ngin.configure', argument: project },
    { group: 'Advanced', label: 'Rebuild Project', description: 'Clean the output and build again', icon: 'sync', command: 'ngin.rebuild', argument: project },
    { group: 'Advanced', label: 'Clean Output', description: 'Move this build context to the trash', icon: 'trash', command: 'ngin.clean', argument: project },
    { group: 'Advanced', label: 'Stage Project', description: 'Prepare the runtime layout', icon: 'package', command: 'ngin.stage', argument: project },
    { group: 'Advanced', label: 'Restore Packages', icon: 'cloud-download', command: 'ngin.restore', argument: project },
    { group: 'Advanced', label: 'Lock Dependencies', icon: 'lock', command: 'ngin.lock', argument: project },
    { group: 'Advanced', label: 'Inspect Project', icon: 'inspect', command: 'ngin.inspect', argument: project }
  );
  if (state.graphReady) result.push(
    { group: 'Advanced', label: 'Open Resolved Project JSON', description: 'Resolved Composition Graph', icon: 'json', command: 'ngin.showGraph', argument: project },
    { group: 'Advanced', label: 'Explain Composition Identity', icon: 'question', command: 'ngin.explain', argument: project },
    { group: 'Advanced', label: 'Diff Composition', icon: 'diff', command: 'ngin.diff', argument: project }
  );
  if (state.canPublish) result.push(
    { group: 'Advanced', label: 'Publish Project', icon: 'package', command: 'ngin.publish', argument: project }
  );
  if (state.hasAnalyze) {
    result.push({ group: 'Advanced', label: 'Analyze Project', icon: 'search', command: 'ngin.analyze', argument: project });
  }
  if (state.hasFormat) {
    result.push({ group: 'Advanced', label: 'Format Project', icon: 'wand', command: 'ngin.formatSources', argument: project });
  }
  return result;
}
