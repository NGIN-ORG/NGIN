import * as path from 'node:path';
import { runTests } from '@vscode/test-electron';

async function main(): Promise<void> {
  // Codex and VS Code terminals can inherit extension-host variables. Electron
  // would otherwise start as plain Node instead of runing the test host.
  for (const name of ['ELECTRON_RUN_AS_NODE', 'VSCODE_CLI', 'VSCODE_IPC_HOOK', 'VSCODE_ESM_ENTRYPOINT']) {
    delete process.env[name];
  }
  const extensionDevelopmentPath = path.resolve(__dirname, '../../..');
  const extensionTestsPath = path.resolve(__dirname, './suite/index');
  const workspace = path.resolve(extensionDevelopmentPath, '../..');
  await runTests({
    version: '1.131.0',
    extensionDevelopmentPath,
    extensionTestsPath,
    launchArgs: [workspace, '--disable-extensions', '--skip-welcome', '--skip-release-notes']
  });
}

main().catch(error => {
  console.error(error);
  process.exit(1);
});
