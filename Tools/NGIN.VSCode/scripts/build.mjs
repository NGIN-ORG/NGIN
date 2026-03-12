import * as esbuild from 'esbuild';

const watch = process.argv.includes('--watch');

const options = {
  entryPoints: ['src/extension.ts'],
  bundle: true,
  platform: 'node',
  format: 'cjs',
  outfile: 'dist/extension.js',
  sourcemap: true,
  external: ['vscode'],
  logLevel: 'info',
  target: ['node20']
};

if (watch) {
  const context = await esbuild.context(options);
  await context.watch();
  console.log('[ngin-tools] watching for changes');
} else {
  await esbuild.build(options);
}
