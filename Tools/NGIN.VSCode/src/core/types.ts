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
  inputs?: InputDeclaration[];
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
  conditions?: ConditionDefinition[];
}

export interface SelectorFields {
  profile?: string;
  platform?: string;
  operatingSystem?: string;
  architecture?: string;
  buildType?: string;
  environment?: string;
  condition?: string;
}

export interface ConditionDefinition extends SelectorFields {
  name: string;
}

export interface ProjectReference extends Omit<SelectorFields, 'profile'> {
  path: string;
  profile?: string;
}

export interface PackageReference extends SelectorFields {
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
  inputs?: InputDeclaration[];
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
  inputs?: InputDeclaration[];
  sourceRoots: string[];
  configInputs: string[];
  localSettingsImports?: string[];
  buildSources: string[];
  conditions?: ConditionDefinition[];
  projectRefs?: ProjectReference[];
  packageRefs?: PackageReference[];
  profiles: ProjectProfile[];
}

export interface InputDeclaration {
  name?: string;
  kind: 'Source' | 'Config' | 'Content' | 'Asset' | 'Generated' | 'ToolInput' | string;
  role?: 'Source' | 'Header' | 'Content' | 'Asset' | 'ToolInput' | string;
  path?: string;
  pattern?: string;
  mode?: 'Directory' | 'File' | 'Glob' | string;
  visibility?: 'Public' | 'Private' | 'Interface' | string;
  target?: string;
  targetRoot?: string;
  basePath?: string;
  contentKind?: string;
  required?: boolean;
  profile?: string;
  platform?: string;
  operatingSystem?: string;
  architecture?: string;
  buildType?: string;
  environment?: string;
  condition?: string;
  include?: string[];
  exclude?: string[];
  setName?: string;
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
