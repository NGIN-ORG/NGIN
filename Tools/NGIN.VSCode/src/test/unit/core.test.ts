import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import path from 'node:path';
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
  extractInitializedSettingsPath,
  extractLocalSettingsWarnings,
  getExecutableCandidatePaths,
  getWorkingDirectoryCandidates,
  parseCliDiagnostics
} from '../../core/helpers';
import { addRootConfigInput, relativeManifestPath, removeConfigInputs, renameConfigInputs } from '../../core/projectAuthoring';
import { buildOverviewSections, buildProjectTreeModels, buildStatusBarModel } from '../../ui/models';
import { parseLaunchManifest, parseLocalSettingsManifest, parseModelManifest, parsePackageManifest, parseProjectManifest, parseWorkspaceManifest } from '../../core/xml';

test('computeOutputDir uses the CLI default layout when no root override is configured', () => {
  const outputDir = computeOutputDir('/workspace', 'App.Basic', 'Runtime');
  assert.equal(outputDir, '/workspace/.ngin/build/App.Basic/Runtime');
});

test('computeOutputDir appends project and profile beneath a configured output root', () => {
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
    '  - /tmp/user.nginsettings: duplicate local setting key',
    'error: unknown profile `Editor`'
  ].join('\n'));

  assert.equal(diagnostics.length, 3);
  assert.equal(diagnostics[0].file, '/tmp/App.Basic.nginproj');
  assert.match(diagnostics[0].message, /failed to parse XML/);
  assert.equal(diagnostics[1].file, '/tmp/user.nginsettings');
  assert.match(diagnostics[1].message, /duplicate local setting key/);
  assert.equal(diagnostics[2].message, 'unknown profile `Editor`');
});

test('settings init output exposes the initialized settings path', () => {
  const output = [
    'Initialized local settings',
    '  settings: "/repo/.ngin/local/user.nginsettings" [created]',
    '  gitignore: "/repo/.gitignore" [ok]'
  ].join('\n');

  assert.equal(extractInitializedSettingsPath(output), '/repo/.ngin/local/user.nginsettings');
});

test('local settings warnings are extracted from CLI output', () => {
  const warnings = extractLocalSettingsWarnings([
    'Warnings:',
    '  - repository-local settings file \'/repo/.ngin/local/user.nginsettings\' is tracked by git; local settings under .ngin/local should be ignored'
  ].join('\n'));

  assert.equal(warnings.length, 1);
  assert.match(warnings[0], /tracked by git/);
});

test('workspace manifests parse project paths relative to the workspace manifest', () => {
  const workspace = parseWorkspaceManifest(
    '<?xml version="1.0" encoding="utf-8"?><Workspace SchemaVersion="3" Name="NGIN"><Includes><Include Path="Examples/Common.nginmodel" /></Includes><Defaults BuildType="Debug" Platform="linux-x64" /><PackageSources><PackageSource Path="Packages" /></PackageSources><Projects><Project Path="Examples/App.Basic/App.Basic.nginproj" /></Projects></Workspace>',
    '/repo/NGIN.ngin'
  );

  assert.equal(workspace.name, 'NGIN');
  assert.deepEqual(workspace.modelIncludes, ['/repo/Examples/Common.nginmodel']);
  assert.equal(workspace.defaults?.buildType, 'Debug');
  assert.deepEqual(workspace.projectPaths, ['/repo/Examples/App.Basic/App.Basic.nginproj']);
  assert.deepEqual(workspace.packageSourcePaths, ['/repo/Packages']);
});

test('model and project parsing apply V3 defaults and profile templates', () => {
  const model = parseModelManifest(
    [
      '<?xml version="1.0" encoding="utf-8"?>',
      '<Model SchemaVersion="3" Name="Common">',
      '  <Defaults BuildType="Debug" Platform="linux-x64" Environment="dev" />',
      '  <ProfileTemplates>',
      '    <ProfileTemplate Name="RuntimeDefaults">',
      '      <Launch Executable="$(OutputName)" WorkingDirectory="." />',
      '      <Inputs><Configs>config/template.cfg</Configs></Inputs>',
      '    </ProfileTemplate>',
      '  </ProfileTemplates>',
      '</Model>'
    ].join(''),
    '/repo/Examples/Common.nginmodel'
  );
  const project = parseProjectManifest(
    '<?xml version="1.0" encoding="utf-8"?><Project SchemaVersion="3" Name="App.Basic" DefaultProfile="Runtime"><Output Kind="Executable" Name="App.Basic" Target="App.Basic" /><Environments><Environment Name="dev" /></Environments><Profiles><Profile Name="Runtime" Template="RuntimeDefaults" /></Profiles></Project>',
    '/repo/Examples/App.Basic/App.Basic.nginproj',
    { defaults: model.defaults, profileTemplates: model.profileTemplates }
  );

  assert.equal(project.profiles[0].buildType, 'Debug');
  assert.equal(project.profiles[0].platform, 'linux-x64');
  assert.equal(project.profiles[0].environment, 'dev');
  assert.equal(project.profiles[0].launchExecutable, 'App.Basic');
  assert.deepEqual(project.profiles[0].configInputs, ['config/template.cfg']);
});

test('project manifests parse profiles, launch metadata, and local settings imports', () => {
  const project = parseProjectManifest(
    '<?xml version="1.0" encoding="utf-8"?><Project Name="App.Basic" DefaultProfile="Runtime"><Inputs><Sources Path="src" /><Configs>config/app.cfg</Configs></Inputs><Build><Sources><Source Path="src/main.cpp" /></Sources></Build><References><Project Path="../Game.Engine/Game.Engine.nginproj" /><Package Name="NGIN.Core" /></References><LocalSettings><Import Path=".ngin/local/user.nginsettings" Optional="true" /></LocalSettings><Profiles><Profile Name="Runtime" BuildType="Debug" OperatingSystem="linux" Architecture="x64" Environment="development"><Launch Executable="App.Basic" WorkingDirectory="." /><References><Package Name="NGIN.Reflection" Optional="true" /></References><Inputs><Configs>config/runtime.cfg</Configs></Inputs></Profile></Profiles></Project>',
    '/repo/Examples/App.Basic/App.Basic.nginproj'
  );

  assert.equal(project.defaultProfile, 'Runtime');
  assert.deepEqual(project.sourceRoots, ['src']);
  assert.deepEqual(project.buildSources, ['src/main.cpp']);
  assert.deepEqual(project.configInputs, ['config/app.cfg']);
  assert.deepEqual(project.localSettingsImports, ['/repo/Examples/App.Basic/.ngin/local/user.nginsettings']);
  assert.deepEqual(project.projectRefs, [{ path: '/repo/Examples/Game.Engine/Game.Engine.nginproj', profile: undefined }]);
  assert.deepEqual(project.packageRefs, [{ name: 'NGIN.Core', version: undefined, optional: false }]);
  assert.deepEqual(project.profiles[0].configInputs, ['config/runtime.cfg']);
  assert.deepEqual(project.profiles[0].packageRefs, [{ name: 'NGIN.Reflection', version: undefined, optional: true }]);
  assert.equal(project.profiles[0].launchExecutable, 'App.Basic');
  assert.equal(project.profiles[0].operatingSystem, 'linux');
  assert.equal(project.profiles[0].architecture, 'x64');
});

test('project manifests parse normalized source roots and files', () => {
  const project = parseProjectManifest(
    [
      '<?xml version="1.0" encoding="utf-8"?><Project Name="Typed" DefaultProfile="Runtime"><Inputs><Headers Path="include">include/Typed/App.hpp</Headers><Sources Path="src">src/main.cpp\nsrc/a.cpp\nsrc/b.cpp</Sources></Inputs><Profiles><Profile Name="Runtime" /></Profiles></Project>'
    ].join('\n'),
    '/repo/Typed.nginproj'
  );

  assert.deepEqual(project.sourceRoots, ['include', 'src']);
  assert.deepEqual(project.buildSources, ['include/Typed/App.hpp', 'src/main.cpp', 'src/a.cpp', 'src/b.cpp']);
});

test('project and package manifests parse generator declarations', () => {
  const project = parseProjectManifest(
    [
      '<?xml version="1.0" encoding="utf-8"?>',
      '<Project SchemaVersion="3" Name="Generated.App" DefaultProfile="Runtime">',
      '  <Generators>',
      '    <Generator Name="ReflectionMetaGen" Kind="MetaGen" Package="NGIN.Reflection" Tool="MetaGen">',
      '      <Outputs>',
      '        <Generated Role="Source" Path="$(GeneratedDir)/reflection/Generated.App.reflection.generated.cpp" />',
      '      </Outputs>',
      '    </Generator>',
      '  </Generators>',
      '  <Profiles><Profile Name="Runtime" /></Profiles>',
      '</Project>'
    ].join('\n'),
    '/repo/Generated.App.nginproj'
  );
  assert.equal(project.generators?.[0].name, 'ReflectionMetaGen');
  assert.equal(project.generators?.[0].outputs?.[0].role, 'Source');

  const packageManifest = parsePackageManifest(
    [
      '<?xml version="1.0" encoding="utf-8"?>',
      '<Package SchemaVersion="3" Name="Generated.Tools" Version="0.1.0">',
      '  <Tools>',
      '    <Tool Name="SchemaCompiler" Kind="Generator" Executable="bin/schema-compiler" />',
      '  </Tools>',
      '  <Features>',
      '    <Feature Name="Schema">',
      '      <Generators>',
      '        <Generator Name="SchemaCodegen" Kind="Command" Tool="SchemaCompiler">',
      '          <Arguments><Arg Value="--version" /></Arguments>',
      '          <Outputs><Generated Role="Header" Path="$(GeneratedDir)/schema/app_schema.hpp" /></Outputs>',
      '        </Generator>',
      '      </Generators>',
      '    </Feature>',
      '  </Features>',
      '</Package>'
    ].join('\n'),
    '/repo/Packages/Generated.Tools/Generated.Tools.nginpkg'
  );
  assert.equal(packageManifest.tools?.[0].name, 'SchemaCompiler');
  assert.equal(packageManifest.features?.[0].generators?.[0].name, 'SchemaCodegen');
  assert.equal(packageManifest.features?.[0].generators?.[0].outputs?.[0].role, 'Header');
});

test('local settings manifests expose keys without values', () => {
  const settings = parseLocalSettingsManifest(
    '<?xml version="1.0" encoding="utf-8"?><LocalSettings SchemaVersion="1"><Settings><Setting Key="feeds.private.token" Value="secret" Secret="true" /><Setting Key="sdk.vulkan.root" Value="/opt/vulkan" /></Settings></LocalSettings>',
    '/repo/.ngin/local/user.nginsettings'
  );

  assert.deepEqual(settings.settings, [
    { key: 'feeds.private.token', secret: true },
    { key: 'sdk.vulkan.root', secret: false }
  ]);
});

test('extension manifest and snippets register local settings support', () => {
  const packageJson = JSON.parse(readFileSync(path.join(process.cwd(), 'package.json'), 'utf8'));
  const language = packageJson.contributes.languages.find((entry: { id?: string }) => entry.id === 'ngin');
  assert.ok(language.extensions.includes('.nginsettings'));
  assert.ok(language.extensions.includes('.nginmodel'));

  const commandIds = packageJson.contributes.commands.map((entry: { command: string }) => entry.command);
  assert.ok(commandIds.includes('ngin.variablesExplain'));
  assert.ok(commandIds.includes('ngin.settingsInit'));
  assert.equal(commandIds.includes('ngin.metagen'), false);

  const snippets = JSON.parse(readFileSync(path.join(process.cwd(), 'snippets/ngin.code-snippets'), 'utf8'));
  assert.ok(snippets['Local Settings File']);
  assert.ok(snippets['Variable From Local Setting']);
  assert.ok(snippets['Model']);
  assert.ok(snippets['MetaGen Generator']);
  assert.ok(snippets['Command Generator']);
});

test('launch manifests surface selected executable and staged files', () => {
  const launch = parseLaunchManifest(
    '<?xml version="1.0" encoding="utf-8"?><LaunchManifest Project="App.Basic" Profile="Runtime" Type="Application" BuildType="Debug" OperatingSystem="linux" Architecture="x64"><Launch Executable="App.Basic" WorkingDirectory="." /><Environment Name="development"><Variables /><Features /></Environment><StagedFiles><File Kind="executable" Destination="/repo/out/bin/App.Basic" RelativeDestination="bin/App.Basic" /></StagedFiles></LaunchManifest>',
    '/repo/out/App.Basic.Runtime.nginlaunch'
  );

  assert.equal(launch.project, 'App.Basic');
  assert.equal(launch.profile, 'Runtime');
  assert.equal(launch.selectedExecutable?.name, 'App.Basic');
  assert.equal(launch.stagedFiles[0].kind, 'executable');
});

test('config input authoring inserts root config inputs once', () => {
  const xml = [
    '<?xml version="1.0" encoding="utf-8"?>',
    '<Project Name="App.Basic">',
    '  <Inputs>',
    '    <Sources Path="src" />',
    '  </Inputs>',
    '  <Profiles />',
    '</Project>'
  ].join('\n');

  const added = addRootConfigInput(xml, 'config/new.cfg');
  assert.equal(added.changed, true);
  assert.match(added.xml, /<Configs>[\s\S]*config\/new.cfg[\s\S]*<\/Configs>/);

  const duplicate = addRootConfigInput(added.xml, 'config/new.cfg');
  assert.equal(duplicate.changed, false);
  assert.equal((duplicate.xml.match(/config\/new\.cfg/g) ?? []).length, 1);
});

test('relativeManifestPath uses project-relative slash paths', () => {
  assert.equal(relativeManifestPath('/repo/Examples/App.Basic', '/repo/Examples/App.Basic/config/new.cfg'), 'config/new.cfg');
});

test('config input authoring renames and removes nested config inputs', () => {
  const xml = [
    '<Project Name="App.Basic">',
    '  <Inputs>',
    '    <Configs>',
    '      config/app.cfg',
    '      config/nested/dev.cfg',
    '    </Configs>',
    '  </Inputs>',
    '</Project>'
  ].join('\n');

  const renamed = renameConfigInputs(xml, 'config/nested', 'config/copy', true);
  assert.equal(renamed.changed, true);
  assert.match(renamed.xml, /config\/copy\/dev\.cfg/);

  const removed = removeConfigInputs(renamed.xml, 'config/copy', true);
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
      profile: 'Runtime',
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
      profile: 'Runtime',
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
      project: { path: '/repo/Examples/App.Basic/App.Basic.nginproj', directory: '/repo/Examples/App.Basic', name: 'App.Basic', sourceRoots: [], configInputs: [], buildSources: [], profiles: [] },
      profile: { name: 'Runtime', operatingSystem: 'linux', architecture: 'x64', environment: 'development', configInputs: [] }
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
  assert.equal(sections[1].children[1].label, 'Profile: Runtime');
  assert.equal(sections[1].children[1].command, 'ngin.selectProfile');
  assert.equal(sections[2].label, 'Build Artifacts');
  assert.equal(sections[2].children[0].label, 'Output Folder');
  assert.equal(sections[2].children[0].command, 'ngin.internal.revealPath');
  assert.equal(sections[3].children[0].command, 'ngin.build');
  assert.equal(sections[3].children[1].command, 'ngin.configure');
  assert.equal(sections[3].children[2].command, 'ngin.rebuild');
  assert.equal(sections[3].children[3].command, 'ngin.clean');
  assert.equal(sections[3].children.some((entry) => entry.command === 'ngin.metagen'), false);
  assert.equal(sections[4].children[1].label, 'Open Last Launch Manifest');
});

test('project tree models mark the selected project and profile', () => {
  const models = buildProjectTreeModels({
    workspace: {
      workspace: { path: '/repo/NGIN.ngin', directory: '/repo', name: 'NGIN', projectPaths: [] },
      projects: [
        {
          path: '/repo/Examples/App.Basic/App.Basic.nginproj',
          directory: '/repo/Examples/App.Basic',
          name: 'App.Basic',
          defaultProfile: 'Runtime',
          sourceRoots: ['src'],
          configInputs: ['config/app.cfg'],
          buildSources: [],
          projectRefs: [{ path: '/repo/Examples/Game.Engine/Game.Engine.nginproj' }],
          packageRefs: [{ name: 'NGIN.Core' }],
          profiles: [{ name: 'Runtime', operatingSystem: 'linux', architecture: 'x64', environment: 'development', configInputs: [] }]
        },
        {
          path: '/repo/Examples/Game.Engine/Game.Engine.nginproj',
          directory: '/repo/Examples/Game.Engine',
          name: 'Game.Engine',
          defaultProfile: 'Runtime',
          sourceRoots: [],
          configInputs: [],
          buildSources: [],
          profiles: []
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
        defaultProfile: 'Runtime',
        sourceRoots: ['src'],
        configInputs: ['config/app.cfg'],
        buildSources: [],
        projectRefs: [{ path: '/repo/Examples/Game.Engine/Game.Engine.nginproj' }],
        packageRefs: [{ name: 'NGIN.Core' }],
        profiles: [{ name: 'Runtime', operatingSystem: 'linux', architecture: 'x64', environment: 'development', configInputs: [] }]
      },
      profile: { name: 'Runtime', operatingSystem: 'linux', architecture: 'x64', environment: 'development', configInputs: [] }
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
    'profiles'
  ]);
  assert.deepEqual(models.dependenciesByProject.get('/repo/Examples/App.Basic/App.Basic.nginproj')?.projects.map((entry) => entry.label), ['Game.Engine']);
  assert.deepEqual(models.dependenciesByProject.get('/repo/Examples/App.Basic/App.Basic.nginproj')?.packages.map((entry) => entry.label), ['NGIN.Core']);
  assert.equal(models.profilesByProject.get('/repo/Examples/App.Basic/App.Basic.nginproj')?.[0].selected, true);
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
          defaultProfile: 'Runtime',
          sourceRoots: [],
          configInputs: [],
          buildSources: [],
          projectRefs: [{ path: '/repo/Game.Engine/Game.Engine.nginproj' }],
          packageRefs: [{ name: 'NGIN.Core' }],
          profiles: [
            {
              name: 'Runtime',
              environment: 'development',
              configInputs: [],
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
          configInputs: [],
          buildSources: [],
          profiles: []
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
      project: { path: '/repo/Examples/App.Basic/App.Basic.nginproj', directory: '/repo/Examples/App.Basic', name: 'App.Basic', sourceRoots: [], configInputs: [], buildSources: [], profiles: [] },
      profile: { name: 'Runtime', operatingSystem: 'linux', architecture: 'x64', environment: 'development', configInputs: [] }
    },
    outputDir: '/repo/.ngin/build/App.Basic/Runtime',
    launchManifestExists: false,
    stagedCompileCommandsAvailable: false
  });

  assert.equal(model.visible, true);
  assert.match(model.workspace?.text ?? '', /\$\(folder-library\)/);
  assert.equal(model.project?.command, 'ngin.internal.pickProject');
  assert.equal(model.profile?.command, 'ngin.internal.pickProfile');
  assert.match(model.profile?.text ?? '', /\$\(symbol-enum\) Runtime/);
  assert.equal(model.configure?.command, 'ngin.configure');
  assert.equal(model.build?.command, 'ngin.build');
  assert.equal(model.run?.command, 'ngin.run');
  assert.equal(model.debug?.command, 'ngin.debug');
});
