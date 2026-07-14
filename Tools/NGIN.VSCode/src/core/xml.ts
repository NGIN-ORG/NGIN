import * as path from 'node:path';
import { XMLParser } from 'fast-xml-parser';
import {
  ConditionDefinition,
  GeneratorArgument,
  GeneratorDeclaration,
  InputDeclaration,
  LaunchDescriptor,
  LaunchManifest,
  LocalSettingsManifest,
  PackageFeature,
  PackageFeatureUse,
  PackageManifest,
  DependencyUse,
  ProjectManifest,
  ProjectProfile,
  ProjectReference,
  SelectionDefaults,
  StagedFile,
  ToolDeclaration,
  WorkspaceManifest
} from './types';

const parser = new XMLParser({
  ignoreAttributes: false,
  attributeNamePrefix: '',
  parseTagValue: false,
  trimValues: true
});

const productKinds = ['Application', 'Library', 'Tool', 'Test', 'Benchmark', 'Plugin', 'Module', 'External'];

function asArray<T>(value: T | T[] | undefined): T[] {
  if (value === undefined || value === null) {
    return [];
  }
  return Array.isArray(value) ? value : [value];
}

function parseBool(value: unknown, fallback = true): boolean {
  if (value === undefined || value === null || value === '') {
    return fallback;
  }
  return value === true || value === 'true' || value === '1' || value === 'yes';
}

function requireCurrentSchema(root: { SchemaVersion?: string }, manifestPath: string): void {
  if (root.SchemaVersion !== '4') {
    throw new Error(`${manifestPath}: project tooling requires SchemaVersion="4"`);
  }
}

function resolveManifestPath(baseDirectory: string, candidate: string): string {
  return path.isAbsolute(candidate) ? candidate : path.resolve(baseDirectory, candidate);
}

function selectorFields(node: {
  When?: string;
  Profile?: string;
  Platform?: string;
  TargetPlatform?: string;
  OperatingSystem?: string;
  Architecture?: string;
  Toolchain?: string;
  Environment?: string;
  Condition?: string;
} | undefined): Pick<InputDeclaration, 'profile' | 'platform' | 'operatingSystem' | 'architecture' | 'toolchain' | 'environment' | 'condition'> {
  return {
    profile: node?.Profile,
    platform: node?.Platform ?? node?.TargetPlatform,
    operatingSystem: node?.OperatingSystem,
    architecture: node?.Architecture,
    toolchain: node?.Toolchain,
    environment: node?.Environment,
    condition: node?.Condition ?? node?.When
  };
}

type SelectorNode = Parameters<typeof selectorFields>[0];

function applySelectorFields<T extends object>(target: T, node: Parameters<typeof selectorFields>[0]): T {
  return { ...target, ...selectorFields(node) };
}

function nodeName(node: unknown): string | undefined {
  if (typeof node === 'string') {
    return node;
  }
  const entry = node as { Name?: string } | undefined;
  return entry?.Name;
}

function parseDefaults(root: { Defaults?: unknown } | undefined): SelectionDefaults | undefined {
  const defaults = root?.Defaults as {
    TargetPlatform?: unknown;
    HostPlatform?: unknown;
    Platform?: unknown;
    OperatingSystem?: unknown;
    Architecture?: unknown;
    Environment?: unknown;
    Toolchain?: unknown;
  } | undefined;
  if (!defaults) {
    return undefined;
  }
  return {
    targetPlatform: nodeName(defaults.TargetPlatform) ?? nodeName(defaults.Platform),
    hostPlatform: nodeName(defaults.HostPlatform),
    operatingSystem: nodeName(defaults.OperatingSystem),
    architecture: nodeName(defaults.Architecture),
    environment: nodeName(defaults.Environment),
    toolchain: nodeName(defaults.Toolchain)
  };
}

function mergeDefaults(base: SelectionDefaults | undefined, overlay: SelectionDefaults | undefined): SelectionDefaults | undefined {
  if (!base && !overlay) {
    return undefined;
  }
  return { ...(base ?? {}), ...(overlay ?? {}) };
}

function getProduct(root: Record<string, unknown>): { kind: string; node: Record<string, unknown> } | undefined {
  for (const kind of productKinds) {
    const node = root[kind];
    if (node !== undefined) {
      return { kind, node: (node ?? {}) as Record<string, unknown> };
    }
  }
  return undefined;
}

function pathInput(
  kind: string,
  role: string | undefined,
  source: string | undefined,
  visibility: string | undefined,
  target?: string,
  generated = false,
  node?: SelectorNode
): InputDeclaration | undefined {
  if (!source) {
    return undefined;
  }
  const isGlob = source.includes('*');
  return {
    ...selectorFields(node),
    kind: generated ? 'Generated' : kind,
    role,
    mode: isGlob ? 'Glob' : 'Directory',
    path: isGlob ? undefined : source,
    pattern: isGlob ? source : undefined,
    visibility: visibility ?? 'Private',
    target
  };
}

function fileInput(kind: string, role: string | undefined, source: string | undefined, visibility: string | undefined, target?: string): InputDeclaration | undefined {
  if (!source) {
    return undefined;
  }
  return {
    kind,
    role,
    mode: 'File',
    path: source,
    visibility: visibility ?? 'Private',
    target
  };
}

function parseBuildInputs(node: unknown, generated = false): InputDeclaration[] {
  const build = node as {
    Sources?: unknown;
    Headers?: unknown;
    Source?: unknown;
    Header?: unknown;
  } | undefined;
  if (!build) {
    return [];
  }
  return [
    ...asArray(build.Sources).map((entry) => {
      const value = entry as { Path?: string; Visibility?: string } & SelectorNode | undefined;
      return pathInput('Source', 'Source', value?.Path, value?.Visibility ?? 'Private', undefined, generated, value);
    }),
    ...asArray(build.Headers).map((entry) => {
      const value = entry as { Path?: string; Visibility?: string } & SelectorNode | undefined;
      return pathInput('Source', 'Header', value?.Path, value?.Visibility ?? 'Public', undefined, generated, value);
    }),
    ...asArray(build.Source).map((entry) => {
      const value = entry as { Path?: string; Visibility?: string } | undefined;
      return fileInput(generated ? 'Generated' : 'Source', 'Source', value?.Path, value?.Visibility ?? 'Private');
    }),
    ...asArray(build.Header).map((entry) => {
      const value = entry as { Path?: string; Visibility?: string } | undefined;
      return fileInput(generated ? 'Generated' : 'Source', 'Header', value?.Path, value?.Visibility ?? 'Public');
    })
  ].filter((entry): entry is InputDeclaration => Boolean(entry));
}

function parseStageInputs(node: unknown): InputDeclaration[] {
  const stage = node as {
    Config?: unknown;
    Content?: unknown;
    Asset?: unknown;
    RuntimeConfig?: unknown;
  } | undefined;
  if (!stage) {
    return [];
  }
  const parse = (entry: unknown, kind: string): InputDeclaration | undefined => {
    const value = entry as { Source?: string; Path?: string; Target?: string; StagePath?: string; Required?: string | boolean } | undefined;
    const source = value?.Source ?? value?.Path;
    const target = value?.Target ?? value?.StagePath;
    const input = fileInput(kind, kind, source, 'Private', target);
    if (input) {
      input.required = parseBool(value?.Required, true);
    }
    return input;
  };
  return [
    ...asArray(stage.Config).map((entry) => parse(entry, 'Config')),
    ...asArray(stage.RuntimeConfig).map((entry) => parse(entry, 'Config')),
    ...asArray(stage.Content).map((entry) => parse(entry, 'Content')),
    ...asArray(stage.Asset).map((entry) => parse(entry, 'Asset'))
  ].filter((entry): entry is InputDeclaration => Boolean(entry));
}

function parseGeneratorOutputs(generator: unknown): InputDeclaration[] {
  const node = generator as { Outputs?: unknown } | undefined;
  const outputs = node?.Outputs as { Sources?: unknown; Headers?: unknown; Files?: unknown } | undefined;
  return [
    ...parseBuildInputs(outputs, true),
    ...asArray(outputs?.Files).map((entry) => {
      const value = entry as { Path?: string } & SelectorNode | undefined;
      return pathInput('Generated', 'Content', value?.Path, 'Private', undefined, true, value);
    })
  ].filter((entry): entry is InputDeclaration => Boolean(entry));
}

function parseToolDeclaration(entry: unknown, requireName: boolean): ToolDeclaration | undefined {
  const node = entry as { Name?: string; Package?: string; Kind?: string; Executable?: string } | undefined;
  if (!node || (requireName && !node.Name)) {
    return undefined;
  }
  return {
    name: node.Name,
    packageName: node.Package,
    kind: node.Kind ?? 'Generator',
    executable: node.Executable
  };
}

function parseGenerators(product: unknown): GeneratorDeclaration[] {
  const node = product as { Generate?: { Generator?: unknown }; Generators?: { Generator?: unknown } } | undefined;
  return asArray(node?.Generate?.Generator ?? node?.Generators?.Generator)
    .map((entry): GeneratorDeclaration | undefined => {
      const generator = entry as {
        Name?: string;
        Kind?: string;
        Phase?: string;
        Tool?: unknown;
        Args?: { Arg?: unknown };
        Arguments?: { Arg?: unknown };
      } | undefined;
      if (!generator?.Name) {
        return undefined;
      }
      const inlineTool = typeof generator.Tool === 'object'
        ? parseToolDeclaration(generator.Tool, false)
        : undefined;
      const toolName = typeof generator.Tool === 'string'
        ? generator.Tool
        : (generator.Tool as { Name?: string } | undefined)?.Name;
      const argumentsNode = generator.Args ?? generator.Arguments;
      return {
        name: generator.Name,
        kind: generator.Kind ?? generator.Phase ?? 'Command',
        toolName,
        inlineTool,
        inputs: [
          ...parseBuildInputs((entry as { Inputs?: unknown }).Inputs),
          ...parseStageInputs((entry as { Inputs?: unknown }).Inputs)
        ],
        outputs: parseGeneratorOutputs(entry),
        arguments: asArray(argumentsNode?.Arg)
          .map((arg): GeneratorArgument | undefined => {
            const value = arg as { Value?: string; Path?: string } | undefined;
            return value?.Value || value?.Path ? { value: value.Value, path: value.Path } : undefined;
          })
          .filter((arg): arg is GeneratorArgument => Boolean(arg))
      };
    })
    .filter((entry): entry is GeneratorDeclaration => Boolean(entry));
}

function configInputsFrom(inputs: InputDeclaration[]): string[] {
  return inputs
    .filter((entry) => entry.kind === 'Config')
    .map((entry) => entry.path ?? entry.pattern)
    .filter((entry): entry is string => Boolean(entry));
}

function sourceRootsFrom(inputs: InputDeclaration[]): string[] {
  return inputs
    .filter((entry) => (entry.kind === 'Source' || (entry.kind === 'Generated' && entry.role === 'Source')))
    .map((entry) => entry.path ?? entry.pattern)
    .filter((entry): entry is string => Boolean(entry));
}

function buildSourcesFrom(inputs: InputDeclaration[]): string[] {
  return inputs
    .filter((entry) => (entry.kind === 'Source' || (entry.kind === 'Generated' && entry.role === 'Source')) && entry.mode === 'File' && Boolean(entry.path))
    .map((entry) => entry.path as string);
}

function parseProjectReferences(product: unknown, baseDirectory: string): ProjectReference[] {
  const uses = (product as { Uses?: { Project?: unknown } } | undefined)?.Uses;
  return asArray(uses?.Project)
    .map((entry): ProjectReference | undefined => {
      const ref = entry as { Name?: string; Path?: string; Profile?: string } | undefined;
      const refPath = ref?.Path ?? ref?.Name;
      if (!refPath) {
        return undefined;
      }
      return {
        name: ref?.Name,
        path: ref.Path ? resolveManifestPath(baseDirectory, ref.Path) : refPath,
        profile: ref.Profile
      };
    })
    .filter((entry): entry is ProjectReference => Boolean(entry));
}

function parseDependencyEntries(product: unknown): DependencyUse[] {
  const uses = (product as { Uses?: { Package?: unknown; Runtime?: unknown; Tool?: unknown } } | undefined)?.Uses;
  const parse = (entry: unknown, kind: string): DependencyUse | undefined => {
    const ref = entry as { Name?: string; Version?: string; Scope?: string; Optional?: string | boolean; Feature?: unknown } | undefined;
    if (!ref?.Name) {
      return undefined;
    }
    return {
      name: ref.Name,
      version: ref.Version,
      scope: ref.Scope,
      kind,
      optional: ref.Optional === true || ref.Optional === 'true',
      features: asArray(ref.Feature)
        .map((feature) => (feature as { Name?: string } | undefined)?.Name)
        .filter((feature): feature is string => Boolean(feature))
    };
  };
  return [
    ...asArray(uses?.Package).map((entry) => parse(entry, 'Package')),
    ...asArray(uses?.Runtime).map((entry) => parse(entry, 'Runtime')),
    ...asArray(uses?.Tool).map((entry) => parse(entry, 'Tool'))
  ].filter((entry): entry is DependencyUse => Boolean(entry));
}

function parsePackageFeatureUses(product: unknown): PackageFeatureUse[] {
  return parseDependencyEntries(product).flatMap((dependency) =>
    (dependency.features ?? []).map((featureName) => ({
      packageName: dependency.name,
      featureName,
      version: dependency.version,
      disabled: false
    }))
  );
}

function parseConditions(root: unknown): ConditionDefinition[] {
  const parent = root as { Conditions?: { Condition?: unknown } } | undefined;
  return asArray(parent?.Conditions?.Condition)
    .map((entry): ConditionDefinition | undefined => {
      const condition = entry as { Name?: string; When?: unknown } | undefined;
      const when = asArray(condition?.When)[0] as Parameters<typeof selectorFields>[0] | undefined;
      return condition?.Name ? { name: condition.Name, ...selectorFields(when) } : undefined;
    })
    .filter((entry): entry is ConditionDefinition => Boolean(entry));
}

export function parseWorkspaceManifest(xml: string, manifestPath: string): WorkspaceManifest {
  const document = parser.parse(xml);
  const root = document.Workspace;
  if (!root) {
    throw new Error(`${manifestPath}: root element must be <Workspace>`);
  }
  requireCurrentSchema(root, manifestPath);

  const directory = path.dirname(manifestPath);
  const authoredOutputRoot = (asArray(root.Defaults?.OutputRoot)[0] as { Path?: string } | undefined)?.Path;
  return {
    path: manifestPath,
    directory,
    name: root.Name ?? path.basename(manifestPath, path.extname(manifestPath)),
    platformVersion: root.PlatformVersion,
    outputRoot: authoredOutputRoot ? resolveManifestPath(directory, authoredOutputRoot) : undefined,
    imports: asArray(root.Imports?.Import)
      .map((entry) => (entry as { Path?: string } | undefined)?.Path)
      .filter((entry): entry is string => Boolean(entry))
      .map((entry) => resolveManifestPath(directory, entry)),
    projectPaths: asArray(root.Projects?.Project)
      .map((entry) => (entry as { Path?: string } | undefined)?.Path)
      .filter((entry): entry is string => Boolean(entry))
      .map((entry) => resolveManifestPath(directory, entry)),
    packageSourcePaths: asArray(root.Packages?.Source)
      .map((entry) => (entry as { Path?: string } | undefined)?.Path)
      .filter((entry): entry is string => Boolean(entry))
      .map((entry) => resolveManifestPath(directory, entry))
  };
}

export function parseProjectManifest(xml: string, manifestPath: string): ProjectManifest {
  const document = parser.parse(xml);
  const root = document.Project;
  if (!root) {
    throw new Error(`${manifestPath}: root element must be <Project>`);
  }
  requireCurrentSchema(root, manifestPath);

  const directory = path.dirname(manifestPath);
  const rootRecord = root as Record<string, unknown> & { Name?: string; DefaultProfile?: string };
  const product = getProduct(rootRecord);
  if (!product) {
    throw new Error(`${manifestPath}: project must declare one product element`);
  }

  const rootDefaults = parseDefaults(root);
  const productInputs = [
    ...parseBuildInputs((product.node as { Build?: unknown }).Build),
    ...parseStageInputs((product.node as { Stage?: unknown }).Stage),
    ...parseGenerators(product.node).flatMap((generator) => generator.outputs ?? [])
  ];
  const productLaunch = (product.node as { Launch?: { Executable?: string; WorkingDirectory?: string } }).Launch;

  const profiles = asArray(root.Profile)
    .map((entry): ProjectProfile | undefined => {
      const profile = entry as Record<string, unknown> & {
        Name?: string;
        Extends?: string;
      };
      if (!profile?.Name) {
        return undefined;
      }
      const profileProduct = getProduct(profile) ?? { kind: product.kind, node: {} };
      const profileBuild = (profileProduct.node as { Build?: {
        Optimization?: { Mode?: string };
        DebugSymbols?: { Enabled?: string | boolean };
        LinkTimeOptimization?: { Enabled?: string | boolean };
      } }).Build;
      const defaults = mergeDefaults(rootDefaults, parseDefaults(profile as { Defaults?: unknown }));
      const overlayLaunch = (profileProduct.node as { Launch?: { Executable?: string; WorkingDirectory?: string } }).Launch;
      const inputs = [
        ...parseBuildInputs((profileProduct.node as { Build?: unknown }).Build),
        ...parseStageInputs((profileProduct.node as { Stage?: unknown }).Stage),
        ...parseGenerators(profileProduct.node).flatMap((generator) => generator.outputs ?? [])
      ];
      return {
        name: profile.Name,
        extends: profile.Extends,
        optimization: profileBuild?.Optimization?.Mode,
        debugSymbols: profileBuild?.DebugSymbols?.Enabled === true || profileBuild?.DebugSymbols?.Enabled === 'true',
        linkTimeOptimization: profileBuild?.LinkTimeOptimization?.Enabled === true ||
          profileBuild?.LinkTimeOptimization?.Enabled === 'true',
        platform: defaults?.targetPlatform,
        targetPlatform: defaults?.targetPlatform,
        hostPlatform: defaults?.hostPlatform,
        operatingSystem: defaults?.operatingSystem,
        architecture: defaults?.architecture,
        environment: defaults?.environment,
        toolchain: defaults?.toolchain,
        launchExecutable: overlayLaunch?.Executable ?? productLaunch?.Executable,
        launchWorkingDirectory: overlayLaunch?.WorkingDirectory ?? productLaunch?.WorkingDirectory,
        inputs,
        configInputs: configInputsFrom(inputs),
        projectRefs: parseProjectReferences(profileProduct.node, directory),
        dependencies: parseDependencyEntries(profileProduct.node),
        packageFeatureUses: parsePackageFeatureUses(profileProduct.node),
        generators: parseGenerators(profileProduct.node)
      };
    })
    .filter((entry): entry is ProjectProfile => Boolean(entry));

  return {
    path: manifestPath,
    directory,
    name: root.Name ?? path.basename(manifestPath, path.extname(manifestPath)),
    productKind: product.kind,
    defaultProfile: rootRecord.DefaultProfile,
    inputs: productInputs,
    sourceRoots: sourceRootsFrom(productInputs),
    configInputs: configInputsFrom(productInputs),
    localSettingsImports: [],
    buildSources: buildSourcesFrom(productInputs),
    conditions: parseConditions(root),
    projectRefs: parseProjectReferences(product.node, directory),
    dependencies: parseDependencyEntries(product.node),
    packageFeatureUses: parsePackageFeatureUses(product.node),
    generators: parseGenerators(product.node),
    profiles
  };
}

export function parseLocalSettingsManifest(xml: string, manifestPath: string): LocalSettingsManifest {
  const document = parser.parse(xml);
  const root = document.LocalSettings;
  if (!root) {
    throw new Error(`${manifestPath}: root element must be <LocalSettings>`);
  }

  const settings = asArray(root.Settings?.Setting)
    .map((entry): { key: string; secret?: boolean } | undefined => {
      const setting = entry as { Key?: string; Secret?: string | boolean } | undefined;
      return setting?.Key ? { key: setting.Key, secret: setting.Secret === true || setting.Secret === 'true' } : undefined;
    })
    .filter((entry): entry is { key: string; secret?: boolean } => Boolean(entry));

  return {
    path: manifestPath,
    directory: path.dirname(manifestPath),
    settings
  };
}

export function parsePackageManifest(xml: string, manifestPath: string): PackageManifest {
  const document = parser.parse(xml);
  const root = document.Package;
  if (!root) {
    throw new Error(`${manifestPath}: root element must be <Package>`);
  }
  requireCurrentSchema(root, manifestPath);

  const features = asArray(root.Features?.Feature)
    .map((entry): PackageFeature | undefined => {
      const feature = entry as {
        Name?: string;
        Description?: string;
        Uses?: unknown;
        Build?: unknown;
        Generate?: unknown;
        Provides?: { Capability?: unknown };
        Requires?: { Capability?: unknown };
      } | undefined;
      if (!feature?.Name) {
        return undefined;
      }
      return {
        name: feature.Name,
        description: feature.Description,
        dependencies: parseDependencyEntries(feature),
        inputs: parseBuildInputs(feature.Build),
        generators: parseGenerators(feature),
        provides: asArray(feature.Provides?.Capability)
          .map((capability): NonNullable<PackageFeature['provides']>[number] | undefined => {
            const value = capability as { Name?: string; Exclusive?: string | boolean } | undefined;
            return value?.Name
              ? {
                  name: value.Name,
                  exclusive: value.Exclusive === undefined ? undefined : value.Exclusive === true || value.Exclusive === 'true'
                }
              : undefined;
          })
          .filter((entry): entry is NonNullable<PackageFeature['provides']>[number] => Boolean(entry)),
        requires: asArray(feature.Requires?.Capability)
          .map((capability) => {
            const value = capability as { Name?: string } | undefined;
            return value?.Name ? { name: value.Name } : undefined;
          })
          .filter((entry): entry is NonNullable<PackageFeature['requires']>[number] => Boolean(entry))
      };
    })
    .filter((entry): entry is PackageFeature => Boolean(entry));

  return {
    path: manifestPath,
    directory: path.dirname(manifestPath),
    name: root.Name ?? path.basename(manifestPath, path.extname(manifestPath)),
    version: root.Version,
    conditions: parseConditions(root),
    tools: [
      ...asArray(root.Tools?.Tool),
      ...asArray(root.Tool)
    ].map((entry) => parseToolDeclaration(entry, true))
      .filter((entry): entry is ToolDeclaration => Boolean(entry)),
    features
  };
}

export function parseLaunchManifest(xml: string, manifestPath: string): LaunchManifest {
  const document = parser.parse(xml);
  const root = document.LaunchManifest;
  if (!root) {
    throw new Error(`${manifestPath}: root element must be <LaunchManifest>`);
  }

  const launch: LaunchDescriptor = {
    workingDirectory: root.Launch?.WorkingDirectory,
    executable: root.Launch?.Executable,
    target: root.Launch?.Target,
    origin: root.Launch?.Origin
  };

  const stagedFiles = asArray(root.StagedFiles?.File).map((entry): StagedFile => ({
    kind: entry?.Kind ?? '',
    source: entry?.Source,
    destination: entry?.Destination,
    relativeDestination: entry?.RelativeDestination
  })).filter((entry) => Boolean(entry.destination));

  return {
    path: manifestPath,
    directory: path.dirname(manifestPath),
    project: root.Project,
    profile: root.Profile,
    type: root.Type,
    optimization: root.Optimization,
    debugSymbols: root.DebugSymbols === 'true',
    linkTimeOptimization: root.LinkTimeOptimization === 'true',
    backendConfiguration: root.BackendConfiguration,
    platform: root.Platform,
    operatingSystem: root.OperatingSystem,
    architecture: root.Architecture,
    environmentName: root.Environment?.Name,
    launch,
    selectedExecutable: root.Launch?.Executable
      ? {
          name: root.Launch.Executable,
          target: root.Launch.Target,
          origin: root.Launch.Origin
        }
      : undefined,
    stagedFiles
  };
}
