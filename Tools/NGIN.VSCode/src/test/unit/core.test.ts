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
  parseCompositionGraphPayload,
  parseCliDiagnostics
} from '../../core/helpers';
import { diagnosticFromEvent, eventLabel, NginJsonlEventParser, NginJsonlParseError } from '../../core/events';
import { addRootConfigInput, relativeManifestPath, removeConfigInputs, renameConfigInputs } from '../../core/projectAuthoring';
import { buildProjectTreeModels, buildStatusBarModel } from '../../ui/models';
import { parseLaunchManifest, parseLocalSettingsManifest, parsePackageManifest, parseProjectManifest, parseWorkspaceManifest } from '../../core/xml';
import {
  addProfile,
  deleteProfile,
  setEnvironmentVariables,
  setInputEntries,
  setDependencyUses,
  setProfileFeatureState,
  updateProfile,
  updateProjectAttributes
} from '../../projectEditor/authoring';
import { buildProjectEditorModel } from '../../projectEditor/model';

test('computeOutputDir uses the CLI default layout when no root override is configured', () => {
  const outputDir = computeOutputDir('/workspace', 'Hello.Hosted', 'Runtime');
  assert.equal(outputDir, '/workspace/.ngin/build/Hello.Hosted/Runtime');
});

test('computeOutputDir appends project and profile beneath a configured output root', () => {
  const outputDir = computeOutputDir('/workspace', 'Hello.Hosted', 'Runtime', 'build/out');
  assert.equal(outputDir, '/workspace/build/out/Hello.Hosted/Runtime');
});

test('computeLaunchManifestPath uses the staged naming convention', () => {
  const manifestPath = computeLaunchManifestPath('/workspace/.ngin/build/Hello.Hosted/Runtime', 'Hello.Hosted', 'Runtime');
  assert.equal(manifestPath, '/workspace/.ngin/build/Hello.Hosted/Runtime/Hello.Hosted.Runtime.nginlaunch');
});

test('computeCompileCommandsPath uses the staged cmake-build location', () => {
  const compileCommandsPath = computeCompileCommandsPath('/workspace/.ngin/build/Hello.Hosted/Runtime');
  assert.equal(compileCommandsPath, '/workspace/.ngin/build/Hello.Hosted/Runtime/.ngin/cmake-build/compile_commands.json');
});

test('getFallbackCompileCommandsPath uses the dev compile database', () => {
  const fallbackPath = getFallbackCompileCommandsPath('/workspace');
  assert.equal(fallbackPath, '/workspace/build/dev/compile_commands.json');
});

test('parseCliDiagnostics extracts structured file and generic errors', () => {
  const diagnostics = parseCliDiagnostics([
    'Validation errors:',
    '  - /tmp/Hello.Hosted.nginproj: failed to parse XML: unexpected token',
    '  - /tmp/user.nginsettings: duplicate local setting key',
    'error: unknown profile `Editor`',
    '[warning] /workspace/src/main.cpp:4:7: prefer auto [clang-tidy:modernize-use-auto]'
  ].join('\n'));

  assert.equal(diagnostics.length, 4);
  assert.equal(diagnostics[0].file, '/tmp/Hello.Hosted.nginproj');
  assert.match(diagnostics[0].message, /failed to parse XML/);
  assert.equal(diagnostics[1].file, '/tmp/user.nginsettings');
  assert.match(diagnostics[1].message, /duplicate local setting key/);
  assert.equal(diagnostics[2].message, 'unknown profile `Editor`');
  assert.equal(diagnostics[3].file, '/workspace/src/main.cpp');
  assert.equal(diagnostics[3].line, 4);
  assert.equal(diagnostics[3].column, 7);
  assert.equal(diagnostics[3].severity, 'warning');
  assert.equal(diagnostics[3].source, undefined);
  assert.equal(diagnostics[3].message, 'prefer auto [clang-tidy:modernize-use-auto]');
});

test('NginJsonlEventParser handles split chunks and event helpers', () => {
  const parser = new NginJsonlEventParser();
  const line = JSON.stringify({
    schemaVersion: '1.0',
    kind: 'NGIN.CLI.Event',
    sequence: 1,
    timestamp: '2026-05-12T00:00:00.000Z',
    type: 'phase.started',
    command: 'build',
    data: { phase: 'build', label: 'CMake build' }
  });

  assert.deepEqual(parser.push(line.slice(0, 20)), []);
  const events = parser.push(line.slice(20) + '\n');
  assert.equal(events.length, 1);
  assert.equal(events[0].type, 'phase.started');
  assert.equal(eventLabel(events[0]), 'CMake build');
});

test('diagnostic events map to extension diagnostic payloads', () => {
  const parser = new NginJsonlEventParser();
  const [event] = parser.push(JSON.stringify({
    schemaVersion: '1.0',
    kind: 'NGIN.CLI.Event',
    sequence: 2,
    timestamp: '2026-05-12T00:00:01.000Z',
    type: 'diagnostic',
    command: 'analyze',
    data: {
      severity: 'warning',
      source: 'clang-tidy',
      code: 'modernize-use-auto',
      message: 'prefer auto',
      file: '/workspace/src/main.cpp',
      line: 4,
      column: 7,
      tags: ['deprecated'],
      editSetIds: ['fix-1'],
      relatedLocations: ['/workspace/include/main.hpp:2:3:declared here']
    }
  }) + '\n');

  const diagnostic = diagnosticFromEvent(event);
  assert.ok(diagnostic);
  assert.equal(diagnostic.severity, 'warning');
  assert.equal(diagnostic.source, 'clang-tidy');
  assert.equal(diagnostic.code, 'modernize-use-auto');
  assert.equal(diagnostic.file, '/workspace/src/main.cpp');
  assert.equal(diagnostic.line, 4);
  assert.equal(diagnostic.column, 7);
  assert.deepEqual(diagnostic.tags, ['deprecated']);
  assert.deepEqual(diagnostic.editSetIds, ['fix-1']);
  assert.deepEqual(diagnostic.relatedLocations, [{
    file: '/workspace/include/main.hpp', line: 2, column: 3, message: 'declared here'
  }]);
});

test('NginJsonlEventParser ignores non-event JSON and rejects malformed lines', () => {
  const parser = new NginJsonlEventParser();
  assert.deepEqual(parser.push('{"kind":"Other"}\n'), []);
  assert.throws(() => parser.push('{not json}\n'), NginJsonlParseError);
});

test('parseCompositionGraphPayload maps V4 composition graph inspect output to extension model', () => {
  const payload = parseCompositionGraphPayload({
    schemaVersion: '4.0',
    kind: 'NGIN.CompositionGraph',
    identity: {
      project: 'Hello.Analyzer',
      projectPath: '/repo/Examples/Hello.Analyzer/Hello.Analyzer.nginproj'
    },
    product: {
      kind: 'Application'
    },
    selection: {
      profile: 'Debug.Analyzer',
      buildType: 'Debug',
      targetPlatform: 'linux-x64',
      operatingSystem: 'linux',
      architecture: 'x64',
      environment: 'local'
    },
    plans: {
      packages: [
        {
          name: 'NGIN.Tooling.ClangTidy',
          version: '0.1.0',
          providerRoot: '/repo/Packages/NGIN.Tooling.ClangTidy',
          closures: ['Dev'],
          provenance: {
            sourceKind: 'project',
            sourceName: 'Hello.Analyzer',
            manifestPath: '/repo/Examples/Hello.Analyzer/Hello.Analyzer.nginproj',
            reason: 'resolved package dependency'
          }
        }
      ],
      packageFeatures: [
        {
          package: 'NGIN.Tooling.ClangTidy',
          packageVersion: '0.1.0',
          feature: 'Analyzer',
          provenance: {
            sourceKind: 'package-feature',
            sourceName: 'NGIN.Tooling.ClangTidy::Analyzer',
            manifestPath: '/repo/Packages/NGIN.Tooling.ClangTidy/NGIN.Tooling.ClangTidy.nginpkg',
            reason: 'selected package feature'
          }
        }
      ],
      build: {
        defines: [
          {
            value: 'HELLO_ANALYZER=1',
            provenance: {
              sourceKind: 'project-profile',
              sourceName: 'Debug.Analyzer',
              manifestPath: '/repo/Examples/Hello.Analyzer/Hello.Analyzer.nginproj',
              reason: 'selected compile definition'
            }
          }
        ],
        inputs: [
          {
            role: 'Source',
            source: 'src/main.cpp',
            owner: 'project:Hello.Analyzer',
            provenance: {
              sourceKind: 'project',
              sourceName: 'Hello.Analyzer',
              manifestPath: '/repo/Examples/Hello.Analyzer/Hello.Analyzer.nginproj',
              reason: 'selected build input'
            }
          }
        ]
      },
      generators: [],
      stage: {
        files: []
      },
      runtime: {
        requiredModules: [
          {
            name: 'Hello.Analyzer.Runtime',
            provenance: {
              sourceKind: 'package-feature',
              sourceName: 'NGIN.Tooling.ClangTidy::Analyzer',
              manifestPath: '/repo/Packages/NGIN.Tooling.ClangTidy/NGIN.Tooling.ClangTidy.nginpkg',
              reason: 'resolved required runtime module'
            }
          }
        ]
      },
      environment: {
        variables: [
          {
            name: 'HELLO_ANALYZER_TOKEN',
            value: '<redacted>',
            secret: true,
            provenance: {
              sourceKind: 'project',
              sourceName: 'Hello.Analyzer',
              manifestPath: '/repo/Examples/Hello.Analyzer/Hello.Analyzer.nginproj',
              reason: 'secret environment contribution'
            }
          }
        ]
      },
      launch: {
        executable: 'Hello.Analyzer',
        workingDirectory: '.',
        provenance: {
          sourceKind: 'project-profile',
          sourceName: 'Debug.Analyzer',
          manifestPath: '/repo/Examples/Hello.Analyzer/Hello.Analyzer.nginproj',
          reason: 'selected launch'
        }
      },
      tooling: {
        runs: [
          {
            name: 'cpp-static-analysis',
            action: 'NGIN.Tooling.ClangTidy::analyze',
            kind: 'Analyze',
            tool: 'clang-tidy',
            driver: 'clang-tidy-driver',
            package: 'NGIN.Tooling.ClangTidy',
            provenance: {
              sourceKind: 'package-feature',
              sourceName: 'NGIN.Tooling.ClangTidy::Analyzer',
              manifestPath: '/repo/Packages/NGIN.Tooling.ClangTidy/NGIN.Tooling.ClangTidy.nginpkg',
              reason: 'selected analyzer'
            }
          }
        ]
      },
      diagnostics: []
    }
  });

  assert.equal(payload.schemaVersion, '4.0');
  assert.equal(payload.kind, 'NGIN.CompositionGraph');
  assert.equal(payload.identity?.project, 'Hello.Analyzer');
  assert.equal(payload.product?.kind, 'Application');
  assert.equal(payload.selection?.profile, 'Debug.Analyzer');
  assert.equal(payload.selection?.targetPlatform, 'linux-x64');
  assert.equal(payload.plans?.packages?.[0]?.name, 'NGIN.Tooling.ClangTidy');
  assert.equal(payload.plans?.packages?.[0]?.provenance?.sourceKind, 'project');
  assert.equal(payload.plans?.packageFeatures?.[0]?.feature, 'Analyzer');
  assert.equal(payload.plans?.packageFeatures?.[0]?.provenance?.sourceName, 'NGIN.Tooling.ClangTidy::Analyzer');
  assert.equal(payload.plans?.build?.inputs?.[0]?.source, 'src/main.cpp');
  assert.equal(payload.plans?.build?.inputs?.[0]?.provenance?.reason, 'selected build input');
  assert.equal(payload.plans?.launch?.executable, 'Hello.Analyzer');
  assert.equal(payload.plans?.launch?.provenance?.sourceKind, 'project-profile');
  assert.equal(payload.plans?.environment?.variables?.[0]?.value, '<redacted>');
  assert.equal(payload.plans?.environment?.variables?.[0]?.provenance?.reason, 'secret environment contribution');
  assert.equal(payload.plans?.tooling?.runs?.[0]?.name, 'cpp-static-analysis');
  assert.equal(payload.plans?.tooling?.runs?.[0]?.provenance?.sourceKind, 'package-feature');
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
    '<?xml version="1.0" encoding="utf-8"?><Workspace SchemaVersion="4" Name="NGIN"><Imports><Import Path="build/platforms.ngin.xml" /></Imports><Packages><Source Name="local" Path="Packages" /></Packages><Projects><Project Path="Examples/Hello.Hosted/Hello.Hosted.nginproj" /></Projects></Workspace>',
    '/repo/NGIN.ngin'
  );

  assert.equal(workspace.name, 'NGIN');
  assert.deepEqual(workspace.imports, ['/repo/build/platforms.ngin.xml']);
  assert.deepEqual(workspace.projectPaths, ['/repo/Examples/Hello.Hosted/Hello.Hosted.nginproj']);
  assert.deepEqual(workspace.packageSourcePaths, ['/repo/Packages']);
});

test('project parsing applies product profile defaults', () => {
  const project = parseProjectManifest(
    '<?xml version="1.0" encoding="utf-8"?><Project SchemaVersion="4" Name="Hello.Hosted" DefaultProfile="dev"><Application><Launch Executable="$(OutputName)" WorkingDirectory="." /></Application><Profile Name="dev"><Defaults><BuildType Name="Debug" /><TargetPlatform Name="linux-x64" /><Environment Name="development" /></Defaults><Application><Stage><Config Source="config/template.cfg" /></Stage></Application></Profile></Project>',
    '/repo/Examples/Hello.Hosted/Hello.Hosted.nginproj'
  );

  assert.equal(project.profiles[0].buildType, 'Debug');
  assert.equal(project.profiles[0].platform, 'linux-x64');
  assert.equal(project.profiles[0].environment, 'development');
  assert.equal(project.profiles[0].launchExecutable, '$(OutputName)');
  assert.deepEqual(project.profiles[0].configInputs, ['config/template.cfg']);
});

test('project manifests parse profiles, launch metadata, and local settings imports', () => {
  const project = parseProjectManifest(
    '<?xml version="1.0" encoding="utf-8"?><Project SchemaVersion="4" Name="Hello.Hosted" DefaultProfile="Runtime"><Application><Uses><Project Name="Engine.Library" Path="../Engine.Library/Engine.Library.nginproj" /><Runtime Name="NGIN.Core" Scope="Target;Runtime" /></Uses><Build><Sources Path="src/**.cpp" /><Source Path="src/main.cpp" /></Build><Stage><Config Source="config/app.cfg" /></Stage><Launch Executable="Hello.Hosted" WorkingDirectory="." /></Application><Profile Name="Runtime"><Defaults><BuildType Name="Debug" /><OperatingSystem Name="linux" /><Architecture Name="x64" /><Environment Name="development" /></Defaults><Application><Uses><Package Name="NGIN.Reflection" Optional="true" /></Uses><Stage><Config Source="config/runtime.cfg" /></Stage></Application></Profile></Project>',
    '/repo/Examples/Hello.Hosted/Hello.Hosted.nginproj'
  );

  assert.equal(project.defaultProfile, 'Runtime');
  assert.deepEqual(project.sourceRoots, ['src/**.cpp', 'src/main.cpp']);
  assert.deepEqual(project.buildSources, ['src/main.cpp']);
  assert.deepEqual(project.configInputs, ['config/app.cfg']);
  assert.deepEqual(project.projectRefs, [{ name: 'Engine.Library', path: '/repo/Examples/Engine.Library/Engine.Library.nginproj', profile: undefined }]);
  assert.deepEqual(project.dependencies, [{ name: 'NGIN.Core', version: undefined, scope: 'Target;Runtime', kind: 'Runtime', optional: false, features: [] }]);
  assert.deepEqual(project.profiles[0].configInputs, ['config/runtime.cfg']);
  assert.deepEqual(project.profiles[0].dependencies, [{ name: 'NGIN.Reflection', version: undefined, scope: undefined, kind: 'Package', optional: true, features: [] }]);
  assert.equal(project.profiles[0].launchExecutable, 'Hello.Hosted');
  assert.equal(project.profiles[0].operatingSystem, 'linux');
  assert.equal(project.profiles[0].architecture, 'x64');
});

test('project manifests parse normalized source roots and files', () => {
  const project = parseProjectManifest(
    [
      '<?xml version="1.0" encoding="utf-8"?><Project SchemaVersion="4" Name="Typed" DefaultProfile="Runtime"><Application><Build><Headers Path="include/**.hpp" /><Source Path="src/main.cpp" /><Source Path="src/a.cpp" /><Source Path="src/b.cpp" /></Build></Application><Profile Name="Runtime" /></Project>'
    ].join('\n'),
    '/repo/Typed.nginproj'
  );

  assert.deepEqual(project.sourceRoots, ['include/**.hpp', 'src/main.cpp', 'src/a.cpp', 'src/b.cpp']);
  assert.deepEqual(project.buildSources, ['src/main.cpp', 'src/a.cpp', 'src/b.cpp']);
});

test('project and package manifests parse generator declarations', () => {
  const project = parseProjectManifest(
    [
      '<?xml version="1.0" encoding="utf-8"?>',
      '<Project SchemaVersion="4" Name="Generated.App" DefaultProfile="Runtime">',
      '  <Application>',
      '  <Generate>',
      '    <Generator Name="ReflectionMetaGen" Phase="Generate">',
      '      <Tool Executable="tools/ngin-metagen" />',
      '      <Args>',
      '        <Arg Value="--context" />',
      '        <Arg Path="$(GeneratorContext)" />',
      '      </Args>',
      '      <Outputs>',
      '        <Sources Path="$(GeneratedDir)/reflection/Generated.App.reflection.generated.cpp" />',
      '      </Outputs>',
      '    </Generator>',
      '  </Generate>',
      '  </Application>',
      '  <Profile Name="Runtime" />',
      '</Project>'
    ].join('\n'),
    '/repo/Generated.App.nginproj'
  );
  assert.equal(project.generators?.[0].name, 'ReflectionMetaGen');
  assert.equal(project.generators?.[0].outputs?.[0].role, 'Source');

  const packageManifest = parsePackageManifest(
    [
      '<?xml version="1.0" encoding="utf-8"?>',
      '<Package SchemaVersion="4" Name="Generated.Tools" Version="0.1.0">',
      '  <Tools>',
      '    <Tool Name="SchemaCompiler" Kind="Generator" Executable="bin/schema-compiler" />',
      '  </Tools>',
      '  <Features>',
      '    <Feature Name="Schema">',
      '      <Generate>',
      '        <Generator Name="SchemaCodegen" Kind="Command" Tool="SchemaCompiler">',
      '          <Args><Arg Value="--version" /></Args>',
      '          <Outputs><Headers Path="$(GeneratedDir)/schema/app_schema.hpp" /></Outputs>',
      '        </Generator>',
      '      </Generate>',
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
  assert.equal(language.extensions.includes('.nginmodel'), false);

  const commandIds = packageJson.contributes.commands.map((entry: { command: string }) => entry.command);
  assert.ok(commandIds.includes('ngin.variablesExplain'));
  assert.ok(commandIds.includes('ngin.settingsInit'));
  assert.ok(commandIds.includes('ngin.openProjectXmlSource'));
  assert.ok(commandIds.includes('ngin.analyze'));
  assert.ok(commandIds.includes('ngin.addToolAction'));
  assert.ok(commandIds.includes('ngin.runToolRun'));
  assert.ok(commandIds.includes('ngin.applyToolEdits'));
  assert.ok(commandIds.includes('ngin.toolingPlan'));
  assert.ok(commandIds.includes('ngin.explainSelection'));
  assert.ok(commandIds.includes('ngin.showResolvedInputs'));
  assert.ok(commandIds.includes('ngin.showInactiveTooling'));
  assert.equal(commandIds.includes('ngin.metagen'), false);

  const titleActions = packageJson.contributes.menus['view/title']
    .filter((entry: { group?: string }) => entry.group?.startsWith('navigation'))
    .map((entry: { command: string }) => entry.command);
  assert.deepEqual(titleActions, ['ngin.build', 'ngin.run', 'ngin.selectProfile', 'ngin.refresh']);

  const activityViews = packageJson.contributes.views.ngin.map((entry: { id: string; name: string }) => `${entry.id}:${entry.name}`);
  assert.deepEqual(activityViews, ['nginWorkspace:Workspace']);
  assert.equal(packageJson.contributes.customEditors[0].viewType, 'ngin.projectEditor');
  assert.equal(packageJson.contributes.customEditors[0].priority, 'default');
  const settings = packageJson.contributes.configuration.properties;
  assert.equal(settings['ngin.validate.onSave'], undefined);
  assert.equal(settings['ngin.analyze.onSave'], undefined);
  assert.equal(settings['ngin.tooling.validateManifestOnSave'].default, false);
  assert.equal(settings['ngin.tooling.runActiveFileOnSave'].default, false);

  const snippets = JSON.parse(readFileSync(path.join(process.cwd(), 'snippets/ngin.code-snippets'), 'utf8'));
  assert.ok(snippets['Local Settings File']);
  assert.ok(snippets['Application Project']);
  assert.ok(snippets['Runtime Dependency']);
  assert.ok(snippets['Tooling Run']);
  assert.equal(snippets['Model'], undefined);
  assert.ok(snippets['Command Generator']);
  assert.ok(snippets['Tool Driver and Action']);
  assert.ok(snippets['Publish']);
  assert.ok(snippets['Package Output']);
});

test('launch manifests surface selected executable and staged files', () => {
  const launch = parseLaunchManifest(
    '<?xml version="1.0" encoding="utf-8"?><LaunchManifest Project="Hello.Hosted" Profile="Runtime" Type="Application" BuildType="Debug" OperatingSystem="linux" Architecture="x64"><Launch Executable="Hello.Hosted" WorkingDirectory="." /><Environment Name="development"><Variables /><Features /></Environment><StagedFiles><File Kind="executable" Destination="/repo/out/bin/Hello.Hosted" RelativeDestination="bin/Hello.Hosted" /></StagedFiles></LaunchManifest>',
    '/repo/out/Hello.Hosted.Runtime.nginlaunch'
  );

  assert.equal(launch.project, 'Hello.Hosted');
  assert.equal(launch.profile, 'Runtime');
  assert.equal(launch.selectedExecutable?.name, 'Hello.Hosted');
  assert.equal(launch.stagedFiles[0].kind, 'executable');
});

test('config input authoring inserts root config inputs once', () => {
  const xml = [
    '<?xml version="1.0" encoding="utf-8"?>',
    '<Project SchemaVersion="4" Name="Hello.Hosted">',
    '  <Application>',
    '    <Build>',
    '      <Sources Path="src/**.cpp" />',
    '    </Build>',
    '  </Application>',
    '</Project>'
  ].join('\n');

  const added = addRootConfigInput(xml, 'config/new.cfg');
  assert.equal(added.changed, true);
  assert.match(added.xml, /<Stage>[\s\S]*<Config Source="config\/new.cfg" \/>[\s\S]*<\/Stage>/);

  const duplicate = addRootConfigInput(added.xml, 'config/new.cfg');
  assert.equal(duplicate.changed, false);
  assert.equal((duplicate.xml.match(/config\/new\.cfg/g) ?? []).length, 1);
});

test('relativeManifestPath uses project-relative slash paths', () => {
  assert.equal(relativeManifestPath('/repo/Examples/Hello.Hosted', '/repo/Examples/Hello.Hosted/config/new.cfg'), 'config/new.cfg');
});

test('config input authoring renames and removes nested config inputs', () => {
  const xml = [
    '<Project SchemaVersion="4" Name="Hello.Hosted">',
    '  <Application>',
    '    <Stage>',
    '      <Config Source="config/app.cfg" />',
    '      <Config Source="config/nested/dev.cfg" />',
    '    </Stage>',
    '  </Application>',
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

test('project editor authoring updates root attributes while preserving unknown XML', () => {
  const xml = [
    '<?xml version="1.0" encoding="utf-8"?>',
    '<Project SchemaVersion="4" Name="Old" DefaultProfile="Runtime">',
    '  <!-- keep this comment -->',
    '  <Application />',
    '  <Profile Name="Runtime" />',
    '</Project>'
  ].join('\n');

  const updated = updateProjectAttributes(xml, { name: 'New', defaultProfile: 'Runtime' });
  assert.match(updated, /Name="New"/);
  assert.doesNotMatch(updated, /Template=/);
  assert.match(updated, /<!-- keep this comment -->/);
  assert.match(updated, /<Application \/>/);
});

test('project editor authoring adds updates and deletes profiles', () => {
  let xml = '<Project SchemaVersion="4" Name="App" DefaultProfile="Runtime"><Application /><Profile Name="Runtime" /></Project>';
  xml = addProfile(xml, 'Tools');
  assert.match(xml, /<Profile Name="Tools" \/>/);

  xml = updateProfile(xml, {
    originalName: 'Tools',
    name: 'Diagnostics',
    buildType: 'Debug',
    operatingSystem: 'linux',
    architecture: 'x64',
    environment: 'dev',
    launchExecutable: 'App',
    launchWorkingDirectory: '.'
  });
  assert.match(xml, /<Profile Name="Diagnostics">/);
  assert.match(xml, /<BuildType Name="Debug" \/>/);
  assert.match(xml, /<OperatingSystem Name="linux" \/>/);
  assert.match(xml, /<Architecture Name="x64" \/>/);
  assert.match(xml, /<Environment Name="dev" \/>/);
  assert.match(xml, /<Launch Executable="App" WorkingDirectory="." \/>/);

  xml = deleteProfile(xml, 'Runtime');
  assert.doesNotMatch(xml, /Name="Runtime"/);
  assert.match(xml, /DefaultProfile="Diagnostics"/);
});

test('project editor authoring manages active profile feature state', () => {
  let xml = '<Project SchemaVersion="4" Name="App"><Application /><Profile Name="Runtime" /></Project>';
  xml = setProfileFeatureState(xml, 'Runtime', 'NGIN.Core', 'Reflection', 'use');
  assert.match(xml, /<Package Name="NGIN\.Core">[\s\S]*<Feature Name="Reflection" \/>[\s\S]*<\/Package>/);

  xml = setProfileFeatureState(xml, 'Runtime', 'NGIN.Core', 'Reflection', 'disable');
  assert.doesNotMatch(xml, /<Feature Name="Reflection" \/>/);
  assert.match(xml, /<Feature Name="Reflection" Enabled="false" \/>/);

  xml = setProfileFeatureState(xml, 'Runtime', 'NGIN.Core', 'Reflection', 'inherit');
  assert.doesNotMatch(xml, /Feature="Reflection"/);
});

test('project editor authoring creates missing profiles for feature overrides', () => {
  let xml = '<Project SchemaVersion="4" Name="App" DefaultProfile="Runtime"><Application /></Project>';
  xml = setProfileFeatureState(xml, 'Runtime', 'NGIN.Core', 'Reflection', 'disable');
  assert.match(xml, /<Profile Name="Runtime">/);
  assert.match(xml, /<Feature Name="Reflection" Enabled="false" \/>/);

  const unchanged = '<Project SchemaVersion="4" Name="App" DefaultProfile="Runtime"><Application /></Project>';
  assert.equal(setProfileFeatureState(unchanged, 'Runtime', 'NGIN.Core', 'Reflection', 'inherit'), unchanged);
});

test('project editor authoring manages dependency uses inputs and environment variables', () => {
  let xml = '<Project SchemaVersion="4" Name="App"><Application /><Profile Name="Runtime" /></Project>';
  xml = setDependencyUses(xml, [{ name: 'NGIN.Core', version: '>=0.1.0 <0.2.0', optional: false }]);
  assert.match(xml, /<Package Name="NGIN\.Core" Version="&gt;=0\.1\.0 &lt;0\.2\.0" Optional="false" \/>/);

  xml = setInputEntries(xml, 'Sources', [{ mode: 'Directory', path: 'src' }]);
  xml = setInputEntries(xml, 'Headers', [{ mode: 'File', path: 'include/App.hpp' }]);
  assert.match(xml, /<Sources Path="src" \/>/);
  assert.match(xml, /include\/App\.hpp/);

  xml = setEnvironmentVariables(xml, 'dev', [
    { name: 'TOKEN', fromLocalSetting: 'app.token', required: false, secret: true },
    { name: 'SDK_ROOT', fromEnvironment: 'SDK_ROOT' }
  ]);
  assert.match(xml, /<Environment>/);
  assert.match(xml, /<Secret Name="TOKEN" From="local:app\.token" Required="false" \/>/);
  assert.match(xml, /<Env Name="SDK_ROOT" FromEnvironment="SDK_ROOT" \/>/);
});

test('project editor authoring preserves selectors on file rules', () => {
  const xml = setInputEntries('<Project SchemaVersion="4" Name="App"><Application /></Project>', 'Sources', [
    {
      mode: 'Directory',
      path: 'src',
      include: '**/*.cpp;**/*.hpp',
      exclude: '**/*.generated.cpp',
      operatingSystem: 'linux',
      architecture: 'x64',
      buildType: 'Debug',
      condition: 'Desktop'
    }
  ]);

  assert.match(xml, /<Sources Path="src" Include="\*\*\/\*\.cpp;\*\*\/\*\.hpp" Exclude="\*\*\/\*\.generated\.cpp" When="Desktop" OperatingSystem="linux" Architecture="x64" BuildType="Debug" \/>/);

  const model = buildProjectEditorModel(xml, '/repo/App.nginproj', 'file:///repo/App.nginproj');
  assert.deepEqual(model.project.inputs.Sources[0], {
    mode: 'Directory',
    path: 'src',
    include: '**/*.cpp;**/*.hpp',
    exclude: '**/*.generated.cpp',
    profile: undefined,
    platform: undefined,
    operatingSystem: 'linux',
    architecture: 'x64',
    buildType: 'Debug',
    environment: undefined,
    condition: 'Desktop'
  });
});

test('project editor authoring keeps root and profile dependency uses separate', () => {
  let xml = [
    '<Project SchemaVersion="4" Name="App">',
    '  <Application />',
    '  <Profile Name="Runtime">',
    '    <Application>',
    '      <Uses>',
    '        <Package Name="Profile.Only" />',
    '      </Uses>',
    '    </Application>',
    '  </Profile>',
    '</Project>'
  ].join('\n');

  xml = setDependencyUses(xml, [{ name: 'Root.Only' }]);
  assert.match(xml, /<Package Name="Root\.Only" \/>/);
  assert.match(xml, /<Package Name="Profile\.Only" \/>/);
});

test('project editor model surfaces parse errors and resolved feature states', () => {
  const invalid = buildProjectEditorModel('<Project>', '/repo/App.nginproj', 'file:///repo/App.nginproj');
  assert.ok(invalid.parseError);

  const model = buildProjectEditorModel(
    [
      '<Project SchemaVersion="4" Name="App" DefaultProfile="Runtime">',
      '  <Application />',
      '  <Profile Name="Runtime"><Application><Uses><Package Name="NGIN.Core"><Feature Name="Reflection" /></Package></Uses></Application></Profile>',
      '</Project>'
    ].join('\n'),
    '/repo/App.nginproj',
    'file:///repo/App.nginproj',
    {
      schemaVersion: '4.0',
      kind: 'NGIN.CompositionGraph',
      plans: {
        packageFeatures: [
          { package: 'NGIN.Core', feature: 'Reflection' },
          { package: 'NGIN.Core', feature: 'Missing' }
        ]
      }
    },
    'Runtime'
  );

  assert.equal(model.features.find((feature) => feature.featureName === 'Reflection')?.state, 'use');
  assert.equal(model.features.find((feature) => feature.featureName === 'Missing')?.resolvedState, 'selected');
});

test('project editor model summarizes resolved inspect data for the project overview', () => {
  const model = buildProjectEditorModel(
    '<Project SchemaVersion="4" Name="App" DefaultProfile="Runtime"><Application /><Profile Name="Runtime" /></Project>',
    '/repo/App.nginproj',
    'file:///repo/App.nginproj',
    {
      schemaVersion: '4.0',
      kind: 'NGIN.CompositionGraph',
      identity: { project: 'App' },
      product: { kind: 'Application' },
      workspace: { name: 'Workspace' },
      selection: { profile: 'Runtime', buildType: 'Debug', targetPlatform: 'linux-x64', environment: 'dev' },
      outputDir: '/repo/.ngin/build/App/Runtime',
      plans: {
        packages: [{ name: 'NGIN.Core', version: '0.1.0', closures: ['project'] }],
        packageFeatures: [
          { package: 'NGIN.Core', feature: 'Reflection' },
          { package: 'NGIN.Core', feature: 'Diagnostics' }
        ],
        generators: [
          { name: 'ReflectionMetaGen', state: 'active' },
          { name: 'WindowsOnly', state: 'excluded' }
        ],
        build: {
          inputs: [{ source: 'src', mode: 'Directory', ownerName: 'App', role: 'Source' }]
        },
        launch: { executable: 'App', workingDirectory: '.' },
        stage: { files: [{ kind: 'executable', target: 'bin/App' }] },
        environment: { variables: [{ name: 'TOKEN', secret: true, resolved: true, source: 'local' }] },
        diagnostics: [{ severity: 'warning', message: 'example' }]
      }
    },
    'Runtime'
  );

  assert.equal(model.resolved.workspaceName, 'Workspace');
  assert.equal(model.resolved.outputDir, '/repo/.ngin/build/App/Runtime');
  assert.equal(model.resolved.packageCount, 1);
  assert.equal(model.resolved.activeFeatureCount, 2);
  assert.equal(model.resolved.activeGeneratorCount, 1);
  assert.equal(model.resolved.stagedFileCount, 1);
  assert.equal(model.resolved.environmentVariableCount, 1);
  assert.equal(model.resolved.diagnosticWarningCount, 1);
  assert.deepEqual(model.resolved.inputs.map((entry) => `${entry.kind}:${entry.source}:${entry.mode}`), ['Source:src:Directory']);
});

test('executable resolution prefers staged manifest entries before bin fallback', () => {
  const candidates = getExecutableCandidatePaths(
    {
      path: '/repo/out/Hello.Hosted.Runtime.nginlaunch',
      directory: '/repo/out',
      project: 'Hello.Hosted',
      profile: 'Runtime',
      launch: { workingDirectory: '.' },
      selectedExecutable: { name: 'Hello.Hosted' },
      stagedFiles: [
        {
          kind: 'executable',
          destination: '/repo/out/bin/Hello.Hosted',
          relativeDestination: 'bin/Hello.Hosted'
        }
      ]
    },
    '/repo/out',
    'linux'
  );

  assert.deepEqual(candidates, ['/repo/out/bin/Hello.Hosted']);
});

test('working directory resolution checks staged and project-relative candidates', () => {
  const candidates = getWorkingDirectoryCandidates(
    {
      path: '/repo/out/Hello.Hosted.Runtime.nginlaunch',
      directory: '/repo/out',
      project: 'Hello.Hosted',
      profile: 'Runtime',
      launch: { workingDirectory: 'config' },
      stagedFiles: []
    },
    '/repo/out',
    '/repo/Examples/Hello.Hosted'
  );

  assert.deepEqual(candidates, [
    '/repo/out/config',
    '/repo/Examples/Hello.Hosted/config',
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
    program: '/repo/out/bin/Hello.Hosted',
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
  assert.equal(basenameWithoutExtension('/repo/out/bin/Hello.Hosted.exe'), 'Hello.Hosted');
});

test('project tree models mark the selected project and profile', () => {
  const models = buildProjectTreeModels({
    workspace: {
      workspace: { path: '/repo/NGIN.ngin', directory: '/repo', name: 'NGIN', projectPaths: [] },
      projects: [
        {
          path: '/repo/Examples/Hello.Hosted/Hello.Hosted.nginproj',
          directory: '/repo/Examples/Hello.Hosted',
          name: 'Hello.Hosted',
          defaultProfile: 'Runtime',
          sourceRoots: ['src'],
          configInputs: ['config/app.cfg'],
          buildSources: [],
          projectRefs: [{ path: '/repo/Examples/Engine.Library/Engine.Library.nginproj' }],
          dependencies: [{ name: 'NGIN.Core' }],
          profiles: [{ name: 'Runtime', operatingSystem: 'linux', architecture: 'x64', environment: 'development', configInputs: [] }]
        },
        {
          path: '/repo/Examples/Engine.Library/Engine.Library.nginproj',
          directory: '/repo/Examples/Engine.Library',
          name: 'Engine.Library',
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
        path: '/repo/Examples/Hello.Hosted/Hello.Hosted.nginproj',
        directory: '/repo/Examples/Hello.Hosted',
        name: 'Hello.Hosted',
        defaultProfile: 'Runtime',
        sourceRoots: ['src'],
        configInputs: ['config/app.cfg'],
        buildSources: [],
        projectRefs: [{ path: '/repo/Examples/Engine.Library/Engine.Library.nginproj' }],
        dependencies: [{ name: 'NGIN.Core' }],
        profiles: [{ name: 'Runtime', operatingSystem: 'linux', architecture: 'x64', environment: 'development', configInputs: [] }]
      },
      profile: { name: 'Runtime', operatingSystem: 'linux', architecture: 'x64', environment: 'development', configInputs: [] }
    },
    launchManifestExists: true,
    stagedCompileCommandsAvailable: true
  });

  assert.equal(models.projects[0].selected, true);
  assert.equal(models.projects[0].description, 'active · Runtime');
  assert.deepEqual(models.childrenByProject.get('/repo/Examples/Hello.Hosted/Hello.Hosted.nginproj')?.map((entry) => entry.kind === 'group' ? entry.group : entry.kind), [
    'manifest',
    'dependencies',
    'artifacts'
  ]);
  assert.equal(models.childrenByProject.get('/repo/Examples/Hello.Hosted/Hello.Hosted.nginproj')?.[0]?.description, 'Hello.Hosted.nginproj');
  assert.deepEqual(models.dependenciesByProject.get('/repo/Examples/Hello.Hosted/Hello.Hosted.nginproj')?.projects.map((entry) => entry.label), ['Engine.Library']);
  assert.deepEqual(models.dependenciesByProject.get('/repo/Examples/Hello.Hosted/Hello.Hosted.nginproj')?.direct.map((entry) => entry.label), ['NGIN.Core']);
});

test('project tree models expose inspect groups for the active project only', () => {
  const activeProject = {
    path: '/repo/App/App.nginproj',
    directory: '/repo/App',
    name: 'App',
    defaultProfile: 'Runtime',
    sourceRoots: [],
    configInputs: [],
    buildSources: [],
    profiles: [{ name: 'Runtime', operatingSystem: 'linux', architecture: 'x64', environment: 'local', configInputs: [] }]
  };
  const inactiveProject = {
    path: '/repo/Tool/Tool.nginproj',
    directory: '/repo/Tool',
    name: 'Tool',
    sourceRoots: [],
    configInputs: [],
    buildSources: [],
    dependencies: [{ name: 'NGIN.Core' }],
    profiles: [{ name: 'Runtime', configInputs: [] }]
  };

  const models = buildProjectTreeModels({
    workspace: {
      workspace: { path: '/repo/NGIN.ngin', directory: '/repo', name: 'NGIN', projectPaths: [] },
      projects: [activeProject, inactiveProject],
      root: '/repo'
    },
    context: {
      workspace: {
        workspace: { path: '/repo/NGIN.ngin', directory: '/repo', name: 'NGIN', projectPaths: [] },
        projects: [activeProject, inactiveProject],
        root: '/repo'
      },
      project: activeProject,
      profile: activeProject.profiles[0]
    },
    inspectGraph: {
      schemaVersion: '4.0',
      kind: 'NGIN.CompositionGraph',
      plans: {
        packages: [{ name: 'NGIN.Core', version: '0.1.0', manifestPath: '/repo/Packages/NGIN.Core/NGIN.Core.nginpkg', closures: ['project'] }],
        packageFeatures: [{ package: 'NGIN.Core', feature: 'Reflection', manifestPath: '/repo/Packages/NGIN.Core/NGIN.Core.nginpkg' }],
        generators: [
          { name: 'ReflectionMetaGen', kind: 'Command', state: 'active', ownerName: 'NGIN.Reflection.MetaGen::ReflectionCodegen', tool: 'MetaGen', outputs: [{ role: 'Source', path: 'generated/reflection.cpp' }] },
          { name: 'WindowsOnly', kind: 'Command', state: 'excluded', reason: 'Platform expected windows-x64' }
        ],
        build: {
          inputs: [
            { role: 'Source', source: 'src', mode: 'Directory', ownerName: 'App' },
            { role: 'Config', source: 'config/app.cfg', stagedRelativePath: 'config/app.cfg', ownerName: 'App' }
          ]
        },
        launch: { executable: 'App', workingDirectory: '.' },
        launches: [{ name: 'default', executable: 'App', selected: true, workingDirectory: '.' }],
        stage: { files: [{ kind: 'config', target: 'config/app.cfg' }] },
        environment: { variables: [{ name: 'TOKEN', value: '<redacted>', secret: true }] },
        diagnostics: [{ severity: 'warning', subject: 'Launch', message: 'Example warning' }]
      }
    },
    launchManifestExists: false,
    stagedCompileCommandsAvailable: false
  });

  assert.equal(models.childrenByProject.get(activeProject.path)?.some((entry) => entry.kind === 'group' && entry.label === 'Dependencies'), true);
  assert.equal(models.childrenByProject.get(inactiveProject.path)?.some((entry) => entry.kind === 'group' && entry.label === 'Dependencies'), false);
  assert.equal(models.projects[0].description, 'active · Runtime · 1 problem');

  const activeInspect = models.inspectByProject.get(activeProject.path);
  assert.deepEqual(activeInspect?.groups.map((group) => group.kind), [
    'tooling',
    'launch',
    'problems'
  ]);
  assert.equal(models.childrenByProject.get(activeProject.path)?.find((entry) => entry.kind === 'group' && entry.group === 'problems')?.description, '1');
  assert.deepEqual(activeInspect?.entriesByGroup.get('tooling')?.map((entry) => `${entry.label}:${entry.description}`), [
    'Reflection code generation:MetaGen · active'
  ]);
  assert.deepEqual(activeInspect?.entriesByGroup.get('generators')?.map((entry) => `${entry.label}:${entry.description}`), [
    'ReflectionMetaGen:Active • NGIN.Reflection.MetaGen::ReflectionCodegen • MetaGen',
    'WindowsOnly:Excluded'
  ]);
  assert.deepEqual(activeInspect?.entriesByGroup.get('generators')?.[0]?.children?.map((entry) => entry.label), [
    'State',
    'Kind',
    'Owner',
    'Tool',
    'Source'
  ]);
  assert.deepEqual(activeInspect?.entriesByGroup.get('inputs')?.map((entry) => `${entry.label}:${entry.description}`), [
    'Sources:1',
    'Configs:1'
  ]);
  assert.deepEqual(activeInspect?.entriesByGroup.get('inputs')?.[0]?.children?.map((entry) => `${entry.label}:${entry.description}`), [
    'src:Source • Directory • App'
  ]);
  assert.equal(models.inspectByProject.has(inactiveProject.path), false);
  assert.deepEqual(models.dependenciesByProject.get(activeProject.path)?.transitive.map((entry) => entry.label), ['NGIN.Core']);
});

test('project tree dependency models group authored base uses', () => {
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
          projectRefs: [{ path: '/repo/Engine.Library/Engine.Library.nginproj' }],
          dependencies: [{ name: 'NGIN.Core' }],
          profiles: [
            {
              name: 'Runtime',
              environment: 'development',
              configInputs: [],
              projectRefs: [{ path: '/repo/Engine.Library/Engine.Library.nginproj' }],
              dependencies: [{ name: 'NGIN.Core' }, { name: 'NGIN.Reflection' }]
            }
          ]
        },
        {
          path: '/repo/Engine.Library/Engine.Library.nginproj',
          directory: '/repo/Engine.Library',
          name: 'Engine.Library',
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
  assert.deepEqual(dependencies?.projects.map((entry) => `${entry.label}:${entry.description}`), ['Engine.Library:workspace']);
  assert.deepEqual(dependencies?.direct.map((entry) => `${entry.label}:${entry.description}:${entry.targetPath ?? 'unresolved'}`), [
    'NGIN.Core:direct:/repo/Packages/NGIN.Core/NGIN.Core.nginpkg'
  ]);
});

test('project tree generator inspect entries tolerate numeric output counts', () => {
  const project = {
    path: '/repo/App/App.nginproj',
    directory: '/repo/App',
    name: 'App',
    sourceRoots: [],
    configInputs: [],
    buildSources: [],
    profiles: [{ name: 'Debug', configInputs: [] }]
  };

  const models = buildProjectTreeModels({
    workspace: {
      workspace: { path: '/repo/NGIN.ngin', directory: '/repo', name: 'NGIN', projectPaths: [] },
      projects: [project],
      root: '/repo'
    },
    context: {
      workspace: {
        workspace: { path: '/repo/NGIN.ngin', directory: '/repo', name: 'NGIN', projectPaths: [] },
        projects: [project],
        root: '/repo'
      },
      project,
      profile: project.profiles[0]
    },
    inspectGraph: {
      schemaVersion: '4.0',
      kind: 'NGIN.CompositionGraph',
      plans: {
        generators: [
          { name: 'ReflectionMetaGen', kind: 'Command', ownerName: 'App', tool: 'MetaGen', outputs: 1 }
        ]
      }
    },
    launchManifestExists: false,
    stagedCompileCommandsAvailable: false
  });

  const generator = models.inspectByProject.get(project.path)?.entriesByGroup.get('generators')?.[0];
  assert.equal(generator?.label, 'ReflectionMetaGen');
  assert.match(generator?.tooltip ?? '', /Outputs: 1/);
  assert.deepEqual(generator?.children?.map((entry) => entry.label), [
    'State',
    'Kind',
    'Owner',
    'Tool'
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
      project: { path: '/repo/Examples/Hello.Hosted/Hello.Hosted.nginproj', directory: '/repo/Examples/Hello.Hosted', name: 'Hello.Hosted', sourceRoots: [], configInputs: [], buildSources: [], profiles: [] },
      profile: { name: 'Runtime', operatingSystem: 'linux', architecture: 'x64', environment: 'development', configInputs: [] }
    },
    outputDir: '/repo/.ngin/build/Hello.Hosted/Runtime',
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
