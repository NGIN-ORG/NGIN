export interface WorkspaceManifest {
  path: string;
  directory: string;
  name: string;
  platformVersion?: string;
  imports?: string[];
  projectPaths: string[];
  packageSourcePaths?: string[];
}

export interface SelectionDefaults {
  buildType?: string;
  targetPlatform?: string;
  hostPlatform?: string;
  operatingSystem?: string;
  architecture?: string;
  environment?: string;
  toolchain?: string;
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
  name?: string;
  path: string;
  profile?: string;
}

export interface DependencyUse extends SelectorFields {
  name: string;
  version?: string;
  scope?: string;
  kind?: 'Package' | 'Runtime' | 'Tool' | string;
  optional?: boolean;
  features?: string[];
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
  dependencies?: DependencyUse[];
  inputs?: InputDeclaration[];
  generators?: GeneratorDeclaration[];
}

export interface ToolDeclaration extends SelectorFields {
  name?: string;
  kind?: string;
  executable?: string;
  packageName?: string;
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
  extends?: string;
  buildType?: string;
  platform?: string;
  targetPlatform?: string;
  hostPlatform?: string;
  operatingSystem?: string;
  architecture?: string;
  environment?: string;
  toolchain?: string;
  launchExecutable?: string;
  launchWorkingDirectory?: string;
  inputs?: InputDeclaration[];
  configInputs: string[];
  projectRefs?: ProjectReference[];
  dependencies?: DependencyUse[];
  packageFeatureUses?: PackageFeatureUse[];
  generators?: GeneratorDeclaration[];
}

export interface ProjectManifest {
  path: string;
  directory: string;
  name: string;
  productKind?: string;
  defaultProfile?: string;
  inputs?: InputDeclaration[];
  sourceRoots: string[];
  configInputs: string[];
  localSettingsImports?: string[];
  buildSources: string[];
  conditions?: ConditionDefinition[];
  projectRefs?: ProjectReference[];
  dependencies?: DependencyUse[];
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
  line?: number;
  column?: number;
  message: string;
  severity: 'error' | 'warning';
  source?: string;
}

export interface LaunchResolution {
  executableCandidates: string[];
  workingDirectoryCandidates: string[];
}

export interface GraphDiagnostic {
  severity: 'error' | 'warning';
  subject?: string;
  message: string;
}

export interface GraphProvenance {
  sourceKind?: string;
  sourceName?: string;
  manifestPath?: string;
  reason?: string;
}

export interface GraphConvention {
  name: string;
  reason?: string;
  provenance?: GraphProvenance;
}

export interface GraphProperty {
  name: string;
  value?: string;
  provenance?: GraphProvenance;
}

export interface GraphPackagePlan {
  name: string;
  version?: string;
  manifestPath?: string;
  providerRoot?: string;
  source?: string;
  provider?: string;
  providerKind?: string;
  providerPackage?: string;
  providerVersion?: string;
  scope?: string;
  closures?: string[];
  dependencies?: string[];
  provenance?: GraphProvenance;
}

export interface GraphPackageFeaturePlan {
  package: string;
  packageVersion?: string;
  feature: string;
  description?: string;
  manifestPath?: string;
  provenance?: GraphProvenance;
}

export interface GraphGeneratorOutput {
  role?: string;
  path?: string;
  target?: string;
}

export interface GraphGeneratorPlan {
  name: string;
  kind?: string;
  state?: 'active' | 'excluded' | string;
  ownerKind?: string;
  ownerName?: string;
  package?: string;
  tool?: string;
  toolName?: string;
  manifestPath?: string;
  reason?: string;
  outputs?: GraphGeneratorOutput[] | number;
  provenance?: GraphProvenance;
}

export interface GraphBuildInput {
  name?: string;
  kind?: string;
  role?: string;
  mode?: string;
  source?: string;
  owner?: string;
  absoluteSourcePath?: string;
  visibility?: string;
  target?: string;
  targetRoot?: string;
  stagedRelativePath?: string;
  ownerKind?: string;
  ownerName?: string;
  manifestPath?: string;
  provenance?: GraphProvenance;
}

export interface GraphBuildDefine {
  name?: string;
  value?: string;
  provenance?: GraphProvenance;
}

export interface GraphLaunchPlan {
  name?: string;
  executable?: string;
  workingDirectory?: string;
  args?: string;
  selected?: boolean;
  provenance?: GraphProvenance;
}

export interface GraphStagedFile {
  kind?: string;
  source?: string;
  target?: string;
  owner?: string;
  relativeDestination?: string;
  provenance?: GraphProvenance;
}

export interface GraphEnvironmentVariable {
  name: string;
  value?: string;
  secret?: boolean;
  resolved?: boolean;
  source?: string;
  provenance?: GraphProvenance;
}

export interface GraphRuntimeModulePlan {
  name?: string;
  stage?: string;
  order?: string | number;
  provenance?: GraphProvenance;
}

export interface GraphRuntimePluginPlan {
  name?: string;
  target?: string;
  load?: string;
  provenance?: GraphProvenance;
}

export interface GraphPublishPlan {
  name?: string;
  kind?: string;
  format?: string;
  output?: string;
  includeStage?: boolean;
  includeRuntimeDependencies?: boolean;
  includeSymbols?: boolean;
  provenance?: GraphProvenance;
}

export interface GraphPackageOutputPlan {
  name?: string;
  version?: string;
  from?: string;
  output?: string;
  headers?: number;
  libraries?: number;
  tools?: number;
  capabilities?: number;
  abi?: string;
  provenance?: GraphProvenance;
}

export interface GraphAnalyzerPlan {
  name: string;
  tool?: string;
  package?: string;
  scope?: string;
  severity?: 'error' | 'warning' | 'Error' | 'Warning' | string;
  configPath?: string;
  configOptional?: boolean;
  provenance?: GraphProvenance;
}

export interface CompositionGraphPayload {
  schemaVersion: '4.0';
  kind: 'NGIN.CompositionGraph';
  state?: string;
  facets?: string[];
  identity?: {
    project?: string;
    projectPath?: string;
    product?: string;
    profile?: string;
  };
  product?: {
    kind?: string;
    outputType?: string;
    outputName?: string;
    targetName?: string;
  };
  conventions?: GraphConvention[];
  properties?: GraphProperty[];
  selection?: {
    profile?: string;
    buildType?: string;
    platform?: string;
    targetPlatform?: string;
    hostPlatform?: string;
    operatingSystem?: string;
    architecture?: string;
    environment?: string;
    toolchain?: string;
    abiTag?: string;
  };
  workspace?: {
    name?: string;
    path?: string;
  } | null;
  outputDir?: string;
  facetsSummary?: Record<string, number>;
  plans?: {
    packages?: GraphPackagePlan[];
    packageFeatures?: GraphPackageFeaturePlan[];
    build?: {
      inputs?: GraphBuildInput[];
      defines?: Array<string | GraphBuildDefine>;
    };
    generators?: GraphGeneratorPlan[];
    stage?: {
      files?: GraphStagedFile[];
    };
    runtime?: {
      requiredModules?: Array<string | GraphRuntimeModulePlan>;
      optionalModules?: Array<string | GraphRuntimeModulePlan>;
      plugins?: Array<string | GraphRuntimePluginPlan>;
    };
    environment?: {
      variables?: GraphEnvironmentVariable[];
    };
    launch?: GraphLaunchPlan;
    launches?: GraphLaunchPlan[];
    publish?: GraphPublishPlan[];
    packageOutputs?: GraphPackageOutputPlan[];
    quality?: {
      analyzers?: GraphAnalyzerPlan[];
    };
    diagnostics?: GraphDiagnostic[];
  };
}
