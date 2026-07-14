import * as vscode from 'vscode';
import {
  addProfile,
  deleteProfile,
  ProjectDependencyUseEdit,
  ProjectEnvironmentVariableEdit,
  ProjectFeatureState,
  ProjectInputBlock,
  ProjectInputEdit,
  setDependencyUses,
  setEnvironmentVariables,
  setInputEntries,
  setLaunch,
  setProfileFeatureState,
  updateProfile,
  updateProjectAttributes
} from './authoring';
import { buildProjectEditorModel } from './model';
import { CompositionGraphPayload } from '../core/types';

interface ProjectEditorInspectState {
  inspectGraph?: CompositionGraphPayload;
  activeProfile?: string;
}

export interface NginProjectEditorServices {
  inspect(document: vscode.TextDocument): Promise<ProjectEditorInspectState>;
  apply(document: vscode.TextDocument, update: (xml: string) => string): Promise<void>;
  openSource(uri: vscode.Uri): Promise<void>;
  validate(uri: vscode.Uri): Promise<void>;
}

type ProjectEditorMessage =
  | { type: 'ready' }
  | { type: 'openSource' }
  | { type: 'validate' }
  | { type: 'updateProject'; name?: string; defaultProfile?: string }
  | { type: 'setRootLaunch'; executable?: string; workingDirectory?: string }
  | { type: 'addProfile'; name: string }
  | { type: 'deleteProfile'; name: string }
  | {
      type: 'updateProfile';
      originalName: string;
      name: string;
      buildType?: string;
      platform?: string;
      operatingSystem?: string;
      architecture?: string;
      environment?: string;
      launchExecutable?: string;
      launchWorkingDirectory?: string;
    }
  | { type: 'setDependencyUses'; profileName?: string; references: ProjectDependencyUseEdit[] }
  | { type: 'setInputEntries'; profileName?: string; block: ProjectInputBlock; entries: ProjectInputEdit[] }
  | { type: 'setFeatureState'; profileName: string; packageName: string; featureName: string; state: ProjectFeatureState }
  | { type: 'setEnvironmentVariables'; environmentName: string; variables: ProjectEnvironmentVariableEdit[] };

function nonce(): string {
  const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
  let value = '';
  for (let index = 0; index < 32; ++index) {
    value += chars.charAt(Math.floor(Math.random() * chars.length));
  }
  return value;
}

function fullDocumentRange(document: vscode.TextDocument): vscode.Range {
  const last = document.lineAt(Math.max(0, document.lineCount - 1));
  return new vscode.Range(new vscode.Position(0, 0), last.range.end);
}

export class NginProjectEditorProvider implements vscode.CustomTextEditorProvider {
  static readonly viewType = 'ngin.projectEditor';

  constructor(
    private readonly extensionUri: vscode.Uri,
    private readonly services: NginProjectEditorServices
  ) {}

  async resolveCustomTextEditor(document: vscode.TextDocument, webviewPanel: vscode.WebviewPanel): Promise<void> {
    webviewPanel.webview.options = {
      enableScripts: true,
      localResourceRoots: [vscode.Uri.joinPath(this.extensionUri, 'media')]
    };
    webviewPanel.webview.html = this.html(webviewPanel.webview);
    let inspectGeneration = 0;

    const postModel = async (state?: ProjectEditorInspectState): Promise<void> => {
      const model = buildProjectEditorModel(document.getText(), document.uri.fsPath, document.uri.toString(), state?.inspectGraph, state?.activeProfile);
      await webviewPanel.webview.postMessage({ type: 'model', model });
    };

    const update = async (includeInspect: boolean): Promise<void> => {
      await postModel();
      if (!includeInspect) {
        return;
      }

      const generation = ++inspectGeneration;
      try {
        const state = await this.services.inspect(document);
        if (generation === inspectGeneration) {
          await postModel(state);
        }
      } catch {
        // The parse-only editor remains usable when inspect is unavailable.
      }
    };

    const documentChange = vscode.workspace.onDidChangeTextDocument((event) => {
      if (event.document.uri.toString() === document.uri.toString()) {
        void update(false);
      }
    });

    webviewPanel.onDidDispose(() => documentChange.dispose());
    webviewPanel.webview.onDidReceiveMessage((message: ProjectEditorMessage) => {
      void this.handleMessage(document, message).then(() => update(true));
    });

    void update(true);
  }

  private async handleMessage(document: vscode.TextDocument, message: ProjectEditorMessage): Promise<void> {
    switch (message.type) {
      case 'ready':
        return;
      case 'openSource':
        await this.services.openSource(document.uri);
        return;
      case 'validate':
        await this.services.validate(document.uri);
        return;
      case 'updateProject':
        await this.services.apply(document, (xml) => updateProjectAttributes(xml, {
          name: message.name,
          defaultProfile: message.defaultProfile
        }));
        return;
      case 'setRootLaunch':
        await this.services.apply(document, (xml) => setLaunch(xml, undefined, message.executable, message.workingDirectory));
        return;
      case 'addProfile':
        await this.services.apply(document, (xml) => addProfile(xml, message.name));
        return;
      case 'deleteProfile':
        await this.services.apply(document, (xml) => deleteProfile(xml, message.name));
        return;
      case 'updateProfile':
        await this.services.apply(document, (xml) => updateProfile(xml, message));
        return;
      case 'setDependencyUses':
        await this.services.apply(document, (xml) => setDependencyUses(xml, message.references, message.profileName));
        return;
      case 'setInputEntries':
        await this.services.apply(document, (xml) => setInputEntries(xml, message.block, message.entries, message.profileName));
        return;
      case 'setFeatureState':
        await this.services.apply(document, (xml) => setProfileFeatureState(xml, message.profileName, message.packageName, message.featureName, message.state));
        return;
      case 'setEnvironmentVariables':
        await this.services.apply(document, (xml) => setEnvironmentVariables(xml, message.environmentName, message.variables));
        return;
    }
  }

  static async applyTextEdit(document: vscode.TextDocument, nextText: string): Promise<void> {
    if (nextText === document.getText()) {
      return;
    }
    const edit = new vscode.WorkspaceEdit();
    edit.replace(document.uri, fullDocumentRange(document), nextText);
    await vscode.workspace.applyEdit(edit);
  }

  private html(webview: vscode.Webview): string {
    const scriptNonce = nonce();
    const styleUri = webview.asWebviewUri(vscode.Uri.joinPath(this.extensionUri, 'media', 'projectEditor', 'projectEditor.css'));
    const scriptUri = webview.asWebviewUri(vscode.Uri.joinPath(this.extensionUri, 'media', 'projectEditor', 'projectEditor.js'));
    return /* html */`<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src ${webview.cspSource}; script-src 'nonce-${scriptNonce}';">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>NGIN Project Editor</title>
  <link rel="stylesheet" href="${styleUri}">
</head>
<body>
  <div id="app" class="editor-shell" aria-live="polite"></div>
  <script nonce="${scriptNonce}" src="${scriptUri}"></script>
</body>
</html>`;
  }
}
