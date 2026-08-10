import type { ActionDiagnostic, ActionDiagnosticsEnvelope } from '../model';

function point(value: unknown): value is { line: number; column: number } {
  const candidate = value as { line?: unknown; column?: unknown } | undefined;
  return Number.isInteger(candidate?.line) && Number.isInteger(candidate?.column);
}

function diagnostic(value: unknown): value is ActionDiagnostic {
  const candidate = value as Partial<ActionDiagnostic> | undefined;
  return typeof candidate?.file === 'string'
    && typeof candidate.message === 'string'
    && typeof candidate.source === 'string'
    && ['error', 'warning', 'information'].includes(candidate.severity ?? '')
    && point(candidate.range?.start)
    && point(candidate.range?.end);
}

export function parseActionDiagnostics(value: string): ActionDiagnosticsEnvelope {
  const parsed = JSON.parse(value) as Partial<ActionDiagnosticsEnvelope>;
  if (parsed.kind !== 'NGIN.ActionDiagnostics' || parsed.state !== 'complete'
    || !Array.isArray(parsed.diagnostics) || !parsed.diagnostics.every(diagnostic)) {
    throw new Error('NGIN returned an invalid Action diagnostics envelope.');
  }
  return parsed as ActionDiagnosticsEnvelope;
}
