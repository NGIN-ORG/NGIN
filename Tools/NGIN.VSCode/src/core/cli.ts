import { spawn, type ChildProcessWithoutNullStreams } from 'node:child_process';
import { promises as fs } from 'node:fs';
import * as path from 'node:path';
import * as vscode from 'vscode';
import type { CliResult } from '../model';
import { parseCliDiagnostics } from './diagnostics';
import { describeCliFailure, unsupportedSelectionOption } from './cliCompatibility';
import { LifecycleOutputPresenter } from './outputPresentation';

export interface RunOptions {
  cwd?: string;
  token?: vscode.CancellationToken;
  requireTrust?: boolean;
  revealOutput?: boolean;
  label?: string;
  exclusive?: boolean;
  presentation?: 'lifecycle';
}

export class CliFailure extends Error {
  readonly unsupportedOption: string | undefined;

  constructor(public readonly result: CliResult) {
    super(describeCliFailure(result));
    this.name = 'CliFailure';
    this.unsupportedOption = unsupportedSelectionOption(result);
  }
}

async function exists(candidate: string): Promise<boolean> {
  try {
    return (await fs.stat(candidate)).isFile();
  } catch {
    return false;
  }
}

function quoteForDisplay(value: string): string {
  return /[\s"]/u.test(value) ? `"${value.replace(/"/g, '\\"')}"` : value;
}

export class NginCli implements vscode.Disposable {
  private readonly output = vscode.window.createOutputChannel('NGIN');
  private readonly active = new Set<ChildProcessWithoutNullStreams>();
  private exclusiveActive = false;
  private exclusiveQueued = false;
  private idleWaiters: Array<() => void> = [];
  private cancellationGeneration = 0;

  dispose(): void {
    for (const child of this.active) this.terminate(child);
    this.output.dispose();
  }

  showOutput(): void {
    this.output.show(true);
  }

  cancel(): void {
    this.cancellationGeneration++;
    for (const child of this.active) this.terminate(child);
  }

  private terminate(child: ChildProcessWithoutNullStreams): void {
    if (process.platform === 'win32' && child.pid) {
      spawn('taskkill', ['/pid', String(child.pid), '/T', '/F'], { windowsHide: true, shell: false });
    } else {
      child.kill('SIGTERM');
    }
  }

  private async waitForIdle(): Promise<void> {
    if (this.active.size === 0) return;
    await new Promise<void>(resolve => this.idleWaiters.push(resolve));
  }

  private signalIdle(): void {
    if (this.active.size !== 0) return;
    const waiters = this.idleWaiters;
    this.idleWaiters = [];
    waiters.forEach(resolve => resolve());
  }

  async resolveExecutable(workspaceFolder: string): Promise<string> {
    const configured = vscode.workspace.getConfiguration('ngin').get<string>('executable', '').trim();
    if (configured && configured !== 'ngin') {
      return path.isAbsolute(configured) ? configured : path.resolve(workspaceFolder, configured);
    }

    const development = path.join(
      workspaceFolder,
      'build',
      'dev',
      'Tools',
      'NGIN.CLI',
      process.platform === 'win32' ? 'ngin.exe' : 'ngin'
    );
    return await exists(development) ? development : (configured || 'ngin');
  }

  async run(args: string[], workspaceFolder: string, options: RunOptions = {}): Promise<CliResult> {
    if (options.requireTrust && !vscode.workspace.isTrusted) {
      throw new Error('Trust this workspace before running NGIN processes.');
    }
    if (this.exclusiveActive || this.exclusiveQueued) {
      throw new Error('Another NGIN operation is already running. Cancel it or wait for it to finish.');
    }

    if (options.exclusive) {
      const cancellationGeneration = this.cancellationGeneration;
      this.exclusiveQueued = true;
      await this.waitForIdle();
      this.exclusiveQueued = false;
      if (options.token?.isCancellationRequested || cancellationGeneration !== this.cancellationGeneration) {
        throw new Error('NGIN operation was cancelled.');
      }
      this.exclusiveActive = true;
    }

    let executable: string;
    try {
      executable = await this.resolveExecutable(workspaceFolder);
    } catch (error) {
      if (options.exclusive) this.exclusiveActive = false;
      throw error;
    }
    const cwd = options.cwd ?? workspaceFolder;
    const command = [executable, ...args].map(quoteForDisplay).join(' ');
    const presenter = options.presentation === 'lifecycle' && options.label
      ? new LifecycleOutputPresenter({
          command: args[0] ?? '', args, label: options.label, append: value => this.output.append(value)
        })
      : undefined;
    if (!presenter) this.output.appendLine(`\n> ${command}`);
    if (options.revealOutput) this.output.show(true);

    return new Promise<CliResult>((resolve, reject) => {
      let stdout = '';
      let stderr = '';
      let settled = false;
      const child = spawn(executable, args, {
        cwd,
        windowsHide: true,
        shell: false,
        env: presenter ? { ...process.env, NGIN_EDITOR_EVENTS: '1' } : process.env
      });
      this.active.add(child);

      const cancellation = options.token?.onCancellationRequested(() => this.terminate(child));
      child.stdout.on('data', chunk => {
        const value = chunk.toString();
        stdout += value;
        if (presenter) presenter.accept('stdout', value);
        else this.output.append(value);
      });
      child.stderr.on('data', chunk => {
        const value = chunk.toString();
        stderr += value;
        if (presenter) presenter.accept('stderr', value);
        else this.output.append(value);
      });
      child.on('error', error => {
        if (settled) return;
        settled = true;
        cancellation?.dispose();
        this.active.delete(child);
        if (options.exclusive) this.exclusiveActive = false;
        this.signalIdle();
        reject(new Error(`Unable to start NGIN CLI '${executable}': ${error.message}`));
      });
      child.on('close', async (code, signal) => {
        if (settled) return;
        settled = true;
        cancellation?.dispose();
        this.active.delete(child);
        if (options.exclusive) this.exclusiveActive = false;
        this.signalIdle();
        const result: CliResult = {
          command: executable,
          args: [...args],
          cwd,
          exitCode: code ?? (signal ? 1 : 0),
          stdout,
          stderr,
          diagnostics: parseCliDiagnostics(stderr)
        };
        if (presenter) {
          presenter.complete(result.exitCode);
          await this.writeLifecycleLog(args, command, presenter.rawOutput);
        }
        if (result.exitCode === 0) resolve(result);
        else reject(new CliFailure(result));
      });
    });
  }

  private async writeLifecycleLog(args: string[], command: string, output: string): Promise<void> {
    const outputIndex = args.indexOf('--output');
    if (outputIndex < 0 || !args[outputIndex + 1]) return;
    try {
      const directory = path.join(args[outputIndex + 1], 'diagnostics');
      await fs.mkdir(directory, { recursive: true });
      await fs.writeFile(path.join(directory, `${args[0] ?? 'operation'}.log`), `> ${command}\n${output}`, 'utf8');
    } catch {
      // Presentation logging must never change the lifecycle command result.
    }
  }
}
