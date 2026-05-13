export interface NginCliEvent {
  schemaVersion: string;
  kind: 'NGIN.CLI.Event';
  sequence: number;
  timestamp: string;
  type: string;
  command?: string;
  project?: string;
  profile?: string;
  data: Record<string, unknown>;
}

export type NginEventDiagnosticSeverity = 'error' | 'warning';

export interface NginEventDiagnostic {
  severity: NginEventDiagnosticSeverity;
  source?: string;
  code?: string;
  message: string;
  file?: string;
  line?: number;
  column?: number;
}

export class NginJsonlEventParser {
  private pending = '';

  push(chunk: string): NginCliEvent[] {
    this.pending += chunk;
    const events: NginCliEvent[] = [];

    while (true) {
      const newline = this.pending.indexOf('\n');
      if (newline < 0) {
        break;
      }

      const line = this.pending.slice(0, newline).trim();
      this.pending = this.pending.slice(newline + 1);
      if (!line) {
        continue;
      }

      const parsed = JSON.parse(line) as unknown;
      if (isNginCliEvent(parsed)) {
        events.push(parsed);
      }
    }

    return events;
  }

  finish(): NginCliEvent[] {
    const trailing = this.pending.trim();
    this.pending = '';
    if (!trailing) {
      return [];
    }
    const parsed = JSON.parse(trailing) as unknown;
    return isNginCliEvent(parsed) ? [parsed] : [];
  }
}

export function isNginCliEvent(value: unknown): value is NginCliEvent {
  if (!value || typeof value !== 'object') {
    return false;
  }
  const candidate = value as Partial<NginCliEvent>;
  return candidate.kind === 'NGIN.CLI.Event'
    && typeof candidate.schemaVersion === 'string'
    && typeof candidate.sequence === 'number'
    && typeof candidate.timestamp === 'string'
    && typeof candidate.type === 'string'
    && !!candidate.data
    && typeof candidate.data === 'object';
}

export function eventLabel(event: NginCliEvent): string | undefined {
  if (event.type === 'phase.started') {
    const label = typeof event.data.label === 'string' ? event.data.label : undefined;
    const phase = typeof event.data.phase === 'string' ? event.data.phase : undefined;
    return label ?? phase;
  }
  if (event.type === 'phase.completed') {
    const label = typeof event.data.label === 'string' ? event.data.label : undefined;
    const durationMs = typeof event.data.durationMs === 'number' ? event.data.durationMs : undefined;
    return durationMs === undefined ? label : `${label ?? 'phase'} complete (${(durationMs / 1000).toFixed(1)}s)`;
  }
  if (event.type === 'phase.failed') {
    const label = typeof event.data.label === 'string' ? event.data.label : undefined;
    return `${label ?? 'phase'} failed`;
  }
  return undefined;
}

export function eventOutputLine(event: NginCliEvent): string | undefined {
  if (event.type === 'summary') {
    const output = typeof event.data.output === 'string' ? event.data.output : undefined;
    const launch = typeof event.data.launch === 'string' ? event.data.launch : undefined;
    const executable = typeof event.data.executable === 'string' ? event.data.executable : undefined;
    return [output ? `output ${output}` : undefined, launch ? `launch ${launch}` : undefined, executable ? `executable ${executable}` : undefined]
      .filter(Boolean)
      .join('\n');
  }
  if (event.type === 'backend.output') {
    return typeof event.data.text === 'string' ? event.data.text : undefined;
  }
  if (event.type === 'diagnostic') {
    const severity = typeof event.data.severity === 'string' ? event.data.severity : 'error';
    const message = typeof event.data.message === 'string' ? event.data.message : undefined;
    return message ? `${severity}: ${message}` : undefined;
  }
  return eventLabel(event);
}

export function diagnosticFromEvent(event: NginCliEvent): NginEventDiagnostic | undefined {
  if (event.type !== 'diagnostic') {
    return undefined;
  }
  const message = typeof event.data.message === 'string' ? event.data.message : undefined;
  if (!message) {
    return undefined;
  }
  const severity = event.data.severity === 'warning' ? 'warning' : 'error';
  return {
    severity,
    source: typeof event.data.source === 'string' ? event.data.source : undefined,
    code: typeof event.data.code === 'string' ? event.data.code : undefined,
    message,
    file: typeof event.data.file === 'string' ? event.data.file : undefined,
    line: typeof event.data.line === 'number' ? event.data.line : undefined,
    column: typeof event.data.column === 'number' ? event.data.column : undefined
  };
}
