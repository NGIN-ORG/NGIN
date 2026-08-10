import { readdirSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';

rmSync('dist-test', { recursive: true, force: true });

const tsc = spawnSync(process.execPath, ['node_modules/typescript/bin/tsc', '-p', './tsconfig.tests.json'], {
  stdio: 'inherit',
  shell: false
});
if (tsc.status !== 0) process.exit(tsc.status ?? 1);
if (process.argv.includes('--compile-only')) process.exit(0);

const files = readdirSync('./dist-test/test/unit')
  .filter(file => file.endsWith('.test.js'))
  .map(file => `./dist-test/test/unit/${file}`);
const tests = spawnSync(process.execPath, ['--test', ...files], {
  stdio: 'inherit',
  shell: false
});
process.exit(tests.status ?? 1);
