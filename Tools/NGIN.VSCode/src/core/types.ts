export interface WorkspaceManifest {
  path: string;
  directory: string;
  name: string;
  platformVersion?: string;
  projectPaths: string[];
}

export interface ProjectVariant {
  name: string;
  profile?: string;
  platform?: string;
  environment?: string;
  workingDirectory?: string;
  launchExecutable?: string;
}

export interface ProjectManifest {
  path: string;
  directory: string;
  name: string;
  defaultVariant?: string;
  variants: ProjectVariant[];
}

export interface TargetRuntime {
  workingDirectory?: string;
  environment?: string;
}

export interface TargetExecutable {
  name: string;
  target?: string;
  origin?: string;
}

export interface StagedFile {
  kind: string;
  source?: string;
  destination: string;
  relativeDestination?: string;
}

export interface TargetManifest {
  path: string;
  directory: string;
  project: string;
  variant: string;
  runtime: TargetRuntime;
  selectedExecutable?: TargetExecutable;
  stagedFiles: StagedFile[];
}

export interface ParsedCliDiagnostic {
  file?: string;
  message: string;
  severity: 'error' | 'warning';
}

export interface LaunchResolution {
  executableCandidates: string[];
  workingDirectoryCandidates: string[];
}
