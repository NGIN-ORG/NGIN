export interface ProjectTreePresentationInput {
  configuration: string;
  activeFile: boolean;
  fallback: boolean;
  busy?: string;
  graphIssue?: string;
  graphReady: boolean;
  configured?: boolean;
  analysisState?: 'analyzing' | 'failed';
  lastOperation?: {
    command: string;
    state: 'succeeded' | 'failed';
    durationMs: number;
  };
}

export interface ProjectTreePresentation {
  description: string;
  status?: string;
}

const successfulStatuses: Readonly<Record<string, string>> = {
  build: 'built',
  stage: 'staged',
  publish: 'published'
};

export function projectTreePresentation(input: ProjectTreePresentationInput): ProjectTreePresentation {
  const completed = input.lastOperation?.state === 'succeeded'
    ? successfulStatuses[input.lastOperation.command]
    : undefined;
  const status = input.busy
    ? `${input.busy} in progress`
    : input.graphIssue
      ? 'model issue'
      : input.lastOperation?.state === 'failed'
        ? `${input.lastOperation.command} failed`
        : input.analysisState === 'failed'
          ? 'analysis issue'
          : input.analysisState === 'analyzing'
            ? 'analyzing'
            : completed
              ? completed
          : input.graphReady
            ? input.configured ? 'configured' : 'needs build'
            : undefined;
  return {
    description: [
      input.configuration,
      input.activeFile ? 'active file' : input.fallback ? 'selected' : undefined,
      status
    ].filter(Boolean).join(' · '),
    status
  };
}
