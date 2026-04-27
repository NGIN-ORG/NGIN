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
  computeLaunchManifestPath,
  computeOutputDir,
  getExecutableCandidatePaths,
  getWorkingDirectoryCandidates,
  parseCliDiagnostics
} from '../../core/helpers';
import { addRootConfigSource, relativeManifestPath, removeConfigSources, renameConfigSources } from '../../core/projectAuthoring';
import { buildOverviewSections, buildProjectTreeModels, buildStatusBarModel } from '../../ui/models';
import { parseLaunchManifest, parseProjectManifest, parseWorkspaceManifest } from '../../core/xml';

test('computeOutputDir uses the CLI default layout when no root override is configured', () => {
  const outputDir = computeOutputDir('/workspace', 'App.Basic', 'Runtime');
  assert.equal(outputDir, '/workspace/.ngin/build/App.Basic/Runtime');
});

test('computeOutputDir appends project and configuration beneath a configured output root', () => {
  const outputDir = computeOutputDir('/workspace', 'App.Basic', 'Runtime', 'build/out');
  assert.equal(outputDir, '/workspace/build/out/App.Basic/Runtime');
});

test('computeLaunchManifestPath uses the staged naming convention', () => {
  const manifestPath = computeLaunchManifestPath('/workspace/.ngin/build/App.Basic/Runtime', 'App.Basic', 'Runtime');
  assert.equal(manifestPath, '/workspace/.ngin/build/App.Basic/Runtime/App.Basic.Runtime.nginlaunch');
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
    'error: unknown configuration `Editor`'
  ].join('\n'));

  assert.equal(diagnostics.length, 2);
  assert.equal(diagnostics[0].file, '/tmp/App.Basic.nginproj');
  assert.match(diagnostics[0].message, /failed to parse XML/);
  assert.equal(diagnostics[1].message, 'unknown configuration `Editor`');
});

test('workspace manifests parse project paths relative to the workspace manifest', () => {
  const workspace = parseWorkspaceManifest(
    '<?xml version="1.0" encoding="utf-8"?><Workspace Name="NGIN"><PackageSources><PackageSource Path="Packages" /></PackageSources><Projects><Project Path="Examples/App.Basic/App.Basic.nginproj" /></Projects></Workspace>',
    '/repo/NGIN.ngin'
  );

  assert.equal(workspace.name, 'NGIN');
  assert.deepEqual(workspace.projectPaths, ['/repo/Examples/App.Basic/App.Basic.nginproj']);
  assert.deepEqual(workspace.packageSourcePaths, ['/repo/Packages']);
});

test('project manifests parse configurations and launch metadata', () => {
  const project = parseProjectManifest(
    '<?xml version="1.0" encoding="utf-8"?><Project Name="App.Basic" DefaultConfiguration="Runtime"><SourceRoots><SourceRoot Path="src" /></SourceRoots><Build><Sources><Source Path="src/main.cpp" /></Sources></Build><References><Project Path="../Game.Engine/Game.Engine.nginproj" /><Package Name="NGIN.Core" /></References><ConfigSources><Config Source="config/app.cfg" /></ConfigSources><Configurations><Configuration Name="Runtime" BuildConfiguration="Debug" OperatingSystem="linux" Architecture="x64" Environment="development"><Launch Executable="App.Basic" WorkingDirectory="." /><References><Package Name="NGIN.Reflection" Optional="true" /></References><ConfigSources><Config Source="config/runtime.cfg" /></ConfigSources></Configuration></Configurations></Project>',
    '/repo/Examples/App.Basic/App.Basic.nginproj'
  );

  assert.equal(project.defaultConfiguration, 'Runtime');
  assert.deepEqual(project.sourceRoots, ['src']);
  assert.deepEqual(project.buildSources, ['src/main.cpp']);
  assert.deepEqual(project.configSources, ['config/app.cfg']);
  assert.deepEqual(project.projectRefs, [{ path: '/repo/Examples/Game.Engine/Game.Engine.nginproj', configuration: undefined }]);
  assert.deepEqual(project.packageRefs, [{ name: 'NGIN.Core', version: undefined, optional: false }]);
  assert.deepEqual(project.configurations[0].configSources, ['config/runtime.cfg']);
  assert.deepEqual(project.configurations[0].packageRefs, [{ name: 'NGIN.Reflection', version: undefined, optional: true }]);
  assert.equal(project.configurations[0].launchExecutable, 'App.Basic');
  assert.equal(project.configurations[0].operatingSystem, 'linux');
  assert.equal(project.configurations[0].architecture, 'x64');
});

test('launch manifests surface selected executable and staged files', () => {
  const launch = parseLaunchManifest(
    '<?xml version="1.0" encoding="utf-8"?><LaunchManifest Project="App.Basic" Configuration="Runtime" Type="Application" BuildConfiguration="Debug" OperatingSystem="linux" Architecture="x64"><Launch Executable="App.Basic" WorkingDirectory="." /><Environment Name="development"><Variables /><Features /></Environment><StagedFiles><File Kind="executable" Destination="/repo/out/bin/App.Basic" RelativeDestination="bin/App.Basic" /></StagedFiles></LaunchManifest>',
    '/repo/out/App.Basic.Runtime.nginlaunch'
  );

  assert.equal(launch.project, 'App.Basic');
  assert.equal(launch.configuration, 'Runtime');
  assert.equal(launch.selectedExecutable?.name, 'App.Basic');
  assert.equal(launch.stagedFiles[0].kind, 'executable');
});

test('config source authoring inserts root config sources once', () => {
  const xml = [
    '<?xml version="1.0" encoding="utf-8"?>',
    '<Project Name="App.Basic">',
    '  <SourceRoots>',
    '    <SourceRoot Path="src" />',
    '  </SourceRoots>',
    '  <Configurations />',
    '</Project>'
  ].join('\n');

  const added = addRootConfigSource(xml, 'config/new.cfg');
  assert.equal(added.changed, true);
  assert.match(added.xml, /<ConfigSources>\n    <Config Source="config\/new.cfg" \/>\n  <\/ConfigSources>/);

  const duplicate = addRootConfigSource(added.xml, 'config/new.cfg');
  assert.equal(duplicate.changed, false);
  assert.equal((duplicate.xml.match(/config\/new\.cfg/g) ?? []).length, 1);
});

test('relativeManifestPath uses project-relative slash paths', () => {
  assert.equal(relativeManifestPath('/repo/Examples/App.Basic', '/repo/Examples/App.Basic/config/new.cfg'), 'config/new.cfg');
});

test('config source authoring renames and removes nested config sources', () => {
  const xml = [
    '<Project Name="App.Basic">',
    '  <ConfigSources>',
    '    <Config Source="config/app.cfg" />',
    '    <Config Source="config/nested/dev.cfg" />',
    '  </ConfigSources>',
    '</Project>'
  ].join('\n');

  const renamed = renameConfigSources(xml, 'config/nested', 'config/copy', true);
  assert.equal(renamed.changed, true);
  assert.match(renamed.xml, /Source="config\/copy\/dev\.cfg"/);

  const removed = removeConfigSources(renamed.xml, 'config/copy', true);
  assert.equal(removed.changed, true);
  assert.doesNotMatch(removed.xml, /config\/copy\/dev\.cfg/);
  assert.match(removed.xml, /config\/app\.cfg/);
});

test('executable resolution prefers staged manifest entries before bin fallback', () => {
  const candidates = getExecutableCandidatePaths(
    {
      path: '/repo/out/App.Basic.Runtime.nginlaunch',
      directory: '/repo/out',
      project: 'App.Basic',
      configuration: 'Runtime',
      launch: { workingDirectory: '.' },
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
      path: '/repo/out/App.Basic.Runtime.nginlaunch',
      directory: '/repo/out',
      project: 'App.Basic',
      configuration: 'Runtime',
      launch: { workingDirectory: 'config' },
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
      project: { path: '/repo/Examples/App.Basic/App.Basic.nginproj', directory: '/repo/Examples/App.Basic', name: 'App.Basic', sourceRoots: [], configSources: [], buildSources: [], configurations: [] },
      configuration: { name: 'Runtime', operatingSystem: 'linux', architecture: 'x64', environment: 'development', configSources: [] }
    },
    outputDir: '/repo/.ngin/build/App.Basic/Runtime',
    launchManifestPath: '/repo/.ngin/build/App.Basic/Runtime/App.Basic.Runtime.nginlaunch',
    launchManifestExists: true,
    stagedCompileCommandsPath: '/repo/.ngin/build/App.Basic/Runtime/.ngin/cmake-build/compile_commands.json',
    stagedCompileCommandsAvailable: true,
    activeCompileCommandsPath: '/repo/.ngin/build/App.Basic/Runtime/.ngin/cmake-build/compile_commands.json',
    activeCompileCommandsSource: 'staged',
    lastLaunchManifestPath: '/repo/.ngin/build/App.Basic/Runtime/App.Basic.Runtime.nginlaunch'
  });

  assert.equal(sections.length, 5);
  assert.equal(sections[1].label, 'Current Context');
  assert.equal(sections[1].children[0].label, 'Project: App.Basic');
  assert.equal(sections[1].children[1].label, 'Configuration: Runtime');
  assert.equal(sections[1].children[1].command, 'ngin.selectConfiguration');
  assert.equal(sections[2].label, 'Build Artifacts');
  assert.equal(sections[2].children[0].label, 'Output Folder');
  assert.equal(sections[2].children[0].command, 'ngin.internal.revealPath');
  assert.equal(sections[3].children[0].command, 'ngin.build');
  assert.equal(sections[3].children[1].command, 'ngin.configure');
  assert.equal(sections[3].children[2].command, 'ngin.rebuild');
  assert.equal(sections[3].children[3].command, 'ngin.clean');
  assert.equal(sections[3].children.some((entry) => entry.command === 'ngin.metagen'), true);
  assert.equal(sections[4].children[1].label, 'Open Last Launch Manifest');
});

test('project tree models mark the selected project and configuration', () => {
  const models = buildProjectTreeModels({
    workspace: {
      workspace: { path: '/repo/NGIN.ngin', directory: '/repo', name: 'NGIN', projectPaths: [] },
      projects: [
        {
          path: '/repo/Examples/App.Basic/App.Basic.nginproj',
          directory: '/repo/Examples/App.Basic',
          name: 'App.Basic',
          defaultConfiguration: 'Runtime',
          sourceRoots: ['src'],
          configSources: ['config/app.cfg'],
          buildSources: [],
          projectRefs: [{ path: '/repo/Examples/Game.Engine/Game.Engine.nginproj' }],
          packageRefs: [{ name: 'NGIN.Core' }],
          configurations: [{ name: 'Runtime', operatingSystem: 'linux', architecture: 'x64', environment: 'development', configSources: [] }]
        },
        {
          path: '/repo/Examples/Game.Engine/Game.Engine.nginproj',
          directory: '/repo/Examples/Game.Engine',
          name: 'Game.Engine',
          defaultConfiguration: 'Runtime',
          sourceRoots: [],
          configSources: [],
          buildSources: [],
          configurations: []
        }
      ],
      packageCatalog: {
        'NGIN.Core': { name: 'NGIN.Core', path: '/repo/Packages/NGIN.Core/NGIN.Core.nginpkg', directory: '/repo/Packages/NGIN.Core' }
      },
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
        defaultConfiguration: 'Runtime',
        sourceRoots: ['src'],
        configSources: ['config/app.cfg'],
        buildSources: [],
        projectRefs: [{ path: '/repo/Examples/Game.Engine/Game.Engine.nginproj' }],
        packageRefs: [{ name: 'NGIN.Core' }],
        configurations: [{ name: 'Runtime', operatingSystem: 'linux', architecture: 'x64', environment: 'development', configSources: [] }]
      },
      configuration: { name: 'Runtime', operatingSystem: 'linux', architecture: 'x64', environment: 'development', configSources: [] }
    },
    launchManifestExists: false,
    stagedCompileCommandsAvailable: false
  });

  assert.equal(models.projects[0].selected, true);
  assert.deepEqual(models.childrenByProject.get('/repo/Examples/App.Basic/App.Basic.nginproj')?.map((entry) => entry.kind === 'group' ? entry.group : entry.kind), [
    'manifest',
    'source',
    'config',
    'dependencies',
    'generated',
    'configurations'
  ]);
  assert.deepEqual(models.dependenciesByProject.get('/repo/Examples/App.Basic/App.Basic.nginproj')?.projects.map((entry) => entry.label), ['Game.Engine']);
  assert.deepEqual(models.dependenciesByProject.get('/repo/Examples/App.Basic/App.Basic.nginproj')?.packages.map((entry) => entry.label), ['NGIN.Core']);
  assert.equal(models.configurationsByProject.get('/repo/Examples/App.Basic/App.Basic.nginproj')?.[0].selected, true);
});

test('project tree dependency models group mixed references and deduplicate owners', () => {
  const models = buildProjectTreeModels({
    workspace: {
      workspace: { path: '/repo/NGIN.ngin', directory: '/repo', name: 'NGIN', projectPaths: [] },
      projects: [
        {
          path: '/repo/App/App.nginproj',
          directory: '/repo/App',
          name: 'App',
          defaultConfiguration: 'Runtime',
          sourceRoots: [],
          configSources: [],
          buildSources: [],
          projectRefs: [{ path: '/repo/Game.Engine/Game.Engine.nginproj' }],
          packageRefs: [{ name: 'NGIN.Core' }],
          configurations: [
            {
              name: 'Runtime',
              environment: 'development',
              configSources: [],
              projectRefs: [{ path: '/repo/Game.Engine/Game.Engine.nginproj' }],
              packageRefs: [{ name: 'NGIN.Core' }, { name: 'NGIN.Reflection' }]
            }
          ]
        },
        {
          path: '/repo/Game.Engine/Game.Engine.nginproj',
          directory: '/repo/Game.Engine',
          name: 'Game.Engine',
          sourceRoots: [],
          configSources: [],
          buildSources: [],
          configurations: []
        }
      ],
      packageCatalog: {
        'NGIN.Core': { name: 'NGIN.Core', path: '/repo/Packages/NGIN.Core/NGIN.Core.nginpkg', directory: '/repo/Packages/NGIN.Core' }
      },
      root: '/repo'
    },
    launchManifestExists: false,
    stagedCompileCommandsAvailable: false
  });

  const dependencies = models.dependenciesByProject.get('/repo/App/App.nginproj');
  assert.deepEqual(dependencies?.projects.map((entry) => `${entry.label}:${entry.description}`), ['Game.Engine:Project, Runtime']);
  assert.deepEqual(dependencies?.packages.map((entry) => `${entry.label}:${entry.description}:${entry.targetPath ?? 'unresolved'}`), [
    'NGIN.Core:Project, Runtime:/repo/Packages/NGIN.Core/NGIN.Core.nginpkg',
    'NGIN.Reflection:Runtime:unresolved'
  ]);
});

test('status bar models expose the compact NGIN bottom-bar actions', () => {
  const model = buildStatusBarModel({
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
      project: { path: '/repo/Examples/App.Basic/App.Basic.nginproj', directory: '/repo/Examples/App.Basic', name: 'App.Basic', sourceRoots: [], configSources: [], buildSources: [], configurations: [] },
      configuration: { name: 'Runtime', operatingSystem: 'linux', architecture: 'x64', environment: 'development', configSources: [] }
    },
    outputDir: '/repo/.ngin/build/App.Basic/Runtime',
    launchManifestExists: false,
    stagedCompileCommandsAvailable: false
  });

  assert.equal(model.visible, true);
  assert.match(model.workspace?.text ?? '', /\$\(folder-library\)/);
  assert.equal(model.project?.command, 'ngin.internal.pickProject');
  assert.equal(model.configuration?.command, 'ngin.internal.pickConfiguration');
  assert.match(model.configuration?.text ?? '', /\$\(symbol-enum\) Runtime/);
  assert.equal(model.configure?.command, 'ngin.configure');
  assert.equal(model.build?.command, 'ngin.build');
  assert.equal(model.run?.command, 'ngin.run');
  assert.equal(model.debug?.command, 'ngin.debug');
});
