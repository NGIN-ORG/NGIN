import * as path from 'node:path';
import { promises as fs } from 'node:fs';

export interface ProcessTermination {
  exitCode: number;
  description: string;
}

export function normalizeProcessTermination(
  code: number | null,
  signal: NodeJS.Signals | null
): ProcessTermination {
  if (code !== null) {
    return {
      exitCode: code,
      description: `exited with code ${code}`
    };
  }
  if (signal) {
    return {
      exitCode: 1,
      description: `terminated by signal ${signal}`
    };
  }
  return {
    exitCode: 1,
    description: 'terminated without an exit code'
  };
}

export function backendOutputModeForVerbosity(verbosity: string): 'compact' | 'stream' {
  return verbosity === 'compact' ? 'compact' : 'stream';
}

export function withExplicitWorkspace(args: string[], workspaceManifestPath?: string): string[] {
  if (!workspaceManifestPath || !args.includes('--project') || args.includes('--workspace')) {
    return [...args];
  }
  return [...args, '--workspace', workspaceManifestPath];
}

export function resolveConfiguredCliPath(workspaceRoot: string, configuredPath?: string): string | undefined {
  if (!configuredPath) {
    return undefined;
  }

  return path.isAbsolute(configuredPath) ? configuredPath : path.resolve(workspaceRoot, configuredPath);
}

export function getDevelopmentCliPath(workspaceRoot: string, platform: NodeJS.Platform): string {
  return path.join(
    workspaceRoot,
    'build',
    'dev',
    'Tools',
    'NGIN.CLI',
    platform === 'win32' ? 'ngin.exe' : 'ngin'
  );
}

export async function fileExists(candidate: string): Promise<boolean> {
  try {
    await fs.access(candidate);
    return true;
  } catch {
    return false;
  }
}

export async function findExecutableOnPath(name: string): Promise<string | undefined> {
  const pathEntries = (process.env.PATH ?? '').split(path.delimiter).filter(Boolean);
  const extensions = process.platform === 'win32'
    ? (process.env.PATHEXT ?? '.EXE;.CMD;.BAT;.COM').split(';').filter(Boolean)
    : [''];

  for (const entry of pathEntries) {
    for (const extension of extensions) {
      const candidate = path.join(entry, process.platform === 'win32' ? `${name}${extension}` : name);
      if (await fileExists(candidate)) {
        return candidate;
      }
    }
  }

  return undefined;
}

export async function isCliStale(cliPath: string, workspaceRoot: string): Promise<boolean> {
  const referenceFiles = [
    path.join(workspaceRoot, 'Tools', 'NGIN.CLI', 'src', 'main.cpp'),
    path.join(workspaceRoot, 'Tools', 'NGIN.CLI', 'CMakeLists.txt'),
    path.join(workspaceRoot, 'CMakeLists.txt')
  ];

  const cliStat = await fs.stat(cliPath);
  for (const reference of referenceFiles) {
    const referenceStat = await fs.stat(reference);
    if (referenceStat.mtimeMs > cliStat.mtimeMs) {
      return true;
    }
  }

  return false;
}
