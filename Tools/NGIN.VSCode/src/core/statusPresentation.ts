import type { NginContext, ProjectCandidate } from '../model';

export interface StatusPresentationInput {
  context: NginContext;
  project: ProjectCandidate;
  reason: 'activeFile' | 'default' | 'operation';
  operation?: string;
  graphError?: string;
  analysisState?: 'idle' | 'analyzing' | 'ready' | 'failed' | 'disabled';
  analysisMessage?: string;
  lastOperation?: { command: string; state: 'succeeded' | 'failed'; completedAt: number; durationMs: number };
}

export interface StatusPresentation {
  text: string;
  tooltip: string;
  accessibilityLabel: string;
}

export function statusPresentation(input: StatusPresentationInput): StatusPresentation {
  const issue = Boolean(input.graphError) || input.analysisState === 'failed';
  const busy = Boolean(input.operation) || input.analysisState === 'analyzing';
  const icon = busy ? 'loading~spin' : issue ? 'warning' : 'project';
  const text = input.operation
    ? `$(${icon}) ${input.project.name} · ${input.operation}`
    : `$(${icon}) ${input.project.name} · ${input.context.configuration}`;
  const reason = input.reason === 'activeFile'
    ? 'Effective because this project owns the active file.'
    : input.reason === 'operation'
      ? 'Project with the active NGIN operation.'
      : 'Default because the active file has no project owner.';
  const last = input.lastOperation
    ? `Last operation: ${input.lastOperation.command} ${input.lastOperation.state} in ${(input.lastOperation.durationMs / 1000).toFixed(1)}s`
    : undefined;
  return {
    text,
    tooltip: [
      reason,
      input.context.projectManifest,
      `Configuration: ${input.context.configuration}`,
      `Target: ${input.context.target}`,
      `Toolchain: ${input.context.toolchain}`,
      input.operation ? `Operation: ${input.operation}` : undefined,
      input.graphError ? 'Project model issue: open Problems or NGIN Output.' : undefined,
      input.analysisMessage ? `Analysis: ${input.analysisMessage}` : undefined,
      last,
      'Click for NGIN project actions.'
    ].filter(Boolean).join('\n'),
    accessibilityLabel: [
      'NGIN', input.project.name, input.context.configuration,
      input.reason === 'activeFile' ? 'active file project' : input.reason === 'default' ? 'default project' : undefined,
      input.operation ? `${input.operation} in progress` : undefined,
      issue ? 'issue' : undefined
    ].filter(Boolean).join(', ')
  };
}
