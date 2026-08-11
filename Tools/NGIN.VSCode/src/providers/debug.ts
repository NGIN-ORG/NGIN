import { promises as fs } from 'node:fs';
import * as path from 'node:path';
import * as vscode from 'vscode';
import type { NginController } from '../core/controller';
import { createNativeDebugConfiguration, createNativeTestDebugConfiguration } from '../core/debugConfiguration';
import { stagedExecutablePath } from '../core/paths';
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
    private readonly sourceAnalysis: SourceAnalysisProvider,
    private readonly extensionContext: vscode.ExtensionContext
  ) {}

  provideDebugConfigurations(): vscode.ProviderResult<vscode.DebugConfiguration[]> {
    return [{ type: 'ngin', request: 'launch', name: 'NGIN: Debug Current Project', preStage: true }];
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

    if (!configuration.test && !configuration.launch && graph.launches.length > 1) {
      const remembered = this.extensionContext.workspaceState.get<Record<string, string>>('ngin.debugLaunches', {})[project.manifest];
      configuration.launch = graph.launches.find(launch => (launch.name ?? launch.identity) === remembered)
        ? remembered
        : await vscode.window.showQuickPick(
          graph.launches.map(launch => ({ label: launch.name ?? launch.identity, description: launch.default ? 'default' : undefined })),
          { title: `Select a Launch for ${graph.product.name}` }
        ).then(value => value?.label);
      if (!configuration.launch) return undefined;
      const rememberedLaunches = this.extensionContext.workspaceState.get<Record<string, string>>('ngin.debugLaunches', {});
      await this.extensionContext.workspaceState.update('ngin.debugLaunches', { ...rememberedLaunches, [project.manifest]: configuration.launch });
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
