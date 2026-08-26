import * as path from 'node:path';
import type { NginContext } from '../model';

export function selectionArguments(context: NginContext): string[] {
  const args = ['--project', context.projectManifest];
  if (context.workspaceManifest) args.push('--workspace', context.workspaceManifest);
  if (context.configuration) args.push('--configuration', context.configuration);
  if (context.target) args.push('--target', context.target);
  if (context.toolchain) args.push('--toolchain', context.toolchain);
  if (context.launch) args.push('--launch', context.launch);
  for (const [name, value] of Object.entries(context.options).sort(([left], [right]) => left.localeCompare(right))) {
    args.push('--option', `${name}=${value}`);
  }
  return args;
}

export function dependencyLockPath(context: NginContext): string {
  return path.join(context.outputDirectory, 'ngin.lock');
}

export function lifecycleArguments(command: string, context: NginContext, extra: string[] = []): string[] {
  if (command === 'lock') {
    return ['package', 'lock', ...selectionArguments(context), '--output', dependencyLockPath(context), ...extra];
  }
  const args = [command, ...selectionArguments(context)];
  if (['configure', 'build', 'stage', 'run', 'test', 'publish', 'analyze', 'format'].includes(command)) {
    args.push('--output', context.outputDirectory);
  }
  if (command === 'analyze' || command === 'format') {
    args.push('--lock', dependencyLockPath(context));
  }
  args.push(...extra);
  return args;
}
