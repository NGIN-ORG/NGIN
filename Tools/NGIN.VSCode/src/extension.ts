import { execFile } from 'node:child_process';
import { dirname } from 'node:path';
import { promisify } from 'node:util';
import * as vscode from 'vscode';
import { registerManifestCompletion } from './manifestCompletion';

const execute = promisify(execFile);

function executable(): string {
  return vscode.workspace.getConfiguration('ngin').get<string>('executable', 'ngin');
}

function activeManifest(): vscode.TextDocument {
  const document = vscode.window.activeTextEditor?.document;
  if (!document || document.languageId !== 'ngin') {
    throw new Error('Open an NGIN manifest first.');
  }
  return document;
}

async function run(arguments_: string[], cwd: string): Promise<string> {
  try {
    const result = await execute(executable(), arguments_, { cwd, windowsHide: true });
    return `${result.stdout}${result.stderr}`;
  } catch (error) {
    const failure = error as { stdout?: string; stderr?: string; message?: string };
    throw new Error(`${failure.stdout ?? ''}${failure.stderr ?? ''}`.trim() || failure.message || 'NGIN command failed');
  }
}

export function activate(context: vscode.ExtensionContext): void {
  const diagnostics = vscode.languages.createDiagnosticCollection('ngin');
  context.subscriptions.push(diagnostics, registerManifestCompletion(context));

  const validate = async (document = activeManifest(), announce = true): Promise<void> => {
    diagnostics.delete(document.uri);
    const output = await run(['validate', '--project', document.uri.fsPath, '--quiet'], vscode.workspace.getWorkspaceFolder(document.uri)?.uri.fsPath ?? document.uri.fsPath);
    if (announce) vscode.window.showInformationMessage(output.trim() || 'NGIN manifest is valid.');
  };

  context.subscriptions.push(
    vscode.commands.registerCommand('ngin.validateManifest', () => validate()),
    vscode.commands.registerCommand('ngin.formatManifest', async () => {
      const document = activeManifest();
      await document.save();
    await run(['manifest', 'format', '--project', document.uri.fsPath], dirname(document.uri.fsPath));
      await vscode.commands.executeCommand('workbench.action.files.revert');
    }),
    vscode.commands.registerCommand('ngin.showSchema', async () => {
      const output = await run(['schema', '--format', 'json'], vscode.workspace.workspaceFolders?.[0]?.uri.fsPath ?? process.cwd());
      const document = await vscode.workspace.openTextDocument({ language: 'json', content: output });
      await vscode.window.showTextDocument(document);
    }),
    vscode.workspace.onDidSaveTextDocument(async (document) => {
      if (document.languageId !== 'ngin' || !vscode.workspace.getConfiguration('ngin').get<boolean>('validateOnSave', true)) return;
      try { await validate(document, false); }
      catch (error) { vscode.window.showErrorMessage(error instanceof Error ? error.message : String(error)); }
    })
  );
}

export function deactivate(): void {}
