import * as path from 'node:path';
import { normalizeBuildConfiguration } from './buildConfiguration';
import { ParsedCliDiagnostic, TargetManifest } from './types';

export function basenameWithoutExtension(filePath: string): string {
  const extension = path.extname(filePath);
  return path.basename(filePath, extension);
}

export function computeOutputDir(
  workspaceRoot: string,
  projectName: string,
  variantName: string,
  configuredOutputRoot?: string,
  configurationName?: string
): string {
  const buildConfiguration = normalizeBuildConfiguration(configurationName);
  if (!configuredOutputRoot) {
    return path.join(workspaceRoot, '.ngin', 'build', projectName, variantName, buildConfiguration);
  }

  const outputRoot = path.isAbsolute(configuredOutputRoot)
    ? configuredOutputRoot
    : path.resolve(workspaceRoot, configuredOutputRoot);

  return path.join(outputRoot, projectName, variantName, buildConfiguration);
}

export function computeTargetManifestPath(outputDir: string, projectName: string, variantName: string): string {
  return path.join(outputDir, `${projectName}.${variantName}.ngintarget`);
}

export function looksLikePath(value: string): boolean {
  return /[\\/]/.test(value) || /\.[A-Za-z0-9]+$/.test(value);
}

export function parseCliDiagnostics(output: string): ParsedCliDiagnostic[] {
  const diagnostics: ParsedCliDiagnostic[] = [];
  let section: 'error' | 'warning' | undefined;

  for (const line of output.split(/\r?\n/)) {
    const trimmed = line.trim();
    if (!trimmed) {
      continue;
    }

    if (trimmed.endsWith('errors:')) {
      section = 'error';
      continue;
    }

    if (trimmed === 'Warnings:') {
      section = 'warning';
      continue;
    }

    let body = trimmed;
    let severity: 'error' | 'warning' = section ?? 'error';

    if (body.startsWith('[error] ')) {
      body = body.slice(8);
      severity = 'error';
    } else if (body.startsWith('[warning] ')) {
      body = body.slice(10);
      severity = 'warning';
    } else if (body.startsWith('error: ')) {
      body = body.slice(7);
      severity = 'error';
    } else if (body.startsWith('warning: ')) {
      body = body.slice(9);
      severity = 'warning';
    } else if (body.startsWith('- ')) {
      body = body.slice(2);
    } else if (!section) {
      continue;
    }

    const fileMatch = body.match(/^((?:[A-Za-z]:)?[^:]+):\s+(.+)$/);
    if (fileMatch && looksLikePath(fileMatch[1])) {
      diagnostics.push({
        file: fileMatch[1],
        message: fileMatch[2],
        severity
      });
      continue;
    }

    diagnostics.push({
      message: body,
      severity
    });
  }

  return diagnostics;
}

export function getExecutableCandidatePaths(
  manifest: TargetManifest,
  outputDir: string,
  platform: NodeJS.Platform
): string[] {
  const candidates: string[] = [];
  const selectedName = manifest.selectedExecutable?.name;
  const executableFiles = manifest.stagedFiles.filter((file) => file.kind.toLowerCase() === 'executable');

  const pushUnique = (candidate?: string) => {
    if (!candidate) {
      return;
    }
    if (!candidates.includes(candidate)) {
      candidates.push(candidate);
    }
  };

  const stagedDestination = (relativeOrAbsolute: string): string =>
    path.isAbsolute(relativeOrAbsolute) ? relativeOrAbsolute : path.resolve(outputDir, relativeOrAbsolute);

  if (selectedName) {
    const selectedMatch = executableFiles.find((file) => {
      const label = file.relativeDestination ?? file.destination;
      return basenameWithoutExtension(label) === selectedName;
    });

    if (selectedMatch) {
      pushUnique(stagedDestination(selectedMatch.relativeDestination ?? selectedMatch.destination));
    }
  }

  if (executableFiles.length === 1) {
    const onlyFile = executableFiles[0];
    pushUnique(stagedDestination(onlyFile.relativeDestination ?? onlyFile.destination));
  }

  if (selectedName) {
    const executableName = platform === 'win32' ? `${selectedName}.exe` : selectedName;
    pushUnique(path.join(outputDir, 'bin', executableName));
  }

  return candidates;
}

export function getWorkingDirectoryCandidates(
  manifest: TargetManifest,
  outputDir: string,
  projectDir: string
): string[] {
  const configured = manifest.runtime.workingDirectory?.trim() || '.';
  if (path.isAbsolute(configured)) {
    return [configured];
  }

  return [
    path.resolve(outputDir, configured),
    path.resolve(projectDir, configured),
    outputDir
  ];
}
