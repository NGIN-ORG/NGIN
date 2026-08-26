export function isTransientAnalysisFailure(error: unknown): boolean {
  const message = error instanceof Error ? error.message : String(error);
  return /another NGIN operation is already running|operation was cancelled|process (?:was )?cancelled/iu.test(message);
}
