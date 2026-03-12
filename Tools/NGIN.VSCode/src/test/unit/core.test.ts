import test from 'node:test';
import assert from 'node:assert/strict';
import { createNativeDebugConfiguration } from '../../core/debug';
import {
  basenameWithoutExtension,
  computeOutputDir,
  computeTargetManifestPath,
  getExecutableCandidatePaths,
  getWorkingDirectoryCandidates,
  parseCliDiagnostics
} from '../../core/helpers';
import { parseProjectManifest, parseTargetManifest, parseWorkspaceManifest } from '../../core/xml';

test('computeOutputDir uses the CLI default layout when no root override is configured', () => {
  const outputDir = computeOutputDir('/workspace', 'App.Basic', 'Runtime');
  assert.equal(outputDir, '/workspace/.ngin/build/App.Basic/Runtime');
});

test('computeOutputDir appends project and variant beneath a configured output root', () => {
  const outputDir = computeOutputDir('/workspace', 'App.Basic', 'Runtime', 'build/out');
  assert.equal(outputDir, '/workspace/build/out/App.Basic/Runtime');
});

test('computeTargetManifestPath uses the staged naming convention', () => {
  const manifestPath = computeTargetManifestPath('/workspace/.ngin/build/App.Basic/Runtime', 'App.Basic', 'Runtime');
  assert.equal(manifestPath, '/workspace/.ngin/build/App.Basic/Runtime/App.Basic.Runtime.ngintarget');
});

test('parseCliDiagnostics extracts structured file and generic errors', () => {
  const diagnostics = parseCliDiagnostics([
    'Validation errors:',
    '  - /tmp/App.Basic.nginproj: failed to parse XML: unexpected token',
    'error: unknown variant `Editor`'
  ].join('\n'));

  assert.equal(diagnostics.length, 2);
  assert.equal(diagnostics[0].file, '/tmp/App.Basic.nginproj');
  assert.match(diagnostics[0].message, /failed to parse XML/);
  assert.equal(diagnostics[1].message, 'unknown variant `Editor`');
});

test('workspace manifests parse project paths relative to the workspace manifest', () => {
  const workspace = parseWorkspaceManifest(
    '<?xml version="1.0" encoding="utf-8"?><Workspace Name="NGIN"><Projects><Project Path="Examples/App.Basic/App.Basic.nginproj" /></Projects></Workspace>',
    '/repo/NGIN.ngin'
  );

  assert.equal(workspace.name, 'NGIN');
  assert.deepEqual(workspace.projectPaths, ['/repo/Examples/App.Basic/App.Basic.nginproj']);
});

test('project manifests parse variants and launch metadata', () => {
  const project = parseProjectManifest(
    '<?xml version="1.0" encoding="utf-8"?><Project Name="App.Basic" DefaultVariant="Runtime"><Variants><Variant Name="Runtime" WorkingDirectory="."><Launch Executable="App.Basic" /></Variant></Variants></Project>',
    '/repo/Examples/App.Basic/App.Basic.nginproj'
  );

  assert.equal(project.defaultVariant, 'Runtime');
  assert.equal(project.variants[0].launchExecutable, 'App.Basic');
});

test('target manifests surface selected executable and staged files', () => {
  const target = parseTargetManifest(
    '<?xml version="1.0" encoding="utf-8"?><TargetLayout Project="App.Basic" Variant="Runtime"><Runtime WorkingDirectory="." /><SelectedExecutable Name="App.Basic" /><StagedFiles><File Kind="executable" Destination="/repo/out/bin/App.Basic" RelativeDestination="bin/App.Basic" /></StagedFiles></TargetLayout>',
    '/repo/out/App.Basic.Runtime.ngintarget'
  );

  assert.equal(target.project, 'App.Basic');
  assert.equal(target.selectedExecutable?.name, 'App.Basic');
  assert.equal(target.stagedFiles[0].kind, 'executable');
});

test('executable resolution prefers staged manifest entries before bin fallback', () => {
  const candidates = getExecutableCandidatePaths(
    {
      path: '/repo/out/App.Basic.Runtime.ngintarget',
      directory: '/repo/out',
      project: 'App.Basic',
      variant: 'Runtime',
      runtime: { workingDirectory: '.' },
      selectedExecutable: { name: 'App.Basic' },
      stagedFiles: [
        {
          kind: 'executable',
          destination: '/repo/out/bin/App.Basic',
          relativeDestination: 'bin/App.Basic'
        }
      ]
    },
    '/repo/out',
    'linux'
  );

  assert.deepEqual(candidates, ['/repo/out/bin/App.Basic']);
});

test('working directory resolution checks staged and project-relative candidates', () => {
  const candidates = getWorkingDirectoryCandidates(
    {
      path: '/repo/out/App.Basic.Runtime.ngintarget',
      directory: '/repo/out',
      project: 'App.Basic',
      variant: 'Runtime',
      runtime: { workingDirectory: 'config' },
      stagedFiles: []
    },
    '/repo/out',
    '/repo/Examples/App.Basic'
  );

  assert.deepEqual(candidates, [
    '/repo/out/config',
    '/repo/Examples/App.Basic/config',
    '/repo/out'
  ]);
});

test('native debug configuration maps to cppdbg on Linux', () => {
  const configuration = createNativeDebugConfiguration({
    platform: 'linux',
    program: '/repo/out/bin/App.Basic',
    cwd: '/repo/out',
    args: [],
    env: {},
    miDebuggerPath: '/usr/bin/gdb'
  });

  assert.equal(configuration.type, 'cppdbg');
  assert.equal(configuration.MIMode, 'gdb');
  assert.equal(configuration.miDebuggerPath, '/usr/bin/gdb');
});

test('basenameWithoutExtension strips a platform executable suffix', () => {
  assert.equal(basenameWithoutExtension('/repo/out/bin/App.Basic.exe'), 'App.Basic');
});
