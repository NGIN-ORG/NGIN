import { XMLParser } from 'fast-xml-parser';
import { InspectPackageFeature, ProjectInspectPayload } from '../core/types';
import { ProjectFeatureState, ProjectInputBlock, ProjectInputEdit, ProjectPackageReferenceEdit } from './authoring';

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

function textLines(node: unknown): string[] {
  if (!node || typeof node !== 'object') {
    return typeof node === 'string' ? [node] : [];
  }
  const text = (node as { '#text'?: string })['#text'];
  if (!text) {
    return [];
  }
  return text
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter(Boolean);
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
  packageReferences: ProjectPackageReferenceEdit[];
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
    packageReferences: ProjectPackageReferenceEdit[];
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

function parsePackageReferences(node: unknown): ProjectPackageReferenceEdit[] {
  const parent = node as { Uses?: { Package?: unknown; Runtime?: unknown; Tool?: unknown } } | undefined;
  return [
    ...asArray(parent?.Uses?.Package),
    ...asArray(parent?.Uses?.Runtime),
    ...asArray(parent?.Uses?.Tool)
  ]
    .map((entry): ProjectPackageReferenceEdit | undefined => {
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
    .filter((entry): entry is ProjectPackageReferenceEdit => Boolean(entry));
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

function parseV4InputBlock(node: unknown, block: ProjectInputBlock): ProjectInputEdit[] {
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
  const v4 = parseV4InputBlock(node, block);
  if (v4.length > 0) {
    return v4;
  }
  const parent = node as { Inputs?: Record<string, unknown> } | undefined;
  const blocks = asArray(parent?.Inputs?.[block]);
  const entries: ProjectInputEdit[] = [];
  const legacySelectors = (value: {
    Profile?: string;
    Platform?: string;
    OperatingSystem?: string;
    Architecture?: string;
    BuildType?: string;
    Environment?: string;
    Condition?: string;
  } | undefined): Pick<ProjectInputEdit, 'profile' | 'platform' | 'operatingSystem' | 'architecture' | 'buildType' | 'environment' | 'condition'> => ({
    profile: value?.Profile,
    platform: value?.Platform,
    operatingSystem: value?.OperatingSystem,
    architecture: value?.Architecture,
    buildType: value?.BuildType,
    environment: value?.Environment,
    condition: value?.Condition
  });
  for (const rawBlock of blocks) {
    const value = rawBlock as {
      Path?: string;
      Include?: string;
      Exclude?: string;
      File?: unknown;
      Directory?: unknown;
      Glob?: unknown;
      Profile?: string;
      Platform?: string;
      OperatingSystem?: string;
      Architecture?: string;
      BuildType?: string;
      Environment?: string;
      Condition?: string;
    } | undefined;
    if (!value) {
      continue;
    }
    if (value.Path) {
      entries.push({ mode: 'Directory', path: value.Path, include: value.Include, exclude: value.Exclude, ...legacySelectors(value) });
    }
    if (value.Include) {
      entries.push({ mode: 'Glob', include: value.Include, exclude: value.Exclude, ...legacySelectors(value) });
    }
    for (const line of textLines(rawBlock)) {
      entries.push({ mode: 'File', path: line, ...legacySelectors(value) });
    }
    for (const file of asArray(value.File)) {
      const entry = file as { Path?: string } & Parameters<typeof legacySelectors>[0] | undefined;
      if (entry?.Path) {
        entries.push({ mode: 'File', path: entry.Path, ...legacySelectors(entry) });
      }
    }
    for (const directory of asArray(value.Directory)) {
      const entry = directory as { Path?: string; Include?: string; Exclude?: string } & Parameters<typeof legacySelectors>[0] | undefined;
      if (entry?.Path) {
        entries.push({ mode: 'Directory', path: entry.Path, include: entry.Include, exclude: entry.Exclude, ...legacySelectors(entry) });
      }
    }
    for (const glob of asArray(value.Glob)) {
      const entry = glob as { Include?: string; Exclude?: string } & Parameters<typeof legacySelectors>[0] | undefined;
      if (entry?.Include) {
        entries.push({ mode: 'Glob', include: entry.Include, exclude: entry.Exclude, ...legacySelectors(entry) });
      }
    }
  }
  return entries;
}

/*
 * Legacy helpers below remain as parser fallbacks for already-open editor
 * documents. The extension no longer creates V1/V2/V3 manifest shapes.
 */
function parseLegacyFeatureUses(node: unknown): ProjectEditorFeatureUse[] {
  const parent = node as { Features?: { Use?: unknown; Disable?: unknown } } | undefined;
  const parse = (entry: unknown, state: Exclude<ProjectFeatureState, 'inherit'>): ProjectEditorFeatureUse | undefined => {
    const value = entry as { Package?: string; Feature?: string } | undefined;
    return value?.Package && value.Feature
      ? { packageName: value.Package, featureName: value.Feature, state }
      : undefined;
  };
  return [
    ...asArray(parent?.Features?.Use).map((entry) => parse(entry, 'use')),
    ...asArray(parent?.Features?.Disable).map((entry) => parse(entry, 'disable'))
  ].filter((entry): entry is ProjectEditorFeatureUse => Boolean(entry));
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

function buildFeatures(inspect: ProjectInspectPayload | undefined, profiles: ProjectEditorProfile[], activeProfile: string | undefined): ProjectEditorFeature[] {
  const activeUses = new Map<string, ProjectFeatureState>();
  const profile = profiles.find((candidate) => candidate.name === activeProfile);
  for (const use of profile?.featureUses ?? []) {
    activeUses.set(featureKey(use.packageName, use.featureName), use.state);
  }

  return (inspect?.packageFeatures ?? []).map((feature: InspectPackageFeature) => {
    const state = activeUses.get(featureKey(feature.package, feature.feature)) ?? 'inherit';
    return {
      packageName: feature.package,
      packageVersion: feature.packageVersion,
      featureName: feature.feature,
      state,
      resolvedState: feature.state,
      description: feature.description,
      manifestPath: feature.manifestPath,
      readOnly: feature.state === 'unavailable'
    };
  });
}

function resolvedSummary(inspect: ProjectInspectPayload | undefined): ProjectEditorResolvedSummary {
  const diagnostics = inspect?.diagnostics ?? [];
  const packages = (inspect?.packages ?? []).map((entry) => ({
    name: entry.name,
    version: entry.version,
    requiredBy: entry.requiredBy ?? [],
    manifestPath: entry.manifestPath
  }));
  const inputs = Object.entries(inspect?.inputs ?? {}).flatMap(([kind, entries]) =>
    entries.map((entry) => ({
      kind,
      source: entry.source,
      mode: entry.mode,
      ownerName: entry.ownerName,
      stagedRelativePath: entry.stagedRelativePath
    }))
  );
  const environmentVariables = (inspect?.environmentVariables ?? []).map((entry) => ({
    name: entry.name,
    source: entry.source,
    secret: entry.secret,
    resolved: entry.resolved
  }));
  const features = inspect?.packageFeatures ?? [];
  const generators = inspect?.generators ?? [];
  return {
    projectName: inspect?.project?.name,
    projectType: inspect?.project?.type,
    workspaceName: inspect?.workspace?.name,
    profileName: inspect?.profile?.name,
    buildType: inspect?.profile?.buildType,
    platform: inspect?.profile?.platform,
    operatingSystem: inspect?.profile?.operatingSystem,
    architecture: inspect?.profile?.architecture,
    environment: inspect?.profile?.environment,
    outputDir: inspect?.outputDir,
    launchExecutable: inspect?.launch?.executable?.name,
    launchWorkingDirectory: inspect?.launch?.workingDirectory,
    packageCount: packages.length,
    featureCount: features.length,
    activeFeatureCount: features.filter((entry) => entry.state === 'selected').length,
    generatorCount: generators.length,
    activeGeneratorCount: generators.filter((entry) => entry.state === 'active').length,
    stagedFileCount: inspect?.stagedFiles?.length ?? 0,
    environmentVariableCount: environmentVariables.length,
    diagnosticErrorCount: diagnostics.filter((diagnostic) => diagnostic.severity === 'error').length,
    diagnosticWarningCount: diagnostics.filter((diagnostic) => diagnostic.severity === 'warning').length,
    packages,
    inputs,
    environmentVariables
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
  inspect?: ProjectInspectPayload,
  activeProfile?: string
): ProjectEditorModel {
  const base: ProjectEditorModel = {
    uri,
    path: manifestPath,
    project: {
      packageReferences: [],
      inputs: emptyInputs()
    },
    activeProfile,
    profiles: [],
    environments: [],
    features: [],
    diagnostics: (inspect?.diagnostics ?? []).map((diagnostic) => `${diagnostic.severity}: ${diagnostic.subject ? `${diagnostic.subject}: ` : ''}${diagnostic.message}`),
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
          packageReferences: parsePackageReferences(overlayProduct),
          featureUses: [...parseFeatureUses(overlayProduct), ...parseLegacyFeatureUses(profile)],
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
        packageReferences: parsePackageReferences(rootProduct),
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
