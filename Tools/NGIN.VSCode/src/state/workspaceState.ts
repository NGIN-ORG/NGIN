import * as path from 'node:path';
import * as vscode from 'vscode';
import { computeCompileCommandsPath, getFallbackCompileCommandsPath } from '../core/compileCommands';
import { computeLaunchManifestPath, computeOutputDir } from '../core/helpers';
import { findNearestWorkspaceManifest, loadWorkspaceProjects, pathExists } from '../core/discovery';
import { PackageCatalogEntry, ProjectInspectPayload, ProjectProfile, ProjectManifest, WorkspaceManifest } from '../core/types';

const LAST_PROJECT_KEY = 'ngin.lastProject';
const LAST_LAUNCH_MANIFEST_KEY = 'ngin.lastLaunchManifest';
const LAST_PROFILE_PREFIX = 'ngin.lastProfile:';

export interface NginCommandTarget {
  preferredUri?: vscode.Uri;
  projectPath?: string;
  profileName?: string;
}

export interface ResolvedWorkspaceInfo {
  workspace: WorkspaceManifest;
  projects: ProjectManifest[];
  packageCatalog?: Record<string, PackageCatalogEntry>;
  root: string;
  folder?: vscode.WorkspaceFolder;
}

export interface ResolvedCommandContext {
  workspace: ResolvedWorkspaceInfo;
  project: ProjectManifest;
  profile: ProjectProfile;
}

export interface NginWorkspaceSnapshot {
  workspace?: ResolvedWorkspaceInfo;
  context?: ResolvedCommandContext;
  buildOutputRoot?: string;
  outputDir?: string;
  launchManifestPath?: string;
  launchManifestExists: boolean;
  stagedCompileCommandsPath?: string;
  stagedCompileCommandsAvailable: boolean;
  activeCompileCommandsPath?: string;
  activeCompileCommandsSource?: 'staged' | 'fallback';
  lastLaunchManifestPath?: string;
  inspect?: ProjectInspectPayload;
  inspectError?: string;
}

function comparablePath(value: string): string {
  const normalized = path.normalize(value);
  return process.platform === 'win32' ? normalized.toLowerCase() : normalized;
}

function projectProfileKey(projectPath: string): string {
  return `${LAST_PROFILE_PREFIX}${comparablePath(projectPath)}`;
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

  getLastLaunchManifestPath(): string | undefined {
    return this.context.workspaceState.get<string>(LAST_LAUNCH_MANIFEST_KEY);
  }

  async setLastLaunchManifestPath(manifestPath: string): Promise<void> {
    await this.context.workspaceState.update(LAST_LAUNCH_MANIFEST_KEY, manifestPath);
    this.fireDidChange();
  }

  async clearLastLaunchManifestPath(manifestPath?: string): Promise<void> {
    if (manifestPath) {
      const current = this.getLastLaunchManifestPath();
      if (!current || comparablePath(current) !== comparablePath(manifestPath)) {
        return;
      }
    }

    await this.context.workspaceState.update(LAST_LAUNCH_MANIFEST_KEY, undefined);
    this.fireDidChange();
  }

  async rememberSelection(context: ResolvedCommandContext): Promise<void> {
    await this.context.workspaceState.update(LAST_PROJECT_KEY, context.project.path);
    await this.context.workspaceState.update(projectProfileKey(context.project.path), context.profile.name);
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

      const { workspace, projects, packageCatalog } = await loadWorkspaceProjects(manifestPath);
      const root = path.dirname(manifestPath);
      return {
        workspace,
        projects,
        packageCatalog,
        root,
        folder: vscode.workspace.getWorkspaceFolder(vscode.Uri.file(root))
      };
    }

    return undefined;
  }

  async resolveCommandContext(options?: {
    preferredUri?: vscode.Uri;
    explicitProjectPath?: string;
    explicitProfile?: string;
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

    const profile = await this.resolveProfile(project, options?.explicitProfile, promptIfNeeded);
    if (!profile) {
      return undefined;
    }

    const context: ResolvedCommandContext = {
      workspace: workspaceInfo,
      project,
      profile
    };

    await this.rememberSelection(context);
    return context;
  }

  async getTaskContexts(): Promise<ResolvedCommandContext[]> {
    const workspaceInfo = await this.getWorkspaceInfo();
    if (!workspaceInfo) {
      return [];
    }

    return workspaceInfo.projects.flatMap((project) => project.profiles.map((profile) => ({
      workspace: workspaceInfo,
      project,
      profile
    })));
  }

  findProject(workspaceInfo: ResolvedWorkspaceInfo, projectPath: string): ProjectManifest | undefined {
    const resolvedPath = path.isAbsolute(projectPath)
      ? projectPath
      : path.resolve(workspaceInfo.root, projectPath);
    return workspaceInfo.projects.find((project) => comparablePath(project.path) === comparablePath(resolvedPath));
  }

  findProfile(project: ProjectManifest, profileName: string): ProjectProfile | undefined {
    return project.profiles.find((profile) => profile.name === profileName);
  }

  async pickProject(workspaceInfo: ResolvedWorkspaceInfo): Promise<ProjectManifest | undefined> {
    return this.resolveProject(workspaceInfo, undefined, undefined, true);
  }

  async pickProfile(project: ProjectManifest): Promise<ProjectProfile | undefined> {
    return this.resolveProfile(project, undefined, true);
  }

  async resolveStoredProfile(project: ProjectManifest): Promise<ProjectProfile | undefined> {
    return this.resolveProfile(project, undefined, false);
  }

  async promptForProject(workspaceInfo: ResolvedWorkspaceInfo): Promise<ProjectManifest | undefined> {
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

  async promptForProfile(project: ProjectManifest): Promise<ProjectProfile | undefined> {
    const picked = await vscode.window.showQuickPick(
      project.profiles.map((profile) => ({
        label: profile.name,
        description: profile.environment ?? [profile.operatingSystem, profile.architecture].filter(Boolean).join('/'),
        profile
      })),
      {
        title: `Select profile for ${project.name}`
      }
    );

    return picked?.profile;
  }

  async getSnapshot(preferredUri?: vscode.Uri): Promise<NginWorkspaceSnapshot> {
    const workspace = await this.getWorkspaceInfo(preferredUri);
    const snapshot: NginWorkspaceSnapshot = {
      workspace,
      launchManifestExists: false,
      stagedCompileCommandsAvailable: false,
      lastLaunchManifestPath: this.getLastLaunchManifestPath()
    };

    if (!workspace) {
      return snapshot;
    }
    snapshot.buildOutputRoot = this.getConfiguredBuildOutputRoot(workspace.folder);

    const project = await this.resolveProject(workspace, undefined, preferredUri, false);
    if (!project) {
      return snapshot;
    }

    const profile = await this.resolveProfile(project, undefined, false);
    if (!profile) {
      return snapshot;
    }

    const context: ResolvedCommandContext = { workspace, project, profile };
    const outputDir = this.computeOutputDirectory(context);
    const launchManifestPath = computeLaunchManifestPath(outputDir, project.name, profile.name);
    const stagedCompileCommandsPath = computeCompileCommandsPath(outputDir);
    const fallbackCompileCommandsPath = getFallbackCompileCommandsPath(workspace.root);
    const stagedCompileCommandsAvailable = await pathExists(stagedCompileCommandsPath);
    const fallbackCompileCommandsAvailable = await pathExists(fallbackCompileCommandsPath);

    snapshot.context = context;
    snapshot.outputDir = outputDir;
    snapshot.launchManifestPath = launchManifestPath;
    snapshot.launchManifestExists = await pathExists(launchManifestPath);
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

  computeOutputDirectory(context: ResolvedCommandContext, override?: string): string {
    if (override) {
      return path.isAbsolute(override) ? override : path.resolve(context.workspace.root, override);
    }

    return computeOutputDir(
      context.workspace.root,
      context.project.name,
      context.profile.name,
      this.getConfiguredBuildOutputRoot(context.workspace.folder)
    );
  }

  private fireDidChange(): void {
    this.onDidChangeEmitter.fire();
  }

  private resolveStoredProject(workspaceInfo: ResolvedWorkspaceInfo): ProjectManifest | undefined {
    const lastProjectPath = this.context.workspaceState.get<string>(LAST_PROJECT_KEY);
    if (lastProjectPath) {
      const lastProject = workspaceInfo.projects.find((project) => comparablePath(project.path) === comparablePath(lastProjectPath));
      if (lastProject) {
        return lastProject;
      }
    }

    if (workspaceInfo.projects.length === 1) {
      return workspaceInfo.projects[0];
    }

    return undefined;
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

    return this.promptForProject(workspaceInfo);
  }

  private async resolveProfile(
    project: ProjectManifest,
    explicitProfile?: string,
    promptIfNeeded = true
  ): Promise<ProjectProfile | undefined> {
    if (explicitProfile) {
      return project.profiles.find((profile) => profile.name === explicitProfile);
    }

    const storedProfile = this.context.workspaceState.get<string>(projectProfileKey(project.path));
    if (storedProfile) {
      const matched = project.profiles.find((profile) => profile.name === storedProfile);
      if (matched) {
        return matched;
      }
    }

    if (project.defaultProfile) {
      const matched = project.profiles.find((profile) => profile.name === project.defaultProfile);
      if (matched) {
        return matched;
      }
    }

    if (project.profiles.length === 1) {
      return project.profiles[0];
    }

    if (!promptIfNeeded) {
      return undefined;
    }

    return this.promptForProfile(project);
  }
}
