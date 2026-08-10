import { promises as fs } from 'node:fs';
import * as path from 'node:path';
import * as vscode from 'vscode';
import type { NginController } from '../core/controller';
import { createNativeDebugConfiguration } from '../core/debugConfiguration';
import { stagedExecutablePath } from '../core/paths';

interface NginDebugConfiguration extends vscode.DebugConfiguration {
  project?: string;
  configuration?: string;
  target?: string;
  toolchain?: string;
  args?: string[];
  preStage?: boolean;
}

async function fileExists(candidate: string): Promise<boolean> {
  try {
    return (await fs.stat(candidate)).isFile();
  } catch {
    return false;
  }
}

export class NginDebugProvider implements vscode.DebugConfigurationProvider {
  constructor(private readonly controller: NginController) {}

  provideDebugConfigurations(): vscode.ProviderResult<vscode.DebugConfiguration[]> {
    return [{ type: 'ngin', request: 'launch', name: 'NGIN: Debug Active Project', preStage: true }];
  }

  async resolveDebugConfiguration(
    _folder: vscode.WorkspaceFolder | undefined,
    configuration: NginDebugConfiguration
  ): Promise<vscode.DebugConfiguration | undefined> {
    if (configuration.project) {
      const requested = path.resolve(this.controller.requireContext().workspaceFolder, configuration.project);
      const project = this.controller.projects.find(candidate => path.resolve(candidate.manifest) === requested);
      if (!project) throw new Error(`NGIN debug project is not discovered: ${configuration.project}`);
      await this.controller.selectProject(project, {
        configuration: configuration.configuration,
        target: configuration.target,
        toolchain: configuration.toolchain
      });
    } else if (configuration.configuration || configuration.target || configuration.toolchain) {
      await this.controller.updateSelection({
        configuration: configuration.configuration,
        target: configuration.target,
        toolchain: configuration.toolchain,
        preset: undefined
      });
    }

    let graph = this.controller.snapshot.graph ?? await this.controller.refreshGraph(true);
    if (!graph) return undefined;
    const hostOperatingSystem = process.platform === 'win32' ? 'windows' : process.platform === 'darwin' ? 'macos' : 'linux';
    if (graph.selection.targetOperatingSystem !== hostOperatingSystem) {
      throw new Error(`Local NGIN debugging requires a ${hostOperatingSystem} target; the active graph targets ${graph.selection.targetOperatingSystem}.`);
    }
    if (configuration.preStage !== false) {
      const staged = await this.controller.execute('stage');
      if (!staged) return undefined;
      graph = this.controller.snapshot.graph ?? graph;
    }

    const context = this.controller.requireContext();
    const program = stagedExecutablePath(context, graph.product.name, graph.selection.targetOperatingSystem);
    if (!await fileExists(program)) {
      throw new Error(`The staged executable was not found at ${program}. Run NGIN: Stage and inspect the NGIN output.`);
    }
    const configuredDebugger = vscode.workspace.getConfiguration('ngin').get<string>('debug.miDebuggerPath', '').trim();
    return createNativeDebugConfiguration(graph, context, program, configuration, process.platform, path.delimiter, configuredDebugger);
  }
}
