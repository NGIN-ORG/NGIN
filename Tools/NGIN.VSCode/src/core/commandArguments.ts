import * as path from 'node:path';
import type { NginContext } from '../model';

export function selectionArguments(context: NginContext): string[] {
  const args = ['--project', context.projectManifest];
  if (context.workspaceManifest) args.push('--workspace', context.workspaceManifest);
  if (context.configuration) args.push('--configuration', context.configuration);
  if (context.target) args.push('--target', context.target);
  if (context.toolchain) args.push('--toolchain', context.toolchain);
  if (context.run) args.push('--run', context.run);
  if (context.profile) args.push('--profile', context.profile);
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
  const args = command === 'format'
    ? ['tooling', 'format', ...selectionArguments(context)]
    : [command, ...selectionArguments(context)];
  if (['configure', 'build', 'stage', 'run', 'test', 'benchmark', 'publish', 'analyze', 'format'].includes(command)) {
    args.push('--output', context.outputDirectory);
  }
  if (command === 'analyze' || command === 'format') {
    args.push('--lock', dependencyLockPath(context));
  }
  args.push(...extra);
  return args;
}

// Split program arguments without passing them through a shell.
export function programArguments(input: string): string[] {
  const args: string[] = [];
  let word = '', quote = '', started = false;
  for (let i = 0; i < input.length; i++) {
    const c = input[i];
    if (quote && c === quote) {
      quote = '';
    } else if (!quote && (c === '"' || c === "'")) {
      quote = c;
    } else if (!quote && /\s/u.test(c)) {
      if (started) args.push(word);
      word = '';
      started = false;
      continue;
    } else if (c === '\\' && input[i + 1] === '"' && quote !== "'") {
      word += input[++i];
    } else {
      word += c;
    }
    started = true;
  }
  if (quote) throw new Error('Close the quoted argument.');
  if (started) args.push(word);
  return args;
}
