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
  tools?: ToolDeclaration[];
  features?: PackageFeature[];
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

export interface PackageFeatureUse extends SelectorFields {
  packageName: string;
  featureName: string;
  version?: string;
  disabled?: boolean;
}

export interface PackageCapability {
  name: string;
  exclusive?: boolean;
}

export interface PackageFeature extends SelectorFields {
  name: string;
  description?: string;
  provides?: PackageCapability[];
  requires?: PackageCapability[];
  dependencies?: PackageReference[];
  inputs?: InputDeclaration[];
  generators?: GeneratorDeclaration[];
}

export interface ToolDeclaration extends SelectorFields {
  name?: string;
  kind?: string;
  executable?: string;
}

export interface GeneratorArgument extends SelectorFields {
  value?: string;
  path?: string;
}

export interface GeneratorDeclaration extends SelectorFields {
  name: string;
  kind: string;
  packageName?: string;
  toolName?: string;
  inlineTool?: ToolDeclaration;
  inputs?: InputDeclaration[];
  outputs?: InputDeclaration[];
  arguments?: GeneratorArgument[];
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
  packageFeatureUses?: PackageFeatureUse[];
  generators?: GeneratorDeclaration[];
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
  packageFeatureUses?: PackageFeatureUse[];
  generators?: GeneratorDeclaration[];
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

export interface InspectDiagnostic {
  severity: 'error' | 'warning';
  subject?: string;
  message: string;
}

export interface InspectPackage {
  name: string;
  version?: string;
  manifestPath?: string;
  providerRoot?: string;
  source?: string;
  requiredBy?: string[];
}

export interface InspectPackageDependencyEdge {
  from: string;
  to: string;
}

export type InspectPackageFeatureState = 'selected' | 'available' | 'disabled' | 'conditionExcluded' | 'unavailable' | string;

export interface InspectPackageFeature {
  package: string;
  packageVersion?: string;
  feature: string;
  state: InspectPackageFeatureState;
  description?: string;
  manifestPath?: string;
}

export interface InspectCapabilityProvider {
  name: string;
  package: string;
  feature: string;
  exclusive?: boolean;
}

export interface InspectCapabilityRequirement {
  name: string;
  package: string;
  feature: string;
  missing?: boolean;
}

export interface InspectCapabilities {
  providers: InspectCapabilityProvider[];
  requirements: InspectCapabilityRequirement[];
  missingRequirements?: string[];
  exclusiveConflicts?: string[];
}

export interface InspectGeneratorOutput {
  role?: string;
  path?: string;
  target?: string;
}

export interface InspectGenerator {
  name: string;
  kind?: string;
  state: 'active' | 'excluded' | string;
  ownerKind?: string;
  ownerName?: string;
  package?: string;
  tool?: string;
  manifestPath?: string;
  reason?: string;
  outputs?: InspectGeneratorOutput[];
}

export interface InspectInput {
  name?: string;
  role?: string;
  mode?: string;
  source?: string;
  absoluteSourcePath?: string;
  visibility?: string;
  target?: string;
  targetRoot?: string;
  stagedRelativePath?: string;
  ownerKind?: string;
  ownerName?: string;
  manifestPath?: string;
}

export interface InspectLaunch {
  executable?: LaunchExecutable | null;
  workingDirectory?: string;
}

export interface InspectStagedFile {
  kind: string;
  source?: string;
  relativeDestination?: string;
}

export interface InspectEnvironmentVariable {
  name: string;
  value?: string;
  secret?: boolean;
  resolved?: boolean;
  source?: string;
}

export interface InspectLockFile {
  path?: string | null;
  status?: string;
}

export interface ProjectInspectPayload {
  schemaVersion: 1;
  project?: {
    name?: string;
    path?: string;
    type?: string;
  };
  profile?: {
    name?: string;
    buildType?: string;
    platform?: string;
    operatingSystem?: string;
    architecture?: string;
    environment?: string;
  };
  workspace?: {
    name?: string;
    path?: string;
  } | null;
  outputDir?: string;
  packages?: InspectPackage[];
  packageDependencyEdges?: InspectPackageDependencyEdge[];
  packageFeatures?: InspectPackageFeature[];
  capabilities?: InspectCapabilities;
  generators?: InspectGenerator[];
  inputs?: Record<string, InspectInput[]>;
  launch?: InspectLaunch;
  stagedFiles?: InspectStagedFile[];
  environmentVariables?: InspectEnvironmentVariable[];
  lockFile?: InspectLockFile;
  diagnostics?: InspectDiagnostic[];
}
