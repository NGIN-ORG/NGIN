import * as path from 'node:path';
import type { CompositionGraph, NginContext } from '../model';
import { stageDirectory } from './paths';

export interface NativeDebugOverrides {
  name?: string;
  run?: string;
  args?: string[];
  stopAtEntry?: boolean;
  [key: string]: unknown;
}

export interface NativeDebugConfiguration extends NativeDebugOverrides {
  name: string;
  type: 'cppvsdbg' | 'cppdbg';
  request: 'run';
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
  if (graph.product.artifactKind !== 'Executable')
    throw new Error(`NGIN product ${graph.product.name} is a Library and is not directly executable.`);
  const run = overrides.run
    ? graph.runs.find(candidate => candidate.name === overrides.run || candidate.identity === overrides.run)
    : graph.runs.find(candidate => candidate.default)
    ?? (graph.runs.length === 1 ? graph.runs[0] : undefined);
  if (!run) throw new Error(overrides.run
    ? `Unknown NGIN Run '${overrides.run}'.`
    : 'Select one of the project Run definitions.');
  if (run.executableKind && run.executableKind !== 'Product') {
    throw new Error('Debugging a package Tool run requires a CLI-exposed RunPlan and is not available yet.');
  }
  if (run.secrets && Object.keys(run.secrets).length > 0) {
    throw new Error('This Run requires secrets, but no VS Code secret provider has been configured.');
  }

  const stage = stageDirectory(context);
  const environment = { ...(run.environment ?? {}) };
  const runtimeVariable = graph.selection.targetOperatingSystem === 'windows'
    ? 'PATH'
    : graph.selection.targetOperatingSystem === 'macos' ? 'DYLD_LIBRARY_PATH' : 'LD_LIBRARY_PATH';
  const libraryDirectory = path.join(stage, 'lib');
  environment[runtimeVariable] = environment[runtimeVariable]
    ? `${libraryDirectory}${pathDelimiter}${environment[runtimeVariable]}`
    : libraryDirectory;

  const common = {
    ...overrides,
    request: 'run' as const,
    name: overrides.name || `NGIN: Debug ${graph.product.name}`,
    program,
    cwd: path.resolve(stage, run.workingDirectory ?? '.'),
    args: [...(run.arguments ?? []), ...(overrides.args ?? [])],
    environment: Object.entries(environment).map(([name, value]) => ({ name, value })),
    stopAtEntry: overrides.stopAtEntry ?? false
  };
  delete common.run;
  if (platform === 'win32') return { ...common, type: 'cppvsdbg' };
  return {
    ...common,
    type: 'cppdbg',
    MIMode: platform === 'darwin' ? 'lldb' : 'gdb',
    ...(configuredDebugger ? { miDebuggerPath: configuredDebugger } : {})
  };
}

export function createNativeTestDebugConfiguration(
  graph: CompositionGraph,
  context: NginContext,
  program: string,
  overrides: NativeDebugOverrides,
  platform: NodeJS.Platform = process.platform,
  pathDelimiter: string = path.delimiter,
  configuredDebugger = ''
): NativeDebugConfiguration {
  const test = graph.tests[0];
  if (!test) throw new Error(`${graph.product.name} does not declare a Test registration.`);
  const stage = stageDirectory(context);
  const runtimeVariable = graph.selection.targetOperatingSystem === 'windows'
    ? 'PATH'
    : graph.selection.targetOperatingSystem === 'macos' ? 'DYLD_LIBRARY_PATH' : 'LD_LIBRARY_PATH';
  const environment = [{ name: runtimeVariable, value: path.join(stage, 'lib') }];
  const common = {
    ...overrides,
    request: 'run' as const,
    name: overrides.name || `NGIN: Debug tests for ${graph.product.name}`,
    program,
    cwd: stage,
    args: [...(test.arguments ?? []), ...(overrides.args ?? [])],
    environment,
    stopAtEntry: overrides.stopAtEntry ?? false
  };
  delete common.run;
  if (platform === 'win32') return { ...common, type: 'cppvsdbg' };
  return {
    ...common, type: 'cppdbg', MIMode: platform === 'darwin' ? 'lldb' : 'gdb',
    ...(configuredDebugger ? { miDebuggerPath: configuredDebugger } : {})
  };
}
