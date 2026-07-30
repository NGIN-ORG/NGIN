import * as path from 'node:path';
import { runTests } from '@vscode/test-electron';

async function main(): Promise<void> {
  const extensionDevelopmentPath = path.resolve(__dirname, '../../..');
  const extensionTestsPath = path.resolve(__dirname, './suite/index');
  const workspacePath = path.resolve(extensionDevelopmentPath, '../..');

  const electronRunAsNode = process.env.ELECTRON_RUN_AS_NODE;
  delete process.env.ELECTRON_RUN_AS_NODE;
  try {
    await runTests({
      extensionDevelopmentPath,
      extensionTestsPath,
      launchArgs: [workspacePath, '--disable-extensions']
    });
  } finally {
    if (electronRunAsNode !== undefined) {
      process.env.ELECTRON_RUN_AS_NODE = electronRunAsNode;
    }
  }
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
