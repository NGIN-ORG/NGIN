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

export type NginEventDiagnosticSeverity = 'note' | 'info' | 'warning' | 'error' | 'fatal';

export interface NginEventRelatedLocation {
  file: string;
  line: number;
  column: number;
  message: string;
}

export interface NginEventDiagnostic {
  severity: NginEventDiagnosticSeverity;
  source?: string;
  code?: string;
  message: string;
  file?: string;
  line?: number;
  column?: number;
  endLine?: number;
  endColumn?: number;
  run?: string;
  action?: string;
  fingerprint?: string;
  tags?: string[];
  relatedLocations?: NginEventRelatedLocation[];
  editSetIds?: string[];
}

export interface NginProducedArtifact {
  path: string;
  kind?: string;
  publish?: string;
  name?: string;
  format?: string;
  version?: string;
}

export class NginJsonlParseError extends Error {
  constructor(line: string, cause: unknown) {
    const message = cause instanceof Error ? cause.message : String(cause);
    super(`invalid NGIN CLI JSONL event: ${message}: ${line.slice(0, 160)}`);
    this.name = 'NginJsonlParseError';
  }
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

      let parsed: unknown;
      try {
        parsed = JSON.parse(line) as unknown;
      } catch (error) {
        throw new NginJsonlParseError(line, error);
      }
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
    let parsed: unknown;
    try {
      parsed = JSON.parse(trailing) as unknown;
    } catch (error) {
      throw new NginJsonlParseError(trailing, error);
    }
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
  if (event.type === 'artifact.produced') {
    const kind = typeof event.data.kind === 'string' ? event.data.kind : 'artifact';
    const path = typeof event.data.path === 'string' ? event.data.path : undefined;
    return path ? `${kind} ${path}` : undefined;
  }
  if (event.type === 'diagnostic') {
    const severity = typeof event.data.severity === 'string' ? event.data.severity : 'error';
    const message = typeof event.data.message === 'string' ? event.data.message : undefined;
    return message ? `${severity}: ${message}` : undefined;
  }
  return eventLabel(event);
}

export function artifactFromEvent(event: NginCliEvent): NginProducedArtifact | undefined {
  if (event.type !== 'artifact.produced' || typeof event.data.path !== 'string') {
    return undefined;
  }
  return {
    path: event.data.path,
    kind: typeof event.data.kind === 'string' ? event.data.kind : undefined,
    publish: typeof event.data.publish === 'string' ? event.data.publish : undefined,
    name: typeof event.data.name === 'string' ? event.data.name : undefined,
    format: typeof event.data.format === 'string' ? event.data.format : undefined,
    version: typeof event.data.version === 'string' ? event.data.version : undefined
  };
}

export function diagnosticFromEvent(event: NginCliEvent): NginEventDiagnostic | undefined {
  if (event.type !== 'diagnostic') {
    return undefined;
  }
  if (event.data.suppressed === true) {
    return undefined;
  }
  const message = typeof event.data.message === 'string' ? event.data.message : undefined;
  if (!message) {
    return undefined;
  }
  const rawSeverity = typeof event.data.severity === 'string' ? event.data.severity : 'error';
  const severity: NginEventDiagnosticSeverity = ['note', 'info', 'warning', 'error', 'fatal'].includes(rawSeverity)
    ? rawSeverity as NginEventDiagnosticSeverity
    : 'error';
  const relatedLocations = Array.isArray(event.data.relatedLocations)
    ? event.data.relatedLocations.flatMap((value): NginEventRelatedLocation[] => {
      if (typeof value !== 'string') return [];
      const match = value.match(/^(.*):(\d+):(\d+):(.*)$/);
      return match ? [{ file: match[1], line: Number(match[2]), column: Number(match[3]), message: match[4] }] : [];
    })
    : undefined;
  return {
    severity,
    source: typeof event.data.source === 'string' ? event.data.source : undefined,
    code: typeof event.data.code === 'string' ? event.data.code : undefined,
    message,
    file: typeof event.data.file === 'string' ? event.data.file : undefined,
    line: typeof event.data.line === 'number' ? event.data.line : undefined,
    column: typeof event.data.column === 'number' ? event.data.column : undefined,
    endLine: typeof event.data.endLine === 'number' ? event.data.endLine : undefined,
    endColumn: typeof event.data.endColumn === 'number' ? event.data.endColumn : undefined,
    run: typeof event.data.run === 'string' ? event.data.run : undefined,
    action: typeof event.data.action === 'string' ? event.data.action : undefined,
    fingerprint: typeof event.data.fingerprint === 'string' ? event.data.fingerprint : undefined,
    tags: Array.isArray(event.data.tags) ? event.data.tags.filter((value): value is string => typeof value === 'string') : undefined,
    relatedLocations,
    editSetIds: Array.isArray(event.data.editSetIds)
      ? event.data.editSetIds.filter((value): value is string => typeof value === 'string')
      : undefined
  };
}
