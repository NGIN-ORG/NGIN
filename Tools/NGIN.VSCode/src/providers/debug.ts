import { promises as fs } from 'node:fs';
import * as path from 'node:path';
import * as vscode from 'vscode';
import type { NginController } from '../core/controller';
import { createNativeDebugConfiguration, createNativeTestDebugConfiguration } from '../core/debugConfiguration';
import { stagedExecutablePath } from '../core/paths';
import type { ProjectCandidate } from '../model';
import type { SourceAnalysisProvider } from './sourceAnalysis';

interface NginDebugConfiguration extends vscode.DebugConfiguration {
  project?: string;
  configuration?: string;
  target?: string;
  toolchain?: string;
  args?: string[];
  launch?: string;
  preStage?: boolean;
  test?: boolean;
}

async function fileExists(candidate: string): Promise<boolean> {
  try {
    return (await fs.stat(candidate)).isFile();
  } catch {
    return false;
  }
}

export class NginDebugProvider implements vscode.DebugConfigurationProvider {
  constructor(
    private readonly controller: NginController,
    private readonly sourceAnalysis: SourceAnalysisProvider
  ) {}

  provideDebugConfigurations(): vscode.ProviderResult<vscode.DebugConfiguration[]> {
    return [{ type: 'ngin', request: 'launch', name: 'NGIN: Debug Current Project', preStage: true }];
  }

  async selectLaunch(project = this.controller.activeProject): Promise<string | undefined> {
    if (!project) throw new Error('No NGIN project is available.');
    const context = this.sourceAnalysis.contextForProject(project);
    const graph = await this.controller.graphForContext(context, true);
    if (!graph?.launches.length) {
      void vscode.window.showInformationMessage(`${project.name} declares no Launch configurations.`);
      return undefined;
    }
    const remembered = context.launch;
    const selected = await vscode.window.showQuickPick(
      graph.launches.map(launch => {
        const identity = launch.name ?? launch.identity;
        return {
          label: identity,
          description: identity === remembered ? 'selected' : launch.default ? 'default' : undefined,
          detail: launch.executable ? `Executable: ${launch.executable}` : undefined,
          identity
        };
      }),
      { title: `Select Launch for ${graph.product.name}`, placeHolder: remembered, matchOnDetail: true }
    );
    if (!selected) return undefined;
    await this.controller.updateProjectSelection(project, { launch: selected.identity, preset: undefined });
    return selected.identity;
  }

  async resolveDebugConfiguration(
    _folder: vscode.WorkspaceFolder | undefined,
    configuration: NginDebugConfiguration
  ): Promise<vscode.DebugConfiguration | undefined> {
    const activeFile = vscode.window.activeTextEditor?.document.uri.fsPath;
    const activeOwner = activeFile ? await this.sourceAnalysis.projectForFile(activeFile, true) : undefined;
    const base = this.controller.snapshot.context ?? (this.controller.activeProject
      ? this.sourceAnalysis.contextForProject(this.controller.activeProject) : undefined);
    const requested = configuration.project && base
      ? path.resolve(base.workspaceFolder, configuration.project)
      : configuration.project ? path.resolve(configuration.project) : undefined;
    const project = requested
      ? this.controller.projects.find(candidate => path.resolve(candidate.manifest) === requested)
      : activeOwner ?? this.controller.activeProject;
    if (!project) throw new Error(configuration.project
      ? `NGIN debug project is not discovered: ${configuration.project}`
      : 'No NGIN project owns the active source file and no project is selected.');
    const context = this.controller.contextForProject(project, {
      configuration: configuration.configuration,
      target: configuration.target,
      toolchain: configuration.toolchain
    });

    let graph = await this.controller.graphForContext(context, true);
    if (!graph) return undefined;
    const hostOperatingSystem = process.platform === 'win32' ? 'windows' : process.platform === 'darwin' ? 'macos' : 'linux';
    if (graph.selection.targetOperatingSystem !== hostOperatingSystem) {
      throw new Error(`Local NGIN debugging requires a ${hostOperatingSystem} target; the active graph targets ${graph.selection.targetOperatingSystem}.`);
    }
    if (configuration.preStage !== false) {
      const staged = await this.controller.execute('stage', [], false, context);
      if (!staged) return undefined;
    }

    if (!configuration.test && !configuration.launch && context.launch
      && graph.launches.some(launch => (launch.name ?? launch.identity) === context.launch)) {
      configuration.launch = context.launch;
    }
    if (!configuration.test && !configuration.launch && graph.launches.length > 1) {
      const remembered = context.launch;
      configuration.launch = graph.launches.find(launch => (launch.name ?? launch.identity) === remembered)
        ? remembered
        : await vscode.window.showQuickPick(
          graph.launches.map(launch => ({ label: launch.name ?? launch.identity, description: launch.default ? 'default' : undefined })),
          { title: `Select a Launch for ${graph.product.name}` }
        ).then(value => value?.label);
      if (!configuration.launch) return undefined;
      await this.controller.updateProjectSelection(project, { launch: configuration.launch, preset: undefined });
    }
    const program = stagedExecutablePath(context, graph.product.name, graph.selection.targetOperatingSystem);
    if (!await fileExists(program)) {
      throw new Error(`The staged executable was not found at ${program}. Run NGIN: Stage and inspect the NGIN output.`);
    }
    const configuredDebugger = vscode.workspace.getConfiguration('ngin').get<string>('debug.miDebuggerPath', '').trim();
    if (configuration.test) {
      return createNativeTestDebugConfiguration(
        graph, context, program, configuration, process.platform, path.delimiter, configuredDebugger);
    }
    return createNativeDebugConfiguration(graph, context, program, configuration, process.platform, path.delimiter, configuredDebugger);
  }
}
