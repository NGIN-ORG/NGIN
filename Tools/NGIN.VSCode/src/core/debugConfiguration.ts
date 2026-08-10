import * as path from 'node:path';
import type { CompositionGraph, NginContext } from '../model';
import { stageDirectory } from './paths';

export interface NativeDebugOverrides {
  name?: string;
  args?: string[];
  stopAtEntry?: boolean;
  [key: string]: unknown;
}

export interface NativeDebugConfiguration extends NativeDebugOverrides {
  name: string;
  type: 'cppvsdbg' | 'cppdbg';
  request: 'launch';
  program: string;
  cwd: string;
  args: string[];
  environment: Array<{ name: string; value: string }>;
  MIMode?: 'gdb' | 'lldb';
  miDebuggerPath?: string;
}

export function createNativeDebugConfiguration(
  graph: CompositionGraph,
  context: NginContext,
  program: string,
  overrides: NativeDebugOverrides,
  platform: NodeJS.Platform = process.platform,
  pathDelimiter: string = path.delimiter,
  configuredDebugger = ''
): NativeDebugConfiguration {
  if (!['Application', 'Tool', 'Test', 'Benchmark'].includes(graph.product.type)) {
    throw new Error(`NGIN product ${graph.product.name} (${graph.product.type}) is not directly executable.`);
  }
  const launch = graph.launches.find(candidate => candidate.default)
    ?? (graph.launches.length === 1 ? graph.launches[0] : undefined);
  if (!launch) throw new Error('Multiple NGIN Launch definitions require one Launch with Default="true".');
  if (launch.executableKind && launch.executableKind !== 'Product') {
    throw new Error('Debugging a package Tool launch requires a CLI-exposed LaunchPlan and is not available yet.');
  }
  if (launch.secrets && Object.keys(launch.secrets).length > 0) {
    throw new Error('This Launch requires secrets, but no VS Code secret provider has been configured.');
  }

  const stage = stageDirectory(context);
  const environment = { ...(launch.environment ?? {}) };
  const runtimeVariable = graph.selection.targetOperatingSystem === 'windows'
    ? 'PATH'
    : graph.selection.targetOperatingSystem === 'macos' ? 'DYLD_LIBRARY_PATH' : 'LD_LIBRARY_PATH';
  const libraryDirectory = path.join(stage, 'lib');
  environment[runtimeVariable] = environment[runtimeVariable]
    ? `${libraryDirectory}${pathDelimiter}${environment[runtimeVariable]}`
    : libraryDirectory;

  const common = {
    ...overrides,
    request: 'launch' as const,
    name: overrides.name || `NGIN: Debug ${graph.product.name}`,
    program,
    cwd: path.resolve(stage, launch.workingDirectory ?? '.'),
    args: [...(launch.arguments ?? []), ...(overrides.args ?? [])],
    environment: Object.entries(environment).map(([name, value]) => ({ name, value })),
    stopAtEntry: overrides.stopAtEntry ?? false
  };
  if (platform === 'win32') return { ...common, type: 'cppvsdbg' };
  return {
    ...common,
    type: 'cppdbg',
    MIMode: platform === 'darwin' ? 'lldb' : 'gdb',
    ...(configuredDebugger ? { miDebuggerPath: configuredDebugger } : {})
  };
}
