import * as path from 'node:path';
import * as vscode from 'vscode';
import type { NginCli } from '../core/cli';
import { lifecycleArguments } from '../core/commandArguments';
import type { NginController } from '../core/controller';

interface NginTaskDefinition extends vscode.TaskDefinition {
  type: 'ngin';
  command: string;
}

const commands = ['configure', 'build', 'stage', 'test', 'restore'] as const;

export class NginTaskProvider implements vscode.TaskProvider {
  constructor(private readonly controller: NginController, private readonly cli: NginCli) {}

  async provideTasks(): Promise<vscode.Task[]> {
    const context = this.controller.snapshot.context;
    if (!context) return [];
    return Promise.all(commands.map(command => this.createTask({ type: 'ngin', command })));
  }

  async resolveTask(task: vscode.Task): Promise<vscode.Task | undefined> {
    const definition = task.definition as NginTaskDefinition;
    if (definition.type !== 'ngin' || !definition.command) return undefined;
    return this.createTask(definition);
  }

  private async createTask(definition: NginTaskDefinition): Promise<vscode.Task> {
    const context = this.controller.requireContext();
    const executable = await this.cli.resolveExecutable(context.workspaceFolder);
    const args = lifecycleArguments(definition.command, context);
    const execution = new vscode.ProcessExecution(executable, args, { cwd: path.dirname(context.projectManifest) });
    const task = new vscode.Task(
      definition,
      vscode.TaskScope.Workspace,
      `${definition.command} ${context.projectName}`,
      'ngin',
      execution,
      ['$ngin-file']
    );
    task.group = definition.command === 'build' ? vscode.TaskGroup.Build
      : definition.command === 'test' ? vscode.TaskGroup.Test
      : undefined;
    task.presentationOptions = { reveal: vscode.TaskRevealKind.Always, panel: vscode.TaskPanelKind.Dedicated, clear: false };
    return task;
  }
}
