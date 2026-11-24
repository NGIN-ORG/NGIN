import * as path from 'node:path';
import * as vscode from 'vscode';
import type { NginCli } from '../core/cli';
import type { NginController } from '../core/controller';

interface NginTaskDefinition extends vscode.TaskDefinition {
  type: 'ngin';
  command: string;
}

const commands = ['configure', 'build', 'stage', 'test', 'benchmark', 'restore'] as const;

export class NginTaskProvider implements vscode.TaskProvider {
  constructor(private readonly controller: NginController, private readonly cli: NginCli) {}

  async provideTasks(): Promise<vscode.Task[]> {
    const context = this.controller.launchProduct
      ? this.controller.contextForProject(this.controller.launchProduct)
      : this.controller.snapshot.context;
    if (!context) return [];
    return Promise.all(commands.filter(command => context.projectSystem !== 'CMake' || ['configure', 'build', 'test'].includes(command)).map(command => this.createTask({ type: 'ngin', command }, context)));
  }

  async resolveTask(task: vscode.Task): Promise<vscode.Task | undefined> {
    const definition = task.definition as NginTaskDefinition;
    if (definition.type !== 'ngin' || !definition.command) return undefined;
    const project = this.controller.launchProduct;
    return project ? this.createTask(definition, this.controller.contextForProject(project)) : undefined;
  }

  private async createTask(definition: NginTaskDefinition, selectedContext = this.controller.requireContext()): Promise<vscode.Task> {
    const context = selectedContext;
    const executable = await this.cli.resolveExecutable(context.workspaceFolder);
    const args = this.controller.commandArguments(definition.command, context);
    const execution = new vscode.ProcessExecution(executable, args, { cwd: context.projectSystem === 'CMake' ? context.projectManifest : path.dirname(context.projectManifest) });
    const task = new vscode.Task(
      definition,
      vscode.TaskScope.Workspace,
      `${definition.command} ${context.projectName}`,
      'ngin',
      execution,
      ['$ngin-file']
    );
    task.group = definition.command === 'build' ? vscode.TaskGroup.Build
      : definition.command === 'test' || definition.command === 'benchmark' ? vscode.TaskGroup.Test
      : undefined;
    task.presentationOptions = { reveal: vscode.TaskRevealKind.Always, panel: vscode.TaskPanelKind.Dedicated, clear: false };
    return task;
  }
}
