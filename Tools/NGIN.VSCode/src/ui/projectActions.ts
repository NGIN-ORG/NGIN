import * as vscode from 'vscode';
import {
  projectActionDescriptors,
  projectActionGroupOrder,
  type ProjectActionDescriptor
} from '../core/projectActions';
import { projectCanRun } from '../core/projectCapabilities';
import type { ProjectCandidate } from '../model';
import type { NginController } from '../core/controller';
import type { SourceAnalysisProvider } from '../providers/sourceAnalysis';

interface ActionPick extends vscode.QuickPickItem {
  descriptor?: ProjectActionDescriptor;
}

function quickPickItems(descriptors: ProjectActionDescriptor[]): ActionPick[] {
  return projectActionGroupOrder.flatMap(group => {
    const values = descriptors.filter(value => value.group === group);
    if (!values.length) return [];
    return [
      { label: group, kind: vscode.QuickPickItemKind.Separator },
      ...values.map(descriptor => ({
        label: descriptor.icon ? `$(${descriptor.icon}) ${descriptor.label}` : descriptor.label,
        description: descriptor.description,
        detail: descriptor.detail,
        descriptor
      }))
    ];
  });
}

export async function openProjectActions(
  controller: NginController,
  analysis: SourceAnalysisProvider,
  requested?: ProjectCandidate
): Promise<void> {
  const activeFile = vscode.window.activeTextEditor?.document.uri.fsPath;
  const owner = !requested && activeFile ? await analysis.projectForFile(activeFile, false) : undefined;
  const project = requested ?? owner ?? controller.launchProduct;
  if (!project) {
    const selected = await vscode.window.showQuickPick([
      { label: '$(new-folder) Create NGIN Project', command: 'ngin.createProject' },
      { label: '$(book) Open Getting Started', command: 'ngin.openWalkthrough' },
      { label: '$(pulse) Check Setup', command: 'ngin.checkSetup' }
    ], { title: 'NGIN: Project Actions', placeHolder: 'No NGIN project is available' });
    if (selected) await vscode.commands.executeCommand(selected.command);
    return;
  }
  const context = analysis.contextForProject(project);
  const graph = await controller.graphForContext(context, false);
  const snapshot = controller.snapshot;
  const current = project.id && snapshot.context?.projectId
    ? project.id === snapshot.context.projectId
    : snapshot.context?.projectManifest === project.manifest;
  const operationMatches = project.id && snapshot.lastOperation?.projectId
    ? project.id === snapshot.lastOperation.projectId
    : snapshot.lastOperation?.projectManifest === project.manifest;
  const descriptors = projectActionDescriptors({
    project,
    context,
    canRun: projectCanRun(project, graph, project.manifest),
    canTest: Boolean(graph?.tests.length) || Boolean(project.hasTests),
    canBenchmark: Boolean(graph?.benchmarks.length) || Boolean(project.hasBenchmarks),
    hasAnalyze: Boolean(graph?.actions.some(action => action.kind === 'Analyze')) || Boolean(project.hasAnalyze),
    hasFormat: Boolean(graph?.actions.some(action => action.kind === 'Format')) || Boolean(project.hasFormat),
    graphReady: Boolean(graph),
    canPublish: Boolean(graph?.publishes.length),
    configurationChoices: project.workspaceChoices?.configurations.length ?? 0,
    targetChoices: project.workspaceChoices?.targets.length ?? 0,
    toolchainChoices: project.workspaceChoices?.toolchains.length ?? 0,
    selectedRun: context.run,
    busy: snapshot.busy,
    graphError: current ? snapshot.graphError : undefined,
    lastOperation: operationMatches ? snapshot.lastOperation : undefined,
    trusted: vscode.workspace.isTrusted
  });
  const selected = await vscode.window.showQuickPick(quickPickItems(descriptors), {
    title: `NGIN: ${project.name}`,
    placeHolder: owner ? 'Project owning the current file' : project.manifest === controller.launchProduct?.manifest ? 'Active Project' : undefined,
    matchOnDescription: true,
    matchOnDetail: true
  });
  if (selected?.descriptor) {
    await vscode.commands.executeCommand(selected.descriptor.command, selected.descriptor.argument);
  }
}
