import * as assert from 'node:assert/strict';
import * as vscode from 'vscode';

suite('NGIN Tools Extension', () => {
  test('activates and contributes NGIN tasks', async () => {
    const extension = vscode.extensions.getExtension('ngin.ngin-tools');
    assert.ok(extension, 'extension should be discoverable by id');
    await extension.activate();
    assert.equal(extension.isActive, true);

    const tasks = await vscode.tasks.fetchTasks({ type: 'ngin' });
    assert.ok(tasks.some((task) => task.name.startsWith('NGIN: Build')));
    assert.ok(tasks.some((task) => task.name.startsWith('NGIN: Rebuild')));
    assert.ok(tasks.some((task) => task.name.startsWith('NGIN: Clean')));
    assert.ok(tasks.some((task) => task.name.startsWith('NGIN: Validate')));
    assert.ok(tasks.some((task) => task.name.startsWith('NGIN: Metagen')));

    const commands = await vscode.commands.getCommands(true);
    assert.ok(commands.includes('ngin.clean'));
    assert.ok(commands.includes('ngin.rebuild'));
    assert.ok(commands.includes('ngin.metagen'));
    assert.ok(commands.includes('ngin.variablesExplain'));
    assert.ok(commands.includes('ngin.settingsInit'));
  });

  test('validate command can execute in the sample workspace', async () => {
    const extension = vscode.extensions.getExtension('ngin.ngin-tools');
    assert.ok(extension);
    await extension.activate();

    await vscode.commands.executeCommand('ngin.validate');
  });
});
