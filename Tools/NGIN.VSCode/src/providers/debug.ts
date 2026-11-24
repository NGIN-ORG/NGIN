import * as path from 'node:path';
import * as vscode from 'vscode';
import type { NginController } from '../core/controller';
import { createNativeDebugConfiguration } from '../core/debugConfiguration';
import type { ProjectCandidate } from '../model';
import type { SourceAnalysisProvider } from './sourceAnalysis';

interface NginDebugConfiguration extends vscode.DebugConfiguration {
  project?: string;
  configuration?: string;
  target?: string;
  toolchain?: string;
  args?: string[];
  run?: string;
  preStage?: boolean;
  test?: boolean;
  testName?: string;
}

export class NginDebugProvider implements vscode.DebugConfigurationProvider {
  constructor(
    private readonly controller: NginController,
    private readonly sourceAnalysis: SourceAnalysisProvider
  ) {}

  provideDebugConfigurations(): vscode.ProviderResult<vscode.DebugConfiguration[]> {
    return [{ type: 'ngin', request: 'launch', name: 'NGIN: Debug Current Project', preStage: true }];
  }

  async selectRun(project = this.controller.launchProduct): Promise<string | undefined> {
    if (!project) throw new Error('No NGIN project is available.');
    const context = this.sourceAnalysis.contextForProject(project);
    const graph = await this.controller.graphForContext(context, true);
    if (!graph?.runs.length) {
      void vscode.window.showInformationMessage(`${project.name} declares no Run configurations.`);
      return undefined;
    }
    const remembered = context.run;
    const selected = await vscode.window.showQuickPick(
      graph.runs.map(run => {
        const identity = run.name ?? run.identity;
        return {
          label: identity,
          description: identity === remembered ? 'selected' : run.default ? 'default' : undefined,
          detail: run.executable ? `Executable: ${run.executable}` : undefined,
          identity
        };
      }),
      { title: `Select Run for ${graph.product.name}`, placeHolder: remembered, matchOnDetail: true }
    );
    if (!selected) return undefined;
    await this.controller.updateProjectSelection(project, { run: selected.identity, profile: undefined });
    return selected.identity;
  }

  async resolveDebugConfiguration(
    _folder: vscode.WorkspaceFolder | undefined,
    configuration: NginDebugConfiguration
  ): Promise<vscode.DebugConfiguration | undefined> {
    const base = this.controller.snapshot.context ?? (this.controller.launchProduct
      ? this.sourceAnalysis.contextForProject(this.controller.launchProduct) : undefined);
    const requested = configuration.project && base
      ? path.resolve(base.workspaceFolder, configuration.project)
      : configuration.project ? path.resolve(configuration.project) : undefined;
    const project = requested
      ? this.controller.projects.find(candidate => path.resolve(candidate.manifest) === requested)
      : this.controller.launchProduct;
    if (!project) throw new Error(configuration.project
      ? `NGIN debug project is not discovered: ${configuration.project}`
      : 'Select an NGIN project to debug.');
    const context = this.controller.contextForProject(project, {
      configuration: configuration.configuration,
      target: configuration.target,
      toolchain: configuration.toolchain
    });

    const graph = await this.controller.graphForContext(context, true);
    if (!graph) return undefined;
    const hostOperatingSystem = process.platform === 'win32' ? 'windows' : process.platform === 'darwin' ? 'macos' : 'linux';
    if (graph.selection.targetOperatingSystem !== hostOperatingSystem) {
      throw new Error(`Local NGIN debugging requires a ${hostOperatingSystem} target; the active graph targets ${graph.selection.targetOperatingSystem}.`);
    }
    let run = configuration.run ?? context.run;
    if (!configuration.test && !run && graph.runs.length > 1 && !graph.runs.some(candidate => candidate.default)) {
      run = await this.selectRun(project);
      if (!run) return undefined;
    }
    const command = configuration.test ? 'test' : 'run';
    const extra = configuration.testName ? [configuration.testName] : [];
    if (configuration.args?.length) extra.push('--', ...configuration.args);
    const plan = await this.controller.prepareLaunch(
      { ...context, run }, command, extra, configuration.preStage === false);
    const configuredDebugger = vscode.workspace.getConfiguration('ngin').get<string>('debug.miDebuggerPath', '').trim();
    return { ...configuration, ...createNativeDebugConfiguration(plan, configuration, process.platform, configuredDebugger) };
  }
}
