import * as path from 'node:path';

export interface CompileCommandEntry {
  directory: string;
  file: string;
  output?: string;
  command?: string;
  arguments?: string[];
}

export interface SourceConfigurationModel {
  includePath: string[];
  defines: string[];
  forcedInclude?: string[];
  compilerPath?: string;
  compilerArgs?: string[];
  standard?: string;
  intelliSenseMode?: string;
}

export interface BrowseConfigurationModel {
  browsePath: string[];
  compilerPath?: string;
  compilerArgs?: string[];
  standard?: string;
}

function comparablePath(value: string): string {
  const normalized = path.normalize(value);
  return process.platform === 'win32' ? normalized.toLowerCase() : normalized;
}

function uniqueValues(values: string[]): string[] {
  const seen = new Set<string>();
  const result: string[] = [];

  for (const value of values) {
    const key = comparablePath(value);
    if (seen.has(key)) {
      continue;
    }

    seen.add(key);
    result.push(value);
  }

  return result;
}

function shellSplit(command: string): string[] {
  const tokens: string[] = [];
  let current = '';
  let quote: '"' | '\'' | undefined;
  let escaping = false;

  for (const character of command) {
    if (escaping) {
      current += character;
      escaping = false;
      continue;
    }

    if (character === '\\' && quote !== '\'') {
      escaping = true;
      continue;
    }

    if (quote) {
      if (character === quote) {
        quote = undefined;
      } else {
        current += character;
      }
      continue;
    }

    if (character === '"' || character === '\'') {
      quote = character;
      continue;
    }

    if (/\s/.test(character)) {
      if (current) {
        tokens.push(current);
        current = '';
      }
      continue;
    }

    current += character;
  }

  if (current) {
    tokens.push(current);
  }

  return tokens;
}

function getCommandTokens(entry: CompileCommandEntry): string[] {
  if (entry.arguments?.length) {
    return [...entry.arguments];
  }

  return entry.command ? shellSplit(entry.command) : [];
}

function resolvePathFromEntry(entry: CompileCommandEntry, candidate: string): string {
  return path.isAbsolute(candidate) ? candidate : path.resolve(entry.directory, candidate);
}

function commonDirectoryPrefixLength(left: string, right: string): number {
  const leftParts = comparablePath(path.resolve(left)).split(path.sep);
  const rightParts = comparablePath(path.resolve(right)).split(path.sep);
  let score = 0;

  for (let index = 0; index < Math.min(leftParts.length, rightParts.length); index += 1) {
    if (leftParts[index] !== rightParts[index]) {
      break;
    }
    score += 1;
  }

  return score;
}

function inferArchitecture(compilerArgs: string[]): 'x86' | 'x64' | 'arm' | 'arm64' {
  const lowered = compilerArgs.map((argument) => argument.toLowerCase());

  if (lowered.some((argument) => argument.includes('arm64') || argument === '-march=armv8-a')) {
    return 'arm64';
  }
  if (lowered.some((argument) => argument.includes('arm'))) {
    return 'arm';
  }
  if (lowered.some((argument) => argument === '-m32' || argument === '/m32')) {
    return 'x86';
  }

  return 'x64';
}

function deriveIntelliSenseMode(
  compilerPath: string | undefined,
  compilerArgs: string[],
  platform: NodeJS.Platform
): string | undefined {
  const compilerName = (compilerPath ? path.basename(compilerPath) : '').toLowerCase();
  const architecture = inferArchitecture(compilerArgs);

  if (compilerName.includes('clang')) {
    if (platform === 'win32' && compilerName.includes('clang-cl')) {
      return `windows-clang-${architecture}`;
    }
    if (platform === 'darwin') {
      return `macos-clang-${architecture}`;
    }
    if (platform === 'win32') {
      return `windows-clang-${architecture}`;
    }
    return `linux-clang-${architecture}`;
  }

  if (platform === 'win32' && (compilerName === 'cl' || compilerName === 'cl.exe')) {
    return `windows-msvc-${architecture}`;
  }

  if (platform === 'darwin') {
    return `macos-gcc-${architecture}`;
  }
  if (platform === 'win32') {
    return `windows-gcc-${architecture}`;
  }
  return `linux-gcc-${architecture}`;
}

function createCompilerArgs(
  compilerArgs: string[],
  includePath: string[],
  defines: string[],
  forcedInclude: string[],
  standard: string | undefined
): string[] {
  const args = [...compilerArgs];

  for (const include of includePath) {
    args.push(`-I${include}`);
  }
  for (const define of defines) {
    args.push(`-D${define}`);
  }
  for (const forced of forcedInclude) {
    args.push('-include', forced);
  }
  if (standard && !args.some((argument) => argument.startsWith('-std=') || argument.startsWith('/std:'))) {
    args.push(`-std=${standard}`);
  }

  return args;
}

function parseCompilerInvocation(entry: CompileCommandEntry, platform: NodeJS.Platform): SourceConfigurationModel {
  const tokens = getCommandTokens(entry);
  const compilerPath = tokens[0] ? resolvePathFromEntry(entry, tokens[0]) : undefined;
  const includePath: string[] = [];
  const defines: string[] = [];
  const forcedInclude: string[] = [];
  const filteredCompilerArgs: string[] = [];
  let standard: string | undefined;

  for (let index = 1; index < tokens.length; index += 1) {
    const token = tokens[index];
    const next = tokens[index + 1];

    if (token === '-c' || token === '/c') {
      continue;
    }

    if (token === '-o' || token === '/Fo' || token === '/Fe' || token === '/Fd' || token === '/Fp') {
      index += next ? 1 : 0;
      continue;
    }

    if (token.startsWith('/Fo') || token.startsWith('/Fe') || token.startsWith('/Fd') || token.startsWith('/Fp')) {
      continue;
    }

    if (comparablePath(resolvePathFromEntry(entry, token)) === comparablePath(entry.file)) {
      continue;
    }

    if (token === '-I' || token === '-isystem' || token === '-iquote' || token === '-idirafter') {
      if (next) {
        const resolved = resolvePathFromEntry(entry, next);
        includePath.push(resolved);
        filteredCompilerArgs.push(token, resolved);
        index += 1;
      }
      continue;
    }

    if (token.startsWith('-I') && token.length > 2) {
      const resolved = resolvePathFromEntry(entry, token.slice(2));
      includePath.push(resolved);
      filteredCompilerArgs.push(`-I${resolved}`);
      continue;
    }

    if (token.startsWith('/I') && token.length > 2) {
      const resolved = resolvePathFromEntry(entry, token.slice(2));
      includePath.push(resolved);
      filteredCompilerArgs.push(`/I${resolved}`);
      continue;
    }

    if (token === '/I') {
      if (next) {
        const resolved = resolvePathFromEntry(entry, next);
        includePath.push(resolved);
        filteredCompilerArgs.push('/I', resolved);
        index += 1;
      }
      continue;
    }

    if (token === '-D' || token === '/D') {
      if (next) {
        defines.push(next);
        filteredCompilerArgs.push(token, next);
        index += 1;
      }
      continue;
    }

    if (token.startsWith('-D') && token.length > 2) {
      const define = token.slice(2);
      defines.push(define);
      filteredCompilerArgs.push(`-D${define}`);
      continue;
    }

    if (token.startsWith('/D') && token.length > 2) {
      const define = token.slice(2);
      defines.push(define);
      filteredCompilerArgs.push(`/D${define}`);
      continue;
    }

    if (token === '-include' || token === '/FI') {
      if (next) {
        const resolved = resolvePathFromEntry(entry, next);
        forcedInclude.push(resolved);
        filteredCompilerArgs.push(token, resolved);
        index += 1;
      }
      continue;
    }

    if (token.startsWith('/FI') && token.length > 3) {
      const resolved = resolvePathFromEntry(entry, token.slice(3));
      forcedInclude.push(resolved);
      filteredCompilerArgs.push(`/FI${resolved}`);
      continue;
    }

    if (token.startsWith('-std=')) {
      standard = token.slice(5);
      filteredCompilerArgs.push(token);
      continue;
    }

    if (token.startsWith('/std:')) {
      standard = token.slice(5);
      filteredCompilerArgs.push(token);
      continue;
    }

    filteredCompilerArgs.push(token);
  }

  const compilerArgs = createCompilerArgs(filteredCompilerArgs, includePath, defines, forcedInclude, standard);
  return {
    includePath: uniqueValues(includePath),
    defines: uniqueValues(defines),
    forcedInclude: forcedInclude.length ? uniqueValues(forcedInclude) : undefined,
    compilerPath,
    compilerArgs: compilerArgs.length ? compilerArgs : undefined,
    standard,
    intelliSenseMode: deriveIntelliSenseMode(compilerPath, compilerArgs, platform)
  };
}

export function computeCompileCommandsPath(outputDir: string): string {
  return path.join(outputDir, '.ngin', 'cmake-build', 'compile_commands.json');
}

export function getFallbackCompileCommandsPath(workspaceRoot: string): string {
  return path.join(workspaceRoot, 'build', 'dev', 'compile_commands.json');
}

export function parseCompileCommands(text: string): CompileCommandEntry[] {
  const parsed = JSON.parse(text) as CompileCommandEntry[];
  return parsed
    .filter((entry) => Boolean(entry?.directory) && Boolean(entry?.file) && (Boolean(entry?.command) || Boolean(entry?.arguments?.length)))
    .map((entry) => ({
      directory: path.resolve(entry.directory),
      file: path.resolve(entry.file),
      output: entry.output,
      command: entry.command,
      arguments: entry.arguments
    }));
}

export function selectCompileCommand(entries: CompileCommandEntry[], filePath: string): CompileCommandEntry | undefined {
  const resolvedFilePath = path.resolve(filePath);
  const exact = entries.find((entry) => comparablePath(entry.file) === comparablePath(resolvedFilePath));
  if (exact) {
    return exact;
  }

  const requestedDirectory = path.dirname(resolvedFilePath);
  let best: CompileCommandEntry | undefined;
  let bestScore = -1;

  for (const entry of entries) {
    const score = commonDirectoryPrefixLength(path.dirname(entry.file), requestedDirectory);
    if (score > bestScore) {
      bestScore = score;
      best = entry;
    }
  }

  return best ?? entries[0];
}

export function createSourceConfiguration(
  entry: CompileCommandEntry,
  filePath: string,
  platform: NodeJS.Platform
): SourceConfigurationModel {
  const configuration = parseCompilerInvocation(entry, platform);
  return {
    ...configuration,
    compilerArgs: configuration.compilerArgs?.filter((argument) => comparablePath(argument) !== comparablePath(filePath))
  };
}

export function createBrowseConfiguration(
  entries: CompileCommandEntry[],
  platform: NodeJS.Platform
): BrowseConfigurationModel | null {
  if (entries.length === 0) {
    return null;
  }

  const parsed = entries.map((entry) => parseCompilerInvocation(entry, platform));
  const browsePath = uniqueValues([
    ...parsed.flatMap((entry) => entry.includePath),
    ...entries.map((entry) => path.dirname(entry.file))
  ]);

  const primary = parsed[0];
  return {
    browsePath,
    compilerPath: primary.compilerPath,
    compilerArgs: primary.compilerArgs,
    standard: primary.standard
  };
}
