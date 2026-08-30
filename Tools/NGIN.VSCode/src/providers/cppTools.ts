import { promises as fs } from 'node:fs';
import * as path from 'node:path';
import * as vscode from 'vscode';
import {
  getCppToolsApi,
  Version,
  type CppToolsApi,
  type CustomConfigurationProvider,
  type SourceFileConfiguration,
  type SourceFileConfigurationItem,
  type WorkspaceBrowseConfiguration
} from 'vscode-cpptools';
import {
  createBrowseConfiguration,
  createFallbackConfiguration,
  createSourceConfiguration,
  findCompileCommand,
  parseCompileCommands,
  selectCompileCommand,
  splitCommandLine,
  type CompileCommandEntry
} from '../core/compileCommands';
import { sameCMakeSnapshotSet, selectCMakeCompileGroup } from '../core/cmakeConfiguration';
import type { NginController } from '../core/controller';
import { graphOwnsFile } from '../core/projectOwnership';
import { compileCommandsPath, isWithin, normalizeForComparison } from '../core/paths';
import type { CMakeProjectSnapshot, CMakeTargetDescription, CompositionGraph, NginContext } from '../model';

interface ProjectConfigurationContext {
  context: NginContext;
  graph?: CompositionGraph;
  cmake?: CMakeProjectSnapshot;
  cmakeTarget?: CMakeTargetDescription;
  cmakeCompileGroupId?: string;
}

function cmakeSourceConfiguration(owner: ProjectConfigurationContext, file: string): SourceFileConfiguration {
  const target = owner.cmakeTarget;
  const source = target?.sources.find(value => normalizeForComparison(value.path) === normalizeForComparison(file));
  const group = target?.compileGroups.find(value => value.id === source?.compileGroup)
    ?? target?.compileGroups.find(value => value.id === owner.cmakeCompileGroupId)
    ?? target?.compileGroups[0];
  const toolchain = owner.cmake?.cmake.toolchains.find(value => value.language === group?.language);
  return {
    includePath: [...new Set(group?.includes ?? [])],
    defines: [...new Set(group?.defines ?? [])],
    forcedInclude: [],
    compilerPath: toolchain?.compilerPath || undefined,
    compilerArgs: group?.compileCommandFragments.flatMap(splitCommandLine) ?? []
  };
}

export class NginCppConfigurationProvider implements CustomConfigurationProvider {
  readonly name = 'NGIN';
  readonly extensionId = 'ngin.ngin-tools';
  private api?: CppToolsApi;
  private readonly entries = new Map<string, { modified: number; values: CompileCommandEntry[] }>();
  private readonly owners = new Map<string, ProjectConfigurationContext | null>();
  private readonly preparing = new Map<string, Promise<void>>();
  private readonly subscription: vscode.Disposable;
  private generation = 0;
  private lastGraph?: CompositionGraph;
  private lastContext = '';
  private lastCMakeSnapshots = new Map<string, CMakeProjectSnapshot | undefined>();

  constructor(private readonly controller: NginController) {
    this.subscription = controller.onDidChange(snapshot => {
      const context = snapshot.context;
      const contextKey = context
        ? [context.projectManifest, context.configuration, context.target, context.toolchain, context.run, snapshot.configured].join('|')
        : '';
      const cmakeSnapshots = new Map(this.controller.projects
        .filter(project => project.projectSystem === 'CMake')
        .map(project => [project.id ?? project.manifest, this.controller.cmakeSnapshot(project)]));
      const cmakeChanged = !sameCMakeSnapshotSet(this.lastCMakeSnapshots, cmakeSnapshots);
      if (this.lastGraph === snapshot.graph && this.lastContext === contextKey && !cmakeChanged) return;
      this.lastGraph = snapshot.graph;
      this.lastContext = contextKey;
      this.lastCMakeSnapshots = cmakeSnapshots;
      this.generation++;
      const generation = this.generation;
      this.owners.clear();
      this.preparing.clear();
      if (this.api) {
        this.api.didChangeCustomConfiguration(this);
        this.api.didChangeCustomBrowseConfiguration(this);
      }
      if (context && snapshot.graph) {
        void this.loadEntries(context).then(entries => {
          if (generation === this.generation && entries.length) {
            this.api?.didChangeCustomConfiguration(this);
            this.api?.didChangeCustomBrowseConfiguration(this);
          }
        });
      }
    });
  }

  async initialize(): Promise<void> {
    if (!vscode.workspace.getConfiguration('ngin').get<boolean>('cpp.enabled', true)) return;
    this.api = await getCppToolsApi(Version.v7);
    if (!this.api) return;
    this.api.registerCustomConfigurationProvider(this);
    this.api.notifyReady(this);
  }

  dispose(): void {
    this.subscription.dispose();
    this.api?.dispose();
  }

  private async loadEntries(context: NginContext): Promise<CompileCommandEntry[]> {
    const candidate = compileCommandsPath(context);
    try {
      const stat = await fs.stat(candidate);
      const cached = this.entries.get(candidate);
      if (!cached || cached.modified !== stat.mtimeMs) {
        const values = parseCompileCommands(await fs.readFile(candidate, 'utf8'));
        this.entries.set(candidate, { modified: stat.mtimeMs, values });
        return values;
      }
      return cached.values;
    } catch {
      this.entries.delete(candidate);
      return [];
    }
  }

  private cachedConfigurationContext(file: string): ProjectConfigurationContext | undefined {
    const key = path.resolve(file);
    const cached = this.owners.get(key);
    if (cached !== undefined) return cached ?? undefined;

    for (const project of this.controller.projectsForFile?.(key) ?? []) {
      if (project.projectSystem !== 'CMake') continue;
      const cmake = this.controller.cmakeSnapshot(project);
      const selection = cmake && selectCMakeCompileGroup(cmake, key);
      if (cmake && selection) {
        const result = {
          context: this.controller.contextForProject(project),
          cmake,
          cmakeTarget: selection.target,
          cmakeCompileGroupId: selection.group.id
        };
        this.owners.set(key, result);
        return result;
      }
    }

    const snapshot = this.controller.snapshot;
    const context = snapshot.context;
    if (context && snapshot.graph
      && (isWithin(context.outputDirectory, key) || graphOwnsFile(snapshot.graph, context, key))) {
      const result = { context, graph: snapshot.graph };
      this.owners.set(key, result);
      return result;
    }
    if (context && snapshot.graph) {
      const cachedEntries = this.entries.get(compileCommandsPath(context));
      if (cachedEntries && findCompileCommand(cachedEntries.values, key)) {
        const result = { context, graph: snapshot.graph };
        this.owners.set(key, result);
        return result;
      }
    }
    return undefined;
  }

  private async resolveConfigurationContext(file: string): Promise<ProjectConfigurationContext | undefined> {
    const cmake = this.cachedConfigurationContext(file);
    if (cmake?.cmake) return cmake;
    const snapshot = this.controller.snapshot;
    if (snapshot.context) {
      const entries = await this.loadEntries(snapshot.context);
      if (findCompileCommand(entries, file)) {
        const graph = snapshot.graph ?? await this.controller.graphForContext(snapshot.context, false);
        if (graph) return { context: snapshot.context, graph };
      }
    }

    for (const project of this.controller.projects) {
      const context = this.controller.contextForProject(project);
      if (!isWithin(context.outputDirectory, file)) continue;
      const graph = await this.controller.graphForContext(context, false);
      if (graph) {
        return { context, graph };
      }
    }

    const candidates = this.controller.projectsForFile(file);
    const matches = (await Promise.all(candidates.map(async project => {
      const context = this.controller.contextForProject(project);
      const graph = await this.controller.graphForContext(context, false);
      return graph && graphOwnsFile(graph, context, file) ? { context, graph } : undefined;
    }))).filter((value): value is { context: NginContext; graph: CompositionGraph } => Boolean(value));
    const activeManifest = this.controller.snapshot.context?.projectManifest;
    const authored = matches.find(match => match.context.projectManifest === activeManifest) ?? matches[0];
    if (authored) return authored;

    for (const project of this.controller.projects) {
      if (project.manifest === activeManifest) continue;
      const context = this.controller.contextForProject(project);
      if (!findCompileCommand(await this.loadEntries(context), file)) continue;
      const graph = await this.controller.graphForContext(context, false);
      if (graph) return { context, graph };
    }
    return undefined;
  }

  private prepareConfiguration(file: string): void {
    const key = path.resolve(file);
    if (this.preparing.has(key)) return;
    const generation = this.generation;
    const request = this.resolveConfigurationContext(key).catch(() => undefined).then(owner => {
      if (generation !== this.generation) return;
      this.owners.set(key, owner ?? null);
      this.api?.didChangeCustomConfiguration(this);
    });
    this.preparing.set(key, request);
    const clean = (): void => {
      if (this.preparing.get(key) === request) this.preparing.delete(key);
    };
    void request.then(clean, clean);
  }

  async canProvideConfiguration(uri: vscode.Uri, token?: vscode.CancellationToken): Promise<boolean> {
    if (this.cachedConfigurationContext(uri.fsPath)) return true;
    if (this.owners.has(path.resolve(uri.fsPath))) return false;
    if (!token?.isCancellationRequested) this.prepareConfiguration(uri.fsPath);
    return false;
  }

  async provideConfigurations(uris: vscode.Uri[], token?: vscode.CancellationToken): Promise<SourceFileConfigurationItem[]> {
    const configurations = await Promise.all(uris.map(async uri => {
      if (token?.isCancellationRequested) return undefined;
      const owner = this.cachedConfigurationContext(uri.fsPath);
      if (!owner) {
        if (!this.owners.has(path.resolve(uri.fsPath))) this.prepareConfiguration(uri.fsPath);
        return undefined;
      }
      if (owner.cmake) return { uri, configuration: cmakeSourceConfiguration(owner, uri.fsPath) };
      const entries = await this.loadEntries(owner.context);
      const entry = selectCompileCommand(entries, uri.fsPath);
      return {
        uri,
        configuration: entry && owner.graph
          ? createSourceConfiguration(entry, owner.graph)
          : createFallbackConfiguration(owner.graph!, path.dirname(owner.context.projectManifest))
      };
    }));
    return configurations.filter((value): value is NonNullable<typeof value> => value !== undefined);
  }

  async canProvideBrowseConfiguration(): Promise<boolean> {
    const context = this.controller.snapshot.context;
    const project = context && this.controller.projects.find(value => value.manifest === context.projectManifest);
    return Boolean(this.controller.snapshot.graph || project && this.controller.cmakeSnapshot(project));
  }

  async provideBrowseConfiguration(): Promise<WorkspaceBrowseConfiguration | null> {
    const snapshot = this.controller.snapshot;
    if (!snapshot.context) return null;
    const project = this.controller.projects.find(value => value.manifest === snapshot.context?.projectManifest);
    const cmake = project && this.controller.cmakeSnapshot(project);
    if (cmake) {
      const groups = cmake.targets.flatMap(target => target.compileGroups);
      return {
        browsePath: [...new Set([project.directory, ...cmake.cmake.directories, ...groups.flatMap(group => group.includes)])],
        compilerPath: cmake.cmake.toolchains.find(value => value.compilerPath)?.compilerPath,
        compilerArgs: groups[0]?.compileCommandFragments.flatMap(splitCommandLine)
      };
    }
    if (!snapshot.graph) return null;
    return createBrowseConfiguration(
      await this.loadEntries(snapshot.context),
      snapshot.graph,
      path.dirname(snapshot.context.projectManifest)
    );
  }

  async canProvideBrowseConfigurationsPerFolder(): Promise<boolean> {
    return true;
  }

  async provideFolderBrowseConfiguration(uri: vscode.Uri): Promise<WorkspaceBrowseConfiguration | null> {
    const context = this.controller.snapshot.context;
    if (!context || !isWithin(uri.fsPath, context.projectManifest)) return null;
    return this.provideBrowseConfiguration();
  }
}
