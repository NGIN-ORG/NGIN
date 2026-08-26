export type OutputVerbosity = 'compact' | 'commands' | 'trace';

export interface OutputPolicy {
  appendCommand: boolean;
  streamRaw: boolean;
  appendLifecycleTrace: boolean;
  machineReadable: boolean;
}

function machineReadableCommand(args: readonly string[]): boolean {
  const format = args.indexOf('--format');
  return args.includes('--quiet')
    || args[0] === '--version'
    || (format >= 0 && args[format + 1]?.toLowerCase() === 'json');
}

export function outputPolicy(
  args: readonly string[],
  lifecycle: boolean,
  revealOutput: boolean,
  verbosity: OutputVerbosity
): OutputPolicy {
  const machineReadable = machineReadableCommand(args);
  if (lifecycle) {
    return {
      appendCommand: false,
      streamRaw: false,
      appendLifecycleTrace: verbosity === 'trace',
      machineReadable
    };
  }
  if (verbosity === 'trace') {
    return { appendCommand: true, streamRaw: true, appendLifecycleTrace: false, machineReadable };
  }
  if (verbosity === 'commands') {
    return { appendCommand: true, streamRaw: revealOutput && !machineReadable, appendLifecycleTrace: false, machineReadable };
  }
  return {
    appendCommand: revealOutput && !machineReadable,
    streamRaw: revealOutput && !machineReadable,
    appendLifecycleTrace: false,
    machineReadable
  };
}
