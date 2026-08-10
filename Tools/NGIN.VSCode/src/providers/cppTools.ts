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
import { compileCommandsPath, isWithin } from '../core/paths';

export class NginCppConfigurationProvider implements CustomConfigurationProvider {
  readonly name = 'NGIN';
  readonly extensionId = 'ngin.ngin-tools';
  private api?: CppToolsApi;
  private entries: CompileCommandEntry[] = [];
  private loadedPath?: string;
  private loadedTime = -1;
  private readonly subscription: vscode.Disposable;

  constructor(private readonly controller: NginController) {
    this.subscription = controller.onDidChange(() => {
      this.loadedPath = undefined;
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

  private async loadEntries(): Promise<CompileCommandEntry[]> {
    const context = this.controller.snapshot.context;
    if (!context) return [];
    const candidate = compileCommandsPath(context);
    try {
      const stat = await fs.stat(candidate);
      if (candidate !== this.loadedPath || stat.mtimeMs !== this.loadedTime) {
        this.entries = parseCompileCommands(await fs.readFile(candidate, 'utf8'));
        this.loadedPath = candidate;
        this.loadedTime = stat.mtimeMs;
      }
    } catch {
      this.entries = [];
      this.loadedPath = candidate;
      this.loadedTime = -1;
    }
    return this.entries;
  }

  async canProvideConfiguration(uri: vscode.Uri): Promise<boolean> {
    const context = this.controller.snapshot.context;
    return Boolean(context && isWithin(path.dirname(context.projectManifest), uri.fsPath));
  }

  async provideConfigurations(uris: vscode.Uri[]): Promise<SourceFileConfigurationItem[]> {
    const snapshot = this.controller.snapshot;
    if (!snapshot.context || !snapshot.graph) return [];
    const entries = await this.loadEntries();
    const projectDirectory = path.dirname(snapshot.context.projectManifest);
    return uris.filter(uri => isWithin(projectDirectory, uri.fsPath)).map(uri => {
      const entry = selectCompileCommand(entries, uri.fsPath);
      return {
        uri,
        configuration: entry
          ? createSourceConfiguration(entry, snapshot.graph!)
          : createFallbackConfiguration(snapshot.graph!, projectDirectory)
      };
    });
  }

  async canProvideBrowseConfiguration(): Promise<boolean> {
    return Boolean(this.controller.snapshot.graph);
  }

  async provideBrowseConfiguration(): Promise<WorkspaceBrowseConfiguration | null> {
    const snapshot = this.controller.snapshot;
    if (!snapshot.context || !snapshot.graph) return null;
    return createBrowseConfiguration(await this.loadEntries(), snapshot.graph, path.dirname(snapshot.context.projectManifest));
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
