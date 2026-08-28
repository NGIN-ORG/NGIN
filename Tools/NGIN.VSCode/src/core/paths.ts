import * as path from 'node:path';
import type { NginContext, ProjectCandidate } from '../model';

export const generatedDirectoryPattern = '**/{.git,.ngin,build,out,node_modules}/**';

export function safePathComponent(value: string): string {
  const sanitized = value.replace(/[^A-Za-z0-9_.-]+/g, '_');
  return sanitized || 'default';
}

export function normalizeForComparison(value: string): string {
  const normalized = path.normalize(value);
  return process.platform === 'win32' ? normalized.toLowerCase() : normalized;
}

export function pathsEqual(left: string | undefined, right: string | undefined): boolean {
  if (!left || !right) return left === right;
  return normalizeForComparison(left) === normalizeForComparison(right);
}

export function isWithin(parent: string, candidate: string): boolean {
  const relative = path.relative(path.resolve(parent), path.resolve(candidate));
  return relative === '' || (!relative.startsWith('..' + path.sep) && relative !== '..' && !path.isAbsolute(relative));
}

export function isProjectConfigurationPath(context: NginContext, candidate: string): boolean {
  return isWithin(path.dirname(context.projectManifest), candidate)
    || isWithin(context.outputDirectory, candidate);
}

export function contextKey(context: NginContext): string {
  const options = Object.entries(context.options).sort(([left], [right]) => left.localeCompare(right));
  return JSON.stringify({
    workspace: context.workspaceManifest,
    project: context.projectManifest,
    configuration: context.configuration,
    target: context.target,
    toolchain: context.toolchain,
    profile: context.profile,
    options
  });
}

export function projectOutputDirectory(
  workspaceFolder: string,
  project: ProjectCandidate,
  configuration: string,
  target: string,
  toolchain: string,
  configuredRoot?: string
): string {
  const root = configuredRoot
    ? path.resolve(workspaceFolder, configuredRoot)
    : path.join(workspaceFolder, '.ngin', 'build');
  const selection = [configuration, target, toolchain].map(safePathComponent).join('.');
  return path.join(root, safePathComponent(project.name), selection);
}

export function compileCommandsPath(context: NginContext): string {
  return path.join(context.outputDirectory, 'cmake', 'compile_commands.json');
}

export function stageDirectory(context: NginContext): string {
  return path.join(context.outputDirectory, 'stage');
}

export function stagedExecutablePath(context: NginContext, productName: string, targetOperatingSystem?: string): string {
  const windows = targetOperatingSystem ? targetOperatingSystem.toLowerCase() === 'windows' : process.platform === 'win32';
  return path.join(stageDirectory(context), 'bin', windows ? `${productName}.exe` : productName);
}
