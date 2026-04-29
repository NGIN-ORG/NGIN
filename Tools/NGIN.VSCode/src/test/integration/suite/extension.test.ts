import * as assert from 'node:assert/strict';
import * as fs from 'node:fs/promises';
import * as os from 'node:os';
import * as path from 'node:path';
import * as vscode from 'vscode';

function repoRoot(): string {
  const folder = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
  assert.ok(folder, 'integration tests should open the repository workspace');
  return folder;
}

function sampleTarget(): { preferredUri: vscode.Uri; projectPath: string; profileName: string } {
  const root = repoRoot();
  const projectPath = path.join(root, 'Examples/App.Basic/App.Basic.nginproj');
  return {
    preferredUri: vscode.Uri.file(projectPath),
    projectPath,
    profileName: 'Runtime'
  };
}

function cliPath(): string {
  return path.join(repoRoot(), 'build/dev/Tools/NGIN.CLI', process.platform === 'win32' ? 'ngin.exe' : 'ngin');
}

async function waitForActiveDocument(predicate: (document: vscode.TextDocument) => boolean): Promise<vscode.TextDocument> {
  for (let attempt = 0; attempt < 40; ++attempt) {
    const document = vscode.window.activeTextEditor?.document;
    if (document && predicate(document)) {
      return document;
    }
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  const active = vscode.window.activeTextEditor?.document;
  assert.fail(`active document did not match; active=${active?.uri.toString() ?? '<none>'}`);
}

suite('NGIN Tools Extension', () => {
  setup(async () => {
    await vscode.workspace.getConfiguration('ngin').update('cli.path', cliPath(), vscode.ConfigurationTarget.Global);
  });

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

    const commands = await vscode.commands.getCommands(true);
    assert.ok(commands.includes('ngin.clean'));
    assert.ok(commands.includes('ngin.rebuild'));
    assert.ok(commands.includes('ngin.variablesExplain'));
    assert.ok(commands.includes('ngin.settingsInit'));
  });

  test('validate command can execute in the sample workspace', async () => {
    const extension = vscode.extensions.getExtension('ngin.ngin-tools');
    assert.ok(extension);
    await extension.activate();

    await vscode.commands.executeCommand('ngin.validate', sampleTarget());
  });

  test('variables explain opens a redacted readonly document without prompting', async () => {
    const extension = vscode.extensions.getExtension('ngin.ngin-tools');
    assert.ok(extension);
    await extension.activate();

    await vscode.commands.executeCommand('ngin.variablesExplain', sampleTarget());

    const document = await waitForActiveDocument((candidate) => candidate.uri.scheme === 'ngin-variables');
    assert.match(document.getText(), /Variables for profile: Runtime/);
    assert.match(document.getText(), /APP_BASIC_API_TOKEN = <missing>/);
    assert.doesNotMatch(document.getText(), /local-development-token|secret-token/);
  });

  test('settings init creates and opens local settings in an explicit temporary workspace', async () => {
    const extension = vscode.extensions.getExtension('ngin.ngin-tools');
    assert.ok(extension);
    await extension.activate();

    const tempRoot = await fs.mkdtemp(path.join(os.tmpdir(), 'ngin-vscode-settings-'));
    try {
      const projectDir = path.join(tempRoot, 'App');
      const projectPath = path.join(projectDir, 'App.nginproj');
      await fs.mkdir(projectDir, { recursive: true });
      await fs.writeFile(path.join(tempRoot, 'Workspace.ngin'), [
        '<?xml version="1.0" encoding="utf-8"?>',
        '<Workspace SchemaVersion="3" Name="TempWorkspace">',
        '  <PackageSources>',
        '    <PackageSource Path="Packages" />',
        '  </PackageSources>',
        '  <Projects>',
        '    <Project Path="App/App.nginproj" />',
        '  </Projects>',
        '</Workspace>'
      ].join('\n'));
      await fs.writeFile(projectPath, [
        '<?xml version="1.0" encoding="utf-8"?>',
        '<Project SchemaVersion="3" Name="Temp.App" Type="Application" DefaultProfile="Runtime">',
        '  <Output Kind="Executable" Name="Temp.App" Target="TempApp" />',
        '  <Environments>',
        '    <Environment Name="dev" />',
        '  </Environments>',
        '  <Profiles>',
        '    <Profile Name="Runtime" BuildType="Debug" OperatingSystem="linux" Architecture="x64" Environment="dev" />',
        '  </Profiles>',
        '</Project>'
      ].join('\n'));

      await vscode.commands.executeCommand('ngin.settingsInit', {
        preferredUri: vscode.Uri.file(projectPath),
        projectPath,
        profileName: 'Runtime'
      });

      const settingsPath = path.join(tempRoot, '.ngin/local/user.nginsettings');
      assert.match(await fs.readFile(settingsPath, 'utf8'), /<LocalSettings SchemaVersion="1">/);
      const document = await waitForActiveDocument((candidate) => candidate.uri.scheme === 'file' && candidate.uri.fsPath === settingsPath);
      assert.equal(document.uri.fsPath, settingsPath);
    } finally {
      await fs.rm(tempRoot, { recursive: true, force: true });
    }
  });
});
