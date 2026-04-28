export interface WorkspaceManifest {
  path: string;
  directory: string;
  name: string;
  platformVersion?: string;
  modelIncludes?: string[];
  defaults?: ModelDefaults;
  profileTemplates?: Record<string, ProjectProfileTemplate>;
  projectPaths: string[];
  packageSourcePaths?: string[];
}

export interface ModelDefaults {
  buildType?: string;
  platform?: string;
  operatingSystem?: string;
  architecture?: string;
  environment?: string;
}

export interface ProjectProfileTemplate {
  name: string;
  extends?: string;
  buildType?: string;
  platform?: string;
  operatingSystem?: string;
  architecture?: string;
  environment?: string;
  launchExecutable?: string;
  launchWorkingDirectory?: string;
  configInputs: string[];
  projectRefs?: ProjectReference[];
  packageRefs?: PackageReference[];
}

export interface PackageCatalogEntry {
  name: string;
  path: string;
  directory: string;
}

export interface PackageManifest {
  path: string;
  directory: string;
  name: string;
  version?: string;
}

export interface ProjectReference {
  path: string;
  profile?: string;
}

export interface PackageReference {
  name: string;
  version?: string;
  optional?: boolean;
}

export interface ProjectProfile {
  name: string;
  buildType?: string;
  platform?: string;
  operatingSystem?: string;
  architecture?: string;
  environment?: string;
  launchExecutable?: string;
  launchWorkingDirectory?: string;
  configInputs: string[];
  projectRefs?: ProjectReference[];
  packageRefs?: PackageReference[];
}

export interface ProjectManifest {
  path: string;
  directory: string;
  name: string;
  defaultProfile?: string;
  modelIncludes?: string[];
  defaults?: ModelDefaults;
  sourceRoots: string[];
  configInputs: string[];
  localSettingsImports?: string[];
  buildSources: string[];
  projectRefs?: ProjectReference[];
  packageRefs?: PackageReference[];
  profiles: ProjectProfile[];
}

export interface LocalSettingEntry {
  key: string;
  secret?: boolean;
}

export interface LocalSettingsManifest {
  path: string;
  directory: string;
  settings: LocalSettingEntry[];
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
  profile: string;
  type?: string;
  buildType?: string;
  platform?: string;
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
