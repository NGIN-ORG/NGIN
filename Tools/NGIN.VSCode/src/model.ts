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
  workspaceFolder: string;
  workspaceManifest?: string;
  projectManifest: string;
  projectName: string;
  configuration: string;
  target: string;
  toolchain: string;
  preset?: string;
  options: Readonly<Record<string, string>>;
  outputDirectory: string;
}

export interface WorkspaceChoices {
  name: string;
  configurations: string[];
  targets: string[];
  toolchains: string[];
  presets: Array<{ name: string; command?: string }>;
  defaults: {
    configuration?: string;
    target?: string;
    toolchain?: string;
  };
}

export interface ProjectCandidate {
  manifest: string;
  directory: string;
  name: string;
  type?: string;
  workspaceManifest?: string;
  workspaceChoices?: WorkspaceChoices;
}

export interface DiscoveryResult {
  workspaceFolder: string;
  workspaceManifest?: string;
  workspaceChoices?: WorkspaceChoices;
  projects: ProjectCandidate[];
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
  type: string;
  linkage?: string;
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

export interface GraphLaunch extends GraphNamedNode {
  default?: boolean;
  executableKind?: string;
  executable?: string;
  workingDirectory?: string;
  arguments?: string[];
  environment?: Record<string, string>;
  secrets?: Record<string, string>;
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
  launches: GraphLaunch[];
  testing: GraphNamedNode | null;
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
}
