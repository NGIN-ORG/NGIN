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
