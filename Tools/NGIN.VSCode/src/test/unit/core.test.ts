import assert from 'node:assert/strict';
import { promises as fs } from 'node:fs';
import { tmpdir } from 'node:os';
import * as path from 'node:path';
import test from 'node:test';
import {
  createBrowseConfiguration,
  createSourceConfiguration,
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
import { projectCanLaunch } from '../../core/projectCapabilities';
import {
  formatLifecycleCommand, formatRuntimeLine, LifecycleOutputPresenter, parseNinjaProgress
} from '../../core/outputPresentation';
import type { CompositionGraph, NginContext, ProjectCandidate } from '../../model';

function graph(): CompositionGraph {
  return {
    kind: 'NGIN.CompositionGraph', state: 'resolved',
    product: { identity: 'App', name: 'App', type: 'Application', languageStandard: 'C++23' },
    selection: {
      identity: 'Selection', configuration: 'Debug', targetOperatingSystem: 'windows',
      targetArchitecture: 'x64', compiler: 'clang', debugSymbols: true
    },
    options: [], packages: [], exports: [], capabilityBindings: [], actions: [], plugins: [], contributions: [],
    buildItems: [
      { identity: 'Include:include', kind: 'IncludeDirectory', path: 'include' },
      { identity: 'Define:APP', kind: 'Define', path: 'APP=1' }
    ],
    launches: [], testing: null, publishes: [], edges: []
  };
}

test('workspace choices and project rules are read without creating a semantic model', () => {
  const source = `<Workspace Name="Demo">
  <Projects><Project Path="App/App.nginproj" /><Project Include="Examples/**/*.nginproj" Exclude="**/Old/**" /></Projects>
  <Configurations><Configuration Name="Debug" /><Configuration Name="Release" /></Configurations>
  <Targets><Target Name="host" OS="host" Architecture="host" /></Targets>
  <Toolchains><Toolchain Name="default" Compiler="default" Linker="default" /></Toolchains>
  <Defaults><Configuration Name="Release" /><Target Name="host" /><Toolchain Name="default" /></Defaults>
  <Presets><Preset Name="dev" Command="build"><Configuration Name="Debug" /></Preset></Presets>
</Workspace>`;
  const choices = parseWorkspaceChoices(source);
  assert.equal(choices.name, 'Demo');
  assert.deepEqual(choices.configurations, ['Debug', 'Release']);
  assert.equal(choices.defaults.configuration, 'Release');
  assert.deepEqual(choices.presets, [{ name: 'dev', command: 'build' }]);
  assert.deepEqual(parseWorkspaceProjectRules(source), [
    { path: 'App/App.nginproj', include: undefined, exclude: undefined },
    { path: undefined, include: 'Examples/**/*.nginproj', exclude: '**/Old/**' }
  ]);
});

test('XML attributes decode entities only after lexical extraction', () => {
  assert.deepEqual(parseAttributes(`Name="A&amp;B" Before='2.0'`), { Name: 'A&B', Before: '2.0' });
});

test('build membership insertion preserves the surrounding XML and ordering', () => {
  const source = `<Project Name="App" Type="Application">\n  <!-- retained -->\n  <Dependencies />\n  <Launch Name="Default" />\n</Project>\n`;
  const edit = insertBuildItem(source, 'Header', 'Include', 'include/App.hpp');
  assert.ok(edit);
  const updated = source.slice(0, edit.start) + edit.text + source.slice(edit.end);
  assert.match(updated, /<Dependencies \/>\n  <Build>\n    <Header Include="include\/App.hpp" \/>\n  <\/Build>\n  <Launch/);
  assert.match(updated, /<!-- retained -->/);
});

test('build membership insertion expands a self-closing Build element', () => {
  const source = `<Project Name="App" Type="Application">\n  <Build />\n</Project>`;
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
  const source = `<Project Name="Old" Type="Application" Version="1.0.0">\n  <!-- keep -->\n  <Build />\n</Project>`;
  const edits = updateProjectAttributes(source, { Name: 'New', Type: 'Library', Version: undefined, Linkage: 'Static' })
    .sort((left, right) => right.start - left.start);
  let updated = source;
  for (const edit of edits) updated = updated.slice(0, edit.start) + edit.text + updated.slice(edit.end);
  assert.match(updated, /<Project Name="New" Type="Library" Linkage="Static">/);
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
    configuration: 'Debug', target: 'host', toolchain: 'default', options: { B: '2', A: '1' }, outputDirectory: output
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
    '--toolchain', 'default', '--option', 'A=1', '--option', 'B=2', '--output', lock
  ]);
  assert.deepEqual(lifecycleArguments('analyze', base).slice(-4), ['--output', output, '--lock', lock]);
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
    configuration: 'Debug', target: 'host', toolchain: 'default', options: {},
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

test('native debug configuration uses staged graph launch intent', () => {
  const root = path.resolve('workspace');
  const context: NginContext = {
    workspaceFolder: root,
    projectManifest: path.join(root, 'App.nginproj'),
    projectName: 'App', configuration: 'Debug', target: 'host', toolchain: 'default', options: {},
    outputDirectory: path.join(root, 'build', 'App')
  };
  const value = graph();
  value.launches = [{
    identity: 'App:Launch:Default', name: 'Default', default: true, executableKind: 'Product', executable: 'App',
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

test('native test debugging uses TestPlan arguments without a Launch definition', () => {
  const root = path.resolve('workspace');
  const context: NginContext = {
    workspaceFolder: root, projectManifest: path.join(root, 'Tests.nginproj'), projectName: 'Tests',
    configuration: 'Debug', target: 'host', toolchain: 'default', options: {}, outputDirectory: path.join(root, 'build')
  };
  const value = graph();
  value.product.type = 'Test';
  value.testing = { identity: 'Tests:Testing', arguments: ['--reporter', 'console'] };
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
    await fs.writeFile(path.join(root, 'App.nginproj'), '<Project Name="App" Type="Application" />');
    await fs.writeFile(path.join(root, 'src', 'main.cpp'), 'int main() {}');
    await fs.writeFile(path.join(root, 'src', 'unused.cpp'), '');
    await fs.writeFile(path.join(root, '.env'), 'MODE=development');
    await fs.writeFile(path.join(root, 'Nested', 'Nested.nginproj'), '<Project Name="Nested" Type="Library" />');
    await fs.writeFile(path.join(root, 'Nested', 'README.md'), '# Nested');
    const generatedDirectory = path.join(root, 'build', 'actions');
    await fs.mkdir(generatedDirectory, { recursive: true });
    await fs.writeFile(path.join(generatedDirectory, 'generated.hpp'), '// generated');
    const value = graph();
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
    assert.equal(all.find(item => item.name === 'unused.cpp')?.state, 'unselected');
    assert.equal(all.find(item => item.name === 'generated.hpp')?.state, 'generated');
    assert.equal(all.find(item => item.name === 'generated.hpp')?.path, path.join(generatedDirectory, 'generated.hpp'));
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
  const app = createProjectTemplate('Hello.App', 'Application');
  assert.match(app.manifest, /<Project Name="Hello\.App" Type="Application">/);
  assert.match(app.manifest, /<Launch Name="default"/);
  assert.ok(app.files['src/main.cpp']);
  const library = createProjectTemplate('Math', 'Library');
  assert.match(library.manifest, /Linkage="Static"/);
  assert.ok(library.files['include/Math/Math.hpp']);
  assert.doesNotMatch(createProjectTemplate('Legacy', 'External').manifest, /<Build>/);
});

test('manifest metadata choices are consumed without editor-side fallbacks', () => {
  const metadata = {
    namespaces: [],
    elements: [{
      id: 'project.root', name: 'Project', namespace: '', documentation: '', children: [],
      attributes: [{ name: 'Type', type: 'enumeration', required: true, values: ['Application', 'Library'] }]
    }]
  };
  assert.deepEqual(attributeChoices(metadata, 'project.root', 'Type'), ['Application', 'Library']);
  assert.deepEqual(attributeChoices(metadata, 'project.root', 'Linkage'), []);
  const generated = loadManifestMetadata(path.resolve('schemas', 'manifest-editor-metadata.json'));
  assert.deepEqual(attributeChoices(generated, 'project.root', 'Linkage'), ['Static', 'Shared', 'Interface']);
  assert.equal(attributeChoices(generated, 'project.root', 'Type').includes('Module'), false);
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

test('Run and Debug require Launch intent', () => {
  const library: ProjectCandidate = {
    manifest: 'Shared.nginproj', directory: '.', name: 'Shared', type: 'Library', hasLaunch: false
  };
  const application: ProjectCandidate = {
    manifest: 'App.nginproj', directory: '.', name: 'App', type: 'Application', hasLaunch: true
  };
  assert.equal(projectCanLaunch(library), false);
  assert.equal(projectCanLaunch(application), true);
  const resolved = graph();
  resolved.launches = [];
  assert.equal(projectCanLaunch(application, resolved, application.manifest), false);
});
