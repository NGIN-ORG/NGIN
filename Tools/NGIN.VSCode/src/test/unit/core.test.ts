import assert from 'node:assert/strict';
import { promises as fs } from 'node:fs';
import { tmpdir } from 'node:os';
import * as path from 'node:path';
import test from 'node:test';
import {
  createBrowseConfiguration,
  createFallbackConfiguration,
  createSourceConfiguration,
  findCompileCommand,
  parseCompileCommands,
  selectCompileCommand,
  splitCommandLine
} from '../../core/compileCommands';
import { dependencyLockPath, lifecycleArguments } from '../../core/commandArguments';
import { parseActionDiagnostics } from '../../core/actionDiagnostics';
import { parseCliDiagnostics, parseCompilerDiagnostics } from '../../core/diagnostics';
import { graphOwnsFile, projectsForFile } from '../../core/projectOwnership';
import { createNativeDebugConfiguration, createNativeTestDebugConfiguration } from '../../core/debugConfiguration';
import { displayOptionValue, parseCompositionGraph } from '../../core/graph';
import { insertBuildItem, kindForPath, updateExactBuildItemPaths, updateProjectAttributes } from '../../core/manifestEdits';
import { parseAttributes, parseWorkspaceChoices, parseWorkspaceProjectRules } from '../../core/manifestText';
import {
  compileCommandsPath,
  contextKey,
  isProjectConfigurationPath,
  isWithin,
  projectOutputDirectory,
  safePathComponent
} from '../../core/paths';
import { enumerateProjectFiles } from '../../core/projectFiles';
import { createProjectTemplate } from '../../core/projectTemplates';
import { attributeChoices, loadManifestMetadata } from '../../core/manifestMetadata';
import { describeCliFailure, shouldLoadGraph, unsupportedSelectionOption } from '../../core/cliCompatibility';
import { projectCanRun } from '../../core/projectCapabilities';
import { projectActionDescriptors } from '../../core/projectActions';
import { projectTreePresentation } from '../../core/projectTreePresentation';
import { resolveWorkspaceChoice } from '../../core/selectionChoices';
import { statusPresentation } from '../../core/statusPresentation';
import { isTransientAnalysisFailure } from '../../core/analysisPolicy';
import { outputPolicy } from '../../core/outputPolicy';
import {
  formatLifecycleCommand, formatRuntimeLine, LifecycleOutputPresenter, parseNinjaProgress
} from '../../core/outputPresentation';
import type { CompositionGraph, NginContext, ProjectCandidate } from '../../model';

function graph(): CompositionGraph {
  return {
    kind: 'NGIN.CompositionGraph', state: 'resolved',
    product: { identity: 'App', name: 'App', artifactKind: 'Executable', libraryKind: 'None', languageStandard: 'C++23' },
    selection: {
      identity: 'Selection', configuration: 'Debug', targetOperatingSystem: 'windows',
      targetArchitecture: 'x64', compiler: 'clang', debugSymbols: true
    },
    options: [], packages: [], exports: [], capabilityBindings: [], actions: [], plugins: [], contributions: [],
    buildItems: [
      { identity: 'Include:include', kind: 'IncludeDirectory', path: 'include' },
      { identity: 'Define:APP', kind: 'Define', path: 'APP=1' }
    ],
    runs: [], tests: [], benchmarks: [], publishes: [], edges: []
  };
}

function projectContext(project: ProjectCandidate): NginContext {
  return {
    workspaceFolder: path.resolve('workspace'),
    projectManifest: project.manifest,
    projectName: project.name,
    configuration: 'Debug',
    target: 'host',
    toolchain: 'auto',
    options: {},
    outputDirectory: path.resolve('workspace', 'build', project.name)
  };
}

test('project actions prioritize valid lifecycle work and progressively disclose expert commands', () => {
  const project: ProjectCandidate = {
    manifest: path.resolve('workspace', 'App.nginproj'), directory: path.resolve('workspace'),
    name: 'App', artifactKind: 'Executable', hasRun: true, hasTests: true, hasBenchmarks: true
  };
  const actions = projectActionDescriptors({
    project, context: projectContext(project), canRun: true, canTest: true, canBenchmark: true,
    hasAnalyze: true, hasFormat: false, graphReady: true, canPublish: true,
    configurationChoices: 2, targetChoices: 1, toolchainChoices: 1
  });
  assert.deepEqual(actions.filter(action => action.group === 'Lifecycle').map(action => action.label), [
    'Build Project', 'Run Project', 'Debug Project', 'Test Project', 'Benchmark Project'
  ]);
  assert.equal(actions.find(action => action.label === 'Select Configuration')?.description, 'Debug');
  assert.equal(actions.find(action => action.command === 'ngin.selectProject')?.label, 'Select Project');
  assert.ok(actions.some(action => action.group === 'Advanced' && action.command === 'ngin.showGraph'));
  assert.ok(actions.some(action => action.command === 'ngin.rebuild'));
  assert.ok(actions.some(action => action.command === 'ngin.clean'));
  assert.ok(actions.some(action => action.command === 'ngin.openOutputDirectory'));
  assert.ok(actions.some(action => action.command === 'ngin.analyze'));
  assert.equal(actions.some(action => action.command === 'ngin.formatSources'), false);

  const busy = projectActionDescriptors({
    project, context: projectContext(project), canRun: true, canTest: true, canBenchmark: true,
    hasAnalyze: true, hasFormat: true, graphReady: true, canPublish: false,
    configurationChoices: 0, targetChoices: 0, toolchainChoices: 0, busy: 'build'
  });
  assert.deepEqual(busy.map(action => action.command), ['ngin.cancel', 'ngin.showOutput']);
});

test('project row context menu exposes selection and lifecycle commands directly', async () => {
  const manifest = JSON.parse(await fs.readFile(path.resolve('package.json'), 'utf8')) as {
    contributes: { menus: { 'view/item/context': Array<{ command: string; group: string }> } };
  };
  const menu = manifest.contributes.menus['view/item/context'];
  assert.equal(menu.find(item => item.group === '1_project@1')?.command, 'ngin.selectProject');
  assert.deepEqual(menu.filter(item => item.group.startsWith('2_lifecycle@')).map(item => item.command), [
    'ngin.configure', 'ngin.build', 'ngin.run', 'ngin.debug', 'ngin.rebuild', 'ngin.clean'
  ]);
});

test('stale persisted workspace selections fall back to valid defaults', () => {
  assert.equal(resolveWorkspaceChoice('default', ['auto'], 'auto'), 'auto');
  assert.equal(resolveWorkspaceChoice('clang', ['auto', 'clang'], 'auto'), 'clang');
  assert.equal(resolveWorkspaceChoice(undefined, ['Debug', 'Release'], 'Debug'), 'Debug');
  assert.equal(resolveWorkspaceChoice('default', undefined, 'auto'), 'default');
});

test('project tree keeps context and readiness compact', () => {
  assert.deepEqual(projectTreePresentation({
    configuration: 'Debug', activeFile: true, fallback: false, graphReady: true, configured: true
  }), { description: 'Debug · active file · configured', status: 'configured' });
  assert.equal(projectTreePresentation({
    configuration: 'Release', activeFile: false, fallback: true, graphReady: true, configured: false
  }).description, 'Release · selected · needs build');
  assert.equal(projectTreePresentation({
    configuration: 'Debug', activeFile: false, fallback: false, graphReady: true, configured: true,
    lastOperation: { command: 'build', state: 'failed', durationMs: 500 }
  }).status, 'build failed');
  assert.equal(projectTreePresentation({
    configuration: 'Debug', activeFile: false, fallback: false, graphReady: true, configured: true, busy: 'stage'
  }).status, 'stage in progress');
  assert.equal(projectTreePresentation({
    configuration: 'Debug', activeFile: false, fallback: false, graphReady: true, configured: true,
    lastOperation: { command: 'build', state: 'succeeded', durationMs: 500 }
  }).status, 'built');
});

test('status presentation explains active-file, fallback, busy, and issue states', () => {
  const project: ProjectCandidate = {
    manifest: path.resolve('workspace', 'App.nginproj'), directory: path.resolve('workspace'), name: 'App'
  };
  const context = projectContext(project);
  const active = statusPresentation({ context, project, reason: 'activeFile' });
  assert.equal(active.text, '$(project) App · Debug');
  assert.match(active.tooltip, /owns the active file/u);
  assert.match(active.accessibilityLabel, /active file project/u);

  const fallback = statusPresentation({
    context, project, reason: 'default',
    lastOperation: { command: 'build', state: 'succeeded', completedAt: 1, durationMs: 1250 }
  });
  assert.match(fallback.tooltip, /Default because/u);
  assert.match(fallback.tooltip, /in 1\.3s/u);

  const busy = statusPresentation({ context, project, reason: 'operation', operation: 'build' });
  assert.equal(busy.text, '$(loading~spin) App · build');
  assert.match(busy.accessibilityLabel, /build in progress/u);

  const issue = statusPresentation({ context, project, reason: 'default', graphError: 'invalid manifest' });
  assert.equal(issue.text, '$(warning) App · Debug');
  assert.match(issue.accessibilityLabel, /issue/u);
});

test('analysis contention and cancellation are retryable rather than user-facing failures', () => {
  assert.equal(isTransientAnalysisFailure(new Error('Another NGIN operation is already running. Cancel it or wait for it to finish.')), true);
  assert.equal(isTransientAnalysisFailure(new Error('NGIN operation was cancelled.')), true);
  assert.equal(isTransientAnalysisFailure(new Error('clang-tidy returned malformed diagnostics')), false);
});

test('compact output hides successful machine payloads while trace remains available', () => {
  const graph = ['graph', '--format', 'json'];
  assert.deepEqual(outputPolicy(graph, false, false, 'compact'), {
    appendCommand: false, streamRaw: false, appendLifecycleTrace: false, machineReadable: true
  });
  assert.deepEqual(outputPolicy(graph, false, false, 'commands'), {
    appendCommand: true, streamRaw: false, appendLifecycleTrace: false, machineReadable: true
  });
  assert.deepEqual(outputPolicy(graph, false, false, 'trace'), {
    appendCommand: true, streamRaw: true, appendLifecycleTrace: false, machineReadable: true
  });
  assert.equal(outputPolicy(['format'], false, true, 'compact').streamRaw, true);
  assert.equal(outputPolicy(['build'], true, false, 'trace').appendLifecycleTrace, true);
});

test('workspace choices and project rules are read without creating a semantic model', () => {
  const source = `<Workspace Name="Demo">
  <Discover><Projects Include="App/App.nginproj" /><Projects Include="Examples/**/*.nginproj" Exclude="**/Old/**" /></Discover>
  <Configurations><Configuration Name="Debug" /><Configuration Name="Release" /></Configurations>
  <Targets><Target Name="host" OS="host" Architecture="host" /></Targets>
  <Toolchains><Toolchain Name="default" Compiler="default" Linker="default" /></Toolchains>
  <Profiles Default="dev"><Profile Name="dev" Configuration="Release" /></Profiles>
</Workspace>`;
  const choices = parseWorkspaceChoices(source);
  assert.equal(choices.name, 'Demo');
  assert.deepEqual(choices.configurations, ['Debug', 'Release']);
  assert.equal(choices.defaults.configuration, 'Release');
  assert.deepEqual(choices.profiles, [{
    name: 'dev', configuration: 'Release', target: undefined, toolchain: undefined, run: undefined
  }]);
  assert.deepEqual(parseWorkspaceProjectRules(source), [
    { include: 'App/App.nginproj', exclude: undefined },
    { include: 'Examples/**/*.nginproj', exclude: '**/Old/**' }
  ]);
});

test('XML attributes decode entities only after lexical extraction', () => {
  assert.deepEqual(parseAttributes(`Name="A&amp;B" Before='2.0'`), { Name: 'A&B', Before: '2.0' });
});

test('build membership insertion preserves the surrounding XML and ordering', () => {
  const source = `<Executable Name="App">\n  <!-- retained -->\n  <Uses />\n  <Run Name="Default" />\n</Executable>\n`;
  const edit = insertBuildItem(source, 'Header', 'Include', 'include/App.hpp');
  assert.ok(edit);
  const updated = source.slice(0, edit.start) + edit.text + source.slice(edit.end);
  assert.match(updated, /<Uses \/>\n  <Build>\n    <Header Include="include\/App.hpp" \/>\n  <\/Build>\n  <Run/);
  assert.match(updated, /<!-- retained -->/);
});

test('build membership insertion expands a self-closing Build element', () => {
  const source = `<Executable Name="App">\n  <Build />\n</Executable>`;
  const edit = insertBuildItem(source, 'Source', 'Remove', 'src/old.cpp');
  assert.ok(edit);
  const updated = source.slice(0, edit.start) + edit.text + source.slice(edit.end);
  assert.match(updated, /<Build>\n    <Source Remove="src\/old.cpp" \/>\n  <\/Build>/);
});

test('exact file rules can follow a rename without touching globs or comments', () => {
  const source = `<Build>\n  <Source Include="src/old.cpp" />\n  <Source Include="src/*.cpp" />\n  <!-- src/old.cpp -->\n</Build>`;
  const edits = updateExactBuildItemPaths(source, 'src/old.cpp', 'src/new.cpp');
  assert.equal(edits.length, 1);
  const edit = edits[0];
  const updated = source.slice(0, edit.start) + edit.text + source.slice(edit.end);
  assert.match(updated, /Include="src\/new.cpp"/);
  assert.match(updated, /Include="src\/\*\.cpp"/);
  assert.match(updated, /<!-- src\/old.cpp -->/);
  assert.equal(kindForPath('include/App.hpp'), 'Header');
  assert.equal(kindForPath('src/module.ixx'), 'CxxModule');
});

test('product editor updates, adds, and removes root attributes without rewriting children', () => {
  const source = `<Library Name="Old" Kind="Shared" Version="1.0.0">\n  <!-- keep -->\n  <Build />\n</Library>`;
  const edits = updateProjectAttributes(source, { Name: 'New', Version: undefined, Kind: 'Static' })
    .sort((left, right) => right.start - left.start);
  let updated = source;
  for (const edit of edits) updated = updated.slice(0, edit.start) + edit.text + updated.slice(edit.end);
  assert.match(updated, /<Library Name="New" Kind="Static">/);
  assert.doesNotMatch(updated, /Version=/);
  assert.match(updated, /<!-- keep -->/);
});

test('CLI diagnostics preserve Windows drive paths and hints', () => {
  const diagnostics = parseCliDiagnostics('C:\\work\\App.nginproj:12:4: error NGIN1001: unexpected element\n  hint: use Build\n');
  assert.deepEqual(diagnostics, [{
    path: 'C:\\work\\App.nginproj', line: 12, column: 4, severity: 'error', code: 'NGIN1001',
    message: 'unexpected element', hint: 'use Build'
  }]);
});

test('compiler diagnostics are parsed independently from manifest diagnostics', () => {
  const values = parseCompilerDiagnostics(
    'C:\\work\\main.cpp:4:7: warning: unused value [-Wunused-value]\n' +
    'C:\\work\\other.cpp(9,3): error C2065: undeclared identifier\n');
  assert.equal(values.length, 2);
  assert.equal(values[0].code, '-Wunused-value');
  assert.equal(values[1].code, 'C2065');
});

test('lifecycle presentation keeps build and run output compact', () => {
  let now = 0;
  let output = '';
  const args = [
    'run', '--project', 'C:\\work\\NGIN.UI.Gallery.Hosted.nginproj', '--configuration', 'Debug',
    '--target', 'host', '--toolchain', 'default', '--output', 'C:\\work\\build'
  ];
  const presenter = new LifecycleOutputPresenter({
    command: 'run', args, label: 'NGIN.UI.Gallery.Hosted', append: value => { output += value; }, now: () => now
  });
  const event = (name: string, target = 'NGIN.UI.Gallery.Hosted', extra: Record<string, unknown> = {}) =>
    `\x1eNGIN ${JSON.stringify({ event: name, kind: 'NGIN.EditorEvent', target, ...extra })}\n`;

  presenter.accept('stdout', event('configure-skip', 'NGIN.UI.Gallery.Shared'));
  presenter.accept('stdout', event('configure-skip'));
  presenter.accept('stdout', event('build-start', 'NGIN.UI.Gallery.Shared'));
  presenter.accept('stdout', '[1/3] Building CXX object CMakeFiles/Gallery.dir/src/Gallery.cpp.obj\n');
  presenter.accept('stdout', '[2/3] Building CXX object CMakeFiles/Gallery.dir/src/GalleryCustomControls.cpp.obj\n');
  presenter.accept('stdout', '[3/3] Linking CXX static library NGIN.UI.Gallery.Shared.lib\n');
  presenter.accept('stdout', 'F:/work/SmartPointers.hpp:36:15: warning: unknown attribute [-Wunknown-attributes]\n');
  now = 1400;
  presenter.accept('stdout', event('build-end', 'NGIN.UI.Gallery.Shared'));
  presenter.accept('stdout', event('build-start'));
  presenter.accept('stdout', 'ninja: no work to do.\n');
  now = 1600;
  presenter.accept('stdout', event('build-end'));
  presenter.accept('stdout', event('stage-start'));
  now = 1700;
  presenter.accept('stdout', event('stage-end', 'NGIN.UI.Gallery.Hosted', { count: 18 }));
  presenter.accept('stdout', event('run-start', 'NGIN.UI.Gallery.Hosted', { detail: 'NGIN.UI.Gallery.Hosted.exe' }));
  presenter.accept('stdout',
    '[2026-08-11T22:20:54.876135100+0200][Info][Kernel] state transition -> ServicesBuilt '
    + '{host="NGIN.UI.Gallery.Hosted"} (F:/work/Kernel.cpp:498)\n'
    + '[2026-08-11T22:20:55.354906500+0200][Info][ModuleLoader] module started: NGIN.UI.Runtime '
    + '{host="NGIN.UI.Gallery.Hosted"} (F:/work/Kernel.cpp:498)\n');
  now = 6300;
  presenter.accept('stdout', event('run-end'));
  presenter.complete(0);

  assert.match(output, /> ngin run NGIN\.UI\.Gallery\.Hosted --configuration Debug/u);
  assert.match(output, /CONFIGURE\n  ✓ NGIN\.UI\.Gallery\.Shared\s+up to date/u);
  assert.match(output, /BUILD\n  NGIN\.UI\.Gallery\.Shared\n    \[1\/3\] Compiling Gallery\.cpp/u);
  assert.match(output, /\[3\/3\] Linking\n  ✓ Completed\s+1\.4s/u);
  assert.match(output, /NGIN\.UI\.Gallery\.Hosted\n  ✓ Completed\s+up to date/u);
  assert.match(output, /⚠ 1 warning/u);
  assert.match(output, /STAGE\n  NGIN\.UI\.Gallery\.Hosted\n  ✓ 18 files\s+0\.1s/u);
  assert.match(output, /RUN\n  Starting NGIN\.UI\.Gallery\.Hosted\.exe/u);
  assert.match(output, /22:20:54\s+Kernel\s+Services built/u);
  assert.match(output, /22:20:55\s+ModuleLoader\s+Started NGIN\.UI\.Runtime/u);
  assert.doesNotMatch(output, /SmartPointers\.hpp|Kernel\.cpp|\[Info\]/u);
  assert.match(output, /Process exited with code 0/u);
  assert.match(output, /Completed in 6\.3s/u);
});

test('lifecycle display uses command syntax instead of a configuration tagline', () => {
  assert.equal(formatLifecycleCommand(
    ['build', '--configuration', 'Release', '--target', 'linux-arm64', '--toolchain', 'clang'], 'Demo App'
  ), '> ngin build "Demo App" --configuration Release --target linux-arm64 --toolchain clang');
  assert.equal(formatRuntimeLine(
    '[2026-08-11T22:20:55.354906500+0200][Info][Kernel] kernel entered Running state (Kernel.cpp:10)'
  ), '');
  assert.deepEqual(parseNinjaProgress(
    '[12/84] Building CXX object CMakeFiles/App.dir/src/Renderer.cpp.obj'
  ), { current: 12, total: 84, action: 'Compiling Renderer.cpp' });
  assert.equal(parseNinjaProgress('[0/2] Re-checking globbed directories...'), undefined);
});

test('Composition Graph parsing rejects incomplete envelopes', () => {
  assert.equal(parseCompositionGraph(JSON.stringify(graph())).product.name, 'App');
  assert.throws(() => parseCompositionGraph('{}'), /unsupported or unresolved/);
  assert.throws(() => parseCompositionGraph('{'), /invalid graph JSON/);
  assert.equal(displayOptionValue('{"type":"Boolean","value":false}'), 'false');
});

test('compile command parsing handles quoted compiler paths and maps configuration', () => {
  const directory = path.resolve('project');
  const sourceFile = path.join(directory, 'src', 'main.cpp');
  const include = path.join(directory, 'include');
  const args = [`${path.join(directory, 'tool chain', 'clang++')}`, `-I${include}`, '-DAPP=1', '-std=c++23', '-c', sourceFile];
  const parsed = parseCompileCommands(JSON.stringify([{ directory, file: sourceFile, arguments: args }]));
  const configuration = createSourceConfiguration(parsed[0], graph());
  assert.equal(configuration.compilerPath, args[0]);
  assert.deepEqual(configuration.includePath, [include]);
  assert.deepEqual(configuration.defines, ['APP=1']);
  assert.equal(configuration.standard, 'c++23');
  assert.equal(configuration.intelliSenseMode, 'windows-clang-x64');
  assert.deepEqual(splitCommandLine(`"${args[0]}" "${sourceFile}"`), [args[0], sourceFile]);
});

test('an exact compile command identifies package sources owned through an application build', () => {
  const packageSource = path.resolve('workspace', 'Packages', 'Example', 'src', 'Control.cpp');
  const entries = [{ directory: path.dirname(packageSource), file: packageSource, arguments: ['c++', '-I../include', '-c', packageSource] }];
  assert.equal(findCompileCommand(entries, packageSource), entries[0]);
  assert.equal(findCompileCommand(entries, path.resolve('workspace', 'App', 'main.cpp')), undefined);
});

test('compile command parsing preserves escaped definition quotes and following includes', () => {
  const directory = path.resolve('project with spaces');
  const compiler = path.join(directory, 'tool chain', 'clang++.exe');
  const sourceFile = path.join(directory, 'src', 'main.cpp');
  const include = path.join(directory, 'include');
  const command = `"${compiler}" -DNGIN_PLATFORM=\\"Windows\\" -I"${include}" -std=c++23 -c "${sourceFile}"`;
  const entry = parseCompileCommands(JSON.stringify([{ directory, file: sourceFile, command }]))[0];
  const configuration = createSourceConfiguration(entry, graph());
  assert.equal(configuration.compilerPath, compiler);
  assert.deepEqual(configuration.defines, ['NGIN_PLATFORM="Windows"']);
  assert.deepEqual(configuration.includePath, [include]);
});

test('fallback configuration preserves graph define values', () => {
  const value = graph();
  value.buildItems.push({
    identity: 'Define:APP_VERSION', kind: 'Define', path: 'APP_VERSION', value: '"1.2.3"'
  });
  assert.deepEqual(createFallbackConfiguration(value, path.resolve('project')).defines, [
    'APP=1', 'APP_VERSION="1.2.3"'
  ]);
});

test('headers use the closest compile command and browse paths are aggregated', () => {
  const root = path.resolve('project');
  const entries = [
    { directory: root, file: path.join(root, 'one', 'a.cpp'), arguments: ['clang++', '-Ione', 'one/a.cpp'] },
    { directory: root, file: path.join(root, 'two', 'b.cpp'), arguments: ['clang++', '-Itwo', 'two/b.cpp'] }
  ];
  assert.equal(selectCompileCommand(entries, path.join(root, 'two', 'b.hpp'))?.file, entries[1].file);
  const browse = createBrowseConfiguration(entries, graph(), root);
  assert.ok(browse.browsePath.includes(path.join(root, 'one')));
  assert.ok(browse.browsePath.includes(path.join(root, 'two')));
});

test('selection keys are deterministic and output paths are bounded', () => {
  const project: ProjectCandidate = { manifest: path.resolve('App.nginproj'), directory: path.resolve('.'), name: 'My App' };
  const output = projectOutputDirectory(path.resolve('.'), project, 'Debug', 'host', 'default');
  assert.ok(isWithin(path.resolve('.'), output));
  assert.equal(safePathComponent('My App/Debug'), 'My_App_Debug');
  const base = {
    workspaceFolder: path.resolve('.'), projectManifest: project.manifest, projectName: project.name,
    configuration: 'Debug', target: 'host', toolchain: 'auto', options: { B: '2', A: '1' }, outputDirectory: output
  } satisfies NginContext;
  assert.equal(contextKey(base), contextKey({ ...base, options: { A: '1', B: '2' } }));
  assert.equal(compileCommandsPath(base), path.join(output, 'cmake', 'compile_commands.json'));
  assert.equal(isProjectConfigurationPath(base, path.join(project.directory, 'src', 'main.cpp')), true);
  assert.equal(isProjectConfigurationPath(base, path.join(output, 'actions', 'generated.cpp')), true);
  assert.equal(isProjectConfigurationPath(base, path.resolve('..', 'unrelated', 'other.cpp')), false);
  const lock = dependencyLockPath(base);
  assert.equal(lock, path.join(output, 'ngin.lock'));
  assert.deepEqual(lifecycleArguments('lock', base), [
    'package', 'lock', '--project', project.manifest, '--configuration', 'Debug', '--target', 'host',
    '--toolchain', 'auto', '--option', 'A=1', '--option', 'B=2', '--output', lock
  ]);
  assert.deepEqual(lifecycleArguments('analyze', base).slice(-4), ['--output', output, '--lock', lock]);
  const runArguments = lifecycleArguments('run', { ...base, run: 'diagnostics', profile: 'dev' });
  assert.equal(runArguments[runArguments.indexOf('--run') + 1], 'diagnostics');
  assert.equal(runArguments[runArguments.indexOf('--profile') + 1], 'dev');
  assert.equal(runArguments[runArguments.indexOf('--output') + 1], output);
});

test('structured Action diagnostics retain source, rule, and precise range', () => {
  const envelope = parseActionDiagnostics(JSON.stringify({
    kind: 'NGIN.ActionDiagnostics', state: 'complete', diagnostics: [{
      file: 'C:/work/main.cpp', range: { start: { line: 8, column: 16 }, end: { line: 8, column: 17 } },
      severity: 'warning', source: 'NGIN.Tooling.ClangTidy::Analyze', code: 'readability-magic-numbers',
      message: '42 is a magic number', fixes: []
    }]
  }));
  assert.equal(envelope.diagnostics[0].code, 'readability-magic-numbers');
  assert.equal(envelope.diagnostics[0].range.start.column, 16);
  assert.throws(() => parseActionDiagnostics('{"kind":"wrong"}'), /invalid Action diagnostics envelope/);
  const withFix = parseActionDiagnostics(JSON.stringify({
    kind: 'NGIN.ActionDiagnostics', state: 'complete', diagnostics: [{
      file: 'main.cpp', range: { start: { line: 1, column: 1 }, end: { line: 1, column: 2 } },
      severity: 'warning', source: 'Example', message: 'replace value',
      fixes: [{ title: 'Replace value', safe: true, edits: [{
        range: { start: { line: 1, column: 1 }, end: { line: 1, column: 2 } }, text: 'x'
      }] }]
    }]
  }));
  assert.equal(withFix.diagnostics[0].fixes?.[0].title, 'Replace value');
});

test('source ownership prefers the deepest project and exposes true ambiguities', () => {
  const root = path.resolve('workspace');
  const projects: ProjectCandidate[] = [
    { manifest: path.join(root, 'Root.nginproj'), directory: root, name: 'Root' },
    { manifest: path.join(root, 'nested', 'One.nginproj'), directory: path.join(root, 'nested'), name: 'One' },
    { manifest: path.join(root, 'nested', 'Two.nginproj'), directory: path.join(root, 'nested'), name: 'Two' }
  ];
  const owners = projectsForFile(projects, path.join(root, 'nested', 'src', 'main.cpp'));
  assert.deepEqual(owners.map(project => project.name), ['One', 'Two', 'Root']);

  const context: NginContext = {
    workspaceFolder: root,
    projectManifest: projects[1].manifest,
    projectName: projects[1].name,
    configuration: 'Debug', target: 'host', toolchain: 'auto', options: {},
    outputDirectory: path.join(root, 'build', projects[1].name)
  };
  const value = graph();
  value.buildItems.push(
    { identity: 'Source:src/main.cpp', kind: 'Source', path: 'src/main.cpp' },
    { identity: 'Source:generated.cpp', kind: 'Source', path: 'generated/generated.cpp', generated: true }
  );
  assert.equal(graphOwnsFile(value, context, path.join(root, 'nested', 'src', 'main.cpp')), true);
  assert.equal(graphOwnsFile(value, context, path.join(context.outputDirectory, 'actions', 'generated', 'generated.cpp')), true);
  assert.equal(graphOwnsFile(value, context, path.join(root, 'nested', 'src', 'other.cpp')), false);
});

test('native debug configuration uses staged graph run intent', () => {
  const root = path.resolve('workspace');
  const context: NginContext = {
    workspaceFolder: root,
    projectManifest: path.join(root, 'App.nginproj'),
    projectName: 'App', configuration: 'Debug', target: 'host', toolchain: 'auto', options: {},
    outputDirectory: path.join(root, 'build', 'App')
  };
  const value = graph();
  value.runs = [{
    identity: 'App:Run:Default', name: 'Default', default: true, executableKind: 'Product', executable: 'App',
    workingDirectory: 'work', arguments: ['from-graph'], environment: { APP_MODE: 'test', PATH: 'existing' }, secrets: {}
  }];
  const configuration = createNativeDebugConfiguration(value, context, path.join(context.outputDirectory, 'stage', 'bin', 'App.exe'), {
    args: ['from-user'], stopAtEntry: true
  }, 'win32', ';');
  assert.equal(configuration.type, 'cppvsdbg');
  assert.deepEqual(configuration.args, ['from-graph', 'from-user']);
  assert.equal(configuration.cwd, path.resolve(context.outputDirectory, 'stage', 'work'));
  assert.equal(configuration.environment.find(item => item.name === 'APP_MODE')?.value, 'test');
  assert.match(configuration.environment.find(item => item.name === 'PATH')?.value ?? '', /stage[\\/]lib;existing$/);
});

test('native test debugging uses TestPlan arguments without a Run definition', () => {
  const root = path.resolve('workspace');
  const context: NginContext = {
    workspaceFolder: root, projectManifest: path.join(root, 'Tests.nginproj'), projectName: 'Tests',
    configuration: 'Debug', target: 'host', toolchain: 'auto', options: {}, outputDirectory: path.join(root, 'build')
  };
  const value = graph();
  value.tests = [{ identity: 'Tests:Test:default', name: 'default', arguments: ['--reporter', 'console'] }];
  const configuration = createNativeTestDebugConfiguration(value, context, path.join(root, 'build', 'stage', 'bin', 'Tests.exe'), {
    args: ['--filter', 'smoke']
  }, 'win32', ';');
  assert.deepEqual(configuration.args, ['--reporter', 'console', '--filter', 'smoke']);
  assert.equal(configuration.cwd, path.join(root, 'build', 'stage'));
});

test('project files distinguish graph membership and physical boundaries', async () => {
  const root = await fs.mkdtemp(path.join(tmpdir(), 'ngin-vscode-'));
  try {
    await fs.mkdir(path.join(root, 'src'), { recursive: true });
    await fs.mkdir(path.join(root, 'Nested'), { recursive: true });
    await fs.writeFile(path.join(root, 'App.nginproj'), '<Executable Name="App" />');
    await fs.writeFile(path.join(root, 'src', 'main.cpp'), 'int main() {}');
    await fs.writeFile(path.join(root, 'src', 'Player.hpp'), '#pragma once');
    await fs.writeFile(path.join(root, 'src', 'unused.cpp'), '');
    await fs.writeFile(path.join(root, '.env'), 'MODE=development');
    await fs.writeFile(path.join(root, 'Nested', 'Nested.nginproj'), '<Library Name="Nested" Kind="Static" />');
    await fs.writeFile(path.join(root, 'Nested', 'README.md'), '# Nested');
    const generatedDirectory = path.join(root, 'build', 'actions');
    await fs.mkdir(generatedDirectory, { recursive: true });
    await fs.writeFile(path.join(generatedDirectory, 'generated.hpp'), '// generated');
    await fs.mkdir(path.join(generatedDirectory, 'generated'), { recursive: true });
    await fs.writeFile(path.join(generatedDirectory, 'generated', 'action.cpp'), '// generated action');
    const value = graph();
    value.actions = [{
      identity: 'Example.Generator::Generate', kind: 'Generate',
      inputs: ['src/Player.hpp'], outputs: ['generated/action.cpp']
    }];
    value.buildItems.push(
      { identity: 'Source:src/main.cpp', kind: 'Source', path: 'src/main.cpp' },
      { identity: 'Header:generated.hpp', kind: 'Header', path: 'generated.hpp', generated: true },
      { identity: 'Header:stale.generated.hpp', kind: 'Header', path: 'stale.generated.hpp', generated: true },
      { identity: 'Header:missing.hpp', kind: 'Header', path: 'missing.hpp' }
    );
    const files = await enumerateProjectFiles(root, path.join(root, 'App.nginproj'), value, 5000, true, generatedDirectory);
    const flatten = (items: typeof files): typeof files => items.flatMap(item => [item, ...flatten(item.children ?? [])]);
    const all = flatten(files);
    assert.equal(all.find(item => item.name === 'App.nginproj')?.state, 'authored');
    assert.equal(all.find(item => item.name === 'main.cpp')?.state, 'selected');
    assert.equal(all.find(item => item.name === 'Player.hpp')?.state, 'input');
    assert.equal(all.find(item => item.name === 'unused.cpp')?.state, 'unselected');
    assert.equal(all.find(item => item.name === 'generated.hpp')?.state, 'generated');
    assert.equal(all.find(item => item.name === 'generated.hpp')?.path, path.join(generatedDirectory, 'generated.hpp'));
    assert.equal(all.find(item => item.name === 'action.cpp')?.state, 'generated');
    assert.equal(all.find(item => item.name === 'stale.generated.hpp')?.state, 'missing');
    assert.equal(all.find(item => item.name === 'missing.hpp')?.state, 'missing');
    assert.equal(all.find(item => item.name === 'Nested')?.state, 'boundary');

    const physicalOnly = await enumerateProjectFiles(root, path.join(root, 'App.nginproj'));
    const physicalAll = flatten(physicalOnly);
    assert.equal(physicalAll.find(item => item.name === 'App.nginproj')?.state, 'authored');
    assert.equal(physicalAll.find(item => item.name === 'main.cpp')?.state, 'unselected');
    assert.equal(physicalAll.find(item => item.name === '.env')?.state, 'unselected');
    assert.equal(physicalAll.some(item => item.name === 'missing.hpp'), false);

    const filesView = await enumerateProjectFiles(root, path.join(root, 'App.nginproj'), undefined, 5000, false);
    const filesViewAll = flatten(filesView);
    assert.equal(filesViewAll.find(item => item.name === 'README.md')?.relativePath, 'Nested/README.md');
    assert.equal(filesViewAll.find(item => item.name === 'Nested')?.state, 'unselected');
  } finally {
    await fs.rm(root, { recursive: true, force: true });
  }
});

test('project templates use the direct product model', () => {
  const app = createProjectTemplate('Hello.App', 'Executable');
  assert.match(app.manifest, /<Executable Name="Hello\.App">/);
  assert.doesNotMatch(app.manifest, /<Run/);
  assert.ok(app.files['src/main.cpp']);
  const library = createProjectTemplate('Math', 'Library');
  assert.match(library.manifest, /Kind="Static"/);
  assert.ok(library.files['include/Math/Math.hpp']);
});

test('manifest metadata choices are consumed without editor-side fallbacks', () => {
  const metadata = {
    documents: [{ kind: 'Project', extension: '.nginproj', roots: ['Library'], schema: 'project.xsd' }],
    namespaces: [],
    elements: [{
      id: 'project.library-root', name: 'Library', namespace: '', documentation: '', children: [],
      attributes: [{ name: 'Kind', type: 'enumeration', required: true, values: ['Static', 'Shared'] }]
    }]
  };
  assert.deepEqual(attributeChoices(metadata, 'project.library-root', 'Kind'), ['Static', 'Shared']);
  assert.deepEqual(attributeChoices(metadata, 'project.library-root', 'Version'), []);
  const generated = loadManifestMetadata(path.resolve('schemas', 'manifest-editor-metadata.json'));
  assert.deepEqual(attributeChoices(generated, 'project.library-root', 'Kind'), ['Static', 'Shared', 'Interface', 'Plugin']);
  assert.equal(attributeChoices(generated, 'project.library-root', 'Kind').includes('Module'), false);
});

test('incompatible CLI selection options produce an actionable diagnosis', () => {
  const result = {
    command: 'C:\\Program Files\\NGIN\\ngin.exe',
    args: ['graph', '--workspace', 'NGIN.ngin'], cwd: '.', exitCode: 1, stdout: '',
    stderr: 'error: unknown option: --workspace', diagnostics: []
  };
  assert.equal(unsupportedSelectionOption(result), '--workspace');
  assert.match(describeCliFailure(result), /incompatible with this version of NGIN Tools/u);
  assert.match(describeCliFailure(result), /NGIN: Executable setting/u);
});

test('a failed graph remains stable until an explicit refresh', () => {
  assert.equal(shouldLoadGraph(undefined, undefined), true);
  assert.equal(shouldLoadGraph(graph(), undefined), false);
  assert.equal(shouldLoadGraph(undefined, 'unknown option: --workspace'), false);
});

test('Run and Debug require Run intent', () => {
  const library: ProjectCandidate = {
    manifest: 'Shared.nginproj', directory: '.', name: 'Shared', artifactKind: 'Library', libraryKind: 'Static', hasRun: false
  };
  const application: ProjectCandidate = {
    manifest: 'App.nginproj', directory: '.', name: 'App', artifactKind: 'Executable', hasRun: true
  };
  assert.equal(projectCanRun(library), false);
  assert.equal(projectCanRun(application), true);
  const resolved = graph();
  resolved.runs = [];
  assert.equal(projectCanRun(application, resolved, application.manifest), false);
});
