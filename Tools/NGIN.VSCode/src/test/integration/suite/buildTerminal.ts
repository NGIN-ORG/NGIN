import assert from 'node:assert/strict';
import { promises as fs } from 'node:fs';
import { tmpdir } from 'node:os';
import * as path from 'node:path';
import * as vscode from 'vscode';

export async function checkBuildInterrupt(workspace: string): Promise<void> {
  const root = await fs.mkdtemp(path.join(tmpdir(), 'ngin-build-interrupt-'));
  const manifest = path.join(root, 'Interrupt.nginproj');
  const launcher = path.join(root, 'compiler-launcher.cjs');
  const marker = path.join(root, 'compiler.pid');
  const previousLauncher = process.env.CMAKE_CXX_COMPILER_LAUNCHER;
  try {
    await fs.mkdir(path.join(root, 'src'));
    await fs.writeFile(path.join(root, 'src', 'main.cpp'), 'int main() {}');
    await fs.writeFile(manifest, '<Executable Name="Interrupt" />');
    await fs.writeFile(launcher, `require('node:fs').writeFileSync(${JSON.stringify(marker)}, String(process.pid));
      setTimeout(() => process.exit(1), 20000);`);
    process.env.CMAKE_CXX_COMPILER_LAUNCHER = `node;${launcher}`;
    // Resolve the workspace CLI while building this standalone temporary manifest.
    const result = vscode.commands.executeCommand('ngin.build', { manifest, directory: workspace, name: 'Interrupt' });
    let pid = 0;
    const deadline = Date.now() + 15000;
    while (!pid && Date.now() < deadline) {
      pid = Number(await fs.readFile(marker, 'utf8').catch(() => ''));
      if (!pid) await new Promise(resolve => setTimeout(resolve, 50));
    }
    assert.ok(pid, 'the compiler launcher is running');
    assert.match(vscode.window.activeTerminal?.name ?? '', /build Interrupt/i);
    const interruptedAt = Date.now();
    await vscode.commands.executeCommand('workbench.action.terminal.sendSequence', { text: '\x03' });
    assert.equal(await result, undefined, 'Ctrl+C cancels the build');
    assert.ok(Date.now() - interruptedAt < 8000, 'the build ended before the compiler fixture could exit naturally');
    assert.throws(() => process.kill(pid, 0), { code: 'ESRCH' }, 'Ctrl+C also stops the compiler descendant');
  } finally {
    if (previousLauncher === undefined) delete process.env.CMAKE_CXX_COMPILER_LAUNCHER;
    else process.env.CMAKE_CXX_COMPILER_LAUNCHER = previousLauncher;
    await fs.rm(root, { recursive: true, force: true });
  }
}
