import { spawn, type ChildProcessWithoutNullStreams } from 'node:child_process';
import { promises as fs } from 'node:fs';
import * as path from 'node:path';
import * as vscode from 'vscode';
import type { CliResult } from '../model';
import { parseCliDiagnostics } from './diagnostics';
import { describeCliFailure, unsupportedSelectionOption } from './cliCompatibility';
import { LifecycleOutputPresenter } from './outputPresentation';
import { outputPolicy, type OutputVerbosity } from './outputPolicy';

export interface RunOptions {
  cwd?: string;
  token?: vscode.CancellationToken;
  requireTrust?: boolean;
  revealOutput?: boolean;
  exclusive?: boolean;
  presenter?: LifecycleOutputPresenter;
}

export class CliFailure extends Error {
  readonly unsupportedOption: string | undefined;

  constructor(public readonly result: CliResult) {
    super(describeCliFailure(result));
    this.name = 'CliFailure';
    this.unsupportedOption = unsupportedSelectionOption(result);
  }
}

// A canceled foreground build can stop waiting for a directory lookup shared
// with other editor features. Its subscribers still receive the lookup result.
export async function waitWithCancellation<T>(work: Promise<T>, token?: vscode.CancellationToken): Promise<T> {
  if (!token) return work;
  let subscription: vscode.Disposable | undefined;
  const canceled = new Promise<never>((_, reject) => {
    const stop = () => reject(new vscode.CancellationError());
    subscription = token.onCancellationRequested(stop);
    if (token.isCancellationRequested) stop();
  });
  try { return await Promise.race([work, canceled]); }
  finally { subscription?.dispose(); }
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
  private readonly terminating = new WeakSet<ChildProcessWithoutNullStreams>();
  private readonly processGroups = new Set<ChildProcessWithoutNullStreams>();
  private readonly canceled = new vscode.EventEmitter<void>();
  readonly onDidCancel = this.canceled.event;
  private exclusiveActive = false;
  private exclusiveQueued = false;
  private idleWaiters: Array<() => void> = [];
  private cancellationGeneration = 0;

  dispose(): void {
    for (const child of this.active) this.terminate(child);
    this.canceled.dispose();
    this.output.dispose();
  }

  showOutput(): void {
    this.output.show(true);
  }

  beginLifecycle(args: string[], label: string, reveal: boolean): LifecycleOutputPresenter {
    const presenter = new LifecycleOutputPresenter({
      command: args[0], args, label, append: value => this.output.append(value)
    });
    if (reveal) this.showOutput();
    return presenter;
  }

  cancel(): void {
    this.cancellationGeneration++;
    this.canceled.fire();
    for (const child of this.active) this.terminate(child);
  }

  private terminate(child: ChildProcessWithoutNullStreams): void {
    if (this.terminating.has(child)) return;
    this.terminating.add(child);
    if (process.platform === 'win32' && child.pid) {
      spawn('taskkill', ['/pid', String(child.pid), '/T', '/F'], { windowsHide: true, shell: false });
    } else if (child.pid && this.processGroups.has(child)) {
      try { process.kill(-child.pid, 'SIGTERM'); } catch (error) {
        if ((error as NodeJS.ErrnoException).code !== 'ESRCH') throw error;
      }
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
      try { await waitWithCancellation(this.waitForIdle(), options.token); }
      finally { this.exclusiveQueued = false; }
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
    if (options.token?.isCancellationRequested) {
      if (options.exclusive) this.exclusiveActive = false;
      throw new vscode.CancellationError();
    }
    const cwd = options.cwd ?? workspaceFolder;
    const command = [executable, ...args].map(quoteForDisplay).join(' ');
    const presenter = options.presenter;
    const verbosity = vscode.workspace.getConfiguration('ngin').get<OutputVerbosity>('output.verbosity', 'compact');
    const policy = outputPolicy(args, Boolean(presenter), Boolean(options.revealOutput), verbosity);
    if (!presenter && policy.appendCommand) this.output.appendLine(`\n> ${command}`);
    if (!presenter && options.revealOutput) this.showOutput();

    return new Promise<CliResult>((resolve, reject) => {
      let stdout = '';
      let stderr = '';
      let settled = false;
      const group = args[0] === 'build' && process.platform !== 'win32';
      const child = spawn(executable, args, {
        cwd,
        windowsHide: true,
        detached: group,
        shell: false,
        env: presenter ? { ...process.env, NGIN_EDITOR_EVENTS: '1' } : process.env
      });
      this.active.add(child);
      if (group) this.processGroups.add(child);

      const cancellation = options.token?.onCancellationRequested(() => this.terminate(child));
      child.stdout.setEncoding('utf8');
      child.stderr.setEncoding('utf8');
      child.stdout.on('data', chunk => {
        const value = chunk.toString();
        stdout += value;
        if (presenter) presenter.accept('stdout', value);
        else if (policy.streamRaw) this.output.append(value);
      });
      child.stderr.on('data', chunk => {
        const value = chunk.toString();
        stderr += value;
        if (presenter) presenter.accept('stderr', value);
        else if (policy.streamRaw) this.output.append(value);
      });
      child.on('error', error => {
        if (settled) return;
        settled = true;
        cancellation?.dispose();
        this.active.delete(child);
        this.processGroups.delete(child);
        if (options.exclusive) this.exclusiveActive = false;
        this.signalIdle();
        if (!presenter && !policy.appendCommand) this.output.appendLine(`\n> ${command}`);
        if (!presenter) this.output.appendLine(`Unable to start NGIN CLI: ${error.message}`);
        reject(new Error(`Unable to start NGIN CLI '${executable}': ${error.message}`));
      });
      child.on('close', async (code, signal) => {
        if (settled) return;
        settled = true;
        cancellation?.dispose();
        this.active.delete(child);
        this.processGroups.delete(child);
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
          if (options.token?.isCancellationRequested) presenter.cancel();
          else presenter.complete(result.exitCode);
          if (policy.appendLifecycleTrace && presenter.rawOutput.trim()) {
            this.output.append(`\nRAW CLI OUTPUT\n${presenter.rawOutput}${presenter.rawOutput.endsWith('\n') ? '' : '\n'}`);
          }
          const logPath = await this.writeLifecycleLog(args, command, presenter.rawOutput);
          if (result.exitCode !== 0 && logPath && !options.token?.isCancellationRequested) presenter.logLocation(logPath);
        } else if (result.exitCode !== 0 && !policy.streamRaw) {
          if (!policy.appendCommand) this.output.appendLine(`\n> ${command}`);
          const relevant = result.stderr.trim() || (!policy.machineReadable ? result.stdout.trim() : '');
          this.output.appendLine(relevant || `Command exited with code ${result.exitCode}.`);
        }
        if (options.token?.isCancellationRequested) reject(new vscode.CancellationError());
        else if (result.exitCode === 0) resolve(result);
        else reject(new CliFailure(result));
      });
    });
  }

  private async writeLifecycleLog(args: string[], command: string, output: string): Promise<string | undefined> {
    const outputIndex = args.indexOf('--output');
    if (outputIndex < 0 || !args[outputIndex + 1]) return;
    try {
      const directory = path.join(args[outputIndex + 1], 'diagnostics');
      await fs.mkdir(directory, { recursive: true });
      const logPath = path.join(directory, `${args[0] ?? 'operation'}.log`);
      await fs.writeFile(logPath, `> ${command}\n${output}`, 'utf8');
      return logPath;
    } catch {
      // Presentation logging must never change the lifecycle command result.
    }
  }
}
