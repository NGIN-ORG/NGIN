import * as path from 'node:path';
import * as vscode from 'vscode';
import { BuildTerminal } from '../ui/buildTerminal';
import type { CliResult, NginContext } from '../model';
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

  async runBuild(context: NginContext, extra: string[] = []): Promise<CliResult | undefined> {
    let result: Promise<CliResult | undefined> | undefined;
    const task = this.buildTask({ type: 'ngin', command: 'build' }, context, extra, run => { result = run; });
    const execution = vscode.tasks.executeTask(task);
    let subscription: vscode.Disposable | undefined;
    const ended = new Promise<void>(resolve => {
      subscription = vscode.tasks.onDidEndTask(event => {
        void execution.then(active => { if (active === event.execution) resolve(); }, () => {});
      });
    });
    try {
      await execution;
      await ended;
      return await result;
    } finally {
      subscription?.dispose();
    }
  }

  private buildTask(definition: NginTaskDefinition, context: NginContext, extra: string[] = [],
    onStart?: (run: Promise<CliResult | undefined>) => void): vscode.Task {
    const args = this.controller.commandArguments('build', context, extra);
    const execution = new vscode.CustomExecution(async () =>
      new BuildTerminal(context.projectName, args, this.cli.onDidCancel, (token, presenter) => {
        const run = this.controller.execute('build', extra, context.projectSystem === 'CMake', context,
          { token, presenter, announceFailure: false });
        onStart?.(run);
        return run;
      }));
    const task = new vscode.Task(definition, vscode.TaskScope.Workspace, `build ${context.projectName}`, 'ngin', execution);
    task.group = vscode.TaskGroup.Build;
    task.presentationOptions = {
      reveal: vscode.TaskRevealKind.Always, focus: true, panel: vscode.TaskPanelKind.Dedicated,
      echo: false, clear: true, showReuseMessage: false
    };
    return task;
  }

  private async createTask(definition: NginTaskDefinition, selectedContext = this.controller.requireContext()): Promise<vscode.Task> {
    const context = selectedContext;
    if (definition.command === 'build') return this.buildTask(definition, context);
    await this.controller.resolveDirectory(context);
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
    task.group = definition.command === 'test' || definition.command === 'benchmark' ? vscode.TaskGroup.Test
      : undefined;
    task.presentationOptions = { reveal: vscode.TaskRevealKind.Always, panel: vscode.TaskPanelKind.Dedicated, clear: false };
    return task;
  }
}
