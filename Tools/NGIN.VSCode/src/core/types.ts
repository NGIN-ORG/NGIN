export interface WorkspaceManifest {
  path: string;
  directory: string;
  name: string;
  platformVersion?: string;
  projectPaths: string[];
}

export interface ProjectConfiguration {
  name: string;
  hostProfile?: string;
  buildConfiguration?: string;
  platform?: string;
  environment?: string;
  workingDirectory?: string;
  launchExecutable?: string;
}

export interface ProjectManifest {
  path: string;
  directory: string;
  name: string;
  defaultConfiguration?: string;
  configurations: ProjectConfiguration[];
}

export interface LaunchRuntime {
  workingDirectory?: string;
  environment?: string;
}

export interface LaunchExecutable {
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

export interface LaunchManifest {
  path: string;
  directory: string;
  project: string;
  configuration: string;
  type?: string;
  buildConfiguration?: string;
  hostProfile?: string;
  platform?: string;
  runtime: LaunchRuntime;
  selectedExecutable?: LaunchExecutable;
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
