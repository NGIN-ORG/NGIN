import type { ActionDiagnostic, ActionDiagnosticsEnvelope } from '../model';

function point(value: unknown): value is { line: number; column: number } {
  const candidate = value as { line?: unknown; column?: unknown } | undefined;
  return Number.isInteger(candidate?.line) && Number.isInteger(candidate?.column);
}

function fixEdit(value: unknown): boolean {
  const candidate = value as { file?: unknown; range?: { start?: unknown; end?: unknown }; text?: unknown } | undefined;
  return (candidate?.file === undefined || typeof candidate.file === 'string')
    && typeof candidate?.text === 'string'
    && point(candidate.range?.start)
    && point(candidate.range?.end);
}

function fix(value: unknown): boolean {
  const candidate = value as { title?: unknown; safe?: unknown; edits?: unknown } | undefined;
  return typeof candidate?.title === 'string'
    && (candidate.safe === undefined || typeof candidate.safe === 'boolean')
    && Array.isArray(candidate.edits)
    && candidate.edits.every(fixEdit);
}

function diagnostic(value: unknown): value is ActionDiagnostic {
  const candidate = value as Partial<ActionDiagnostic> | undefined;
  return typeof candidate?.file === 'string'
    && typeof candidate.message === 'string'
    && typeof candidate.source === 'string'
    && ['error', 'warning', 'information'].includes(candidate.severity ?? '')
    && point(candidate.range?.start)
    && point(candidate.range?.end)
    && (candidate.fixes === undefined || (Array.isArray(candidate.fixes) && candidate.fixes.every(fix)));
}

export function parseActionDiagnostics(value: string): ActionDiagnosticsEnvelope {
  const parsed = JSON.parse(value) as Partial<ActionDiagnosticsEnvelope>;
  if (parsed.kind !== 'NGIN.ActionDiagnostics' || parsed.state !== 'complete'
    || !Array.isArray(parsed.diagnostics) || !parsed.diagnostics.every(diagnostic)) {
    throw new Error('NGIN returned an invalid Action diagnostics envelope.');
  }
  return parsed as ActionDiagnosticsEnvelope;
}
