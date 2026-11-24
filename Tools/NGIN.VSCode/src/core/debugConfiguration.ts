export interface LaunchPlan {
  kind: 'NGIN.Launch';
  version: 1;
  program: string;
  cwd: string;
  args: string[];
  environment: Record<string, string>;
}

export function parseLaunchPlan(text: string): LaunchPlan {
  const plan = JSON.parse(text);
  if (plan?.kind !== 'NGIN.Launch' || plan.version !== 1 || typeof plan.program !== 'string'
    || typeof plan.cwd !== 'string' || !Array.isArray(plan.args) || !plan.args.every((arg: unknown) => typeof arg === 'string')
    || !plan.environment || typeof plan.environment !== 'object' || Array.isArray(plan.environment)
    || !Object.values(plan.environment).every(value => typeof value === 'string')) {
    throw new Error('NGIN returned an invalid launch plan.');
  }
  return plan;
}

export function createNativeDebugConfiguration(plan: LaunchPlan, overrides: Record<string, unknown>,
  platform: NodeJS.Platform, debuggerPath: string) {
  return {
    ...overrides,
    type: platform === 'win32' ? 'cppvsdbg' : 'cppdbg', request: 'launch',
    program: plan.program, cwd: plan.cwd, args: plan.args,
    environment: Object.entries(plan.environment).map(([name, value]) => ({ name, value })),
    ...(platform === 'win32' ? {} : {
      MIMode: platform === 'darwin' ? 'lldb' : 'gdb',
      ...(debuggerPath ? { miDebuggerPath: debuggerPath } : {})
    })
  };
}
