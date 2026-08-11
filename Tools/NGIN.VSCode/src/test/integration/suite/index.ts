import assert from 'node:assert/strict';
import * as path from 'node:path';
import * as vscode from 'vscode';

export async function run(): Promise<void> {
  const extension = vscode.extensions.getExtension('ngin.ngin-tools');
  assert.ok(extension, 'NGIN extension is discoverable in the extension host');
  await extension.activate();
  assert.equal(extension.isActive, true);

  const commands = await vscode.commands.getCommands(true);
  for (const command of ['ngin.setDefaultProject', 'ngin.createProject', 'ngin.showFilesView', 'ngin.showProjectView', 'ngin.newSourceFile', 'ngin.newHeaderFile', 'ngin.configure', 'ngin.build', 'ngin.run', 'ngin.runWithArguments', 'ngin.debug', 'ngin.lock', 'ngin.analyzeFile', 'ngin.revealOwningProject', 'ngin.showGraph', 'ngin.openDashboard']) {
    assert.ok(commands.includes(command), `${command} is registered`);
  }

  const workspace = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
  assert.ok(workspace, 'test workspace is open');
  await vscode.commands.executeCommand('ngin.showFilesView');
  await vscode.commands.executeCommand('ngin.showProjectView');
  const manifest = vscode.Uri.file(path.join(workspace, 'Examples', 'Hello.Native', 'Hello.Native.nginproj'));
  await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(manifest));
  await vscode.commands.executeCommand('ngin.refresh');
  await vscode.commands.executeCommand('ngin.setDefaultProject', {
    manifest: manifest.fsPath,
    directory: path.dirname(manifest.fsPath),
    name: 'Hello.Native',
    type: 'Application'
  });
  await vscode.commands.executeCommand('ngin.showGraph');
  const graphDocument = vscode.window.activeTextEditor?.document;
  assert.equal(graphDocument?.languageId, 'json');
  assert.match(graphDocument?.getText() ?? '', /"kind": "NGIN\.CompositionGraph"/);
  assert.match(graphDocument?.getText() ?? '', /"name": "Hello\.Native"/);

  const tasks = await vscode.tasks.fetchTasks({ type: 'ngin' });
  assert.ok(tasks.some(task => task.definition.command === 'build'), 'active project supplies a build task');

  await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(manifest));
  const manifestDocument = vscode.window.activeTextEditor!.document;
  const typeOffset = manifestDocument.getText().indexOf('Type="') + 'Type="'.length;
  const completion = await vscode.commands.executeCommand<vscode.CompletionList>(
    'vscode.executeCompletionItemProvider', manifest, manifestDocument.positionAt(typeOffset), '"'
  );
  assert.ok(completion.items.some(item => item.label === 'Application'));
  assert.ok(completion.items.some(item => item.label === 'Library'));
  const valid = await vscode.commands.executeCommand<boolean>('ngin.validate');
  assert.equal(valid, true);

  const staleManifest = await vscode.workspace.openTextDocument({
    language: 'ngin', content: '<Project Name="Legacy" Type="Module" Linkage="HeaderOnly" />'
  });
  const fixes = await vscode.commands.executeCommand<(vscode.CodeAction | vscode.Command)[]>(
    'vscode.executeCodeActionProvider', staleManifest.uri, new vscode.Range(0, 0, 0, staleManifest.getText().length)
  );
  assert.ok(fixes.some(fix => fix.title === 'Change product type to Library'));
  assert.ok(fixes.some(fix => fix.title === 'Change library linkage to Interface'));

  const analyzerManifest = path.join(workspace, 'Examples', 'Hello.Analyzer', 'Hello.Analyzer.nginproj');
  const analyzerProject = {
    manifest: analyzerManifest,
    directory: path.dirname(analyzerManifest),
    name: 'Hello.Analyzer',
    type: 'Application',
    workspaceManifest: path.join(workspace, 'NGIN.ngin'),
    hasAnalyze: true
  };
  await vscode.commands.executeCommand('ngin.enableProjectTooling', analyzerProject);
  const analyzedSource = vscode.Uri.file(path.join(path.dirname(analyzerManifest), 'src', 'main.cpp'));
  await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(analyzedSource));
  const deadline = Date.now() + 20_000;
  let sourceProblems = vscode.languages.getDiagnostics(analyzedSource);
  while (!sourceProblems.some(problem => problem.code === 'readability-magic-numbers') && Date.now() < deadline) {
    await new Promise(resolve => setTimeout(resolve, 100));
    sourceProblems = vscode.languages.getDiagnostics(analyzedSource);
  }
  const tidy = sourceProblems.find(problem => problem.code === 'readability-magic-numbers');
  assert.ok(tidy, 'opening a source file publishes its resolved analyzer diagnostics');
  assert.equal(tidy.source, 'NGIN.Tooling.ClangTidy::Analyze');
}
