import { promises as fs } from 'node:fs';
import * as path from 'node:path';
import * as vscode from 'vscode';
import {
  getCppToolsApi,
  Version,
  type CppToolsApi,
  type CustomConfigurationProvider,
  type SourceFileConfigurationItem,
  type WorkspaceBrowseConfiguration
} from 'vscode-cpptools';
import {
  createBrowseConfiguration,
  createFallbackConfiguration,
  createSourceConfiguration,
  parseCompileCommands,
  selectCompileCommand,
  type CompileCommandEntry
} from '../core/compileCommands';
import type { NginController } from '../core/controller';
import { graphOwnsFile } from '../core/projectOwnership';
import { compileCommandsPath, isWithin } from '../core/paths';
import type { CompositionGraph, NginContext } from '../model';

interface ProjectConfigurationContext {
  context: NginContext;
  graph: CompositionGraph;
}

export class NginCppConfigurationProvider implements CustomConfigurationProvider {
  readonly name = 'NGIN';
  readonly extensionId = 'ngin.ngin-tools';
  private api?: CppToolsApi;
  private readonly entries = new Map<string, { modified: number; values: CompileCommandEntry[] }>();
  private readonly configuring = new Map<string, Promise<void>>();
  private readonly owners = new Map<string, ProjectConfigurationContext | null>();
  private readonly subscription: vscode.Disposable;

  constructor(private readonly controller: NginController) {
    this.subscription = controller.onDidChange(() => {
      this.owners.clear();
      if (this.api) {
        this.api.didChangeCustomConfiguration(this);
        this.api.didChangeCustomBrowseConfiguration(this);
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

  private async configure(context: NginContext): Promise<void> {
    const candidate = compileCommandsPath(context);
    const existing = this.configuring.get(candidate);
    if (existing) return existing;
    const request = (async () => {
      if (!vscode.workspace.isTrusted) return;
      await this.controller.execute('configure', [], false, context, { progress: false, announceFailure: false });
    })();
    this.configuring.set(candidate, request);
    try {
      await request;
    } finally {
      this.configuring.delete(candidate);
    }
  }

  private async loadEntries(context: NginContext, configureIfMissing = false): Promise<CompileCommandEntry[]> {
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
      if (configureIfMissing) {
        await this.configure(context);
        return this.loadEntries(context, false);
      }
      return [];
    }
  }

  private async configurationContext(file: string): Promise<ProjectConfigurationContext | undefined> {
    const key = path.resolve(file);
    const cached = this.owners.get(key);
    if (cached !== undefined) return cached ?? undefined;

    for (const project of this.controller.projects) {
      const context = this.controller.contextForProject(project);
      if (!isWithin(context.outputDirectory, file)) continue;
      const graph = await this.controller.graphForContext(context, false);
      if (graph) {
        const result = { context, graph };
        this.owners.set(key, result);
        return result;
      }
    }

    const candidates = this.controller.projectsForFile(file);
    const matches = (await Promise.all(candidates.map(async project => {
      const context = this.controller.contextForProject(project);
      const graph = await this.controller.graphForContext(context, false);
      return graph && graphOwnsFile(graph, context, file) ? { context, graph } : undefined;
    }))).filter((value): value is ProjectConfigurationContext => Boolean(value));
    const activeManifest = this.controller.snapshot.context?.projectManifest;
    const result = matches.find(match => match.context.projectManifest === activeManifest) ?? matches[0];
    this.owners.set(key, result ?? null);
    return result;
  }

  async canProvideConfiguration(uri: vscode.Uri): Promise<boolean> {
    return Boolean(await this.configurationContext(uri.fsPath));
  }

  async provideConfigurations(uris: vscode.Uri[]): Promise<SourceFileConfigurationItem[]> {
    const configurations = await Promise.all(uris.map(async uri => {
      const owner = await this.configurationContext(uri.fsPath);
      if (!owner) return undefined;
      const entries = await this.loadEntries(owner.context, true);
      const entry = selectCompileCommand(entries, uri.fsPath);
      return {
        uri,
        configuration: entry
          ? createSourceConfiguration(entry, owner.graph)
          : createFallbackConfiguration(owner.graph, path.dirname(owner.context.projectManifest))
      };
    }));
    return configurations.filter((value): value is NonNullable<typeof value> => value !== undefined);
  }

  async canProvideBrowseConfiguration(): Promise<boolean> {
    return Boolean(this.controller.snapshot.graph);
  }

  async provideBrowseConfiguration(): Promise<WorkspaceBrowseConfiguration | null> {
    const snapshot = this.controller.snapshot;
    if (!snapshot.context || !snapshot.graph) return null;
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
