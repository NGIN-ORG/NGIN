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

export class NginBackendOutputBuffer {
  private pending = '';

  push(chunk: string): string[] {
    this.pending += chunk;
    const lines: string[] = [];

    while (true) {
      const lineFeed = this.pending.indexOf('\n');
      const carriageReturn = this.pending.indexOf('\r');
      const newline = lineFeed < 0
        ? carriageReturn
        : carriageReturn < 0
          ? lineFeed
          : Math.min(lineFeed, carriageReturn);
      if (newline < 0 || (this.pending[newline] === '\r' && newline === this.pending.length - 1)) {
        break;
      }

      lines.push(this.pending.slice(0, newline));
      const newlineLength = this.pending[newline] === '\r' && this.pending[newline + 1] === '\n' ? 2 : 1;
      this.pending = this.pending.slice(newline + newlineLength);
    }

    return lines;
  }

  finish(): string[] {
    const trailing = this.pending.endsWith('\r') ? this.pending.slice(0, -1) : this.pending;
    this.pending = '';
    return trailing ? [trailing] : [];
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
  if (event.type === 'command.selection') {
    const project = event.project || stringData(event, 'project');
    const profile = event.profile || stringData(event, 'profile');
    return project && profile ? `${project} [${profile}]` : project ?? undefined;
  }
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
  if (event.type === 'tool.run.started') {
    const run = stringData(event, 'run');
    return run ? `${run} started` : 'tool run started';
  }
  if (event.type === 'tool.progress') {
    const run = stringData(event, 'run');
    const message = stringData(event, 'message') ?? stringData(event, 'state') ?? stringData(event, 'stage');
    return [run, message].filter(Boolean).join(': ') || undefined;
  }
  if (event.type === 'tool.cache') {
    const run = stringData(event, 'run');
    const status = stringData(event, 'status');
    return [run, status ? `cache ${status}` : undefined].filter(Boolean).join(': ') || undefined;
  }
  if (event.type === 'tool.run.completed') {
    const run = stringData(event, 'run');
    const status = stringData(event, 'executionStatus') ?? 'completed';
    const durationMs = numberData(event, 'durationMs');
    const label = run ? `${run} ${status}` : `tool run ${status}`;
    return durationMs === undefined ? label : `${label} (${formatDuration(durationMs)})`;
  }
  if (event.type === 'command.completed') {
    const status = stringData(event, 'status') ?? 'completed';
    const durationMs = numberData(event, 'durationMs');
    return durationMs === undefined ? status : `${status} (${formatDuration(durationMs)})`;
  }
  return undefined;
}

export function eventOutputLine(event: NginCliEvent): string | undefined {
  if (event.type === 'command.selection') {
    const project = event.project || stringData(event, 'project');
    const profile = event.profile || stringData(event, 'profile');
    const productKind = stringData(event, 'productKind');
    const optimization = stringData(event, 'optimization');
    const backendConfiguration = stringData(event, 'backendConfiguration');
    const targetPlatform = stringData(event, 'targetPlatform');
    const toolchain = stringData(event, 'toolchain');
    const context = [project && profile ? `${project} [${profile}]` : project, productKind].filter(Boolean).join(' ');
    const details = [
      optimization ? `optimization=${optimization}` : undefined,
      backendConfiguration ? `backend=${backendConfiguration}` : undefined,
      targetPlatform ? `target=${targetPlatform}` : undefined,
      toolchain ? `toolchain=${toolchain}` : undefined
    ].filter(Boolean);
    return context ? ['selected', context, ...details].join(' ') : undefined;
  }
  if (event.type === 'summary') {
    const output = typeof event.data.output === 'string' ? event.data.output : undefined;
    const launch = typeof event.data.launch === 'string' ? event.data.launch : undefined;
    const executable = typeof event.data.executable === 'string' ? event.data.executable : undefined;
    const buildDir = stringData(event, 'buildDir');
    const compileDatabase = stringData(event, 'compileDatabase');
    const knownSummary = [
      output ? `output ${output}` : undefined,
      buildDir ? `build ${buildDir}` : undefined,
      compileDatabase ? `compile database ${compileDatabase}` : undefined,
      launch ? `launch ${launch}` : undefined,
      executable ? `executable ${executable}` : undefined
    ].filter(Boolean);
    if (knownSummary.length) {
      return knownSummary.join('\n');
    }
    const counts = ['runs', 'skippedRuns', 'sources', 'findings', 'changes', 'metrics', 'checks']
      .map((name) => keyValue(event, name))
      .filter(Boolean);
    const healthy = booleanData(event, 'healthy');
    if (healthy !== undefined) {
      counts.push(`healthy=${healthy}`);
    }
    return counts.length ? `summary ${counts.join(' ')}` : undefined;
  }
  if (event.type === 'backend.output') {
    return typeof event.data.text === 'string' ? event.data.text : undefined;
  }
  if (event.type === 'tool.run.started') {
    const run = stringData(event, 'run');
    const action = stringData(event, 'action');
    const inputContract = stringData(event, 'inputContract');
    const inputScope = stringData(event, 'inputScope');
    const inputs = keyValue(event, 'inputs');
    return run ? [
      `tool ${run} started`,
      action ? `action=${action}` : undefined,
      inputContract ? `input=${inputContract}` : undefined,
      inputScope ? `scope=${inputScope}` : undefined,
      inputs
    ].filter(Boolean).join(' ') : undefined;
  }
  if (event.type === 'tool.progress') {
    const run = stringData(event, 'run');
    const message = stringData(event, 'message') ?? stringData(event, 'state') ?? stringData(event, 'stage');
    const current = numberData(event, 'current');
    const total = numberData(event, 'total');
    const progress = current !== undefined && total !== undefined ? `${current}/${total}` : undefined;
    return run && message ? [`tool ${run}: ${message}`, progress].filter(Boolean).join(' ') : undefined;
  }
  if (event.type === 'tool.cache') {
    const run = stringData(event, 'run');
    const status = stringData(event, 'status');
    return run && status ? `tool ${run} cache ${status}` : undefined;
  }
  if (event.type === 'metric') {
    const run = stringData(event, 'run');
    const name = stringData(event, 'name');
    const value = numberData(event, 'value');
    const unit = stringData(event, 'unit');
    if (!run || !name || value === undefined) return undefined;
    return `tool ${run} metric ${name}=${value}${unit ? ` ${unit}` : ''}`;
  }
  if (event.type === 'gate.evaluated') {
    const run = stringData(event, 'run');
    const status = stringData(event, 'status');
    if (!run || !status) return undefined;
    return [
      `tool ${run} gate ${status}`,
      keyValue(event, 'findings'),
      keyValue(event, 'warnings')
    ].filter(Boolean).join(' ');
  }
  if (event.type === 'edit.proposed') {
    const run = stringData(event, 'run');
    const label = stringData(event, 'label') ?? stringData(event, 'editSetId');
    if (!run || !label) return undefined;
    return [
      `tool ${run} proposed edit ${label}`,
      stringData(event, 'applicability'),
      keyValue(event, 'files')
    ].filter(Boolean).join(' ');
  }
  if (event.type === 'tool.run.completed') {
    const run = stringData(event, 'run');
    const status = stringData(event, 'executionStatus') ?? 'completed';
    const durationMs = numberData(event, 'durationMs');
    return run ? [
      `tool ${run} ${status}`,
      keyValue(event, 'gateStatus'),
      keyValue(event, 'cacheStatus'),
      keyValue(event, 'changeStatus'),
      keyValue(event, 'findings'),
      keyValue(event, 'edits'),
      durationMs === undefined ? undefined : `duration=${formatDuration(durationMs)}`
    ].filter(Boolean).join(' ') : undefined;
  }
  if (event.type === 'command.completed') {
    const status = stringData(event, 'status') ?? 'completed';
    const exitCode = numberData(event, 'exitCode');
    const durationMs = numberData(event, 'durationMs');
    return [
      `command ${status}`,
      exitCode === undefined ? undefined : `exit=${exitCode}`,
      durationMs === undefined ? undefined : `duration=${formatDuration(durationMs)}`
    ].filter(Boolean).join(' ');
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

function stringData(event: NginCliEvent, name: string): string | undefined {
  const value = event.data[name];
  return typeof value === 'string' && value.length > 0 ? value : undefined;
}

function numberData(event: NginCliEvent, name: string): number | undefined {
  const value = event.data[name];
  return typeof value === 'number' ? value : undefined;
}

function booleanData(event: NginCliEvent, name: string): boolean | undefined {
  const value = event.data[name];
  return typeof value === 'boolean' ? value : undefined;
}

function keyValue(event: NginCliEvent, name: string): string | undefined {
  const value = event.data[name];
  if (typeof value !== 'string' && typeof value !== 'number' && typeof value !== 'boolean') {
    return undefined;
  }
  if (typeof value === 'string' && value.length === 0) {
    return undefined;
  }
  return `${name}=${value}`;
}

function formatDuration(durationMs: number): string {
  return `${(durationMs / 1000).toFixed(1)}s`;
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
