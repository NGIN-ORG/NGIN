import { parseCompilerDiagnostics } from './diagnostics';

const editorEventPrefix = '\x1eNGIN ';
const runtimeLinePattern =
  /^\[(\d{4}-\d{2}-\d{2}T(\d{2}:\d{2}:\d{2})\.\d+(?:Z|[+-]\d{4}))\]\[([^\]]+)\]\[([^\]]+)\] (.*)$/u;

interface EditorEvent {
  kind: 'NGIN.EditorEvent';
  event: string;
  target?: string;
  detail?: string;
  count?: number;
}

export interface NinjaProgress {
  current: number;
  total: number;
  action: string;
}

export interface LifecyclePresentationOptions {
  command: string;
  args: string[];
  label: string;
  append(value: string): void;
  now?: () => number;
}

function argumentValue(args: string[], option: string): string | undefined {
  const index = args.indexOf(option);
  return index >= 0 ? args[index + 1] : undefined;
}

function displayArgument(value: string): string {
  return /\s/u.test(value) ? `"${value.replace(/"/gu, '\\"')}"` : value;
}

export function formatLifecycleCommand(args: string[], label: string): string {
  const command = args[0] ?? '';
  const parts = ['>', 'ngin', command, displayArgument(label)];
  const configuration = argumentValue(args, '--configuration');
  const target = argumentValue(args, '--target');
  const toolchain = argumentValue(args, '--toolchain');
  if (configuration) parts.push('--configuration', displayArgument(configuration));
  if (target && target !== 'host') parts.push('--target', displayArgument(target));
  if (toolchain && toolchain !== 'default') parts.push('--toolchain', displayArgument(toolchain));
  const trailing = args.indexOf('--');
  if (trailing >= 0) parts.push('--', ...args.slice(trailing + 1).map(displayArgument));
  return parts.filter(Boolean).join(' ');
}

function parseEditorEvent(line: string): EditorEvent | undefined {
  if (!line.startsWith(editorEventPrefix)) return undefined;
  try {
    const value = JSON.parse(line.slice(editorEventPrefix.length)) as Partial<EditorEvent>;
    if (value.kind !== 'NGIN.EditorEvent' || typeof value.event !== 'string') return undefined;
    return value as EditorEvent;
  } catch {
    return undefined;
  }
}

function compactMessage(message: string): string | undefined {
  if (message === 'kernel entered Running state' || message === 'kernel shutdown complete') return undefined;
  if (message === 'state transition -> ServicesBuilt') return 'Services built';
  if (message === 'state transition -> ModulesLoaded') return 'Modules loaded';
  if (message === 'state transition -> Running') return 'Running';
  if (message === 'state transition -> Stopping') return undefined;
  if (message === 'state transition -> Stopped') return 'Stopped';
  if (message === 'state transition -> Shutdown') return undefined;
  if (message.startsWith('module started: ')) return `Started ${message.slice('module started: '.length)}`;
  if (message.startsWith('stop requested: ')) return `Stopping (${message.slice('stop requested: '.length)})`;
  return message;
}

export function formatRuntimeLine(line: string): string | undefined {
  const match = runtimeLinePattern.exec(line);
  if (!match) return undefined;
  const level = match[3];
  const logger = match[4];
  let message = match[5];
  let source = '';
  const sourceMatch = /\s+(\([^()]+:\d+\))$/u.exec(message);
  if (sourceMatch) {
    source = sourceMatch[1];
    message = message.slice(0, sourceMatch.index);
  }
  message = message.replace(/\s+\{[^{}]*\}$/u, '');
  const compact = compactMessage(message);
  if (compact === undefined) return '';
  const category = level === 'Info' ? logger : `${level.toUpperCase()} ${logger}`;
  const location = level === 'Info' || !source ? '' : ` ${source}`;
  return `  ${match[2]}  ${category.padEnd(14)} ${compact}${location}\n`;
}

function buildInputName(value: string): string {
  const name = value.split(/[\\/]/u).pop() ?? value;
  return name.replace(/\.(?:obj|o)$/u, '');
}

export function parseNinjaProgress(line: string): NinjaProgress | undefined {
  const match = /^\[(\d+)\/(\d+)\]\s+(.+)$/u.exec(line.trim());
  if (!match) return undefined;
  const current = Number(match[1]);
  const total = Number(match[2]);
  const raw = match[3];
  if (current === 0 || /Re-checking globbed directories/u.test(raw)) return undefined;

  let action: string;
  const compile = /^Building (?:CXX|C) object (.+)$/u.exec(raw);
  const resource = /^Building RC object (.+)$/u.exec(raw);
  if (compile) action = `Compiling ${buildInputName(compile[1])}`;
  else if (resource) action = `Compiling resource ${buildInputName(resource[1])}`;
  else if (/^Linking /u.test(raw)) action = 'Linking';
  else if (/^Generating /u.test(raw)) action = raw;
  else return undefined;
  return { current, total, action };
}

function formatDuration(milliseconds: number): string {
  return `${Math.max(milliseconds, 0) / 1000 < 10
    ? (Math.max(milliseconds, 0) / 1000).toFixed(1)
    : Math.round(Math.max(milliseconds, 0) / 1000)}s`;
}

export class LifecycleOutputPresenter {
  private readonly now: () => number;
  private readonly startedAt: number;
  private readonly pending = { stdout: '', stderr: '' };
  private readonly warnings = new Set<string>();
  private readonly sections = new Set<string>();
  private phase: string | undefined;
  private phaseStartedAt = 0;
  private activeTarget: string | undefined;
  private activeBuild: { upToDate: boolean; lastProgressBucket: number } | undefined;
  private sawEvent = false;
  private sawRun = false;
  private warningSummaryWritten = false;
  private raw = '';

  constructor(private readonly options: LifecyclePresentationOptions) {
    this.now = options.now ?? Date.now;
    this.startedAt = this.now();
    options.append(`\n${formatLifecycleCommand(options.args, options.label)}\n`);
  }

  get rawOutput(): string {
    return this.raw;
  }

  accept(stream: 'stdout' | 'stderr', value: string): void {
    this.raw += value;
    this.pending[stream] += value;
    const lines = this.pending[stream].split(/\r?\n/u);
    this.pending[stream] = lines.pop() ?? '';
    for (const line of lines) this.line(line);
  }

  complete(exitCode: number): void {
    for (const stream of ['stdout', 'stderr'] as const) {
      if (this.pending[stream]) this.line(this.pending[stream]);
      this.pending[stream] = '';
    }
    if (!this.sawEvent) {
      this.options.append(this.raw);
      return;
    }
    if (exitCode !== 0 && !this.sawRun) {
      const target = this.activeTarget ?? this.options.label;
      this.options.append(`  ✗ ${target}\n\n${this.rawWithoutEvents()}`);
      return;
    }
    this.writeWarningSummary();
    if (this.sawRun) this.options.append(`\n  Process exited with code ${exitCode}\n`);
    if (exitCode !== 0) return;
    this.options.append(`\nCompleted in ${formatDuration(this.now() - this.startedAt)}\n`);
  }

  private line(line: string): void {
    for (const diagnostic of parseCompilerDiagnostics(line)) {
      if (diagnostic.severity === 'warning') {
        this.warnings.add(`${diagnostic.path}:${diagnostic.line}:${diagnostic.column}:${diagnostic.code ?? ''}`);
      }
    }

    const event = parseEditorEvent(line);
    if (event) {
      this.sawEvent = true;
      this.event(event);
      return;
    }
    if (!this.sawEvent) return;
    if (this.phase === 'build') {
      if (line.trim() === 'ninja: no work to do.') {
        if (this.activeBuild) this.activeBuild.upToDate = true;
        return;
      }
      const progress = parseNinjaProgress(line);
      if (progress && this.shouldWriteProgress(progress)) {
        this.options.append(`    [${progress.current}/${progress.total}] ${progress.action}\n`);
      }
      return;
    }
    if (this.phase === 'run' || this.phase === 'test') {
      const formatted = formatRuntimeLine(line);
      if (formatted === undefined) this.options.append(`${line}\n`);
      else if (formatted) this.options.append(formatted);
    }
  }

  private event(value: EditorEvent): void {
    const [phase, transition] = value.event.split('-');
    if (transition === 'skip' && phase === 'configure') {
      this.section('CONFIGURE');
      const target = value.target ?? this.options.label;
      this.options.append(`  ✓ ${target.padEnd(32)} up to date\n`);
      return;
    }
    if (transition === 'start') {
      this.phase = phase;
      this.phaseStartedAt = this.now();
      this.activeTarget = value.target ?? this.options.label;
      if (phase === 'configure') {
        this.section('CONFIGURE');
        this.options.append(`  ${this.activeTarget}\n`);
      } else if (phase === 'build') {
        this.section('BUILD');
        this.activeBuild = { upToDate: false, lastProgressBucket: -1 };
        this.options.append(`  ${this.activeTarget}\n`);
      } else if (phase === 'stage') {
        this.writeWarningSummary();
        this.section('STAGE');
        this.options.append(`  ${this.activeTarget}\n`);
      } else if (phase === 'publish') {
        this.section('PUBLISH');
        this.options.append(`  ${this.activeTarget}\n`);
      }
      else if (phase === 'run' || phase === 'test') {
        this.writeWarningSummary();
        this.options.append(`\n${phase.toUpperCase()}\n`);
        this.options.append(`  Starting ${value.detail ?? this.activeTarget}\n`);
        this.sawRun = true;
      }
      return;
    }
    if (transition !== 'end') return;
    if (phase === 'build') {
      const result = this.activeBuild?.upToDate ? 'up to date' : formatDuration(this.now() - this.phaseStartedAt);
      this.options.append(`  ✓ ${'Completed'.padEnd(32)} ${result}\n`);
      this.activeBuild = undefined;
    } else if (phase === 'configure' || phase === 'publish') {
      this.options.append(`  ✓ ${'Completed'.padEnd(32)} ${formatDuration(this.now() - this.phaseStartedAt)}\n`);
    } else if (phase === 'stage') {
      const staged = value.count === undefined ? 'Completed' : `${value.count} file${value.count === 1 ? '' : 's'}`;
      this.options.append(`  ✓ ${staged.padEnd(32)} ${formatDuration(this.now() - this.phaseStartedAt)}\n`);
    }
    if (this.phase === phase) this.phase = undefined;
  }

  private shouldWriteProgress(progress: NinjaProgress): boolean {
    if (!this.activeBuild) return false;
    if (progress.total <= 20 || progress.current === 1 || progress.current === progress.total) return true;
    const bucket = Math.floor(progress.current * 10 / progress.total);
    if (bucket <= this.activeBuild.lastProgressBucket) return false;
    this.activeBuild.lastProgressBucket = bucket;
    return true;
  }

  private section(name: string): void {
    if (this.sections.has(name)) return;
    this.sections.add(name);
    this.options.append(`\n${name}\n`);
  }

  private writeWarningSummary(): void {
    if (this.warningSummaryWritten || this.warnings.size === 0) return;
    this.warningSummaryWritten = true;
    this.options.append(`  ⚠ ${this.warnings.size} warning${this.warnings.size === 1 ? '' : 's'}\n`);
  }

  private rawWithoutEvents(): string {
    return this.raw
      .split(/\r?\n/u)
      .filter(line => !line.startsWith(editorEventPrefix))
      .join('\n');
  }
}
