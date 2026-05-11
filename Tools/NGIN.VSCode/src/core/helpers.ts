import * as path from 'node:path';
import { LaunchManifest, ParsedCliDiagnostic, ProjectInspectPayload } from './types';

export function basenameWithoutExtension(filePath: string): string {
  const extension = path.extname(filePath);
  return path.basename(filePath, extension);
}

export function computeOutputDir(
  workspaceRoot: string,
  projectName: string,
  profileName: string,
  configuredOutputRoot?: string
): string {
  if (!configuredOutputRoot) {
    return path.join(workspaceRoot, '.ngin', 'build', projectName, profileName);
  }

  const outputRoot = path.isAbsolute(configuredOutputRoot)
    ? configuredOutputRoot
    : path.resolve(workspaceRoot, configuredOutputRoot);

  return path.join(outputRoot, projectName, profileName);
}

export function computeLaunchManifestPath(outputDir: string, projectName: string, profileName: string): string {
  return path.join(outputDir, `${projectName}.${profileName}.nginlaunch`);
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

    const locationMatch = body.match(/^((?:[A-Za-z]:)?[^:]+):(\d+):(\d+):\s+(.+)$/);
    if (locationMatch && looksLikePath(locationMatch[1])) {
      diagnostics.push({
        file: locationMatch[1],
        line: Number(locationMatch[2]),
        column: Number(locationMatch[3]),
        message: locationMatch[4],
        severity
      });
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

function asRecord(value: unknown): Record<string, unknown> | undefined {
  return typeof value === 'object' && value !== null ? value as Record<string, unknown> : undefined;
}

function asArray(value: unknown): Record<string, unknown>[] {
  return Array.isArray(value) ? value.map(asRecord).filter((entry): entry is Record<string, unknown> => Boolean(entry)) : [];
}

function asString(value: unknown): string | undefined {
  return typeof value === 'string' ? value : undefined;
}

function asDiagnosticSeverity(value: unknown): 'error' | 'warning' {
  return value === 'warning' ? 'warning' : 'error';
}

function normalizeInspectGraphPayload(payload: Record<string, unknown>): ProjectInspectPayload {
  const identity = asRecord(payload.identity);
  const product = asRecord(payload.product);
  const selection = asRecord(payload.selection);
  const plans = asRecord(payload.plans);
  const build = asRecord(plans?.build);
  const stage = asRecord(plans?.stage);
  const runtime = asRecord(plans?.runtime);
  const environment = asRecord(plans?.environment);
  const launch = asRecord(plans?.launch);
  const quality = asRecord(plans?.quality);

  return {
    schemaVersion: 1,
    project: {
      name: asString(identity?.project),
      path: asString(identity?.projectPath),
      type: asString(product?.kind)
    },
    profile: {
      name: asString(selection?.profile),
      buildType: asString(selection?.buildType),
      platform: asString(selection?.targetPlatform),
      operatingSystem: asString(selection?.operatingSystem),
      architecture: asString(selection?.architecture),
      environment: asString(selection?.environment)
    },
    workspace: asRecord(payload.workspace) as ProjectInspectPayload['workspace'],
    packages: asArray(plans?.packages).map((pkg) => ({
      name: asString(pkg.name) ?? '',
      version: asString(pkg.version),
      providerRoot: asString(pkg.providerRoot),
      source: asString(pkg.source),
      requiredBy: Array.isArray(pkg.closures) ? pkg.closures.map(String) : undefined
    })).filter((pkg) => pkg.name.length > 0),
    packageFeatures: asArray(plans?.packageFeatures).map((feature) => ({
      package: asString(feature.package) ?? '',
      packageVersion: asString(feature.packageVersion),
      feature: asString(feature.feature) ?? '',
      state: 'selected'
    })).filter((feature) => feature.package.length > 0 && feature.feature.length > 0),
    generators: asArray(plans?.generators).map((generator) => ({
      name: asString(generator.name) ?? '',
      state: asString(generator.state) ?? 'active',
      ownerName: asString(generator.ownerName),
      package: asString(generator.package),
      tool: asString(generator.tool),
      reason: asString(generator.reason)
    })).filter((generator) => generator.name.length > 0),
    inputs: {
      Source: asArray(build?.inputs).map((input) => ({
        role: asString(input.role),
        source: asString(input.source),
        ownerName: asString(input.owner)
      })).filter((input) => Boolean(input.source))
    },
    launch: launch ? {
      executable: asString(launch.executable) ? { name: asString(launch.executable) } : undefined,
      workingDirectory: asString(launch.workingDirectory)
    } : undefined,
    stagedFiles: asArray(stage?.files).map((file) => ({
      kind: asString(file.kind) ?? 'file',
      source: asString(file.source),
      relativeDestination: asString(file.target) ?? asString(file.relativeDestination)
    })),
    environmentVariables: asArray(environment?.variables).map((variable) => ({
      name: asString(variable.name) ?? '',
      value: asString(variable.value),
      secret: variable.secret === true,
      resolved: variable.resolved === true,
      source: asString(variable.source)
    })).filter((variable) => variable.name.length > 0),
    diagnostics: asArray(plans?.diagnostics).map((diagnostic) => ({
      severity: asDiagnosticSeverity(diagnostic.severity),
      subject: asString(diagnostic.subject),
      message: asString(diagnostic.message) ?? ''
    })).filter((diagnostic) => diagnostic.message.length > 0),
    capabilities: {
      providers: [
        ...asArray(runtime?.requiredModules).map((module) => ({
          name: asString(module.name) ?? '',
          package: '',
          feature: ''
        })),
        ...asArray(quality?.analyzers).map((analyzer) => ({
          name: asString(analyzer.name) ?? '',
          package: asString(analyzer.package) ?? '',
          feature: ''
        }))
      ].filter((provider) => provider.name.length > 0),
      requirements: []
    }
  };
}

export function normalizeInspectPayload(payload: unknown): ProjectInspectPayload {
  const record = asRecord(payload);
  if (!record) {
    throw new Error('inspect returned a non-object payload');
  }

  if (record.schemaVersion === 1) {
    return record as unknown as ProjectInspectPayload;
  }

  if (record.schemaVersion === '4.0' && record.kind === 'NGIN.CompositionGraph') {
    return normalizeInspectGraphPayload(record);
  }

  throw new Error(`unsupported inspect schema version: ${String(record.schemaVersion)}`);
}

export function extractInitializedSettingsPath(output: string): string | undefined {
  for (const line of output.split(/\r?\n/)) {
    const match = line.match(/^\s*settings:\s+("?)(.*?)\1\s+\[(?:created|exists)\]\s*$/);
    if (match?.[2]) {
      return match[2].trim();
    }
  }
  return undefined;
}

export function extractLocalSettingsWarnings(output: string): string[] {
  return output
    .split(/\r?\n/)
    .map((line) => line.trim().replace(/^\-\s+/, ''))
    .filter((line) =>
      /local settings/i.test(line) &&
      (/tracked by git/i.test(line) || /should be ignored/i.test(line) || /not ignored/i.test(line))
    );
}

export function getExecutableCandidatePaths(
  manifest: LaunchManifest,
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
  manifest: LaunchManifest,
  outputDir: string,
  projectDir: string
): string[] {
  const configured = manifest.launch.workingDirectory?.trim() || '.';
  if (path.isAbsolute(configured)) {
    return [configured];
  }

  return [
    path.resolve(outputDir, configured),
    path.resolve(projectDir, configured),
    outputDir
  ];
}
