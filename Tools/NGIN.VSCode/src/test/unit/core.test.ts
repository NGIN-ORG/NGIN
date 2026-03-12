import test from 'node:test';
import assert from 'node:assert/strict';
import {
  computeCompileCommandsPath,
  createBrowseConfiguration,
  createSourceConfiguration,
  getFallbackCompileCommandsPath,
  selectCompileCommand
} from '../../core/compileCommands';
import { createNativeDebugConfiguration } from '../../core/debug';
import {
  basenameWithoutExtension,
  computeOutputDir,
  computeTargetManifestPath,
  getExecutableCandidatePaths,
  getWorkingDirectoryCandidates,
  parseCliDiagnostics
} from '../../core/helpers';
import { buildOverviewSections, buildProjectTreeModels, buildStatusBarModel } from '../../ui/models';
import { parseProjectManifest, parseTargetManifest, parseWorkspaceManifest } from '../../core/xml';

test('computeOutputDir uses the CLI default layout when no root override is configured', () => {
  const outputDir = computeOutputDir('/workspace', 'App.Basic', 'Runtime');
  assert.equal(outputDir, '/workspace/.ngin/build/App.Basic/Runtime/Debug');
});

test('computeOutputDir appends project and variant beneath a configured output root', () => {
  const outputDir = computeOutputDir('/workspace', 'App.Basic', 'Runtime', 'build/out', 'Release');
  assert.equal(outputDir, '/workspace/build/out/App.Basic/Runtime/Release');
});

test('computeTargetManifestPath uses the staged naming convention', () => {
  const manifestPath = computeTargetManifestPath('/workspace/.ngin/build/App.Basic/Runtime', 'App.Basic', 'Runtime');
  assert.equal(manifestPath, '/workspace/.ngin/build/App.Basic/Runtime/App.Basic.Runtime.ngintarget');
});

test('computeCompileCommandsPath uses the staged cmake-build location', () => {
  const compileCommandsPath = computeCompileCommandsPath('/workspace/.ngin/build/App.Basic/Runtime');
  assert.equal(compileCommandsPath, '/workspace/.ngin/build/App.Basic/Runtime/.ngin/cmake-build/compile_commands.json');
});

test('getFallbackCompileCommandsPath uses the dev compile database', () => {
  const fallbackPath = getFallbackCompileCommandsPath('/workspace');
  assert.equal(fallbackPath, '/workspace/build/dev/compile_commands.json');
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

test('selectCompileCommand falls back to the closest matching directory for headers', () => {
  const entry = selectCompileCommand([
    {
      directory: '/repo/build',
      file: '/repo/src/main.cpp',
      command: '/usr/bin/clang++ -I/repo/include -c /repo/src/main.cpp'
    },
    {
      directory: '/repo/build',
      file: '/repo/src/engine/render/Renderer.cpp',
      command: '/usr/bin/clang++ -I/repo/include -c /repo/src/engine/render/Renderer.cpp'
    }
  ], '/repo/src/engine/render/Renderer.hpp');

  assert.equal(entry?.file, '/repo/src/engine/render/Renderer.cpp');
});

test('createSourceConfiguration maps compile commands to cpptools-friendly fields', () => {
  const configuration = createSourceConfiguration(
    {
      directory: '/repo/build',
      file: '/repo/src/main.cpp',
      command: '/usr/bin/clang++ -I../include -DAPP=1 -std=c++23 -include prelude.hpp -c /repo/src/main.cpp'
    },
    '/repo/src/main.cpp',
    'linux'
  );

  assert.equal(configuration.compilerPath, '/usr/bin/clang++');
  assert.match(configuration.compilerArgs?.join(' ') ?? '', /-std=c\+\+23/);
  assert.deepEqual(configuration.includePath, ['/repo/include']);
  assert.deepEqual(configuration.defines, ['APP=1']);
  assert.deepEqual(configuration.forcedInclude, ['/repo/build/prelude.hpp']);
  assert.equal(configuration.intelliSenseMode, 'linux-clang-x64');
});

test('createBrowseConfiguration aggregates include paths across compile commands', () => {
  const browse = createBrowseConfiguration([
    {
      directory: '/repo/build',
      file: '/repo/src/main.cpp',
      command: '/usr/bin/g++ -I/repo/include -I/repo/vendor/include -c /repo/src/main.cpp'
    },
    {
      directory: '/repo/build',
      file: '/repo/src/game/Game.cpp',
      command: '/usr/bin/g++ -I/repo/game/include -c /repo/src/game/Game.cpp'
    }
  ], 'linux');

  assert.ok(browse);
  assert.deepEqual(browse?.browsePath, [
    '/repo/include',
    '/repo/vendor/include',
    '/repo/game/include',
    '/repo/src',
    '/repo/src/game'
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

test('overview sections describe the current workspace selection and actions', () => {
  const sections = buildOverviewSections({
    buildConfiguration: 'Release',
    workspace: {
      workspace: { path: '/repo/NGIN.ngin', directory: '/repo', name: 'NGIN', projectPaths: [] },
      projects: [],
      root: '/repo'
    },
    context: {
      workspace: {
        workspace: { path: '/repo/NGIN.ngin', directory: '/repo', name: 'NGIN', projectPaths: [] },
        projects: [],
        root: '/repo'
      },
      project: { path: '/repo/Examples/App.Basic/App.Basic.nginproj', directory: '/repo/Examples/App.Basic', name: 'App.Basic', variants: [] },
      variant: { name: 'Runtime', profile: 'Game' }
    },
    outputDir: '/repo/.ngin/build/App.Basic/Runtime/Release',
    targetManifestPath: '/repo/.ngin/build/App.Basic/Runtime/Release/App.Basic.Runtime.ngintarget',
    targetManifestExists: true,
    stagedCompileCommandsPath: '/repo/.ngin/build/App.Basic/Runtime/Release/.ngin/cmake-build/compile_commands.json',
    stagedCompileCommandsAvailable: true,
    activeCompileCommandsPath: '/repo/.ngin/build/App.Basic/Runtime/Release/.ngin/cmake-build/compile_commands.json',
    activeCompileCommandsSource: 'staged',
    lastTargetManifestPath: '/repo/.ngin/build/App.Basic/Runtime/Release/App.Basic.Runtime.ngintarget'
  });

  assert.equal(sections.length, 4);
  assert.equal(sections[1].children[0].label, 'App.Basic');
  assert.equal(sections[1].children[2].label, 'Release');
  assert.equal(sections[1].children[2].command, 'ngin.selectConfiguration');
  assert.equal(sections[3].children[0].command, 'ngin.build');
});

test('project tree models mark the selected project and variant', () => {
  const models = buildProjectTreeModels({
    buildConfiguration: 'Debug',
    workspace: {
      workspace: { path: '/repo/NGIN.ngin', directory: '/repo', name: 'NGIN', projectPaths: [] },
      projects: [
        {
          path: '/repo/Examples/App.Basic/App.Basic.nginproj',
          directory: '/repo/Examples/App.Basic',
          name: 'App.Basic',
          defaultVariant: 'Runtime',
          variants: [{ name: 'Runtime', profile: 'Game' }]
        }
      ],
      root: '/repo'
    },
    context: {
      workspace: {
        workspace: { path: '/repo/NGIN.ngin', directory: '/repo', name: 'NGIN', projectPaths: [] },
        projects: [],
        root: '/repo'
      },
      project: {
        path: '/repo/Examples/App.Basic/App.Basic.nginproj',
        directory: '/repo/Examples/App.Basic',
        name: 'App.Basic',
        defaultVariant: 'Runtime',
        variants: [{ name: 'Runtime', profile: 'Game' }]
      },
      variant: { name: 'Runtime', profile: 'Game' }
    },
    targetManifestExists: false,
    stagedCompileCommandsAvailable: false
  });

  assert.equal(models.projects[0].selected, true);
  assert.equal(models.variantsByProject.get('/repo/Examples/App.Basic/App.Basic.nginproj')?.[0].selected, true);
});

test('status bar models expose the compact NGIN bottom-bar actions', () => {
  const model = buildStatusBarModel({
    buildConfiguration: 'Release',
    workspace: {
      workspace: { path: '/repo/NGIN.ngin', directory: '/repo', name: 'NGIN', projectPaths: [] },
      projects: [],
      root: '/repo'
    },
    context: {
      workspace: {
        workspace: { path: '/repo/NGIN.ngin', directory: '/repo', name: 'NGIN', projectPaths: [] },
        projects: [],
        root: '/repo'
      },
      project: { path: '/repo/Examples/App.Basic/App.Basic.nginproj', directory: '/repo/Examples/App.Basic', name: 'App.Basic', variants: [] },
      variant: { name: 'Runtime', profile: 'Game' }
    },
    outputDir: '/repo/.ngin/build/App.Basic/Runtime/Release',
    targetManifestExists: false,
    stagedCompileCommandsAvailable: false
  });

  assert.equal(model.visible, true);
  assert.match(model.workspace?.text ?? '', /\$\(folder-library\)/);
  assert.match(model.configuration?.text ?? '', /\$\(settings-gear\) Release/);
  assert.equal(model.configuration?.command, 'ngin.selectConfiguration');
  assert.equal(model.build?.command, 'ngin.build');
  assert.equal(model.run?.command, 'ngin.run');
  assert.equal(model.debug?.command, 'ngin.debug');
});
