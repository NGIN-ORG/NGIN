import * as path from 'node:path';
import type { SourceFileConfiguration, WorkspaceBrowseConfiguration } from 'vscode-cpptools';
import type { CompositionGraph } from '../model';
import { normalizeForComparison } from './paths';

export interface CompileCommandEntry {
  directory: string;
  file: string;
  command?: string;
  arguments?: string[];
}

export function splitCommandLine(command: string): string[] {
  const result: string[] = [];
  let current = '';
  let quote: '"' | "'" | undefined;
  for (let index = 0; index < command.length; index++) {
    const character = command[index];
    if (quote) {
      if (character === quote) quote = undefined;
      else if (character === '\\' && quote === '"' && (command[index + 1] === '"' || command[index + 1] === '\\')) {
        current += command[++index];
      }
      else current += character;
    } else if (character === '\\' && command[index + 1] === '"') {
      current += command[++index];
    } else if (character === '"' || character === "'") {
      quote = character;
    } else if (/\s/u.test(character)) {
      if (current) {
        result.push(current);
        current = '';
      }
    } else {
      current += character;
    }
  }
  if (current) result.push(current);
  return result;
}

export function parseCompileCommands(source: string): CompileCommandEntry[] {
  const value = JSON.parse(source) as unknown;
  if (!Array.isArray(value)) throw new Error('compile_commands.json must contain an array.');
  return value.filter((entry): entry is CompileCommandEntry => {
    if (!entry || typeof entry !== 'object') return false;
    const item = entry as Partial<CompileCommandEntry>;
    return typeof item.directory === 'string'
      && typeof item.file === 'string'
      && (typeof item.command === 'string' || Array.isArray(item.arguments));
  });
}

function absolute(directory: string, value: string): string {
  return path.isAbsolute(value) ? path.normalize(value) : path.resolve(directory, value);
}

function entryFile(entry: CompileCommandEntry): string {
  return absolute(entry.directory, entry.file);
}

function commonDirectoryDepth(left: string, right: string): number {
  const leftParts = normalizeForComparison(path.dirname(left)).split(path.sep);
  const rightParts = normalizeForComparison(path.dirname(right)).split(path.sep);
  let index = 0;
  while (index < leftParts.length && index < rightParts.length && leftParts[index] === rightParts[index]) index++;
  return index;
}

export function selectCompileCommand(entries: CompileCommandEntry[], file: string): CompileCommandEntry | undefined {
  const normalized = normalizeForComparison(file);
  const exact = entries.find(entry => normalizeForComparison(entryFile(entry)) === normalized);
  if (exact) return exact;
  return [...entries].sort((left, right) => {
    const rightDepth = commonDirectoryDepth(file, entryFile(right));
    const leftDepth = commonDirectoryDepth(file, entryFile(left));
    return rightDepth - leftDepth || entryFile(left).localeCompare(entryFile(right));
  })[0];
}

function takeValue(args: string[], index: number, prefixes: string[]): { value?: string; consumed: number } {
  const argument = args[index];
  for (const prefix of prefixes) {
    if (argument === prefix) return { value: args[index + 1], consumed: 2 };
    if (argument.startsWith(prefix) && argument.length > prefix.length) return { value: argument.slice(prefix.length), consumed: 1 };
  }
  return { consumed: 0 };
}

function standardValue(value: string): SourceFileConfiguration['standard'] | undefined {
  const normalized = value.replace(/^\/std:/, '').replace(/^-std=/, '').toLowerCase();
  const aliases: Record<string, SourceFileConfiguration['standard']> = {
    'c++latest': 'c++26', 'latest': 'c++26', 'c++2b': 'c++23', 'gnu++2b': 'gnu++23',
    'c++2a': 'c++20', 'gnu++2a': 'gnu++20', 'c2x': 'c23', 'gnu2x': 'gnu23'
  };
  const result = aliases[normalized] ?? normalized;
  const supported = new Set(['c89', 'c99', 'c11', 'c17', 'c23', 'gnu89', 'gnu99', 'gnu11', 'gnu17', 'gnu23',
    'c++98', 'c++03', 'c++11', 'c++14', 'c++17', 'c++20', 'c++23', 'c++26',
    'gnu++98', 'gnu++03', 'gnu++11', 'gnu++14', 'gnu++17', 'gnu++20', 'gnu++23', 'gnu++26']);
  return supported.has(result) ? result as SourceFileConfiguration['standard'] : undefined;
}

function mode(compiler: string, graph: CompositionGraph): SourceFileConfiguration['intelliSenseMode'] {
  const name = path.basename(compiler).toLowerCase();
  const compilerKind = name.includes('cl') && !name.includes('clang') ? 'msvc' : name.includes('clang') ? 'clang' : 'gcc';
  const os = graph.selection.targetOperatingSystem === 'windows' ? 'windows'
    : graph.selection.targetOperatingSystem === 'macos' ? 'macos' : 'linux';
  const rawArchitecture = graph.selection.targetArchitecture.toLowerCase();
  const architecture = rawArchitecture.includes('arm64') || rawArchitecture.includes('aarch64') ? 'arm64'
    : rawArchitecture.includes('arm') ? 'arm' : rawArchitecture.includes('86') && !rawArchitecture.includes('64') ? 'x86' : 'x64';
  if (compilerKind === 'msvc') return `msvc-${architecture}` as SourceFileConfiguration['intelliSenseMode'];
  return `${os}-${compilerKind}-${architecture}` as SourceFileConfiguration['intelliSenseMode'];
}

export function createSourceConfiguration(entry: CompileCommandEntry, graph: CompositionGraph): SourceFileConfiguration {
  const args = entry.arguments?.length ? [...entry.arguments] : splitCommandLine(entry.command ?? '');
  const compilerPath = args.shift();
  const includePath: string[] = [];
  const defines: string[] = [];
  const forcedInclude: string[] = [];
  const compilerArgs: string[] = [];
  let standard: SourceFileConfiguration['standard'] = standardValue(graph.product.languageStandard?.replace(/^C\+\+/, 'c++') ?? 'c++23');

  for (let index = 0; index < args.length;) {
    const argument = args[index];
    const include = takeValue(args, index, ['-I', '-isystem', '/I']);
    if (include.consumed) {
      if (include.value) includePath.push(absolute(entry.directory, include.value));
      index += include.consumed;
      continue;
    }
    const define = takeValue(args, index, ['-D', '/D']);
    if (define.consumed) {
      if (define.value) defines.push(define.value);
      index += define.consumed;
      continue;
    }
    const forced = takeValue(args, index, ['-include', '/FI']);
    if (forced.consumed) {
      if (forced.value) forcedInclude.push(absolute(entry.directory, forced.value));
      index += forced.consumed;
      continue;
    }
    if (argument.startsWith('-std=') || argument.startsWith('/std:')) {
      standard = standardValue(argument) ?? standard;
      index++;
      continue;
    }
    const skipPair = argument === '-o' || argument === '/Fo' || argument === '/Fd';
    const isInput = normalizeForComparison(absolute(entry.directory, argument)) === normalizeForComparison(entryFile(entry));
    if (!isInput && argument !== '-c' && argument !== '/c' && !argument.startsWith('/Fo') && !argument.startsWith('/Fd')) {
      compilerArgs.push(argument);
    }
    index += skipPair ? 2 : 1;
  }

  return {
    includePath: [...new Set(includePath)],
    defines: [...new Set(defines)],
    forcedInclude: [...new Set(forcedInclude)],
    compilerPath,
    compilerArgs,
    standard,
    intelliSenseMode: compilerPath ? mode(compilerPath, graph) : undefined
  };
}

export function createFallbackConfiguration(graph: CompositionGraph, projectDirectory: string): SourceFileConfiguration {
  const includePath = graph.buildItems
    .filter(item => item.kind === 'IncludeDirectory')
    .map(item => absolute(projectDirectory, item.path));
  const defines = graph.buildItems
    .filter(item => item.kind === 'Define')
    .map(item => item.value === undefined ? item.path : `${item.path}=${item.value}`);
  const forcedInclude = graph.buildItems
    .filter(item => item.kind === 'PrecompiledHeader')
    .map(item => absolute(projectDirectory, item.path));
  return {
    includePath: [...new Set([projectDirectory, ...includePath])],
    defines: [...new Set(defines)],
    forcedInclude: [...new Set(forcedInclude)],
    standard: standardValue(graph.product.languageStandard?.replace(/^C\+\+/, 'c++') ?? 'c++23')
  };
}

export function createBrowseConfiguration(
  entries: CompileCommandEntry[],
  graph: CompositionGraph,
  projectDirectory: string
): WorkspaceBrowseConfiguration {
  const configurations = entries.map(entry => createSourceConfiguration(entry, graph));
  const fallback = createFallbackConfiguration(graph, projectDirectory);
  return {
    browsePath: [...new Set([projectDirectory, ...fallback.includePath, ...configurations.flatMap(item => item.includePath)])],
    compilerPath: configurations.find(item => item.compilerPath)?.compilerPath,
    compilerArgs: configurations.find(item => item.compilerArgs?.length)?.compilerArgs,
    standard: configurations.find(item => item.standard)?.standard ?? fallback.standard
  };
}
