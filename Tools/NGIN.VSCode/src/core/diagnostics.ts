import type { NginDiagnostic } from '../model';

const diagnosticPattern = /^(.*):(\d+):(\d+):\s+(error|warning)\s+(NGIN\d+):\s+(.+)$/;

export function parseCliDiagnostics(stderr: string): NginDiagnostic[] {
  const diagnostics: NginDiagnostic[] = [];
  for (const line of stderr.split(/\r?\n/)) {
    const match = diagnosticPattern.exec(line.trim());
    if (!match) {
      const hint = /^\s*hint:\s+(.+)$/.exec(line);
      if (hint && diagnostics.length > 0) diagnostics[diagnostics.length - 1].hint = hint[1];
      continue;
    }
    diagnostics.push({
      path: match[1],
      line: Number(match[2]),
      column: Number(match[3]),
      severity: match[4] as 'error' | 'warning',
      code: match[5],
      message: match[6]
    });
  }
  return diagnostics;
}

const clangDiagnosticPattern = /^(.*):(\d+):(\d+):\s+(error|warning):\s+(.+?)(?:\s+\[([^\]]+)\])?$/u;
const msvcDiagnosticPattern = /^(.*)\((\d+),(\d+)\):\s+(error|warning)\s+([A-Z]+\d+):\s+(.+)$/u;

export function parseCompilerDiagnostics(output: string): NginDiagnostic[] {
  const diagnostics: NginDiagnostic[] = [];
  for (const raw of output.split(/\r?\n/u)) {
    const line = raw.trim();
    const clang = clangDiagnosticPattern.exec(line);
    if (clang) {
      diagnostics.push({
        path: clang[1], line: Number(clang[2]), column: Number(clang[3]),
        severity: clang[4] as 'error' | 'warning', message: clang[5], code: clang[6]
      });
      continue;
    }
    const msvc = msvcDiagnosticPattern.exec(line);
    if (msvc) diagnostics.push({
      path: msvc[1], line: Number(msvc[2]), column: Number(msvc[3]),
      severity: msvc[4] as 'error' | 'warning', code: msvc[5], message: msvc[6]
    });
  }
  return diagnostics;
}
