import { parseCompilerDiagnostics } from './diagnostics';

const editorEventPrefix = '\x1eNGIN ';
const runtimeLinePattern =
  /^\[(\d{4}-\d{2}-\d{2}T(\d{2}:\d{2}:\d{2})\.\d+(?:Z|[+-]\d{4}))\]\[([^\]]+)\]\[([^\]]+)\] (.*)$/u;

interface EditorEvent {
  kind: 'NGIN.EditorEvent';
  event: string;
  target?: string;
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
    if (this.phase === 'run' || this.phase === 'test') {
      const formatted = formatRuntimeLine(line);
      if (formatted === undefined) this.options.append(`${line}\n`);
      else if (formatted) this.options.append(formatted);
    }
  }

  private event(value: EditorEvent): void {
    const [phase, transition] = value.event.split('-');
    if (transition === 'start') {
      this.phase = phase;
      this.phaseStartedAt = this.now();
      this.activeTarget = value.target ?? this.options.label;
      if (phase === 'build') this.section('BUILD');
      else if (phase === 'configure' && this.options.command === 'configure') this.section('CONFIGURE');
      else if (phase === 'stage' && this.options.command === 'stage') this.section('STAGE');
      else if (phase === 'publish' && this.options.command === 'publish') this.section('PUBLISH');
      else if (phase === 'run' || phase === 'test') {
        this.writeWarningSummary();
        this.options.append(`\n${phase.toUpperCase()}\n`);
        this.sawRun = true;
      }
      return;
    }
    if (transition !== 'end') return;
    if (phase === 'build' || (phase === 'configure' && this.options.command === 'configure') ||
        (phase === 'stage' && this.options.command === 'stage') ||
        (phase === 'publish' && this.options.command === 'publish')) {
      const target = value.target ?? this.activeTarget ?? this.options.label;
      this.options.append(`  ✓ ${target.padEnd(32)} ${formatDuration(this.now() - this.phaseStartedAt)}\n`);
    }
    if (this.phase === phase) this.phase = undefined;
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
