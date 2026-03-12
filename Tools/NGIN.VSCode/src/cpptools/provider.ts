import * as path from 'node:path';
import * as vscode from 'vscode';
import {
  CppToolsApi,
  CustomConfigurationProvider,
  SourceFileConfigurationItem,
  Version,
  WorkspaceBrowseConfiguration,
  getCppToolsApi
} from 'vscode-cpptools';
import {
  CompileCommandEntry,
  createBrowseConfiguration,
  createSourceConfiguration,
  parseCompileCommands,
  selectCompileCommand
} from '../core/compileCommands';
import { pathExists, readTextFile } from '../core/discovery';
import { NginWorkspaceSnapshot } from '../state/workspaceState';

interface ActiveCompileDatabase {
  path: string;
  source: 'staged' | 'fallback';
  signature: string;
  entries: CompileCommandEntry[];
}

export class NginCppToolsProviderService implements CustomConfigurationProvider {
  readonly name = 'NGIN';

  private api?: CppToolsApi;
  private registered = false;
  private readyNotified = false;
  private activeDatabase?: ActiveCompileDatabase;
  private readonly watcherDisposables: vscode.Disposable[] = [];

  constructor(
    private readonly extensionIdentifier: string,
    private readonly snapshotProvider: () => Promise<NginWorkspaceSnapshot>,
    private readonly outputChannel: vscode.OutputChannel
  ) {}

  get extensionId(): string {
    return this.extensionIdentifier;
  }

  dispose(): void {
    this.disposeWatchers();
  }

  async refresh(snapshot?: NginWorkspaceSnapshot): Promise<void> {
    if (!vscode.workspace.getConfiguration('ngin').get<boolean>('cpp.configurationProvider.enabled', true)) {
      this.activeDatabase = undefined;
      this.disposeWatchers();
      this.notifyChanged();
      return;
    }

    await this.ensureRegistered();
    const next = await this.loadActiveDatabase(snapshot ?? await this.snapshotProvider());
    const changed = this.activeDatabase?.signature !== next?.signature;

    this.activeDatabase = next;
    this.resetWatchers(next ? [next.path] : []);

    if (changed) {
      if (next) {
        this.outputChannel.appendLine(`[cpptools] using ${next.source} compile database: ${next.path}`);
      } else {
        this.outputChannel.appendLine('[cpptools] compile database unavailable for the current NGIN selection');
      }
      this.notifyChanged();
    }
  }

  async canProvideConfiguration(uri: vscode.Uri): Promise<boolean> {
    if (uri.scheme !== 'file') {
      return false;
    }

    return Boolean(this.activeDatabase?.entries.length);
  }

  async provideConfigurations(uris: vscode.Uri[]): Promise<SourceFileConfigurationItem[]> {
    if (!this.activeDatabase) {
      return [];
    }

    const items: SourceFileConfigurationItem[] = [];
    for (const uri of uris) {
      if (uri.scheme !== 'file') {
        continue;
      }

      const entry = selectCompileCommand(this.activeDatabase.entries, uri.fsPath);
      if (!entry) {
        continue;
      }

      items.push({
        uri,
        configuration: createSourceConfiguration(entry, uri.fsPath, process.platform) as SourceFileConfigurationItem['configuration']
      });
    }

    return items;
  }

  async canProvideBrowseConfiguration(): Promise<boolean> {
    return Boolean(this.activeDatabase?.entries.length);
  }

  async provideBrowseConfiguration(): Promise<WorkspaceBrowseConfiguration | null> {
    if (!this.activeDatabase) {
      return null;
    }

    return createBrowseConfiguration(this.activeDatabase.entries, process.platform) as WorkspaceBrowseConfiguration | null;
  }

  async canProvideBrowseConfigurationsPerFolder(): Promise<boolean> {
    return false;
  }

  async provideFolderBrowseConfiguration(): Promise<WorkspaceBrowseConfiguration | null> {
    return null;
  }

  private async ensureRegistered(): Promise<void> {
    if (this.registered) {
      return;
    }

    this.api = await getCppToolsApi(Version.v2);
    if (!this.api) {
      return;
    }

    this.api.registerCustomConfigurationProvider(this);
    this.registered = true;

    if (this.api.notifyReady && !this.readyNotified) {
      this.api.notifyReady(this);
      this.readyNotified = true;
    } else {
      this.notifyChanged();
    }
  }

  private async loadActiveDatabase(snapshot: NginWorkspaceSnapshot): Promise<ActiveCompileDatabase | undefined> {
    const candidates = [
      snapshot.stagedCompileCommandsAvailable && snapshot.stagedCompileCommandsPath
        ? { path: snapshot.stagedCompileCommandsPath, source: 'staged' as const }
        : undefined,
      snapshot.activeCompileCommandsSource === 'fallback' && snapshot.activeCompileCommandsPath
        ? { path: snapshot.activeCompileCommandsPath, source: 'fallback' as const }
        : undefined
    ].filter((candidate): candidate is { path: string; source: 'staged' | 'fallback' } => Boolean(candidate));

    for (const candidate of candidates) {
      if (!(await pathExists(candidate.path))) {
        continue;
      }

      try {
        const contents = await readTextFile(candidate.path);
        return {
          path: candidate.path,
          source: candidate.source,
          signature: `${candidate.source}:${candidate.path}:${contents}`,
          entries: parseCompileCommands(contents)
        };
      } catch (error) {
        const message = error instanceof Error ? error.message : String(error);
        this.outputChannel.appendLine(`[cpptools] failed to read ${candidate.path}: ${message}`);
      }
    }

    return undefined;
  }

  private notifyChanged(): void {
    if (!this.api || !this.registered) {
      return;
    }

    this.api.didChangeCustomConfiguration(this);
    if (this.api.didChangeCustomBrowseConfiguration) {
      this.api.didChangeCustomBrowseConfiguration(this);
    }
  }

  private resetWatchers(paths: string[]): void {
    this.disposeWatchers();
    for (const filePath of paths) {
      const watcher = vscode.workspace.createFileSystemWatcher(
        new vscode.RelativePattern(path.dirname(filePath), path.basename(filePath))
      );
      const refresh = () => {
        void this.refresh();
      };
      watcher.onDidChange(refresh);
      watcher.onDidCreate(refresh);
      watcher.onDidDelete(refresh);
      this.watcherDisposables.push(watcher);
    }
  }

  private disposeWatchers(): void {
    while (this.watcherDisposables.length > 0) {
      this.watcherDisposables.pop()?.dispose();
    }
  }
}
