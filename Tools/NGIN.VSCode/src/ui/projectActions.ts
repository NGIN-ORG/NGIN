import * as vscode from 'vscode';
import {
  projectActionDescriptors,
  projectActionGroupOrder,
  type ProjectActionDescriptor
} from '../core/projectActions';
import { projectCanLaunch } from '../core/projectCapabilities';
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
  const project = requested ?? owner ?? controller.activeProject;
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
  const descriptors = projectActionDescriptors({
    project,
    context,
    canLaunch: projectCanLaunch(project, graph, project.manifest),
    canTest: Boolean(graph?.testing) || graph?.product.type === 'Test' || Boolean(project.hasTesting),
    hasAnalyze: Boolean(graph?.actions.some(action => action.kind === 'Analyze')) || Boolean(project.hasAnalyze),
    hasFormat: Boolean(graph?.actions.some(action => action.kind === 'Format')) || Boolean(project.hasFormat),
    graphReady: Boolean(graph),
    canPublish: Boolean(graph?.publishes.length),
    configurationChoices: project.workspaceChoices?.configurations.length ?? 0,
    targetChoices: project.workspaceChoices?.targets.length ?? 0,
    toolchainChoices: project.workspaceChoices?.toolchains.length ?? 0,
    selectedLaunch: context.launch,
    busy: snapshot.busy,
    graphError: snapshot.context?.projectManifest === project.manifest ? snapshot.graphError : undefined,
    lastOperation: snapshot.lastOperation?.projectManifest === project.manifest ? snapshot.lastOperation : undefined
  });
  const selected = await vscode.window.showQuickPick(quickPickItems(descriptors), {
    title: `NGIN: ${project.name}`,
    placeHolder: owner ? 'Project owning the active file' : project.manifest === controller.activeProject?.manifest ? 'Default project' : undefined,
    matchOnDescription: true,
    matchOnDetail: true
  });
  if (selected?.descriptor) {
    await vscode.commands.executeCommand(selected.descriptor.command, selected.descriptor.argument);
  }
}
