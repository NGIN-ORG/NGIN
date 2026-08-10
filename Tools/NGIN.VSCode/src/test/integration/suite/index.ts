import assert from 'node:assert/strict';
import * as path from 'node:path';
import * as vscode from 'vscode';

export async function run(): Promise<void> {
  const extension = vscode.extensions.getExtension('ngin.ngin-tools');
  assert.ok(extension, 'NGIN extension is discoverable in the extension host');
  await extension.activate();
  assert.equal(extension.isActive, true);

  const commands = await vscode.commands.getCommands(true);
  for (const command of ['ngin.selectProject', 'ngin.configure', 'ngin.build', 'ngin.run', 'ngin.debug', 'ngin.lock', 'ngin.showGraph', 'ngin.openDashboard']) {
    assert.ok(commands.includes(command), `${command} is registered`);
  }

  const workspace = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
  assert.ok(workspace, 'test workspace is open');
  const manifest = vscode.Uri.file(path.join(workspace, 'Examples', 'Hello.Native', 'Hello.Native.nginproj'));
  await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(manifest));
  await vscode.commands.executeCommand('ngin.refresh');
  await vscode.commands.executeCommand('ngin.showGraph');
  const graphDocument = vscode.window.activeTextEditor?.document;
  assert.equal(graphDocument?.languageId, 'json');
  assert.match(graphDocument?.getText() ?? '', /"kind": "NGIN\.CompositionGraph"/);
  assert.match(graphDocument?.getText() ?? '', /"name": "Hello\.Native"/);

  const tasks = await vscode.tasks.fetchTasks({ type: 'ngin' });
  assert.ok(tasks.some(task => task.definition.command === 'build'), 'active project supplies a build task');

  await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(manifest));
  const valid = await vscode.commands.executeCommand<boolean>('ngin.validate');
  assert.equal(valid, true);
}
