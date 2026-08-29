export interface SourceLocation {
  path: string;
  line: number;
  column: number;
}

export interface NginDiagnostic extends SourceLocation {
  severity: 'error' | 'warning';
  code?: string;
  message: string;
  hint?: string;
}

export interface NginContext {
  projectId?: string;
  projectSystem?: 'Ngin' | 'CMake';
  workspaceFolder: string;
  workspaceManifest?: string;
  projectManifest: string;
  projectName: string;
  configuration: string;
  target: string;
  toolchain: string;
  run?: string;
  profile?: string;
  options: Readonly<Record<string, string>>;
  outputDirectory: string;
  configurePreset?: string;
}

export interface WorkspaceChoices {
  name: string;
  configurations: string[];
  targets: string[];
  toolchains: string[];
  profiles: Array<{
    name: string;
    configuration?: string;
    target?: string;
    toolchain?: string;
    run?: string;
  }>;
  defaults: {
    configuration?: string;
    target?: string;
    toolchain?: string;
  };
}

export interface ProjectCandidate {
  id?: string;
  projectSystem?: 'Ngin' | 'CMake';
  capabilities?: readonly string[];
  manifest: string;
  directory: string;
  root?: string;
  name: string;
  artifactKind?: 'Executable' | 'Library';
  libraryKind?: 'Static' | 'Shared' | 'Interface' | 'Plugin';
  workspaceManifest?: string;
  workspaceChoices?: WorkspaceChoices;
  hasAnalyze?: boolean;
  hasFormat?: boolean;
  hasTests?: boolean;
  hasBenchmarks?: boolean;
  hasRun?: boolean;
}

export interface ActionDiagnosticPoint {
  line: number;
  column: number;
}

export interface ActionDiagnostic {
  file: string;
  range: { start: ActionDiagnosticPoint; end: ActionDiagnosticPoint };
  severity: 'error' | 'warning' | 'information';
  source: string;
  code?: string;
  message: string;
  fixes?: ActionDiagnosticFix[];
}

export interface ActionDiagnosticFixEdit {
  file?: string;
  range: { start: ActionDiagnosticPoint; end: ActionDiagnosticPoint };
  text: string;
}

export interface ActionDiagnosticFix {
  title: string;
  safe?: boolean;
  edits: ActionDiagnosticFixEdit[];
}

export interface ActionDiagnosticsEnvelope {
  kind: 'NGIN.ActionDiagnostics';
  state: 'complete';
  diagnostics: ActionDiagnostic[];
}

export interface DiscoveryResult {
  workspaceId?: string;
  workspaceFolder: string;
  workspaceManifest?: string;
  workspaceChoices?: WorkspaceChoices;
  packages?: WorkspacePackage[];
  projects: ProjectCandidate[];
}

export interface WorkspacePackage {
  name: string;
  manifest: string;
  developmentProjectId?: string;
  consumingProjectIds: string[];
  exportedTargets: string[];
}

export interface CMakePresetDescription {
  name: string;
  displayName: string;
  description: string;
  configurePreset?: string;
}

export interface CMakeSourceDescription {
  path: string;
  compileGroup?: string;
  declaration?: string;
  declarationLine?: number;
}

export interface CMakeTargetDescription {
  id: string;
  name: string;
  type: string;
  sources: CMakeSourceDescription[];
  compileGroups: Array<{
    id: string;
    language: string;
    compileCommandFragments: string[];
    includes: string[];
    defines: string[];
  }>;
  dependencies: string[];
  artifacts: string[];
  declaration?: string;
  declarationLine?: number;
}

export interface CMakeProjectSnapshot {
  kind: 'NGIN.EditorCMakeProjectSnapshot';
  version: 2;
  state: 'ready' | 'degraded';
  project: { id: string; name: string; projectSystem: 'CMake'; root: string };
  capabilities: string[];
  cmake: {
    buildDirectory: string;
    configurations: string[];
    configurePreset: string;
    configuration: string;
    configured: boolean;
    directories: string[];
    stale: boolean;
    multiConfig: boolean;
    configurePresets: CMakePresetDescription[];
    buildPresets: CMakePresetDescription[];
    testPresets: CMakePresetDescription[];
    toolchains: Array<{
      language: string;
      compilerPath: string;
      compilerId: string;
      compilerVersion: string;
      target: string;
    }>;
  };
  targets: CMakeTargetDescription[];
  tests: Array<{ name: string }>;
  diagnostics: string[];
}

export interface GraphProvenance {
  kind?: string;
  owner?: string;
  document?: string;
  line?: number;
  column?: number;
  reason?: string;
}

export interface GraphProduct {
  identity: string;
  name: string;
  artifactKind: 'Executable' | 'Library';
  libraryKind: 'None' | 'Static' | 'Shared' | 'Interface' | 'Plugin';
  version?: string;
  languageStandard?: string;
  languageExtensions?: boolean;
  languageRequired?: boolean;
  provenance?: GraphProvenance;
}

export interface GraphSelection {
  identity: string;
  configuration: string;
  targetOperatingSystem: string;
  targetArchitecture: string;
  compiler: string;
  compilerVersion?: string;
  runtimeLibrary?: string;
  optimization?: string;
  debugSymbols?: boolean;
  linkTimeOptimization?: boolean;
  provenance?: GraphProvenance;
}

export interface GraphBuildItem {
  identity: string;
  kind: string;
  path: string;
  value?: string;
  visibility?: string;
  generated?: boolean;
  provenance?: GraphProvenance;
}

export interface GraphPackage {
  identity: string;
  name?: string;
  version?: string;
  context?: string;
  providerKind?: string;
  providerIdentity?: string;
  artifactIdentity?: string;
  provenance?: GraphProvenance;
  [key: string]: unknown;
}

export interface GraphExport {
  identity: string;
  name?: string;
  kind?: string;
  package?: string;
  provenance?: GraphProvenance;
  [key: string]: unknown;
}

export interface GraphOption {
  identity: string;
  name?: string;
  owner?: string;
  type?: string;
  value?: unknown;
  artifact?: boolean;
  provenance?: GraphProvenance;
  [key: string]: unknown;
}

export interface GraphNamedNode {
  identity: string;
  name?: string;
  kind?: string;
  provenance?: GraphProvenance;
  [key: string]: unknown;
}

export interface GraphRun extends GraphNamedNode {
  default?: boolean;
  executableKind?: string;
  executable?: string;
  workingDirectory?: string;
  arguments?: string[];
  environment?: Record<string, string>;
  secrets?: Record<string, string>;
}

export interface GraphTestRegistration extends GraphNamedNode {
  arguments?: string[];
  environment?: Record<string, string>;
  timeoutSeconds?: number;
}

export interface GraphBenchmarkRegistration extends GraphTestRegistration {
  repetitions?: number;
  warmupSeconds?: number;
}

export interface GraphEdge {
  identity: string;
  from: string;
  to: string;
  kind: string;
  [key: string]: unknown;
}

export interface CompositionGraph {
  kind: 'NGIN.CompositionGraph';
  state: 'resolved';
  product: GraphProduct;
  selection: GraphSelection;
  options: GraphOption[];
  packages: GraphPackage[];
  exports: GraphExport[];
  capabilityBindings: GraphNamedNode[];
  actions: GraphNamedNode[];
  plugins: GraphNamedNode[];
  contributions: GraphNamedNode[];
  buildItems: GraphBuildItem[];
  runs: GraphRun[];
  tests: GraphTestRegistration[];
  benchmarks: GraphBenchmarkRegistration[];
  publishes: GraphNamedNode[];
  edges: GraphEdge[];
}

export interface CliResult {
  command: string;
  args: string[];
  cwd: string;
  exitCode: number;
  stdout: string;
  stderr: string;
  diagnostics: NginDiagnostic[];
}

export interface ContextSnapshot {
  context?: NginContext;
  graph?: CompositionGraph;
  graphError?: string;
  busy?: string;
  busyProjectId?: string;
  busyProjectManifest?: string;
  configured?: boolean;
  lastOperation?: { projectId?: string; projectManifest: string; command: string; state: 'succeeded' | 'failed'; completedAt: number; durationMs: number; message?: string };
}
