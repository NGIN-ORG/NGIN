export interface NativeDebugRequest {
  platform: NodeJS.Platform;
  program: string;
  cwd: string;
  args: string[];
  env: Record<string, string>;
  miDebuggerPath?: string;
}

export interface NativeDebugConfiguration {
  type: string;
  request: 'launch';
  name: string;
  program: string;
  cwd: string;
  args: string[];
  env: Record<string, string>;
  stopAtEntry: boolean;
  externalConsole: boolean;
  MIMode?: 'gdb' | 'lldb';
  miDebuggerPath?: string;
}

export function createNativeDebugConfiguration(request: NativeDebugRequest): NativeDebugConfiguration {
  if (request.platform === 'win32') {
    return {
      type: 'cppvsdbg',
      request: 'launch',
      name: 'NGIN Native Debug',
      program: request.program,
      cwd: request.cwd,
      args: request.args,
      env: request.env,
      stopAtEntry: false,
      externalConsole: false
    };
  }

  const miMode = request.platform === 'darwin' ? 'lldb' : 'gdb';
  return {
    type: 'cppdbg',
    request: 'launch',
    name: 'NGIN Native Debug',
    program: request.program,
    cwd: request.cwd,
    args: request.args,
    env: request.env,
    stopAtEntry: false,
    externalConsole: false,
    MIMode: miMode,
    miDebuggerPath: request.miDebuggerPath
  };
}

export function quoteShellArgument(value: string): string {
  if (process.platform === 'win32') {
    return `"${value.replace(/"/g, '\\"')}"`;
  }
  return `'${value.replace(/'/g, `'\\''`)}'`;
}
