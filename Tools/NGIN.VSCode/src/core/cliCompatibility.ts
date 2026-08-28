import type { CliResult, CompositionGraph } from '../model';

const selectionOptionPattern = /unknown option:\s*(--(?:workspace|configuration|target|toolchain|profile|option))/iu;

export function unsupportedSelectionOption(result: CliResult): string | undefined {
  return selectionOptionPattern.exec(`${result.stderr}\n${result.stdout}`)?.[1];
}

export function describeCliFailure(result: CliResult): string {
  const detail = result.stderr.trim() || result.stdout.trim() || `NGIN exited with code ${result.exitCode}`;
  const option = unsupportedSelectionOption(result);
  if (!option) return detail;
  return `The selected NGIN CLI '${result.command}' is incompatible with this version of NGIN Tools: `
    + `it does not support ${option}. Build or install the current CLI, then update the NGIN: Executable setting.\n${detail}`;
}

export function shouldLoadGraph(graph: CompositionGraph | undefined, graphError: string | undefined): boolean {
  return !graph && !graphError;
}
