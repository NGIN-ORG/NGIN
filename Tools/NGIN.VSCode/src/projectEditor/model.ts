import { XMLParser } from 'fast-xml-parser';
import { CompositionGraphPayload, GraphPackageFeaturePlan } from '../core/types';
import { ProjectFeatureState, ProjectInputBlock, ProjectInputEdit, ProjectDependencyUseEdit } from './authoring';

const parser = new XMLParser({
  ignoreAttributes: false,
  attributeNamePrefix: '',
  parseTagValue: false,
  trimValues: true
});

function asArray<T>(value: T | T[] | undefined): T[] {
  if (value === undefined || value === null) {
    return [];
  }
  return Array.isArray(value) ? value : [value];
}

const productKinds = ['Application', 'Library', 'Tool', 'Test', 'Benchmark', 'Plugin', 'Module', 'External'];

function productNode(root: Record<string, unknown> | undefined): Record<string, unknown> | undefined {
  if (!root) {
    return undefined;
  }
  for (const kind of productKinds) {
    if (root[kind] !== undefined) {
      return (root[kind] ?? {}) as Record<string, unknown>;
    }
  }
  return undefined;
}

function boolValue(value: unknown): boolean | undefined {
  if (value === undefined || value === null || value === '') {
    return undefined;
  }
  return value === true || value === 'true' || value === '1' || value === 'yes';
}

export interface ProjectEditorProfile {
  name: string;
  buildType?: string;
  platform?: string;
  operatingSystem?: string;
  architecture?: string;
  environment?: string;
  launchExecutable?: string;
  launchWorkingDirectory?: string;
  dependencies: ProjectDependencyUseEdit[];
  featureUses: ProjectEditorFeatureUse[];
  inputs: Record<ProjectInputBlock, ProjectInputEdit[]>;
}

export interface ProjectEditorFeatureUse {
  packageName: string;
  featureName: string;
  state: Exclude<ProjectFeatureState, 'inherit'>;
}

export interface ProjectEditorEnvironmentVariable {
  name: string;
  value?: string;
  fromEnvironment?: string;
  fromLocalSetting?: string;
  required?: boolean;
  secret?: boolean;
}

export interface ProjectEditorEnvironment {
  name: string;
  variables: ProjectEditorEnvironmentVariable[];
}

export interface ProjectEditorFeature {
  packageName: string;
  packageVersion?: string;
  featureName: string;
  state: ProjectFeatureState;
  resolvedState?: string;
  description?: string;
  manifestPath?: string;
  readOnly?: boolean;
}

export interface ProjectEditorResolvedPackage {
  name: string;
  version?: string;
  requiredBy: string[];
  manifestPath?: string;
}

export interface ProjectEditorResolvedInput {
  kind: string;
  source?: string;
  mode?: string;
  ownerName?: string;
  stagedRelativePath?: string;
}

export interface ProjectEditorResolvedEnvironmentVariable {
  name: string;
  source?: string;
  secret?: boolean;
  resolved?: boolean;
}

export interface ProjectEditorResolvedAnalyzer {
  name: string;
  tool?: string;
  packageName?: string;
  scope?: string;
  severity?: string;
  configPath?: string;
  configOptional?: boolean;
}

export interface ProjectEditorResolvedSummary {
  projectName?: string;
  projectType?: string;
  workspaceName?: string;
  profileName?: string;
  buildType?: string;
  platform?: string;
  operatingSystem?: string;
  architecture?: string;
  environment?: string;
  outputDir?: string;
  launchExecutable?: string;
  launchWorkingDirectory?: string;
  packageCount: number;
  featureCount: number;
  activeFeatureCount: number;
  generatorCount: number;
  activeGeneratorCount: number;
  stagedFileCount: number;
  environmentVariableCount: number;
  diagnosticErrorCount: number;
  diagnosticWarningCount: number;
  packages: ProjectEditorResolvedPackage[];
  inputs: ProjectEditorResolvedInput[];
  environmentVariables: ProjectEditorResolvedEnvironmentVariable[];
  analyzers: ProjectEditorResolvedAnalyzer[];
  toolingPackages: ProjectEditorResolvedPackage[];
}

export interface ProjectEditorModel {
  uri: string;
  path: string;
  parseError?: string;
  project: {
    name?: string;
    productKind?: string;
    defaultProfile?: string;
    launchExecutable?: string;
    launchWorkingDirectory?: string;
    dependencies: ProjectDependencyUseEdit[];
    inputs: Record<ProjectInputBlock, ProjectInputEdit[]>;
  };
  activeProfile?: string;
  profiles: ProjectEditorProfile[];
  environments: ProjectEditorEnvironment[];
  features: ProjectEditorFeature[];
  diagnostics: string[];
  unsupportedSections: string[];
  resolved: ProjectEditorResolvedSummary;
}

function emptyInputs(): Record<ProjectInputBlock, ProjectInputEdit[]> {
  return {
    Sources: [],
    Headers: [],
    Configs: []
  };
}

function parseDependencyUses(node: unknown): ProjectDependencyUseEdit[] {
  const parent = node as { Uses?: { Package?: unknown; Runtime?: unknown; Tool?: unknown } } | undefined;
  return [
    ...asArray(parent?.Uses?.Package),
    ...asArray(parent?.Uses?.Runtime),
    ...asArray(parent?.Uses?.Tool)
  ]
    .map((entry): ProjectDependencyUseEdit | undefined => {
      const ref = entry as { Name?: string; Version?: string; VersionRange?: string; Optional?: string | boolean } | undefined;
      if (!ref?.Name) {
        return undefined;
      }
      return {
        name: ref.Name,
        version: ref.Version ?? ref.VersionRange,
        optional: boolValue(ref.Optional)
      };
    })
    .filter((entry): entry is ProjectDependencyUseEdit => Boolean(entry));
}

function parseFeatureUses(node: unknown): ProjectEditorFeatureUse[] {
  const parent = node as { Uses?: { Package?: unknown; Runtime?: unknown; Tool?: unknown } } | undefined;
  const parse = (dependency: unknown): ProjectEditorFeatureUse[] => {
    const value = dependency as { Name?: string; Feature?: unknown } | undefined;
    if (!value?.Name) {
      return [];
    }
    return asArray(value.Feature)
      .map((feature): ProjectEditorFeatureUse | undefined => {
        const featureValue = feature as { Name?: string; Enabled?: string | boolean } | undefined;
        return featureValue?.Name
          ? {
              packageName: value.Name,
              featureName: featureValue.Name,
              state: featureValue.Enabled === false || featureValue.Enabled === 'false' ? 'disable' : 'use'
            }
          : undefined;
      })
      .filter((entry): entry is ProjectEditorFeatureUse => Boolean(entry));
  };
  return [
    ...asArray(parent?.Uses?.Package).flatMap(parse),
    ...asArray(parent?.Uses?.Runtime).flatMap(parse),
    ...asArray(parent?.Uses?.Tool).flatMap(parse)
  ];
}

function selectors(value: {
  Profile?: string;
  Platform?: string;
  TargetPlatform?: string;
  OperatingSystem?: string;
  Architecture?: string;
  BuildType?: string;
  Environment?: string;
  Condition?: string;
  When?: string;
} | undefined): Pick<ProjectInputEdit, 'profile' | 'platform' | 'operatingSystem' | 'architecture' | 'buildType' | 'environment' | 'condition'> {
  return {
    profile: value?.Profile,
    platform: value?.Platform ?? value?.TargetPlatform,
    operatingSystem: value?.OperatingSystem,
    architecture: value?.Architecture,
    buildType: value?.BuildType,
    environment: value?.Environment,
    condition: value?.Condition ?? value?.When
  };
}

function parseProductInputBlock(node: unknown, block: ProjectInputBlock): ProjectInputEdit[] {
  const product = node as { Build?: Record<string, unknown>; Stage?: Record<string, unknown> } | undefined;
  if (block === 'Configs') {
    return asArray(product?.Stage?.Config)
      .map((entry): ProjectInputEdit | undefined => {
        const value = entry as { Source?: string; Path?: string; Target?: string } & Parameters<typeof selectors>[0] | undefined;
        return value?.Source || value?.Path
          ? { mode: 'File', path: value.Source ?? value.Path, ...selectors(value) }
          : undefined;
      })
      .filter((entry): entry is ProjectInputEdit => Boolean(entry));
  }
  return asArray(product?.Build?.[block])
    .map((entry): ProjectInputEdit | undefined => {
      const value = entry as { Path?: string; Include?: string; Exclude?: string } & Parameters<typeof selectors>[0] | undefined;
      return value?.Path || value?.Include
        ? {
            mode: value.Path && !value.Path.includes('*') ? 'Directory' : 'Glob',
            path: value.Path,
            include: value.Include,
            exclude: value.Exclude,
            ...selectors(value)
          }
        : undefined;
    })
    .filter((entry): entry is ProjectInputEdit => Boolean(entry));
}

function parseInputBlock(node: unknown, block: ProjectInputBlock): ProjectInputEdit[] {
  return parseProductInputBlock(node, block);
}

function parseInputs(node: unknown): Record<ProjectInputBlock, ProjectInputEdit[]> {
  return {
    Sources: parseInputBlock(node, 'Sources'),
    Headers: parseInputBlock(node, 'Headers'),
    Configs: parseInputBlock(node, 'Configs')
  };
}

function parseProductEnvironment(product: Record<string, unknown> | undefined): ProjectEditorEnvironment[] {
  const environment = product?.Environment as { Env?: unknown; Secret?: unknown } | undefined;
  const variables: ProjectEditorEnvironmentVariable[] = [
    ...asArray(environment?.Env).map((entry): ProjectEditorEnvironmentVariable | undefined => {
      const value = entry as { Name?: string; Value?: string; FromEnvironment?: string; Required?: string | boolean } | undefined;
      return value?.Name
        ? {
            name: value.Name,
            value: value.Value,
            fromEnvironment: value.FromEnvironment,
            required: boolValue(value.Required),
            secret: false
          }
        : undefined;
    }),
    ...asArray(environment?.Secret).map((entry): ProjectEditorEnvironmentVariable | undefined => {
      const value = entry as { Name?: string; From?: string; Required?: string | boolean } | undefined;
      if (!value?.Name) {
        return undefined;
      }
      const from = value.From ?? '';
      return {
        name: value.Name,
        fromLocalSetting: from.startsWith('local:') ? from.slice('local:'.length) : undefined,
        fromEnvironment: from.startsWith('env:') ? from.slice('env:'.length) : undefined,
        required: boolValue(value.Required),
        secret: true
      };
    })
  ].filter((entry): entry is ProjectEditorEnvironmentVariable => Boolean(entry));
  return variables.length > 0 ? [{ name: 'product', variables }] : [];
}

function featureKey(packageName: string, featureName: string): string {
  return `${packageName}::${featureName}`;
}

function buildFeatures(inspect: CompositionGraphPayload | undefined, profiles: ProjectEditorProfile[], activeProfile: string | undefined): ProjectEditorFeature[] {
  const activeUses = new Map<string, ProjectFeatureState>();
  const profile = profiles.find((candidate) => candidate.name === activeProfile);
  for (const use of profile?.featureUses ?? []) {
    activeUses.set(featureKey(use.packageName, use.featureName), use.state);
  }

  return (inspect?.plans?.packageFeatures ?? []).map((feature: GraphPackageFeaturePlan) => {
    const state = activeUses.get(featureKey(feature.package, feature.feature)) ?? 'inherit';
    return {
      packageName: feature.package,
      packageVersion: feature.packageVersion,
      featureName: feature.feature,
      state,
      resolvedState: 'selected',
      description: feature.description,
      manifestPath: feature.manifestPath,
      readOnly: false
    };
  });
}

function resolvedSummary(inspect: CompositionGraphPayload | undefined): ProjectEditorResolvedSummary {
  const diagnostics = inspect?.plans?.diagnostics ?? [];
  const packages = (inspect?.plans?.packages ?? []).map((entry) => ({
    name: entry.name,
    version: entry.version,
    requiredBy: entry.closures ?? (entry.scope ? [entry.scope] : []),
    manifestPath: entry.manifestPath
  }));
  const inputs = (inspect?.plans?.build?.inputs ?? []).map((entry) => ({
      kind: entry.kind ?? entry.role ?? 'Source',
      source: entry.source,
      mode: entry.mode,
      ownerName: entry.ownerName ?? entry.owner,
      stagedRelativePath: entry.stagedRelativePath
    }));
  const environmentVariables = (inspect?.plans?.environment?.variables ?? []).map((entry) => ({
    name: entry.name,
    source: entry.source,
    secret: entry.secret,
    resolved: entry.resolved
  }));
  const features = inspect?.plans?.packageFeatures ?? [];
  const generators = inspect?.plans?.generators ?? [];
  const analyzers = (inspect?.plans?.quality?.analyzers ?? []).map((entry) => ({
    name: entry.name,
    tool: entry.tool,
    packageName: entry.package,
    scope: entry.scope,
    severity: entry.severity,
    configPath: entry.configPath,
    configOptional: entry.configOptional
  }));
  return {
    projectName: inspect?.identity?.project,
    projectType: inspect?.product?.kind,
    workspaceName: inspect?.workspace?.name,
    profileName: inspect?.selection?.profile,
    buildType: inspect?.selection?.buildType,
    platform: inspect?.selection?.targetPlatform ?? inspect?.selection?.platform,
    operatingSystem: inspect?.selection?.operatingSystem,
    architecture: inspect?.selection?.architecture,
    environment: inspect?.selection?.environment,
    outputDir: inspect?.outputDir,
    launchExecutable: inspect?.plans?.launch?.executable,
    launchWorkingDirectory: inspect?.plans?.launch?.workingDirectory,
    packageCount: packages.length,
    featureCount: features.length,
    activeFeatureCount: features.length,
    generatorCount: generators.length,
    activeGeneratorCount: generators.filter((entry) => entry.state === undefined || entry.state === 'active').length,
    stagedFileCount: inspect?.plans?.stage?.files?.length ?? 0,
    environmentVariableCount: environmentVariables.length,
    diagnosticErrorCount: diagnostics.filter((diagnostic) => diagnostic.severity === 'error').length,
    diagnosticWarningCount: diagnostics.filter((diagnostic) => diagnostic.severity === 'warning').length,
    packages,
    inputs,
    environmentVariables,
    analyzers,
    toolingPackages: packages.filter((pkg) => pkg.name.startsWith('NGIN.Tooling.'))
  };
}

function unsupportedSections(root: Record<string, unknown>): string[] {
  return ['Conditions', 'LocalSettings']
    .filter((name) => root[name] !== undefined);
}

function defaultsValue(profile: Record<string, unknown>, name: string): string | undefined {
  const defaults = profile.Defaults as Record<string, unknown> | undefined;
  const node = defaults?.[name] as { Name?: string } | undefined;
  return node?.Name;
}

export function buildProjectEditorModel(
  xml: string,
  manifestPath: string,
  uri: string,
  inspect?: CompositionGraphPayload,
  activeProfile?: string
): ProjectEditorModel {
  const base: ProjectEditorModel = {
    uri,
    path: manifestPath,
    project: {
      dependencies: [],
      inputs: emptyInputs()
    },
    activeProfile,
    profiles: [],
    environments: [],
    features: [],
    diagnostics: (inspect?.plans?.diagnostics ?? []).map((diagnostic) => `${diagnostic.severity}: ${diagnostic.subject ? `${diagnostic.subject}: ` : ''}${diagnostic.message}`),
    unsupportedSections: [],
    resolved: resolvedSummary(inspect)
  };

  try {
    const document = parser.parse(xml);
    const root = document.Project as Record<string, unknown> & {
      Name?: string;
      DefaultProfile?: string;
      Launch?: { Executable?: string; WorkingDirectory?: string };
      Profile?: unknown;
    };
    if (!root) {
      return { ...base, parseError: 'Root element must be <Project>.' };
    }

    const rootProduct = productNode(root);
    const profiles = asArray(root.Profile)
      .map((entry): ProjectEditorProfile | undefined => {
        const profile = entry as Record<string, unknown> & {
          Name?: string;
        } | undefined;
        if (!profile?.Name) {
          return undefined;
        }
        const overlayProduct = productNode(profile);
        const launch = (overlayProduct?.Launch ?? rootProduct?.Launch) as { Executable?: string; WorkingDirectory?: string } | undefined;
        return {
          name: profile.Name,
          buildType: defaultsValue(profile, 'BuildType'),
          platform: defaultsValue(profile, 'TargetPlatform'),
          operatingSystem: defaultsValue(profile, 'OperatingSystem'),
          architecture: defaultsValue(profile, 'Architecture'),
          environment: defaultsValue(profile, 'Environment'),
          launchExecutable: launch?.Executable,
          launchWorkingDirectory: launch?.WorkingDirectory,
          dependencies: parseDependencyUses(overlayProduct),
          featureUses: parseFeatureUses(overlayProduct),
          inputs: parseInputs(overlayProduct)
        };
      })
      .filter((entry): entry is ProjectEditorProfile => Boolean(entry));

    const selectedProfile = activeProfile ?? root.DefaultProfile ?? profiles[0]?.name;
    const rootLaunch = rootProduct?.Launch as { Executable?: string; WorkingDirectory?: string } | undefined;
    return {
      ...base,
      project: {
        name: root.Name,
        productKind: productKinds.find((kind) => root[kind] !== undefined),
        defaultProfile: root.DefaultProfile,
        launchExecutable: rootLaunch?.Executable,
        launchWorkingDirectory: rootLaunch?.WorkingDirectory,
        dependencies: parseDependencyUses(rootProduct),
        inputs: parseInputs(rootProduct)
      },
      activeProfile: selectedProfile,
      profiles,
      environments: parseProductEnvironment(rootProduct),
      features: buildFeatures(inspect, profiles, selectedProfile),
      unsupportedSections: unsupportedSections(root)
    };
  } catch (error) {
    return {
      ...base,
      parseError: error instanceof Error ? error.message : String(error)
    };
  }
}
