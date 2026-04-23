export interface WorkspaceManifest {
  path: string;
  directory: string;
  name: string;
  platformVersion?: string;
  projectPaths: string[];
}

export interface ProjectConfiguration {
  name: string;
  buildConfiguration?: string;
  operatingSystem?: string;
  architecture?: string;
  environment?: string;
  launchExecutable?: string;
  launchWorkingDirectory?: string;
}

export interface ProjectManifest {
  path: string;
  directory: string;
  name: string;
  defaultConfiguration?: string;
  configurations: ProjectConfiguration[];
}

export interface LaunchDescriptor {
  workingDirectory?: string;
  executable?: string;
  target?: string;
  origin?: string;
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
  operatingSystem?: string;
  architecture?: string;
  environmentName?: string;
  launch: LaunchDescriptor;
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
