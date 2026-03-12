import * as path from 'node:path';
import * as vscode from 'vscode';
import { DEFAULT_BUILD_CONFIGURATION, normalizeBuildConfiguration } from '../core/buildConfiguration';
import { computeCompileCommandsPath, getFallbackCompileCommandsPath } from '../core/compileCommands';
import { computeOutputDir, computeTargetManifestPath } from '../core/helpers';
import {
  findNearestWorkspaceManifest,
  loadWorkspaceProjects,
  pathExists
} from '../core/discovery';
import { ProjectManifest, ProjectVariant, WorkspaceManifest } from '../core/types';

const LAST_PROJECT_KEY = 'ngin.lastProject';
const LAST_CONFIGURATION_KEY = 'ngin.lastConfiguration';
const LAST_TARGET_MANIFEST_KEY = 'ngin.lastTargetManifest';
const LAST_VARIANT_PREFIX = 'ngin.lastVariant:';

export interface NginCommandTarget {
  preferredUri?: vscode.Uri;
  projectPath?: string;
  variantName?: string;
}

export interface ResolvedWorkspaceInfo {
  workspace: WorkspaceManifest;
  projects: ProjectManifest[];
  root: string;
  folder?: vscode.WorkspaceFolder;
}

export interface ResolvedCommandContext {
  workspace: ResolvedWorkspaceInfo;
  project: ProjectManifest;
  variant: ProjectVariant;
}

export interface NginWorkspaceSnapshot {
  workspace?: ResolvedWorkspaceInfo;
  context?: ResolvedCommandContext;
  buildConfiguration: string;
  outputDir?: string;
  targetManifestPath?: string;
  targetManifestExists: boolean;
  stagedCompileCommandsPath?: string;
  stagedCompileCommandsAvailable: boolean;
  activeCompileCommandsPath?: string;
  activeCompileCommandsSource?: 'staged' | 'fallback';
  lastTargetManifestPath?: string;
}

function comparablePath(value: string): string {
  const normalized = path.normalize(value);
  return process.platform === 'win32' ? normalized.toLowerCase() : normalized;
}

function workspaceVariantKey(projectPath: string): string {
  return `${LAST_VARIANT_PREFIX}${comparablePath(projectPath)}`;
}

export class WorkspaceStateService implements vscode.Disposable {
  private readonly onDidChangeEmitter = new vscode.EventEmitter<void>();
  private readonly disposables: vscode.Disposable[];

  readonly onDidChange = this.onDidChangeEmitter.event;

  constructor(private readonly context: vscode.ExtensionContext) {
    this.disposables = [
      this.onDidChangeEmitter,
      vscode.workspace.onDidChangeWorkspaceFolders(() => this.fireDidChange()),
      vscode.window.onDidChangeActiveTextEditor(() => this.fireDidChange()),
      vscode.workspace.onDidChangeConfiguration((event) => {
        if (
          event.affectsConfiguration('ngin.build.outputRoot') ||
          event.affectsConfiguration('ngin.ui.statusBar.enabled') ||
          event.affectsConfiguration('ngin.cpp.configurationProvider.enabled')
        ) {
          this.fireDidChange();
        }
      })
    ];
  }

  dispose(): void {
    vscode.Disposable.from(...this.disposables).dispose();
  }

  async refresh(): Promise<void> {
    this.fireDidChange();
  }

  getLastTargetManifestPath(): string | undefined {
    return this.context.workspaceState.get<string>(LAST_TARGET_MANIFEST_KEY);
  }

  async setLastTargetManifestPath(manifestPath: string): Promise<void> {
    await this.context.workspaceState.update(LAST_TARGET_MANIFEST_KEY, manifestPath);
    this.fireDidChange();
  }

  async rememberSelection(context: ResolvedCommandContext): Promise<void> {
    await this.context.workspaceState.update(LAST_PROJECT_KEY, context.project.path);
    await this.context.workspaceState.update(workspaceVariantKey(context.project.path), context.variant.name);
    this.fireDidChange();
  }

  async getWorkspaceInfo(preferredUri?: vscode.Uri): Promise<ResolvedWorkspaceInfo | undefined> {
    const candidatePaths: string[] = [];

    if (preferredUri?.scheme === 'file') {
      candidatePaths.push(preferredUri.fsPath);
    }

    const activeUri = vscode.window.activeTextEditor?.document.uri;
    if (activeUri?.scheme === 'file' && !candidatePaths.includes(activeUri.fsPath)) {
      candidatePaths.push(activeUri.fsPath);
    }

    for (const folder of vscode.workspace.workspaceFolders ?? []) {
      if (!candidatePaths.includes(folder.uri.fsPath)) {
        candidatePaths.push(folder.uri.fsPath);
      }
    }

    for (const candidate of candidatePaths) {
      const manifestPath = await findNearestWorkspaceManifest(candidate);
      if (!manifestPath) {
        continue;
      }

      const { workspace, projects } = await loadWorkspaceProjects(manifestPath);
      const root = path.dirname(manifestPath);
      return {
        workspace,
        projects,
        root,
        folder: vscode.workspace.getWorkspaceFolder(vscode.Uri.file(root))
      };
    }

    return undefined;
  }

  async resolveCommandContext(options?: {
    preferredUri?: vscode.Uri;
    explicitProjectPath?: string;
    explicitVariant?: string;
    promptIfNeeded?: boolean;
  }): Promise<ResolvedCommandContext | undefined> {
    const promptIfNeeded = options?.promptIfNeeded ?? true;
    const workspaceInfo = await this.getWorkspaceInfo(options?.preferredUri);
    if (!workspaceInfo) {
      return undefined;
    }

    const project = await this.resolveProject(workspaceInfo, options?.explicitProjectPath, options?.preferredUri, promptIfNeeded);
    if (!project) {
      return undefined;
    }

    const variant = await this.resolveVariant(project, options?.explicitVariant, promptIfNeeded);
    if (!variant) {
      return undefined;
    }

    const context: ResolvedCommandContext = {
      workspace: workspaceInfo,
      project,
      variant
    };

    await this.rememberSelection(context);
    return context;
  }

  async getTaskContexts(): Promise<ResolvedCommandContext[]> {
    const workspaceInfo = await this.getWorkspaceInfo();
    if (!workspaceInfo) {
      return [];
    }

    return workspaceInfo.projects.flatMap((project) => project.variants.map((variant) => ({
      workspace: workspaceInfo,
      project,
      variant
    })));
  }

  findProject(workspaceInfo: ResolvedWorkspaceInfo, projectPath: string): ProjectManifest | undefined {
    const resolvedPath = path.isAbsolute(projectPath)
      ? projectPath
      : path.resolve(workspaceInfo.root, projectPath);
    return workspaceInfo.projects.find((project) => comparablePath(project.path) === comparablePath(resolvedPath));
  }

  findVariant(project: ProjectManifest, variantName: string): ProjectVariant | undefined {
    return project.variants.find((variant) => variant.name === variantName);
  }

  async pickProject(workspaceInfo: ResolvedWorkspaceInfo): Promise<ProjectManifest | undefined> {
    return this.resolveProject(workspaceInfo, undefined, undefined, true);
  }

  async pickVariant(project: ProjectManifest): Promise<ProjectVariant | undefined> {
    return this.resolveVariant(project, undefined, true);
  }

  async getSnapshot(preferredUri?: vscode.Uri): Promise<NginWorkspaceSnapshot> {
    const workspace = await this.getWorkspaceInfo(preferredUri);
    const snapshot: NginWorkspaceSnapshot = {
      buildConfiguration: this.getSelectedBuildConfiguration(),
      workspace,
      targetManifestExists: false,
      stagedCompileCommandsAvailable: false,
      lastTargetManifestPath: this.getLastTargetManifestPath()
    };

    if (!workspace) {
      return snapshot;
    }

    const project = await this.resolveProject(workspace, undefined, preferredUri, false);
    if (!project) {
      return snapshot;
    }

    const variant = await this.resolveVariant(project, undefined, false);
    if (!variant) {
      return snapshot;
    }

    const context: ResolvedCommandContext = { workspace, project, variant };
    const outputDir = this.computeOutputDirectory(context, undefined, snapshot.buildConfiguration);
    const targetManifestPath = computeTargetManifestPath(outputDir, project.name, variant.name);
    const stagedCompileCommandsPath = computeCompileCommandsPath(outputDir);
    const fallbackCompileCommandsPath = getFallbackCompileCommandsPath(workspace.root);
    const stagedCompileCommandsAvailable = await pathExists(stagedCompileCommandsPath);
    const fallbackCompileCommandsAvailable = await pathExists(fallbackCompileCommandsPath);

    snapshot.context = context;
    snapshot.outputDir = outputDir;
    snapshot.targetManifestPath = targetManifestPath;
    snapshot.targetManifestExists = await pathExists(targetManifestPath);
    snapshot.stagedCompileCommandsPath = stagedCompileCommandsPath;
    snapshot.stagedCompileCommandsAvailable = stagedCompileCommandsAvailable;
    snapshot.activeCompileCommandsPath = stagedCompileCommandsAvailable
      ? stagedCompileCommandsPath
      : fallbackCompileCommandsAvailable
        ? fallbackCompileCommandsPath
        : stagedCompileCommandsPath;
    snapshot.activeCompileCommandsSource = stagedCompileCommandsAvailable
      ? 'staged'
      : fallbackCompileCommandsAvailable
        ? 'fallback'
        : undefined;

    return snapshot;
  }

  getConfiguredBuildOutputRoot(scope?: vscode.WorkspaceFolder): string | undefined {
    return vscode.workspace.getConfiguration('ngin', scope).get<string>('build.outputRoot')?.trim() || undefined;
  }

  getSelectedBuildConfiguration(): string {
    return normalizeBuildConfiguration(this.context.workspaceState.get<string>(LAST_CONFIGURATION_KEY) ?? DEFAULT_BUILD_CONFIGURATION);
  }

  async setSelectedBuildConfiguration(configurationName: string): Promise<void> {
    await this.context.workspaceState.update(LAST_CONFIGURATION_KEY, normalizeBuildConfiguration(configurationName));
    this.fireDidChange();
  }

  computeOutputDirectory(context: ResolvedCommandContext, override?: string, configurationName?: string): string {
    if (override) {
      return path.isAbsolute(override) ? override : path.resolve(context.workspace.root, override);
    }

    return computeOutputDir(
      context.workspace.root,
      context.project.name,
      context.variant.name,
      this.getConfiguredBuildOutputRoot(context.workspace.folder),
      configurationName ?? this.getSelectedBuildConfiguration()
    );
  }

  private fireDidChange(): void {
    this.onDidChangeEmitter.fire();
  }

  private async resolveProject(
    workspaceInfo: ResolvedWorkspaceInfo,
    explicitProjectPath?: string,
    preferredUri?: vscode.Uri,
    promptIfNeeded = true
  ): Promise<ProjectManifest | undefined> {
    if (explicitProjectPath) {
      const projectPath = path.isAbsolute(explicitProjectPath)
        ? explicitProjectPath
        : path.resolve(workspaceInfo.root, explicitProjectPath);
      return workspaceInfo.projects.find((project) => comparablePath(project.path) === comparablePath(projectPath));
    }

    const activeDocument = preferredUri?.scheme === 'file'
      ? preferredUri.fsPath
      : vscode.window.activeTextEditor?.document.uri.scheme === 'file'
        ? vscode.window.activeTextEditor.document.uri.fsPath
        : undefined;

    if (activeDocument) {
      const matchedProject = workspaceInfo.projects.find((project) => {
        const projectDir = comparablePath(project.directory) + path.sep;
        const documentPath = comparablePath(activeDocument);
        return documentPath === comparablePath(project.path) || documentPath.startsWith(projectDir);
      });
      if (matchedProject) {
        return matchedProject;
      }
    }

    if (workspaceInfo.projects.length === 1) {
      return workspaceInfo.projects[0];
    }

    const lastProjectPath = this.context.workspaceState.get<string>(LAST_PROJECT_KEY);
    if (lastProjectPath) {
      const lastProject = workspaceInfo.projects.find((project) => comparablePath(project.path) === comparablePath(lastProjectPath));
      if (lastProject) {
        return lastProject;
      }
    }

    if (!promptIfNeeded) {
      return undefined;
    }

    const picked = await vscode.window.showQuickPick(
      workspaceInfo.projects.map((project) => ({
        label: project.name,
        description: path.relative(workspaceInfo.root, project.path),
        project
      })),
      {
        title: 'Select NGIN project'
      }
    );

    return picked?.project;
  }

  private async resolveVariant(project: ProjectManifest, explicitVariant?: string, promptIfNeeded = true): Promise<ProjectVariant | undefined> {
    if (explicitVariant) {
      return project.variants.find((variant) => variant.name === explicitVariant);
    }

    const storedVariant = this.context.workspaceState.get<string>(workspaceVariantKey(project.path));
    if (storedVariant) {
      const matched = project.variants.find((variant) => variant.name === storedVariant);
      if (matched) {
        return matched;
      }
    }

    if (project.defaultVariant) {
      const matched = project.variants.find((variant) => variant.name === project.defaultVariant);
      if (matched) {
        return matched;
      }
    }

    if (project.variants.length === 1) {
      return project.variants[0];
    }

    if (!promptIfNeeded) {
      return undefined;
    }

    const picked = await vscode.window.showQuickPick(
      project.variants.map((variant) => ({
        label: variant.name,
        description: variant.profile ?? '',
        variant
      })),
      {
        title: `Select variant for ${project.name}`
      }
    );

    return picked?.variant;
  }
}
